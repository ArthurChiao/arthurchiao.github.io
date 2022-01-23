// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2022 Authors of Cilium */
//
// @ArthurChiao 202201123: borrowed some code from the Cilium Project

#include <linux/bpf.h>     // struct __sk_buff
#include <linux/pkt_cls.h> // TC_ACT_*
#include <linux/ip.h>      // struct iphdr
#include <linux/tcp.h>     // struct tcphdr
#include <arpa/inet.h>     // ntohs/ntohl, IPPROTO_TCP

#ifndef __section
# define __section(NAME)                  \
       __attribute__((section(NAME), used))
#endif

#ifndef BPF_FUNC
#define BPF_FUNC(NAME, ...)     \
        (*NAME)(__VA_ARGS__) = (void *) BPF_FUNC_##NAME
#endif

static void BPF_FUNC(trace_printk, const char *fmt, int fmt_size, ...);

#ifndef printk
# define printk(fmt, ...)                                      \
    ({                                                         \
     char ____fmt[] = fmt;                                  \
     trace_printk(____fmt, sizeof(____fmt), ##__VA_ARGS__); \
     })
#endif

#define ETH_HLEN 14
const int l3_off = ETH_HLEN;        // IP header offset in raw packet data
const int l4_off = l3_off + 20;     // TCP header offset: l3_off + IP header
const int l7_off = l4_off + 20;     // Payload offset: l4_off + TCP header

#define DB_POD_IP         0x020011AC // 172.17.0.2 in network order
#define FRONTEND_POD_IP   0x030011AC // 172.17.0.3 in network order
#define BACKEND_POD1_IP   0x040011AC // 172.17.0.4 in network order
#define BACKEND_POD2_IP   0x050011AC // 172.17.0.5 in network order

struct policy {           // Ingress/inbound policy representation:
    int    src_identity;  // traffic from a service with 'identity == src_identity'
    __u8   proto;         // are allowed to access the 'proto:dst_port' of
    __u8   pad1;          // the destination pod.
    __be16 dst_port;
};
struct policy db_ingress_policy_cache[4] = { // Per-pod policy cache,
    { 10003, IPPROTO_TCP, 0, 6379 },         // We just hardcode one policy here
    {},
};

static __always_inline int
policy_lookup(int src_identity, __u8 proto, __be16 dst_port) {
    printk("policy_lookup: %d %d %d\n", src_identity, proto, dst_port);

    struct policy *c = db_ingress_policy_cache;
    for (int i=0; i<4; i++) {
        if (c[i].src_identity == src_identity && c[i].proto == proto && c[i].dst_port == dst_port) {
            return 1;
        }
    }

    return 0; // not found
}

static __always_inline int
ipcache_lookup(__be32 ip)
{
    switch (ip) {
        case DB_POD_IP:        return 10001;
        case FRONTEND_POD_IP:  return 10002;
        case BACKEND_POD1_IP:  return 10003;
        case BACKEND_POD2_IP:  return 10003;
        default:               return -1;
    }
}

static __always_inline int
__policy_can_access(struct __sk_buff *skb, int src_identity, __u8 proto)
{
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;

    if (proto == IPPROTO_TCP) {
        if (data_end < data + l7_off) {
            printk("Invalid TCP packet, drop it, data length: %d\n", data_end - data);
            return 0;
        }

        struct tcphdr *tcp = (struct tcphdr *)(data + l4_off);
        return policy_lookup(src_identity, proto, ntohs(tcp->dest))? 1 : 0;
    }

    return 0;
}

__section("egress")
int tc_egress(struct __sk_buff *skb)
{
    // 1. Basic validation
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    if (data_end < data + l4_off) { // May be system packet, for simplicity just let it go
        printk("Toy-enforcer: PASS, as not an IP packet, data length: %d\n", data_end - data);
        return TC_ACT_OK;
    }

    // 2. Extract header and map src_ip -> src_identity
    struct iphdr *ip4 = (struct iphdr *)(data + l3_off);
    int src_identity = ipcache_lookup(ip4->saddr);
    if (src_identity < 0) { // packet from a service with unknown identity, just drop it
        printk("Toy-enforcer: DROP, as src_identity not found from ipcache: %x\n", ip4->saddr);
        return TC_ACT_SHOT;
    }

    // 3. Determine if traffic with src_identity could access this pod
	if (__policy_can_access(skb, src_identity, ip4->protocol)) {
        printk("Toy-enforcer: PASS, as policy found\n");
        return TC_ACT_OK;
    }

    printk("Toy-enforcer: DROP, as policy not found\n");
    return TC_ACT_SHOT;
}

char __license[] __section("license") = "GPL";
