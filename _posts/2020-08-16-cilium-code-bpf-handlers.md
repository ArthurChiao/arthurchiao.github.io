---
layout    : post
title     : "Cilium code walk through: BPF handlers (entrypoints)"
date      : 2020-08-16
lastupdate: 2020-08-16
categories: cilium bpf
---

* TOC
{:toc}


## TL; DR

This post walks through the C code that will eventually be compiled into BPF
code and loaded into kernel while Cilium agent sets up network for a Pod.
When the Pod sends/receives packets later, these BPF handlers will be triggered.
See Fig 1-1 for an illustration.

Code based on Cilium `1.8.2`.

# 1 From the start

## 1.1 Generate and compile BPF code

> Cilium supports several types of network devices for inter-connecting a Pod and
> its host, such as veth pair, IPVLAN, etc. This post assumes we are using veth
> pair, which is the default configuration. Further, this post assumes we are
> handling IPv4 traffic.

On setting up network for a Pod, Cilium agent will generate a piece of C code,
compile it into BPF object file, then load it into the Pod's network device
(vNIC).

We have covered the implementation of the above steps in
[Cilium Code Walk Through: CNI Create Network]({% link _posts/2019-06-17-cilium-code-cni-create-network.md %}).
We elaberate a little more about the loading process.

## 1.2 Loading BPF code into kernel

As shown in Fig 1-1:

<p align="center"><img src="/assets/img/cilium-code-bpf-handlers/bpf-hooks.png" width="60%" height="60%"></p>
<p align="center">Fig 1-1. Veth pair and BPF hooking points</p>

BPF code for a Pod is split into two parts,

1. `from-container`: for egress packets sent out from the Pod.
    * Will be loaded into the `egress` hook of the device
1. `to-container`: ingress packets destinated to the Pod.
    * Will be loaded into the `ingress` hook of the device

The loading is accomplished by calling method `replaceDatapath()`:

```
// pkg/datapath/loader/loader.go

// symbolFromEndpoint = "from-container"
replaceDatapath(ctx, ep.InterfaceName(), objPath, symbolFromEndpoint, dirIngress)

// symbolToEndpoint = "to-container"
replaceDatapath(ctx, ep.InterfaceName(), objPath, symbolToEndpoint, dirEgress)
```

and `replaceDatapath()` will call **shell command `tc`** to fulfill the loading:

```
    args := []string{"filter", "replace",
        "dev", ifName,    // host side of container's veth pair, e.g. lxc123456
        progDirection,    // ingress or egress
        "prio", "1",
        "handle", "1",
        "bpf", "da",      // da: direct action mode
        "obj", objPath,   // path of the generated BPF object file
        "sec",
        progSec,          // which section this BPF file will be loaded to
    }
    cmd = exec.CommandContext(ctx, "tc", args...)
```

## 1.3 BPF handlers

On success, subsequent packets will **trigger the BPF handler's execution**
when they arrive corresponding hooking points:

1. for `ingress` packets: trigger method `handle_to_container()`
1. for `egress` packets: trigger method `handle_xgress()`

BPF handlers are used for filtering and manipulating packets, such as,

1. check whether allow this packet or not
2. modify packet, e.g. perform NAT, DSR
3. other actions that faciliate the above functionalities, e.g. CT

BPF handlers always return a verdict result to the caller, which may be one of:

1. accept: this packet is ok for me, please go on further processing
2. drop: this packet should be dropped (or already dropped by me)
3. others, such as, redirect to a proxy

BPF code is generated from C source code, which locates at the `bpf/` folder in
Cilium's source code tree. In the following, we will walk through some
implementation details of these handlers, including:

* connection tracking (CT, conntrack)
* network address translation (NAT)
* direct server return (DSR)
* policy enforcing (L3/L4 in BPF, L7 will be redirect to a proxy)

# 2 BPF handler: ingress packets processing

```
/----------------------------------------------------------------------\
|A packet arrives Pod's network device (lxcxxx), destinated for the Pod|
\----------------------------------------------------------------------/
            ||
            || trigger BPF handler execution: call handle_to_container(pkt)
            ||
            \/
handle_to_container                                                             //    bpf/bpf_lxc.c
  |-inherit_identity_from_host(skb, &identity)                                  // -> bpf/lib/identity.h
  |-tail_ipv4_to_endpoint                                                       //    bpf/bpf_lxc.c
      |-ipv4_policy                                                             //    bpf/bpf_lxc.c
          |-policy_can_access_ingress                                           //    bpf/lib/policy.h
              |-__policy_can_access_ingress                                     //    bpf/lib/policy.h
                  |-policy = map_lookup_elem()
                    if policy exists
                        account()                                               // -> bpf/lib/policy.h
                        return TC_ACK_OK;
                    return DROP_POLICY;
```

## 2.1 `handle_to_container()`: start BPF processing

steps:

1. Validate packet (header and body sizes), extract L3 proto
2. Get identity for this packet
3. Further processing based on different L3 protocols:
    1. ARP: return ACCEPT to the caller.
    2. IP packet: call ipv4/ipv6 handler for further processing.
    3. Unkown protocol: return DROP to the caller.

```c
// bpf/bpf_lxc.c

__section("to-container")
int handle_to_container(struct __sk_buff *skb)
{
    validate_ethertype(skb, &proto));               // extract L3 proto from pkt

    bpf_clear_cb(skb);
    inherit_identity_from_host(skb, &identity);     // get identity from skb->mark
    skb->cb[CB_SRC_LABEL] = identity;               // used for policy validation

    switch (proto) {
    case ETH_P_ARP: ret = TC_ACT_OK;                               break;
    case ETH_P_IP : invoke_tailcall_if(.., tail_ipv4_to_endpoint); break; // tail call
    default       : ret = DROP_UNKNOWN_L3;                         break;
    }

    return ret;
}
```

To workaround the max instruction limit of single method in BPF, Cilium heavily
utilizes tail calls. Note tail calls will not return to the caller if runned
succssful. In other words, it a tail call returns to the caller function, there
must be errors, we will see some handlings in such cases later.

Let's see the handling code if this is a normal IPv4 packet.

## 2.2 `tail_ipv4_to_endpoint()`: IPv4 packet processing

Extract identity (security label), then perform policy enforcement:

1. for L3/L4 policy, enforce in BPF
1. for L7 policy, redirect packet to the dedicated L7 proxy (e.g. Kafka L7 filter)

```c
int
tail_ipv4_to_endpoint(struct __sk_buff *skb)
{
    src_identity = skb->cb[CB_SRC_LABEL]; // get identity, which is saved in handle_to_container(), Section 2.1
    revalidate_data(skb, &ip4);           // extract ipv4 header: struct iphdr *ip4

    skb->cb[CB_SRC_LABEL] = 0;

    // L3/L4 policy enforcement
    ret = ipv4_policy(skb, 0, src_identity, &reason, &proxy_port);

    // L7 policy enforcement: redirect to proxy
    if (ret == POLICY_ACT_PROXY_REDIRECT)
        ret = skb_redirect_to_proxy_hairpin(skb, proxy_port);

    return ret;
}
```

Then let's see what happened in `ipv4_policy()`.

## 2.3 `ipv4_policy()`: L3/L4 policy enforcement

steps:

* if is reply of Pod's egress traffic, and get NAT-ed: do reverse NAT
* L3/L4 policy verdict
* perform DSR if needed
* if first packet of a new connection, create conntrack entry
* tell the caller to redirect this packet to the given proxy for L7 policy enforcement

```c
int
ipv4_policy(struct __sk_buff *skb, int ifindex, __u32 src_label, __u8 *reason, __u16 *proxy_port)
{
    ret = ct_lookup4(&tuple, skb, CT_INGRESS, &ct_state);

    // Check it this is return traffic to an egress proxy.
    if ((ret == CT_REPLY || ret == CT_RELATED) && ct_state.proxy_redirect && !tc_index_skip_egress_proxy(skb))
        return POLICY_ACT_PROXY_REDIRECT;

    // if is reply of Pod's egress traffic, and get NAT-ed: do reverse NAT
    if (ret == CT_REPLY && ct_state.rev_nat_index && !ct_state.loopback)
        lb4_rev_nat(skb, .., &ct_state, &tuple, REV_NAT_F_TUPLE_SADDR);

    verdict = policy_can_access_ingress(skb, src_label, tuple.dport, ..)

    // Reply/related packets packets allowed, but all others must go through policy
    if (ret != CT_REPLY && ret != CT_RELATED && verdict < 0)
        return verdict;

    if (ret == CT_NEW) { // ret is the result of ct_lookup4()
#ifdef ENABLE_DSR        // decided when generating this BPF file
        handle_dsr_v4(skb, &dsr);
        ct_state_new.dsr = dsr;
#endif /* ENABLE_DSR */
        ct_create4(&CT_MAP_ANY4, &tuple, skb, CT_INGRESS, &ct_state_new, verdict > 0);
    }

    if (redirect_to_proxy(verdict, *reason)) {
        *proxy_port = verdict;              // tell the caller to redirect this packet to the specified proxy
        return POLICY_ACT_PROXY_REDIRECT;   // the proxy will do L7 policy enforcement for this pkt
    }

    ifindex = skb->cb[CB_IFINDEX];
    if (ifindex)
        return redirect_peer(ifindex, 0);

    return TC_ACT_OK;
}
```

## 2.4 `policy_can_access()`

`policy_can_access()` wraps over `__policy_can_access`:

```c
int
__policy_can_access(void *map, struct __sk_buff *skb, __u32 identity,
            __u16 dport, __u8 proto, int dir, bool is_fragment)
{
    struct policy_key key = {
        .sec_label = identity,
        .dport = dport,
        .protocol = proto,
        .egress = !dir,
    };

    /* Start with L3/L4 lookup. */
    if policy = map_lookup_elem(map, &key); policy {
        account(skb, policy);
        return policy->proxy_port;
    }

    /* L4-only lookup. */
    key.sec_label = 0;
    if policy = map_lookup_elem(map, &key); policy {
        account(skb, policy);
        return policy->proxy_port;
    }

    key.sec_label = identity;

    /* If L4 policy check misses, fall back to L3. */
    key.dport = 0;
    key.protocol = 0;
    if policy = map_lookup_elem(map, &key); policy {
        account(skb, policy);
        return TC_ACT_OK;
    }

    /* Final fallback if allow-all policy is in place. */
    key.sec_label = 0;
    if policy = map_lookup_elem(map, &key); policy {
        account(skb, policy);
        return TC_ACT_OK;
    }

    if (skb->cb[CB_POLICY])
        return TC_ACT_OK;

    return DROP_POLICY;
}
```

```c
void
account(struct __sk_buff *skb, struct policy_entry *policy)
{
    __sync_fetch_and_add(&policy->packets, 1);
    __sync_fetch_and_add(&policy->bytes, skb->len);
}
```

# 3 BPF handler: egress packets processing

```
/-----------------------------------------------------------------------\
|A packet is sent out from the Pod, arrives Pod's network device (vNIC) |
\-----------------------------------------------------------------------/
            ||
            || trigger BPF code execution, call handle_xgress(pkt)
            ||
            \/
handle_xgress                                                                   // bpf/bpf_lxc.c
  |-tail_handle_ipv4                                                            // bpf/bpf_lxc.c
      |-handle_ipv4_from_lxc                                                    // bpf/bpf_lxc.c
          |-if dst is k8s Service
                lb4_local() // create/update CT, do DNAT
            if l7 proxy
                return redirect_to_proxy()

            policy_can_egress4()

            ret = ct_lookup4()
            switch (ret) {
            case CT_NEW:
                ct_create4()
            CT_REPLY,CT_RELATED,CT_ESTABLISHED: // perform DSR or SNAR
                xlate_dsr_v4() or lb4_rev_nat()
            }

            if l7 proxy
                return redirect_to_proxy()

            // pass to stack (continue normal routing)
            ipv4_l3() // dec TTL, set src/dst MAC
            asm_set_seclabel_identity(skb);
            return TC_ACT_OK;
```

## 3.1 `handle_xgress()`: start BPF processing

```c
// bpf/bpf_lxc.c

__section("from-container")
int handle_xgress(struct __sk_buff *skb)
{
    validate_ethertype(skb, &proto) // extract L3 proto

    switch (proto) {
    case ETH_P_IP : invoke_tailcall_if(.., tail_handle_ipv4); break;
    case ETH_P_ARP: ...;                                      break;
    default       : ret = DROP_UNKNOWN_L3;                    break;
    }
}
```

Let's see the processing if this is an IPv4 packet.

## 3.2 `handle_ipv4_from_lxc()`: IPv4 processing

1. if dst is Service IP, then create/update CT, and perform NAT (DNAT); if not a
   Service IP, do nothing
2. lookup CT
3. redirect to proxy if L7
4. policy checking

```c
int
handle_ipv4_from_lxc(struct __sk_buff *skb, __u32 *dstID)
{
    union macaddr router_mac = NODE_MAC;
    revalidate_data(skb, ..., &ip4);      // extract ipv4 header (struct ipv4hdr *ip4)

    ret = lb4_extract_key(skb, &tuple, &key, CT_EGRESS); // extract tuple, determine if dst is K8s Service
    if (ret == DROP_UNKNOWN_L4)           // not a k8s Service
        goto skip_service_lookup;

    if (svc = lb4_lookup_service(&key))   // Service exists
        lb4_local(...);                   // create/update CT, perform NAT by calling lb4_xlate()

skip_service_lookup:
    ret = ct_lookup4(.., skb, CT_EGRESS, &ct_state);

    // Redirect to ingress proxy if this is return traffic to an ingress proxy.
    if ((ret == CT_REPLY || ret == CT_RELATED) && ct_state.proxy_redirect)
        return skb_redirect_to_proxy(skb, 0); // Stack will do a socket match and deliver locally

    /* Determine the destination category for policy fallback. */
    info = lookup_ip4_remote_endpoint(orig_dip);
    if (info && info->sec_label) *dstID = info->sec_label;
    else                         *dstID = WORLD_ID;

    verdict = policy_can_egress4(skb, &tuple, *dstID); // perform policy checking
    if (ret != CT_REPLY && ret != CT_RELATED && verdict < 0)
        return verdict;

    switch (ret) { // ret is the result of ct_lookup4()
    case CT_NEW:
ct_recreate4:
        ct_state_new.src_sec_id = SECLABEL;
        ret = ct_create4(&CT_MAP_ANY4, &tuple, skb, CT_EGRESS, &ct_state_new, verdict > 0);
        break;
    case CT_ESTABLISHED:
    case CT_RELATED:
    case CT_REPLY:
        policy_mark_skip(skb); // Mark skb to skip policy enforcement
        if (ct_state.dsr) // replace src_ip with ClusterIP or ExternalIP for egress packets
            ret = xlate_dsr_v4(skb, &tuple, l4_off);
        if (ct_state.rev_nat_index) // reverse NAT for egress packets
            lb4_rev_nat(skb, l3_off, l4_off, &csum_off, &ct_state, &tuple, 0);
        break;
    default:
        return DROP_UNKNOWN_CT;
    } // end of switch(ret)

    if (redirect_to_proxy(verdict, reason))
        return skb_redirect_to_proxy(skb, verdict);

#if tunnel mode
    encap_and_redirect_lxc()
# else // direct routing, e.g. with BGP
    goto pass_to_stack;
#endif

pass_to_stack:
    ipv4_l3(skb, l3_off, NULL, &router_mac.addr, ip4); // dec TTL, set src/dst MAC
    asm_set_seclabel_identity(skb); // Always encode the source identity when passing to the stack.
    return TC_ACT_OK;               // return verdict=OK to kernel processing stack
}
```

## 3.3 `lb4_local()`

```c
// bpf/lib/lb.h

int
lb4_local(...)
{
    ret = ct_lookup4(map, tuple, skb, l4_off, CT_SERVICE, state, &monitor);
    switch(ret) {
    case CT_NEW:
        backend = lb4_lookup_backend(skb, slave_svc->backend_id);
        ct_create4(map, NULL, tuple, skb, CT_SERVICE, state, false);
        goto update_state;
    case CT_ESTABLISHED:
    case CT_RELATED:
    case CT_REPLY:
        if (unlikely(state->rev_nat_index == 0)) { // For backward-compatibility
            state->rev_nat_index = svc->rev_nat_index;
            ct_update4_rev_nat_index(map, tuple, state);
        }
        break;
    default:
        goto drop_no_service;
    }

    backend = lb4_lookup_backend(skb, state->backend_id));

update_state:
    tuple->flags = flags;
    state->rev_nat_index = svc->rev_nat_index;
    state->addr = new_daddr = backend->address;

    if (!state->loopback)
        tuple->daddr = backend->address;

    return lb4_xlate(); // do NAT for any fields (src/dst ip, src/dst port) needed

drop_no_service:
    return DROP_NO_SERVICE;
}
```

## 3.4 `policy_can_egress4()`

```c
int
policy_can_egress4(struct __sk_buff *skb, struct ipv4_ct_tuple *tuple, __u32 identity)
{
    return policy_can_egress(skb, identity, tuple->dport, tuple->nexthdr);
}

int
policy_can_egress(struct __sk_buff *skb, __u32 identity, __u16 dport, __u8 proto)
{
    ret = __policy_can_access(&POLICY_MAP, skb, identity, dport, proto, CT_EGRESS, false);
    if (ret >= 0)
        return ret;

#ifdef IGNORE_DROP
    ret = TC_ACT_OK;
#endif

    return ret;
}
```

We have detailed method `__policy_can_access()` in section 2.4.

# References

1. [Cilium Code Walk Through: CNI Create Network]({% link _posts/2019-06-17-cilium-code-cni-create-network.md %})
