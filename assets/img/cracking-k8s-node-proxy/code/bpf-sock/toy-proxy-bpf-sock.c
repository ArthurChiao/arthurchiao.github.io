// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2020 Authors of Cilium */
//
// @ArthurChiao 20210203: borrowed some code from Cilium Project

#include <linux/bpf.h> // struct bpf_sock_addr

#define SYS_REJECT	0
#define SYS_PROCEED	1

#ifndef __section
# define __section(NAME)                  \
    __attribute__((section(NAME), used))
#endif

static int
__sock4_xlate_fwd(struct bpf_sock_addr *ctx)
{
    const __be32 cluster_ip = 0x0100070A; // 10.7.255.114
    const __be32 pod_ip = 0x0641060A;     // 10.6.65.6

    if (ctx->user_ip4 != cluster_ip) {
        return 0;
    }

	ctx->user_ip4 = pod_ip;
	return 0;
}

__section("connect4")
int sock4_connect(struct bpf_sock_addr *ctx)
{
	__sock4_xlate_fwd(ctx);
	return SYS_PROCEED;
}
