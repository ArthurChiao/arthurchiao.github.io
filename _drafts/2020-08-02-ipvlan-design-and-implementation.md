---
layout    : post
title     : "IPVLAN: Design and Implementation"
date      : 2020-08-02
lastupdate: 2020-08-02
categories: ipvlan kernel
---

* TOC
{:toc}

----

This post explores the IPVLAN implementation inside Linux kernel.

Code based on kernel `4.19`.

# 1 Introduction

There are many types of virtual network devices that could be used to interconnect
a virtual instance (e.g. a Docker container or a VM) and the host it resides on.
Among them, the most oftenly used may be veth pair. IPVLAN is such an
alternative to veth pair, which provides better performance.

An example of IPVLAN usage (taken from `Documentation/networking/ipvlan.txt`) [1]:

```
  +=============================================================+
  |  Host: host1                                                |
  |                                                             |
  |   +----------------------+      +----------------------+    |
  |   |   NS:ns0             |      |  NS:ns1              |    |
  |   |                      |      |                      |    |
  |   |        ipvl0         |      |         ipvl1        |    |
  |   +----------#-----------+      +-----------#----------+    |
  |              #                              #               |
  |              ################################               |
  |                              # eth0                         |
  +==============================#==============================+
```

steps to create the above network topology:

```shell
(a) Create two network namespaces - ns0, ns1
    ip netns add ns0
    ip netns add ns1

(b) Create two ipvlan slaves on eth0 (master device)
    ip link add link eth0 ipvl0 type ipvlan mode l2
    ip link add link eth0 ipvl1 type ipvlan mode l2

(c) Assign slaves to the respective network namespaces
    ip link set dev ipvl0 netns ns0
    ip link set dev ipvl1 netns ns1

(d) Now switch to the namespace (ns0 or ns1) to configure the slave devices
    - For ns0
        (1) ip netns exec ns0 bash
        (2) ip link set dev ipvl0 up
        (3) ip link set dev lo up
        (4) ip -4 addr add 127.0.0.1 dev lo
        (5) ip -4 addr add $IPADDR dev ipvl0
        (6) ip -4 route add default via $ROUTER dev ipvl0
    - For ns1
        (1) ip netns exec ns1 bash
        (2) ip link set dev ipvl1 up
        (3) ip link set dev lo up
        (4) ip -4 addr add 127.0.0.1 dev lo
        (5) ip -4 addr add $IPADDR dev ipvl1
        (6) ip -4 route add default via $ROUTER dev ipvl1
```

# 2 Design (TODO)

# 3 Implementation

To have a better understanding of the general data flow, we will intentionally
omit lots of coding details and error handling logics, only keep the core logic.

## 3.1 Init

### 3.1.1 `ipvlan_init`: init module

Register necessary callbacks for `ipvlan` type network devices.

In `drivers/net/ipvlan/ipvlan_main.c`:

```c
static struct notifier_block ipvlan_notifier_block = {
    .notifier_call = ipvlan_device_event,    // -> register netfilter hooks
};

static struct rtnl_link_ops ipvlan_link_ops = {
    .kind       = "ipvlan",
    .setup      = ipvlan_link_setup,
    .newlink    = ipvlan_link_new,
    .dellink    = ipvlan_link_delete,
};

static int __init ipvlan_init_module(void)
{
    register_netdevice_notifier(&ipvlan_notifier_block); // register netfilter handlers
    ipvlan_link_register(&ipvlan_link_ops);              // register RX/TX handlers
}

module_init(ipvlan_init_module);
```

### 3.2.2 `ipvlan_link_setup`: register TX handler

When setting up a IPVLAN device, `ipvlan_link_setup` method will be called. It
will further register the IPVLAN device's:

1. init/uninit handlers
2. transmit (TX) handlers
3. `ethtool` handlers, so IPVLAN could respond to `ethtool` commands
3. other misc handlers

```c
static const struct net_device_ops ipvlan_netdev_ops = {
    .ndo_init        = ipvlan_init,
    .ndo_uninit        = ipvlan_uninit,
    .ndo_open        = ipvlan_open,
    .ndo_start_xmit        = ipvlan_start_xmit,
    .ndo_set_rx_mode    = ipvlan_set_multicast_mac_filter,
    .ndo_vlan_rx_add_vid    = ipvlan_vlan_rx_add_vid,
    ...
};

void
ipvlan_link_setup(struct net_device *dev)
{
    ether_setup(dev);

    dev->priv_flags |= IFF_UNICAST_FLT | IFF_NO_QUEUE;
    dev->netdev_ops = &ipvlan_netdev_ops;
    dev->ethtool_ops = &ipvlan_ethtool_ops;
    ...
}
```

### 3.2.3 `ipvlan_init`: register RX handler

For IPVLAN devices, device init handler (`.ndo_init`) is implementation as `ipvlan_init`.
`ipvlan_init` will register a handler for receiving ether frames destinated for
this device:

```
ipvlan_netdev_ops.ndo_init
                   |-ipvlan_init
                       |-ipvlan_port_create
                          |-netdev_rx_handler_register(dev, ipvlan_handle_frame, port)
```

implementations:

```c
static int
ipvlan_init(struct net_device *dev)
{
    struct ipvl_dev *ipvlan = netdev_priv(dev);
    struct net_device *phy_dev = ipvlan->phy_dev;
    struct ipvl_port *port;

    dev->state = (dev->state & ~IPVLAN_STATE_MASK) |
             (phy_dev->state & IPVLAN_STATE_MASK);
    dev->features = phy_dev->features & IPVLAN_FEATURES;
    dev->features |= NETIF_F_LLTX | NETIF_F_VLAN_CHALLENGED;

    netdev_lockdep_set_classes(dev);

    if (!netif_is_ipvlan_port(phy_dev)) {
        ipvlan_port_create(phy_dev);
    }
    port = ipvlan_port_get_rtnl(phy_dev);
    port->count += 1;
}

static int
ipvlan_port_create(struct net_device *dev)
{
    struct ipvl_port *port;

    port = kzalloc(sizeof(struct ipvl_port), GFP_KERNEL);

    write_pnet(&port->pnet, dev_net(dev));
    port->dev = dev;
    port->mode = IPVLAN_MODE_L3;
    INIT_LIST_HEAD(&port->ipvlans);
    for (idx = 0; idx < IPVLAN_HASH_SIZE; idx++)
        INIT_HLIST_HEAD(&port->hlhead[idx]);

    skb_queue_head_init(&port->backlog);
    ida_init(&port->ida);
    port->dev_id_start = 1;

    netdev_rx_handler_register(dev, ipvlan_handle_frame, port);
}
```

### 3.2.4 `ipvlan_link_init`: add a new IPVLAN device

```c
int
ipvlan_link_new(struct net *src_net, struct net_device *dev, struct nlattr *tb[],
    struct nlattr *data[], struct netlink_ext_ack *extack)
{
    struct ipvl_dev *ipvlan = netdev_priv(dev);
    u16 mode = IPVLAN_MODE_L3;

    phy_dev = __dev_get_by_index(src_net, ...);
    ipvlan->phy_dev = phy_dev;
    ipvlan->dev = dev;

    memcpy(dev->dev_addr, phy_dev->dev_addr, ETH_ALEN);
    dev->priv_flags |= IFF_NO_RX_HANDLER;
    register_netdevice(dev);

    /* ipvlan_init() would have created the port, if required */
    port = ipvlan_port_get_rtnl(phy_dev);
    ipvlan->port = port;

    /* Since L2 address is shared among all IPvlan slaves including
     * master, use unique 16 bit dev-ids to diffentiate among them.
     * Assign IDs between 0x1 and 0xFFFE (used by the master) to each slave link */
    err = ida_simple_get(&port->ida, port->dev_id_start, 0xFFFE, GFP_KERNEL);
    dev->dev_id = err;

    port->dev_id_start = err + 1; /* for the future assignment */

    netdev_upper_dev_link(phy_dev, dev, extack);

    if (data && data[IFLA_IPVLAN_FLAGS])
        port->flags = nla_get_u16(data[IFLA_IPVLAN_FLAGS]);

    if (data && data[IFLA_IPVLAN_MODE])
        mode = nla_get_u16(data[IFLA_IPVLAN_MODE]);

    ipvlan_set_port_mode(port, mode);

    list_add_tail_rcu(&ipvlan->pnode, &port->ipvlans);
    netif_stacked_transfer_operstate(phy_dev, dev);
    return 0;
}
```

### 3.2.5 `ipvlan_device_event`: register netfilter handlers

Netfilter is a **packet filtering framework** inside kernel [2,3]. With
netfilter hooks, userspace tools (e.g. `iptables`, `tcpdump`) could be used to
filter packets with specified matching rules.

For IPVLAN devices to be able used by such tools, the driver needs
to implement netfilter hooks. Let's see two of them:

1. L2 mode: `ipvlan_nf_input`
1. L3 mode: `ipvlan_l3_rcv`

```
ipvlan_init_module
  |
  | // register L2 mode nf hook
  |-register_netdevice_notifier(&ipvlan_notifier_block)
  |                              /
  |     /-----------------------/
  |     /
  |     |-ipvlan_device_event
  |       |-ipvlan_register_nf_hook
  |          |-.hook = ipvlan_nf_input
  |
  | // register L2 mode nf hook
  |-register_netdevice_notifier(&ipvlan_notifier_block)
      |-ipvlan_nl_changelink
          |-ipvlan_set_port_mode
              |-l3mdev_ops = &ipvl_l3mdev_ops
```

#### L2 mode: `ipvlan_nf_input`

```c
static const struct nf_hook_ops ipvl_nfops[] = {
    {
        .hook     = ipvlan_nf_input,
        .hooknum  = NF_INET_LOCAL_IN,
    },
};

static int
ipvlan_register_nf_hook(struct net *net)
{
    struct ipvlan_netns *vnet = net_generic(net, ipvlan_netid);

    if (!vnet->ipvl_nf_hook_refcnt) {
        err = nf_register_net_hooks(net, ipvl_nfops, ARRAY_SIZE(ipvl_nfops));
        if (!err)
            vnet->ipvl_nf_hook_refcnt = 1;
    } else {
        vnet->ipvl_nf_hook_refcnt++;
    }
}
```

#### L3 mode: `ipvlan_l3_rcv`

```c
static const struct l3mdev_ops ipvl_l3mdev_ops = {
    .l3mdev_l3_rcv = ipvlan_l3_rcv,
};

static int
ipvlan_set_port_mode(struct ipvl_port *port, u16 nval)
{
    if (port->mode != nval) {
        if (nval == IPVLAN_MODE_L3S) { /* New mode is L3S */
            ipvlan_register_nf_hook(read_pnet(&port->pnet));
            mdev->l3mdev_ops = &ipvl_l3mdev_ops;
            mdev->priv_flags |= IFF_L3MDEV_MASTER;
        } else if (port->mode == IPVLAN_MODE_L3S) { /* Old mode was L3S */
            mdev->priv_flags &= ~IFF_L3MDEV_MASTER;
            ipvlan_unregister_nf_hook(read_pnet(&port->pnet));
            mdev->l3mdev_ops = NULL;
        }
        port->mode = nval;
    }
}
```

## 3.2 TX call stack

Let's see how a packet is transmitted on a IPVLAN device.

`drivers/net/ipvlan/ipvlan_main.c`:

```c
ipvlan_netdev_ops.ndo_start_xmit
                   /
   /--------------/
  /
 /-ipvlan_start_xmit
      |-ipvlan_queue_xmit                               //    drivers/net/ipvlan/ipvlan_core.c
         |-switch mode
           case l2:
             ipvlan_xmit_mode_l2
               |-return ipvlan_rcv_frame                //    drivers/net/ipvlan/ipvlan_core.c
               |-return dev_queue_xmit                  // -> net/core/dev.c
           case l3:
           case l3s:
             ipvlan_xmit_mode_l3
               |-return ipvlan_rcv_frame                //    drivers/net/ipvlan/ipvlan_core.c
               |-return ipvlan_process_outbound
                          |-ipvlan_process_v4_outbound
                              |-ip_local_out            // -> net/ipv4/ip_output.c
```

Sum up of the above logic:

1. L2 mode: perform **forwarding** by passing frame to
    * `ipvlan_rcv_frame`: destination is master device or another slave
    * `dev_queue_xmit`: destination is outside, transmit via NIC
2. L3 mode:
    * `ipvlan_rcv_frame`: destination is master device or another slave
    * `ip_local_out`: perform **routing**

### 3.2.1 L2 mode: `ipvlan_xmit_mode_l2`

processing logic:

1. processing **device-local forwarding**: destination is main device or
   another slave device (both having `src_mac == dst_mac`)
2. processing multicast frame
3. processing **normal frame**, which is, `unicast && src_mac != dst_mac`: put
   to physical (master) device's transmit queue, namely send the frame via NIC

```c
static int
ipvlan_xmit_mode_l2(struct sk_buff *skb, struct net_device *dev)
{
    const struct ipvl_dev *ipvlan = netdev_priv(dev);
    struct ethhdr *eth = eth_hdr(skb);

    // if not in VEPA mode, and SRC_MAC == DST_MAC, target may be
    //    1. another slave on the same master device
    //    2. master device itself
    if (!is_vepa && ether_addr_equal(eth->h_dest, eth->h_source)) {
        void *l3hdr = ipvlan_get_L3_hdr(ipvlan->port, skb);
        if (l3hdr) {
            struct ipvl_addr *addr = ipvlan_addr_lookup(ipvlan->port, l3hdr);
            if (addr) {
                if (ipvlan_is_private(ipvlan->port)) {
                    consume_skb(skb);
                    return NET_XMIT_DROP;
                }

                return ipvlan_rcv_frame(addr, &skb, true); // forward to another slave
            }
        }

        return dev_forward_skb(ipvlan->phy_dev, skb);      // forward to master device
    }

    if (is_multicast(eth->h_dest)) {
        ipvlan_skb_crossing_ns(skb, NULL);
        ipvlan_multicast_enqueue(ipvlan->port, skb, true);
        return NET_XMIT_SUCCESS;
    }

    skb->dev = ipvlan->phy_dev;
    return dev_queue_xmit(skb);
}
```

#### VEPA: Virtual Ethernet Port Aggregator.

From https://www.networkworld.com/article/2197460/vepa--an-answer-to-virtual-switching.html:

> Virtual Ethernet Port Aggregator (VEPA) moves switching out of the server back
> to the physical network and makes all virtual machine traffic visible to the
> external network switch, freeing up server resources to support virtual
> machines.
>
> ...
>
> VEPA is proposed as a promising alternative to the virtual switch; both in the
> standardization track, as well as by a broad set of industry vendors.
> 
> A VEPA in effect takes all the traffic generated from virtual machines on a
> server and moves it out to the external network switch. The external network
> switch in turn provides connectivity between the virtual machines on the same
> physical server as well as to the rest of the infrastructure.
>
> ...
>
> This behavior is also being standardized as part of the IEEE working group
> 802.1Qbg. A VEPA can be implemented on the server either in software as a thin
> layer in the hypervisor, or can be implemented in hardware in NIC cards, in
> which case it can be used in conjunction with PCIe I/O virtualization
> technologies such as SR-IOV. An example of a software based VEPA implementation
> is available in the Linux KVM hypervisor.

From `Documentation/networking/ipvlan.txt`:

> 5.3 vepa:
>
> If this is added to the command-line, the port is set in VEPA mode.
> i.e. port will offload switching functionality to the external entity as
> described in 802.1Qbg
>
> Note: VEPA mode in IPvlan has limitations. IPvlan uses the mac-address of the
> master-device, so the packets which are emitted in this mode for the adjacent
> neighbor will have source and destination mac same. This will make the switch /
> router send the redirect message.


### 3.2.2 L3 Mode: `ipvlan_xmit_mode_l3`

```c
static int
ipvlan_xmit_mode_l3(struct sk_buff *skb, struct net_device *dev)
{
    const struct ipvl_dev *ipvlan = netdev_priv(dev);

    void *l3hdr = ipvlan_get_L3_hdr(ipvlan->port, skb, &addr_type);
    if (!l3hdr)
        goto out;

    if (!is_vepa) {
        struct ipvl_addr *addr = ipvlan_addr_lookup(ipvlan->port, l3hdr);
        if (addr) {
            if (ipvlan_is_private(ipvlan->port)) {
                consume_skb(skb);
                return NET_XMIT_DROP;
            }

            return ipvlan_rcv_frame(addr, &skb, true);
        }
    }
out:
    ipvlan_skb_crossing_ns(skb, ipvlan->phy_dev);
    return ipvlan_process_outbound(skb);
}

static int
ipvlan_process_outbound(struct sk_buff *skb)
{
    struct ethhdr *ethh = eth_hdr(skb);

    /* The ipvlan is a pseudo-L2 device, so the packets that we receive
     * will have L2; which need to discarded and processed further
     * in the net-ns of the main-device.  */
    if (skb_mac_header_was_set(skb)) {
        skb_pull(skb, sizeof(*ethh));
        skb->mac_header = (typeof(skb->mac_header))~0U;
        skb_reset_network_header(skb);
    }

    ipvlan_process_v4_outbound(skb);
}

static int
ipvlan_process_v4_outbound(struct sk_buff *skb)
{
    const struct iphdr *ip4h = ip_hdr(skb);
    struct net_device *dev = skb->dev;
    struct net *net = dev_net(dev);

    struct flowi4 fl4 = {
        .flowi4_oif = dev->ifindex,
        .daddr = ip4h->daddr,
        .saddr = ip4h->saddr,
    };

    struct rtable *rt = ip_route_output_flow(net, &fl4, NULL);

    skb_dst_set(skb, &rt->dst);
    ip_local_out(net, skb->sk, skb); // net/ipv4/ip_output.c
}
```

## 3.3 RX call stack

Let's see how a packet is received.

```
ipvlan_handle_frame                            //    ipvlan_core.c
  |-switch mode
    case L2:
      ipvlan_handle_mode_l2
        |-return ipvlan_multicast_enqueue
        |-return ipvlan_handle_mode_l3
    case L3:
    case L3S:
      ipvlan_handle_mode_l3
        |-ipvlan_rcv_frame
            |-dev_forward_skb                   // -> net/core/dev.c
```

### 3.3.1 L2 Mode RX

```c
static rx_handler_result_t
ipvlan_handle_mode_l2(struct sk_buff **pskb, struct ipvl_port *port)
{
    struct sk_buff *skb = *pskb;
    struct ethhdr *eth = eth_hdr(skb);
    rx_handler_result_t ret = RX_HANDLER_PASS;

    if (is_multicast_ether_addr(eth->h_dest)) {
        if (ipvlan_external_frame(skb, port)) {
            struct sk_buff *nskb = skb_clone(skb, GFP_ATOMIC);

            /* External frames are queued for device local
             * distribution, but a copy is given to master
             * straight away to avoid sending duplicates later
             * when work-queue processes this frame. This is
             * achieved by returning RX_HANDLER_PASS.  */
            if (nskb) {
                ipvlan_skb_crossing_ns(nskb, NULL);
                ipvlan_multicast_enqueue(port, nskb, false);
            }
        }
    } else {
        /* Perform like l3 mode for non-multicast packet */
        ret = ipvlan_handle_mode_l3(pskb, port);
    }

    return ret;
}
```

### 3.3.2 L3 Mode RX

```c
static rx_handler_result_t
ipvlan_handle_mode_l3(struct sk_buff **pskb, struct ipvl_port *port)
{
    void *l3hdr;
    int addr_type;
    struct ipvl_addr *addr;
    struct sk_buff *skb = *pskb;
    rx_handler_result_t ret = RX_HANDLER_PASS;

    l3hdr = ipvlan_get_L3_hdr(port, skb, &addr_type);
    if (!l3hdr)
        goto out;

    addr = ipvlan_addr_lookup(port, l3hdr, addr_type, true);
    if (addr)
        ret = ipvlan_rcv_frame(addr, pskb, false);

out:
    return ret;
}
```

```c
static int
ipvlan_rcv_frame(struct ipvl_addr *addr, struct sk_buff **pskb, bool local)
{
    struct ipvl_dev *ipvlan = addr->master;
    struct net_device *dev = ipvlan->dev;
    unsigned int len;
    rx_handler_result_t ret = RX_HANDLER_CONSUMED;
    bool success = false;
    struct sk_buff *skb = *pskb;

    len = skb->len + ETH_HLEN;
    /* Only packets exchanged between two local slaves need to have
     * device-up check as well as skb-share check.  */
    if (local) {
        if (un!(dev->flags & IFF_UP))) {
            kfree_skb(skb);
            goto out;
        }

        skb = skb_share_check(skb, GFP_ATOMIC);
        if (!skb)
            goto out;

        *pskb = skb;
    }

    if (local) {
        skb->pkt_type = PACKET_HOST;
        if (dev_forward_skb(ipvlan->dev, skb) == NET_RX_SUCCESS) // -> net/core/dev.c
            success = true;
    } else {
        skb->dev = dev;
        ret = RX_HANDLER_ANOTHER;
        success = true;
    }

out:
    return ret;
}
```

## 3.4 Netfilter hook call stack (TODO)

# References

1. [ipvlan: Initial check-in of the IPVLAN driver](https://lwn.net/Articles/620087/), lwn.net
2. [A Deep Dive into Iptables and Netfilter Architecture](https://www.digitalocean.com/community/tutorials/a-deep-dive-into-iptables-and-netfilter-architecture)
3. [Cracking kubernetes node proxy (aka kube-proxy)]({% link _posts/2019-11-30-cracking-k8s-node-proxy.md %})
