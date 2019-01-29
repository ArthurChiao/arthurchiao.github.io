---
layout: post
title:  "OVS Deep Dive 5: Datapath and TX Offloading"
date:   2017-03-08
categories: OVS
---

## 1. TX Offloading

For performance considerations, instances (VMs, containers, etc) often offload
the checksum job (TCP, UDP checksums, etc) to physical NICs. You could check
the offload settings with `ethtool`:

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

Fig.1.1 is a deployment graph, OVS manages both physical ports (NICs) and
virtual ports (vNIC) of VMs.

<p align="center"><img src="/assets/img/ovs-deep-dive/ovs-instances-attached.jpg" width="50%" height="50%"></p>
<p align="center">Fig.1.1 OVS Deployment</p>

When TX offload is enabled on instance's tap devices (default setting), OVS will
utilize physical NICs for checksum calculating for each packet sent out from
instance, and this is handled by the kernel module `openvswitch.ko`.

## 2. TX Offloading with Userspace Datapath

**A problem occurs when TX offloading is enabled while OVS bridge uses Userspace
Datapath: Userspace Datapath does not support TX offloading**. What's more
tricky is that OVS doesn't complaining or report any errors in log for this
configuration.  I'm not sure if all userspace datapath implementations do not
support TX offloading, but according to my tests, at least OVS 2.3~2.7 Userspace
datapath does not support. The phenomenon of this problem is that **instances
connected with OVS could not establish TCP connection**, while L3 (e.g `ping`)
and `UDP` connections are OK.


Let's see an example: ssh from instance A to instance B, where A and B connect
to the same OVS bridge which uses Userspace datapath. Here, instance A has
an ip address 10.18.100.6 on its tap device `test5`, instance B with ip
`10.18.100.7` on its tap device `test6`.

First, to make sure the L3 connection is OK, we do a ping from A to B:

```
root@instance-A: ping 10.18.100.7
PING 10.18.100.7 (10.18.100.7) 56(84) bytes of data.
64 bytes from 10.18.100.7: icmp_seq=1 ttl=255 time=0.592 ms
```

OK. Using `telnet` or `nc`, we could further verify that UDP connection is OK
between A and B. Then let's try `ssh`:

```
root@instance-A: ssh 10.18.100.7
```

**Stucked!** Look at the `tcpdump` outputs:

```shell
root@host-02:~  # tcpdump -vv -i test5 'ip'
10:37:54.957911 IP (tos 0x0, ttl 64, id 18862, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.6.33340 > 10.18.100.7.ssh: Flags [S], cksum 0xdc5f (incorrect -> 0x2efd), seq 2593635005, win 29200, options [mss 1460,sackOK,TS val 455443 ecr 0,nop,wscale 7], length 0
10:37:55.957995 IP (tos 0x0, ttl 64, id 18863, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.6.33340 > 10.18.100.7.ssh: Flags [S], cksum 0xdc5f (incorrect -> 0x2b14), seq 2593635005, win 29200, options [mss 1460,sackOK,TS val 456444 ecr 0,nop,wscale 7], length 0
```

```shell
root@dpdk-02:~  # tcpdump -vv -i test6 'ip'
10:37:54.958093 IP (tos 0x0, ttl 64, id 18862, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.6.33340 > 10.18.100.7.ssh: Flags [S], cksum 0xdc5f (incorrect -> 0x2efd), seq 2593635005, win 29200, options [mss 1460,sackOK,TS val 455443 ecr 0,nop,wscale 7], length 0
10:37:55.958054 IP (tos 0x0, ttl 64, id 18863, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.6.33340 > 10.18.100.7.ssh: Flags [S], cksum 0xdc5f (incorrect -> 0x2b14), seq 2593635005, win 29200, options [mss 1460,sackOK,TS val 456444 ecr 0,nop,wscale 7], length 0
```

We could see that the first TCP packet (**SYNC**) arrives instance B, but is
marked as `cksum incorrect` then discarded.  The reason is that A enables TCP TX
offloading, so the packet is sent to OVS with faked TCP checksum; while OVS with
Userspace Datapath does not do TCP checksum, the packet is sent out (or
forwarded to B) with wrong checksum; on receiving this packet, B calculates the
TCP checksum and finds that the TCP checksum field in packet is incorrect, then
discards it directly.  A retransmits the packet after timeout (1 second), but
the packet still get dropped by B. This process is depicted in Fig.2.1.

<p align="center"><img src="/assets/img/ovs-deep-dive/tcp_conn_1.png" width="70%" height="70%"></p>
<p align="center">Fig.2.1 TCP Connection Establishment: TX offload enabled on A and B</p>

---

If we turn off TCP TX offload on A:

```shell
root@instance-A: ethtool -K eth0 tx off
```

watch `tcpdump` outputs:

```shell
root@host-02:~  # tcpdump -vv -i test5 'ip'
10:53:45.980674 IP (tos 0x0, ttl 64, id 57556, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.6.33344 > 10.18.100.7.ssh: Flags [S], cksum 0xc048 (correct), seq 2884605223, win 29200, options [mss 1460,sackOK,TS val 1406451 ecr 0,nop,wscale 7], length 0
10:53:45.981241 IP (tos 0x0, ttl 64, id 0, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.7.ssh > 10.18.100.6.33344: Flags [S.], cksum 0xdc5f (incorrect -> 0xa8f1), seq 3399142456, ack 2884605224, win 28960, options [mss 1460,sackOK,TS val 1409357 ecr 1406451,nop,wscale 7], length 0
10:53:46.981289 IP (tos 0x0, ttl 64, id 57557, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.6.33344 > 10.18.100.7.ssh: Flags [S], cksum 0xbc5f (correct), seq 2884605223, win 29200, options [mss 1460,sackOK,TS val 1407452 ecr 0,nop,wscale 7], length 0
10:53:46.981770 IP (tos 0x0, ttl 64, id 0, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.7.ssh > 10.18.100.6.33344: Flags [S.], cksum 0xdc5f (incorrect -> 0xa508), seq 3399142456, ack 2884605224, win 28960, options [mss 1460,sackOK,TS val 1410358 ecr 1406451,nop,wscale 7], length 0
```

```shell
root@dpdk-02:~  # tcpdump -vv -i test6 'ip'
10:53:45.980793 IP (tos 0x0, ttl 64, id 57556, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.6.33344 > 10.18.100.7.ssh: Flags [S], cksum 0xc048 (correct), seq 2884605223, win 29200, options [mss 1460,sackOK,TS val 1406451 ecr 0,nop,wscale 7], length 0
10:53:45.981046 IP (tos 0x0, ttl 64, id 0, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.7.ssh > 10.18.100.6.33344: Flags [S.], cksum 0xdc5f (incorrect -> 0xa8f1), seq 3399142456, ack 2884605224, win 28960, options [mss 1460,sackOK,TS val 1409357 ecr 1406451,nop,wscale 7], length 0
10:53:46.981420 IP (tos 0x0, ttl 64, id 57557, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.6.33344 > 10.18.100.7.ssh: Flags [S], cksum 0xbc5f (correct), seq 2884605223, win 29200, options [mss 1460,sackOK,TS val 1407452 ecr 0,nop,wscale 7], length 0
10:53:46.981639 IP (tos 0x0, ttl 64, id 0, offset 0, flags [DF], proto TCP (6), length 60)
    10.18.100.7.ssh > 10.18.100.6.33344: Flags [S.], cksum 0xdc5f (incorrect -> 0xa508), seq 3399142456, ack 2884605224, win 28960, options [mss 1460,sackOK,TS val 1410358 ecr 1406451,nop,wscale 7], length 0
```

the first TCP packet would be correctly received by B; then, an **SYNC+ACK** packet
is sent from B to A. On receiving, A discards this ACK packet because of
the same reason: TCP checksum incorrect, as shown in Fig.2.2.

<p align="center"><img src="/assets/img/ovs-deep-dive/tcp_conn_2.png" width="70%" height="70%"></p>
<p align="center">Fig.2.2 TCP Connection Establishment: TX offload enabled B</p>

---

Further, turn off TX offload in B:

```shell
root@instance-B: ethtool -K eth0 tx off
```

This time, TCP conncection will be established right away:

<p align="center"><img src="/assets/img/ovs-deep-dive/tcp_conn_3.png" width="70%" height="70%"></p>
<p align="center">Fig.2.3 TCP Connection Establishment: TX offload diabled in A and B</p>

## 3. Conclusion

**Why does Userspace Datapath not support TX offloading?**

As far as I could figure out[1,2], Userspace Datapath is highly optimized which
conflicts with TX offloading. Some optimizations have to be sacrificed if
supporting TX offloading. However, the benefits is not obvious, or [even
worse](https://mail.openvswitch.org/pipermail/ovs-dev/2016-August/322058.html).
RX offloading could achieve ~10% perfomance increase, so in Userspace Datapth,
RX offloading is enabled by default, but TX offloading is not supported.

In summary, **if deploy OVS with DPDK enabled, you have to turn off TX offload
flags in your instances**. In this case, instances themselves will take care
of the checksum calculating.

## References
1. [netdev-dpdk: Enable Rx checksum offloading	feature on DPDK physical ports](https://mail.openvswitch.org/pipermail/ovs-dev/2016-August/322058.html)
2. [OVS Hardware Offload Discuss Panel](http://openvswitch.org/support/ovscon2016/7/1450-stringer.pdf)
