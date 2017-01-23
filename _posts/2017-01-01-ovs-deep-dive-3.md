---
layout: post
title:  "OVS Deep Dive 3: Datapath"
date:   2017-01-01
---

<p class="intro"><span class="dropcap">I</span>n this OVS Deep Dive series,
I will walk through the <a href="https://github.com/openvswitch/ovs">Open vSwtich</a>
 source code to look into the core designs
and implementations of OVS. The code is based on
 <span style="font-weight:bold">ovs 2.6.1</span>.
</p>

## 1. Datapath
Traditionally, datapath is the kernel module of ovs, and it is
kept as small as possible. Apart from
the datapath, other components are implemented in userspace, and have little
dependences with the underlying systems. That means, porting ovs
to another OS or platform is simple (in concept): just porting or
re-implement the
kernel part to the target OS or platform. As an example of this, ovs-dpdk is
just an effort to run OVS over Intel [DPDK](dpdk.org). For those who do, there
is an official [porting guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
for porting OVS to other platforms.

In fact, in recent versions of OVS, there are already two type of datapath that
you could choose from: **kernel datapath and userspace datapath** (i'm not sure
since which version, but according to my tests, 2.3+ support this).

<p align="center"><img src="/assets/img/ovs-deep-dive/dpif_providers.png" width="75%" height="75%"></p>
<p align="center">Fig.1.1 Two Types of Datapaths</p>

### 1.1 Kernel Datapath

This is the default datapath type. It needs a kernel module called
`openvswitch.ko` to be loaded:

```shell
$ lsmod | grep openvswitch
openvswitch            98304  3
```

If it is not loaded, you need to install it manually:

```shell
$ find / -name openvswitch.ko
<path>/openvswitch.ko

$ modprobe openvswitch.ko
$ insmod <path>/openvswitch.ko
$ lsmod | grep openvswitch
```

Creating an OVS bridge:

```shell
$ ovs-vsctl add-br br-test

$ ovs-vsctl show
05daf6f1-da58-4e01-8530-f6ec0d51b4e1
    Bridge br-test
        Port br-test
            Interface br-test
                type: internal
    ovs_version: "2.5.0"
```

### 1.2 Userspace Datapath

Userspace datapath differs from the traditional datapath in that its packet forwarding and processing
are done in userspace. Among those, netdev-dpdk is one of the most popular
implementations.

Commands for creating an OVS bridge using userspace datapath:

```shell
$ ovs-vsctl add-br br-test -- set Bridge br-test datapath_type=netdev
```

As shown, you must specify the `datapath_type` to be `netdev` when creating a
bridge, otherwise you will get an error like ***ovs-vsctl: Error detected while
setting up 'br-test'***.

## 1.3 Datapath and TX Offloading

For performance considerations, instances (VMs, containers, etc) often offload
the checksum job (TCP, UDP checksums, etc) to physical NICs. You could check
the offload flags of interface with `ethtool`:

```shell
$ ethtool -k eth0
Features for eth0:
rx-checksumming: on
tx-checksumming: off
        tx-checksum-ipv4: off
        tx-checksum-ip-generic: off [fixed]
        tx-checksum-ipv6: off
        tx-checksum-fcoe-crc: off [fixed]
        tx-checksum-sctp: off [fixed]
scatter-gather: off
        tx-scatter-gather: off
        tx-scatter-gather-fraglist: off [fixed]
tcp-segmentation-offload: off
        tx-tcp-segmentation: off
        tx-tcp-ecn-segmentation: off [fixed]
        tx-tcp6-segmentation: off
udp-fragmentation-offload: off [fixed]

...

l2-fwd-offload: off [fixed]
busy-poll: off [fixed]
hw-tc-offload: off [fixed]
```

In typical deployments, instances connect its virtual device (NIC) to OVS bridge through 
veth pair, and OVS also manages one or more physical NICs:

```
(VM) tap <---> veth -- OVS -- phy_NIC
```

When the TX offload is enabled on the VM's tap device (usual case), OVS will pass
packets to physical NIC for checksum calculations, the passing is done through
the kernel module. So in this scenario, everything goes ok.

A Problem occurs when the OVS bridge uses Userspace Datapath. I'm not sure
if all userspace datapath implementations do not support TX offloading, but
according to my tests, at least OVS 2.3+ Userspace datapath
does not support. The phenomenon of this is that two instances connected with
OVS could not establish TCP connection, but `ping` and `UDP` connections are OK.

`tcpdump -vv -i <tap of dst instance>`, we could find that the first TCP packets
arrives destination instance B, but is detected as `TCP checksum` error, and discarded by
B. This is because the source instance A enables TCP offloading,
so the packet is sent to OVS with faked TCP checksum; while OVS with Userspace
Datapath do not do TCP checksum, so the packet is sent out (or just forwarded to)
with wrong checksum; on receiving this packet, B detects
that the TCP checksum is incorrect, then discards it directly.
Thus the TCP connection could never be established.

If we turn off the TCP TX offload on A, we could see that the first
TCP packet is correctly received by B; and then, an ACK packet
is sent from B to A. However, A discards the ACK packet because of
the same reason: TCP checksum incorrect.

Further, turn off TCP TX offload in B. Then TCP conncection will be established
right away.

Why does Userspace Datapath does not support TX offloading? As far as I could
figure out[4], it's because Userspace Datapath is highly optimized which
conflicts with TX offloading. Some optimizations have to be turned off for
supporting TX offloading. However, the benefits is not obvious, or [even
worse](https://mail.openvswitch.org/pipermail/ovs-dev/2016-August/322058.html).
TX offloading could achieves ~10% perfomance increase, so it is enabled by
default.

In summary, **if deploy OVS with DPDK enabled, you have to turn off TX offload
flags in your instances** in which case the instance itself will take care of
the checksum calculating.

### Official Doc
The Open vSwitch kernel module allows flexible userspace control over
flow-level packet processing on selected network devices.  It can be used to
implement a plain Ethernet switch, network device bonding, VLAN processing,
network access control, flow-based network control, and so on.

The kernel module implements multiple ***datapaths*** (analogous to bridges), each
of which can have multiple ***vports*** (analogous to ports within a bridge).  Each
datapath also has associated with it a ***flow table*** that userspace populates
with ***flows*** that map from keys based on packet headers and metadata to sets of
actions.  The most common action forwards the packet to another vport; other
actions are also implemented.

When a packet arrives on a vport, the kernel module processes it by extracting
its flow key and looking it up in the flow table.  If there is a matching flow,
it executes the associated actions.  If there is no match, it queues the packet
to userspace for processing (as part of its processing, userspace will likely
set up a flow to handle further packets of the same type entirely in-kernel).


## 2. Key Data Structures

Some key data structures in kernel module:

* `datapath` - flow-based packet forwarding/swithcing module
* `flow`
* `flow_table`
* `sw_flow_key`
* `vport`

### 2.1 Datapath

```c
/** struct datapath - datapath for flow-based packet switching */
struct datapath {
	struct rcu_head rcu;
	struct list_head list_node;

	struct flow_table table;
	struct hlist_head *ports; /* Switch ports. */
	struct dp_stats_percpu __percpu *stats_percpu;
	possible_net_t net; /* Network namespace ref. */

	u32 user_features;
	u32 max_headroom;
};
```

### 2.2 Flow

```c
struct sw_flow {
	struct rcu_head rcu;
	struct {
		struct hlist_node node[2];
		u32 hash;
	} flow_table, ufid_table;
	int stats_last_writer;		/* NUMA-node id of the last writer on * 'stats[0]'.  */
	struct sw_flow_key key;
	struct sw_flow_id id;
	struct sw_flow_mask *mask;
	struct sw_flow_actions __rcu *sf_acts;
	struct flow_stats __rcu *stats[]; /* One for each NUMA node.  First one
					   * is allocated at flow creation time,
					   * the rest are allocated on demand
					   * while holding the 'stats[0].lock'.
					   */
};
```

### 2.3 Flow Table

```c
struct table_instance {
	struct flex_array *buckets;
	unsigned int n_buckets;
	struct rcu_head rcu;
	int node_ver;
	u32 hash_seed;
	bool keep_flows;
};

struct flow_table {
	struct table_instance __rcu *ti;
	struct table_instance __rcu *ufid_ti;
	struct mask_cache_entry __percpu *mask_cache;
	struct mask_array __rcu *mask_array;
	unsigned long last_rehash;
	unsigned int count;
	unsigned int ufid_count;
};
```

### 2.4 vport

```c
/** struct vport - one port within a datapath */
struct vport {
	struct net_device *dev;
	struct datapath	*dp;
	struct vport_portids __rcu *upcall_portids;
	u16 port_no;

	struct hlist_node hash_node;
	struct hlist_node dp_hash_node;
	const struct vport_ops *ops;

	struct list_head detach_list;
	struct rcu_head rcu;
};
```

## References
1. [OVS Doc: Open vSwitch Datapath Development Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/datapath.rst)
1. [OVS Doc: Porting Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
1. [ovs bridge breaking TCP between two virtio net devices when checksum offload on](https://bugs.launchpad.net/ubuntu/+source/openvswitch/+bug/1629053)
1. [netdev-dpdk: Enable Rx checksum offloading	feature on DPDK physical ports](https://mail.openvswitch.org/pipermail/ovs-dev/2016-August/322058.html)
