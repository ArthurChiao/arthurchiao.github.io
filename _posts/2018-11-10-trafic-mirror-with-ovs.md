---
layout: post
title:  "Traffic Mirroring With OVS"
date:   2018-11-10
author: ArthurChiao
categories: traffic-mirror OVS docker container
---

Traffic mirroring, or port mirroring, is a technique to send a copy of the
network traffic to a backend system. It is commonly used by network
administrators and developers who need to monitor the network traffic, or
diagnose network problems. Systems relying on traffic mirror include: intrusion
detection system (IDS), passive probe, real user monitoring (RUM), traffic
replay system, etc. [1]

Traditional traffic mirroring solutions are based on hardware switches, and are
vendor-specific, which often found expensive, hard to use and evolve.
With the growing of cloud computing and network virtualization,
software-based and vendor-agnostic solutions emerge.

In this article, we will investigate the feasibility of traffic mirroring based
on OVS [2].

## 1 Preliminary Knowledge

### 1.1 OVS Bridge

An OVS bridge is a software implemented Layer 2 (L2) bridge.

### 1.2 OVS Port

A port is a virtual device which could be used as a NIC by applications to
send/receive packets.

An OVS port could be many types, for example:

1. physical NIC
1. tap device
1. a veth pair
1. OVS internal port, patch port

### 1.3 OVS Port Mirroring

OVS provides a [way](http://docs.openvswitch.org/en/latest/faq/configuration/)
to duplicate network traffic of specified ports to a dedicated output port.
The duplication could be in single direction (ingress/egress) or both.

### 1.4 Container Network Basics

This article needs some container basics, see our previous post
[Play With Container Network Interface]({% link _posts/2018-11-07-play-with-container-network-if.md %})
if you are unfamilir with this topic.

## 2 Mirror Traffic With OVS

This post referenced an earlier blog [Port mirroring with Linux bridges](https://backreference.org/2014/06/17/port-mirroring-with-linux-bridges/). Some syntax sugar of OVS CLI got well explained there, we do not repeat them here.

For simplicity, we wrapped some shell commands into (really) simple scripts. You
could find them in the appendix.

### 2.1 Prepare Environment

Refer to appendix A or our post [4] for how to setup a test environment like this:

<p align="center"><img src="/assets/img/traffic-mirror-with-ovs/net-topo.png" width="45%" height="45%"></p>

### 2.2 Create Mirror

First, add a device as the output port of mirrored traffic. We use OVS internal
port here:

```shell
$ ./add-port.sh mirror0-output

$ ovs-vsctl show | grep mirror0-output
        Port "mirror0-output"
            Interface "mirror0-output"

$ ovs-vsctl get port mirror0-output _uuid
d74a2e7e-5d66-4a48-9261-7d080444964f
```

then configure our mirror:

```shell
$ ./add-mirror.sh br-int mirror0 mirror0-output
762fe791-bf4a-4180-b0f8-d22627d3970f

$ ovs-vsctl list mirror
name                : "mirror0"
output_port         : d74a2e7e-5d66-4a48-9261-7d080444964f
select_all          : false
select_dst_port     : []
select_src_port     : []
statistics          : {tx_bytes=0, tx_packets=0}
```

### 2.3 Add Traffic Source

Mirror only the egress traffic:

```shell
$ ./add-traffic-source.sh mirror0 vnic-1 egress

$ ./list-mirror.sh
_uuid               : 762fe791-bf4a-4180-b0f8-d22627d3970f
name                : "mirror0"
output_port         : d74a2e7e-5d66-4a48-9261-7d080444964f
select_all          : false
select_dst_port     : []
select_src_port     : [29a6d786-5ad2-4e8a-afdb-2f099fbe0de1]
statistics          : {tx_bytes=448, tx_packets=7}
```

All ports whose `egress` traffic are being mirrored are shown in the `select_src_port` list,
and those whose `ingress` being mirrored shown in `select_dst_port` list.

Verify that `29a6d786-5ad2-4e8a-afdb-2f099fbe0de1` is just `vnic-1`'s UUID:

```shell
$ ovs-vsctl get port vnic-1 _uuid
29a6d786-5ad2-4e8a-afdb-2f099fbe0de1
```

You could also mirror both ingress or both:

```shell
$ ./add-traffic-source.sh mirror0 vnic-1 ingress

$ ./add-traffic-source.sh mirror0 vnic-1 all
```

Now our network topology:

<p align="center"><img src="/assets/img/traffic-mirror-with-ovs/net-topo-with-output.png" width="45%" height="45%"></p>

## 3 Test

### 3.1 Setup Capture

To test the mirror, we will generate and capture ICMP packets.

Use `tcpdump` to capture ICMP packets on `vnic-1`:

```shell
$ ip netns exec 21580 tcpdump -n -i vnic-1 'icmp'
listening on vnic-1, link-type EN10MB (Ethernet), capture size 65535 bytes
```

this is equivalent to log into `ctn-1` with `docker exec -it ctn-1 sh` and run
the `tcpdump` command in it.

Capture on `mirror0-output`:

```shell
$ tcpdump -n -i mirror0-output 'icmp'
tcpdump: WARNING: mirror0-output: no IPv4 address assigned
listening on mirror0-output, link-type EN10MB (Ethernet), capture size 65535 bytes
```

**No packets on both.**

### 3.2 Generate Traffic

Now ping `ctn-2` (`192.168.1.4`) from `ctn-1` (`192.168.1.3`), which will send
ICMP echo request and receive ICMP echo reply packets:

```shell
$ docker exec -it ctn-1 sh

/ # ping 192.168.1.4
PING 192.168.1.4 (192.168.1.4): 56 data bytes
64 bytes from 192.168.1.4: seq=0 ttl=64 time=0.460 ms
64 bytes from 192.168.1.4: seq=1 ttl=64 time=0.079 ms
64 bytes from 192.168.1.4: seq=2 ttl=64 time=0.084 ms
...
```

Output of `vnic-1`:

```
$ ip netns exec 21580 tcpdump -n -i vnic-1 'icmp'
14:55:01.011274 IP 192.168.1.3 > 192.168.1.4: ICMP echo request, id 5376, seq 0, length 64
14:55:01.011567 IP 192.168.1.4 > 192.168.1.3: ICMP echo reply, id 5376, seq 0, length 64
14:55:02.011385 IP 192.168.1.3 > 192.168.1.4: ICMP echo request, id 5376, seq 1, length 64
14:55:02.011551 IP 192.168.1.4 > 192.168.1.3: ICMP echo reply, id 5376, seq 1, length 64
14:55:03.011490 IP 192.168.1.3 > 192.168.1.4: ICMP echo request, id 5376, seq 2, length 64
14:55:03.011537 IP 192.168.1.4 > 192.168.1.3: ICMP echo reply, id 5376, seq 2, length 64
```

Output of `mirror0-output`:

```shell
$ tcpdump -n -i mirror0-output 'icmp'
14:55:01.011467 IP 192.168.1.3 > 192.168.1.4: ICMP echo request, id 5376, seq 0, length 64
14:55:02.011529 IP 192.168.1.3 > 192.168.1.4: ICMP echo request, id 5376, seq 1, length 64
14:55:03.011506 IP 192.168.1.3 > 192.168.1.4: ICMP echo request, id 5376, seq 2, length 64
```

**As we expected, only the egress traffic ov `ctn-1` get mirrored to the output port.**

If you chose `ingress` or `both` in Section 3.2, results will be diffrent accordingly.

### 3.3 Performance Test

The ICMP traffic we generated here is just used to verify the mirror works.

To do performance benchmark, you should consider more powerful traffic
generating tools, e.g. [iperf3](https://iperf.fr/iperf-download.php), [Pktgen-DPDK](https://pktgen-dpdk.readthedocs.io/en/latest/).
Or, if you would like to replay a captured traffic, which may come from real environments,
consider [tcprelay](https://tcpreplay.appneta.com).

Quick Commands:

```shell
# send 100 packets
$ tcpreplay --limit=100 -i eth1 test.pcap

# loop 10 times, 2x faster
$ tcpreplay --loop=10 -x 2 -i eth1 test.pcap
```

### 3.4 Add More Traffic Source

Several ports may be mirrored to the same output, for example, to add `vnic-2`'s
egress to the mirror:

```shell
$ ./add-traffic-source.sh mirror0 vnic-2 egress

$ ./list-mirror.sh
_uuid               : 762fe791-bf4a-4180-b0f8-d22627d3970f
name                : "mirror0"
output_port         : d74a2e7e-5d66-4a48-9261-7d080444964f
select_all          : false
select_dst_port     : []
select_src_port     : [29a6d786-5ad2-4e8a-afdb-2f099fbe0de1, e1ba2b63-f492-4f88-8c3e-e6fcb22c0008]
statistics          : {tx_bytes=448, tx_packets=7}
```

### 3.4 Cleanup

Delete all mirrors on a bridge, run:

```shell
$ ./del-mirrors.sh
```

Delete output port:


```
$ ovs-vsctl del-port mirror0-output
```

## 4 Summary

In this post, we showed how to implement a simply traffic mirror environment
with OVS port mirroring function.

## References

1. [Wikipedia: Port Mirroring](https://en.wikipedia.org/wiki/Port_mirroring)
2. [OpenvSwitch]()
3. [Port mirroring with Linux bridges](https://backreference.org/2014/06/17/port-mirroring-with-linux-bridges/)
4. [Play With Container Network Interface]({% link _posts/2018-11-07-play-with-container-network-if.md %})
5. [OVS FAQ: Port Mirroring](http://docs.openvswitch.org/en/latest/faq/configuration/)
6. [Tool: tcpreplay](https://tcpreplay.appneta.com)
7. [Pktgen-DPDK](https://pktgen-dpdk.readthedocs.io/en/latest/)
8. [iperf3](https://iperf.fr/iperf-download.php)

## Appendix A: Setup Environment

Create OVS Bridge:

```shell
$ ovs-vsctl add-br br-int
```

With simple scripts from [Play With Container Network Interface]({% link _posts/2018-11-07-play-with-container-network-if.md %}):

```shell
$ docker run -d --name ctn-1 library/alpine:3.5 sleep 3600d
$ docker run -d --name ctn-2 library/alpine:3.5 sleep 3600d

$ ./expose-netns.sh ctn-1
$ ./expose-netns.sh ctn-2
$ ip netns
21978 (id: 18)
21580 (id: 17)

$ ./add-nic.sh ctn-1 vnic-1 br-int
$ ./add-nic.sh ctn-2 vnic-2 br-int

$ ip netns exec 21580 ifconfig vnic-1 192.168.1.3 netmask 255.255.255.0 up
$ ip netns exec 21978 ifconfig vnic-2 192.168.1.4 netmask 255.255.255.0 up
```

Check:

```shell
$ ovs-vsctl show
    Bridge "br-int"
        ...
        Port "vnic-1"
            Interface "vnic-1"
                type: internal
        Port "vnic-2"
            Interface "vnic-2"
                type: internal
```

## Appendix B: Scripts Used In This Article

1. [add-mirror.sh](/assets/img/traffic-mirror-with-ovs/add-mirror.sh)
1. [add-port.sh]({/assets/img/traffic-mirror-with-ovs/add-port.sh)
1. [add-traffic-source.sh](/assets/img/traffic-mirror-with-ovs/add-traffic-source.sh)
1. [del-mirrors.sh](/assets/img/traffic-mirror-with-ovs/del-mirrors.sh)
1. [del-mirror.sh](/assets/img/traffic-mirror-with-ovs/del-mirror.sh)
1. [del-port.sh](/assets/img/traffic-mirror-with-ovs/del-port.sh)
1. [list-mirror.sh](/assets/img/traffic-mirror-with-ovs/list-mirror.sh)
