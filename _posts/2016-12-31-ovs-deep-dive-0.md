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
The official doc [WHY Open vSwitch](https://github.com/openvswitch/ovs/blob/master/Documentation/intro/why-ovs.rst)
explains the reason, but is very abstract. I bet you need years of
virtualization and networking experiences to understand what it's saying.

Instead, the following materials are highly recommended for new comers to OVS:

* excellent hands-on video to OVS [Introduction to Open vSwitch](https://www.youtube.com/watch?v=rYW7kQRyUvA)
* [presentation](https://www.google.com.hk/url?sa=t&rct=j&q=&esrc=s&source=web&cd=8&cad=rja&uact=8&ved=0ahUKEwiy6sCB_pXRAhWKnpQKHblDC2wQFgg-MAc&url=https%3A%2F%2Fnsrc.org%2Fworkshops%2F2014%2Fnznog-sdn%2Fraw-attachment%2Fwiki%2FAgenda%2FOpenVSwitch.pdf&usg=AFQjCNFg9VULvEmHMXQAsuTOE6XLH6WbzQ&sig2=UlVrLltLct2F_xjgnqZiOA)
from NSRC

## 2. OVS Architecture

<p align="center"><img src="/assets/img/ovs-deep-dive/ovs_arch.jpg" width="80%" height="80%"></p>
<p align="center">Fig.2.1. OVS Architecture (image source NSRC[1])</p>

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

`vswitchd` is the deamon process of OVS, handles control messages from
`ovs-appctl`. `ovsdb-server` is the database of OVS, which talks
`ovsdb protocol` through `ovs-vsctl` and `ovs-ofctl`. These are independent
services after OVS started:

```shell
$ ps -ef | grep ovs
root     63346     1  0  2016 ?        00:00:00 ovsdb-server: monitoring pid 63347 (healthy)
root     63347 63346  0  2016 ?        01:16:25 ovsdb-server /etc/openvswitch/conf.db -vconsole:emer -vsyslog:err -vfile:info --remote=punix:/var/run/openvswitch/db.sock --private-key=db:Open_vSwitch,SSL,private_key --certificate=db:Open_vSwitch,SSL,certificate --bootstrap-ca-cert=db:Open_vSwitch,SSL,ca_cert --no-chdir --log-file=/var/log/openvswitch/ovsdb-server.log --pidfile=/var/run/openvswitch/ovsdb-server.pid --detach --monitor
root     63356     1  0  2016 ?        00:00:00 ovs-vswitchd: monitoring pid 63357 (healthy)
root     63357 63356  0  2016 ?        01:03:31 ovs-vswitchd unix:/var/run/openvswitch/db.sock -vconsole:emer -vsyslog:err -vfile:info --mlockall --no-chdir --log-file=/var/log/openvswitch/ovs-vswitchd.log --pidfile=/var/run/openvswitch/ovs-vswitchd.pid --detach --monitor
```

The third part is usually called `datapath`, which is a kernel module that
performs platform-dependent packet forwarding.

<p align="center"><img src="/assets/img/ovs-deep-dive/ovs_packet_flow.jpg" width="80%" height="80%"></p>
<p align="center">Fig.2.2. Components and Interfaces of OVS (image source[6])</p>

## 3. ovs-vswitchd
The main Open vSwitch userspace program, in vswitchd/.  It reads the desired
Open vSwitch configuration from the ovsdb-server program over an IPC channel
and passes this configuration down to the "ofproto" library.  It also passes
certain status and statistical information from ofproto back into the
database.

<p align="center"><img src="/assets/img/ovs-deep-dive/vswitchd_ovsdb_ofproto.png" width="75%" height="75%"></p>
<p align="center">Fig.3.1. vswitchd: pass messages between ovsdb and ofproto</p>

## 4. ofproto
  The Open vSwitch library, in ofproto/, that implements an OpenFlow switch.
  It talks to OpenFlow controllers over the network and to switch hardware or
  software through an "ofproto provider", explained further below.

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
<p align="center">Fig.4.1. OVS Architecture [2]</p>

## 5. netdev
  The Open vSwitch library, in lib/netdev.c, that abstracts interacting with
  network devices, that is, Ethernet interfaces.  The netdev library is a thin
  layer over "netdev provider" code, explained further below.

A **netdev provider** implements an OS- and hardware-specific interface to
"network devices", e.g. eth0 on Linux. **Open vSwitch must be able to open
each port on a switch as a netdev**, so you will need to implement a
"netdev provider" that works with your switch hardware and software.

Specifically, the end of DPDK init process is to register it's netdev provider
classes:

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


## Summary
1. Three components of OVS
  * `vswitchd`
  * `ovsdb`
  * `datapath` (kernel module)
1. Some implementation terms
  * `ofproto`: ovs bridge
  * `ofproto provider`: interface to manage an specific OpenFlow-capable software/hardware switch
  * `ofproto-dpif` - the built-in ofproto provider implementation in OVS
  * `dpif` - a library servers for `ofproto-dpif`

## References
1. [PDF: An OpenVSwitch Introduction From NSRC](https://www.google.com.hk/url?sa=t&rct=j&q=&esrc=s&source=web&cd=8&cad=rja&uact=8&ved=0ahUKEwiy6sCB_pXRAhWKnpQKHblDC2wQFgg-MAc&url=https%3A%2F%2Fnsrc.org%2Fworkshops%2F2014%2Fnznog-sdn%2Fraw-attachment%2Fwiki%2FAgenda%2FOpenVSwitch.pdf&usg=AFQjCNFg9VULvEmHMXQAsuTOE6XLH6WbzQ&sig2=UlVrLltLct2F_xjgnqZiOA)
1. [OVS Doc: Porting Guide](https://github.com/openvswitch/ovs/blob/master/Documentation/topics/porting.rst)
1. [OVS Doc: What Is OVS](https://github.com/openvswitch/ovs/blob/master/Documentation/intro/what-is-ovs.rst)
1. [OVS Doc: WHY OVS](https://github.com/openvswitch/ovs/blob/master/Documentation/intro/why-ovs.rst)
1. [YouTube: Introduction to Open vSwitch](https://www.youtube.com/watch?v=rYW7kQRyUvA)
1. [The Design and Implementation of Open vSwitch](https://www.usenix.org/system/files/conference/nsdi15/nsdi15-paper-pfaff.pdf)
