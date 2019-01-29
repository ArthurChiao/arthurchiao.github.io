---
layout: post
title:  "OVS Deep Dive 0: Overview"
date:   2016-12-31
categories: OVS
---

<p class="intro"><span class="dropcap">I</span>n this OVS Deep Dive series,
I will walk through the <a href="https://github.com/openvswitch/ovs">Open vSwtich</a>
 source code to look into the core designs
and implementations of OVS. The code is based on
 <span style="font-weight:bold">ovs 2.6.1</span>.
</p>

## 1. WHY OVS
The official doc [WHY Open vSwitch](https://github.com/openvswitch/ovs/blob/master/Documentation/intro/why-ovs.rst)
describes how OVS emerges and what problems it aims at solving.
But the explanation is very high-level and abstract. I bet you need years of
virtualization and networking experiences to understand what it's saying.

Instead, I highly recommend the following materials to new comers of OVS:

* excellent hands-on intro to OVS [Introduction to Open vSwitch (video)](https://www.youtube.com/watch?v=rYW7kQRyUvA)
* [presentation (PDF)](https://www.google.com.hk/url?sa=t&rct=j&q=&esrc=s&source=web&cd=8&cad=rja&uact=8&ved=0ahUKEwiy6sCB_pXRAhWKnpQKHblDC2wQFgg-MAc&url=https%3A%2F%2Fnsrc.org%2Fworkshops%2F2014%2Fnznog-sdn%2Fraw-attachment%2Fwiki%2FAgenda%2FOpenVSwitch.pdf&usg=AFQjCNFg9VULvEmHMXQAsuTOE6XLH6WbzQ&sig2=UlVrLltLct2F_xjgnqZiOA)
from NSRC

## 2. OVS Architecture

<p align="center"><img src="/assets/img/ovs-deep-dive/ovs_arch.jpg" width="80%" height="80%"></p>
<p align="center">Fig.2.1. OVS Architecture (image source NSRC[1])</p>

OVS is usually used to bridge up multiple VMs/contaiers within one host.  Such
as, in OpenStack compute node, it is used as integration bridge to connect all
the VMs running on the node. It manages both physical ports (e.g. eth0, eth1)
and virtual ports (tap devices of VMs).

As depicted in Fig.2.1, OVS is composed of three components:

* vswitchd
  * user space program, ovs deamon
  * tools: `ovs-appctl`
* ovsdb-server
  * user space program, database server of OVS
  * tools: `ovs-vsctl`, `ovs-ofctl`
* kernel module (datapath)
  * kernel space module, OVS packet forwarder
  * tools: `ovs-dpctl`

`vswitchd` is the main deamon process of OVS,
`ovsdb-server` is the database server of OVS, and `datapath` is a kernel
module that performs **platform-dependent** packet
forwarding. After OVS started, we could see two services: `ovs-vswitchd` and
`ovsdb-server`:

```shell
$ ps -ef | grep ovs
root     63346     1  0  2016 ?        00:00:00 ovsdb-server: monitoring pid 63347 (healthy)
root     63347 63346  0  2016 ?        01:16:25 ovsdb-server /etc/openvswitch/conf.db -vconsole:emer -vsyslog:err -vfile:info --remote=punix:/var/run/openvswitch/db.sock --private-key=db:Open_vSwitch,SSL,private_key --certificate=db:Open_vSwitch,SSL,certificate --bootstrap-ca-cert=db:Open_vSwitch,SSL,ca_cert --no-chdir --log-file=/var/log/openvswitch/ovsdb-server.log --pidfile=/var/run/openvswitch/ovsdb-server.pid --detach --monitor
root     63356     1  0  2016 ?        00:00:00 ovs-vswitchd: monitoring pid 63357 (healthy)
root     63357 63356  0  2016 ?        01:03:31 ovs-vswitchd unix:/var/run/openvswitch/db.sock -vconsole:emer -vsyslog:err -vfile:info --mlockall --no-chdir --log-file=/var/log/openvswitch/ovs-vswitchd.log --pidfile=/var/run/openvswitch/ovs-vswitchd.pid --detach --monitor
```

`ovs-ovswitchd` receives OpenFlow messages from OpenFlow controller, and
`OVSDB-protocol` format messages from `ovsdb-server`. Communication between
`ovs-vswitchd` and `datapath` is through
[netlink](https://en.wikipedia.org/wiki/Netlink) (a socket family similar with
Unix Domain Socket).

## 3. OVS Components
Let's get a quick glance of the three components of OVS.
Detailed explorations will be in subsequent articles of this series.

### 3.1. OVS Daemon
`ovs-vswitchd` is
the main Open vSwitch userspace program. It reads the desired
Open vSwitch configuration from ovsdb-server over an IPC channel
and passes this configuration down to the ovs bridges (implemented as
a library called `ofproto`).  It also passes
certain status and statistical information from ovs bridges back into the
database.

<p align="center"><img src="/assets/img/ovs-deep-dive/vswitchd_ovsdb_ofproto.png" width="75%" height="75%"></p>
<p align="center">Fig.3.1. vswitchd: ovs main daemon</p>

### 3.2 OVSDB
Some transient configurations, e.g. flows, are stored in datapaths and vswitchd.
Persistent configurations are stored in ovsdb, which survives reboot.

`ovsdb-server` provides RPC itnerfaces to OVSDB. It supports JSON-RPC
client connections over active or passive TCP/IP or Unix domain sockets.

`ovsdb-server` runs either as a backup server, or as an active server. Only
the active server handles transactions that will change the OVSDB.

### 3.3 Datapath
Datapath is the main packet forwarding module of OVS, implemented in kernel
space for high performance. It caches OpenFlow flows, and execute actions
on received packets which match specific flow(s). If no flow is matched for
one packet, the packet will be delivered to userspace program `ovs-vswitchd`.
Usually,
`ovs-vswitchd` will issue an new flow to datapath which will be used to handle
subsequent packets of this type. The high performance comes from the fact
that most packets will match flows successfully in datapath, thus will be
processed directly in kernel space.

## 4. OVS Packet Handling

Let's first see how a packet traverses through OVS.

<p align="center"><img src="/assets/img/ovs-deep-dive/ovs_packet_flow.jpg" width="80%" height="80%"></p>
<p align="center">Fig.4.1. OVS Packet Handling (image source[6])</p>

OVS is an OpenFlow-capable software switch.

An OpenFlow controller is responsible for instructing
datapath how to handle different types packets, in the form called ***flows***.
A ***flow*** describes how should datapth handle packets of one specific type,
in the form called ***actions***. Action types include forwarding to another
port, output, modify vlan tag, etc.
The process of finding a flow for a packet is called ***flow matching***.

For performance consideration, part of the flows are cached in datapath, and
the others stored in `vswitchd`.

Fig.4.1 depicts how OVS forwards packets.

A packet enters OVS datapath after it is received on a NIC.
If a flow is matched in datapath for the packet, the datapath simply excutes the
actions described in the flow. Otherwise (flow missing), datapath delivers the
packet to
`ovs-vswitchd`, and another flow-matching process will be done there. After
`ovs-vswitchd` determines how the packet should be handled, it passes the packet
back to the datapath with the desired handling.  Usually, it also tells the
datapath to cache the flow, for handling similar packets later.

## 5. Implementation

### 5.1 vswitchd
Implementation of `vswitchd` is at `vswitchd/`.

Implementation of ovs bridge is at `ofproto/`.

### 5.2 ovsdb
Implementation of `OVSDB` is at `ovsdb/`.

### 5.1 datapath
Implementation of `vswitchd` is at `datapath/`, and `datapath-windows/` for
windows.

## Summary
1. Three components of OVS
  * `vswitchd`
  * `ovsdb`
  * `datapath` (kernel module)
1. Some implementation terms
  * `ovs-vswitchd`: main OVS daemon
  * `ovsdb-server`: OVSDB service daemon

## References
1. [PDF: An OpenVSwitch Introduction From NSRC](https://www.google.com.hk/url?sa=t&rct=j&q=&esrc=s&source=web&cd=8&cad=rja&uact=8&ved=0ahUKEwiy6sCB_pXRAhWKnpQKHblDC2wQFgg-MAc&url=https%3A%2F%2Fnsrc.org%2Fworkshops%2F2014%2Fnznog-sdn%2Fraw-attachment%2Fwiki%2FAgenda%2FOpenVSwitch.pdf&usg=AFQjCNFg9VULvEmHMXQAsuTOE6XLH6WbzQ&sig2=UlVrLltLct2F_xjgnqZiOA)
1. [OVS Doc: Porting Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
1. [OVS Doc: What Is OVS](https://github.com/openvswitch/ovs/blob/master/Documentation/intro/what-is-ovs.rst)
1. [OVS Doc: WHY OVS](https://github.com/openvswitch/ovs/blob/master/Documentation/intro/why-ovs.rst)
1. [YouTube: Introduction to Open vSwitch](https://www.youtube.com/watch?v=rYW7kQRyUvA)
1. [The Design and Implementation of Open vSwitch](https://www.usenix.org/system/files/conference/nsdi15/nsdi15-paper-pfaff.pdf)
