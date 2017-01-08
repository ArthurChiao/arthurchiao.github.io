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

