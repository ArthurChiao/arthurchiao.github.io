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

<p align="center"><img src="/assets/img/ovs_arch.jpg"></p>
<p align="center">Fig.1. OVS Architecture (image source NSRC[1])</p>

## 1. Datapath
Datapath is the kernel module of ovs, and it is
kept as small as possible. Apart from
the datapath, other components are implemented in userspace, and have little
dependences with the underlying systems. That means, porting ovs
to another OS or platform is simple (in concept): just porting or
re-implement the
kernel part to the target OS or platform. As an example of this, ovs-dpdk is
just an effort to run OVS over Intel [DPDK](dpdk.org). For those who do, there
is an official [porting guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
for porting OVS to other platforms.

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
