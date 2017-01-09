---
layout: post
title:  "OVS Deep Dive 4: Patch Port"
date:   2017-01-07
---

This post introduces OVS patch port, and compares it with linux veth pair.

## 1. What is OVS `patch port`

An OVS patch port is like a physical cable plugged from
one (OVS) switch port to another. In this sense, it is quite similar to linux veth
pair.

Indeed, in some situations, these two could be used as alternatively.  Such as,
in OpenStack compute node, there are usually two ovs bridges: `br-int` and
`br-eth1`. In the early OpenStack releases (prior to Kilo), the two are connected
by linux veth pair. In newer releases (such as, releases after Liberty),
however, the default connection fashion has been changed to OVS patch port.

<p align="center"><img src="/assets/img/ovs-deep-dive/ovs-compute-node.png" width="80%" height="80%"></p>
<p align="center">Fig.1.1. Network On OpenStack Compute Node[1])</p>

According to some materials[2,3][6][7], the reason of switching from
linux veth pair to OVS patch port is for performance consideration. Apart from
this, at least for OpenStack, the patch port brings another great benefit:
traffic of instances (VMs) will not get down during OVS neutron agent restart -
this is what ***graceful OVS agent restart[4,5]*** achieves in newer
OpenStack releases.

However, there is also a disadvanage of patch port: you could no longer capture
packets on the patch ports using tools such as `tcpdump` - like what you have
been doing on linux veth pair ports.

In this article, we will dig into the source code and get to know why it behaves
this way.

## 2. Data Structures

We need to get familir with following data structures before moving on:

* `dpif` - OVS datapath interface, wraps over `dpif_class`
* `dpif_class` - base `dpif provider` structure
* `netdev_class` - base netdev provider structure
* `vport_class` - a kind of netdev provider (implementation)

### 2.1. `dpif` and `dpif_class`

`dpif` is an abbreviaion of **datapth interface**, and is defined in
`lib/dpif-provider.h`. Each implementation of a datapath interface needs to
define the callbacks in `dpif_class`. The two structures are shown below:

```c
/* Open vSwitch datapath interface.
 *
 * This structure should be treated as opaque by dpif implementations. */
struct dpif {
    const struct dpif_class *dpif_class;
    char *base_name;
    char *full_name;
    uint8_t netflow_engine_type;
    uint8_t netflow_engine_id;
};

/* Datapath interface class structure, to be defined by each implementation of
 * a datapath interface. */
struct dpif_class {
    /* Type of dpif in this class, e.g. "system", "netdev", etc.
     *
     * One of the providers should supply a "system" type, since this is
     * the type assumed if no type is specified when opening a dpif. */
    const char *type;

    int (*init)(void);
    int (*open)(const struct dpif_class *dpif_class, const char *name, bool create, struct dpif **dpifp);

    ...

    bool (*run)(struct dpif *dpif); /* Performs periodic work needed by 'dpif', if any is necessary. */
    void (*wait)(struct dpif *dpif);
    int (*port_add)(struct dpif *dpif, struct netdev *netdev, odp_port_t *port_no);
    int (*port_poll)(const struct dpif *dpif, char **devnamep);
    struct dpif_flow_dump *(*flow_dump_create)(const struct dpif *dpif, bool terse);

    /* Polls for an upcall from 'dpif' for an upcall handler. */
    int (*recv)(struct dpif *dpif, uint32_t handler_id,
                struct dpif_upcall *upcall, struct ofpbuf *buf);
};
```
<p align="center"><img src="/assets/img/ovs-deep-dive/dpif_providers.png" width="75%" height="75%"></p>
<p align="center">Fig.2.1 dpif providers</p>

**All types of `dpif` classes:**

* `system`
  - default `dpif` type
* `netdev`
  - `dpif_netdev_class`
  - implemented in `lib/dpif-netdev.c`
* `netlink`
  - `dpif_netlink_class`
  - implemented in `lib/dpif-netlink.c`

calls `dpif_init()` to init the classes.

### 2.2 `netdev`

`netdev_class` is a general abstraction of all network devices, and
it is defined in `lib/netdev-provider.h`. Any network device type (called
**netdev provider**) has to
implement the methods in `netdev_class` before being used:

```c
struct netdev_class {
    const char *type; /* Type of netdevs in this class, e.g. "system", "tap", "gre", etc. */
    bool is_pmd;      /* If 'true' then this netdev should be polled by PMD threads. */

    /* ## Top-Level Functions ## */
    int (*init)(void);
    void (*run)(const struct netdev_class *netdev_class);
    void (*wait)(const struct netdev_class *netdev_class);

    /* ## netdev Functions ## */
    int (*construct)(struct netdev *);
    void (*destruct)(struct netdev *);

    ...

    int (*rxq_recv)(struct netdev_rxq *rx, struct dp_packet_batch *batch);
    void (*rxq_wait)(struct netdev_rxq *rx);
};
```

<p align="center"><img src="/assets/img/ovs-deep-dive/netdev_providers.png" width="75%" height="75%"></p>
<p align="center">Fig.2.2. netdev providers</p>

Fig.2.2 depicts the `netdev` and `netdev` providers in OVS. The detailed
`netdev` provider types are listed below:

**All types of `netdev` classes**:

* (***built in?***) netdev
  - `netdev_linux_class`
  - `netdev_tap_class`
  - `netdev_bsd_class`
  - `netdev_windows_class`
  - `netdev_internal_class`
* dummy netdev
  - `dummy_class`
  - `dummy_internal_class`
  - `dummy_pmd_class`
* vport netdev
  - tunnel class: `geneve`, `gre`, `vxlan`, `lisp`, `stt`
  - `patch_class`
* dpdk netdev
  - `dpdk_class`
  - `dpdk_ring_class`
  - `dpdk_vhost_class`
  - `dpdk_vhost_client_class`

### 2.3 `vport`

In this section, let's look into one specific `netdev provider`: `vport`.
`vport_class` is defined in `lib/netdev-vport.c`, thus its an internal
structure used only in this file.

```c
/* A port within a datapath.
 *
 * 'name' and 'type' are suitable for passing to netdev_open(). */
struct dpif_port {
    char *name;                 /* Network device name, e.g. "eth0". */
    char *type;                 /* Network device type, e.g. "system". */
    odp_port_t port_no;         /* Port number within datapath. */
};

struct vport_class {
    const char *dpif_port;
    struct netdev_class netdev_class;
};
```

From the definition we could see, it is a
simple wrapper on `netdev_class` (base netdev provider structure), plus a
`dpif_port` (a port within a datapath).

**All vport classes**:

* tunnel vports: `geneve`, `gre`, `vxlan`, `lisp`, `stt`
* `patch` (patch port)

The registration of the tunnel vports is in `netdev_vport_tunnel_register()`;
and `patch` port in `netdev_vport_patch_register()`. Both of them will then
call `netdev_register_provider()`.

```c
void
netdev_vport_tunnel_register(void)
{
    /* The name of the dpif_port should be short enough to accomodate adding
     * a port number to the end if one is necessary. */
    static const struct vport_class vport_classes[] = {
        TUNNEL_CLASS("geneve", "genev_sys", netdev_geneve_build_header,
                                            netdev_tnl_push_udp_header,
                                            netdev_geneve_pop_header),
        TUNNEL_CLASS("gre", "gre_sys", netdev_gre_build_header,
                                       netdev_gre_push_header,
                                       netdev_gre_pop_header),
        TUNNEL_CLASS("vxlan", "vxlan_sys", netdev_vxlan_build_header,
                                           netdev_tnl_push_udp_header,
                                           netdev_vxlan_pop_header),
        TUNNEL_CLASS("lisp", "lisp_sys", NULL, NULL, NULL),
        TUNNEL_CLASS("stt", "stt_sys", NULL, NULL, NULL),
    };

    for (i = 0; i < ARRAY_SIZE(vport_classes); i++)
        netdev_register_provider(&vport_classes[i].netdev_class);

    unixctl_command_register("tnl/egress_port_range", "min max", 0, 2)
}

void
netdev_vport_patch_register(void)
{
    static const struct vport_class patch_class =
        { NULL,
            { "patch", false,
              VPORT_FUNCTIONS(get_patch_config,
                              set_patch_config,
                              NULL,
                              NULL, NULL, NULL, NULL) }};
    netdev_register_provider(&patch_class.netdev_class);
}
```

## 3. Patch Port

Patch Port as a kind of `netdev` is register by calling
`netdev_register_provider(const struct netdev_class *new_class)`

The routine initializes and registers a new netdev provider.  After successful
registration, new netdevs of that type can be opened using `netdev_open()`.

`patch` port accepts exactly one parameter: `peer` - the other side of the
connection. This is much like linux **veth pair**.

### 3.1 How Patch Port Works

First, notice that `patch port` does not implement the `rx_recv()` method
(default is `NULL`) of
`netdev_class` - this means, `patch port` will not receives packets from
physical devices. Actually, a patch port only receives packets from other ports
of ovs bridge (`ofproto`).

Then, let's see when a patch-port's `rx` counter gets updated:

```shell
$ ./grep.sh netdev_vport_inc_rx *
lib/netdev-vport.h:void netdev_vport_inc_rx(const struct netdev *, lib/netdev-vport.c:netdev_vport_inc_rx(const struct netdev *netdev,
ofproto/ofproto-dpif-xlate-cache.c:        netdev_vport_inc_rx(entry->dev.rx, stats);
ofproto/ofproto-dpif-xlate.c:            netdev_vport_inc_rx(peer->netdev, ctx->xin->resubmit_stats);
ofproto/ofproto-dpif-xlate.c:            netdev_vport_inc_rx(in_port->netdev, ctx.xin->resubmit_stats);
```

Looking into `ofproto-dpif-xlate.c`, we could figure out that the rx
counter get updated when there is a `OUPUT` action that forwards a packet from
one OVS port to the patch port, and tx counter updated when a packet is `OUPUT`
from patch port to the peer or other ports.

### 3.2 Why Packets Not Captured On Patch Ports

### 3.3 Patch Port Performance

As has been pointed out in [2][3][6][7], there is great performance boost in OpenStack
compute node when replacing linux veth pair with OVS patch port for connections
between `br-int` and `br-phy`. Then, the question is: where the performance
increase comes from?

I could only figure out the calling stack of `patch port`, but not familir
with veth pair.

Here are some explanations from [5]: it "saves an extra lookup in the kernel
datapath and an extra trip to userspace to figure out what happens in the second
bridge".


**I'll come back and update this when i get more info in the later.**

## References
1. [doc: Open vSwitch L2 Agent](http://docs.openstack.org/developer/neutron/devref/openvswitch_agent.html)
2. [disuss: OVS performance with Openstack Neutron](https://mail.openvswitch.org/pipermail/ovs-discuss/2013-December/032222.html)
3. [datasheet: Kilo vs Liberty - OVS Agent restart outage](https://docs.google.com/spreadsheets/d/1ZGra_MszBlL0fNsFqd4nOvh1PsgWu58-GxEeh1m1BPw/edit#gid=1328396036)
4. [patch: Graceful OVS Agent Restart](https://bugs.launchpad.net/openstack-manuals/+bug/1487250)
5. [discuss: Restarting neutron openvswitch agent causes network hiccup by throwing away all flows](https://bugs.launchpad.net/neutron/+bug/1383674)
6. [Switching Performance - Chaining OVS bridges](http://www.opencloudblog.com/?p=386)
7. [Switching Performance – Connecting Linux Network Namespaces](http://www.opencloudblog.com/?p=96)
