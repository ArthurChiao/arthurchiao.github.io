---
layout: post
title:  "OVS Deep Dive 3: Datapath"
date:   2017-01-01
---

<p class="intro"><span class="dropcap">I</span>n this OVS Deep Dive series,
I will walk through the <a href="https://github.com/openvswitch/ovs">Open vSwtich</a>
 source code to get familiar with the core designs
and implementations of OVS. The code is based on
 <span style="font-weight:bold">ovs 2.6.1</span>.
</p>

<p align="center"><img src="/assets/img/ovs_arch.jpg"></p>
<p align="center">Fig.1. OVS Architecture (image source NSRC[1])</p>

## Datapath
Datapath is the kernel module of ovs, as seen in the above picture. Apart from
the datapath, other components are implemented in userspace, and have little[
***TODO: request for proofs***] dependences with systems. In that sense, porting ovs
to another OS or platform is perfectly simple in theory: just re-implement the
kernel module in the target OS or platform. As an example os this, ovs-dpdk
is just an effort to run ovs over Intel [DPDK](dpdk.org).
