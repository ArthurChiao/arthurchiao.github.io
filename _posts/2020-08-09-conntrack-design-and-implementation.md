---
layout    : post
title     : "Connection Tracking (conntrack): Design and Implementation Inside Linux Kernel"
date      : 2020-08-09
lastupdate: 2021-04-26
categories: conntrack nat netfilter kernel
---

> Note: this post also provides a
> [Chinese version]({% link _posts/2020-08-05-conntrack-design-and-implementation-zh.md %}).

* TOC
{:toc}

## Abstract

This post talks about connection tracking (conntrack, CT), as well as its design
and implementation inside Linux kernel.

Code analysis based on `4.19`. For illustration purposes, only the core
logics are preserved in all pasted code. Source files are provided for
each code piece, refer to them if you need.

Note that I'm not a kernel developer, so there may be mistakes in this
post. Glad if anyone corrects me.

# 1 Introduction

Connection tracking is the basis of many network services and applications. For
example, [Kubernetes Service](https://kubernetes.io/docs/concepts/services-networking/service/),
[ServiceMesh sidecar](https://istio.io/latest/docs/reference/config/networking/sidecar/),
software layer 4 load balancer (L4LB) [LVS/IPVS](https://en.wikipedia.org/wiki/Linux_Virtual_Server),
[Docker network](https://docs.docker.com/network/bridge/),
[OpenvSwitch (OVS)](http://docs.openvswitch.org/en/latest/tutorials/ovs-conntrack/),
OpenStack [security group](https://docs.openstack.org/nova/queens/admin/security-groups.html) (host firewall),
etc, all rely on it.

## 1.1 Concepts

As the name illustrates itself, connection tracking **<mark>tracks (and maintains) connections and their states</mark>**.

<p align="center"><img src="/assets/img/conntrack/node-conntrack.png" width="40%" height="40%"></p>
<p align="center">Fig 1.1 Connection tracking example on a Linux node</p>

For example, in Fig 1.1, the Linux node has an IP address `10.1.1.2`,
we could see 3 connections on this node:

1. `10.1.1.2:55667 <-> 10.2.2.2.:80`: **locally originated** connection for accessing external HTTP/TCP service
2. `10.3.3.3:23456 <-> 10.3.3.2.:21`: **externally originated** connection for accessing FTP/TCP service in this node
3. `10.1.1.2:33987 <-> 10.4.4.4.:53`: **locally originated** connection for accessing external DNS/UDP service

Conntrack module is responsible for **discovering and recording these connections
and their statuses**, including:

* Extract `tuple` from packets, distinguish `flow` and the related `connection`.
* Maintain a **"database"** (`conntrack table`) for all connections, deposit
  information such as connection's created time, packets sent, bytes sent, etc.
* Garbage collecting (GC) stale connection info
* Serve for upper layer functionalities, e.g. NAT

But note that, the term **"connection" in "connection tracking"** is different
from the **"connection" concept that we usually mean in TCP/IP stack**. Put it simply,

* In TCP/IP stack, "connection" is a layer 4 (transport layer) concept.
    * TCP is a connection-oriented protocol, all packets need to be acknowledged
      (ACK), and there is retransmission mechanism.
    * UDP is a connectionless protocol, acknowledgement (ACK) is not required,
      no retransmission either.
* In connection tracking, a `tuple` uniquely defines a `flow`, and a flow
  represents a `connection`.
    * We will see later that UDP, or **even ICMP (layer 3 protocol) have connection entries**.
    * But **not all protocols will be connection tracked**.

When refering to the term "connection", we mean the latter one in most cases,
namely, the "connection" in "connection tracking" context.

## 1.2 Thoery

With the above concepts in mind, let's reasons about the underlying theory of
connction tracking.

To track the states of all connections on a node, we need to,

1. **Hook (or filter) every packet** that passes through this node, and **analyze the packet**.
2. **Setup a "database"** for recoding the status of those connections (conntrack table).
3. **Update connection status timely** to database based on the extracted information from hooked packets.

For example,

1. When hooked a TCP `SYNC` packet, we could confirm that a new connection
   attempt is under the way, so we need to create a new conntrack entry to
   record this connection.
2. When got a packet that belongs to an existing connection, we need to update
   the conntrack entry statistics, e.g. bytes sent, packets sent, timeout
   value, e.g.
3. When no packets match a conntrack entry for more than 30 minutes, we
   consider to delete this entry from the database.

Except the above functional requirements, performance requirements also
also deserve our concern, as conntrack module filters and analyzes every single
packet. Performance considerations are fairly important, but they are beyond the
scope of this post. We will come back to performance issues again when walking through
the kernel conntrack implementation later.

Besides, it's better to have some management tools to faciliate the using of
conntrack.

## 1.3 Design: Netfilter

Connction tracking in Linux kernel is **implemented as a module** in [Netfilter](https://en.wikipedia.org/wiki/Netfilter) framework.

<p align="center"><img src="/assets/img/conntrack/netfilter-design.png" width="50%" height="50%"></p>
<p align="center">Fig 1.2 Netfilter architecture inside Linux kernel</p>

[Netfilter](https://en.wikipedia.org/wiki/Netfilter) is a packet manipulating
and filtering framework inside the kernel. It provides several hooking
points inside the kernel, so packet hooking, filtering and many other processings
could be done.

> Put it more clearly, hooking is a mechanism that places several checking points
> in the travesal path of packets. When a packet arrives a hooking point, it
> first gets checked, and the checking result could be one of:
>
> 1. let it go: no modifications to the packet, push it back to the original travesal path and let it go
> 2. modify it: e.g. replace network address (NAT), then push back to the original travesal path and let it go
> 3. drop it: e.g. by firewall rules configured at this checking (hooking) point
>
> Note that conntrack module only extracts connection information and maintains
> its database, it does not modify or drop a packet. Modification and dropping
> are done by other modules, e.g. NAT.

Netfilter is one of the earliest networking frameworks inside Linux kernel, it
initially got developed in 1998, and merged into `2.4.x` kernel mainline in
2000.

After more than 20 years' evolvement, it gets so complicated that results
to **degraded performance** in certain scenarios, we will talk a little more
about this later.

## 1.4 Design: further considerations

From our discussions in the previous section, **the concept of connection tracking is
independent from Netfilter**, and the latter is only one of the implementations
of connection tracking.

In other words, **as long as the hooking capability is possessed** - the ability to
hook every single packet that goes through the system -  **we could implement
our own connection trakcing**.

<p align="center"><img src="/assets/img/conntrack/cilium-conntrack.png" width="50%" height="50%"></p>
<p align="center">Fig 1.3. Cilium's conntrack and NAT architectrue</p>

[Cilium](https://github.com/cilium/cilium), a cloud native networking solution
for Kubernetes, implements such a conntrack and NAT mechanism. The underlyings
of the impelentation:

1. Hook packets based on BPF hooking points (BPF's equivalent part of the Netfilter hooks)
2. Implement a completely new conntrack & NAT module based on BPF hooks (need kernel `4.19+` to be fully functional)

So, one could even [remove the entire Netfilter module](https://github.com/cilium/cilium/issues/12879)
, and Cilium would still work for
Kubernetes functionalities such as ClusterIP, NodePort, ExternalIPs and
LoadBalancer [2].

As this connction tracking implementation is independent from Netfilter, its
conntrack and NAT entries are not stored in system's (namely, Netfilter's)
conntrack table and NAT table. So frequently used network tools
`conntrack/netstats/ss/lsof` could not list them, you must use Cilium's
alternatives, e.g:

```shell
$ cilium bpf nat list
$ cilium bpf ct list global
```

Also, configurations are also independent, you need to specify Cilium's
configuration parameters, such as command line argument `--bpf-ct-tcp-max`.

We clarified that conntrack concept is independent from NAT module, but **for
performance considerations, the code may be coupled**. For
example, when performing GC for conntrack table, it will efficiently remove
related entries in NAT table, rather than maintaining a separate GC loop for NAT
table.

## 1.5 Use cases

Let's see some specific network applications/functions that are on top of
conntrack.

### 1.5.1 Network address translation (NAT)

As the name illustrates, **NAT** translates (packets') network addresses (`IP + Port`).

<p align="center"><img src="/assets/img/conntrack/node-nat.png" width="40%" height="40%"></p>
<p align="center">Fig 1.4 NAT for node local IP addresses</p>

For example, in the above Fig, assume node IP `10.1.1.2` is reachable from other
nodes, while IP addresses within network `192.168.1.0/24` are not reachable. This
indicates:

1. Packets with source IPs in `192.168.1.0/24` **could be sent out**, as egress routing only relies on destination IP.
2. But, the **reply packets** (with destination IPs falling into `192.168.1.0/24`)
   **could not come back**, as `192.168.1.0/24` is not routable within nodes.

One solution for this scenario:

1. On sending packets with source IPs falling into `192.168.1.0/24`,
   replace these source IPs (and/or ports) with node IP `10.1.1.2`, then send out.
2. On receiving reply packets, do the reverse translation, then forward traffic
   to the original senders.

This is just the underlying working mechanism of NAT.

The default network mode of Docker, `bridge` network mode, uses NAT in the same
way as above [4]. Within the node, each Docker container allocates a
node local IP address. This address enables communications between containers
within the node, but when communicates with services outside the node,
the traffic will be NAT-ed.

> NAT may replace the source ports as well. It's not too hard to understand: each
> IP address can use the full port range (e.g. 1~65535). So assume
> we have two connections:
>
> * 192.168.1.2:3333 <--> NAT <--> 10.2.2.2:80
> * 192.168.1.3:3333 <--> NAT <--> 10.2.2.2:80
>
> if NAT only replaces source IP addresses to node IP, the above two distinct
> connections after NAT will be:
>
> * 10.1.1.2:3333 <--> 10.2.2.2:80
> * 10.1.1.2:3333 <--> 10.2.2.2:80
>
> which mixed into a same connection that could not be distinguished and the
> reverse translation will fail. So NAT also replaces source ports if collision
> happens.

NAT can be further categorized as:

* SNAT: do translation on source address
* DNAT: do translation on destination address
* Full NAT: do translation on both source and destination addresses

Our above examples falls into SNAT case.

**NAT relies on the results of connection tracking**, and, NAT the most
important use case of connection tracking.

#### Layer 4 load balancing (L4LB)

Let's step a little further from above discussions, to the topic of layer 4 load
balancing **(L4LB) under NAT mode**.

L4 LB distributes traffic acorrding to packets' L3+L4 info, e.g. `src/dst ip, src/dst port, proto`.

**VIP (Virtual IP)** is one of the mechanisms/designs to implement L4LB:

* Multiple backend nodes with distinct Real IPs register to the same virtual IP (VIP)
* Traffic from clients first arrives VIP, then be load balanced to specific
  backend IPs

If L4LB uses NAT mode (between VIP and Real IPs), L4LB will perform full
NAT between client-server traffic, the data flow depicted as picture below:

<p align="center"><img src="/assets/img/conntrack/nat.png" width="60%" height="60%"></p>
<p align="center">Fig 1.5 L4LB: traffic path in NAT mode [3]</p>

### 1.5.2 Stateful firewall

Stateful firewall is relative to the **stateless firewall** in the early days.
With stateless firewall, one could only apply simple rules like
`drop syn to port 443` or `allow syn to port 80`, and it has no concept of flow. It's impossible to
configure a rule like **"allow this `ack` if `syn` has been seen, otherwise drop
it"**, so the funtionality was quite limited [6].

Apparently, to provide a stateful firewall, one must track flow and states -
which is just what conntrack is doing.

Let's see a more specific example: OpenStack security group - its host firewall solution.

#### OpenStack security group

Security group provides **VM-level** security isolation, which is realized by
applying stateful firewall rules on the host-side network devices of the VMs.
And at that time, the most mature candicates for stateful firewalling might be
Netfilter/iptables.

Now back to the network topology inside each compute node: each compute node
connects (integrates) all VMs inside it with an OVS bridge (`br-int`).
If only considering network connectivity, each VM should attach to `br-int`
directly. But here comes the problem [7]:

* (Early versions) OVS has **no conntrack module**,
* Linux kernel has conntrack, but the firewall based on this **works at IP layer (L3)**, manipulated via iptables,
* **OVS is a L2 module**, which means it could not utilize L3 modules,

As a result, firewalling at the OVS (node-side) network devices of VMs is impossible.

OpenStack solved this problem by **inserting a Linux bridge between every VM and `br-int`**, as shown below:

<p align="center"><img src="/assets/img/conntrack/ovs-compute.png" width="60%" height="60%"></p>
<p align="center">Fig 1.6. Network topology within an OpenStack compute node,
picture from <a href="https://thesaitech.wordpress.com/2017/09/24/how-to-trace-the-tap-interfaces-and-linux-bridges-on-the-hypervisor-your-openstack-vm-is-on/"> Sai's Blog</a></p>

<mark>Linux bridge is also a L2 module, so it could not use iptables</mark>.
But, **it has a L2 filtering machanism
called ebtables, which could jump to iptables rules**, and thus makes
Netfilter/iptables workable.

But this workaround is ugly, and leads to significant performance issues. So in 2016, RedHat
proposed an OVS conntrack solution [7]. Until then, people could possibly turn off
Linux bridge while still having security group funtionality.

## 1.6 Summary

This ends our discussion on the theory of connection tracking, and
in the following, we will dig into the kernel implementation.

# 2 Implementation: Netfilter hooks

Netfilter consists of several modules:

1. conntrack: kernel module
2. NAT: kernel module
3. iptables: userspace tools

## 2.1 Netfilter framework

### The 5 hooking points

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/hooks.png" width="60%" height="60%"></p>
<p align="center"> Fig 2.1 The 5 hook points in netfilter framework</p>

As shown in the above picture, Netfilter provides 5 hooking points in the packet
travesing path inside Linux kernel:

```c
// include/uapi/linux/netfilter_ipv4.h

#define NF_IP_PRE_ROUTING    0 /* After promisc drops, checksum checks. */
#define NF_IP_LOCAL_IN       1 /* If the packet is destined for this box. */
#define NF_IP_FORWARD        2 /* If the packet is destined for another interface. */
#define NF_IP_LOCAL_OUT      3 /* Packets coming from a local process. */
#define NF_IP_POST_ROUTING   4 /* Packets about to hit the wire. */
#define NF_IP_NUMHOOKS       5
```

Users could register callback functions (handlers) at these points. When a
packet arrives at the hook point, it will triger the related handlers being called.

> There is another definition for these `NF_INET_*` variables, in `include/uapi/linux/netfilter.h`.
> These two definitions are equivalent, from the comments in the code, `NF_IP_*`
> are probably used for backward compatibility.
>
> ```c
> enum nf_inet_hooks {
>     NF_INET_PRE_ROUTING,
>     NF_INET_LOCAL_IN,
>     NF_INET_FORWARD,
>     NF_INET_LOCAL_OUT,
>     NF_INET_POST_ROUTING,
>     NF_INET_NUMHOOKS
> };
> ```

### Hook handler return values

hook handlers return a verdict result after checking against a given packet, and
this verdict will guide next processings on this packet. Possible verdict
results include:

```c
// include/uapi/linux/netfilter.h

#define NF_DROP   0  // the packet has been dropped this packet in handler
#define NF_ACCEPT 1  // the packet is no dropped, continue following processing
#define NF_STOLEN 2  // silently holds the packet until something happends, no further processing is needed
                     // usually used to collect fragmented packets (for later assembling)
#define NF_QUEUE  3  // the packet should be pushed into queue
#define NF_REPEAT 4  // current handler should be called again against the packet
```

### Hook handler priorities

Multiple handlers could be registered to the same hooking point.

When register a handler, a priority parameter must be provided. So that when a
packet arrives at this point, the system could call these handlers in order
with their priorities.

## 2.2 Filtering rules management

`iptables` is the userspace tool for configuring Netfilter hooking capabilities.
To faciliate management, it splits rules into several tables by functionalities:

* raw
* filter
* nat
* mangle

This is not the focus of this post, more information on this, refer to
[A Deep Dive into iptables and Netfilter Architecture](https://www.digitalocean.com/community/tutorials/a-deep-dive-into-iptables-and-netfilter-architecture).

# 3 Implementation: Netfilter conntrack

conntrack module traces the **<mark>connection status of trackable protocols</mark>** [1].
That is, connection tracking targets at **specific protocols**, not all. We will see later what protocols it supports.

## 3.1 Data structures and functions

Key data structures:

* `struct nf_conntrack_tuple {}`: **defines a `tuple`**.
    * `struct nf_conntrack_man {}`: the manipulatable part of a tuple
        * `struct nf_conntrack_man_proto {}`: the protocol specific part in tuple's manipulatable part
* `struct nf_conntrack_l4proto {}`: a collection of **<mark>methods</mark>** a trackable
  protocol needs to implement (and other fields).
* `struct nf_conntrack_tuple_hash {}`: **defines a conntrack entry** (value)
  stored in hash table (conntrack table), hash key is a `uint32` integer
  computed from tuple info.
* `struct nf_conn {}`: **<mark>defines a flow</mark>**.

Key functions:

* `hash_conntrack_raw()`: calculates a 32bit hash key from tuple info.
* `nf_conntrack_in()`: **core of the conntrack module, the entrypoint of connection tracking**.
* `resolve_normal_ct() -> init_conntrack() -> ct = __nf_conntrack_alloc(); l4proto->new(ct)`

    Create a new conntrack entry, then init it with protocol-specific method.

* `nf_conntrack_confirm()`: confirms the new connection that previously created via `nf_conntrack_in()`.

## 3.2 `struct nf_conntrack_tuple {}`: Tuple

Tuple is one of the most important concepts in connection tracking.

**A tuple uniquely defines a unidirectional flow**, this is clearly explained in
kernel code comments:

> //include/net/netfilter/nf_conntrack_tuple.h
>
> A `tuple` is a structure containing the information to uniquely
> identify a connection.  ie. if two packets have the same tuple, they
> are in the same connection; if not, they are not.

### Data structure definitions

To facilite NAT module's implementation, kernel splits the tuple structure into
"manipulatable" and "non-manipulatable" parts.

> In the following code, `_man` is short for `manipulatable`, which is a bad
> naming/abbreviating from today's point of view.

```
// include/net/netfilter/nf_conntrack_tuple.h
                                               // ude/uapi/linux/netfilter.h
                                               union nf_inet_addr {
                                                   __u32            all[4];
                                                   __be32           ip;
                                                   __be32           ip6[4];
                                                   struct in_addr   in;
                                                   struct in6_addr  in6;
/* manipulatable part of the tuple */       /  };
struct nf_conntrack_man {                  /
    union nf_inet_addr           u3; -->--/
    union nf_conntrack_man_proto u;  -->--\
                                           \   // include/uapi/linux/netfilter/nf_conntrack_tuple_common.h
    u_int16_t l3num; // L3 proto            \  // protocol specific part
};                                            union nf_conntrack_man_proto {
                                                  __be16 all;/* Add other protocols here. */

                                                  struct { __be16 port; } tcp;
                                                  struct { __be16 port; } udp;
                                                  struct { __be16 id;   } icmp;
                                                  struct { __be16 port; } dccp;
                                                  struct { __be16 port; } sctp;
                                                  struct { __be16 key;  } gre;
                                              };

struct nf_conntrack_tuple { /* This contains the information to distinguish a connection. */
    struct nf_conntrack_man src;  // source address info, manipulatable part
    struct {
        union nf_inet_addr u3;
        union {
            __be16 all;     // Add other protocols here

            struct { __be16 port;         } tcp;
            struct { __be16 port;         } udp;
            struct { u_int8_t type, code; } icmp;
            struct { __be16 port;         } dccp;
            struct { __be16 port;         } sctp;
            struct { __be16 key;          } gre;
        } u;
        u_int8_t protonum;  // The protocol
        u_int8_t dir;       // The direction (for tuplehash)
    } dst;                       // destination address info
};
```

**<mark>There are only 2 fields (src and dst) inside struct nf_conntrack_tuple {}</mark>**,
each stores source and destination address information.

But `src` and `dst` are themselves also structs, storing protocol specific data.
Take IPv4 UDP as example, information of the 5-tuple stores in the following
fields:

* `dst.protonum`: protocol type (`IPPROTO_UDP`)
* `src.u3.ip`: source IP address
* `dst.u3.ip`: destination IP address
* `src.u.udp.port`: source UDP port number
* `dst.u.udp.port`: destination UDP port number

### Connection-trackable protocols

From the above code, we could see that **only 6 protocols support connection
tracking currently**: TCP, UDP, ICMP, DCCP, SCTP, GRE.

**<mark>Pay attention to the ICMP protocol</mark>**. People may think that connction tracking is
done by hashing over L3+L4 headers of packets, while ICMP is a L3 protocol, so it
could not be conntrack-ed. But actually it could be, from the above code, we see
that the **`type` and `code` fields in ICMP header** are used for defining a
tuple and performing subsequent hashing.

## 3.3 `struct nf_conntrack_l4proto {}`: methods trackable protocols need to implement

Protocols that support connection tracking need to implement the methods defined
in `struct nf_conntrack_l4proto {}`, for example `pkt_to_tuple()`, which
extracts tuple information from given packet's L3/L4 header.

```c
// include/net/netfilter/nf_conntrack_l4proto.h

struct nf_conntrack_l4proto {
    u_int16_t l3proto; /* L3 Protocol number. */
    u_int8_t  l4proto; /* L4 Protocol number. */

    // extract tuple info from given packet (skb)
    bool (*pkt_to_tuple)(struct sk_buff *skb, ... struct nf_conntrack_tuple *tuple);

    // returns verdict for packet
    int (*packet)(struct nf_conn *ct, const struct sk_buff *skb ...);

    // create a new conntrack, return TRUE if succeeds.
    // if returns TRUE, packet() method will be called against this skb later
    bool (*new)(struct nf_conn *ct, const struct sk_buff *skb, unsigned int dataoff);

    // determin if this packet could be conntrack-ed.
    // if could, packet() method will be called against this skb later
    int (*error)(struct net *net, struct nf_conn *tmpl, struct sk_buff *skb, ...);

    ...
};
```

## 3.4 `struct nf_conntrack_tuple_hash {}`: conntrack entry

conntrack modules stores active connections in a hash table:

* key: 32bit value calculated from tuple info
* value: conntrack entry (`struct nf_conntrack_tuple_hash {}`)

Method `hash_conntrack_raw()` calculates a 32bit hash key from tuple info:

```c
// net/netfilter/nf_conntrack_core.c

static u32 hash_conntrack_raw(struct nf_conntrack_tuple *tuple, struct net *net)
{
    get_random_once(&nf_conntrack_hash_rnd, sizeof(nf_conntrack_hash_rnd));

    /* The direction must be ignored, so we hash everything up to the
     * destination ports (which is a multiple of 4) and treat the last three bytes manually.  */
    u32 seed = nf_conntrack_hash_rnd ^ net_hash_mix(net);
    unsigned int n = (sizeof(tuple->src) + sizeof(tuple->dst.u3)) / sizeof(u32);

    return jhash2((u32 *)tuple, n, seed ^ ((tuple->dst.u.all << 16) | tuple->dst.protonum));
}
```

Pay attention to the calculating logic, and how source and destination address
fields are used for the final hash value.

Conntrack entry is defined as `struct nf_conntrack_tuple_hash {}`:

```c
// include/net/netfilter/nf_conntrack_tuple.h

// Connections have two entries in the hash table: one for each way
struct nf_conntrack_tuple_hash {
    struct hlist_nulls_node   hnnode;   // point to the related connection `struct nf_conn`,
                                        // list for fixing hash collisions
    struct nf_conntrack_tuple tuple;
};
```

## 3.5 `struct nf_conn {}`: connection

**<mark>Each flow in Netfilter is called a connection</mark>**, even for those connectionless
protocols (e.g. UDP). A connection is defined as `struct nf_conn {}`, with
important fields as follows:

```c
// include/net/netfilter/nf_conntrack.h

                                                  // include/linux/skbuff.h
                                        ------>   struct nf_conntrack {
                                        |             atomic_t use;  // refcount?
                                        |         };
struct nf_conn {                        |
    struct nf_conntrack            ct_general;

    struct nf_conntrack_tuple_hash tuplehash[IP_CT_DIR_MAX]; // conntrack entry, array for ingress/egress flows

    unsigned long status; // connection status, see below for detailed status list
    u32 timeout;          // timer for connection status

    possible_net_t ct_net;

    struct hlist_node    nat_bysource;
                                                        // per conntrack: protocol private data
    struct nf_conn *master;                             union nf_conntrack_proto {
                                                       /    /* insert conntrack proto private data here */
    u_int32_t mark;    /* mark skb */                 /     struct nf_ct_dccp dccp;
    u_int32_t secmark;                               /      struct ip_ct_sctp sctp;
                                                    /       struct ip_ct_tcp tcp;
    union nf_conntrack_proto proto; ---------->----/        struct nf_ct_gre gre;
};                                                          unsigned int tmpl_padto;
                                                        };
```

**<mark>The collection of all possible connection states</mark>** in conntrack module, `enum ip_conntrack_status`:

```c
// include/uapi/linux/netfilter/nf_conntrack_common.h

enum ip_conntrack_status {
    IPS_EXPECTED      = (1 << IPS_EXPECTED_BIT),
    IPS_SEEN_REPLY    = (1 << IPS_SEEN_REPLY_BIT),
    IPS_ASSURED       = (1 << IPS_ASSURED_BIT),
    IPS_CONFIRMED     = (1 << IPS_CONFIRMED_BIT),
    IPS_SRC_NAT       = (1 << IPS_SRC_NAT_BIT),
    IPS_DST_NAT       = (1 << IPS_DST_NAT_BIT),
    IPS_NAT_MASK      = (IPS_DST_NAT | IPS_SRC_NAT),
    IPS_SEQ_ADJUST    = (1 << IPS_SEQ_ADJUST_BIT),
    IPS_SRC_NAT_DONE  = (1 << IPS_SRC_NAT_DONE_BIT),
    IPS_DST_NAT_DONE  = (1 << IPS_DST_NAT_DONE_BIT),
    IPS_NAT_DONE_MASK = (IPS_DST_NAT_DONE | IPS_SRC_NAT_DONE),
    IPS_DYING         = (1 << IPS_DYING_BIT),
    IPS_FIXED_TIMEOUT = (1 << IPS_FIXED_TIMEOUT_BIT),
    IPS_TEMPLATE      = (1 << IPS_TEMPLATE_BIT),
    IPS_UNTRACKED     = (1 << IPS_UNTRACKED_BIT),
    IPS_HELPER        = (1 << IPS_HELPER_BIT),
    IPS_OFFLOAD       = (1 << IPS_OFFLOAD_BIT),

    IPS_UNCHANGEABLE_MASK = (IPS_NAT_DONE_MASK | IPS_NAT_MASK |
                 IPS_EXPECTED | IPS_CONFIRMED | IPS_DYING |
                 IPS_SEQ_ADJUST | IPS_TEMPLATE | IPS_OFFLOAD),
};
```

## 3.6 `nf_conntrack_in()`: enter conntrack

<p align="center"><img src="/assets/img/conntrack/netfilter-conntrack.png" width="60%" height="60%"></p>
<p align="center">Fig 3.1 Conntrack points in Netfilter framework</p>

As illustrated in Fig 3.1, Netfilter performs connection tracking at 4 hooking
positions:

1. `PRE_ROUTING` and `LOCAL_OUT`

    **<mark>Start connection tracking</mark>** by calling `nf_conntrack_in()`, normally this
    will create a new conntrack entry, then insert it into **<mark>unconfirmed list</mark>**.

    Why starting at these two places? Because both of them are the **earliest
    points that the initial packet of a new connection arrives Netfilter
    framework**.

    * `PRE_ROUTING`: earliest point that the first packet of a new **externally-initiated** connection arrives.
    * `LOCAL_OUT`: earliest point that the first packet of a new **locally-initiated** connection arrives.

1. `POST_ROUTING` 和 `LOCAL_IN`

    Call `nf_conntrack_confirm()`, **<mark>move the newly created (in previous
    nf_conntrack_in()) connection</mark>** in unconfirmed list to **<mark>confirmed list</mark>**.

    Again, why these two hooking points? It is because if the first packet of a
    new connection is not dropped during internal processing, it should arrive
    these places and these are **the last place before it leaving Netfilter
    framework**:

    * For packet of **externally-initiated** connection, `LOCAL_IN` is the last hooking point
      before it is sent to upper layer application (e.g. a Nginx process).
    * For packet of **locally-initiated** connection, `POST_ROUTING` is the last hooking point
      before this packet is sent out on the wire.

We could see **<mark>how these handlers got registered into the netfilter framwork</mark>**:

```c
// net/netfilter/nf_conntrack_proto.c

/* Connection tracking may drop packets, but never alters them, so make it the first hook.  */
static const struct nf_hook_ops ipv4_conntrack_ops[] = {
    {
        .hook        = ipv4_conntrack_in,       // enter conntrack by calling nf_conntrack_in()
        .pf        = NFPROTO_IPV4,
        .hooknum    = NF_INET_PRE_ROUTING,     // PRE_ROUTING hook point
        .priority    = NF_IP_PRI_CONNTRACK,
    },
    {
        .hook        = ipv4_conntrack_local,    // enter conntrack by calling nf_conntrack_in()
        .pf        = NFPROTO_IPV4,
        .hooknum    = NF_INET_LOCAL_OUT,       // LOCAL_OUT hook point
        .priority    = NF_IP_PRI_CONNTRACK,
    },
    {
        .hook        = ipv4_confirm,            // call nf_conntrack_confirm()
        .pf        = NFPROTO_IPV4,
        .hooknum    = NF_INET_POST_ROUTING,    // POST_ROUTING hook point
        .priority    = NF_IP_PRI_CONNTRACK_CONFIRM,
    },
    {
        .hook        = ipv4_confirm,            // call nf_conntrack_confirm()
        .pf        = NFPROTO_IPV4,
        .hooknum    = NF_INET_LOCAL_IN,        // LOCAL_IN hook point
        .priority    = NF_IP_PRI_CONNTRACK_CONFIRM,
    },
};
```

Method `nf_conntrack_in()` is **<mark>the core of connection tracking module</mark>**.

```c
// net/netfilter/nf_conntrack_core.c

unsigned int
nf_conntrack_in(struct net *net, u_int8_t pf, unsigned int hooknum, struct sk_buff *skb)
{
  struct nf_conn *tmpl = nf_ct_get(skb, &ctinfo); // get related conntrack_info and conntrack entry
  if (tmpl || ctinfo == IP_CT_UNTRACKED) {        // if conntrack exists, or if is un-trackable protocols
      if ((tmpl && !nf_ct_is_template(tmpl)) || ctinfo == IP_CT_UNTRACKED) {
          NF_CT_STAT_INC_ATOMIC(net, ignore);     // no need to conntrack, inc ignore stats
          return NF_ACCEPT;                       // return NF_ACCEPT, continue normal processing
      }
      skb->_nfct = 0;                             // need to conntrack, reset refcount of this packet,
  }                                               // prepare for further processing

  struct nf_conntrack_l4proto
           *l4proto = __nf_ct_l4proto_find();    // extract protocol-specific L4 info

  if (l4proto->error(tmpl, skb, pf) <= 0) {      // skb validation
      NF_CT_STAT_INC_ATOMIC(net, error);
      NF_CT_STAT_INC_ATOMIC(net, invalid);
          goto out;
  }

repeat:
  resolve_normal_ct(net, tmpl, skb, ... l4proto); // start conntrack, extract tuple:
                                                  // create new conntrack or update existing one
  l4proto->packet(ct, skb, ctinfo);               // protocol-specific processing, e.g. update UDP timeout

  if (ctinfo == IP_CT_ESTABLISHED_REPLY && !test_and_set_bit(IPS_SEEN_REPLY_BIT, &ct->status))
      nf_conntrack_event_cache(IPCT_REPLY, ct);
out:
  if (tmpl)
      nf_ct_put(tmpl); // un-reference tmpl
}
```

Rough processing steps:

1. Get conntrack info of this skb.
1. Determine if conntrack is needed for this skb. If not needed, update ignore
   counter (check with `conntrack -S`), return `NF_ACCEPT`; if needed, init (reset) this skb's refcount.
1. Extract L4 information from skb, init protocol-specific `struct
   nf_conntrack_l4proto {}` variable, which contains the protocol's callback
   methods for performing connection tracking.
1. Call protocol-specific `error()` method for data validation, e.g. checksum.
1. **Start conntrack** by calling `resolve_normal_ct()` method, it
   will create new tuple, new conntrack entry, or update status of existing
   conntrack entry.
1. Call `packet()` method for some protocol-specific processing. E.g. for UDP,
   if `IPS_SEEN_REPLY` is set in status, it will update `timeout` value. timeout
   varies according to different protocols, the smaller it is, the more it will
   be stronger on anti-DDos attacks (DDos attacks a system by exausting all the
   system's connections).

## 3.7 `init_conntrack()`: create new conntrack entry

If connection does not exist yet (the first packet of a flow),
`resolve_normal_ct()` method will call `init_conntrack()`, and the latter will
further call protocol-specific method `new()` to create a new conntrack entry.

```c
// include/net/netfilter/nf_conntrack_core.c

// Allocate a new conntrack
static noinline struct nf_conntrack_tuple_hash *
init_conntrack(struct net *net, struct nf_conn *tmpl,
           const struct nf_conntrack_tuple *tuple,
           const struct nf_conntrack_l4proto *l4proto,
           struct sk_buff *skb, unsigned int dataoff, u32 hash)
{
    struct nf_conn *ct;

    // Allocate an entry in conntrack hash table, if the table is full, print following log
    // into kernel: "nf_conntrack: table full, dropping packet". Check with `dmesg -T`
    ct = __nf_conntrack_alloc(net, zone, tuple, &repl_tuple, GFP_ATOMIC, hash);

    l4proto->new(ct, skb, dataoff); // protocol-specific method for creating a conntrack entry

    local_bh_disable();             // disable softirq
    if (net->ct.expect_count) {
        exp = nf_ct_find_expectation(net, zone, tuple);
        if (exp) {
            __set_bit(IPS_EXPECTED_BIT, &ct->status);

            /* exp->master safe, refcnt bumped in nf_ct_find_expectation */
            ct->master = exp->master;

            ct->mark = exp->master->mark;
            ct->secmark = exp->master->secmark;
            NF_CT_STAT_INC(net, expect_new);
        }
    }

    /* Now it is inserted into the unconfirmed list, bump refcount */
    nf_conntrack_get(&ct->ct_general);
    nf_ct_add_to_unconfirmed_list(ct);
    local_bh_enable();              // re-enable softirq

    if (exp) {
        if (exp->expectfn)
            exp->expectfn(ct, exp);
        nf_ct_expect_put(exp);
    }

    return &ct->tuplehash[IP_CT_DIR_ORIGINAL];
}
```

Implementations of protocol-specific `new()` method, see `net/netfilter/nf_conntrack_proto_*.c`.
Such as TCP's one:

```c
// net/netfilter/nf_conntrack_proto_tcp.c

/* Called when a new connection for this protocol found. */
static bool tcp_new(struct nf_conn *ct, const struct sk_buff *skb, unsigned int dataoff)
{
    if (new_state == TCP_CONNTRACK_SYN_SENT) {
        memset(&ct->proto.tcp, 0, sizeof(ct->proto.tcp));
        /* SYN packet */
        ct->proto.tcp.seen[0].td_end = segment_seq_plus_len(ntohl(th->seq), skb->len, dataoff, th);
        ct->proto.tcp.seen[0].td_maxwin = ntohs(th->window);
        ...
}
```

If current packet will influence the status of subsequent packets,
`init_conntrack()` will set the `master` field in `struct nf_conn`.
Connection oriented protocols (e.g. TCP) uses this feature.

## 3.8 `nf_conntrack_confirm()`: confirm a new connection

The new conntrack entry that `nf_conntrack_in()` creates will be inserted into
an **unconfirmed connection list**.

If this packet is not dropped during intermediate processing, then when it
arrives `POST_ROUTING` hook, it will be further processed by
`nf_conntrack_confirm()` method. We have analyzed why it is further processed
here instead of other hooking points.

After `nf_conntrack_confirm()` is done, status of the connection will turn to
`IPS_CONFIRMED`, and the conntrack will be move from unconfirmed list to
**confirmed list**.

> Why bother to split the conntrack entry creating process into two stages (`new` and `confirm`)?
>
> Think about this: after the initial packet passed `nf_conntrack_in()`, but before
> arriving `nf_conntrack_confirm()`, it is possible that the packet get
> dropped by the kernel somewhere in the middle. This may result in large amounts of
> half-connected conntrack entries, and it would be a big concern in terms of
> both performance and security. Spliting into two steps will significantly
> accelerate the GC procedure.

```c
// include/net/netfilter/nf_conntrack_core.h

/* Confirm a connection: returns NF_DROP if packet must be dropped. */
static inline int nf_conntrack_confirm(struct sk_buff *skb)
{
    struct nf_conn *ct = (struct nf_conn *)skb_nfct(skb);
    int ret = NF_ACCEPT;

    if (ct) {
        if (!nf_ct_is_confirmed(ct))
            ret = __nf_conntrack_confirm(skb);
        if (likely(ret == NF_ACCEPT))
            nf_ct_deliver_cached_events(ct);
    }
    return ret;
}
```

confirm logic, error handling code omitted:

```c
// net/netfilter/nf_conntrack_core.c

/* Confirm a connection given skb; places it in hash table */
int
__nf_conntrack_confirm(struct sk_buff *skb)
{
    struct nf_conn *ct;
    ct = nf_ct_get(skb, &ctinfo);

    local_bh_disable();               // disable softirq

    hash = *(unsigned long *)&ct->tuplehash[IP_CT_DIR_REPLY].hnnode.pprev;
    reply_hash = hash_conntrack(net, &ct->tuplehash[IP_CT_DIR_REPLY].tuple);

    ct->timeout += nfct_time_stamp;   // update timer, will be GC-ed after timeout
    atomic_inc(&ct->ct_general.use);  // update conntrack entry refcount?
    ct->status |= IPS_CONFIRMED;      // set status as `confirmed`

    __nf_conntrack_hash_insert(ct, hash, reply_hash);  // insert into conntrack table

    local_bh_enable();                // re-enable softirq

    nf_conntrack_event_cache(master_ct(ct) ? IPCT_RELATED : IPCT_NEW, ct);
    return NF_ACCEPT;
}
```

One thing needs to be noted here: we could see **<mark>softirq (soft interrupt) gets
frequently disabled/re-enabled; besides, there are lots of lock/unlock
operations (omitted in the above code)</mark>**. This may be the main reasons of
degraded performance in certain scenarios (e.g. massive concurrent short-time
connections)?

# 4 Implementation: Netfilter NAT

NAT is a function module built upon conntrack module, it relies on connection
tracking's results to work properly.

Again, not all protocols supports NAT.

## 4.1 Data structures and functions

**Data structures:**

Protocols that support NAT needs to implement the methods defined in:

* `struct nf_nat_l3proto {}`
* `struct nf_nat_l4proto {}`

**Functions:**

* `nf_nat_inet_fn()`: **<mark>core of NAT module</mark>**, will be called at **<mark>all hooking points except NF_INET_FORWARD</mark>**.

## 4.2 NAT module init

```c
// net/netfilter/nf_nat_core.c

static struct nf_nat_hook nat_hook = {
    .parse_nat_setup    = nfnetlink_parse_nat_setup,
    .decode_session        = __nf_nat_decode_session,
    .manip_pkt        = nf_nat_manip_pkt,
};

static int __init nf_nat_init(void)
{
    nf_nat_bysource = nf_ct_alloc_hashtable(&nf_nat_htable_size, 0);

    nf_ct_helper_expectfn_register(&follow_master_nat);

    RCU_INIT_POINTER(nf_nat_hook, &nat_hook);
}

MODULE_LICENSE("GPL");
module_init(nf_nat_init);
```

## 4.3 `struct nf_nat_l3proto {}`: protocol specific methods

```c
// include/net/netfilter/nf_nat_l3proto.h

struct nf_nat_l3proto {
    u8    l3proto; // e.g. AF_INET

    u32     (*secure_port    )(const struct nf_conntrack_tuple *t, __be16);
    bool    (*manip_pkt      )(struct sk_buff *skb, ...);
    void    (*csum_update    )(struct sk_buff *skb, ...);
    void    (*csum_recalc    )(struct sk_buff *skb, u8 proto, ...);
    void    (*decode_session )(struct sk_buff *skb, ...);
    int     (*nlattr_to_range)(struct nlattr *tb[], struct nf_nat_range2 *range);
};
```

## 4.4 `struct nf_nat_l4proto {}`: protocol specific methods

`manip` is the abbraviation of `manipulate` in the code:

```c
// include/net/netfilter/nf_nat_l4proto.h

struct nf_nat_l4proto {
    u8 l4proto; // L4 proto id, e.g. IPPROTO_UDP, IPPROTO_TCP

    // Modify L3/L4 header according to the given tuple and NAT type (SNAT/DNAT)
    bool (*manip_pkt)(struct sk_buff *skb, *l3proto, *tuple, maniptype);

    // Create a unique tuple
    // e.g. for UDP, will generate a 16bit dst_port with src_ip, dst_ip, src_port and a rand
    void (*unique_tuple)(*l3proto, tuple, struct nf_nat_range2 *range, maniptype, struct nf_conn *ct);

    // If the address range is exhausted the NAT modules will begin to drop packets.
    int (*nlattr_to_range)(struct nlattr *tb[], struct nf_nat_range2 *range);
};
```

Implementations of these methods, see `net/netfilter/nf_nat_proto_*.c`. For
example, the TCP's implementation:

```c
// net/netfilter/nf_nat_proto_tcp.c

const struct nf_nat_l4proto nf_nat_l4proto_tcp = {
    .l4proto        = IPPROTO_TCP,
    .manip_pkt        = tcp_manip_pkt,
    .in_range        = nf_nat_l4proto_in_range,
    .unique_tuple        = tcp_unique_tuple,
    .nlattr_to_range    = nf_nat_l4proto_nlattr_to_range,
};
```

## 4.5 `nf_nat_inet_fn()`: enter NAT

`nf_nat_inet_fn()` will be called in following hooking points:

* `NF_INET_PRE_ROUTING`
* `NF_INET_POST_ROUTING`
* `NF_INET_LOCAL_OUT`
* `NF_INET_LOCAL_IN`

namely, all Netfilter hooking points except `NF_INET_FORWARD`.

Priorities at these hooking points: **Conntrack > NAT > Packet Filtering**.

**conntrack has a higher priority than NAT, since NAT relies on the results of
connection tracking**.

<p align="center"><img src="/assets/img/conntrack/hook-to-nat.png" width="60%" height="60%"></p>
<p align="center">Fig. NAT</p>

```c
unsigned int
nf_nat_inet_fn(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
    ct = nf_ct_get(skb, &ctinfo);
    if (!ct)    // exit NAT if conntrack not exist. This is why we say NAT relies on conntrack's results
        return NF_ACCEPT;

    nat = nfct_nat(ct);

    switch (ctinfo) {
    case IP_CT_RELATED:
    case IP_CT_RELATED_REPLY: /* Only ICMPs can be IP_CT_IS_REPLY.  Fallthrough */
    case IP_CT_NEW: /* Seen it before? This can happen for loopback, retrans, or local packets. */
        if (!nf_nat_initialized(ct, maniptype)) {
            struct nf_hook_entries *e = rcu_dereference(lpriv->entries); // obtain all NAT rules
            if (!e)
                goto null_bind;

            for (i = 0; i < e->num_hook_entries; i++) { // execute NAT rules in order
                if (e->hooks[i].hook(e->hooks[i].priv, skb, state) != NF_ACCEPT )
                    return ret;                         // return if any rule returns non ACCEPT verdict

                if (nf_nat_initialized(ct, maniptype))
                    goto do_nat;
            }
null_bind:
            nf_nat_alloc_null_binding(ct, state->hook);
        } else { // Already setup manip
            if (nf_nat_oif_changed(state->hook, ctinfo, nat, state->out))
                goto oif_changed;
        }
        break;
    default: /* ESTABLISHED */
        if (nf_nat_oif_changed(state->hook, ctinfo, nat, state->out))
            goto oif_changed;
    }
do_nat:
    return nf_nat_packet(ct, ctinfo, state->hook, skb);
oif_changed:
    nf_ct_kill_acct(ct, ctinfo, skb);
    return NF_DROP;
}
```

It first queries conntrack info for this packet, if conntrack info not exists,
it means this connection could not be tracked, then we could never perform NAT
for it. So just exit NAT in this case.

If conntrack info exists, and the connection is in
 `IP_CT_RELATED` or `IP_CT_RELATED_REPLY` or
`IP_CT_NEW` states, then get all NAT rules.

If found, execute `nf_nat_packet()` method, it will further call
protocol-specific `manip_pkt` method to modify the packet. If failed, the packet
will be dropped.

### Masquerade

NAT module could be configured in two fashions:

1. Normal: `change IP1 to IP2 if matching XXX`.
2. Special: `change IP1 to dev1's IP if matching XXX`, this is **<mark>a special case of SNAT, called masquerade</mark>**.

Pros & Cons:

* Masquerade differentiates itself from SNAT in that when device's IP address
  changes, the rules still valid. It could be seen as dynamic SNAT
  (dynamically adapting to the source IP changes in SNAT rules).
* The drawback of masquerade is that it has degraded performance compared with
  SNAT, and this is easy to understand.

## 4.6 `nf_nat_packet()`: performing NAT

```c
// net/netfilter/nf_nat_core.c

/* Do packet manipulations according to nf_nat_setup_info. */
unsigned int nf_nat_packet(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
               unsigned int hooknum, struct sk_buff *skb)
{
    enum nf_nat_manip_type mtype = HOOK2MANIP(hooknum);
    enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
    unsigned int verdict = NF_ACCEPT;

    statusbit = (mtype == NF_NAT_MANIP_SRC? IPS_SRC_NAT : IPS_DST_NAT)

    if (dir == IP_CT_DIR_REPLY)     // Invert if this is reply dir
        statusbit ^= IPS_NAT_MASK;

    if (ct->status & statusbit)     // Non-atomic: these bits don't change. */
        verdict = nf_nat_manip_pkt(skb, ct, mtype, dir);

    return verdict;
}
```

```c
static unsigned int nf_nat_manip_pkt(struct sk_buff *skb, struct nf_conn *ct,
                     enum nf_nat_manip_type mtype, enum ip_conntrack_dir dir)
{
    struct nf_conntrack_tuple target;

    /* We are aiming to look like inverse of other direction. */
    nf_ct_invert_tuplepr(&target, &ct->tuplehash[!dir].tuple);

    l3proto = __nf_nat_l3proto_find(target.src.l3num);
    l4proto = __nf_nat_l4proto_find(target.src.l3num, target.dst.protonum);
    if (!l3proto->manip_pkt(skb, 0, l4proto, &target, mtype)) // protocol-specific processing
        return NF_DROP;

    return NF_ACCEPT;
}
```

# 5. Configuration and monitoring

## 5.1 Inspect and load/unload nf_conntrack module

```shell
$ modinfo nf_conntrack
license:        GPL
alias:          nf_conntrack-10
alias:          nf_conntrack-2
alias:          ip_conntrack
srcversion:     30B45E5822722ACEDE23A4B
depends:        nf_defrag_ipv6,libcrc32c,nf_defrag_ipv4
retpoline:      Y
intree:         Y
name:           nf_conntrack
vermagic:       5.15.0-46-generic SMP mod_unload modversions
sig_id:         PKCS#7
signer:         Build time autogenerated kernel key
sig_key:        17:6F:92:2F:58:6B:B2:28:13:DC:71:DC:5A:97:EE:BA:D8:4B:C7:DE
sig_hashalgo:   sha512
signature:      0B:32:AA:93:F4:31:52:9C:FE:0D:80:B4:F6:7C:30:63:4C:F6:03:AA:
                ...
                E9:1F:45:C6:77:C2:29:99:B4:3D:1A:D2
parm:           tstamp:Enable connection tracking flow timestamping. (bool)
parm:           acct:Enable connection tracking flow accounting. (bool)
parm:           nf_conntrack_helper:Enable automatic conntrack helper assignment (default 0) (bool)
parm:           expect_hashsize:uint
parm:           enable_hooks:Always enable conntrack hooks (bool)
```

Remove the module:

```shell
$ rmmod nf_conntrack_netlink nf_conntrack
```

Load the module:

```shell
$ modprobe nf_conntrack

# Also support to pass configuration parameters, e.g.:
$ modprobe nf_conntrack nf_conntrack_helper=1 expect_hashsize=131072
```

## 5.2 sysctl options

```shell
$ sysctl -a | grep nf_conntrack
net.netfilter.nf_conntrack_acct = 0
net.netfilter.nf_conntrack_buckets = 262144                 # hashsize = nf_conntrack_max/nf_conntrack_buckets
net.netfilter.nf_conntrack_checksum = 1
net.netfilter.nf_conntrack_count = 2148
... # DCCP options
net.netfilter.nf_conntrack_events = 1
net.netfilter.nf_conntrack_expect_max = 1024
... # IPv6 options
net.netfilter.nf_conntrack_generic_timeout = 600
net.netfilter.nf_conntrack_helper = 0
net.netfilter.nf_conntrack_icmp_timeout = 30
net.netfilter.nf_conntrack_log_invalid = 0
net.netfilter.nf_conntrack_max = 1048576                    # conntrack table size
... # SCTP options
net.netfilter.nf_conntrack_tcp_be_liberal = 0
net.netfilter.nf_conntrack_tcp_loose = 1
net.netfilter.nf_conntrack_tcp_max_retrans = 3
net.netfilter.nf_conntrack_tcp_timeout_close = 10
net.netfilter.nf_conntrack_tcp_timeout_close_wait = 60
net.netfilter.nf_conntrack_tcp_timeout_established = 21600
net.netfilter.nf_conntrack_tcp_timeout_fin_wait = 120
net.netfilter.nf_conntrack_tcp_timeout_last_ack = 30
net.netfilter.nf_conntrack_tcp_timeout_max_retrans = 300
net.netfilter.nf_conntrack_tcp_timeout_syn_recv = 60
net.netfilter.nf_conntrack_tcp_timeout_syn_sent = 120
net.netfilter.nf_conntrack_tcp_timeout_time_wait = 120
net.netfilter.nf_conntrack_tcp_timeout_unacknowledged = 300
net.netfilter.nf_conntrack_timestamp = 0
net.netfilter.nf_conntrack_udp_timeout = 30
net.netfilter.nf_conntrack_udp_timeout_stream = 180
```

## 5.3 Monitoring

### conntrack statistics

```shell
$ cat /proc/net/stat/nf_conntrack
entries   searched found    new      invalid  ignore   delete   delete_list insert   insert_failed drop     early_drop icmp_error  expect_new expect_create expect_delete search_restart
000008e3  00000000 00000000 00000000 0000309d 001e72d4 00000000 00000000    00000000 00000000      00000000 00000000   000000ee    00000000   00000000      00000000       000368d7
000008e3  00000000 00000000 00000000 00007301 002b8e8c 00000000 00000000    00000000 00000000      00000000 00000000   00000170    00000000   00000000      00000000       00035794
000008e3  00000000 00000000 00000000 00001eea 001e6382 00000000 00000000    00000000 00000000      00000000 00000000   00000059    00000000   00000000      00000000       0003f166
...
```

There is also a command line tool `conntrack`:

```shell
$ conntrack -S
cpu=0   found=0 invalid=743150 ignore=238069 insert=0 insert_failed=0 drop=195603 early_drop=118583 error=16 search_restart=22391652
cpu=1   found=0 invalid=2004   ignore=402790 insert=0 insert_failed=0 drop=44371  early_drop=34890  error=0  search_restart=1225447
...
```

Fields:

* ignore: untracked packets (recall that only packets of trackable protocols will be tracked)

### conntrack table usage

Number of current conntrack entries:

```shell
$ cat /proc/sys/net/netfilter/nf_conntrack_count
257273
```

Number of max allowed conntrack entries:

```shell
$ cat /proc/sys/net/netfilter/nf_conntrack_max
262144
```

# 6. Conntrack related issues

## 6.1 nf_conntrack: table full

### Symptoms

#### Application layer symptoms

1. Probabilistic **<mark>connection timeout</mark>**.

    E.g. if the application is written in Java, the raised errors are `jdbc4.CommunicationsException` communications link failure, etc.

2. **<mark>Existing (e.g. established) connections</mark>** works normally.

    That is to say, there are no read/write timeouts or something like that at that moment, but only connect timeouts.

#### Network layer symptoms

1. With traffic capturing, we could see **<mark>the first packet (SYN) got siliently dropped by the kernel</mark>**.

    Unfortunately, common NIC stats (`ifconfig`) and kernel stats (`/proc/net/softnet_stat`)
    **<mark>don't show these droppings</mark>**.

2. SYN got restransmitted after `1s+`, or the connection is closed by retransmission.

    **<mark>Retransmission of the first SYN takes 1s, this is a hardcode value in the kernel, not configurable</mark>** (See [appendix](#ch_8.1) for the detailed implementation) .

    Considering other overheads, the real retransmission will take place 1+ second's later.
    If the client has a very small connect timeout setting, e.g.  `1.05s`, then
    the connection will be closed before retransmission, and reports connection
    timeout errors to upper layers.

#### OS/kernel layer symptoms

Kernel log,

```shell
$ demsg -T
[Tue Apr  6 18:12:30 2021] nf_conntrack: nf_conntrack: table full, dropping packet
[Tue Apr  6 18:12:30 2021] nf_conntrack: nf_conntrack: table full, dropping packet
[Tue Apr  6 18:12:30 2021] nf_conntrack: nf_conntrack: table full, dropping packet
...
```

### Trouble shooting

The above described phenomenons indicate that conntrack table is blown out.

```shell
$ cat /proc/sys/net/netfilter/nf_conntrack_count
257273

$ cat /proc/sys/net/netfilter/nf_conntrack_max
262144
```

Compare above two numbers, we could conclude that conntrack table indeeded get
blown out.

Besides, we could also see dropping statistics in `cat /proc/net/stat/nf_conntrack` or `conntrack -S` output.

### Resolution

With decreasing priority:

1. Increase conntrack table size

    Runtime configuration (**will not disrupt existing connections/traffic**) :

    ```shell
    $ sysctl -w net.netfilter.nf_conntrack_max=524288
    $ echo 131072 > /sys/module/nf_conntrack/parameters/hashsize # recommendation: hashsize=nf_conntrack_count/4
    ```

    Permanent configuration:

    ```shell
    $ echo 'net.netfilter.nf_conntrack_max = 524288' >> /etc/sysctl.conf

    # Write hashsize either to system boot file or module config file
    # Method 1: write to system boot file
    $ echo 'echo 131072 > /sys/module/nf_conntrack/parameters/hashsize' >> /etc/rc.local
    # Method 2: write to module load file
    $ echo 'options nf_conntrack expect_hashsize=131072 hashsize=131072' >> /etc/modprobe.d/nf_conntrack.conf
    ```

    Side effect：**<mark>more memory will be reserved</mark>** by conntrack module.
    Refer to the [appendix](#ch_8.2) for the detailed calculation.

2. Decrease GC durations (timeout values)

    Besides increase conntrack table size, we could also decrease conntrack GC values (also called timeouts),
    this will acclerate eviction of stale entries.

    `nf_conntrack` has several timeout setting, each for entries of different TCP states (established、fin_wait、time_wait, etc).

    For example, **<mark>the default timeout for established state conntrack entries is 423000s (5 days!) </mark>**.
    **Possible reason** for so large a value may be: TCP/IP specification
    allows established connection stays idle for infinite long time (but still
    alive) [8], specific implementations (Linux、BSD、Windows, etc) could set their own max allowed idle timeout.
    To avoid to accidently GC out such connection, Linux kernel chose a long enough duration.
    [8] recommends to timeout value to be no smaller than 2 hours 4 minutes (as mentioned previously,
    Cilium implements its own CT module, as comparison and reference,**<mark>Cilium's established timeout is 6 hours</mark>**).
    But there are also recommendations that are far more smaller than this, such as 20 minutes.

    Unless certainly know what you are doing, you should decrease this value with caution,
    **<mark>such as 6 hours</mark>**, which is already smaller significantly than the default one.

    Runtime configuration:

    ```shell
    $ sysctl -w net.netfilter.nf_conntrack_tcp_timeout_established = 21600
    ```

    Permanent configuration:

    ```shell
    $ echo 'net.netfilter.nf_conntrack_tcp_timeout_established = 21600' >> /etc/sysctl.conf
    ```

    You could also consider to decrease the other timeout values (especially `nf_conntrack_tcp_timeout_time_wait`, which defaults to `120s`).
    But still to remind: **unless sure what you're doing, do not decrease these values radically**.

# 7. Summary

Connection tracking (conntrack) is a fairly fundamental and important network module,
but it goes into normal developer or system maintainer's eyes only when they
are stucked in some specific network troubles.

For example, in highly concurrent connection conditions, L4LB node will receive
large amounts of short-lived requestions/connections, which may breakout the conntrack table.
Phenomenons in this case:

* Clients connect to L4LB failed, the failures may be random, but may also be bulky.
* Client retries may succeed, but may also failed again and again.
* Capturing traffic at L4LB nodes, could see that L4LB nodes received
  SYNC (take TCP as example) packets, but no ACK is replied, in other words, the
  packets get siliently dropped.

The reasons here maybe that conntrack table size is not big enough, or GC
interval is too large, or even there are [bugs in conntrack GC](https://github.com/cilium/cilium/pull/12729).

# 8. Appendix

<a name="ch_8.1"></a>

## 8.1 Retransmission interval calculation of the first SYN (Linux 4.19.118)

Call stack: `tcp_connect() -> tcp_connect_init() -> tcp_timeout_init()`.

```c
// net/ipv4/tcp_output.c
/* Do all connect socket setups that can be done AF independent. */
static void tcp_connect_init(struct sock *sk)
{
    inet_csk(sk)->icsk_rto = tcp_timeout_init(sk);
    ...
}

// include/net/tcp.h
static inline u32 tcp_timeout_init(struct sock *sk)
{
    // Get SYN-RTO: return -1 if
    //   * no BPF programs attached to the socket/cgroup, or
    //   * there are BPF programs, but the programs excuting failed
    //
    // Unless users write their own BPF programs and attach to cgroup/socket,
    // there will be no BPF programs. so here will (always) return -1
    timeout = tcp_call_bpf(sk, BPF_SOCK_OPS_TIMEOUT_INIT, 0, NULL);

    if (timeout <= 0)                // timeout == -1, using default value in the below
        timeout = TCP_TIMEOUT_INIT;  // defined as the HZ of the system, which is effectively 1 second, see below
    return timeout;
}

// include/net/tcp.h
#define TCP_RTO_MAX    ((unsigned)(120*HZ))
#define TCP_RTO_MIN    ((unsigned)(HZ/5))
#define TCP_TIMEOUT_MIN    (2U) /* Min timeout for TCP timers in jiffies */
#define TCP_TIMEOUT_INIT ((unsigned)(1*HZ))    /* RFC6298 2.1 initial RTO value    */
```

<a name="ch_8.2"></a>

## 8.2 Calculating conntrack memory usage

```shell
$ cat /proc/slabinfo | head -n2; cat /proc/slabinfo | grep conntrack
slabinfo - version: 2.1
# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab> : tunables <limit> <batchcount> <sharedfactor> : slabdata <active_slabs> <num_slabs> <sharedavail>
nf_conntrack      512824 599505    320   51    4 : tunables    0    0    0 : slabdata  11755  11755      0
```

in the above output, **<mark>objsize means the kernel object size</mark>** (`struct nf_conn` here),
in unit of **<mark>bytes</mark>**. So the above information tells us that
**<mark>each conntrack entry takes 320 bytes of memory</mark>**.

If page overheads are ignored (kernel allocates memory with slabs), then the
**<mark>memory usage under different table sizes</mark>** would be:

* `nf_conntrack_max=512K`: `512K * 320Byte = 160MB`
* `nf_conntrack_max=1M`: `1M * 320Byte = 320MB`

For more accurate calculation, refer to [9].

# References

1. [Netfilter connection tracking and NAT implementation](https://wiki.aalto.fi/download/attachments/69901948/netfilter-paper.pdf). Proc.
   Seminar on Network Protocols in Operating Systems, Dept. Commun. and Networking, Aalto Univ. 2013.
2. [Cilium: Kubernetes without kube-proxy](https://docs.cilium.io/en/v1.7/gettingstarted/kubeproxy-free/)
3. [L4LB for Kubernetes: Theory and Practice with Cilium+BGP+ECMP]({% link _posts/2020-04-10-k8s-l4lb.md %})
4. [Docker bridge network mode](https://docs.docker.com/network/bridge/)
5. [Wikipedia: Netfilter](https://en.wikipedia.org/wiki/Netfilter)
6. [Conntrack tales - one thousand and one flows](https://blog.cloudflare.com/conntrack-tales-one-thousand-and-one-flows/)
7. [How connection tracking in Open vSwitch helps OpenStack performance](https://www.redhat.com/en/blog/how-connection-tracking-open-vswitch-helps-openstack-performance)
8. [NAT Behavioral Requirements for TCP](https://tools.ietf.org/html/rfc5382#section-5), RFC5382
9. [Netfilter Conntrack Memory Usage](https://johnleach.co.uk/posts/2009/06/17/netfilter-conntrack-memory-usage/)
