---
layout: post
title:  "OVS Deep Dive 1: vswitchd"
date:   2016-12-31
---

<p class="intro"><span class="dropcap">I</span>n this OVS Deep Dive series,
I will walk through the <a href="https://github.com/openvswitch/ovs">Open vSwtich</a>
 source code to look into the core designs
and implementations of OVS. The code is based on
 <span style="font-weight:bold">ovs 2.6.1</span>.
</p>

### OVS Architecture
<p align="center"><img src="/assets/img/ovs_arch.jpg"></p>
<p align="center">Fig.1. OVS Architecture (image source NSRC[1])</p>

The following diagram shows the very high-level architecture of Open vSwitch
from a porter's perspective.

```shell
+-------------------+
|    ovs-vswitchd   |<-->ovsdb-server
+-------------------+
|      ofproto      |<-->OpenFlow controllers
+--------+-+--------+
| netdev | | ofproto|
+--------+ |provider|
| netdev | +--------+
|provider|
+--------+
```

ovs-vswitchd

  The main Open vSwitch userspace program, in vswitchd/.  It reads the desired
  Open vSwitch configuration from the ovsdb-server program over an IPC channel
  and passes this configuration down to the "ofproto" library.  It also passes
  certain status and statistical information from ofproto back into the
  database.

## 1. vswitchd
1. Core components in the system
  * communicate with **outside world** using **OpenFlow**
  * communicate with **ovsdb-server** using **OVSDB protocol**
  * communicate with **kernel** over **netlink**
  * communicate with **system** through **netdev** abstract interface
1. Implements mirroring, bonding, and VLANs
1. Tools: **ovs-ofctl, ovs-appctl**

### 1.1. Entrypoint
Entrypoint of `vswitchd` is in `vswitchd/ovs-vswitchd.c`.

Control flow of `vswitchd`:

```c
int main()
{
    /* step.1. init bridge module, obtain configs from ovsdb */
    bridge_init();

    /* step.2. deamon loop */
    while (!exiting) {
        /* step.2.1. process control messages from OpenFlow Controller and CLI */
        bridge_run()
          |
          |--dpdk_init()
          |--bridge_init_ofproto() // init bridges, only once
          |--bridge_run__()
              |
              |--/* Let each datapath type do the work that it needs to do. */
                 for(datapath types):
                     ofproto_type_run(type)
              |--for(all_bridges):
                     ofproto_run(bridge) // handle messages from OpenFlow Controller

        /* receive control messages from CLI (ovs-appctl <xxx>) */
        unixctl_server_run(unixctl);

        /* Performs periodic work needed by all the various kinds of netdevs */
        netdev_run();

        /* step.2.2. wait events arriving */
        bridge_wait();
        unixctl_server_wait(unixctl);
        netdev_wait();

        /* step.2.3. block util events arrive */
        poll_block();
    }
}
```

### 1.2 bridge module init
Let's see what the `bridge_init()` really does:

```c
/* Initializes the bridge module, configuring it to obtain its configuration
 * from an OVSDB server accessed over 'remote', which should be a string in a
 * form acceptable to ovsdb_idl_create(). */
void
bridge_init(const char *remote)
{
    /* step.1. Create connection to database. */
    idl = ovsdb_idl_create(remote, &ovsrec_idl_class, true, true);

    /* step.2. manipulate ovsdb column settings */
    ovsdb_idl_omit_alert(idl, &ovsrec_open_vswitch_col_cur_cfg);
    ...
    ovsdb_idl_omit(idl, &ovsrec_open_vswitch_col_external_ids);

    /* step.3. Register unixctl commands. (ovs-appctl <command>) */
    unixctl_command_register("qos/show-types", "interface", 1, 1,
                             qos_unixctl_show_types, NULL);
    unixctl_command_register("qos/show", "interface", 1, 1,)
    unixctl_command_register("bridge/dump-flows", "bridge", 1, 1,)
    unixctl_command_register("bridge/reconnect", "[bridge]", 0, 1,)

    /* step.4. init submodules */
    lacp_init(); // register command lacp/show <port>
    bond_init(); // register bond commands
    cfm_init();
    bfd_init();
    ovs_numa_init();
    stp_init();
    lldp_init();
    rstp_init();
    ifaces_changed = seq_create();
    last_ifaces_changed = seq_read(ifaces_changed);
    ifnotifier = if_notifier_create(if_change_cb, NULL);
}
```

As we mentioned in the beginning, the command line tool to interact with
vswitchd is
`ovs-appctl`, and note that in step.3, some vswitchd control commands are
registered, so you could verify these commands in an ovs-installed environment:

```shell
$ ovs-appctl --version
ovs-appctl (Open vSwitch) 2.5.0
Compiled Mar 18 2016 15:00:11

$ ovs-appctl bridge/dump-flows br-int
duration=850872s, n_packets=2828, n_bytes=181182, priority=3,in_port=1,dl_vlan=1003,actions=mod_vlan_vid:1003,NORMAL
duration=868485s, n_packets=107181, n_bytes=114634557, priority=2,in_port=1,actions=drop
duration=868485s, n_packets=3886, n_bytes=222426, priority=1,actions=NORMAL
table_id=23, duration=1019439s, n_packets=0, n_bytes=0, ,actions=drop
table_id=254, duration=1019881s, n_packets=0, n_bytes=0, priority=0,reg0=0x3,actions=drop

$ ovs-appctl qos/show br-int
QoS not configured on br-int
ovs-appctl: ovs-vswitchd: server returned an error
```

In step.4, the submodules will also register their `ovs-appctl` commands that
could be used to manage respective resources. This is a good place to get
familiar with those commands.

### 1.3 bridge run
```c
void
bridge_run(void)
{
    /* step.1. init all needed */
    ovsdb_idl_run(idl);
    if_notifier_run();

    if (cfg) {
        dpdk_init(&cfg->other_config);
    }

    /* init ofproto library.  This only runs once */
    bridge_init_ofproto(cfg);
      |
      |--ofproto_init(); // resiter `ofproto/list` command

    /* step.2. datapath & bridge processing */
    bridge_run__();
      |
      |  /* Let each datapath type do the work that it needs to do. */
      |--SSET_FOR_EACH (type, &types) {
      |      ofproto_type_run(type);
      |
      |  /* Let each bridge do the work that it needs to do. */
      |--HMAP_FOR_EACH (br, node, &all_bridges) {
             ofproto_run(br->ofproto);
               |
               |  // handles messages from OpenFlow controller
               |--connmgr_run(connmgr, handle_openflow)

    /* step.3. commit to ovsdb if needed */
    ovsdb_idl_txn_commit(txn);
}
```

`ofproto/ofproto-provider.h`:

```c
/* ofproto class structure, to be defined by each ofproto implementation.
 *
 *
 * Data Structures
 * ===============
 *
 * These functions work primarily with four different kinds of data
 * structures:
 *
 *   - "struct ofproto", which represents an OpenFlow switch (ovs bridge).
 *                       all flow/port operations are done on the ofproto (bridge)
 *
 *   - "struct ofport", which represents a port within an ofproto.
 *
 *   - "struct rule", which represents an OpenFlow flow within an ofproto.
 *
 *   - "struct ofgroup", which represents an OpenFlow 1.1+ group within an
 *     ofproto.
 */
```

```shell
# an ofproto instance is an ovs bridge, so to list all bridges, just issue:
$ ovs-appctl ofproto/list
br-bond
br-int

# you can also get the bridges info by querying ovsdb directly:
$ ovs-vsctl show
f9c76d49-891c-4670-b2fc-75aabca7a1a6
    Bridge br-int
        fail_mode: secure
        Port br-int
            Interface br-int
                type: internal
    Bridge br-bond
        Port br-bond
            Interface br-bond
                type: internal
    ovs_version: "2.5.0"
```

### 1.4 unixctl server run
The unixctl server in ovs receives control commands that
you typed in shell (`ovs-appctl <xxx>`). It is opens a unix socket, and listens
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


### 1.5 netdev run
Here the `netdev` is a general wrapper, which has different implementations on
different platforms, e.g. BSD, linux, DPDK. These implementations are in
`lib/netdev_xxx.c`.

```c
/* Performs periodic work needed by all the various kinds of netdevs.
 *
 * If your program opens any netdevs, it must call this function within its
 * main poll loop. */
void
netdev_run(void)
    OVS_EXCLUDED(netdev_mutex)
{
    netdev_initialize();

    struct netdev_registered_class *rc;
    CMAP_FOR_EACH (rc, cmap_node, &netdev_classes) {
        if (rc->class->run) {
            rc->class->run(rc->class);
        }
    }
}
```

The `rc->class->run(rc->class)` will run into specific implementations, such
as, on linux machine, it will call into `netdev_linux_run()` in
 `lib/netdev_linux.c`.

### 1.6 event loop: wait & block

## References
1. [An OpenVSwitch Introduction From NSRC](https://www.google.com.hk/url?sa=t&rct=j&q=&esrc=s&source=web&cd=8&cad=rja&uact=8&ved=0ahUKEwiy6sCB_pXRAhWKnpQKHblDC2wQFgg-MAc&url=https%3A%2F%2Fnsrc.org%2Fworkshops%2F2014%2Fnznog-sdn%2Fraw-attachment%2Fwiki%2FAgenda%2FOpenVSwitch.pdf&usg=AFQjCNFg9VULvEmHMXQAsuTOE6XLH6WbzQ&sig2=UlVrLltLct2F_xjgnqZiOA)
1. [OVS Doc: Porting Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
