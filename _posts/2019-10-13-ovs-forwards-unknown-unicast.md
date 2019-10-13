---
layout    : post
title     : "OVS balance-slb bond Forwards Unknown Unicast"
date      : 2019-10-13
lastupdate: 2019-10-13
categories: ovs unicast-flooding
---

## TL; DR

In some cases, OVS `2.5.6` bond with mode `balance-slb` will forward the [unknown
unicast](https://en.wikipedia.org/wiki/Broadcast,_unknown-unicast_and_multicast_traffic)
[4] traffic it received from one physical NIC back into the physical network
through another NIC, which will **result in a L2 loop** with physical network,
this in turn will cause the physical switches complaining **MAC flapping** (the
same MAC address presents at multiple places at a short time) and then
**stopping MAC learning** for some time (e.g. 1 minute), which will be a severe
network problem.

Note that at the time of writing this post, `2.5.x` is the latest LTS, and
`2.3.x` is the previous LTS. The documents on `balance-slb` bond are same in
those two series, however, the behavior changed according to our experiences.

# 1 Problem Description

Recently, our datacenter network team found that there were occasional MAC
flapping problems, these problems **occurred several times a day**, but the
**consequences were severe**: affected switches would stop learning new MAC for 1
minute or so, this would further result to, e.g. new instances (containers, VMs)
booted during period would report gateway unreachable.

The physical switch errors looked like this:

```shell
FWM-6-MAC_MOVE_NOTIFICATION: Host fa16.2ead.e6cf in vlan 2001 is flapping between port Po19 and port Po22
FWM-6-MAC_MOVE_NOTIFICATION: Host fa16.2ead.e6cf in vlan 2001 is flapping between port Po22 and port Po19
```

## 2 Infra & Environment Info

This section provides some basic infrastructure information to help understand
the problem. For more detailed information, please refer to my previous post [Ctrip
Network Architecture Evolution in the Cloud Computing Era]({% link _posts/2019-04-17-ctrip-network-arch-evolution.md %}).

The data center network utilizes a 3-tier architecture, TOR switches are **NOT**
stacked (stacking, e.g, Cisco vPC).

<p align="center"><img src="/assets/img/ovs-forwards-unicast-flooding/2-1.png" width="45%" height="45%"></p>
<p align="center">Fig. 2.1 Physical topology</p>

Others:

* OVS version: `2.5.6` (contrast group: `2.3.1`) 
* OVS bond mode: `balance-slb`, with `rebalance` turned off (`rebalance-interval=0`)
* Linux Kernel: `4.14`

# 3 Trouble Shooting

## 3.1 Confirm: `balance-slb` mode bond forwards unicast flooding packets

As the problem appeared only since recently, we went through all our
configuration and software changes within this period. At last, we suspected
that upgrading OVS from 2.3.1 to 2.5.6 maybe the most potential cause.

We also found **several similar reports [1,2]**, unfortunately these ended
with no conclusions. But at this point, we were ensure that we were not the only
user encountered this problem.

Digging into the [documentation](https://ovs-reviews.readthedocs.io/en/latest/topics/bonding.html#slb-bonding):

> When the remote switch has not learned the MAC for the destination of a
> unicast packet and hence floods the packet to all of the links on the SLB
> bond, Open vSwitch will forward duplicate packets, one per link, to each other
> switch port.
>
> Open vSwitch does not solve this problem.

It clearly says that it **dose behave in this way**.

## 3.2 Verify/Capture

Then we'd like to capture the occurrance when this problem happens.
We used the following script to capture packets on an OVS `2.5.6` node:

```shell
$ cat capture.sh
#!/bin/bash

timeout 36000 tcpdump -i eth0 --direction=in  -s 16 -w eth0-in.pcap &
timeout 36000 tcpdump -i eth1 --direction=out -s 16 -w eth1-out.pcap &

timeout 36000 tcpdump -i eth1 --direction=in  -s 16 -w eth1-in.pcap &
timeout 36000 tcpdump -i eth0 --direction=out -s 16 -w eth0-out.pcap &
```

As we had a large traffic on NICs, we only captured the `ether+IP`
headers of each packet (`-s 16`: first 16 bytes); at the meantime, we separate
the traffic into two diretions:

1. TOR0 -> eth0 -> OVS -> eth1 -> TOR1
1. TOR1 -> eth1 -> OVS -> eth2 -> TOR0

So there are totally 4 captured files.

**According to our observation, this problem was occasional** (this means not
all unicast flooding packets would be forwarded, but I haven't dig further into
this just some observation). We run 4 hours until the problem happened again on
this host (let's call it `HostA`), resulted total 30GB pcap files.

When problem happened, MAC `fa:16:3e:b2:2e:27` was learned by TOR from this
`HostA`'s eth1 interface, while this MAC actually belongs to a container on
another host (`HostB`). So it is a unicast flooding traffic (we further
confirmed that the `dst_mac` belongs to a container that's not on `HostA`).

Check this in the pcap files:

```shell
$ tcpdump -e -r eth0-in.pcap | grep fa:16:3e:b2:2e:27
15:10:40.110642 fa:16:3e:b2:2e:27 (oui Unknown) > fa:16:3e:d1:15:17, length 64: [|vlan]
15:10:40.110891 fa:16:3e:d1:15:17 (oui Unknown) > fa:16:3e:b2:2e:27, length 78: [|vlan]
15:10:40.111090 fa:16:3e:b2:2e:27 (oui Unknown) > fa:16:3e:d1:15:17, length 78: [|vlan]
15:10:40.111118 fa:16:3e:b2:2e:27 (oui Unknown) > fa:16:3e:d1:15:17, length 78: [|vlan]
15:10:40.111214 fa:16:3e:b2:2e:27 (oui Unknown) > fa:16:3e:d1:15:17, length 78: [|vlan]
15:10:40.111275 fa:16:3e:b2:2e:27 (oui Unknown) > fa:16:3e:d1:15:17, length 78: [|vlan]
15:10:40.111299 fa:16:3e:b2:2e:27 (oui Unknown) > fa:16:3e:d1:15:17, length 78: [|vlan]
15:10:40.292307 fa:16:3e:d1:15:17 (oui Unknown) > fa:16:3e:b2:2e:27, length 78: [|vlan]

$ tcpdump -e -r eth1-out.pcap | grep fa:16:3e:b2:2e:27
15:10:40.111248 fa:16:3e:b2:2e:27 (oui Unknown) > fa:16:3e:d1:15:17, length 78: [|vlan]
```

Indeed, it was received from `eth0` and forwared to `eth1` then sent out. The
process looked like Fig 2.1 (may not be that accurate, as I'm not very familir
with some data center network details):

<p align="center"><img src="/assets/img/ovs-forwards-unicast-flooding/2-2.png" width="45%" height="45%"></p>
<p align="center">Fig. 2.2 OVS bond forwarded unicast flooding traffic, resulting a L2 loop which further caused MAC flapping</p>

## 3.3 Why `2.3.1` is OK?

Checking 2.3.1 documentation, the `balance-slb` mode description is the same: it
clearly says it will forward unicast flooding, but why we never encounter this
problem in 2.3.1?

Unfortunately, I haven't found any answer to this problem. But a code diff might
provide some hints:

```shell
$ git diff v2.3.1 v2.5.6 -- ofproto/ofproto-dpif-xlate.c | grep -C 2 xlate_normal_flood
+
+static void
+xlate_normal_flood(struct xlate_ctx *ctx, struct xbundle *in_xbundle,
+                   uint16_t vlan)
+{
--
+            } else {
+                xlate_report(ctx, "multicast traffic, flooding");
+                xlate_normal_flood(ctx, in_xbundle, vlan);
+            }
+            return;
--
+            } else {
+                xlate_report(ctx, "MLD query, flooding");
+                xlate_normal_flood(ctx, in_xbundle, vlan);
+            }
+        } else {
--
+                 * be forwarded on all ports */
+                xlate_report(ctx, "RFC4541: section 2.1.2, item 2, flooding");
+                xlate_normal_flood(ctx, in_xbundle, vlan);
+                return;
+            }
--
+            if (mcast_snooping_flood_unreg(ms)) {
+                xlate_report(ctx, "unregistered multicast, flooding");
+                xlate_normal_flood(ctx, in_xbundle, vlan);
+            } else {
+                xlate_normal_mcast_send_mrouters(ctx, ms, in_xbundle, vlan);
--
+        } else {
+            xlate_report(ctx, "no learned MAC for destination, flooding");
+            xlate_normal_flood(ctx, in_xbundle, vlan);
         }
-        ctx->xout->nf_output_iface = NF_OUT_FLOOD;
```

We can see that `2.5` indeed added some flooding related logic, especially this:

```c
+            xlate_report(ctx, "no learned MAC for destination, flooding");
+            xlate_normal_flood(ctx, in_xbundle, vlan);
```

## 3.4 Fixup/Workaround

To continue with `2.5.6`, we have to switch the bond to `active-backup` mode
before we have better solutions:

```shell
$ ovs-vsctl set Port <bond> lacp=off bond-mode=active-backup
```

Change back to `balance-slb` if you need:

```shell
$ ovs-vsctl set Port <bond> bond_mode=balance-slb
$ ovs-vsctl set port <bond> other_config:bond-rebalance-interval=0
```

# 4 Summary

**OVS bond assumes** that all its slave devices (physical NICs) are **connected to a single
logical switch**, that involves some vendor-specific stacking technologies, such
as [Cisco vPC](https://community.cisco.com/t5/switching/cisco-vpc-vs-stack/td-p/3079750).
Unfortunately not all physical networks included that, such as our case.

On the other hand, `2.5.x` seems to have broken `2.3.x` behaviors, although
`2.3.x` should already behave like `2.5.x` according to its documentation.

## References

1. [Openvswitch retransmits ARP unicast packets on incoming port](https://bugzilla.redhat.com/show_bug.cgi?id=1557405)
2. [OVS 2.5.1 in an LACP bond does not correctlyhandleunicast flooding](https://mail.openvswitch.org/pipermail/ovs-discuss/2017-February/043789.html)
3. [Cisco vPC vs Stack - Cisco Community](https://community.cisco.com/t5/switching/cisco-vpc-vs-stack/td-p/3079750)
4. [Broadcast, unknown-unicast and multicast traffic - Wikipedia](https://en.wikipedia.org/wiki/Broadcast,_unknown-unicast_and_multicast_traffic)
