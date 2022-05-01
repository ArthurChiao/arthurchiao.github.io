---
layout    : post
title     : "BPF 进阶笔记（一）：BPF 程序（BPF Prog）类型详解：使用场景、函数签名、执行位置及程序示例"
date      : 2021-07-04
lastupdate: 2022-05-01
categories: bpf xdp socket cgroup
---

## 关于本文

内核目前支持 [30 来种](https://github.com/torvalds/linux/blob/v5.8/include/uapi/linux/bpf.h#L161)
BPF 程序类型。对于主要的程序类型，本文将介绍其：

1. **使用场景**：适合用来做什么？
1. **Hook 位置**：在**<mark>何处（where）、何时（when）</mark>**会触发执行？例如在内核协议栈的哪个位置，或是什么事件触发执行。
1. **程序签名**（程序 **<mark>入口函数</mark>** 签名）
    1. 传入参数：调用到 BPF 程序时，传给它的上下文（context，也就是函数参数）是什么？
    1. 返回值：返回值类型、含义、合法列表等。
1. **加载方式**：如何将程序附着（attach）到执行点？
1. **程序示例**：一些实际例子。
1. **延伸阅读**：其他高级主题，例如相关的内核设计与实现。

本文参考：

1. [BPF: A Tour of Program Types](https://blogs.oracle.com/linux/notes-on-bpf-1)，内容略老，基于内核 `4.14`
1. [BPF Features by Linux Kernel Version](https://github.com/iovisor/bcc/blob/v0.20.0/docs/kernel-versions.md)，bcc 文档，`v0.20.0`

## 关于 “BPF 进阶笔记” 系列

平时学习使用 BPF 时所整理。由于是笔记而非教程，因此内容不会追求连贯，有基础的
同学可作查漏补缺之用。

文中涉及的代码，如无特殊说明，均基于内核 **<mark>5.8/5.10</mark>** 版本。

* [BPF 进阶笔记（一）：BPF 程序（BPF Prog）类型详解：使用场景、函数签名、执行位置及程序示例]({% link _posts/2021-07-04-bpf-advanced-notes-1-zh.md %})
* [BPF 进阶笔记（二）：BPF Map 类型详解：使用场景、程序示例]({% link _posts/2021-07-04-bpf-advanced-notes-2-zh.md %})
* [BPF 进阶笔记（三）：BPF Map 内核实现]({% link _posts/2021-07-04-bpf-advanced-notes-3-zh.md %})

----

* TOC
{:toc}

----

# 基础

## BPF 程序类型：完整列表

Kernel 5.10 支持的 BPF 程序类型：

```c
// https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/bpf.h#L170

enum bpf_prog_type {
    BPF_PROG_TYPE_UNSPEC,
    BPF_PROG_TYPE_SOCKET_FILTER,
    BPF_PROG_TYPE_KPROBE,
    BPF_PROG_TYPE_SCHED_CLS,
    BPF_PROG_TYPE_SCHED_ACT,
    BPF_PROG_TYPE_TRACEPOINT,
    BPF_PROG_TYPE_XDP,
    BPF_PROG_TYPE_PERF_EVENT,
    BPF_PROG_TYPE_CGROUP_SKB,
    BPF_PROG_TYPE_CGROUP_SOCK,
    BPF_PROG_TYPE_LWT_IN,
    BPF_PROG_TYPE_LWT_OUT,
    BPF_PROG_TYPE_LWT_XMIT,
    BPF_PROG_TYPE_SOCK_OPS,
    BPF_PROG_TYPE_SK_SKB,
    BPF_PROG_TYPE_CGROUP_DEVICE,
    BPF_PROG_TYPE_SK_MSG,
    BPF_PROG_TYPE_RAW_TRACEPOINT,
    BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
    BPF_PROG_TYPE_LWT_SEG6LOCAL,
    BPF_PROG_TYPE_LIRC_MODE2,
    BPF_PROG_TYPE_SK_REUSEPORT,
    BPF_PROG_TYPE_FLOW_DISSECTOR,
    BPF_PROG_TYPE_CGROUP_SYSCTL,
    BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE,
    BPF_PROG_TYPE_CGROUP_SOCKOPT,
    BPF_PROG_TYPE_TRACING,
    BPF_PROG_TYPE_STRUCT_OPS,
    BPF_PROG_TYPE_EXT,
    BPF_PROG_TYPE_LSM,
    BPF_PROG_TYPE_SK_LOOKUP,
};
```

## 每种程序能使用的 helper 函数：完整列表

见 [bcc 维护的文档](https://github.com/iovisor/bcc/blob/master/docs/kernel-versions.md#program-types)。
<a name="enum-bpf_attach_type"></a>
## BPF attach 类型：完整列表

通过 `socket()` 系统调用将 BPF 程序 attach 到 hook 点时用到，

```c
// https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/bpf.h#L204

enum bpf_attach_type {
    BPF_CGROUP_INET_INGRESS,
    BPF_CGROUP_INET_EGRESS,
    BPF_CGROUP_INET_SOCK_CREATE,
    BPF_CGROUP_SOCK_OPS,
    BPF_SK_SKB_STREAM_PARSER,
    BPF_SK_SKB_STREAM_VERDICT,
    BPF_CGROUP_DEVICE,
    BPF_SK_MSG_VERDICT,
    BPF_CGROUP_INET4_BIND,
    BPF_CGROUP_INET6_BIND,
    BPF_CGROUP_INET4_CONNECT,
    BPF_CGROUP_INET6_CONNECT,
    BPF_CGROUP_INET4_POST_BIND,
    BPF_CGROUP_INET6_POST_BIND,
    BPF_CGROUP_UDP4_SENDMSG,
    BPF_CGROUP_UDP6_SENDMSG,
    BPF_LIRC_MODE2,
    BPF_FLOW_DISSECTOR,
    BPF_CGROUP_SYSCTL,
    BPF_CGROUP_UDP4_RECVMSG,
    BPF_CGROUP_UDP6_RECVMSG,
    BPF_CGROUP_GETSOCKOPT,
    BPF_CGROUP_SETSOCKOPT,
    BPF_TRACE_RAW_TP,
    BPF_TRACE_FENTRY,
    BPF_TRACE_FEXIT,
    BPF_MODIFY_RETURN,
    BPF_LSM_MAC,
    BPF_TRACE_ITER,
    BPF_CGROUP_INET4_GETPEERNAME,
    BPF_CGROUP_INET6_GETPEERNAME,
    BPF_CGROUP_INET4_GETSOCKNAME,
    BPF_CGROUP_INET6_GETSOCKNAME,
    BPF_XDP_DEVMAP,
    BPF_CGROUP_INET_SOCK_RELEASE,
    BPF_XDP_CPUMAP,
    BPF_SK_LOOKUP,
    BPF_XDP,
    __MAX_BPF_ATTACH_TYPE
};
```

`BPF_CGROUP_DEVICE` 使用场景可参考 [(译) Control Group v2 (cgroupv2)（KernelDoc, 2021）]({% link _posts/2021-09-10-cgroupv2-zh.md %})。

# ------------------------------------------------------------------------
# Socket 相关类型
# ------------------------------------------------------------------------

用于 **<mark>过滤和重定向 socket 数据，或者监听 socket 事件</mark>**。类型包括：

* `BPF_PROG_TYPE_SOCKET_FILTER`
* `BPF_PROG_TYPE_SOCK_OPS`
* `BPF_PROG_TYPE_SK_SKB`
* `BPF_PROG_TYPE_SK_MSG`
* `BPF_PROG_TYPE_SK_REUSEPORT`

# 1 `BPF_PROG_TYPE_SOCKET_FILTER`

## 使用场景

### 场景一：流量过滤/复制（只读，相当于抓包）

从名字 **<mark>SOCKET_FILTER</mark>** 可以看出，这种类型的 BPF 程序能对流量进行
过滤（filtering）。

### 场景二：可观测性：流量统计

仍然是对流量进行过滤，但只统计流量信息，不要包本身。

## Hook 位置：`sock_queue_rcv_skb()`

在 [`sock_queue_rcv_skb()`](https://github.com/torvalds/linux/blob/v5.10/net/core/sock.c#L473) 中触发执行：

```c
// net/core/sock.c

// 处理 socket 入向流量，TCP/UDP/ICMP/raw-socket 等协议类型都会执行到这里
int sock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
    err = sk_filter(sk, skb); // 执行 BPF 代码，这里返回的 err 表示对这个包保留前多少字节（trim）
    if (err)                  // 如果字节数大于 0
        return err;           // 跳过接下来的处理逻辑，直接返回到更上层

    // 如果字节数等于 0，继续执行内核正常的 socket receive 逻辑
    return __sock_queue_rcv_skb(sk, skb);
}
```


## 程序签名

<a name="struct-__sk_buff"></a>
### 传入参数：`struct __sk_buff *`

上面可以看到，hook 入口 `sk_filter(sk, skb)` 传的是 `struct sk_buff *skb`，
但经过层层传递，最终传递给 BPF 程序的其实是 `struct __sk_buff *`。
这个结构体的**<mark>定义</mark>**见 [include/uapi/linux/bpf.h](https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/bpf.h#L4080)，

```c
// include/uapi/linux/bpf.h

// user accessible mirror of in-kernel sk_buff.
struct __sk_buff {
    ...
}
```

* 如注释所说，它是对 `struct sk_buff` 的**<mark>用户可访问字段</mark>**的镜像。
* BPF 程序中对 `struct __sk_buff` 字段的访问，将会被 **<mark>BPF 校验器转换成对相应的
  struct sk_buff 字段的访问</mark>**。
* **<mark>为什么要多引入这一层封装</mark>**，见 [bpf: allow extended BPF programs access skb fields](https://lwn.net/Articles/636647)。

### 返回值

再来看一下 hook 前后的逻辑：

```c
// net/core/sock.c

// 处理 socket 入向流量，TCP/UDP/ICMP/raw-socket 等协议类型都会执行到这里
int sock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
    err = sk_filter(sk, skb); // 执行 BPF 代码，这里返回的 err 表示对这个包保留前多少字节（trim）
    if (err)                  // 如果字节数大于 0
        return err;           // 跳过接下来的处理逻辑，直接返回到更上层

    // 如果字节数等于 0，继续执行内核正常的 socket receive 逻辑
    return __sock_queue_rcv_skb(sk, skb);
}
```

如果 `sk_filter()` 的返回值 `err`

1. `err != 0`：直接 `return err`，返回到调用方，**<mark>不再继续原来正常的内核处理逻辑</mark>** `__sock_queue_rcv_skb()`；所以效果就是：**<mark>将这个包过滤了出来</mark>**（符合过滤条件）；
2. `err == 0`：接下来继续执行正常的内核处理，也就是**<mark>这个包不符合过滤条件</mark>**；

所以至此大概就知道要实现过滤和截断功能，程序应该返回什么了。要精确搞清楚，需要
看 `sk_filter()` 一直调用到 BPF 程序的代码，看中间是否对 BPF 程序的返回值做了封
装和转换。

这里给出结论：BPF 程序的**<mark>返回值</mark>**，

* `n`（`n < pkt_size`）：返回一个 **截断的包**（副本），只保留前面 `n` 个字节。
* `0`：**忽略**这个包；

需要说明：

1. 这里所谓的截断并不是截断原始包，而只是<mark>复制一份包的元数据</mark>，修改其中的包长字段；
1. 程序本身不会截断或丢弃原始流量，也就是说，对**<mark>原始流量是只读的</mark>**（read only）；

## 加载方式：`setsockopt()`

通过 <code>setsockopt(fd, <mark>SO_ATTACH_BPF</mark>, ...)</code> 系统调用，其中 fd 是
**<mark>BPF 程序的文件描述符</mark>**。

## 程序示例

### 1. 可观测性：内核自带 `samples/bpf/sockex1` ~ `samples/bpf/sockex3`

这三个例子都是用 BPF 程序 **<mark>过滤网络设备设备上的包</mark>**，
根据协议类型、IP、端口等信息统计流量。

源码 [samples/bpf/](https://github.com/torvalds/linux/blob/v5.10/samples/bpf/)。

```shell
$ cd samples/bpf/
$ make
$ ./sockex1
```

### 2. 流量复制：每个包只保留前 N 个字节

下面的例子根据包的协议类型对包进行截断。简单起见，不解析 IPv4 option 字段。

```c
#include <uapi/linux/bpf.h>
#include <uapi/linux/in.h>
#include <uapi/linux/types.h>
#include <uapi/linux/string.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/udp.h>
#include <bpf/bpf_helpers.h>

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

// We are only interested in TCP/UDP headers, so drop every other protocol
// and trim packets after the TCP/UDP header by returning eth_len + ipv4_hdr + TCP/UDP header
__section("socket")
int bpf_trim_skb(struct __sk_buff *skb)
{
    int proto = load_byte(skb, ETH_HLEN + offsetof(struct iphdr, protocol));
    int size = ETH_HLEN + sizeof(struct iphdr);

    switch (proto) {
        case IPPROTO_TCP: size += sizeof(struct tcphdr); break;
        case IPPROTO_UDP: size += sizeof(struct udphdr); break;
        default: size = 0; break;                               // drop this packet
    }
    return size;
}

char _license[] __section("license") = "GPL";
```

编译及测试：比较简单的方法是将源文件放到内核源码树中 `samples/bpf/` 目录下。
参考其中的 `sockex1` 来编译、加载和测试。

## 延伸阅读

相关实现见 [`sk_filter_trim_cap()`](https://github.com/torvalds/linux/blob/v5.8/net/core/filter.c#L90)，
它会进一步调用 [`bpf_prog_run_save_cb()`](https://github.com/torvalds/linux/blob/v5.8/include/linux/filter.h#L679) 。

# 2 `BPF_PROG_TYPE_SOCK_OPS`

## 使用场景：动态跟踪/修改 socket 操作

这里所说的 socket 事件包括建连（connection establishment）、重传（retransmit）、超时（timeout）等等。

### 场景一：动态跟踪：监听 socket 事件

这种场景只会拦截和解析系统事件，不会修改任何东西。

### 场景二：动态修改 socket（例如 tcp 建连）操作

<mark>拦截到事件后</mark>，通过 `bpf_setsockopt()` <mark>动态修改 socket 配置</mark>，
能够实现 per-connection 的优化，提升性能。例如，

1. 监听到被动建连（passive establishment of a connection）事件时，如果
   **对端和本机不在同一个网段**，就**<mark>动态修改这个 socket 的 MTU</mark>**。
   这样能避免包因为太大而被中间路由器分片（fragmenting）。
2. **<mark>替换目的 IP 地址</mark>**，实现高性能的**透明代理及负载均衡**。
   Cilium 对 K8s 的 Service 就是这样实现的，详见 [1]。

### 场景三：socket redirection（需要 `BPF_PROG_TYPE_SK_SKB` 程序配合）

这个其实算是“动态修改”的特例。与 `BPF_PROG_TYPE_SK_SKB` 程序配合，通过
sockmap+redirection 实现 socket 重定向。这种情况下分为两段 BPF 程序，

* 第一段是 `BPF_PROG_TYPE_SOCK_OPS` 程序，拦截 socket 事件，并从 `struct bpf_sock_ops` 中提取 socket 信息存储到 sockmap；
* 第二段是 `BPF_PROG_TYPE_SK_SKB` 类型程序，从拦截到的 socket message 提取
  socket 信息，然后去 sockmap 查找对端 socket，然后通过 `bpf_sk_redirect_map()`
  直接重定向过去。

## Hook 位置：多个地方

其他类型的 BPF 程序都是在某个特定的代码出执行的，但 SOCK_OPS 程序不同，它们
**<mark>在多个地方执行，op 字段表示触发执行的地方</mark>**。
`op` 字段是枚举类型，[完整列表](https://github.com/torvalds/linux/blob/v5.8/include/uapi/linux/bpf.h#L4002)：

```c
// include/uapi/linux/bpf.h

/* List of known BPF sock_ops operators. */
enum {
    BPF_SOCK_OPS_VOID,
    BPF_SOCK_OPS_TIMEOUT_INIT,          // 初始化 TCP RTO 时调用 BPF 程序
                                        //   程序应当返回希望使用的 SYN-RTO 值；-1 表示使用默认值
    BPF_SOCK_OPS_RWND_INIT,             // BPF 程序应当返回 initial advertized window (in packets)；-1 表示使用默认值
    BPF_SOCK_OPS_TCP_CONNECT_CB,        // 主动建连 初始化之前 回调 BPF 程序
    BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB, // 主动建连 成功之后   回调 BPF 程序
    BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB,// 被动建连 成功之后   回调 BPF 程序
    BPF_SOCK_OPS_NEEDS_ECN,             // If connection's congestion control needs ECN */
    BPF_SOCK_OPS_BASE_RTT,              // 获取 base RTT。The correct value is based on the path，可能还与拥塞控制
                                        //   算法相关。In general it indicates
                                        //   a congestion threshold. RTTs above this indicate congestion
    BPF_SOCK_OPS_RTO_CB,                // 触发 RTO（超时重传）时回调 BPF 程序，三个参数：
                                        //   Arg1: value of icsk_retransmits
                                        //   Arg2: value of icsk_rto
                                        //   Arg3: whether RTO has expired
    BPF_SOCK_OPS_RETRANS_CB,            // skb 发生重传之后，回调 BPF 程序，三个参数：
                                        //   Arg1: sequence number of 1st byte
                                        //   Arg2: # segments
                                        //   Arg3: return value of tcp_transmit_skb (0 => success)
    BPF_SOCK_OPS_STATE_CB,              // TCP 状态发生变化时，回调 BPF 程序。参数：
                                        //   Arg1: old_state
                                        //   Arg2: new_state
    BPF_SOCK_OPS_TCP_LISTEN_CB,         // 执行 listen(2) 系统调用，socket 进入 LISTEN 状态之后，回调 BPF 程序
};
```

从以上注释可以看到，这些 OPS 分为两种类型：

1. **<mark>通过 BPF 程序的返回值来动态修改配置</mark>**，类型包括

    1. `BPF_SOCK_OPS_TIMEOUT_INIT`
    1. `BPF_SOCK_OPS_RWND_INIT`
    1. `BPF_SOCK_OPS_NEEDS_ECN`
    1. `BPF_SOCK_OPS_BASE_RTT`

2. 在 socket/tcp 状态发生变化时，**<mark>回调（callback）BPF 程序</mark>**，类型包括

    1. `BPF_SOCK_OPS_TCP_CONNECT_CB`
    1. `BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB`
    1. `BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB`
    1. `BPF_SOCK_OPS_RTO_CB`
    1. `BPF_SOCK_OPS_RETRANS_CB`
    1. `BPF_SOCK_OPS_STATE_CB`
    1. `BPF_SOCK_OPS_TCP_LISTEN_CB`

引入该功能的内核 patch 见 [bpf: Adding support for sock_ops](https://lwn.net/Articles/727189/)；

SOCK_OPS 类型的 BPF 程序**<mark>都是从</mark>** [tcp_call_bpf()](https://github.com/torvalds/linux/blob/v5.8/include/net/tcp.h#L2227)
**<mark>调用过来的</mark>**，这个文件中多个地方都会调用到该函数。

## 程序签名
### 传入参数： `struct bpf_sock_ops *`

结构体 [定义](https://github.com/torvalds/linux/blob/v5.8/include/uapi/linux/bpf.h#L3946)，

```c
// include/uapi/linux/bpf.h

struct bpf_sock_ops {
    __u32 op;               // socket 事件类型，就是上面的 BPF_SOCK_OPS_*
    union {
        __u32 args[4];      // Optionally passed to bpf program
        __u32 reply;        // BPF 程序的返回值。例如，op==BPF_SOCK_OPS_TIMEOUT_INIT 时，
                            //   BPF 程序的返回值就表示希望为这个 TCP 连接设置的 RTO 值
        __u32 replylong[4]; // Optionally returned by bpf prog
    };
    __u32 family;
    __u32 remote_ip4;        /* Stored in network byte order */
    __u32 local_ip4;         /* Stored in network byte order */
    __u32 remote_ip6[4];     /* Stored in network byte order */
    __u32 local_ip6[4];      /* Stored in network byte order */
    __u32 remote_port;       /* Stored in network byte order */
    __u32 local_port;        /* stored in host byte order */
    ...
};
```

### 返回值

如前面所述，ops 类型不同，返回值也不同。

## 加载方式：attach 到某个 cgroup（可使用 bpftool 等工具）

指定以 `BPF_CGROUP_SOCK_OPS` 类型，将 BPF 程序 attach 到某个 cgroup 文件描述符。

<mark>依赖 cgroupv2</mark>。

内核已经有了 `BPF_PROG_TYPE_CGROUP_SOCK` 类型的 BPF 程序，这里为什么又要引入一个
`BPF_PROG_TYPE_SOCK_OPS` 类型的程序呢？

1. `BPF_PROG_TYPE_CGROUP_SOCK` 类型的 BPF 程序：在一个连接（connection）的生命周期中**<mark>只执行一次</mark>**，
2. `BPF_PROG_TYPE_SOCK_OPS` 类型的 BPF 程序：在一个连接的生命周期中，在**<mark>不同地方被多次调用</mark>**。

## 程序示例

### 1. [Customize TCP initial RTO (retransmission timeout) with BPF]({% link _posts/2021-04-28-customize-tcp-initial-rto-with-bpf.md %})
### 2. [Cracking kubernetes node proxy (aka kube-proxy)]({% link _posts/2019-11-30-cracking-k8s-node-proxy.md %})，其中的第五种实现方式
### 3. [(译) 利用 ebpf sockmap/redirection 提升 socket 性能（2020）]({% link _posts/2021-01-28-socket-acceleration-with-ebpf-zh.md %})

## 延伸阅读

1. [bpf: Adding support for sock_ops](https://lwn.net/Articles/727189/)

# 3 `BPF_PROG_TYPE_SK_SKB`

## 使用场景

### 场景一：修改 skb/socket 信息，socket 重定向

这个功能依赖 sockmap，后者是一种特殊类型的 BPF map，其中存储的是 socket 引用（references）。

典型流程：

* 创建 sockmap
* 拦截 socket 操作，将 socket 信息存入 sockmap
* 拦截 socket sendmsg/recvmsg 等系统调用，从 msg 中提取信息（IP、port 等），然后
  在 sockmap 中查找对端 socket，然后重定向过去。

根据提取到的 socket 信息判断接下来应该做什么的过程称为 <mark>verdict（判决）</mark>。
verdict 类型可以是：

* `__SK_DROP`
* `__SK_PASS`
* `__SK_REDIRECT`

### 场景二：动态解析消息流（stream parsing）

这种程序的一个应用是 [strparser framework](https://www.kernel.org/doc/Documentation/networking/strparser.txt)。

它与上层应用配合，**<mark>在内核中提供应用层消息解析的支持</mark>**（provide kernel support
for application layer messages）。两个使用了 strparser 框架的例子：TLS 和 KCM（
Kernel Connection Multiplexor）。

## Hook 位置

### socket redirection 类型

TODO

### strparser 类型：`smap_parse_func_strparser()` / `smap_verdict_func()`

Socket receive 过程执行到 [`smap_parse_func_strparser()`](https://github.com/torvalds/linux/blob/v5.8/kernel/bpf/sockmap.c)
时，触发 STREAM_PARSER BPF 程序执行。

执行到 `smap_verdict_func()` 时，触发 VERDICT BPF 程序执行。

## 程序签名

### 传入参数： `struct __sk_buff *`

见 [前面](#struct-__sk_buff) 的介绍。

从中可以提取出 socket 信息（IP、port 等）。

### 返回值

TODO

## 加载方式：attach 到某个 sockmap（可使用 bpftool 等工具）

这种程序需要指定 `BPF_SK_SKB_STREAM_*` 类型，将 BPF 程序 attach 到 sockmap：

* 重定向程序：指定 `BPF_SK_SKB_STREAM_VERDICT` 加载到某个 sockmap。
* strparser 程序：指定 `BPF_SK_SKB_STREAM_PARSER` 加载到某个 sockmap。

## 程序示例

### 1. [(译) 利用 ebpf sockmap/redirection 提升 socket 性能（2020）]({% link _posts/2021-01-28-socket-acceleration-with-ebpf-zh.md %})

### 2. `strparser` 框架：解析消息流

## 延伸阅读

1. 内核 patch 文档：[BPF: sockmap and sk redirect support](https://lwn.net/Articles/731133/)

# 4 `BPF_PROG_TYPE_SK_MSG`

# 5 `BPF_PROG_TYPE_SK_REUSEPORT`

## 使用场景

### 场景一：发布系统：新老进程无损流量切换

发布系统要做到服务发布时客户端完全无感知，一种实现方式就是让
**<mark>运行老代码的进程和运行新代码的进程共享同一个端口</mark>**（例如 `80`），
这对 BPF 程序提出的要求就是：能够正确对 `<dst_ip:dst_port>` 相同的流量正确进行分流：

* 一部分转发给老进程（老连接）
* 一部分转发给新进程（新连接）

`BPF_PROG_TYPE_SK_REUSEPORT` + `BPF_MAP_TYPE_REUSEPORT_SOCKARRAY` 最初就是针对这个需求引入的，
前者 hook 到 cgroup level 的socket 事件，后者存储特定信息（BPF 程序的设计者来决定）到 socket 的映射。

更多参考 ：

* [(译) Facebook 流量路由最佳实践：从公网入口到内网业务的全路径 XDP/BPF 基础设施（LPC, 2021）]({% link _posts/2021-12-05-facebook-from-xdp-to-socket-zh.md %})。
* 内核 patch [Introduce BPF_MAP_TYPE_REUSEPORT_SOCKARRAY and BPF_PROG_TYPE_SK_REUSEPORT](http://archive.lwn.net:8080/netdev/20180808080131.3014367-1-kafai@fb.com/t/)。

### 场景二：加速 socket 查找

这种程序类型是为了加速 listener socket 的查找速度，使用方式：

1. 创建一个 `BPF_MAP_TYPE_REUSEPORT_SOCKARRAY` 类型的数组，来存放监听在同一端口的所有 sockets。
2. 加载一段 `BPF_PROG_TYPE_SK_REUSEPORT` 类型的 BPF 程序，程序返回值是 socket
   数组中的 index。接下来系统就会选中这个 index 对应的 socket。

## Hook 位置

## 程序签名

### 传入参数

### 返回值

Socket array 中的 index。

## 内核实现

逻辑如下，还是非常简单的：

```c
// net/core/sock_reuseport.c

static struct sock *run_bpf_filter(struct sock_reuseport *reuse, u16 socks,
                   struct bpf_prog *prog, struct sk_buff *skb, int hdr_len)
{
    if (skb_shared(skb)) {
        struct sk_buff *nskb = skb_clone(skb, GFP_ATOMIC);
        skb = nskb;
    }

    pskb_pull(skb, hdr_len); // temporarily advance data past protocol header
    index = bpf_prog_run_save_cb(prog, skb);
    __skb_push(skb, hdr_len);

    consume_skb(nskb);

    if (index >= socks)
        return NULL;

    return reuse->socks[index];
}
```

# ------------------------------------------------------------------------
# TC 子系统相关类型
# ------------------------------------------------------------------------

将 BPF 程序用作 tc 分类器（classifiers）和执行器（actions）。

* `BPF_PROG_TYPE_SCHED_CLS`：tc classifier，分类器
* `BPF_PROG_TYPE_SCHED_ACT`：tc action，动作

<mark>TC 是 Linux 的 QoS 子系统</mark>。帮助信息（非常有用）：

* `tc(8)` manpage for a general introduction
* `tc-bpf(8)` for BPF specifics

# 1 `BPF_PROG_TYPE_SCHED_CLS`

## 使用场景

### 场景一：tc 分类器

`tc(8)` 命令<mark>支持 eBPF</mark>，因此能直接将 BPF 程序作为
classifiers 和 actions 加载到 ingress/egress hook 点。

如何使用 tc BPF 提供的能力，参考 [man8: tc-bpf](http://man7.org/linux/man-pages/man8/tc-bpf.8.html)。

## Hook 位置：`sch_handle_ingress()`/`sch_handle_egress()`

`sch_handle_ingress()/egress()` 会调用到 `tcf_classify()`，

* 对于 ingress，通过网络设备的 receive 方法做**<mark>流量分类</mark>**，这个处
  理位置在**<mark>网卡驱动处理之后，在内核协议栈（IP 层）处理之前</mark>**。
* 对于 egress，将包交给设备队列（device queue）发送之前，执行 BPF 程序。

## 程序签名

### 传入参数：`struct __sk_buff *`

见 [前面](#struct-__sk_buff) 的介绍。

### 返回值

返回 TC verdict 结果。

## 加载方式：`tc` 命令（背后使用 netlink）

步骤：

1. <mark>为网络设备添加分类器</mark>（classifier/qdisc）：创建一个 "clsact" qdisc
2. <mark>为网络设备添加过滤器</mark>（filter）：需要指定方向（egress/ingress）、目标文件、ELF section 等选项

例如，

```shell
$ tc qdisc add dev eth0 clsact
$ tc filter add dev eth0 egress bpf da obj toy-proxy-bpf.o sec egress
```

加载过程分为 tc 前端和内核 bpf 后端两部分，**<mark>中间通过 netlink socket 通信，源码分析见</mark>**
[Firewalling with BPF/XDP: Examples and Deep Dive]({ % link _posts/2021-06-27-firewalling-with-bpf-xdp.md %})

## 程序示例

1. [Firewalling with BPF/XDP: Examples and Deep Dive]({ % link _posts/2021-06-27-firewalling-with-bpf-xdp.md %})
1. [Cracking Kubernetes Node Proxy (aka kube-proxy)]({ % link _posts/2019-11-30-cracking-k8s-node-proxy.md %})

## 延伸阅读

* [cls_bpf.c]() 实现 tc classifier 模块

# 2 `BPF_PROG_TYPE_SCHED_ACT`

使用方式与 `BPF_PROG_TYPE_SCHED_CLS` 类似，但用作 TC action。

## 使用场景

### 场景一：tc action

## Hook 位置

## 程序签名

## 加载方式：`tc` 命令

## 延伸阅读

* [act_bpf.c]() 实现 tc action 模块

# ------------------------------------------------------------------------
# XDP（eXpress Data Path）程序
# ------------------------------------------------------------------------

XDP 位于<mark>设备驱动中（在创建 skb 之前）</mark>，因此能最大化网络处理性能，
而且可编程、通用（很多厂商的设备都支持）。

各厂商网卡/驱动对 **<mark>XDP 及其内核版本</mark>**的支持，见 bcc
[维护的文档](https://github.com/iovisor/bcc/blob/master/docs/kernel-versions.md#xdp)。

# 1 `BPF_PROG_TYPE_XDP`

## 使用场景

### 场景一：防火墙、四层负载均衡等

由于 XDP 程序执行时 skb 都还没创建，开销非常低，因此效率非常高。适用于 DDoS 防御、四层负载均衡等场景。

XDP 就是通过 BPF hook 对内核进行**<mark>运行时编程</mark>**（run-time
programming），但**<mark>基于内核而不是绕过（bypass）内核</mark>**。

## Hook 位置：网络驱动

XDP 是在**<mark>网络驱动</mark>**中实现的，**<mark>有专门的 TX/RX queue</mark>**（native 方式）。

对于没有实现 XDP 的驱动，内核中实现了一个称为 **<mark>“generic XDP”</mark>** 的 fallback 实现，
见 [net/core/dev.c](https://github.com/torvalds/linux/blob/v5.8/net/core/dev.c)。

* Native XDP：处理的阶段非常早，在 skb 创建之前，因此性能非常高；
* Generic XDP：**<mark>在 skb 创建之后</mark>**，因此性能比前者差，但功能是一样的。

## 程序签名

<a name="struct-xdp_md"></a>
### 传入参数：`struct xdp_md *`

[定义](https://github.com/torvalds/linux/blob/v5.8/include/uapi/linux/bpf.h#L3754)，非常轻量级：

```c
// include/uapi/linux/bpf.h

/* user accessible metadata for XDP packet hook */
struct xdp_md {
    __u32 data;
    __u32 data_end;
    __u32 data_meta;

    /* Below access go through struct xdp_rxq_info */
    __u32 ingress_ifindex; /* rxq->dev->ifindex */
    __u32 rx_queue_index;  /* rxq->queue_index  */
    __u32 egress_ifindex;  /* txq->dev->ifindex */
};
```

### 返回值：`enum xdp_action`

```c
// include/uapi/linux/bpf.h

enum xdp_action {
    XDP_ABORTED = 0,
    XDP_DROP,
    XDP_PASS,
    XDP_TX,
    XDP_REDIRECT,
};
```


## 加载方式：netlink socket

通过 **<mark>netlink socket</mark>** 消息 attach：

* 首先创建一个 netlink 类型的 socket：`socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE)`
* 然后发送一个 `NLA_F_NESTED | 43` 类型的 netlink 消息，表示这是 XDP message。消息中包含 BPF fd, the interface index (ifindex) 等信息。

用 `tc` attach BPF 程序，其实背后使用的也是 netlink socket。

## 程序示例

### 1. [samples/bpf/bpf_load.c](https://github.com/torvalds/linux/blob/v5.8/samples/bpf/bpf_load.c)


## 延伸阅读

TODO

# ------------------------------------------------------------------------
# cgroup (v2) 相关类型
# ------------------------------------------------------------------------

**<mark>cgroup</mark>** 最典型的使用场景是容器（containers）。

* 命名空间（namespace）：控制资源视图，即**<mark>能看到什么，不能看到什么</mark>**，
* cgroup：控制的**<mark>能使用多少</mark>**？

**<mark>cgroup BPF</mark>** 用于在 cgroup 级别对**<mark>进程、socket、设备文件</mark>**
（device file）等进行动态控制，

* 处理资源分配，例如 CPU、网络带宽等。
* 系统资源权限控制（allowing or denying）。
* 控制访问权限（allow or deny），程序的返回结果只有两种：

    1. 放行
    2. 禁止（导致随后包被丢弃）

完整的 cgroups BPF hook 列表见 [前面](#enum-bpf_attach_type) `enum bpf_attach_type`
列表，其中的 `BPF_CGROUP_*`。

# 0 cgroup BPF 通用调用栈

## 0.1 创建 socket 时初始化其 cgroupv2 配置

```c
// https://github.com/torvalds/linux/blob/v5.10/net/core/sock.c#L1715
/**
 *    sk_alloc - All socket objects are allocated here
 *    @net: the applicable net namespace
 *    @family: protocol family
 *    @priority: for allocation (%GFP_KERNEL, %GFP_ATOMIC, etc)
 *    @prot: struct proto associated with this new sock instance
 *    @kern: is this to be a kernel socket?
 */
struct sock *sk_alloc(struct net *net, int family, gfp_t priority, struct proto *prot, int kern)
{
    struct sock *sk = sk_prot_alloc(prot, priority | __GFP_ZERO, family);
    if (sk) {
        sk->sk_family = family;
        sk->sk_prot = sk->sk_prot_creator = prot;
        sk->sk_kern_sock = kern;
        sk->sk_net_refcnt = kern ? 0 : 1;
        if (likely(sk->sk_net_refcnt)) {
            get_net(net);                          // 网络命名空间
            sock_inuse_add(net, 1);
        }

        sock_net_set(sk, net);
        refcount_set(&sk->sk_wmem_alloc, 1);

        mem_cgroup_sk_alloc(sk);                   // memory cgroup 信息单独维护
        cgroup_sk_alloc(&sk->sk_cgrp_data);        // per-socket cgroup 信息，包括了 memory cgroup 之外
                                                   // 该 socket 的 cgroup 信息
        sock_update_classid(&sk->sk_cgrp_data);
        sock_update_netprioidx(&sk->sk_cgrp_data);
        sk_tx_queue_clear(sk);
    }

    return sk;
}
```

可以看到，创建 socket 时会初始化其所属的 cgroup 信息，因此后面就能
**<mark>在 cgroup 级别</mark>**监听 socket 事件或拦截 socket 操作。

## 0.2 入向（ingress）hook 处理

很多 hook 点会执行到下面两个宏来**<mark>执行 cgroup BPF 代码</mark>**：

```c
// include/linux/bpf-cgroup.h

#define BPF_CGROUP_RUN_PROG_INET_INGRESS(sk, skb)                  \
({                                                                 \
    int __ret = 0;                                                 \
    if (cgroup_bpf_enabled)                                        \
        __ret = __cgroup_bpf_run_filter_skb(sk, skb,               \
                            BPF_CGROUP_INET_INGRESS);              \
                                                                   \
    __ret;                                                         \
})
```

函数的定义：

```c
// https://github.com/torvalds/linux/blob/v5.10/kernel/bpf/cgroup.c#L987

int __cgroup_bpf_run_filter_skb(struct sock *sk, struct sk_buff *skb, enum bpf_attach_type type)
{
    unsigned int offset = skb->data - skb_network_header(skb);
    struct sock *save_sk;
    void *saved_data_end;
    struct cgroup *cgrp;
    int ret;

    cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data); // 获取 socket cgroup 信息
    save_sk = skb->sk;
    skb->sk = sk;
    __skb_push(skb, offset);

    bpf_compute_and_save_data_end(skb, &saved_data_end);

    if (type == BPF_CGROUP_INET_EGRESS) {
        ret = BPF_PROG_CGROUP_INET_EGRESS_RUN_ARRAY(cgrp->bpf.effective[type], skb, __bpf_prog_run_save_cb);
    } else {
        ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], skb, __bpf_prog_run_save_cb);
        ret = (ret == 1 ? 0 : -EPERM);
    }
    bpf_restore_data_end(skb, saved_data_end);
    __skb_pull(skb, offset);
    skb->sk = save_sk;

    return ret;
}
```

## 0.3 出向（egress）hook 处理

```c
// include/linux/bpf-cgroup.h

#define BPF_CGROUP_RUN_SK_PROG(sk, type)                       \
({                                                                 \
    int __ret = 0;                                                 \
    if (cgroup_bpf_enabled) {                                      \
        __ret = __cgroup_bpf_run_filter_sk(sk, type);              \
    }                                                              \
    __ret;                                                         \
})
```

函数的定义：

```c
// https://github.com/torvalds/linux/blob/v5.10/kernel/bpf/cgroup.c#L1040
int __cgroup_bpf_run_filter_sk(struct sock *sk, enum bpf_attach_type type)
{
    struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data); // 获取 socket cgroup 信息

    ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], sk, BPF_PROG_RUN);
    return ret == 1 ? 0 : -EPERM;
}
```

# 1 `BPF_PROG_TYPE_CGROUP_SKB`

## 使用场景

### 场景一：在 cgroup 级别：放行/丢弃数据包

在 IP egress/ingress 层禁止或允许网络访问。

## Hook 位置

### 入向：`__sk_receive_skb`/`tcp_v4_rcv->tcp_filter`/`udp_queue_rcv_one_skb` -> `sk_filter_trim_cap()`

对于 ingress，上述三个函数会分别从 **<mark>IP/TCP/UDP 处理逻辑</mark>**里调用到 `sk_filter_trim_cap()`，
后者又会调用 `BPF_CGROUP_RUN_PROG_INET_INGRESS(sk, skb)`。这个宏上面有介绍。

下面代码忽略了一些错误处理：

```c
// https://github.com/torvalds/linux/blob/v5.10/net/core/filter.c#L120

/**
 *    sk_filter_trim_cap - run a packet through a socket filter
 *    @cap: limit on how short the eBPF program may trim the packet
 *
 * Run the eBPF program and then cut skb->data to correct size returned by
 * the program. If pkt_len is 0 we toss packet. If skb->len is smaller
 * than pkt_len we keep whole skb->data. This is the socket level
 * wrapper to BPF_PROG_RUN. It returns 0 if the packet should
 * be accepted or -EPERM if the packet should be tossed.
 *
 */
int sk_filter_trim_cap(struct sock *sk, struct sk_buff *skb, unsigned int cap)
{
    BPF_CGROUP_RUN_PROG_INET_INGRESS(sk, skb); // 上面有介绍
    security_sock_rcv_skb(sk, skb);

    struct sk_filter *filter = rcu_dereference(sk->sk_filter);
    if (filter) {
        struct sock *save_sk = skb->sk;
        unsigned int pkt_len;

        skb->sk = sk;
        pkt_len = bpf_prog_run_save_cb(filter->prog, skb);
        skb->sk = save_sk;
        err = pkt_len ? pskb_trim(skb, max(cap, pkt_len)) : -EPERM;
    }

    return err;
}
```

如果返回值非零，调用方（例如 `__sk_receive_skb()`）随后会将包丢弃并释放。

### 出向：`ip[6]_finish_output()`

egress 是类似的，但在 `ip[6]_finish_output()` 中。

## 程序签名

### 传入参数：`struct sk_buff *skb`

### 返回值

* `1`：放行；
* 其他任何值：会使 `__cgroup_bpf_run_filter_skb()` 返回 `-EPERM`，这会进一步返
  回给调用方，告诉它们应该丢弃该包。

## 加载方式：attach 到 cgroup 文件描述符

根据 BPF attach 的 hook 位置，选择合适的 attach 类型：

* `BPF_CGROUP_INET_INGRESS`
* `BPF_CGROUP_INET_EGRESS`

# 2 `BPF_PROG_TYPE_CGROUP_SOCK`

## 使用场景

### 场景一：在 cgroup 级别：触发 socket 操作时拒绝/放行网络访问

这里的 socket 相关事件包括
BPF_CGROUP_INET_SOCK_CREATE、BPF_CGROUP_SOCK_OPS。

## 程序签名

### 传入参数：`struct sk_buff *skb`

### 返回值

跟前面一样，程序返回 1 表示允许访问。
返回其他值会导致 `__cgroup_bpf_run_filter_sk()` 返回 `-EPERM`，调用方收到这个返回值会将包丢弃。

## 触发执行：`inet_create()`

Socket 创建时会执行 `inet_create()`，里面会调用
`BPF_CGROUP_RUN_PROG_INET_SOCK()`，如果该函数执行失败，socket 就会被释放。

## 加载方式：attach 到 cgroup 文件描述符

TODO

# 3 `BPF_PROG_TYPE_CGROUP_DEVICE`

## 使用场景

### 场景一：设备文件（device file）访问控制

## 程序签名

### 传入参数：`struct bpf_cgroup_dev_ctx *`

```c
// https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/bpf.h#L4833

struct bpf_cgroup_dev_ctx {
    __u32 access_type; /* encoded as (BPF_DEVCG_ACC_* << 16) | BPF_DEVCG_DEV_* */
    __u32 major;
    __u32 minor;
};
```

字段含义：

* `access_type`：访问操作的类型，例如 **<mark><code>mknod/read/write</code></mark>**；
* `major` 和 `minor`：主次设备号；

### 返回值

1. `0`：访问失败（`-EPERM`）
2. 其他值：访问成功。

## 触发执行：创建或访问设备文件时

## 加载方式：attach 到 cgroup 文件描述符

指定 attach 类型为 `BPF_CGROUP_DEVICE`。

## 程序示例

内核测试用例：

1. [tools/testing/selftests/bpf/progs/dev_cgroup.c](https://github.com/torvalds/linux/blob/v5.10/tools/testing/selftests/bpf/progs/dev_cgroup.c)
1. [tools/testing/selftests/bpf/test_dev_cgroup.c](https://github.com/torvalds/linux/blob/v5.10/tools/testing/selftests/bpf/test_dev_cgroup.c)

## 延伸阅读

可参考 [(译) Control Group v2 (cgroupv2)（KernelDoc, 2021）]({% link _posts/2021-09-10-cgroupv2-zh.md %})。

# ------------------------------------------------------------------------
# kprobes、tracepoints、perf events
# ------------------------------------------------------------------------

<mark>三者都用于 kernel instrumentation</mark>。简单对比：

| 数据源    | Type | Kernel/User space | |
|:----|:----|:----|:----|
| kprobes     | Dynamic | Kernel | 观测内核函数的运行时（进入和离开函数）参数值等信息 |
| uprobes     | Dynamic | Userspace | 同上，但观测的是用户态函数 |
| tracepoints | Static  | Kernel | 将自定义 handler 编译并加载到某些内核 hook，能拿到更多观测信息 |
| USDT        | Static  | Userspace | |

> 更具体区别可参考
> [Linux tracing systems & how they fit together](https://jvns.ca/blog/2017/07/05/linux-tracing-systems/)。

* [kprobes](https://www.kernel.org/doc/Documentation/kprobes.txt)：对**<mark>特定函数</mark>**进行 instrumentation。

    * **进入**函数时触发 `kprobe`
    * **离开**函数时触发 `kretprobe`

    启用后，会 **<mark>将 probe 位置的一段空代码替换为一个断点指令</mark>**。
    当程序执行到这个断点时，会**触发一条 trap 指令**，然后保存寄存器状态，
    **跳转到指定的处理函数**（instrumentation handler）。

    * kprobes 由 [kprobe_dispatcher()](https://github.com/torvalds/linux/blob/v5.8/kernel/trace/trace_kprobe.c#L1697) 处理，
      其中会获取 kprobe 的地址和寄存器上下文信息。
    * kretprobes 是通过 kprobes 实现的。

* [Tracepoints](https://www.kernel.org/doc/Documentation/trace/tracepoints.rst)：内核中的轻量级 hook。

    Tracepoints 与 kprobes 类似，但
    前者是动态插入代码来完成的，后者**<mark>显式地（静态地）写在代码中的</mark>**。
    启用之后，会**<mark>从这些地方收集 debug 信息</mark>**。

    同一个 tracepoints 可能会在多个地方声明；例如，
    `trace_drv_return_int()` 在 net/mac80211/driver-ops.c 中的多个地方被调用。

    <mark>查看可用的 tracepoints 列表</mark>：`ls /sys/kernel/debug/tracing/events`。

* [Perf events](https://perf.wiki.kernel.org/index.php/Main_Page)：是这里提到的几种 eBPF 程序的基础。

    BPF 基于已有的基础设施来完成事件采样（event sampling），允许 attach 程序到
    感兴趣的 perf 事件，包括 kprobes, uprobes, tracepoints 以及软件和硬件事件。

这些 instrumentation points **<mark>使 BPF 成为了一个通用的跟踪工具</mark>**，
超越了最初的网络范畴。

# 1 `BPF_PROG_TYPE_KPROBE`

## 使用场景

### 场景一：观测内核函数（kprobe）和用户空间函数（uprobe）

通过 `kprobe`/`kretprobe` 观测内核函数。
`k[ret]probe_perf_func()` 会执行加载到 probe 点的 BPF 程序。

另外，这种程序也能 attach 到 `u[ret]probes`，详情见
[uprobetracer.txt](https://www.kernel.org/doc/Documentation/trace/uprobetracer.txt)。

## Hook 位置：`k[ret]probe_perf_func()`/`u[ret]probe_perf_func()`

启用某个 probe 并执行到断点时，`k[ret]probe_perf_func()` 会通过
`trace_call_bpf()` 执行 attach 在这个 probe 位置的 BPF 程序。

`u[ret]probe_perf_func()` 也是类似的。


## 程序签名
### 传入参数：`struct pt_regs *ctx`

可以通过这个指针<mark>访问寄存器</mark>。

这个变量内的很多字段是平台相关的，但也有一些通用函数，例如
`regs_return_value(regs)`，返回的是存储程序返回值的寄存器内的值（x86 上对应的是 `ax` 寄存器）。

### 返回值


## 加载方式：`/sys/fs/debug/tracing/` 目录下的配置文件

* `/sys/kernel/debug/tracing/events/[uk]probe/<probename>/id`
* `/sys/kernel/debug/tracing/events/[uk]retprobe/<probename>/id`

## 程序示例

[Documentation/trace/kprobetrace.txt](https://www.kernel.org/doc/Documentation/trace/kprobetrace.txt)
有详细的例子。例如，

```shell
# 创建一个名为 `myprobe` 的程序，attach 到进入函数 `tcp_retransmit_skb()` 的地方
$ echo 'p:myprobe tcp_retransmit_skb' > /sys/kernel/debug/tracing/kprobe_events

# 获取 probe id
$ cat /sys/kernel/debug/tracing/events/kprobes/myprobe/id
2266
```

用以上 id 打开一个 perf event，启用它，然后将这个 perf event 的 BPF 程序指定为我们的程序。
过程可参考 [load_and_attach()](https://github.com/torvalds/linux/blob/v5.8/samples/bpf/bpf_load.c#L76)：

```c
// samples/bpf/bpf_load.c

static int load_and_attach(const char *event, struct bpf_insn *prog, int size)
{
    struct perf_event_attr attr;

    /* Load BPF program and assign programfd to it; and get probeid of probe from sysfs */
    attr.type = PERF_TYPE_TRACEPOINT;
    attr.sample_type = PERF_SAMPLE_RAW;
    attr.sample_period = 1;
    attr.wakeup_events = 1;
    attr.config = probeid;               // /sys/kernel/debug/tracing/events/kprobes/<probe>/id

    eventfd = sys_perf_event_open(&attr, -1, 0, programfd, 0);
    ioctl(eventfd, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(eventfd, PERF_EVENT_IOC_SET_BPF, programfd);
    ...
}
```

## 延伸阅读

TODO

# 2 `BPF_PROG_TYPE_TRACEPOINT`

## 使用场景

### 场景一：Instrument 内核代码中的 tracepoints

启用方式和上面的 kprobe 类似：

```shell
$ echo 1 > /sys/kernel/xxx/enable
```

**<mark>可跟踪的事件都在 <code>/sys/kernel/debug/tracing/events</code> 目录下面</mark>**。

## Hook 位置：`perf_trace_<event_class>()`

相应的 tracepoint 启用并执行到之后，`perf_trace_<event_class>()` （定义见 include/trace/perf.h）
调用 `perf_trace_run_bpf_submit()`，后者通过 `trace_call_bpf()` 触发 BPF 程序执行。

## 程序签名
### 传入参数：因 tracepoint 而异

传入的参数和类型因 tracepoint 而异，见其定义。

见 `/sys/kernel/debug/tracing/events/<tracepoint>/format`。例如，

```shell
$ sudo cat /sys/kernel/debug/tracing/events/net/netif_rx/format
name: netif_rx
ID: 1457
format:
        field:unsigned short common_type;       offset:0;       size:2; signed:0;
        field:unsigned char common_flags;       offset:2;       size:1; signed:0;
        field:unsigned char common_preempt_count;       offset:3;       size:1; signed:0;
        field:int common_pid;   offset:4;       size:4; signed:1;

        field:void * skbaddr;   offset:8;       size:8; signed:0;
        field:unsigned int len; offset:16;      size:4; signed:0;
        field:__data_loc char[] name;   offset:20;      size:4; signed:1;

print fmt: "dev=%s skbaddr=%p len=%u", __get_str(name), REC->skbaddr, REC->len
```

顺便看一下这个 tracepoint 在内核中的[实现](https://github.com/torvalds/linux/blob/v5.8/net/core/dev.c#L4758)，

```c
// net/core/dev.c

static int netif_rx_internal(struct sk_buff *skb)
{
    net_timestamp_check(netdev_tstamp_prequeue, skb);

    trace_netif_rx(skb);
    ...
}
```

### 返回值

## 加载方式：`/sys/fs/debug/tracing/` 目录下的配置文件

例如，

```shell
# 启用 `net/net_dev_xmit` tracepoint as "myprobe2"
$ echo 'p:myprobe2 trace:net/net_dev_xmit' > /sys/kernel/debug/tracing/kprobe_events

# 获取 probe ID
$ cat /sys/kernel/debug/tracing/events/kprobes/myprobe2/id
2270
```

过程加载代码可参考 [load_and_attach()](https://github.com/torvalds/linux/blob/v5.8/samples/bpf/bpf_load.c#L76)。


# 3 `BPF_PROG_TYPE_PERF_EVENT`

## 使用场景

### 场景一：Instrument 软件/硬件 perf 事件

包括系统调用事件、定时器超时事件、硬件采样事件等。硬件事件包括 PMU（processor
monitoring unit）事件，它告诉我们已经执行了多少条指令之类的信息。

Perf 事件监控能具体到某个进程、组、处理器，也可以指定采样频率。

## 加载方式：`ioctl()`

1. perf_event_open() ，带一些采样配置信息；
2. <code>ioctl(fd, <mark>PERF_EVENT_IOC_SET_BPF</mark>)</code> 设置 BPF 程序，
3. 然后用 ioctl(fd, PERF_EVENT_IOC_ENABLE) 启用事件，

## 程序签名
### 传入参数：`struct bpf_perf_event_data *`

[定义](https://github.com/torvalds/linux/blob/v5.8/include/uapi/linux/bpf_perf_event.h#L13)：

```c
// include/uapi/linux/bpf_perf_event.h

struct bpf_perf_event_data {
    bpf_user_pt_regs_t regs;
    __u64 sample_period;
    __u64 addr;
};
```

## 触发执行：每个采样间隔执行一次

取决于 perf event firing 和选择的采样频率。


# ------------------------------------------------------------------------
# 轻量级隧道类型
# ------------------------------------------------------------------------

[Lightweight tunnels](https://lwn.net/Articles/650778/) 提供了**<mark>对内核路
由子系统的编程能力</mark>**，据此可以实现轻量级隧道。

举个例子，下面是没有 BPF 编程能力时，如何（为不同协议）添加路由：

```shell
# VXLAN:
$ ip route add 40.1.1.1/32 encap vxlan id 10 dst 50.1.1.2 dev vxlan0

$ MPLS:
$ ip route add 10.1.1.0/30 encap mpls 200 via inet 10.1.1.1 dev swp1
```

有了 BPF 可编程性之后，能为<mark>出向流量</mark>（入向是只读的）做封装。
详见 [BPF for lightweight tunnel encapsulation](https://lwn.net/Articles/705609/)。

与 `tc` 类似，`ip route` 支持直接将 BPF 程序 attach 到网络设备：

```shell
$ ip route add 192.168.253.2/32 \
     encap bpf out obj lwt_len_hist_kern.o section len_hist \
     dev veth0
```

# 1 `BPF_PROG_TYPE_LWT_IN`

## 使用场景

### 场景一：检查入向流量是否需要做解封装（decap）

Examine inbound packets for lightweight tunnel de-encapsulation.

## Hook 位置：`lwtunnel_input()`

该函数支持多种封装类型。
The BPF case runs bpf_input in net/core/lwt_bpf.c with redirection disallowed.

## 程序签名
### 传入参数：`struct sk_buff *`

### 返回值

## 加载方式：`ip route add`

```shell
$ ip route add <route+prefix> encap bpf in obj <bpf obj file.o> section <ELF section> dev <device>
```

## 延伸阅读

TODO

# 2 `BPF_PROG_TYPE_LWT_OUT`

## 使用场景

## 场景一：对出向流量做封装（encap）

## 加载方式：`ip route add`

```shell
$ ip route add <route+prefix> encap bpf out obj <bpf object file.o> section <ELF section> dev <device>
```

## 程序签名
### 传入参数：`struct __sk_buff *`

## 触发执行：`lwtunnel_output()`

# 3 `BPF_PROG_TYPE_LWT_XMIT`

## 使用场景

### 场景一：实现轻量级隧道发送端的 encap/redir 方法

## Hook 位置：`lwtunnel_xmit()` 

## 程序签名
### 传入参数：`struct __sk_buff *`

定义见 [前面](#struct-__sk_buff)。


## 加载方式：`ip route add`

```shell
$ ip route add <route+prefix> encap bpf xmit obj <bpf obj file.o> section <ELF section> dev <device>
```
