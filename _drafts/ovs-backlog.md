## coverage
Function/code hotness statistics.

`ovs-appctl coverage/show`. Cool!

`lib/coverage.c`.

## connmgr - connection to OpenFlow Controller
 ofproto supports two kinds of OpenFlow connections:

   - "Primary" connections to ordinary OpenFlow controllers.  ofproto
     maintains persistent connections to these controllers and by default
     sends them asynchronous messages such as packet-ins.

   - "Service" connections, e.g. from ovs-ofctl.  When these connections
     drop, it is the other side's responsibility to reconnect them if
     necessary.  ofproto does not send them asynchronous messages by default.

 Currently, active (tcp, ssl, unix) connections are always "primary"
 connections and passive (ptcp, pssl, punix) connections are always "service"
 connections.  There is no inherent reason for this, but it reflects the
 common case.

`ofproto/connmgr.c`

## event loop implementation
poll() based.

run - wait - block

## netlink
communication between userspace and kernel.

## unixctl
vswitchd <---> unix sock <---> cli binary (ovs-vsctl, ovs-ofctl, ovs-appctl, etc)

## 4. unixctl server run
The unixctl server in ovs receives control commands that
you typed in shell (`ovs-appctl <xxx>`). It opens a unix socket, and listens
on it. Typically, the socket file is located at `/var/run/openvswitch/`, and
one socket file for each bridge:

```shell
$ ll /var/run/openvswitch
total 8.0K
drwxr-xr-x  2 root root  220 Dec 19 16:56 ./
drwxr-xr-x 35 root root 1.1K Dec 21 16:58 ../
srwxr-x---  1 root root    0 Dec 19 16:56 br-bond.mgmt=
srwxr-x---  1 root root    0 Dec 19 16:56 br-bond.snoop=
srwxr-x---  1 root root    0 Dec 19 16:56 br-int.mgmt=
srwxr-x---  1 root root    0 Dec 19 16:56 br-int.snoop=
srwxr-x---  1 root root    0 Dec 19 16:56 db.sock=
srwxr-x---  1 root root    0 Dec 19 16:56 ovsdb-server.63347.ctl=
-rw-r--r--  1 root root    6 Dec 19 16:56 ovsdb-server.pid
srwxr-x---  1 root root    0 Dec 19 16:56 ovs-vswitchd.63357.ctl=
-rw-r--r--  1 root root    6 Dec 19 16:56 ovs-vswitchd.pid
```

## CLI tools
in `lib/`.


### route changes

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


The biggest user of this construct might be libpcap. Issuing a high-level
filter command like `tcpdump -i em1 port 22` passes through the libpcap
internal compiler that generates a structure that can eventually be loaded
via SO_ATTACH_FILTER to the kernel. `tcpdump -i em1 port 22 -ddd`
displays what is being placed into this structure[8].

`tcpdump` uses `libpcap` for packet capturing, and `libpcap` translates user
specified high-level filter commands (e.g. `tcpdump -i eth0 'icmp'`) into BPF code.
BPF[8] works with kernel stack. So if your
packet does not go through kernel network stack, you could not get any packets
captured. When a packet goes through a patch port, is is just forwared between
OVS datapath ports (to be specific, the `patch_class` vport type), and the
packet will not go through kernel stack, so tcpdump is out-of-work.

BPF allows a user-space program to attach a filter onto any socket and
allow or disallow certain types of data to come through the socket. 

