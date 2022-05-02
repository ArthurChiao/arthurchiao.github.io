---
layout    : post
title     : "BPF 进阶笔记（四）：调试 BPF 程序"
date      : 2022-05-02
lastupdate: 2022-05-02
categories: bpf
---

## 关于 “BPF 进阶笔记” 系列

平时学习和使用 BPF 时所整理。由于是笔记而非教程，因此内容不会追求连贯，有基础的
同学可作查漏补缺之用。

文中涉及的代码，如无特殊说明，均基于内核 **<mark>5.10</mark>**。

* [BPF 进阶笔记（一）：BPF 程序（BPF Prog）类型详解：使用场景、函数签名、执行位置及程序示例]({% link _posts/2021-07-04-bpf-advanced-notes-1-zh.md %})
* [BPF 进阶笔记（二）：BPF Map 类型详解：使用场景、程序示例]({% link _posts/2021-07-04-bpf-advanced-notes-2-zh.md %})
* [BPF 进阶笔记（三）：BPF Map 内核实现]({% link _posts/2021-07-04-bpf-advanced-notes-3-zh.md %})
* [BPF 进阶笔记（四）：调试 BPF 程序]({% link _posts/2021-07-04-bpf-advanced-notes-4-zh.md %})

----

* TOC
{:toc}

----

# 1 打印日志

## 1.1 日志路径及格式

本节将介绍的几种打印日志方式最终都会输出到 debugfs 路径 **<mark><code>/sys/kernel/debug/tracing/trace</code></mark>**：

```shell
$ sudo tail /sys/kernel/debug/tracing/trace
# 字段说明  <taskname>-<pid>    <cpuid>  <opts>  <timestamp>    <fake by bpf>  <log content>
            telnet-470          [001]     .N..   419421.045894: 0x00000001:    <formatted msg>
```

> 以上看到的是默认 trace 输出格式，
>
> 1. 可通过 `/sys/kernel/debug/tracing/trace_options` 定制化 trace 输出格式（打印哪些列）；
> 2. 另外还可参考 `/sys/kernel/debug/tracing/README`，其中有更详细的说明。

[<mark>字段说明</mark>](https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/bpf.h#L796)：

1. `telnet`：进程名；
1. `470`：进程 ID；
1. `001`：进程所在的 CPU；
1. `.N..`：每个字符表示一组配置选项，依次为，

    * 是否启用了中断（irqs）；
    * 调度选项，这里 N 表示设置了 `TIF_NEED_RESCHED` 和 `PREEMPT_NEED_RESCHED` 标志位；
    * 硬中断/软中断是否正在运行；
    * level of preempt_disabled

1. `419421.045894`：时间戳；
1. `0x00000001`：BPF 使用的一个 fake value，for instruction pointer register；
1. `<formatted msg>`：日志内容。

## 1.2 `bpf_printk()`：kernel `5.2+`

### 使用方式

这是内核 libbpf 库提供的一个宏：

```c
// https://github.com/torvalds/linux/blob/v5.10/tools/lib/bpf/bpf_helpers.h#L17

/* Helper macro to print out debug messages */
#define bpf_printk(fmt, ...)                \
({                            \
    char ____fmt[] = fmt;                \
    bpf_trace_printk(____fmt, sizeof(____fmt),    \
             ##__VA_ARGS__);        \
})
```

使用非常方便，和 C 的 `printf()` 差不多，例如，

```c
    bpf_printk("tcp_v4_connect latency_us: %u", latency_us);
```

### 使用限制

1. 需要内核 5.2+，否则编译能通过，但执行时会报错：

    ```
    map .rodata: map create: read- and write-only maps not supported (requires >= v5.2)
    ```

    这个错误提示非常奇怪（实际上目前来说，大部分 BPF 错误提示都不那么直接）。

    简单来说，BPF 的栈空间非常小，每次调用 `bpf_printk()` 都会动态声明一个 `char ____fmt[] = fmt;` 并放到栈上，导致性能很差。
    **<mark>5.2 引入了 BPF global (and static) 变量</mark>**，因此 clang 在编译时
    可以直接将这些变量放到 ELF 的只读区域（`.rodata`，read-only data），libbpf
    加载程序时将这些数据放到一个 `.rodata` BPF map 中，程序在用到这些变量时，背后执行一次 map lookup 即可。
    相比于每次都在栈上创建一个字符数组（字符串），这样更加快速和高效。

    更多内容，见 Andrii Nakryiko 的博客 [Improving `bpf_printk()`](https://nakryiko.com/posts/bpf-tips-printk/) 。

2. **<mark>最多只能带 3 个参数</mark>**，即 `bpf_printk(fmt, arg1, arg2, arg3)`。

    这是由 `bpf_trace_printk()` 的限制决定的，下一节有具体解释。

### 内核实现

前面已经看到 `bpf_printk()` 非常简单，只是单纯封装了一下 **<mark><code>bpf_trace_printk()</code></mark>**，
后者定义在 `include/uapi/linux/bpf.h`，具体实现见下文。

## 1.3 `bpf_trace_printk()`

对于 5.2 以下的内核，打印日志可以用 `bpf_trace_printk()`，它比 `bpf_printk()`
要麻烦一点：要提前声明格式字符串 `fmt`。

### 使用方式

```c
//  https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/bpf.h#L772

/**
 * long bpf_trace_printk(const char *fmt, u32 fmt_size, ...)
 */
```

1. 功能与 `printk()` 类似，按指定格式将日志打印到 `/sys/kernel/debug/tracing/trace` 中；
   但**<mark>支持的格式比 printk() 少</mark>**；

    * `5.10` 支持 `%d`, `%i`, `%u`, `%x`, `%ld`, `%li`, `%lu`, `%lx`, `%lld`,
      `%lli`, `%llu`, `%llx`, `%p`, `%s`。不支持指定字符串或数字长度等，否则会返回
      `-EINVAL`（同时什么都不打印）。
    * `5.13` 有进一步增强，见 [Detecting full-powered bpf_trace_printk()](https://nakryiko.com/posts/bpf-tips-printk/)。

2. 每次调用这个函数时，会往 trace 中追加一行；当 `/sys/kernel/debug/tracing/trace` is open，日志会被丢弃，
  可使用 `/sys/kernel/debug/tracing/trace_pipe` 来避免这种情况；
3. 这个函数**<mark>执行很慢，因此只应在调试时使用</mark>**；
4. `fmt` 格式串**<mark>是否有默认换行</mark>**：

    * `5.9` 之前没有，需要自己加 `\n`；
    * `5.9+` 会默认加一个换行符，patch 见 [bpf: Use dedicated bpf_trace_printk event instead of trace_printk()](https://github.com/torvalds/linux/commit/ac5a72ea5c898)。

函数的返回值是写到 buffer 的字节数，出错时返回负的 error code。

例子：

```c
    char fmt[] = "tcp_v4_connect latency_us: %u";
    bpf_trace_printk(fmt, sizeof(fmt), latency_us);
```

### 使用限制

1. **<mark>最多只能带 3 个参数</mark>**（这是因为 **<mark>eBPF helpers 最多只能带 5 个参数</mark>**，前面 `fmt` 和 `fmt_size` 已经占了两个了）；
2. 使用该函数的代码必须是 [GPL 兼容的](https://nakryiko.com/posts/bpf-tips-printk/)；
3. 前面已经提到，格式字符串支持的类型有限，但 `5.13` 有进一步改进，详见 [Detecting full-powered bpf_trace_printk()](https://nakryiko.com/posts/bpf-tips-printk/)。

### 内核实现

实现：

```c
// https://github.com/torvalds/linux/blob/v5.10/kernel/trace/bpf_trace.c#L428

BPF_CALL_5(bpf_trace_printk, char *, fmt, u32, fmt_size, u64, arg1, u64, arg2, u64, arg3)
{
    ...
}
```

其中 BPF_CALL_5 的定义：

```c
// https://github.com/torvalds/linux/blob/v5.10/include/linux/filter.h#L485

#define BPF_CALL_x(x, name, ...)                           \
    static __always_inline u64 ____##name(__BPF_MAP(x, __BPF_DECL_ARGS, __BPF_V, __VA_ARGS__));   \
    typedef u64                (*btf_##name)(__BPF_MAP(x, __BPF_DECL_ARGS, __BPF_V, __VA_ARGS__)); \
    u64                        name(__BPF_REG(x, __BPF_DECL_REGS, __BPF_N, __VA_ARGS__));           \
    u64                        name(__BPF_REG(x, __BPF_DECL_REGS, __BPF_N, __VA_ARGS__)) {       \
        return ((btf_##name)____##name)(__BPF_MAP(x,__BPF_CAST,__BPF_N,__VA_ARGS__));\
    }                                       \
    static __always_inline u64 ____##name(__BPF_MAP(x, __BPF_DECL_ARGS, __BPF_V, __VA_ARGS__))

#define BPF_CALL_5(name, ...)    BPF_CALL_x(5, name, __VA_ARGS__)
```

# 2 用 BPF 程序 trace 另一个 BPF 程序

## 2.1 使用场景

[BPF trampoline](https://lwn.net/Articles/804112/) 是
内核函数和 BPF 程序之间、BPF 程序和其他 BPF 程序之间的桥梁（更多介绍见附录）。
它的使用场景之一就是 **<mark>tracing 其他 BPF 程序</mark>**，例如 XDP 程序。
现在能向任何网络类型的 BPF 程序 attach 类似 fentry/fexit 的 BPF 程序，因
此能够看到 XDP、TC、LWT、cgroup 等任何类型 BPF 程序中包的进进出出，而不会影
响到这些程序的执行，大大降低了基于 BPF 的网络排障难度。

一些 patch，如果感兴趣:

* [bpf: Introduce BPF trampoline](https://lwn.net/ml/netdev/20191107054644.1285697-4-ast@kernel.org/)
* [bpf: Support attaching tracing BPF program to other BPF programs](https://lwn.net/ml/netdev/20191107054644.1285697-15-ast@kernel.org/)
* [libbpf: Add support for attaching BPF programs to other BPF programs](https://lwn.net/ml/netdev/20191107054644.1285697-17-ast@kernel.org/)
* [libbpf: Add support to attach to fentry/fexit tracing progs](https://github.com/torvalds/linux/commit/b8c54ea455dc2e0bda7ea9b0370279c224e21045)
* [trampoline impl: jit_com, trampoline, verifier, btf, good doc](https://lwn.net/ml/netdev/20191107054644.1285697-4-ast@kernel.org/)

BPF trampoline 其他使用场景：

1. **<mark>fentry/fexit</mark>** BPF 程序：**<mark>功能与 kprobe/kretprobe 类似，但性能更好</mark>**，几乎没有性能开销（practically zero overhead）；
2. **<mark>动态链接 BPF 程序</mark>**（dynamicly link BPF programs）。

    在 tracing、networking、cgroup BPF 程序中，中，是比 prog array 和 prog link list 更加通用的机制。
    在很多情况下，可直接作为基于 bpf_tail_call 程序链的一种替代方案。

这些特性都需要 root 权限。

## 2.2 依赖：kernel `5.5+`

# 3 设置断点，单步调试

## 3.1 `bpf_dbg`（仅限 cBPF）

见 [(译) Linux Socket Filtering (LSF, aka BPF)（Kernel，2021）]({% link _posts/2021-08-27-linux-socket-filtering-aka-bpf-zh.md %})。

----

# 附录

## BPF trampoline 简介

> "trampoline" 是意思是“蹦床”，这里是指程序执行时的特殊“适配+跳转”。
> BPF trampoline 最初用于 tracing 和 fentry/fexit，但后面扩展到了其他场景，例如
> 更高效地跟踪 XDP 程序，解决 XDP 程序开发和排障痛点。

这个 [patch](https://lwn.net/ml/netdev/20191107054644.1285697-4-ast@kernel.org/) 引入了
BPF trampoline 概念，**<mark>将原生调用约定</mark>**（native calling convention）
**<mark>转换成 BPF 调用约定</mark>**（BPF calling convention），
从而使内核代码能**<mark>几乎零开销地（practically zero overhead）调用 BPF 程序</mark>**。

BPF 架构和调用约定：

* 64 位 ISA（即使在 32 位架构上），
* **<mark>R1-R5 用于 BPF function 传参</mark>**，
* **<mark>主 BPF program 只接受一个参数</mark>** `ctx`，通过 R1 传递。

CPU 原生调用约定：

* x86-64 前 6 个参数通过寄存器传递，其他参数通过栈传递；
* x86-32 前 3 个参数通过寄存器传递；
* sparc64 前 6 个参数通过寄存器传递；

**<mark>trampoline 是架构相关的，因此其代码生成逻辑因架构而异</mark>**。

### BPF-to-kernel trampoline（BPF 调用内核函数）

这种 trampoline 早就有了：
宏 BPF_CALL_x （定义在 `include/linux/filter.h`）将 BPF 中的 trampolines
**<mark>静态地编译为内核辅助函数</mark>**（helpers）。
这个过程**<mark>最多能将 5 个参数转换成内核 C 指针或整数</mark>**。

* 在 64 位机器上：**<mark>不需做额外的处理</mark>**（因为 BPF 本来就是针对 64
  架构设计的，尤其关注与底层 ISA 的高效转换），因此 trampolines 都是 `nop` 指令；
* 在 32 位架构上：trampolines 是有实际作用的。

### Kernel-to-BPF trampoline（内核函数调用 BPF）

这些反向 trampolines 是由宏 `CAST_TO_U64` 和 `__bpf_trace_##call()` shim functions（定义在 include/trace/bpf_probe.h）完成的。
它们**<mark>将内核函数的参数们转成 u64 数组，这样 BPF 程序通过 R1=ctx 指针就能消费了</mark>**。

这个 [patch set](https://lwn.net/ml/netdev/20191107054644.1285697-4-ast@kernel.org/)
所做的工作与 `__bpf_trace_##call()` static trampolines 类似，但通过**<mark>动态方式，支持任何内核函数</mark>**：

* **<mark>内核有 ~22k global 内核函数</mark>**，能够在进入函数时（at function entry）**<mark>通过 nop 来 attach</mark>**。
* **<mark>函数参数和类型在 BTF 中有描述</mark>**。

    **<mark><code>btf_distill_func_proto()</code></mark>** 从
    BTF 中提取有用信息，转换成“函数模型”（function model），然后架构相关的
    trampoline generators 就能用这些信息来生成汇编代码，将内核函数参数转成 u64 数组。

    例如，内核函数 `eth_type_trans()` 有两个指针，它们会被转成 u64 然后存储到生
    成的 trampoline 的栈中；指向这个栈空间的指针会放到 R1 传给 BPF 程序。

    在 x86-64 架构上，这种 generated trampoline 将会占用 16 字节栈空间，并将 `%rdi` 和 `%rsi` 存储到栈上。
    校验器会保证 BPF 程序只能以 read-only 方式访问到两个 u64 参数。此外，校验器
    还能精确识别出指针的类型，不允许在 BPF 程序内将其转换（typecast）成其他类型。

### `fentry/fexit` 相比 `kprobe/kretprobe` 的优势

1. **<mark>性能更好</mark>**。

    数据中心中的一些真实 tracing 场景显示，
    某些关键的内核函数（例如 `tcp_retransmit_skb`）有 2 个甚至更多永远活跃的 kprobes，
    其他一些函数同时有 kprobe and kretprobe。

    所以，**<mark>最大化</mark>**内核代码和 BPF 程序的**<mark>执行速度</mark>**就非常有必要。因此
    在每个新程序 attach 时或者 detach 时，BPF trampoline 都会重新生成，以保证最高性能。
    （另外在设计上，从 trampoline detach BPF 程序不会失败。）

2. **<mark>能拿到的信息更多</mark>**。

    * fentry BPF 程序能拿到内核**<mark>函数参数</mark>**， 而
    * fexit BPF 程序除了能拿到函数参数，还能拿到**<mark>函数返回值</mark>**；而  kretprobe 只能拿到返回结果。

    kprobe BPF 程序通常将函数参数记录到一个 map 中，然后 kretprobe 从 map 中
    拿出参数，并和返回值一起做一些分析处理。fexit BPF 程序加速了这个典型的使用场景。

3. **<mark>可用性更好</mark>**。

    和普通 C 程序一样，**<mark>直接对指针参数解引用</mark>**，
    不再需要各种繁琐的 probe read helpers 了。

限制：fentry/fexit BPF 程序需要更高的内核版本（5.5+）才能支持。
