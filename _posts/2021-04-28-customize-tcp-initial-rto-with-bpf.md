---
layout    : post
title     : "Customize TCP initial RTO (retransmission timeout) with BPF"
date      : 2021-04-28
lastupdate: 2021-04-28
categories: bpf tcp bpftool
---

# TL; DR

On initiating a new TCP connection (`connect()`),
the initial retransmission timeout (RTO) has been set as a
**<mark>harcoded value of 1 second</mark>** in Linux kernel (not configurable).
Since 4.13, a BPF hook has been added to the `connect` operation, which provides
a chance to dynamically override the hardcode (instead of re-compiling
kernel) with custom BPF programs.

This post explores that facility, and implements **<mark>one such program with
several lines of BPF code</mark>**.  Hope that it helps readers to understand why
**<mark>"BPF makes Linux kernel programmable"</mark>**.

----

* TOC
{:toc}

----

# 1 Introduction

## 1.1 TCP initial RTO (kernel <= 4.12)

In Linux kernel (`<= 4.12`), the **<mark>initial RTO</mark>** when establishing a new TCP connection
is **<mark>exactly 1 second</mark>**, which is **<mark>hardcoded in the kernel code and
not configurable</mark>**, taking [v4.12](https://github.com/torvalds/linux/blob/v4.12/net/ipv4/tcp_output.c#L3251)
as example:

> Calling stack `tcp_connect() -> tcp_connect_init() -> tcp_timeout_init()`

```c
// net/ipv4/tcp_output.c

static void tcp_connect_init(struct sock *sk)
{
    ...
    inet_csk(sk)->icsk_rto = TCP_TIMEOUT_INIT; // Set initial timeout value
    ...
}
```

where macro [`TCP_TIMEOUT_INIT()`](https://github.com/torvalds/linux/blob/v4.12/include/net/tcp.h#L138)
is defined as a constant,

```c
// include/net/tcp.h

#define TCP_TIMEOUT_INIT ((unsigned)(1*HZ))    /* RFC6298 2.1 initial RTO value    */
#define TCP_RTO_MAX    ((unsigned)(120*HZ))
#define TCP_RTO_MIN    ((unsigned)(HZ/5))
#define TCP_TIMEOUT_MIN    (2U) /* Min timeout for TCP timers in jiffies */
```

which is the **<mark>HZ of the machine</mark>**,

```shell
$ grep 'CONFIG_HZ=' /boot/config-$(uname -r)
CONFIG_HZ=250
```

## 1.2 Measure the initial RTO (kernel <= 4.12)

Confirm the setting by initiating a TCP request to a non-existing service (so it will always timeout):

```shell
$ telnet 9.9.9.9 9999
```

the captured traffic:

```shell
$ sudo tcpdump -nn -i enp0s3 host 9.9.9.9 and port 9999
21:26:43.834860 IP 192.168.1.5.53844 > 9.9.9.9.9999: Flags [S], seq 281070166, ... length 0 # +0s
21:26:44.859801 IP 192.168.1.5.53844 > 9.9.9.9.9999: Flags [S], seq 281070166, ... length 0 # +1s
21:26:46.876328 IP 192.168.1.5.53844 > 9.9.9.9.9999: Flags [S], seq 281070166, ... length 0 # +2s
21:26:51.068268 IP 192.168.1.5.53844 > 9.9.9.9.9999: Flags [S], seq 281070166, ... length 0 # +4s
21:26:59.259304 IP 192.168.1.5.53844 > 9.9.9.9.9999: Flags [S], seq 281070166, ... length 0 # +8s
21:27:15.389522 IP 192.168.1.5.53844 > 9.9.9.9.9999: Flags [S], seq 281070166, ... length 0 # +16s
...
```

As shown in the last column (comments), **<mark>the first retry got triggered after 1s</mark>**, then 
exponentially backoffed with `2->4->8->16...`.

## 1.3 BPF hook in new kernels (>= 4.13)

Glancing at kernel [4.19](https://github.com/torvalds/linux/blob/v4.19/net/ipv4/tcp_output.c#L3348)
, the initial RTO constant `TCP_TIMEOUT_INIT` is **<mark>replaced by an inline function call</mark>**,

```c
// net/ipv4/tcp_output.c
/* Do all connect socket setups that can be done AF independent. */
static void tcp_connect_init(struct sock *sk)
{
    ...
    inet_csk(sk)->icsk_rto = tcp_timeout_init(sk);
    ...
}
```

and the latter internally sets the initial RTO to `TCP_TIMEOUT_INIT`, but **<mark>gives us a
chance to retrieve the desired initial RTO from the BPF code attached to this socket</mark>**:

```c
// include/net/tcp.h

#define TCP_TIMEOUT_INIT ((unsigned)(1*HZ))    /* RFC6298 2.1 initial RTO value    */

static inline u32 tcp_timeout_init(struct sock *sk)
{
    timeout = tcp_call_bpf(sk, BPF_SOCK_OPS_TIMEOUT_INIT, 0, NULL);

    if (timeout <= 0)                // timeout == -1, using default value in the below
        timeout = TCP_TIMEOUT_INIT;  // defined as the HZ of the system, which is effectively 1 second
    return timeout;
}
```

`tcp_call_bpf(sk, BPF_SOCK_OPS_TIMEOUT_INIT, 0, NULL)` return `-1` if

* No BPF programs attached to the socket/cgroup, or
* There are BPF programs, but the the excution failed

Otherwise, if **<mark>it returns a positive value, that value will be used as the initial RTO</mark>**,
instead of the default `TCP_TIMEOUT_INIT` (1 second).

## 1.4 Purpose of this post (with kernels >= 4.13)

Unless write your own BPF program and attach it to the right place, which
will later be triggered and got executed in `tcp_call_bpf()`,
there will be no BPF programs, so it will use default RTO.

This post tries to **<mark>set a custom initial RTO with the BPF mechanism provided above</mark>**.
With this piece of BPF code, we'll be able to dynamically set/unset custom initial RTOs.

# 2 Implementation and verification

## 2.1 BPF code

It turns out that achieving our goal requires only a fairly small piece of BPF
code (for demo), as shown below (`tcp-rto.c`):

```c
#include <linux/bpf.h>

#ifndef __section
# define __section(NAME)                  \
    __attribute__((section(NAME), used))
#endif

__section("sockops")
int set_initial_rto(struct bpf_sock_ops *skops)
{
    int timeout = 3;
    int hz = 250;    // grep 'CONFIG_HZ=' /boot/config-$(uname -r), HZ of my machine

    int op = (int) skops->op;
    if (op == BPF_SOCK_OPS_TIMEOUT_INIT) {
        skops->reply = hz * timeout; // 3 seconds
    }

    return 1;
}

char _license[] __section("license") = "GPL";
```

Let's leave the explanations to the next section, and try it and see the result first.

## 2.2 Compile, load and attach

Compile to BPF object code,

```shell
$ clang -O2 -target bpf -c tcp-rto.c -o tcp-rto.o
```

Load it into kernel,

```shell
$ sudo bpftool prog load tcp-rto.o /sys/fs/bpf/tcp-rto
$ sudo bpftool prog show
...
169: sock_ops  name set_initial_rto  tag e4384b8da577553a  gpl
        loaded_at 2021-04-29T15:49:03+0800  uid 0
        xlated 296B  jited 186B  memlock 4096B
```

Attach to default cgroup (v2):

```
$ PROG_ID=169
$ sudo bpftool cgroup attach /sys/fs/cgroup/unified/ sock_ops id $PROG_ID
```

> **<mark>BPF programs of <code>sockops</code> type requires cgroupv2</mark>**. After attaching a BPF program to a cgroup,
> the program will be executed for all the sockets in that cgroup.
>
> You could also attach the program to a custom cgroup, but that's beyong the scope
> of this post. Refer to [Cracking kubernetes node proxy (aka kube-proxy)]({% link _posts/2019-11-30-cracking-k8s-node-proxy.md %}) if you need it.

## 2.3 Verify

We've intentionally set our initial RTO as a weired value of `3s` in the BPF code, now let's confirm it works:

```shell
$ telnet 9.9.9.9 9999
```

The captured traffic:

```shell
$ sudo tcpdump -nn -i enp0s3 host 9.9.9.9 and port 9999
21:37:46.357686 IP 192.168.1.5.53866 > 9.9.9.9.9999: Flags [S], seq 3392061475, .. length 0 # +0s
21:37:49.372053 IP 192.168.1.5.53866 > 9.9.9.9.9999: Flags [S], seq 3392061475, .. length 0 # +3s
21:37:55.515914 IP 192.168.1.5.53866 > 9.9.9.9.9999: Flags [S], seq 3392061475, .. length 0 # +6s
21:38:07.547362 IP 192.168.1.5.53866 > 9.9.9.9.9999: Flags [S], seq 3392061475, .. length 0 # +12s
21:38:32.635499 IP 192.168.1.5.53866 > 9.9.9.9.9999: Flags [S], seq 3392061475, .. length 0 # +24s
...
```

Started with a RTO of `3s`, then exponentially backoff-ed as `3->6->12->24...`,
**<mark>just as expected!</mark>**

## 2.4 Cleanup

```shell
$ sudo rm /sys/fs/bpf/tcp-rto
$ sudo bpftool cgroup detach /sys/fs/cgroup/unified/ sock_ops id $PROG_ID
```

# 3 Explanations

## 3.1 Hook at the right place (sockops)

To fulfill our goal, **<mark>our BPF program needs to be excuted whenever there
are TCP <code>connect</code> socket operations</mark>** (sockops).
This is achieved by declaring our BPF handler to be placed at `sockops` section:

```c
__section("sockops")
int set_initial_rto(struct bpf_sock_ops *skops)
{
    ...
}
```

## 3.2 Set custom initial RTO

The next task is to implement our handler, the logic is quite simple:
**<mark>check socket operation type</mark>**, then

1. If it's `BPF_SOCK_OPS_TIMEOUT_INIT` (correspoinding to the code in `tcp_timeout_init()`),
  modify the timeout value, then return
2. Otherwise, do nothing, just return

First see **<mark>how the custom timeout value will be parsed by the kernel</mark>**:

```c
static inline u32 tcp_timeout_init(struct sock *sk)
{
    timeout = tcp_call_bpf(sk, BPF_SOCK_OPS_TIMEOUT_INIT, 0, NULL);

    if (timeout <= 0)                // timeout == -1, using default value in the below
        timeout = TCP_TIMEOUT_INIT;  // defined as the HZ of the system, which is effectively 1 second, see below
    return timeout;
}
```

and the `tcp_call_bpf()`:

```c
//  include/net/tcp.h

/* Call BPF_SOCK_OPS program that returns an int. If the return value
 * is < 0, then the BPF op failed (for example if the loaded BPF
 * program does not support the chosen operation or there is no BPF program loaded).  */
static inline int tcp_call_bpf(struct sock *sk, int op, u32 nargs, u32 *args)
{
	...
	ret = BPF_CGROUP_RUN_PROG_SOCK_OPS(&sock_ops);
	if (ret == 0)
		ret = sock_ops.reply;
	else
		ret = -1;
	return ret;
}
```

If `ret == 0`, the timeout value will be **<mark>parsed from field sock_ops.reply</mark>**.

So for our handler, we just need to check the `op` field, if it's
`BPF_SOCK_OPS_TIMEOUT_INIT`, then change it to the desired value:

```c
int set_initial_rto(struct bpf_sock_ops *skops)
{
    int timeout = 3;
    int hz = 250;    // grep 'CONFIG_HZ=' /boot/config-$(uname -r), HZ of my machine

    int op = (int) skops->op;
    if (op == BPF_SOCK_OPS_TIMEOUT_INIT) {
        skops->reply = hz * timeout; // seconds
    }

    return <RET>;
}
```

Then only piece is missing: **<mark>determine the correct return value</mark>** `<RET>` of our handler.

## 3.3 Determine the return value

Take a further look at the calling stack:
**<mark>tcp_call_bpf() -> BPF_CGROUP_RUN_PROG_SOCK_OPS() -> __cgroup_bpf_run_filter_sock_ops() -> set_initial_rto()</mark>**:

```c
// include/linux/bpf-cgroup.h

#define BPF_CGROUP_RUN_PROG_SOCK_OPS(sock_ops)				       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled && (sock_ops)->sk) {	       \
		typeof(sk) __sk = sk_to_full_sk((sock_ops)->sk);	       \
		if (__sk && sk_fullsock(__sk))				       \
			__ret = __cgroup_bpf_run_filter_sock_ops(__sk,	       \
								 sock_ops,     \
							 BPF_CGROUP_SOCK_OPS); \
	}								       \
	__ret;								       \
})

// kernel/bpf/cgroup.c
/**
 * __cgroup_bpf_run_filter_sock_ops() - Run a program on a sock
 * @sk: socket to get cgroup from
 * @sock_ops: bpf_sock_ops_kern struct to pass to program. Contains
 * sk with connection information (IP addresses, etc.) May not contain
 * cgroup info if it is a req sock.
 * @type: The type of program to be exectuted
 *
 * socket passed is expected to be of type INET or INET6.
 *
 * The program type passed in via @type must be suitable for sock_ops
 * filtering. No further check is performed to assert that.
 *
 * This function will return %-EPERM if any if an attached program was found
 * and if it returned != 1 during execution. In all other cases, 0 is returned.
 */
int __cgroup_bpf_run_filter_sock_ops(struct sock *sk,
				     struct bpf_sock_ops_kern *sock_ops, enum bpf_attach_type type)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	int ret;

	ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], sock_ops,
				 BPF_PROG_RUN);
	return ret == 1 ? 0 : -EPERM;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_sock_ops);
```

So **<mark>for a successful run</mark>**, `tcp_call_bpf()` expects a return value
of `0` from `BPF_CGROUP_RUN_PROG_SOCK_OPS`, which effectively requires our handler
`set_initial_rto()` returning `1`.

That's all! We've done with our handler.

# 4 Debug facility

`bpf_trace_printk()` can be used to print logs to kernel's tracing facility,
see appendix for the full code, the output looks like this:

```shell
$ sudo cat /sys/kernel/debug/tracing/trace_pipe
 ...
  NetworkManager-709     [000] .... 1492923.972742: 0: Miss, op=6
          <idle>-0       [003] ..s. 1492924.174092: 0: Miss, op=4
          <idle>-0       [002] ..s. 1492938.675528: 0: Set TCP connect timeout = 3s
          <idle>-0       [002] ..s. 1492938.675575: 0: Set TCP connect timeout = 3s
```

# 5 Summary

This post creates a simple BPF program to dynamically change TCP initial RTO, which
reveals the tip of the BPF iceberg.

Without this facility, in some cases, we have to modify and re-compile the kernel
to achieve the same effects. In this sense, BPF make Linux kernel programmable.

# References

1. [bpf: Adding support for sock_ops](https://lwn.net/Articles/727189/)
2. [samples/bpf/tcp_clamp_kern.c](http://github.com/torvalds/linux/blob/v4.19/samples/bpf/tcp_clamp_kern.c), linux v4.19 

# Appendix: full code

```c
#include <linux/bpf.h>

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

__section("sockops")
int set_initial_rto(struct bpf_sock_ops *skops)
{
    const int timeout = 3;
    const int hz = 250;    // grep 'CONFIG_HZ=' /boot/config-$(uname -r), HZ of my machine

    int op = (int) skops->op;
    if (op == BPF_SOCK_OPS_TIMEOUT_INIT) {
        skops->reply = hz * timeout; // 3 seconds
        printk("Set TCP connect timeout = %ds\n", timeout);
        return 1;
    }

    printk("Miss, op=%d\n", op);
    return 1;
}

char _license[] __section("license") = "GPL";
```
