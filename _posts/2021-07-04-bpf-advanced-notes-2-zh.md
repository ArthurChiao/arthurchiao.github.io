---
layout    : post
title     : "BPF 进阶笔记（二）：BPF Map 类型详解：使用场景、程序示例"
date      : 2021-07-13
lastupdate: 2021-07-13
categories: bpf xdp
---

## 关于本文

内核目前支持 [30 来种](https://github.com/torvalds/linux/blob/v5.8/include/uapi/linux/bpf.h#L122)
BPF map 类型。对于主要的类型，本文将介绍其：

1. **使用场景**：适合用来做什么？
1. **程序示例**：一些实际例子。

本文参考：

1. [notes-on-bpf-3](https://blogs.oracle.com/linux/notes-on-bpf-3)，内容较老，基于内核 `4.14`

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

## BPF map 类型：完整列表

所有 map 类型的[定义](https://github.com/torvalds/linux/blob/v5.8/include/uapi/linux/bpf.h#L122)：

```c
// include/uapi/linux/bpf.h

enum bpf_map_type {
    BPF_MAP_TYPE_UNSPEC,
    BPF_MAP_TYPE_HASH,               // 哈希表
    BPF_MAP_TYPE_ARRAY,              // 数组
    BPF_MAP_TYPE_PROG_ARRAY,         // 存放 BPF 程序的数组
    BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    BPF_MAP_TYPE_PERCPU_HASH,
    BPF_MAP_TYPE_PERCPU_ARRAY,
    BPF_MAP_TYPE_STACK_TRACE,
    BPF_MAP_TYPE_CGROUP_ARRAY,
    BPF_MAP_TYPE_LRU_HASH,
    BPF_MAP_TYPE_LRU_PERCPU_HASH,
    BPF_MAP_TYPE_LPM_TRIE,
    BPF_MAP_TYPE_ARRAY_OF_MAPS,
    BPF_MAP_TYPE_HASH_OF_MAPS,
    BPF_MAP_TYPE_DEVMAP,
    BPF_MAP_TYPE_SOCKMAP,
    BPF_MAP_TYPE_CPUMAP,
    BPF_MAP_TYPE_XSKMAP,
    BPF_MAP_TYPE_SOCKHASH,
    BPF_MAP_TYPE_CGROUP_STORAGE,
    BPF_MAP_TYPE_REUSEPORT_SOCKARRAY,
    BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE,
    BPF_MAP_TYPE_QUEUE,
    BPF_MAP_TYPE_STACK,
    BPF_MAP_TYPE_SK_STORAGE,
    BPF_MAP_TYPE_DEVMAP_HASH,
    BPF_MAP_TYPE_STRUCT_OPS,
    BPF_MAP_TYPE_RINGBUF,
};
```

# ------------------------------------------------------------------------
# Hash Maps
# ------------------------------------------------------------------------

Hash map 的实现见
[kernel/bpf/hashtab.c](https://github.com/torvalds/linux/blob/v5.8/kernel/bpf/hashtab.c)。
五种类型共用一套代码：

* `BPF_MAP_TYPE_HASH`
* `BPF_MAP_TYPE_PERCPU_HASH`
* `BPF_MAP_TYPE_LRU_HASH`
* `BPF_MAP_TYPE_LRU_PERCPU_HASH`
* `BPF_MAP_TYPE_HASH_OF_MAPS`

Hash map 的特点：

* **<mark>key 的长度没有限制</mark>**，但显然应该大于 0。
* 给定 key 查找 value 时，内部通过哈希实现，而非数组索引。
* **<mark>key/value 是可删除的</mark>**；作为对比，**<mark>Array 类型</mark>**的
  map 中，key/value 是**<mark>不可删除的</mark>**（但用空值覆盖掉 value
  ，可实现删除效果）。

    原因其实也很简单：哈希表是链表，可以删除链表中的元素；array 是内存空间连续的
    数组，即使某个 index 处的 value 不用了，这段内存区域还是得留着，不可能将其释放掉。

不带与带 `PERCPU` 的 map 的区别：

* 前者是 global 的，只有一个实例；后者是 cpu-local 的，每个 CPU 上都有一个 map 实例；
* 多核并发访问时，global map 要加锁；per-cpu map 无需加锁，每个核上的程序访问
  local-cpu 上的 map；最后将所有 CPU 上的 map 汇总。

# 1 `BPF_MAP_TYPE_HASH`

最简单的哈希 map。

初始化时需要指定**<mark>支持的最大条目数</mark>**（max_entries）。
满了之后继续插入数据时，会报 `E2BIG` 错误。

## 使用场景

### 场景一：将内核态得到的数据，传递给用户态程序

这是非常典型的**<mark>在内核态和用户态传递数据</mark>**场景。

例如，BPF 程序过滤网络设备设备上的包，统计流量信息，并将其写到 map。
用户态程序从 map 读取统计，做后续处理。

### 场景二：存放全局配置信息，供 BPF 程序使用

例如，对于防火墙功能的 BPF 程序，将过滤规则放到 map 里。用户态控制程序通过
bpftool 之类的工具更新 map 里的配置信息，BPF 程序动态加载。

## 程序示例

<a name="sockex2"></a>
### 1. 将内核态数据传递到用户态：`samples/bpf/sockex2`

这个例子用 BPF 程序 **<mark>过滤网络设备设备上的包</mark>**，统计包数和字节数，
并以目的 IP 地址为 key 将统计信息写到 map：

```c
// samples/bpf/sockex2_kern.c

struct {
    __uint(type, BPF_MAP_TYPE_HASH);  // BPF map 类型
    __type(key, __be32);              // 目的 IP 地址
    __type(value, struct pair);       // 包数和字节数
    __uint(max_entries, 1024);        // 最大 entry 数量
} hash_map SEC(".maps");

SEC("socket2")
int bpf_prog2(struct __sk_buff *skb)
{
    flow_dissector(skb, &flow);

    key = flow.dst; // 目的 IP 地址
    value = bpf_map_lookup_elem(&hash_map, &key);
    if (value) {    // 如果已经存在，则更新相应计数
        __sync_fetch_and_add(&value->packets, 1);
        __sync_fetch_and_add(&value->bytes, skb->len);
    } else {        // 否则，新建一个 entry
        struct pair val = {1, skb->len};
        bpf_map_update_elem(&hash_map, &key, &val, BPF_ANY);
    }
    return 0;
}
```

# 2 `BPF_MAP_TYPE_PERCPU_HASH`

## 使用场景

基本同上。

## 程序示例

### 1. `samples/bpf/map_perf_test_kern.c`

# 3 `BPF_MAP_TYPE_LRU_HASH`

普通 hash map 的问题是有大小限制，超过最大数量后无法再插入了。LRU map 可以避
免这个问题，如果 map 满了，再插入时它会自动将**<mark>最久未被使用（least
recently used）</mark>**的 entry 从 map 中移除。

## 使用场景

### 场景一：连接跟踪（conntrack）表、NAT 表等固定大小哈希表

满了之后最老的 entry 会被踢出去。

## 程序示例

### 1. `samples/bpf/map_perf_test_kern.c`

### 2. [Cilium Conntrack & NAT 表]()

**<mark>TODO: update this.</mark>**

# 4 `BPF_MAP_TYPE_LRU_PERCPU_HASH`

基本同上。

# 5 `BPF_MAP_TYPE_HASH_OF_MAPS`

map-in-map：**<mark>第一个 map 内的元素是指向另一个 map 的指针</mark>**。
与后面将介绍的 `BPF_MAP_TYPE_ARRAY_OF_MAPS` 类似，但外层 map 使用的是哈希而不是数组。

相关 [commit message](https://www.mail-archive.com/netdev@vger.kernel.org/msg159383.html)。

## 使用场景

### 场景一：map-in-map

## 程序示例

### 1. `samples/bpf/test_map_in_map_kern.c`

测试了如下两级查找场景:

1. Array of array
1. Hash of array
1. Hash of hash

# ------------------------------------------------------------------------
# Array Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_ARRAY`

最大的特点：**<mark>key 就是数组中的索引（index）</mark>**（因此 key 一定
是整形），因此无需对 key 进行哈希。

## 使用场景：key 是整形

## 程序示例

<a name="sockex1"></a>
### 1. 根据协议类型（proto as key）统计流量：`samples/bpf/sockex1`

```c
// samples/bpf/sockex1_kern.c

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, u32);                  // L4 协议类型（长度是 uint8），例如 IPPROTO_TCP，范围是 0~255
    __type(value, long);               // 累计包长（skb->len）
    __uint(max_entries, 256);
} my_map SEC(".maps");

SEC("socket1")
int bpf_prog1(struct __sk_buff *skb)
{
    int index = load_byte(skb, ETH_HLEN + offsetof(struct iphdr, protocol)); // L4 协议类型

    if (skb->pkt_type != PACKET_OUTGOING)
        return 0;

    // 注意：在用户态程序和这段 BPF 程序里都没有往 my_map 里插入数据；
    //   * 如果这是 hash map 类型，那下面的 lookup 一定失败，因为我们没插入过任何数据；
    //   * 但这里是 array 类型，而且 index 表示的 L4 协议类型，在 IP 头里占一个字节，因此范围在 255 以内；
    //     又 map 的长度声明为 256，所以这里的 lookup 一定能定位到 array 的某个位置，即查找一定成功。
    value = bpf_map_lookup_elem(&my_map, &index);
    if (value)
        __sync_fetch_and_add(value, skb->len);

    return 0;
}
```

# 2 `BPF_MAP_TYPE_PERCPU_ARRAY`

基本同上。

# 3 `BPF_MAP_TYPE_PROG_ARRAY`

程序数组，尾调用 `bpf_tail_call()` 时会用到。

* key：任意整形（因为要作为 array index），具体表示什么由使用者设计（例如表示协议类型 proto）。
* value：**<mark>BPF 程序的文件描述符（fd）</mark>**。

## 使用场景：尾调用（tail call）

## 程序示例

### 1. 根据协议类型尾调用到下一层 parser：`samples/bpf/sockex3`

# 4 `BPF_MAP_TYPE_PERF_EVENT_ARRAY`

## 使用场景：保存 tracing 结果

## 程序示例

### 1. 保存 perf event：`samples/bpf/trace_output_kern.c`

```c
// samples/bpf/trace_output_kern.c

struct bpf_map_def SEC("maps") my_map = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(int),
    .value_size = sizeof(u32),
    .max_entries = 2,
};

SEC("kprobe/sys_write")
int bpf_prog1(struct pt_regs *ctx)
{
    struct S {
        u64 pid;
        u64 cookie;
    } data;

    data.pid = bpf_get_current_pid_tgid();
    data.cookie = 0x12345678;

    bpf_perf_event_output(ctx, &my_map, 0, &data, sizeof(data));

    return 0;
}
```

# 5 `BPF_MAP_TYPE_ARRAY_OF_MAPS`

## 使用场景：map-in-map

map-in-map，values 是指向内层 map 的 fd。只支持两层 map。
two levels of map，也就是一层 map 嵌套另一层 map。

`BPF_MAP_TYPE_PROG_ARRAY` 类型的 BPF 程序**<mark>不支持 map-in-map 功能</mark>**
，因为这会使 tail call 的 verification 更加困难。
详见 [patch](https://www.mail-archive.com/netdev@vger.kernel.org/msg159387.html)。

## 程序示例

### 1. `samples/bpf/map_perf_test_kern.c`

### 2. `samples/bpf/test_map_in_map_kern.c`


<a name="bpf_map_type_cgroup_array"></a>
# 6 `BPF_MAP_TYPE_CGROUP_ARRAY`

在用户空间存放 cgroup fds，用来 **<mark>检查给定的 skb 是否与 cgroup_array[index] 指向的 cgroup 关联</mark>**。

## 使用场景

### 场景一：cgroup 级别的包过滤（拒绝/放行）

### 场景二：cgroup 级别的进程过滤（权限控制等）

## 程序示例

### 1. Pin & update pinned cgroup array：[`samples/bpf/test_cgrp2_array_pin.c`](https://github.com/torvalds/linux/blob/v5.10/samples/bpf/test_cgrp2_array_pin.c)

程序功能：

1. **<mark>将 cgroupv2 array pin 到 BPFFS</mark>**
2. 更新 pinned cgroupv2 array

```c
// samples/bpf/test_cgrp2_array_pin.c

    if (create_array) {
        array_fd = bpf_create_map(BPF_MAP_TYPE_CGROUP_ARRAY, sizeof(uint32_t), sizeof(uint32_t), 1, 0);
    } else {
        array_fd = bpf_obj_get(pinned_file);
    }

    bpf_map_update_elem(array_fd, &array_key, &cg2_fd, 0);

    if (create_array) {
        ret = bpf_obj_pin(array_fd, pinned_file);
    }
```

### 2. CGroup 级别的包过滤：`samples/bpf/test_cgrp2_tc_kern.c`

核心是**<mark>调用 <code>bpf_skb_under_cgroup()</code> 判断 skb 是否在给定 cgroup 中</mark>**：

```c
// samples/bpf/test_cgrp2_tc_kern.c

struct bpf_elf_map SEC("maps") test_cgrp2_array_pin = {
    .type        = BPF_MAP_TYPE_CGROUP_ARRAY,
    .size_key    = sizeof(uint32_t),
    .size_value  = sizeof(uint32_t),
    .pinning     = PIN_GLOBAL_NS,
    .max_elem    = 1,
};

SEC("filter")
int handle_egress(struct __sk_buff *skb)
{
    ...
    if (bpf_skb_under_cgroup(skb, &test_cgrp2_array_pin, 0) != 1) {
        bpf_trace_printk(pass_msg, sizeof(pass_msg));
        return TC_ACT_OK;
    }
    ...
}
```

### 3. 判断进程是否在给定 cgroup 中：`samples/bpf/test_current_task_under_cgroup_kern.c`

调用 `bpf_current_task_under_cgroup()` **<mark>判断当前进程是否在给定 cgroup 中</mark>**：

```c

struct bpf_map_def SEC("maps") cgroup_map = {
    .type            = BPF_MAP_TYPE_CGROUP_ARRAY,
    .key_size        = sizeof(u32),
    .value_size        = sizeof(u32),
    .max_entries    = 1,
};

/* Writes the last PID that called sync to a map at index 0 */
SEC("kprobe/sys_sync")
int bpf_prog1(struct pt_regs *ctx)
{
    ...
    if (!bpf_current_task_under_cgroup(&cgroup_map, 0))
        return 0;
    ...
}
```

# ------------------------------------------------------------------------
# CGroup Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_CGROUP_ARRAY`

[上面](#bpf_map_type_cgroup_array) 已经有详细介绍。

# 2 `BPF_MAP_TYPE_CGROUP_STORAGE`

Attach 到一个 cgroup 的所有 BPF 程序，会共用一组 cgroup storage，包括：

```c
    for (stype = 0; stype < MAX_BPF_CGROUP_STORAGE_TYPE; stype++)
        storages[stype] = bpf_cgroup_storage_alloc(prog, stype);
```

这里的 types 目前只有两种：

1. shared
2. per-cpu

## 使用场景

### 场景一：cgroup 内所有 BPF 程序的共享存储

## 程序示例

### 1. `samples/bpf/hbm_kern.h`：host bandwidth manager

```c

struct {
    __uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
    __type(key, struct bpf_cgroup_storage_key);
    __type(value, struct hbm_vqueue);
} queue_state SEC(".maps");
```

# 3 `BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE`

同上。

# ------------------------------------------------------------------------
# Tracing Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_STACK_TRACE`

内核程序能通过 bpf_get_stackid() helper 存储 stack 信息。
将 stack 信息关联到一个 id，而这个 id 是**<mark>对当前栈的
指令指针地址（instruction pointer address）进行 32-bit hash</mark>** 得到的。

## 使用场景
### 场景一：存储 profiling 信息

在内核中获取 stack id，用它作为 key 更新另一个 map。
例如通过对指定的 stack traces 进行 profiling，统计它们的出现次数，或者将 stack
trace 信息与当前 pid 关联起来。

## 程序示例

### 1. 打印调用栈：`samples/bpf/offwaketime_kern.c`

# 2 `BPF_MAP_TYPE_STACK`

## 使用场景
### 场景一：


# 3 `BPF_MAP_TYPE_RINGBUF`

## 使用场景

### 场景一：


# 4 `BPF_MAP_TYPE_PERF_EVENT_ARRAY`

## 使用场景

### 场景一：Perf events

BPF 程序将数据存储在 `mmap()` 共享内存中，用户空间程序可以访问。

场景：

* 非固定大小数据（不适合 map）
* 无需与其他 BPF 程序共享数据

## 程序示例

### 1. `samples/bpf/trace_output`：trace `write()` 系统调用



# ------------------------------------------------------------------------
# Socket Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_SOCKMAP`

主要用于 socket redirection：将 sockets 信息插入到 map，后面执行到
bpf_sockmap_redirect() 时，用 map 里的信息触发重定向。

## 使用场景

### 场景一：socket redirection（重定向）

## 程序示例

TODO.


# 2 `BPF_MAP_TYPE_REUSEPORT_SOCKARRAY`

配合 `BPF_PROG_TYPE_SK_REUSEPORT` 类型的 BPF 程序使用，加速 socket 查找。

## 使用场景

### 场景一：配合 `_SK_REUSEPORT` 类型 BPF 程序，加速 socket 查找

# 3 `BPF_MAP_TYPE_SK_STORAGE`

## 使用场景

### 场景一：per-socket 存储空间

## 程序示例

### 1. 在内核定期 dump socket 详情：`samples/bpf/tcp_dumpstats_kern.c`

```c

struct {
    __u32 type;
    __u32 map_flags;
    int *key;
    __u64 *value;
} bpf_next_dump SEC(".maps") = {
    .type = BPF_MAP_TYPE_SK_STORAGE,
    .map_flags = BPF_F_NO_PREALLOC,
};

SEC("sockops")
int _sockops(struct bpf_sock_ops *ctx)
{
    struct bpf_tcp_sock *tcp_sk;
    struct bpf_sock *sk;
    __u64 *next_dump;

    switch (ctx->op) {
    case BPF_SOCK_OPS_TCP_CONNECT_CB:
        bpf_sock_ops_cb_flags_set(ctx, BPF_SOCK_OPS_RTT_CB_FLAG);
        return 1;
    case BPF_SOCK_OPS_RTT_CB:
        break;
    default:
        return 1;
    }

    sk = ctx->sk;
    next_dump = bpf_sk_storage_get(&bpf_next_dump, sk, 0, BPF_SK_STORAGE_GET_F_CREATE);
    now = bpf_ktime_get_ns();
    if (now < *next_dump)
        return 1;

    tcp_sk = bpf_tcp_sock(sk);
    *next_dump = now + INTERVAL;

    bpf_printk("dsack_dups=%u delivered=%u\n", tcp_sk->dsack_dups, tcp_sk->delivered);
    bpf_printk("delivered_ce=%u icsk_retransmits=%u\n", tcp_sk->delivered_ce, tcp_sk->icsk_retransmits);

    return 1;
}
```

# ------------------------------------------------------------------------
# XDP Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_SOCKHASH`

## 使用场景

### 场景一：XDP 重定向

## 程序示例

### 1. [(译) 利用 ebpf sockmap/redirection 提升 socket 性能（2020）]({% link _posts/2021-01-28-socket-acceleration-with-ebpf-zh.md %})

# 2 `BPF_MAP_TYPE_DEVMAP`

**<mark>功能与 sockmap 类似，但用于 XDP 场景</mark>**，在 `bpf_redirect()` 时触发。

## 使用场景

### 场景一：存放 XDP 配置信息

**<mark>对于 TC BPF 程序，配置信息放到普通的 hash 或 array map 里就行了</mark>**。但对于
XDP 程序来说，由于它们开始执行的位置非常靠前，此时大部分网络基础设施它们都是用
不了的。因此引入了一些专门针对 XDP 的基础设施，例如这里的 DEVMAP（对应 TC 场景
下的普通 BPF MAP）。

### 场景二：XDP redirection

## 程序示例

### 1. 存储 XDP 配置信息：`samples/bpf/xdp_fwd_kern.c`

这个例子里，将允许通过哪些网卡发送数据的配置信息放到了一个 DEVMAP 里，

```c
struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(key_size, sizeof(int));      // key 表示 ifindex，即网卡 ID
    __uint(value_size, sizeof(int));    // val 表示是否允许从这个网卡发送（TX）数据
    __uint(max_entries, 64);
} xdp_tx_ports SEC(".maps");
```

主逻辑里查询这个 map，判断是否能通过这个网卡发送数据：

```c
    if (rc == BPF_FIB_LKUP_RET_SUCCESS) {
        // Verify egress index has been configured as TX-port.
        if (!bpf_map_lookup_elem(&xdp_tx_ports, &fib_params.ifindex))
            return XDP_PASS;

        memcpy(eth->h_dest, fib_params.dmac, ETH_ALEN);
        memcpy(eth->h_source, fib_params.smac, ETH_ALEN);
        return bpf_redirect_map(&xdp_tx_ports, fib_params.ifindex, 0);
    }
```

### 2. XDP 重定向：`samples/bpf/xdp_redirect_map_kern.c`

将包从指定网卡重定向出去：

```c
struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(key_size, sizeof(int));    // virtual port index
    __uint(value_size, sizeof(int));  // physical port index
    __uint(max_entries, 100);
} tx_port SEC(".maps");

SEC("xdp_redirect_map")
int xdp_redirect_map_prog(struct xdp_md *ctx)
{
    swap_src_dst_mac(data);

    /* send packet out physical port */
    return bpf_redirect_map(&tx_port, vport, 0);
}
```

### 3. 极简 XDP 路由器：`samples/bpf/xdp_router_ipv4_kern.c`

用到了多种类型的 MAP，实现 IPv4 路由功能：

```c
/* Map for trie implementation*/
struct {
    __uint(type, BPF_MAP_TYPE_LPM_TRIE);
    __uint(key_size, 8);
    __uint(value_size, sizeof(struct trie_value));
    __uint(max_entries, 50);
    __uint(map_flags, BPF_F_NO_PREALLOC);
} lpm_map SEC(".maps");

/* Map for ARP table*/
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __be32);
    __type(value, __be64);
    __uint(max_entries, 50);
} arp_table SEC(".maps");

/* Map to keep the exact match entries in the route table*/
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __be32);
    __type(value, struct direct_map);
    __uint(max_entries, 50);
} exact_match SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
    __uint(max_entries, 100);
} tx_port SEC(".maps");
```

# 3 `BPF_MAP_TYPE_DEVMAP_HASH`

同上。

# 4 `BPF_MAP_TYPE_XSKMAP`

都是 XDP map，都可用于 XDP socket 重定向，**<mark>与 DEVMAP 有什么区别？</mark>**

## 使用场景：XDP

## 程序示例

### 1. XDP socket 重定向：`samples/bpf/xdpsock_kern.c`

```c
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, MAX_SOCKS);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
} xsks_map SEC(".maps");

static unsigned int rr;

SEC("xdp_sock") int xdp_sock_prog(struct xdp_md *ctx)
{
    rr = (rr + 1) & (MAX_SOCKS - 1);
    return bpf_redirect_map(&xsks_map, rr, XDP_DROP);
}
```



# ------------------------------------------------------------------------
# 其他 Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_CPUMAP`

## 使用场景

### 场景一：

# 2 `BPF_MAP_TYPE_QUEUE`

## 使用场景

### 场景一：


# 3 `BPF_MAP_TYPE_STRUCT_OPS`

## 使用场景

### 场景一：

# 4 `BPF_MAP_TYPE_LPM_TRIE`

支持高效的 longest-prefix matching。

## 使用场景

### 场景一：存储 IP 路由等

## 程序示例

### 1. `samples/bpf/map_perf_test_kern.c`

### 2. `samples/bpf/xdp_router_ipv4_kern.c`

### 3. [Cilium (TODO)]()




# ------------------------------------------------------------------------
# 其他相关内容
# ------------------------------------------------------------------------

## 用户空间 map 操作：`tools/lib/bpf/bpf.c` 提供的封装

BPF map 是内核对象，为方便从用户空间对 map 进行操作，
[tools/lib/bpf/bpf.c](https://github.com/torvalds/linux/blob/v5.8/tools/lib/bpf/bpf.c)
封装了一些通用 API。例如，

```c
# 带 _node 字样的函数或类型都表示感知 NUMA 结构
int bpf_create_map_node(enum bpf_map_type map_type, const char *name,
       int key_size, int value_size, int max_entries, __u32 map_flags, int node);

int bpf_create_map_in_map_node(enum bpf_map_type map_type, const char *name,
       int key_size, int inner_map_fd, int max_entries, __u32 map_flags, int node);
```

二者最后都会执行到 bpf 系统调用：

```c
    sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
```

## 声明/创建 BPF map 的方式

### 常规方式

下面是来自 samples/bpf 中的一个例子：

```c
// samples/bpf/lathist_kern.c

struct bpf_map_def SEC("maps") my_map = {
    .type        = BPF_MAP_TYPE_ARRAY,
    .key_size    = sizeof(int),
    .value_size  = sizeof(u64),
    .max_entries = MAX_CPU,
};
```

以上声明了一个 BPF map，

* map 类型是 `BPF_MAP_TYPE_ARRAY`，
* 指定 key 和 value 的长度；

    BPF map 是任意类型的 key/value 存储，key 和 value 类型都是在 BPF 程序中定义的。
    因此内核关心的并不是 key/value 类型，而且它们的大小（长度）。BPF map 操作也
    用的的是 key/value 的 `void *` 类型地址。BPF 程序需要解析 map 数据时，先拿到
    key/value 地址，然后自己做相应的强制类型转换。

* 数组最大长度（map size）

建议声明 map 时优先使用这种封装好的方式，不要重复造轮子。

### tc/iproute2 方式

如果使用的是 tc/iproute2，那声明和创建 map 的过程会稍有不同，见 iproute2
[源码](https://git.kernel.org/pub/scm/network/iproute2/iproute2.git/tree/include/bpf_elf.h?h=v4.14.1)。


```c
// samples/bpf/tc_l2_redirect_kern.c

// key 结构体需要 64bit 对其，否则内核校验器会拒绝加载程序
struct bpf_elf_map {
    __u32 type;
    __u32 size_key;
    __u32 size_value;
    __u32 max_elem;
    __u32 flags;
    __u32 id;
    __u32 pinning;
};

struct bpf_elf_map SEC("maps") tun_iface = {
    .type = BPF_MAP_TYPE_ARRAY,
    .size_key = sizeof(int),
    .size_value = sizeof(int),
    .pinning = PIN_GLOBAL_NS,
    .max_elem = 1,
};
```

## Map pinning

```c
/* Object pinning settings */

#define PIN_NONE        0
#define PIN_OBJECT_NS   1
#define PIN_GLOBAL_NS   2 // 绑定到 `/sys/fs/bpf/tc/globals/` 下面
```

这个选项决定了**<mark>以何种文件系统方式将 map 暴露出来</mark>**。

例如，如果使用的是 libbpf 库，

* 可以通过 `bpf_obj_pin(fd, path)` **<mark>将 map fd 绑定到文件系统中的指定文件</mark>**；
* 接下来，其他程序**<mark>获取这个 fd</mark>**，只需执行 `bpf_obj_get(pinned_file)`。
