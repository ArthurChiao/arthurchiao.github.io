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

## Datapath
Datapath is the kernel module of ovs, as seen in the above picture. Apart from
the datapath, other components are implemented in userspace, and have little
dependences with systems. In that sense, porting ovs
to another OS or platform is perfectly simple (in concept): just porting or
re-implement the
kernel part to the target OS or platform. As an example os this, ovs-dpdk
is just an effort to run OVS over Intel [DPDK](dpdk.org). There is an official
[guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
for porting OVS to other platforms.

### From Official Doc
The Open vSwitch kernel module allows flexible userspace control over
flow-level packet processing on selected network devices.  It can be used to
implement a plain Ethernet switch, network device bonding, VLAN processing,
network access control, flow-based network control, and so on.

The kernel module implements multiple "datapaths" (analogous to bridges), each
of which can have multiple "vports" (analogous to ports within a bridge).  Each
datapath also has associated with it a "flow table" that userspace populates
with "flows" that map from keys based on packet headers and metadata to sets of
actions.  The most common action forwards the packet to another vport; other
actions are also implemented.

When a packet arrives on a vport, the kernel module processes it by extracting
its flow key and looking it up in the flow table.  If there is a matching flow,
it executes the associated actions.  If there is no match, it queues the packet
to userspace for processing (as part of its processing, userspace will likely
set up a flow to handle further packets of the same type entirely in-kernel).

## References
1. [OVS Doc: Open vSwitch Datapath Development Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/datapath.rst)
1. [OVS Doc: Porting Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
