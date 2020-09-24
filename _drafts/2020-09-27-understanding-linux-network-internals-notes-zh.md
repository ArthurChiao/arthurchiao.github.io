---
layout    : post
title     : "[笔记] Understanding Linux Network Internals (O'Reily, 2005)"
date      : 2020-09-27
lastupdate: 2020-09-27
categories: awk
---

### 编者按

本文是阅读 ***Understanding Linux Network Internals*** 一书时所做的笔记。

这本书引用的内核版本已经很老（`2.x`），因此在阅读时只参考了其内容主线，具体数据
结构和代码对照的是 `4.19`。

本文内容仅供学习交流，如有侵权立即删除。

----

* TOC
{:toc}

# 1 Intro

## 1.2 Common Coding Patterns

### Vector Definitions

在结构体最后留一个 size 为零的数组，

```c
#include <stdio.h>
#include <stdlib.h>

int main() {
    struct A {
        int age;
        char placeholder[0];
    };
    printf("sizeof(A): %lu\n", sizeof(struct A));

    struct A *a = (struct A *)malloc(sizeof(struct A));
    if (a) {
        printf("a: %p, a->placeholder: %p\n", a, a->placeholder);
    }

    return 0;
}
```

当需要存放自定义数据时，就为 `placeholder` 分配内存；否则就不分配，此时
`placeholder` 就只是一个**指向结构体末尾的指针**，**不占用任何空间**：

```shell
$ clang main.c

$ ./a.out
sizeof(A): 4
a: 0x8f16b0, a->placeholder: 0x8f16b4
```

### Measuring Time

The passing of time in kernel space is measured in ticks.

一秒之内的 tick 数就是 HZ。

每经过一个 `tick`，`jiffies` 加 1。因此 `jiffies` 是从系统启动以来的累计 tick 数。

## 1.4 When a Feature Is Offered as a Patch

### Stateful NAT vs. Stateless NAT

2.6 之前内核 routing 模块支持 stateless NAT。

2.6 之后，大家认为 firewall 模块中的 stateful NAT更加灵活，因此从内核中移除了之
前的 stateless NAT（虽然后者更快、占用内存更少）。

> Note that a new module could be written for Netfilter at any time to provide
> stateless NAT support if necessary.

# 2 核心数据结构（Critical Data Structures）

两个核心数据结构：

1. `struct sk_buff`：数据包的内核表示。
1. `struct net_device`：网络设备的内核表示。详见第八章。
1. `struct sock`：socket 的内核表示，非常重要，但本书不涉及。

## 2.1 数据包的内核表示: `struct sk_buff`

```c
// include/linux/skbuff.h

// struct sk_buff - socket buffer
struct sk_buff {
    union {
        struct {
            struct sk_buff        *next;   // Next buffer in list
            struct sk_buff        *prev;   // Previous buffer in list
            union {
                struct net_device *dev;    // Device we arrived on/are leaving by
                unsigned long      dev_scratch;
            };
        };
        struct rb_node             rbnode; // used in netem, ip4 defrag, and tcp stack
        struct list_head           list;
    };

    union {
        struct sock    *sk;        // Socket we are owned by
        int            ip_defrag_offset;
    };
    union {
        ktime_t        tstamp;     // Time we arrived/left
        u64            skb_mstamp; // Used by TCP protocol
    };

    char cb[48];                   // control buffer. free to use for every layer.

    union {
        struct {
            unsigned long    _skb_refdst; // destination entry (with norefcount bit)
            void            (*destructor)(struct sk_buff *skb);  // Destruct function
        };
        struct list_head    tcp_tsorted_anchor; // list structure for TCP (tp->tsorted_sent_queue)
    };

    struct   sec_path      *sp;        // the security path, used for xfrm
    unsigned long           _nfct;     // conntrack: associated connection, if any (with nfctinfo bits)
    struct nf_bridge_info   *nf_bridge;// Saved data about a bridged frame - see br_netfilter.c
    unsigned int            len,       // Length of actual data
                            data_len;  // Data length
    __u16                   mac_len,   // Length of link layer header
                            hdr_len;   // writable header length of cloned skb

    __u16       queue_mapping;// Queue mapping for multiqueue devices

    __u8        __cloned_offset[0];
    __u8        cloned:1,     // Head may be cloned (check refcnt to be sure)
                nohdr:1,      // Payload reference only, must not modify header
                fclone:2,     // skbuff clone status
                peeked:1,     // this pkt has been seen already, so don't stats them again
                head_frag:1,
                xmit_more:1,  // More SKBs are pending for this queue
                pfmemalloc:1; // skbuff was allocated from PFMEMALLOC reserves

    __u32       headers_start[0];

    __u8        __pkt_type_offset[0];
    __u8        pkt_type:3;  // Packet class
    __u8        ignore_df:1; // allow local fragmentation
    __u8        nf_trace:1;  // netfilter packet trace flag
    __u8        ip_summed:2; // Driver fed us an IP checksum

    __u8        ooo_okay:1;  // allow the mapping of a socket to a queue to be changed

    __u8        l4_hash:1;   // indicate hash is a canonical 4-tuple hash over transport ports.
    __u8        sw_hash:1;   // indicates hash was computed in software stack
    __u8        no_fcs:1;    // Request NIC to treat last 4 bytes as Ethernet FCS

    // Indicates the inner headers are valid in the skbuff.
    __u8        encapsulation:1;
    __u8        encap_hdr_csum:1;
    __u8        csum_valid:1;

    __u8        csum_complete_sw:1;
    __u8        csum_level:2;
    __u8        csum_not_inet:1;
    __u8        dst_pending_confirm:1; // need to confirm neighbour
    __u8        ipvs_property:1; // skbuff is owned by ipvs

    __u8        inner_protocol_type:1;
    __u8        remcsum_offload:1;
    __u8        offload_fwd_mark:1;
    __u8        offload_mr_fwd_mark:1;

    __u8        tc_skip_classify:1;// do not classify packet. set by IFB device
    __u8        tc_at_ingress:1;   // used within tc_classify to distinguish in/egress
    __u8        tc_redirected:1;   // packet was redirected by a tc action
    __u8        tc_from_ingress:1; // if tc_redirected, tc_at_ingress at time of redirect
    __u8        decrypted:1;       // TLS: Decrypted SKB
    __u16       tc_index;          // traffic control index

    union {
        __wsum  csum;
        struct  { __u16 csum_start; __u16 csum_offset; };
    };
    __u32       priority; // Packet queueing priority
    int         skb_iif;  // ifindex of device we arrived on
    __u32       hash;     // the packet hash
    __be16      vlan_proto;
    __u16       vlan_tci;
    union {
        unsigned int    napi_id;    // id of the NAPI struct this skb came from
        unsigned int    sender_cpu;
    };
    __u32       secmark;  // security marking

    union {
        __u32   mark; // Generic packet mark
        __u32   reserved_tailroom;
    };

    union {
        __be16  inner_protocol; // Protocol (encapsulation)
        __u8    inner_ipproto;
    };

    __u16       inner_transport_header; // Inner transport layer header (encapsulation)
    __u16       inner_network_header;   // Network layer header (encapsulation)
    __u16       inner_mac_header;       // Link layer header (encapsulation)

    __be16      protocol;         // Packet protocol from driver
    __u16       transport_header; // Transport layer header
    __u16       network_header;   // Network layer header
    __u16       mac_header;       // Link layer header
    __u32       headers_end[0];

    /* These elements must be at the end, see alloc_skb() for details.  */
    sk_buff_data_t    tail;     // Tail pointer
    sk_buff_data_t    end;      // End pointer
    unsigned char     *head,    // Head of buffer
                      *data;    // Data head pointer
    unsigned int      truesize; // Buffer size
    refcount_t        users;    // User count - see {datagram,tcp}.c
};
```

* `char cb[48]`：这是一个“控制缓冲区”（control buffer），存储私有信息。每一层都
  可以将自己的数据放到里面，通常定义一套自己的宏来访问和设置。
* `mark`

## 2.2 网络设备的内核表示：`struct net_device`

```c
// include/linux/netdevice.h

//  struct net_device - The DEVICE structure.
//
//  Actually, this whole structure is a big mistake.  It mixes I/O
//  data with strictly "high-level" data, and it has to know about
//  almost every data structure used in the INET module.  */
struct net_device {
    char                 name[IFNAMSIZ]; // name of the interface.
    struct hlist_node    name_hlist;     // Device name hash chain, please keep it close to name[]
    struct dev_ifalias    __rcu *ifalias;// SNMP alias

    // I/O specific fields
    unsigned long        mem_end;   // Shared memory end
    unsigned long        mem_start; // Shared memory start
    unsigned long        base_addr; // Device I/O address
    int                  irq;       // Device IRQ number

    // Some hardware also needs these fields (state,dev_list, napi_list,unreg_list,close_list).
    unsigned long       state;          // Generic network queuing layer state, see netdev_state_t
    struct list_head    dev_list;       // The global list of network devices
    struct list_head    napi_list;      // List entry used for polling NAPI devices
    struct list_head    unreg_list;     // List entry when we are unregistering the device; see unregister_netdev()
    struct list_head    close_list;     // List entry used when we are closing the device
    struct list_head    ptype_all;      // Device-specific packet handlers for all protocols
    struct list_head    ptype_specific; // Device-specific, protocol-specific packet handlers

    struct { struct list_head upper, lower; } adj_list; // Directly linked devices, like slaves for bonding

    netdev_features_t    features;            // Currently active device features
    netdev_features_t    hw_features;         // User-changeable features
    netdev_features_t    wanted_features;     // User-requested features
    netdev_features_t    vlan_features;       // Mask of features inheritable by VLAN devices
    netdev_features_t    hw_enc_features;     // Mask of features inherited by encapsulating devices.
    netdev_features_t    mpls_features;       // Mask of features inheritable by MPLS
    netdev_features_t    gso_partial_features;

    int                  ifindex; // interface index
    int                  group;   // The group the device belongs to

    atomic_long_t  rx_dropped;  // Dropped packets by core network, do not use this in drivers
    atomic_long_t  tx_dropped;  // Dropped packets by core network, do not use this in drivers
    atomic_long_t  rx_nohandler;// nohandler dropped packets by core network on inactive devices, do not use in drivers

    /* Stats to monitor link on/off, flapping */
    atomic_t       carrier_up_count;   // Number of times the carrier has been up
    atomic_t       carrier_down_count; // Number of times the carrier has been down

    const struct net_device_ops *netdev_ops;    // Includes several pointers to callbacks, if one wants to override the ndo_*() functions
    const struct ethtool_ops    *ethtool_ops;
    const struct switchdev_ops  *switchdev_ops;
    const struct l3mdev_ops     *l3mdev_ops;
    const struct xfrmdev_ops    *xfrmdev_ops;
    const struct tlsdev_ops     *tlsdev_ops;
    const struct header_ops     *header_ops;    // Includes callbacks for creating,parsing,caching,etc of Layer 2 headers.

    unsigned int        flags;          // Interface flags (a la BSD)
    unsigned int        priv_flags;     // Like 'flags' but invisible to userspace, see if.h for the definitions
    unsigned short      gflags;         // Global flags ( kept as legacy )
    unsigned short      padded;         // How much padding added by alloc_netdev()
    unsigned char       operstate;      // RFC2863 operstate
    unsigned char       link_mode;      // Mapping policy to operstate
    unsigned char       if_port;        // Selectable AUI, TP, ...
    unsigned char       dma;            // DMA channel
    unsigned int        mtu;            // Interface MTU value
    unsigned int        min_mtu;        // Interface Minimum MTU value
    unsigned int        max_mtu;        // Interface Maximum MTU value
    unsigned short      type;           // Interface hardware type
    unsigned short      hard_header_len;// Maximum hardware header length.
    unsigned char       min_header_len; // Minimum hardware header length

    unsigned short        needed_headroom;
    unsigned short        needed_tailroom;

    /* Interface address info. */
    unsigned char    perm_addr[MAX_ADDR_LEN];// Permanent hw address                                                                                                                                                     
    unsigned char    addr_assign_type;       // Hw address assignment type
    unsigned char    addr_len;               // Hardware address length
    unsigned short   neigh_priv_len;         // Used in neigh_alloc()
    unsigned short   dev_id;                 // Used to differentiate devices that share the same link layer address
    unsigned short   dev_port;               // Used to differentiate devices that share the same function
    spinlock_t       addr_list_lock;         //
    unsigned char    name_assign_type;       //
    bool             uc_promisc;             // Counter that indicates promiscuous mode has been enabled due to the need to listen to additional unicast addresses in a device that does not implement ndo_set_rx_mode()
    struct netdev_hw_addr_list    uc;        // unicast mac addresses
    struct netdev_hw_addr_list    mc;        // multicast mac addresses
    struct netdev_hw_addr_list    dev_addrs; // list of device hw addresses 

    struct kset      *queues_kset; // Group of all Kobjects in the Tx and RX queues
    unsigned int      promiscuity; // Number of times the NIC is told to work in promiscuous mode; if it becomes 0 the NIC will exit promiscuous mode
    unsigned int      allmulti;    // Counter, enables or disables allmulticast mode

    /* Protocol-specific pointers */
    struct vlan_info __rcu    *vlan_info;
    struct in_device __rcu    *ip_ptr;
    struct dn_dev __rcu       *dn_ptr;
    struct mpls_dev __rcu     *mpls_ptr;

/* Cache lines mostly used on receive path (including eth_type_trans()) */
    /* Interface address info used in eth_type_trans() */
    unsigned char        *dev_addr; // Hw address (before bcast, because most packets are unicast)

    struct netdev_rx_queue       *_rx;               // Array of RX queues
    unsigned int                 num_rx_queues;      // Number of RX queues allocated at register_netdev() time
    unsigned int                 real_num_rx_queues; // Number of RX queues currently active in device

    struct bpf_prog __rcu        *xdp_prog;
    unsigned long                gro_flush_timeout;
    rx_handler_func_t __rcu      *rx_handler;        // handler for received packets
    void __rcu                   *rx_handler_data;

    struct mini_Qdisc __rcu      *miniq_ingress;     // ingress/clsact qdisc specific data for ingress processing
    struct netdev_queue __rcu    *ingress_queue;
    struct nf_hook_entries __rcu *nf_hooks_ingress;

    unsigned char        broadcast[MAX_ADDR_LEN];
    struct cpu_rmap      *rx_cpu_rmap;  // CPU reverse-mapping for RX completion interrupts, indexed by RX queue number. Assigned by driver.
    struct hlist_node    index_hlist;   // Device index hash chain

/* Cache lines mostly used on transmit path */
    struct netdev_queue *_tx;               // Array of TX queues
    unsigned int        num_tx_queues;      // Number of TX queues allocated at alloc_netdev_mq() time
    unsigned int        real_num_tx_queues; // Number of TX queues currently active in device
    struct Qdisc        *qdisc;             // Root qdisc from userspace point of view
    DECLARE_HASHTABLE   (qdisc_hash, 4);
    unsigned int        tx_queue_len;       // Max frames per queue allowed
    spinlock_t          tx_global_lock;
    struct mini_Qdisc __rcu    *miniq_egress;// clsact qdisc specific data for egress processing

    int __percpu        *pcpu_refcnt;        // Number of references to this device
    struct list_head    todo_list;

    struct list_head    link_watch_list;

    enum { NETREG_UNINITIALIZED=0,
           NETREG_REGISTERED,    /* completed register_netdevice */
           NETREG_UNREGISTERING, /* called unregister_netdevice */
           NETREG_UNREGISTERED,  /* completed unregister todo */
           NETREG_RELEASED,      /* called free_netdev */
           NETREG_DUMMY,         /* dummy device for NAPI poll */
    } reg_state:8; // Register/unregister state machine

    bool dismantle; // Device is going to be freed

    enum {
        RTNL_LINK_INITIALIZED,
        RTNL_LINK_INITIALIZING,
    } rtnl_link_state:16; // This enum represents the phases of creating a new link

    bool needs_free_netdev; // Should unregister perform free_netdev?
    void (*priv_destructor)(struct net_device *dev);

    struct netpoll_info __rcu    *npinfo;

    possible_net_t nd_net; // Network namespace this network device is inside

    /* mid-layer private */
    union {
        void                               *ml_priv;// Mid-layer private
        struct pcpu_lstats __percpu        *lstats; // Loopback statistics
        struct pcpu_sw_netstats __percpu   *tstats; // Tunnel statistics
        struct pcpu_dstats __percpu        *dstats; // Dummy statistics
        struct pcpu_vstats __percpu        *vstats; // Virtual ethernet statistics
    };

    struct garp_port __rcu    *garp_port;
    struct mrp_port __rcu     *mrp_port;

    struct device        dev;                          // Class/net/name entry
    const struct attribute_group *sysfs_groups[4];     // Space for optional device, statistics and wireless sysfs groups
    const struct attribute_group *sysfs_rx_queue_group;// Space for optional per-rx queue attributes

    const struct rtnl_link_ops *rtnl_link_ops;

    /* for setting kernel sock attribute on TCP connection setup */
#define GSO_MAX_SIZE        65536
    unsigned int        gso_max_size;
#define GSO_MAX_SEGS        65535
    u16            gso_max_segs;

    const struct dcbnl_rtnl_ops *dcbnl_ops;
    s16                     num_tc;  // Number of traffic classes in the net device
    struct netdev_tc_txq    tc_to_txq[TC_MAX_QUEUE];
    u8                      prio_tc_map[TC_BITMASK + 1];

    struct netprio_map __rcu *priomap;
    struct phy_device        *phydev;      // Physical device may attach itself for hardware timestamping
    struct sfp_bus           *sfp_bus;
    struct lock_class_key    *qdisc_tx_busylock; // lockdep class annotating Qdisc->busylock spinlock
    struct lock_class_key    *qdisc_running_key; // lockdep class annotating Qdisc->running seqcount
    bool                      proto_down;    // protocol port state information can be sent to the switch driver and used to set the phys state of the switch port.
    unsigned                  wol_enabled:1; // Wake-on-LAN is enabled
};
```

# 3 User-Space-to-Kernel Interface

* `/proc`
* `sysctl`
* `sysfs`
* Netlink

# II. 系统初始化（System Initialization）

# 4 Notification Chains

## 4.1 Reasons for Notification Chains

使用场景，例如，

1. 静态路由场景：某个网卡 down 掉，应该从系统路由表中删除相应的路由。
2. 动态路由场景：利用 BGP 动态调整系统路由表。

## Notification Chains for the Networking Subsystems

* `inetaddr_chain`：本机的 IPv4 接口地址发送变化时，触发通知。
* `netdev_chain`：通知网络设备的注册状态。

实现：`include/linux/notifier.h`。

# 5 Network Device Initialization

# 6 The PCI Layer and Network Interface Cards

# 7 Kernel Infrastructure for Component Initialization

# 8 Device Registration and Initialization

## Registering and Unregistering Devices

### Device Registration Status Notification

通过 `netdev_chain` 通知其他子系统网络的状态。

这个 chain 定义在 `net/core/dev.c`。

事件类型 `NETDEV_XX` 定义在 `include/linux/notifier.h`。

Quite a few kernel components register to netdev_chain. Among them are:

* routing
* firewall
* protocol code (e.g. ARP, IP)
* virtual devices
* RTnetlink

# III. 发送和接收（Transmission and Reception）

## 9. 中断和网络驱动

### `struct softnet_data`

each CPU has its own queue for incoming frames.
Because each CPU has its own data structure to manage ingress and egress
traffic,
there is no need for any locking among different CPUs. The data structure for
this
queue, softnet_data, is defined in 

```c
// include/linux/netdevice.h

// Incoming packets are placed on per-CPU queues
struct softnet_data {
    struct list_head       poll_list;
    struct sk_buff_head    process_queue;

    /* stats */
    unsigned int           processed;
    unsigned int           time_squeeze;
    unsigned int           received_rps;
    struct softnet_data    *rps_ipi_list;
    struct sd_flow_limit __rcu *flow_limit;
    struct Qdisc           *output_queue;
    struct Qdisc           **output_queue_tailp;
    struct sk_buff         *completion_queue;
    struct sk_buff_head    xfrm_backlog;

#ifdef CONFIG_RPS
    /* input_queue_head should be written by cpu owning this struct,
     * and only read by other cpus. Worth using a cache line.  */
    unsigned int        input_queue_head ____cacheline_aligned_in_smp;

    /* Elements below can be accessed between CPUs for RPS/RFS */
    call_single_data_t    csd ____cacheline_aligned_in_smp;
    struct softnet_data    *rps_ipi_next;
    unsigned int        cpu;
    unsigned int        input_queue_tail;
#endif

    unsigned int           dropped;
    struct sk_buff_head    input_pkt_queue;
    struct napi_struct     backlog;
};
```

## Call stack

以 Intel ixgbe 网卡为例：

```
ixgbe_poll
 |-ixgbe_clean_rx_irq
    |-if support XDP offload                                      // XDP offload processing
    |    skb = ixgbe_run_xdp()
    |-skb = ixgbe_construct_skb()                                 // Create skb
    |-ixgbe_rx_skb(skb)
       |-napi_gro_receive(skb)                                    // GRO processing
          |-napi_skb_finish(dev_gro_receive(skb))
             |-netif_receive_skb_internal
                |-if generic XDP                                  // Generic XDP processing
                |  |-if do_xdp_generic() != XDP_PASS
                |       return NET_RX_DROP
                |-__netif_receive_skb(skb)
                   |-__netif_receive_skb_one_core
                      |-__netif_receive_skb_core(&pt_prev)
                      |  |-for tap in taps:                       // Taps processing
                      |  |   deliver_skb
                      |  |-sch_handle_ingress                     // TC ingress processing
                      |  |  |-tc_classify
                      |  |     |-for tp in tps:
                      |  |         tp->classify()
                      |  |-nf_ingress                             // Netfilter ingress processing
                      |-if pt_prev
                          pt_prev->func(skb, skb->dev, pt_prev, orig_dev)
                                    |-ip_rcv                      // Enter kernel stack L3
```

```c
// net/core/dev.c

/**
 *    netif_rx    -    post buffer to the network code
 *    @skb: buffer to post
 *
 *    This function receives a packet from a device driver and queues it for
 *    the upper (protocol) levels to process.  It always succeeds. The buffer
 *    may be dropped during processing for congestion control or by the protocol layers.
 *
 *    return values:
 *    NET_RX_SUCCESS    (no congestion)
 *    NET_RX_DROP     (packet was dropped) */
int netif_rx(struct sk_buff *skb)
{
    return netif_rx_internal(skb);
}

static int netif_rx_internal(struct sk_buff *skb)
{
    if (generic_xdp_needed_key) {
        preempt_disable();
        ret = do_xdp_generic(rcu_dereference(skb->dev->xdp_prog), skb);
        preempt_enable();

        if (ret != XDP_PASS)       // Consider XDP consuming the pkt as a success from the netdev's point of view
            return NET_RX_SUCCESS; // we do not want to count this as an error.
    }

    ret = enqueue_to_backlog(skb, get_cpu(), &qtail);
    put_cpu();
    return ret;
}

gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
    skb_mark_napi_id(skb, napi);
    skb_gro_reset_offset(skb);

    return napi_skb_finish(dev_gro_receive(napi, skb), skb);
}

static int netif_receive_skb_internal(struct sk_buff *skb)
{
    net_timestamp_check(netdev_tstamp_prequeue, skb);

    if (skb_defer_rx_timestamp(skb))
        return NET_RX_SUCCESS;

    if (static_branch_&generic_xdp_needed_key)) {
        preempt_disable();
        ret = do_xdp_generic(rcu_dereference(skb->dev->xdp_prog), skb);
        preempt_enable();

        if (ret != XDP_PASS)
            return NET_RX_DROP;
    }

    ret = __netif_receive_skb(skb);
    return ret;
}

static int __netif_receive_skb_core(struct sk_buff *skb, bool pfmemalloc,
                    struct packet_type **ppt_prev)
{
    struct packet_type *ptype, *pt_prev;
    orig_dev = skb->dev;

another_round:
    skb->skb_iif = skb->dev->ifindex;
    __this_cpu_inc(softnet_data.processed);

    if (skb->protocol == ETH_P_8021Q || skb->protocol == ETH_P_8021AD) {
        skb = skb_vlan_untag(skb);
    }

    if (skb_skip_tc_classify(skb)) // skip tc
        goto skip_classify;
    if (pfmemalloc)                // pf: packet filter
        goto skip_taps;

    // 1. 将包送给各种 taps。Taps 是监听 L3 协议（ARP、IPv4、IPv6 等）的 socket。
    // ptype_all: global taps
    list_for_each_entry_rcu(ptype, &ptype_all, list) {
        if (pt_prev) ret = deliver_skb(skb, pt_prev, orig_dev);
        pt_prev = ptype;
    }

    // skb->dev->ptype_all: Device-specific packet handlers for all protocols
    list_for_each_entry_rcu(ptype, &skb->dev->ptype_all, list) {
        if (pt_prev) ret = deliver_skb(skb, pt_prev, orig_dev);
        pt_prev = ptype;
    }

    // 2. 执行 tc 和 netfilter 过滤
skip_taps:
    if (static_branch_&ingress_needed_key)) {
        skb = sch_handle_ingress(skb, &pt_prev, &ret, orig_dev); // 执行 tc 规则
        if (!skb)
            goto out;
        if (nf_ingress(skb, &pt_prev, &ret, orig_dev) < 0)       // 执行 Netfilter 规则
            goto out;
    }
    skb_reset_tc(skb);                                           // 重置 tc

    // 3. pf L2 过滤
skip_classify:
    if (pfmemalloc && !skb_pfmemalloc_protocol(skb))
        goto drop;

    if (skb_vlan_tag_present(skb)) {
        if (pt_prev) {
            ret = deliver_skb(skb, pt_prev, orig_dev);
            pt_prev = NULL;
        }
        if (vlan_do_receive(&skb))
            goto another_round;
        else if (!skb))
            goto out;
    }

    // 4. 正常协议栈处理，对于 IPv4，rx_handle = ip_rcv
    rx_handler = rcu_dereference(skb->dev->rx_handler);
    if (rx_handler) {
        if (pt_prev) {
            ret = deliver_skb(skb, pt_prev, orig_dev);
            pt_prev = NULL;
        }
        switch (rx_handler(&skb)) {
        case RX_HANDLER_CONSUMED: ret = NET_RX_SUCCESS; goto out;
        case RX_HANDLER_ANOTHER :                       goto another_round;
        case RX_HANDLER_PASS    :                       break;
        default                 : BUG();
        }
    }

    if (skb_vlan_tag_present(skb))) {
        if (skb_vlan_tag_get_id(skb)) skb->pkt_type = PACKET_OTHERHOST;
        skb->vlan_tci = 0;
    }

    type = skb->protocol;
    deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type, &orig_dev->ptype_specific);

    if (skb->dev != orig_dev)) {
        deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type, &skb->dev->ptype_specific);
    }

    if (pt_prev) {
        if (skb_orphan_frags_rx(skb, GFP_ATOMIC))) goto drop;
        *ppt_prev = pt_prev;
    } else {
drop:
        kfree_skb(skb);
        ret = NET_RX_DROP;
    }

out:
    return ret;
}

static inline struct sk_buff *
sch_handle_ingress(struct sk_buff *skb, struct packet_type **pt_prev, int *ret,
           struct net_device *orig_dev)
{
    struct mini_Qdisc *miniq = rcu_dereference_bh(skb->dev->miniq_ingress);
    if (*pt_prev) {
        *ret = deliver_skb(skb, *pt_prev, orig_dev);
        *pt_prev = NULL;
    }

    qdisc_skb_cb(skb)->pkt_len = skb->len;
    skb->tc_at_ingress = 1;
    mini_qdisc_bstats_cpu_update(miniq, skb);

    struct tcf_result cl_res;
    switch (tcf_classify(skb, miniq->filter_list, &cl_res, false)) {
    case TC_ACT_OK:
    case TC_ACT_RECLASSIFY:
        skb->tc_index = TC_H_MIN(cl_res.classid);
        break;
    case TC_ACT_SHOT:
        mini_qdisc_qstats_cpu_drop(miniq);
        kfree_skb(skb);
        return NULL;
    case TC_ACT_STOLEN:
    case TC_ACT_QUEUED:
    case TC_ACT_TRAP:
        consume_skb(skb);
        return NULL;
    case TC_ACT_REDIRECT:
        __skb_push(skb, skb->mac_len); // skb_mac_header check was done by cls/act_bpf, so we can safely push
        skb_do_redirect(skb);          // the L2 header back before redirecting to another netdev
        return NULL;
    case TC_ACT_REINSERT:
        skb_tc_reinsert(skb, &cl_res); // this does not scrub the packet, and updates stats on error
        return NULL;
    default:
        break;
    }

    return skb;
}
```

```c
// net/sched/cls_api.c

// Main classifier routine: scans classifier chain attached to this qdisc,
// (optionally) tests for protocol and asks specific classifiers.
int tcf_classify(struct sk_buff *skb, const struct tcf_proto *tp,
         struct tcf_result *res, bool compat_mode)
{
    __be16 protocol = tc_skb_protocol(skb);
    const struct tcf_proto *orig_tp = tp;
    const struct tcf_proto *first_tp;
    int limit = 0;

reclassify:
    for (; tp; tp = rcu_dereference_bh(tp->next)) {
        if (tp->protocol != protocol && tp->protocol != htons(ETH_P_ALL))
            continue;

        err = tp->classify(skb, tp, res);
        if (err == TC_ACT_RECLASSIFY && !compat_mode) {
            first_tp = orig_tp; goto reset;
        } else if (TC_ACT_EXT_CMP(err, TC_ACT_GOTO_CHAIN)) {
            first_tp = res->goto_tp; goto reset;
        }
        if (err >= 0)
            return err;
    }
    return TC_ACT_UNSPEC; /* signal: continue lookup */

reset:
    if (limit++ >= max_reclassify_loop)) { // const int max_reclassify_loop = 4;
        return TC_ACT_SHOT;
    }

    tp = first_tp;
    protocol = tc_skb_protocol(skb);
    goto reclassify;
}
```

```shell
$ grep -R "\.classify" net/*
net/sched/cls_tcindex.c:.classify       =       tcindex_classify,
net/sched/cls_rsvp.h:   .classify       =       rsvp_classify,
net/sched/cls_flow.c:   .classify       =       flow_classify,
net/sched/cls_route.c:  .classify       =       route4_classify,
net/sched/cls_u32.c:    .classify       =       u32_classify,
net/sched/cls_basic.c:  .classify       =       basic_classify,
net/sched/cls_bpf.c:    .classify       =       cls_bpf_classify,
net/sched/cls_flower.c: .classify       =       fl_classify,
net/sched/cls_matchall.c:.classify      =       mall_classify,
net/sched/cls_cgroup.c: .classify       =       cls_cgroup_classify,
net/sched/cls_fw.c:     .classify       =       fw_classify,
```

# 10 数据帧的接收流程（Frame Reception）

上一章 “Interrupt Handlers” 小节中介绍过，interrupt handler
会立即做几件事情，然后将执行权交给中断处理逻辑的下半部分（bottom half）。
这几件事情包括：

1. 初始化一个 `struct sk_buff` 变量。如果启用了 DMA（现在一般都用了），这一步只是初始化一个结构体；否则还需要拷贝整个包的数据。
2. 触发一个 `NET_RX_SOFTIRQ` 类型软中断，通知内核接收数据帧。

## 与其他内核模块的交互

* bridge
* **packet action** (`CONFIG_NET_CLS_ACT`)：TC 可以对 ingress traffic 进行分类和处理（classify and apply actions）。 

Egress queues are associated directly to devices; Traffic Control (the Quality of Service,
or QoS, layer) defines one queue for each device. As we will see in Chapter 11,
the kernel keeps track of devices waiting to transmit frames, not the frames themselves.
We will also see that not all devices actually use Traffic Control. The situation
with ingress queues is a bit more complicated, as we’ll see later.

### Ingress Frame Processing

<p align="center"><img src="/assets/img/understanding-linux-network-internals/10-7.png" width="80%" height="80%"></p>
<p align="center">Fig 10-7. The netif_receive_skb function</p>

When neither the bridging code nor the ingress Traffic Control code consumes the
frame, the latter is passed to the L3 protocol handlers

# 11 数据帧的发送流程（Frame Transmission）

### Queuing Discipline Interface

Each Traffic Control queuing discipline can provide different function pointers to be
called by higher layers to accomplish different tasks. Among the most important
functions are:

* enqueue: Adds an element to the queue
* dequeue: Extracts an element from the queue
* requeue: Puts back on the queue an element that was previously extracted (e.g., because of a transmission failure)

Whenever a device is scheduled for transmission, the next frame to transmit is
selected by the `qdisc_run()` function, which indirectly calls the `dequeue()` virtual function
of the associated queuing discipline.

Once again, the real job is actually done by another function, `qdisc_restart()`. The
`qdisc_run()` function is simply a wrapper that filters
out requests for devices whose egress queues are disabled:

`include/net/pkt_sched.h`

TODO：walk through。

### dev_queue_xmit Function

As
shown in Figure 9-2 in Chapter 9, dev_queue_xmit can lead to the execution of the
driver transmit function hard_start_xmit through two alternate paths:
Interfacing to Traffic Control (the QoS layer)
This is done through the qdisc_run function that we already described in the previous
section.
Invoking hard_start_xmit directly
This is done only for devices that do not use the Traffic Control infrastructures
(i.e., virtual devices).

When dev_queue_xmit is called, all the information required to transmit the frame,
such as the outgoing device, the next hop, and its link layer address, is ready. Parts
VI and VII describe how those parameters are initialized.

# 12 General and Reference Material About Interrupts

# IV. Bridging

the link layer or L2 counterpart of routing: bridging.

# 14 Bridging: Concepts

The algorithm used by bridges to find the best loop-free topology is the Spanning
Tree Protocol defined by the 802.1D-1998 IEEE standard, which was extended with
the new Rapid Spanning Tree Protocol (RSTP) and became 802.1D-2004. RSTP is
sometimes also referred to as 802.1w.

# 15 Bridging: The Spanning Tree Protocol

# 16 Bridging: Linux Implementation

We saw in Chapter 10 how the bridging code can capture ingress packets in netif_
receive_skb.

## Important Data Structures

```c
// net/bridge/br_private.h

struct net_bridge {
    spinlock_t            lock;
    spinlock_t            hash_lock;
    struct list_head        port_list;
    struct net_device        *dev;
    struct pcpu_sw_netstats        __percpu *stats;

    /* These fields are accessed on each packet */
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
    u8                vlan_enabled;
    u8                vlan_stats_enabled;
    __be16            vlan_proto;
    u16               default_pvid;
    struct net_bridge_vlan_group    __rcu *vlgrp;
#endif

    struct rhashtable        fdb_hash_tbl;

#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
    union {
        struct rtable        fake_rtable;
        struct rt6_info        fake_rt6_info;
    };
    bool                nf_call_iptables;
    bool                nf_call_ip6tables;
    bool                nf_call_arptables;
#endif
    u16                group_fwd_mask;
    u16                group_fwd_mask_required;

    /* STP */
    bridge_id            designated_root;
    bridge_id            bridge_id;
    u32                root_path_cost;
    unsigned char            topology_change;
    unsigned char            topology_change_detected;
    u16                root_port;
    unsigned long            max_age;
    unsigned long            hello_time;
    unsigned long            forward_delay;
    unsigned long            ageing_time;
    unsigned long            bridge_max_age;
    unsigned long            bridge_hello_time;
    unsigned long            bridge_forward_delay;
    unsigned long            bridge_ageing_time;

    u8                group_addr[ETH_ALEN];
    bool                group_addr_set;

    enum {
        BR_NO_STP,         /* no spanning tree */
        BR_KERNEL_STP,        /* old STP in kernel */
        BR_USER_STP,        /* new RSTP in userspace */
    } stp_enabled;

    struct timer_list        hello_timer;
    struct timer_list        tcn_timer;
    struct timer_list        topology_change_timer;
    struct delayed_work        gc_work;
    struct kobject            *ifobj;
    u32                auto_cnt;

    bool                neigh_suppress_enabled;
    bool                mtu_set_by_user;
    struct hlist_head        fdb_list;
};
```

```c
// net/bridge/br.c
```

# V. IPv4)

# 18 IPv4: Concepts

## iphdr Structure

# 19 IPv4: Linux Foundations and Features

## General Packet Handling

### Interaction with Netfilter

In each case just listed, the function in charge of the operation is split into two parts,
usually called do_something and do_something_finish. (In a few cases, the names are
do_something and do_something2.) do_something contains only some sanity checks
and maybe some housekeeping. The code that does the real job is in do_something_
finish or do_something2. do_something ends by calling the Netfilter function NF_HOOK,
passing in the point where the call comes from (for instance, packet reception) and
the function to execute if the filtering rules configured by the user with the iptables
command do not decide to drop or reject the packet. If there are no rules to apply or
they simply indicate “go ahead,” the function do_something_finish is executed.
Given the following general call:

### Interaction with the Routing Subsystem

* ip_route_input
Determines the destiny of an input packet. As you can see in Figure 18-1 in
Chapter 18, the packet could be delivered locally, forwarded, or dropped.
* ip_route_output_flow
Used before transmitting a packet. This function returns both the next hop gateway
and the egress device to use.
NF_HOOK(PROTOCOL, HOOK_POSITION_IN_THE_STACK, SKB_BUFFER, IN_DEVICE, OUT_DEVICE, do_
something_finish)

### Processing Input IP Packets

int ip_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt)

ip_rcv did not do much more than a basic sanity check of the packet. So when ip_
rcv_finish is called, it will take care of the main processing, such as:
• Deciding whether the packet has to be locally delivered or forwarded. In the second
case, it needs to find both the egress device and the next hop.
• Parsing and processing some of the IP options. Not all of the options are processed
here, however, as we will see when analyzing the forwarding case.

// net/ipv4/ip_input.c
static inline int ip_rcv_finish(struct sk_buff *skb)

Then the function updates some statistics that are used by Traffic Control (the Quality
of Service, or QoS, layer).

# 20 IPv4: Forwarding and Local Delivery

## Forward

net/ipv4/ip_forward.c

## Local Delivery

int ip_local_deliver(struct sk_buff *skb)

# 21 IPv4: Transmission

## Interface to the Neighboring Subsystem

transmissions end with a call to ip_finish_
output. The latter is a simple wrapper for a Netfilter hook point.

Note that ip_
finish_output does not follow the naming convention do_something + do_something_
finish, but instead the convention do_something + do_something2. ip_finish_output2
is described in the section “Interaction Between Neighboring Protocols and L3
Transmission Functions” in Chapter 27.

```
int ip_finish_output(struct sk_buff *skb)
{
struct net_device *dev = skb->dst->dev;
skb->dev = dev;
skb->protocol = _ _constant_htons(ETH_P_IP);
return NF_HOOK(PF_INET, NF_IP_POST_ROUTING, skb, NULL, dev,
ip_finish_output2);
}
```

When everything is finally in place (including the L2 header), the dev_queue_xmit
function is called (via hh->hh_output or dst->neighbour->output) to do the “hard job”
of transmission.

# 22 IPv4: Handling Fragmentation

# 23 IPv4: Miscellaneous Topics

## Change Notification: rtmsg_ifa

* RTM_NEWADDR
* RTM_DELADDR

So, who is interested in this kind of notification? Routing protocols are a major
example. If you are using Zebra, the routing protocols you have configured would
like to remove all of the routes that are directly or indirectly dependent on an address
that has gone away.

### inetaddr_chain Notification Chain

Here are two examples of users for this notification chain:

* Routing: See the section “External Events” in Chapter 32.
* Netfilter masquerading: When a local IPaddress is used by the Netfilter’s masquerading feature, and that
address disappears, all of the connections that are using that address must be
dropped (see net/ipv4/netfilter/ipt_MASQUERADE.c).

The two NETDEV_DOWN and NETDEV_UP events, respectively, are notified when an IP
address is removed and when it is added to a local device.

# 24 Layer Four Protocol and Raw IP Handling

This chapter describes the interface between L3 and L4 protocols.

<p align="center">Table 24-1. </p>
<p align="center"><img src="/assets/img/understanding-linux-network-internals/table-24-1.png" width="80%" height="80%"></p>

<p align="center"><img src="/assets/img/understanding-linux-network-internals/table-24-2.png" width="80%" height="80%"></p>

## L3 to L4 Delivery: ip_local_deliver_finish

Not all the L4 protocols are implemented in kernel space. For instance, an application
can use raw sockets, as shown earlier in the Zebra code, to bypass L4 in kernel
space. When using raw sockets, the applications supply the kernel with IPpackets
that already include all the necessary L4 information. This makes it possible both to
implement new L4 protocols in user space and to do extra processing in user space
on those L4 protocols normally processed in kernel space. Some L4 protocols, therefore,
are implemented entirely in kernel space (e.g., TCPand UDP), some entirely in
user space (e.g., OSPF), and some partially in kernel space and partially in user space

# 25 Internet Control Message Protocol (ICMPv4)

# VI. Neighboring Subsystem

# 26 Neighboring Subsystem: Concepts

## What Is a Neighbor?

A host is your neighbor if it is connected to the same LAN (i.e., you are directly connected
to it through either a shared medium or a point-to-point link) and it is configured
on the same L3 network.

### Shared Medium

以太网属于 共享介质，需要 Carrier Sense Multiple Access with Collision Detection protocol (CSMA/CD).

串行线路属于点对点连接，designed for communication
between two endpoints only. 无冲突，因此不需要邻居协议。

<p align="center"><img src="/assets/img/understanding-linux-network-internals/26-5.png" width="80%" height="80%"></p>
<p align="center">Fig 26-5. The netif_receive_skb function</p>

# 27 Neighboring Subsystem: Infrastructure

include/net/neighbour.h

## Interaction with Other Subsystems

* routing
* netfilter

The interaction between
Netfilter and the neighboring protocols is taken care of independently from the
neighboring infrastructure, partly because different neighboring protocols sit at
different layers of the network stack.
Figure 28-13 in Chapter 28 shows how Netfilter and ARPinteract by means of
the three dedicated hook points NF_ARP_IN, NF_ARP_OUT, and NF_ARP_FORWARD.
Unlike ARP, ND sits on top of its L3 protocol, IPv6, so it can be firewalled with
the default NF_IP6_PRE_ROUTING, NF_IP6_POST_ROUTING, NF_IP6_LOCAL_IN, and NF_
IP6_LOCAL_OUT hooks used for IPv6 traffic.

## Queuing

### Egress Queuing

When transmitting a data packet, if the association between the destination layer L3
and L2 address has not been resolved yet, the neighboring protocol inserts the packet
temporarily into the arp_queue queue.

# 28 Neighboring Subsystem: Address Resolution Protocol (ARP)

## Virtual IP

一台机器挂掉后，另一台机器
靠 GARP 刷新所有客户端的 arp 缓存。

# 29 Neighboring Subsystem: Miscellaneous Topics

# VII. Routing

# 30 Routing: Concepts

<p align="center"><img src="/assets/img/understanding-linux-network-internals/30-1.png" width="80%" height="80%"></p>
<p align="center">Fig 30-1. Relationship between the routing subsystem and the other main network subsystems</p>

## Routing Table

### Special Routes

Linux 用一张专门的路由表存储 `dst_ip`s 是本机的路由。

这意味着默认情况下，Linux 有两张路由表：

1. 一张存储目的是本机的路由。
2. 一张存储目的是外部主机的路由。

来看一下：

```shell
$ ip rule list
0:      from all lookup local    # <-- 目的是本机的路由
32766:  from all lookup main     # <-- 目的是其他主机的路由
```

```shell
$ ip route show table local
broadcast 10.0.2.0        dev eth0    proto kernel scope link src 10.0.2.15
local     10.0.2.15       dev eth0    proto kernel scope host src 10.0.2.15   # 10.0.2.15 是本机 IP
broadcast 10.0.2.255      dev eth0    proto kernel scope link src 10.0.2.15
broadcast 127.0.0.0       dev lo      proto kernel scope link src 127.0.0.1
local     127.0.0.0/8     dev lo      proto kernel scope host src 127.0.0.1
local     127.0.0.1       dev lo      proto kernel scope host src 127.0.0.1
broadcast 127.255.255.255 dev lo      proto kernel scope link src 127.0.0.1
broadcast 172.17.0.0      dev docker0 proto kernel scope link src 172.17.0.1 linkdown
local     172.17.0.1      dev docker0 proto kernel scope host src 172.17.0.1
broadcast 172.17.255.255  dev docker0 proto kernel scope link src 172.17.0.1 linkdown

$ ip route show table main # 等价于 `ip route` 或 `ip route show`
default via     10.0.2.2        dev     eth0    proto   dhcp    metric  100
10.0.2.0/24     dev     eth0    proto   kernel  scope   link    src     10.0.2.15       metric  100
169.254.0.0/16  dev     eth0    scope   link    metric  1000
172.17.0.0/16   dev     docker0 proto   kernel  scope   link    src     172.17.0.1      linkdown
```

## Routing Table Versus Routing Cache

<p align="center"><img src="/assets/img/understanding-linux-network-internals/table-30-4.png" width="80%" height="80%"></p>

<p align="center"><img src="/assets/img/understanding-linux-network-internals/table-30-5.png" width="80%" height="80%"></p>

<p align="center"><img src="/assets/img/understanding-linux-network-internals/30-9.png" width="80%" height="80%"></p>

# 31 Routing: Advanced

shows how routing interacts with the
Traffic Control subsystem in charge of QoS, and the firewall code (Netfilter).

## Concepts Behind Policy Routing

The main idea behind policy routing is to allow the user to configure routing based
on more parameters than just the destination IP addresses.

An example of the use of policy routing is for an ISPto route traffic based on the
originating customer, or on Quality of Service (QoS) requirements.

The QoS requirements can be derived from
the DiffServ Code Point (DSCP) field of the IP header and from a combination of the
fields of the higher-layer headers (these identify the applications).

### Lookup with Policy Routing

When policy routing is in use, a lookup for a destination consists of two steps:

1. Identify the routing table to use, based on the policies configured. This extra
task inevitably increases routing lookup times.
2. Do a lookup on the selected routing table.

Of course, before taking these two steps, the kernel always tries the routing cache.

### Routing Table Selection

The policies that let the kernel select the routing table to use can be based on the following
parameters:

* Source and/or destination IP address
* Ingress device
* TOS
* Fwmark

### Concepts Behind Multipath Routing

```shell
$ ip route add default scope global nexthop via 100.100.100.1 weight 1 \
                                    nexthop via 200.200.200.1 weight 2
```

如果所有权重都一样，就是 ECMP。

配置算法：

```shell
$ ip route add 10.0.1.0/24 mpath rr nexthop via 192.168.1.1 weight 1 \
                                    nexthop via 192.168.1.2 weight 2
```

## Interactions with Other Kernel Subsystems


<p align="center"><img src="/assets/img/understanding-linux-network-internals/31-6.png" width="80%" height="80%"></p>
<p align="center"> Interactions among routing, Traffic Control, and Firewall (Netfilter)</p>

## Routing Protocol Daemons

Routes can be inserted into the kernel routing tables from three main sources:
• Static configuration via user commands (e.g., ip route, route)
• Dynamic configuration via routing protocols such as Border Gateway Protocol
(BGP), Exterior Gateway Protocol (EGP), and Open Shortest Path First (OSPF),
implemented as user-space routing daemons
• ICMPredirect messages received and processed by the kernel due to suboptimal
configurations

BIRD。

Each daemon maintains its own routing tables in user space. These are not used to
select any routes directly—only the kernel’s routing tables in kernel memory are used
for that. However, the daemons are one of the sources used to populate the kernel
tables, as mentioned earlier in this section. Most of the daemons introduced earlier
implement multiple routing protocols.

Each routing protocol, when running, keeps
its own routing table. Depending on the design of the daemon, each protocol might
install routes into the kernel’s routing table on its own (as shown on the left side of
Figure 31-9), or the protocols may share a common layer within the daemon that
does the talking to the kernel (as shown on the right side of Figure 31-9). The
approach used is a user-space design choice outside the scope of this book.

Communication between routing protocols and the kernel is bidirectional:
• The routing protocols install routes into the kernel’s routing table and remove
routes they have determined to be expired or no longer valid.
• The kernel notifies routing protocols about the installation or removal of new
routes, and about a change of state in a local device link (which of course indirectly
affects all the associated routes). This is possible only when the routing
daemons talk to the kernel via a Netlink socket; that is, a bidirectional channel.

The IPROUTE2 package allows the user not only to configure routes, but also to listen
to the aforementioned notifications generated by the kernel and by routing daemons.
Thus, an administrator can log them or dump them on the screen for
debugging purposes.

# 32 Routing: Linux Implementation

### Advanced Options

* policy routing ()
* use netfilter MARK value as routing key ()
* equal cost multipath (ONFIG_IP_ROUTE_MULTIPATH, ECMP)

## Main Data Structures

* `struct ip_rt_acct`
* `struct rt_cache_stat`
* `struct fib_result`
* `struct fib_rule`
* `struct flowi`

# 33 Routing: The Routing Cache

# 34 Routing: Routing Tables

# 35 Routing: Lookups

# 36 Routing: Miscellaneous Topics
