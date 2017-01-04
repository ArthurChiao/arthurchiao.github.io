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

## OVS Architecture
<p align="center"><img src="/assets/img/ovs-deep-dive/ovs_arch.jpg" width="80%" height="80%"></p>
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

1. Core components in the system
  * communicate with **outside world** using **OpenFlow**
  * communicate with **ovsdb-server** using **OVSDB protocol**
  * communicate with **kernel** over **netlink**
  * communicate with **system** through **netdev** abstract interface
1. Implements mirroring, bonding, and VLANs
1. Tools: **ovs-ofctl, ovs-appctl**

## 1. Entrypoint
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
              |--for(datapath types):/* Let each datapath type do the work that it needs to do. */
                     ofproto_type_run(type)
              |--for(all_bridges):
                     ofproto_run(bridge) // handle messages from OpenFlow Controller

        unixctl_server_run(unixctl); /* receive control messages from CLI (ovs-appctl <xxx>) */
        netdev_run(); /* Performs periodic work needed by all the various kinds of netdevs */

        /* step.2.2. wait events arriving */
        bridge_wait();
        unixctl_server_wait(unixctl);
        netdev_wait();

        /* step.2.3. block util events arrive */
        poll_block();
    }
}
```

## 2. bridge module init
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

    /* step.2. Register unixctl commands. (ovs-appctl <command>) */
    unixctl_command_register("qos/show-types", "interface", 1, 1)
    ...
    unixctl_command_register("bridge/dump-flows", "bridge", 1, 1,)

    /* step.3. init submodules */
    lacp_init(); // register command lacp/show <port>
    bond_init(); // register bond commands
    cfm_init();
    bfd_init();
    ovs_numa_init();
    stp_init();
    lldp_init();
    rstp_init();
    ifnotifier = if_notifier_create(if_change_cb, NULL);
}
```

***idl*** is short for **Interface Definition Language**.
The OVSDB IDL maintains an in-memory replica of a database. It issues RPC
requests to an OVSDB database server and parses the responses, converting
raw JSON into data structures that are easier for clients to digest.
There is more explanations about OVSDB IDL in `ovsdb-idl.h`.

`ovsdb_idl_create()` creates and returns a connection to database 'remote',
the connection will maintain an in-memory replica of the remote database.

As we mentioned in the beginning, the command line tool to interact with
vswitchd is `ovs-appctl`, so you could verify the commands registered in
step.2:

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

## 3. bridge run
```c
void
bridge_run(void)
{
    /* step.1. init all needed */
    ovsdb_idl_run(idl); // handle RPC; sync with remote OVSDB
    if_notifier_run(); // TODO: not sure what's doing here

    if (cfg)
        dpdk_init(&cfg->other_config);

    /* init ofproto library.  This only runs once */
    bridge_init_ofproto(cfg);
      |
      |--ofproto_init(); // resiter `ofproto/list` command
           |
           |--ofproto_class_register(&ofproto_dpif_class) // register default ofproto class
           |--for (ofproto classes):
                ofproto_classes[i]->init() // for ofproto_dpif_class, this will call the init() method in ofproto-dpif.c

    /* step.2. datapath & bridge processing */
    bridge_run__();
      |
      |--FOR_EACH (type, &types) /* Let each datapath type do the work that it needs to do. */
      |    ofproto_type_run(type);
      |
      |--FOR_EACH (br, &all_bridges) /* Let each bridge do the work that it needs to do. */
           ofproto_run(br->ofproto);
               |
               |--ofproto_class->run()
               |--connmgr_run(connmgr, handle_openflow) // handles messages from OpenFlow controller

    /* step.3. commit to ovsdb if needed */
    ovsdb_idl_txn_commit(txn);
}
```

### 3.1. core data structures
In `ofproto/ofproto-provider.h`:

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

### 3.2 preparations before bridges run

ofproto maintains a registered ofproto class array `ofproto_classes`.
In `ofproto_init()`, the default ofproto class `ofproto_dpif_class` will be
registered, which is defined and implemented in `ofproto/ofproto-dpif.c`:

```c
const struct ofproto_class ofproto_dpif_class = {
    init,
    ...
    port_alloc,
    port_construct,
    port_destruct,
    port_dealloc,
    port_modified,
    port_query_by_name,
    port_add,
    port_del,
    ...
    ct_flush,                   /* ct_flush */
};
```

The `init()` method of `ofproto_dpif_class` will register its unixctl commands:

```c
static void
init(const struct shash *iface_hints)
{
    struct shash_node *node;

    /* Make a local copy, since we don't own 'iface_hints' elements. */
    SHASH_FOR_EACH(node, iface_hints) {
        const struct iface_hint *orig_hint = node->data;
        struct iface_hint *new_hint = xmalloc(sizeof *new_hint);

        new_hint->br_name = xstrdup(orig_hint->br_name);
        new_hint->br_type = xstrdup(orig_hint->br_type);
        new_hint->ofp_port = orig_hint->ofp_port;

        shash_add(&init_ofp_ports, node->name, new_hint);
    }

    ofproto_unixctl_init(); // register fdb/xxx commands
    udpif_init();           // register upcall/xxx commands
}
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

### 3.3. datapath and bridge processing

#### 3.3.1 datapath processing
For `dpif`, `ofproto_type_run(type)` will call into `type_run()` defined
in `ofproto/ofproto_dpif.c`.

```c
static int
type_run(const char *type)
{
    udpif_run(backer->udpif);
      |
      |--unixctl_command_reply() // handles upcall

    if (backer->recv_set_enable) {
        udpif_set_threads(backer->udpif, n_handlers, n_revalidators);
    }

    if (backer->need_revalidate) {
        // revalidate

        udpif_revalidate(backer->udpif);
    }

    /* Check for and handle port changes dpif. */
    process_dpif_port_changes(backer);
}
```

#### 3.3.2 bridge processing
`ofproto_run()` is defined in `vswitch/bridge.c`:

```c
int
ofproto_run(struct ofproto *p)
{
    p->ofproto_class->run(p); // calls into ofproto-dpif.c for dpif class

    if (p->ofproto_class->port_poll) {
        while ((error = p->ofproto_class->port_poll(p, &devname)) != EAGAIN) {
            process_port_change(p, error, devname);
        }
    }

    if (new_seq != p->change_seq) {
        /* Update OpenFlow port status for any port whose netdev has changed.
         *
         * Refreshing a given 'ofport' can cause an arbitrary ofport to be
         * destroyed, so it's not safe to update ports directly from the
         * HMAP_FOR_EACH loop, or even to use HMAP_FOR_EACH_SAFE.  Instead, we
         * need this two-phase approach. */
        SSET_FOR_EACH (devname, &devnames) {
            update_port(p, devname);
        }
    }

    connmgr_run(p->connmgr, handle_openflow); // handles openflow messages
}
```

For `dpif`, the `p->ofproto_class->run(p)` calls into `run()` in
`ofproto/ofproto-dpif.c`:

```c
static int
run(struct ofproto *ofproto_)
{
    if (ofproto->netflow) {
        netflow_run(ofproto->netflow);
    }
    if (ofproto->sflow) {
        dpif_sflow_run(ofproto->sflow);
    }
    if (ofproto->ipfix) {
        dpif_ipfix_run(ofproto->ipfix);
    }
    if (ofproto->change_seq != new_seq) {
        HMAP_FOR_EACH (ofport, up.hmap_node, &ofproto->up.ports) {
            port_run(ofport);
        }
    }
    if (ofproto->lacp_enabled || ofproto->has_bonded_bundles) {
        HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles) {
            bundle_run(bundle);
        }
    }

    stp_run(ofproto);
    rstp_run(ofproto);
    if (mac_learning_run(ofproto->ml)) {
        ofproto->backer->need_revalidate = REV_MAC_LEARNING;
    }
    if (mcast_snooping_run(ofproto->ms)) {
        ofproto->backer->need_revalidate = REV_MCAST_SNOOPING;
    }
    if (ofproto->dump_seq != new_dump_seq) {
        /* Expire OpenFlow flows whose idle_timeout or hard_timeout has passed. */
        LIST_FOR_EACH_SAFE (rule, next_rule, expirable,
                            &ofproto->up.expirable) {
            rule_expire(rule_dpif_cast(rule), now);
        }

        /* All outstanding data in existing flows has been accounted, so it's a
         * good time to do bond rebalancing. */
        if (ofproto->has_bonded_bundles) {
            HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles)
                if (bundle->bond)
                    bond_rebalance(bundle->bond);
        }
    }
}

```

### 3.4 commit ovsdb changes

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


## 5. netdev run
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

```c
static void
netdev_linux_run(const struct netdev_class *netdev_class OVS_UNUSED)
{
    if (netdev_linux_miimon_enabled()) {
        netdev_linux_miimon_run();
    }

    sock = netdev_linux_notify_sock();

    do {
        error = nl_sock_recv(sock, &buf, false); // receive from userspace
        if (!error) {
            if (rtnetlink_parse(&buf, &change)) {
                netdev_linux_update(netdev, &change);
            }
        } else if (error == ENOBUFS) {
            nl_sock_drain(sock);

            shash_init(&device_shash);
            netdev_get_devices(&netdev_linux_class, &device_shash);
            SHASH_FOR_EACH (node, &device_shash) {
                get_flags(netdev_, &flags);
                netdev_linux_changed(netdev, flags, 0);
            }
        }
    } while (!error);
}

```

### 5.1 netlink
Netlink socket family is a Linux kernel interface used for inter-process communication (IPC) between both the kernel and userspace processes, and between different userspace processes, in a way similar to the Unix domain sockets. Similarly to the Unix domain sockets, and unlike INET sockets, Netlink communication cannot traverse host boundaries. However, while the Unix domain sockets use the file system namespace, Netlink processes are addressed by process identifiers (PIDs).
Netlink is designed and used for transferring miscellaneous networking information between the kernel space and userspace processes.

## 6. event loop: wait & block

## 7. ingress flow diagram (TODO)

## 8. egress flow diagram (TODO)

## References
1. [An OpenVSwitch Introduction From NSRC](https://www.google.com.hk/url?sa=t&rct=j&q=&esrc=s&source=web&cd=8&cad=rja&uact=8&ved=0ahUKEwiy6sCB_pXRAhWKnpQKHblDC2wQFgg-MAc&url=https%3A%2F%2Fnsrc.org%2Fworkshops%2F2014%2Fnznog-sdn%2Fraw-attachment%2Fwiki%2FAgenda%2FOpenVSwitch.pdf&usg=AFQjCNFg9VULvEmHMXQAsuTOE6XLH6WbzQ&sig2=UlVrLltLct2F_xjgnqZiOA)
1. [OVS Doc: Porting Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
