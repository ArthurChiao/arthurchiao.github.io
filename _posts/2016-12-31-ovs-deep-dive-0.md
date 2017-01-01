---
layout: post
title:  "OVS Deep Dive 0: Overview"
date:   2016-12-31
---

<p class="intro"><span class="dropcap">I</span>n this OVS Deep Dive series,
I will walk through the <a href="https://github.com/openvswitch/ovs">Open vSwtich</a>
 source code to look into the core designs
and implementations of OVS. The code is based on
 <span style="font-weight:bold">ovs 2.6.1</span>.
</p>

## 1. WHY OVS

## 2. OVS Architecture
<p align="center"><img src="/assets/img/ovs_arch.jpg"></p>
<p align="center">Fig.1. OVS Architecture (image source NSRC[1])</p>

Three Components of OVS:

* vswitchd
  * tools: `ovs-appctl`
* ovsdb
  * tools: `ovs-vsctl`, `ovs-ofctl`
* kernel module (datapath)
  * tools: `ovs-dpctl`


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
<p align="center">Fig.2. OVS High Level Architecture [2]</p>

Some of the components are generic.  Modulo bugs or inadequacies, these
components should not need to be modified as part of a port:

ovs-vswitchd
  The main Open vSwitch userspace program, in vswitchd/.  It reads the desired
  Open vSwitch configuration from the ovsdb-server program over an IPC channel
  and passes this configuration down to the "ofproto" library.  It also passes
  certain status and statistical information from ofproto back into the
  database.

ofproto
  The Open vSwitch library, in ofproto/, that implements an OpenFlow switch.
  It talks to OpenFlow controllers over the network and to switch hardware or
  software through an "ofproto provider", explained further below.

netdev
  The Open vSwitch library, in lib/netdev.c, that abstracts interacting with
  network devices, that is, Ethernet interfaces.  The netdev library is a thin
  layer over "netdev provider" code, explained further below.

A **netdev provider** implements an OS- and hardware-specific interface to
"network devices", e.g. eth0 on Linux. **Open vSwitch must be able to open
each port on a switch as a netdev**, so you will need to implement a
"netdev provider" that works with your switch hardware and software.

An **ofproto provider** is what ofproto uses to directly **monitor and control
an OpenFlow-capable switch**. struct `ofproto_class`, in `ofproto/ofproto-provider.h`,
defines the interfaces to implement an ofproto provider for new hardware or software.

Open vSwitch has a **built-in ofproto provider** named **ofproto-dpif**, which
is built on top of a library for manipulating datapaths, called **dpif**.
A "datapath" is a simple flow table, one that is only required to support
exact-match flows, that is, flows without wildcards. When a packet arrives on
a network device, the datapath looks for it in this table.  If there is a
match, then it performs the associated actions.  If there is no match, the
datapath passes the packet up to ofproto-dpif, which maintains the full
OpenFlow flow table.  If the packet matches in this flow table, then
ofproto-dpif executes its actions and inserts a new entry into the dpif flow
table.  (Otherwise, ofproto-dpif passes the packet up to ofproto to send the
packet to the OpenFlow controller, if one is configured.)

The "dpif" library in turn delegates much of its functionality to a "dpif
provider".  The following diagram shows how dpif providers fit into the Open
vSwitch architecture:

```shell
    Architecure

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
<p align="center">Fig.3. OVS Architecture [2]</p>

## Summary
1. Three components of OVS
  * `vswitchd`
  * `ovsdb`
  * `datapath` (kernel module)
1. Some concepts
  * `ofproto`: ovs bridge
  * `ofproto provider`: interface to manage an specific OpenFlow-capable software/hardware switch
  * `ofproto-dpif` - the built-in ofproto provider implementation in OVS
  * `dpif` - a library servers for `ofproto-dpif`

## References
1. [An OpenVSwitch Introduction From NSRC](https://www.google.com.hk/url?sa=t&rct=j&q=&esrc=s&source=web&cd=8&cad=rja&uact=8&ved=0ahUKEwiy6sCB_pXRAhWKnpQKHblDC2wQFgg-MAc&url=https%3A%2F%2Fnsrc.org%2Fworkshops%2F2014%2Fnznog-sdn%2Fraw-attachment%2Fwiki%2FAgenda%2FOpenVSwitch.pdf&usg=AFQjCNFg9VULvEmHMXQAsuTOE6XLH6WbzQ&sig2=UlVrLltLct2F_xjgnqZiOA)
1. [OVS Doc: Porting Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
