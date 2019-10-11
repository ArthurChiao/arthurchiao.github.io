---
layout    : post
title     : "Behind the TCP Handshakes in Modern Networking Infrastructures"
date      : 2019-10-11
lastupdate: 2019-10-11
categories: tcp handshake service-mesh istio
---

## TCP 3-way Handshake

In its simplest form, TCP 3-way handshake is easy to understand, and there are
plenty of online materials talking about this. (You could check out one of my
[previous post]({% link _posts/2018-12-14-tcpdump-practice-zh.md  %}) if you could read Chinease.)

However, understanding, practising and trouble shooting TCP issues in the real
world is another matter. As container platforms begin to dominate the world, as
well as service-mesh's emerging as the next major shift of the underlying
networking infrastructure, modern networking falicities in these platforms make
TCP related problems even more complicated. In traditional views, those problems
may look fairly weired.

This article will show two of such scenarios.

# 1. Scenario 1

## 1.1 Phenomenon: SYN -> SYN+ACK -> RST

Client initiated a connection to server, server immediately acked
(SYN+ACK), but client reset this packet on receiving it, and kept waiting
for next SYN+ACK from server.

## 1.2 Capture

The tcpdump output:

```shell
1 18:56:40.353352 IP 10.4.26.45.35582 > 10.4.26.234.80: Flags [S],  seq 853654705, win 29200, length 0
2 18:56:40.353506 IP 10.4.26.11.80 > 10.4.26.45.35582:  Flags [S.], seq 914414059, ack 853654706, win 28960, length 0
3 18:56:40.353521 IP 10.4.26.45.35582 > 10.4.26.11.80:  Flags [R],  seq 853654706, win 0, length 0
4 18:56:41.395322 IP 10.4.26.45.35582 > 10.4.26.234.80: Flags [S],  seq 853654705, win 29200, length 0
5 18:56:41.395441 IP 10.4.26.11.80 > 10.4.26.45.35582:  Flags [S.], seq 930694343, ack 853654706, win 28960, length 0
6 18:56:41.395457 IP 10.4.26.45.35582 > 10.4.26.11.80:  Flags [R],  seq 853654706, win 0, length 0
```

where,

* Client: `10.4.26.45`
* Server: `10.4.26.234`, providing HTTP service at port `80`

What's the problem? Think about this before you proceed on.

## 1.3 Analysis

Let's try to understand what's happend in depth:

1. `#1`: client initiated a connection to server, with `src_port=35582,dst_port=80`
2. `#2`: server acked (SYN+ACK)
3. `#3`: client reset the server's SYN+ACK packet
4. `#4`: `#1` timed out, client retransmits it
5. `#5`: server acked `#4` (still SYN+ACK)
6. `#6`: client rejected again (`#5`, SYN+ACK)

At first look, this seems fairly strange, because server acked client's request,
while client immediately reset this packet on receiving, then kept waiting for next
SYN+ACK from server (instead of closing this connecting attempt). It
even retransmitted the first SYN packet on timeout (noticed by that `#4`
uses the same temporary port as `#1` do).

## 1.4 Root Cause

Pay attention to this: client assumed the server is at `10.4.26.234`, why the
SYN+ACK packets (`#2` and `#4`) came from `10.4.26.11`? By some investigations,
we found that: the server was deployed as a K8S **ExternalIP Service**, with
`10.4.26.11` as the VIP (ExternalIP), and `10.4.26.234` as the PodIP.

### 1.4.1 The Short Answer

Client connected to server with server's VIP as destination IP, but server
(instance) replied with its real IP (PodIP). IP mismatch made client believing
that the SYN+ACK packets were invalid, so it rejected them.

### 1.4.2 The Long Answer

First of all, we are in a Cilium powered K8S cluster.
Cilium will generate BPF rules to load balance the traffics to this VIP
to all its backend Pods. The normal traffic path looks like Fig 1.1:

<p align="center"><img src="/assets/img/tcp-handshake-in-modern-network-infra/1-1.png" width="70%" height="70%"></p>
<p align="center">Fig. 1.1 Normal data flow between client and server instances</p>

1. @Client: client sends traffic to server `VIP`
2. @ClientHost: Cilium does DNAT, change VIP to one of its `PodIP` (backend
   instance IP)
3. @ServerHost: traffic routed to server instance whose IP is `PodIP`
4. @Server: server instance reply with its own `PodIP`
5. @ServerHost: route reply packet to client host
6. @ClientHost: Cilium does SNAT, change server's `PodIP` to `VIP`, then forward
   traffic to client instance
7. @Client: client receives traffic. From its own viewpoint, the `src_ip` of the
   received packet is just the `dst_ip` of the previous sent packet (both are
   `VIP`), so it accepts the packet. 3-way handshake finish.

The problem arises when **client and server are on the same host**, in which
case, **step 6 is not implemented by Cilium**, as shown in Fig 1.2:

<p align="center"><img src="/assets/img/tcp-handshake-in-modern-network-infra/1-2.png" width="45%" height="45%"></p>
<p align="center">Fig. 1.2 Data flow when client and server are on the same host</p>

We have reported this problem and it is confirmed a bug, see [this
issue](https://github.com/cilium/cilium/issues/9285) for more details.

# 2. Scenario 2

## 2.1 Phenomenon: Handshake OK, Connection Reset on Transmit Data

Client initiated a TCP connection to server succesfully (3 packets), however, on
sending the first data packet (the 4th packet in total), the connection got
reset by server immediately.

## 2.2 Capture

```shell
1 12:10:30.083284 IP 10.6.2.2.51136 > 10.7.3.3.8080: Flags [S],  seq 1658620893, win 29200, length 0
2 12:10:30.083513 IP 10.6.3.3.8080 > 10.7.2.2.51136: Flags [S.], seq 2918345428, ack 1658620894, win 28960, length 0
3 12:10:30.083612 IP 10.6.2.2.51136 > 10.7.3.3.8080: Flags [.],  ack 1, win 229, length 0
4 12:10:30.083899 IP 10.6.2.2.51136 > 10.7.3.3.8080: Flags [P.], seq 1:107, ack 1, win 229, length 106
5 12:10:30.084038 IP 10.6.3.3.8080 > 10.7.2.2.51136: Flags [.],  ack 107, win 227, length 0
6 12:10:30.084251 IP 10.6.3.3.8080 > 10.7.2.2.51136: Flags [R.], seq 1, ack 107, win 227, length 0
```

Again, it's worth to think about this before proceed on.

## 2.3 Analysis

1. `#1`: client initiated a connection to server, `src_port=51136,dst_port=8080`
2. `#2`: server acked (SYN+ACK)
3. `#3`: client acked server, **TCP connection succesfully established**
4. `#4`: client sent a `106` byte data packet
5. `#5`: server acked `#4`
6. `#6`: server reset this connection right after `#5`

## 2.4 Root Cause

Client sees a topology like Fig 2.1:

<p align="center"><img src="/assets/img/tcp-handshake-in-modern-network-infra/2-1.png" width="55%" height="55%"></p>
<p align="center">Fig. 2.1 Client view of the two sides</p>

It initiated an connection, which got accepted by server succesfully, namely,
the 3-way handshake finished withouth any error. But on transmitting data,
server immediately rejected this connection. So, the problem must reside in the
server side.

Digging into the server side, we found that a sidecar (envoy, to be specific)
was injected for the server side container. If you are not familir with this
word, please refer to some introductory documents of [Istio](https://istio.io).
In short words, the sidecar serves as a middle man between server container and
the outside world:

* on ingress direction, it intercepts all ingress traffic to server, do some
  processing, then forwards the allowed traffic to server
* on egress direction, it intercepts all egress traffic from server, again do
  some processing, and forwards the allowed traffic to outside world.

> The traffic interception is implemented with iptables rules in Istio.
> Explanation of the detailed implementations is beyong the scope of this post,
> but you could refer to the figure in Appendix A if you are interested.

So this is the magic: the connection is not established between client and
server directly, but **split into 2 separate connections**:

1. connection between client and sidecar
2. connection between sidecar and server

Those two connections are independently handshaked, thus even if the latter
failed, the former could still be succesful.

<p align="center"><img src="/assets/img/tcp-handshake-in-modern-network-infra/2-2.png" width="55%" height="55%"></p>
<p align="center">Fig. 2.2 Actual view of the two sides: a middleman sits between client and server</p>

This is what exactly happened: server failed to start due to some internal
errors, but the connection between client and sidecar was established. When
client began to send data packets, sidecar first acked for receiving, then
forwarded this to (the failed) server, and got rejected. It then realized that
the backend service was not available, so closed (RST) the connection between
itself and the client.

<p align="center"><img src="/assets/img/tcp-handshake-in-modern-network-infra/2-3.png" width="55%" height="55%"></p>
<p align="center">Fig. 2.3 Connection between sidecar and server not established</p>

# 3. Closing Remarks

In modern days, the underlying network infrastructures are increasingly powerful
and flexible, but comes at a price of deeper stack depth, and poses more
challenges on developers and maintainers for trouble shooting.  This inevitablly
requires more in-depth knowledge on the network infrastructures, virtualization
technologies, kernel stack, etc.

# 4. Appendix A: Istio Sidecar Interception

<p align="center"><img src="/assets/img/tcp-handshake-in-modern-network-infra/4-1.png" width="70%" height="70%"></p>
<p align="center">Fig. 4.1 Istio sidecar interception (inbound) with iptables rules</p>

Corresponding iptables rules:

```shell
# get the Pod netns
$ docker inspect <Container ID or Name> | grep \"Pid\"
            "Pid": 82881,

# show iptables rules in Pod netns
$ nsenter -t 82881 -n iptables -t nat -nvL
Chain PREROUTING (policy ACCEPT 1725 packets, 104K bytes)
 pkts bytes target     prot opt in     out     source               destination
 2086  125K ISTIO_INBOUND  tcp  --  *      *       0.0.0.0/0            0.0.0.0/0

Chain INPUT (policy ACCEPT 2087 packets, 125K bytes)
 pkts bytes target     prot opt in     out     source               destination

Chain OUTPUT (policy ACCEPT 465 packets, 29339 bytes)
 pkts bytes target     prot opt in     out     source               destination
  464 27840 ISTIO_OUTPUT  tcp  --  *      *       0.0.0.0/0            0.0.0.0/0

Chain POSTROUTING (policy ACCEPT 498 packets, 31319 bytes)
 pkts bytes target     prot opt in     out     source               destination

Chain ISTIO_INBOUND (1 references)
 pkts bytes target     prot opt in     out     source               destination
  362 21720 ISTIO_IN_REDIRECT  tcp  --  *      *       0.0.0.0/0            0.0.0.0/0            tcp dpt:8080

Chain ISTIO_IN_REDIRECT (1 references)
 pkts bytes target     prot opt in     out     source               destination
  362 21720 REDIRECT   tcp  --  *      *       0.0.0.0/0            0.0.0.0/0            redir ports 15001

Chain ISTIO_OUTPUT (1 references)
 pkts bytes target     prot opt in     out     source               destination
    0     0 ISTIO_REDIRECT  all  --  *      lo      0.0.0.0/0           !127.0.0.1
  420 25200 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            owner UID match 1337
    0     0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            owner GID match 1337
   11   660 RETURN     all  --  *      *       0.0.0.0/0            127.0.0.1
   33  1980 ISTIO_REDIRECT  all  --  *      *       0.0.0.0/0            0.0.0.0/0

Chain ISTIO_REDIRECT (2 references)
 pkts bytes target     prot opt in     out     source               destination
   33  1980 REDIRECT   tcp  --  *      *       0.0.0.0/0            0.0.0.0/0            redir ports 15001
```
