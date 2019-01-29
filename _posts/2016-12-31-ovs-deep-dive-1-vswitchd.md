---
layout: post
title:  "OVS Deep Dive 1: vswitchd"
date:   2016-12-31
categories: OVS
---

<p class="intro"><span class="dropcap">I</span>n this OVS Deep Dive series,
I will walk through the <a href="https://github.com/openvswitch/ovs">Open vSwtich</a>
 source code to look into the core designs
and implementations of OVS. The code is based on
 <span style="font-weight:bold">ovs 2.6.1</span>.
</p>

## 1. vswitchd Overview
<p align="center"><img src="/assets/img/ovs-deep-dive/ovs_arch.jpg" width="80%" height="80%"></p>
<p align="center">Fig.1. OVS Architecture (image source NSRC[1])</p>

As depicted in Fig.1, `ovs-vswitchd` sits in the key position of OVS, which
needs to interact with OpenFlow controller, OVSDB, and kernel module.

1. Core components in the system
  * communicate with **outside world** using `OpenFlow`
  * communicate with **ovsdb-server** using `OVSDB protocol`
  * communicate with **kernel** over `netlink`
  * communicate with **system** through `netdev` abstract interface
1. Implements mirroring, bonding, and VLANs
1. CLI Tools: `ovs-ofctl`, `ovs-appctl`

The following diagram reveals more details:

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

Here, the `vswitch` module is further devided into submodules/libraies:

* `ovs-vswitchd`: `vswitchd` daemon
* `ofproto`: library which abstracts ovs bridge
* `ofproto-provider`: interface to control an specific kind of OpenFlow switch
* `netdev`: library which abstracts network devices
* `netdev-provider`: OS- and hardware-specific interface to network devices

We explain these concepts and data structures in following sections.

## 2. Key Data Structures

```shell
                          _
                         |   +-------------------+
                         |   |    ovs-vswitchd   |<-->ovsdb-server
                         |   +-------------------+
                         |   |      ofproto      |<-->OpenFlow controllers
                         |   +--------+-+--------+  _
                         |   | netdev | |ofproto-|   |
               userspace |   +--------+ |  dpif  |   |
                         |   | netdev | +--------+   |
                         |   |provider| |  dpif  |   |
                         |   +---||---+ +--------+   |
                         |       ||     |  dpif  |   | implementation of
                         |       ||     |provider|   | ofproto provider
                         |_      ||     +---||---+   |
                                 ||         ||       |
                          _  +---||-----+---||---+   |
                         |   |          |datapath|   |
                  kernel |   |          +--------+  _|
                         |   |                   |
                         |_  +--------||---------+
                                      ||
                                   physical
                                      NIC

```
<p align="center">Fig.2.1. OVS Internal Architecture [2]</p>

An OVS bridges manages two types of resources:

* the forwarding plane it controls (`datapath`)
* the (physical and virtual) network devices attached to it (`netdev`)

Key data structures: 

* OVS bridge implementation

  `ofproto`, `ofproto-provider`

* for `datapath` management

  `dpif`, `dpif-provider`

* for network devices management
  
  `netdev`, `netdev-provider`
  
We explain them, respectively.

### 2.1 ofproto

`struct ofproto` abstracts OpenFlow switches.
An ofproto instance is just an OpenFlow switch (bridge).

Data Structures (`ofproto/ofproto-provider.h`):

  - `struct ofproto`: represents an OpenFlow switch (ovs bridge),
                      all flow/port operations are done on the ofproto
  - `struct ofport`: represents a port within an ofproto
  - `struct rule`: represents an OpenFlow flow within an ofproto
  - `struct ofgroup`: represents an OpenFlow 1.1+ group within an ofproto

```c
/* An OpenFlow switch. */
struct ofproto {
    const struct ofproto_class *ofproto_class;
    char *type;                 /* Datapath type. */
    char *name;                 /* Datapath name. */

    /* Settings. */
    uint64_t fallback_dpid;     /* Datapath ID if no better choice found. */
    uint64_t datapath_id;       /* Datapath ID. */

    /* Datapath. */
    struct hmap ports;          /* Contains "struct ofport"s. */
    struct simap ofp_requests;  /* OpenFlow port number requests. */
    uint16_t max_ports;         /* Max possible OpenFlow port num, plus one. */

    /* Flow tables. */
    struct oftable *tables;

    /* Rules indexed on their cookie values, in all flow tables. */

    /* List of expirable flows, in all flow tables. */

    /* OpenFlow connections. */

    /* Groups. */

    /* Tunnel TLV mapping table. */
};


/* An OpenFlow port within a "struct ofproto".
 *
 * The port's name is netdev_get_name(port->netdev).
 */
struct ofport {
    struct hmap_node hmap_node; /* In struct ofproto's "ports" hmap. */
    struct ofproto *ofproto;    /* The ofproto that contains this port. */
    struct netdev *netdev;
    struct ofputil_phy_port pp;
    ofp_port_t ofp_port;        /* OpenFlow port number. */
    uint64_t change_seq;
    long long int created;      /* Time created, in msec. */
    int mtu;
};
```

### 2.2 ofproto-provider

<p align="center"><img src="/assets/img/ovs-deep-dive/ofproto_providers.png"></p>
<p align="center">Fig.2.1. OVS Providers</p>

**ofproto class structure, to be defined by each ofproto (ovs bridge)
implementation.**

An **ofproto provider** is what ofproto uses to directly **monitor and control
an OpenFlow-capable switch**. struct `ofproto_class`, in `ofproto/ofproto-provider.h`,
defines the interfaces to implement an ofproto provider for new hardware or software.

Open vSwitch has a **built-in ofproto provider** named **ofproto-dpif**, which
is built on top of a library for manipulating datapaths, called **dpif**.
A "datapath" is a simple flow table, one that is only required to support
exact-match flows, that is, flows without wildcards. When a packet arrives on
a network device, the datapath looks for it in this table.  If there is a
match, then it performs the associated actions.  If there is no match, the
datapath passes the packet up to `ofproto-dpif`, **which maintains the full
OpenFlow flow table**.  If the packet matches in this flow table, then
ofproto-dpif executes its actions and inserts a new entry into the dpif flow
table.  (Otherwise, ofproto-dpif passes the packet up to ofproto to send the
packet to the OpenFlow controller, if one is configured.)

The "dpif" library in turn delegates much of its functionality to a "dpif
provider".  Fig.2.1 shows how dpif providers fit into the Open
vSwitch architecture.

### 2.3 netdev

The Open vSwitch library, defined in `lib/netdev-provider.h`, implemented in
`lib/netdev.c`, that **abstracts interacting with
network devices**, that is, Ethernet interfaces.

Every port on a switch must have a corresponding netdev that must minimally
support a few operations, such as the ability to read the netdev's MTU, get the
number of RX and TX queues.

The netdev library is a thin
layer over "netdev provider" code, explained further below.

### 2.4 netdev-provider

<p align="center"><img src="/assets/img/ovs-deep-dive/netdev_providers.png"></p>
<p align="center">Fig.2.2. netdev providers</p>

A **netdev provider** implements an **OS- and hardware-specific interface to
"network devices"**, e.g. and ethernet device. **Open vSwitch must be able to open
each port on a switch as a netdev**, so you will need to implement a
"netdev provider" that works with your switch hardware and software.

Fig.2.2 depicts the `netdev` and `netdev providers` in OVS. The detailed
`netdev` provider types are listed below:

**All types of `netdev` classes**:

* linux netdev (`lib/netdev-linux.c`, for linux platform)
  - `system` - `netdev_linux_class`
  - `tap` - `netdev_tap_class`
  - `internal` - `netdev_internal_class`
* bsd netdev (`lib/netdev-bsd.c`, for bsd platform)
  - `system` - `netdev_bsd_class`
  - `tap` - `netdev_tap_class`
* windows netdev (for windows platform)
  - `system` - `netdev_windows_class`
  - `internal` - `netdev_internal_class`
* dummy netdev (`lib/netdev-dummy.c`)
  - `dummy` - `dummy_class`
  - `dummy-internal` - `dummy_internal_class`
  - `dummy-pmd` - `dummy_pmd_class`
* vport netdev (`lib/netdev-vport.c`, a vport holds a reference to a port in
  datapath, the latter could be opened with `netdev_open()`)
  - tunnel class:
    - `geneve`
    - `gre`
    - `vxlan`
    - `lisp`
    - `stt`
  - `patch` - `patch_class`
* dpdk netdev
  - `dpdk_class`
  - `dpdk_ring_class`
  - `dpdk_vhost_class`
  - `dpdk_vhost_client_class`

For example, the community is experimenting running OVS over DPDK, which
performs high performance packet processing in userspace. In this solution, the
kernel module of OVS will be replaced by the counterparts in DPDK.  That means a
DPDK netdev must be implemented as the **netdev-provider** for this platform. If
you look at the source code, you will see that's what exactly been doing at
the end of DPDK init code - register it's netdev provider classes:

```c
void
netdev_dpdk_register(void)
{
    netdev_register_provider(&dpdk_class);
    netdev_register_provider(&dpdk_ring_class);
    netdev_register_provider(&dpdk_vhost_class);
    netdev_register_provider(&dpdk_vhost_client_class);
}
```

The Porting section of the documentation has more information in the
"Writing a netdev Provider" section.

## 3. Call Flows

<p align="center"><img src="/assets/img/ovs-deep-dive/vswitch_flow_diagram.jpg" width="35%" height="35%"></p>
<p align="center">Fig.3.1 vswitchd flow diagram</p>

Entrypoint of `vswitchd` is in `vswitchd/ovs-vswitchd.c`.
Logical control diagram of `ovs-vswitchd` is depicted in Fig.3.1.

At the start, it initializes the bridge module, which is implemented in
`vswitchd/bridge.c`. The bridge module will retrieve some configuration
parameters from ovsdb.

Then, `ovs-vswitchd` enters the main loop. In the first iteration of this loop,
it initializes some libraries, include DPDK (if configured), and the most
important, `ofproto` library. Note that these resources only init once.

Then, each datapath will do its work by running `ofproto_type_run()`, which will
call into the specific `type_run()` implementation of that datapath type.

Then, each bridge will do its work by running `ofproto_run()`, which will call
into the specific `run()` implementation of ofproto class.

Then, `ovs-vswitchd` will handle IPC (JSON-RPC) messages, which comes from
command line (`ovs-appctl`) and `ovsdb-server`.

Then, `netdev_run()` is called to process all the various kinds of netdevs.

After all the above work is done, the brige, unixctl server, and netdev modules
will enter blocking state until new signals trigger.

Corresponding psudo-code is shown below:

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

We explain some of the most important procedures in the following.

## 4. Procedures and Submodules

### 4.1. bridge module init
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

`ovs-vswitchd` first creates a connection to `ovsdb-server` using a module
called ***OVSDB IDL***.
***IDL*** is short for **Interface Definition Language**.
The OVSDB IDL maintains an in-memory replica of a database. It issues RPC
requests to an OVSDB database server and parses the responses, converting
raw JSON into data structures that are easier for clients to digest.
There is more explanations about OVSDB IDL in `ovsdb-idl.h`.

`unixctl_command_register()` will register a single unixctl command, which
allow controlling `ovs-vswitchd` over CLI. Each submodule 
calls this method to register its subcommands and expose them to the outside.
As we mentioned in the beginning, the command line tool to interact with
vswitchd is `ovs-appctl`, so you could verify the those registered commands:

```shell
$ ovs-appctl --version
ovs-appctl (Open vSwitch) 2.5.0
Compiled Mar 18 2016 15:00:11

$ ovs-appctl bridge/dump-flows br-int
duration=850872s, n_packets=2828, n_bytes=181182, priority=3,in_port=1,dl_vlan=1003,actions=mod_vlan_vid:1003,NORMAL
duration=868485s, n_packets=3886, n_bytes=222426, priority=1,actions=NORMAL
table_id=23, duration=1019439s, n_packets=0, n_bytes=0, ,actions=drop

$ ovs-appctl qos/show br-int
QoS not configured on br-int
ovs-appctl: ovs-vswitchd: server returned an error
```

At the end of `bridge_init()`, some vswitchd submodules are initialized,
including **LACP, BOND, CFM, BDF, NUMA, STP, LLDP, RSTP**, and `inotifiers`.

The internal structure of `ovs-vswitchd` is shown in Fig.4.1.

<p align="center"><img src="/assets/img/ovs-deep-dive/vswitchd_2.png" width="65%" height="65%"></p>
<p align="center">Fig.4.1 vswitchd internal modules</p>

### 4.2. ofproto library init

ofproto maintains a registered ofproto class array `ofproto_classes`, in
`ofproto/ofproto.c`:

```c
288 /* All registered ofproto classes, in probe order. */
289 static const struct ofproto_class **ofproto_classes;
290 static size_t n_ofproto_classes;
291 static size_t allocated_ofproto_classes;
```

In `ofproto_init()`, the built in ofproto class `ofproto_dpif_class` will be
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

Let's test one:

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

Note that `ofproto` library only inits once.

### 4.3. Datapath Processing

<p style="color: red; font-weight:bold">TODO: this section needs to refine</p>

After `ofproto` library is correctly initialized (which means all datapath
types that will be used later have been registered), vswitchd will loop over
the datapath types and let the datapath handle all the things it needs to,
such as, process port changes in this datapath, handle upcall (sending
mis-matched packets to OpenFlow controller). Datapath finishes these
logics by implementing the callback `type_run()`.

For `ofproto-dpif` - the built in datapath type, the `type_run()` is implemented
in `ofproto/ofproto_dpif.c`:

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

### 4.4. Bridge Processing

<p style="color: red; font-weight:bold">TODO: this section needs to refine</p>

In each loop, vswitchd also let each bridge handle all its affairs in
`ofproto_run()`.  `ofproto_run()` is defined in `vswitch/bridge.c`:

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

In the above, the bridge first calls the `run()` method of this ofproto class,
to let the ofproto class handle all its class-specific affairs. Then it proceeds
to the handling of port changes and OpenFlow messages.  
For `dpif`, the `run()` method is implemented in `ofproto/ofproto-dpif.c`:

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

### 4.5 Sum Up: bridge_run()

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

### 4.6 Unixctl IPC Handling
In each loop of `ovs-vswitchd`, `unixctl_server_run()` will be called once.
In this method, the unixctl server first accepts connctions from IPC clients,
then processes requests from each connection.

```c
void
unixctl_server_run(struct unixctl_server *server)
{
    // accept connections
    for (i = 0; i < 10; i++) {
        error = pstream_accept(server->listener, &stream);
        if (!error) {
            conn->rpc = jsonrpc_open(stream);
        } else if (error == EAGAIN) {
            break;
    }

    // process requests from each connection
    LIST_FOR_EACH_SAFE (conn, next, node, &server->conns) {
        run_connection(conn);
          |
          |--jsonrpc_run()
          |    |--stream_send()
          |--jsonrpc_recv(conn_rpc, &msg)
               |--assert(msg.type == JSONRPC_REQUEST)
               |--process_command(conn, msg) // format the received text to desired output
                    |--registerd unixctl command callback
    }
}
```

<p style="color: red; font-weight:bold">TODO: add an example</p>

### 4.7. netdev run
In `netdev_run()`, vswitchd **loops over all the network devices, and updates
the `netdev` information of any of them changed** (e.g. mtu, src/dst mac).
The netdev processing in each loop is as follows:

```c
/* Performs periodic work needed by all the various kinds of netdevs.
 *
 * If your program opens any netdevs, it must call this function within its
 * main poll loop. */
void
netdev_run(void)
{
    netdev_initialize();

    struct netdev_registered_class *rc;
    CMAP_FOR_EACH (rc, cmap_node, &netdev_classes) {
        if (rc->class->run)
            rc->class->run(rc->class);
    }
}
```

`struct netdev` is a generic abstraction of network devices, defined in
`lib/netdev-provider.h`:

```c
/* A network device (e.g. an Ethernet device).
 *
 * Network device implementations may read these members but should not modify
 * them. */
struct netdev {
    char *name;                         /* Name of network device. */
    struct netdev_class *netdev_class; /* Functions to control this device. */
    ...
    int n_txq;
    int n_rxq;
    int ref_cnt;                        /* Times this devices was opened. */
    struct shash_node *node;            /* Pointer to element in global map. */
};
```

Each specific type of network device needs to implement the methods in `struct
netdev_class`, e.g. there are implementations for BSD, linux, DPDK. These
implementations are in `lib/netdev_xxx.c`.

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

`rc->class->run(rc->class)` will run into specific implementations, for linux
`netdev`s, it will call into `netdev_linux_run()` in `lib/netdev_linux.c`. And
what it does in the callback is **detecting linux network device changes through
netlink, e.g. mtc, src/dst mac changes, and updates these changes to
corresponding `netdev`**:

```c
static void
netdev_linux_run(const struct netdev_class *netdev_class OVS_UNUSED)
{
    if (netdev_linux_miimon_enabled())
        netdev_linux_miimon_run();

    /* Returns a NETLINK_ROUTE socket listening for RTNLGRP_LINK,
     * RTNLGRP_IPV4_IFADDR and RTNLGRP_IPV6_IFADDR changes */
    sock = netdev_linux_notify_sock();

    do {
        error = nl_sock_recv(sock, &buf, false); // receive from kernel space
        if (!error) {
            if (rtnetlink_parse(&buf, &change)) {
                netdev_linux_update(netdev, &change);
                  |  // update netdev changes, e.g. mtu, src/dst mac, etc
                  |- netdev_linux_changed(netdev, flags, 0);
            }
        } else if (error == ENOBUFS) {
            netdev_get_devices(&netdev_linux_class, &device_shash);
            SHASH_FOR_EACH (node, &device_shash) {
                get_flags(netdev_, &flags);
                netdev_linux_changed(netdev, flags, 0);
            }
        }
    } while (!error);
}
```

Netlink socket family is a Linux kernel interface used for **IPC
between both the kernel and userspace processes, and between different userspace
processes**. Similarly to the Unix domain sockets, and unlike INET sockets,
Netlink communication cannot traverse host boundaries. However, while the Unix
domain sockets use the file system namespace, Netlink processes are addressed by
process identifiers (PIDs).  Netlink is designed and used for transferring
miscellaneous networking information between the kernel space and userspace
processes[3].

## Summary
1. `ovs-vswitchd` flow diagram

    <p align="center"><img src="/assets/img/ovs-deep-dive/vswitch_flow_diagram.jpg" width="35%" height="35%"></p>
    <p align="center">Fig.3.1 vswitchd flow diagram</p>

1. `ovs-vswitchd` iteraction with other modules

    <p align="center"><img src="/assets/img/ovs-deep-dive/vswitchd_2.png" width="65%" height="65%"></p>
    <p align="center">Fig.4.1 vswitchd internal modules</p>

1. Implementation terms
  * `ofproto`: ovs bridge
  * `ofproto provider`: interface to manage an specific OpenFlow-capable software/hardware switch
  * `ofproto-dpif` - the built-in ofproto provider implementation in OVS
  * `dpif` - a library servers for `ofproto-dpif`
  * `netdev` - generic abstraction of network devices
  * `netdev-provider` - interface to OS- and platform-specific network devices

## 5. event loop: wait & block

## 6. ingress flow diagram (TODO)

## 7. egress flow diagram (TODO)

## References
1. [An OpenVSwitch Introduction From NSRC](https://www.google.com.hk/url?sa=t&rct=j&q=&esrc=s&source=web&cd=8&cad=rja&uact=8&ved=0ahUKEwiy6sCB_pXRAhWKnpQKHblDC2wQFgg-MAc&url=https%3A%2F%2Fnsrc.org%2Fworkshops%2F2014%2Fnznog-sdn%2Fraw-attachment%2Fwiki%2FAgenda%2FOpenVSwitch.pdf&usg=AFQjCNFg9VULvEmHMXQAsuTOE6XLH6WbzQ&sig2=UlVrLltLct2F_xjgnqZiOA)
1. [OVS Doc: Porting Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
1. [Wikipedia: netlink](https://en.wikipedia.org/wiki/Netlink)
