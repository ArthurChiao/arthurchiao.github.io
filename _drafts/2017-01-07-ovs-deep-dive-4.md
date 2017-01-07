---
layout: post
title:  "OVS Deep Dive 4: Patch Port"
date:   2017-01-07
---

This post introduces OVS patch port, and compares it with linux veth pair.

## 1. What is OVS `patch port`

## 2. Data Structures

* `netdev_class`
* `vport_class`

`vport_class` is defined in `lib/netdev-vport.c`, thus its an internal
structure used only in this file:

```c
struct vport_class {
    const char *dpif_port;
    struct netdev_class netdev_class;
};
```

While, `netdev_class` is a general abstraction of all network devices.
It is defined in `lib/netdev-provider.h`, any network device type has to 
implement the methods in `netdev_class` before used:

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

## 3. `netdev` classes

### 3.1 `netdev` Classes

**All `netdev` classes**:

* vport
  - tunnel class: `geneve`, `gre`, `vxlan`, `lisp`, `stt`
  - `patch_class`
* dpdk
  - `dpdk_class`
  - `dpdk_ring_class`
  - `dpdk_vhost_class`
  - `dpdk_vhost_client_class`
* dummy
  - `dummy_class`
  - `dummy_internal_class`
  - `dummy_pmd_class`
* netdev
  - `netdev_linux_class`
  - `netdev_tap_class`
  - `netdev_bsd_class`
  - `netdev_windows_class`
  - `netdev_internal_class`

```
/* Type of netdevs in this class, e.g. "systt em", "tap", "gre", etc.
 *
 * One of the providers should supply a "system" type, since this is
 * the type assumed if no type is specified when opening a netdev.
 * The "system" type corresponds to an existing network device on
 * the system. */
```

### 3.2 `vport` Classes

**All vport classes**:

* tunnel vports: `geneve`, `gre`, `vxlan`, `lisp`, `stt`
* `patch` (patch port)

The registration of the tunnel vports is in `netdev_vport_tunnel_register()`;
and `patch` port in `netdev_vport_patch_register()`;

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

## 4. `dpif`

**All `dpif` classes:**

* `dpif_netdev_class`
  - implemented in `lib/dpif-netdev.c`
* `dpif_netlink_class`
  - implemented in `lib/dpif-netlink.c`

calls `dpif_init()` to init the classes.

## 4. Patch Port
Patch Port as a kind of `netdev` is register by calling
`netdev_register_provider(const struct netdev_class *new_class)`

The routine initializes and registers a new netdev provider.  After successful
registration, new netdevs of that type can be opened using `netdev_open()`.

`patch` port accepts exactly one parameter: `peer`, the other side of the 
connection. This is much like linux **veth pair**.

### 4.1 How Patch Port Works

route changes:

```c
 * Device Change Notification
 * ==========================
 *
 * Minimally, implementations are required to report changes to netdev flags,
 * features, ethernet address or carrier through connectivity_seq. Changes to

/* Whenever the route-table change number is incremented,
 * netdev_vport_route_changed() should be called to update
 * the corresponding tunnel interface status. */
static void
netdev_vport_route_changed(void)
{
    struct netdev **vports;
    size_t i, n_vports;

    vports = netdev_get_vports(&n_vports);
    for (i = 0; i < n_vports; i++) {
        struct netdev *netdev_ = vports[i];
        struct netdev_vport *netdev = netdev_vport_cast(netdev_);

        ovs_mutex_lock(&netdev->mutex);
        /* Finds all tunnel vports. */
        if (ipv6_addr_is_set(&netdev->tnl_cfg.ipv6_dst)) {
            if (tunnel_check_status_change__(netdev)) {
                netdev_change_seq_changed(netdev_);
            }
        }
        ovs_mutex_unlock(&netdev->mutex);

        netdev_close(netdev_);
    }

    free(vports);
}

```

***What's the relationship of netdev provider and dpif provider?***

## RX Process

```c
dpif_netdev_run() 
  |--for (rxq)
       dp_netdev_process_rxq_port()
         |--netdev_rxq_recv()
              |--rx->netdev->netdev_class->rxq_recv(rx, batch)
                   |  //implementation specific
                   |--netdev_linux_rxq_recv_sock()
                   |--netdev_linux_rxq_recv_tap()
                   |--netdev_linux_rxq_recv()
                   |
                   |--netdev_dummy_rxq_recv()
                   |
                   |--netdev_bsd_rxq_recv()
                   |
                   |--netdev_dpdk_rxq_recv()
                   |--netdev_dpdk_vhost_rxq_recv()
```
