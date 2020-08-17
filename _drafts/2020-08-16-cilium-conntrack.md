---
layout    : post
title     : "Cilium code walk through: connection tracking (conntrack)"
date      : 2020-08-16
lastupdate: 2020-08-16
categories: cilium conntrack
---

* TOC
{:toc}


# CT implementation

```
ct_lookup4
  |-update_timestamp

ct_create4
  |-update_timestamp
```

```c
// bpf/lib/conntrack.h

enum {
    ACTION_UNSPEC,
    ACTION_CREATE,
    ACTION_CLOSE,
};
```

## Lookup

For ICMP, default ACTION_CREATE, some exceptions:

1. ICMP_DEST_UNREACH, ICMP_TIME_EXCEEDED, or ICMP_PARAMETERPROB: `tuple->flags |= TUPLE_F_RELATED; break;`
2. ICMP_ECHOREPLY:     tuple->sport = identifier;       break;

For TCP:

1. if sees `RST` or `FIN` flag, ACTION_CLOSE
2. else ACTION_CREATE

```c
/* Offset must point to IPv4 header */
int
ct_lookup4(void *map, struct ipv4_ct_tuple *tuple, struct __sk_buff *skb, int off, int dir,
                    struct ct_state *ct_state, __u32 *monitor)
{
    int ret = CT_NEW, action = ACTION_UNSPEC;
    bool is_tcp = tuple->nexthdr == IPPROTO_TCP;
    union tcp_flags tcp_flags = { .value = 0 };

    /* The tuple is created in reverse order initially to find a potential reverse flow.
     * This is required because the RELATED or REPLY state takes precedence over ESTABLISHED due to
     * policy requirements. */
    if      (dir == CT_INGRESS) tuple->flags = TUPLE_F_OUT;
    else if (dir == CT_EGRESS)  tuple->flags = TUPLE_F_IN;
    else if (dir == CT_SERVICE) tuple->flags = TUPLE_F_SERVICE;
    else                        return DROP_CT_INVALID_HDR;

    switch (tuple->nexthdr) {
    case IPPROTO_ICMP:
        skb_load_bytes(skb, off, &type, 1)             // load 1-byte ICMP type
        if (type == ICMP_ECHO || type == ICMP_ECHOREPLY)
             skb_load_bytes(skb, ..., &identifier, 2)  // load 2-byte ICMP echo ID

        tuple->sport = tuple->dport = 0;

        switch (type) {
        case ICMP_DEST_UNREACH:
        case ICMP_TIME_EXCEEDED:
        case ICMP_PARAMETERPROB: tuple->flags |= TUPLE_F_RELATED; break;
        case ICMP_ECHOREPLY:     tuple->sport = identifier;       break;
        case ICMP_ECHO:          tuple->dport = identifier;       /* fall through */
        default:                 action = ACTION_CREATE;          break;
        }
        break;

    case IPPROTO_TCP:
        skb_load_bytes(skb, off + 12, &tcp_flags, 2)

        if (tcp_flags & (TCP_FLAG_RST|TCP_FLAG_FIN)) action = ACTION_CLOSE;
        else                                         action = ACTION_CREATE;

        skb_load_bytes(skb, off, &tuple->dport, 4); /* load sport + dport into tuple */
        break;

    case IPPROTO_UDP:
        ...
    default: /* Can't handle extension headers yet */
        relax_verifier();
        return DROP_CT_UNKNOWN_PROTO;
    }

    // Lookup the reverse direction
    ret = __ct_lookup(map, skb, tuple, action, dir, ct_state, is_tcp, tcp_flags, monitor);
    if (ret != CT_NEW) {
        if (likely(ret == CT_ESTABLISHED)) {
            if (tuple->flags & TUPLE_F_RELATED) ret = CT_RELATED;
            else                                ret = CT_REPLY;
        }
        goto out;
    }

    // Lookup entry in forward direction
    if (dir != CT_SERVICE) {
        ipv4_ct_tuple_reverse(tuple);
        ret = __ct_lookup(map, skb, tuple, action, dir, ct_state, is_tcp, tcp_flags, monitor);
    }
out:
    if (conn_is_dns(tuple->dport))
        *monitor = MTU;
    return ret;
}

__u8
__ct_lookup(void *map, struct __sk_buff *skb, void *tuple, int action, int dir,
                      struct ct_state *ct_state, bool is_tcp, union tcp_flags seen_flags, __u32 *monitor)
{
    struct ct_entry *entry;
    int reopen;

    if ((entry = map_lookup_elem(map, tuple))) {
        if (ct_entry_alive(entry)) {
            *monitor = ct_update_timeout(entry, is_tcp, dir, seen_flags);
        }

        if (ct_state) {
            ct_state->rev_nat_index = entry->rev_nat_index;
            ct_state->loopback = entry->lb_loopback;
            ct_state->node_port = entry->node_port;
            ct_state->dsr = entry->dsr;
            ct_state->proxy_redirect = entry->proxy_redirect;
        }

#ifdef CONNTRACK_ACCOUNTING
        if (dir == CT_INGRESS) {
            __sync_fetch_and_add(&entry->rx_packets, 1);
            __sync_fetch_and_add(&entry->rx_bytes, skb->len);
        } else if (dir == CT_EGRESS) {
            __sync_fetch_and_add(&entry->tx_packets, 1);
            __sync_fetch_and_add(&entry->tx_bytes, skb->len);
        }
#endif

        switch (action) {
        case ACTION_CREATE:
            reopen = entry->rx_closing | entry->tx_closing;
            reopen |= seen_flags.value & TCP_FLAG_SYN;
            if (unlikely(reopen == (TCP_FLAG_SYN|0x1))) {
                ct_reset_closing(entry);
                *monitor = ct_update_timeout(entry, is_tcp, dir, seen_flags);
            }
            break;
        case ACTION_CLOSE: /* RST or similar, immediately delete ct entry */
            if (dir == CT_INGRESS) entry->rx_closing = 1;
            else                   entry->tx_closing = 1;

            *monitor = TRACE_PAYLOAD_LEN;
            if (ct_entry_alive(entry))
                break;

            __ct_update_timeout(entry, CT_CLOSE_TIMEOUT, dir, seen_flags, CT_REPORT_FLAGS);
            break;
        }

        return CT_ESTABLISHED;
    }

    *monitor = TRACE_PAYLOAD_LEN;
    return CT_NEW;
}
```

## Create

```c
int
ct_create4(const void *map_main, const void *map_related, struct ipv4_ct_tuple *tuple,
                      struct __sk_buff *skb, int dir, struct ct_state *ct_state, bool proxy_redirect)
{
    /* Create entry in original direction */
    struct ct_entry entry = { };
    bool is_tcp = tuple->nexthdr == IPPROTO_TCP;
    union tcp_flags seen_flags = { .value = 0 };

    /* if this is a proxy connection, the replies will be redirected back to the proxy. */
    entry.proxy_redirect = proxy_redirect;
    entry.lb_loopback    = ct_state->loopback;
    entry.node_port      = ct_state->node_port;
    entry.dsr            = ct_state->dsr;

    seen_flags.value |= is_tcp ? TCP_FLAG_SYN : 0;
    ct_update_timeout(&entry, is_tcp, dir, seen_flags);

    if (dir == CT_INGRESS) {
        entry.rx_packets = 1;
        entry.rx_bytes = skb->len;
    } else if (dir == CT_EGRESS) {
        entry.tx_packets = 1;
        entry.tx_bytes = skb->len;
    }

    entry.src_sec_id = ct_state->src_sec_id;
    if (map_update_elem(map_main, tuple, &entry, 0) < 0)
        return DROP_CT_CREATE_FAILED;

    if (ct_state->addr && ct_state->loopback) {
        __u8 flags = tuple->flags;
        __be32 saddr, daddr;

        saddr = tuple->saddr;
        daddr = tuple->daddr;

        /* We are looping back into the origin endpoint through a service,
         * set up a conntrack tuple for the reply to ensure we do rev NAT
         * before attempting to route the destination address which will
         * not point back to the right source. */
        tuple->flags = TUPLE_F_IN;
        if (dir == CT_INGRESS) {
            tuple->saddr = ct_state->addr;
            tuple->daddr = ct_state->svc_addr;
        } else {
            tuple->saddr = ct_state->svc_addr;
            tuple->daddr = ct_state->addr;
        }

        if (map_update_elem(map_main, tuple, &entry, 0) < 0)
            return DROP_CT_CREATE_FAILED;
        tuple->saddr = saddr;
        tuple->daddr = daddr;
        tuple->flags = flags;
    }

    if (map_related != NULL) { // Create an ICMP entry to relate errors
        struct ipv4_ct_tuple icmp_tuple = {
            .daddr = tuple->daddr,
            .saddr = tuple->saddr,
            .nexthdr = IPPROTO_ICMP,
            .sport = 0,
            .dport = 0,
            .flags = tuple->flags | TUPLE_F_RELATED,
        };

        entry.seen_non_syn = true; /* For ICMP, there is no SYN. */

        /* Previous map update succeeded, we could delete it in case
         * the below throws an error, but we might as well just let it time out. */
        if (map_update_elem(map_related, &icmp_tuple, &entry, 0) < 0)
            return DROP_CT_CREATE_FAILED;
    }
    return 0;
}
```

## Update timestamp

lifetime calculation:

1. if not TCP
    * if is SERVICE: **service** lifetime for non-TCP
    * else         : **connection** lifetime for non-TCP
2. if is TCP
    1. if is SYNC
        * SYNC timeout
    2. if is not SYNC
        * if is SERVICE: **service** lifetime for TCP
        * else         : **connection** lifetime for TCP

```c
// Update the CT timeouts for the specified entry.
//
// Update the last_updated timestamp if CT_REPORT_INTERVAL has elapsed since the last update.
__u32
ct_update_timeout(struct ct_entry *entry, bool tcp, int dir, union tcp_flags seen_flags)
{
    bool syn = seen_flags.value & TCP_FLAG_SYN;
    if (tcp) {
        entry->seen_non_syn |= !syn;
    }

    lifetime = xxx // calculate lifetime with conditions: is SERVICE? is TCP? is SYNC?

    return __ct_update_timeout(entry, lifetime, dir, seen_flags, CT_REPORT_FLAGS);
}
```

Take ingress as example:

```c
/**
 * Returns how many bytes of the packet should be monitored:
 * - Zero if this flow was recently monitored.
 * - Non-zero if this flow has not been monitored recently.  */
__u32
__ct_update_timeout(struct ct_entry *entry, __u32 lifetime, int dir, union tcp_flags flags, __u8 report_mask)
{
    __u8 seen_flags = flags.lower_bits & report_mask;

    WRITE_ONCE(entry->lifetime, now + lifetime);

    if (dir == CT_INGRESS) {
        accumulated_flags = READ_ONCE(entry->rx_flags_seen);
        last_report       = READ_ONCE(entry->last_rx_report);
    } else { ...  }
    seen_flags |= accumulated_flags;

    if (last_report + CT_REPORT_INTERVAL < now || accumulated_flags != seen_flags) {
        if (dir == CT_INGRESS) {
            WRITE_ONCE(entry->rx_flags_seen, seen_flags);
            WRITE_ONCE(entry->last_rx_report, now);
        } else { ...  }
        return TRACE_PAYLOAD_LEN;
    }
    return 0;
}
```

## Keepalive

```c
/* Helper for holding 2nd service entry alive in nodeport case. */
static inline bool
__ct_entry_keep_alive(void *map, void *tuple)
{
    /* Lookup indicates to LRU that key/value is in use. */
    if ((struct ct_entry *entry = map_lookup_elem(map, tuple))) {
        if (entry->node_port) {
            lifetime = (entry->seen_non_syn ?  CT_SERVICE_LIFETIME_TCP : CT_SERVICE_LIFETIME_NONTCP) \
                + bpf_ktime_get_sec();
            WRITE_ONCE(entry->lifetime, lifetime);

            if (!ct_entry_alive(entry))
                ct_reset_closing(entry);
        }
        return true;
    }
    return false;
}
```
