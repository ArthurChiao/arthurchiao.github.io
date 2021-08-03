---
layout    : post
title     : "[译] [论文] 迈向完全可编程 tc 分类器（cls_bpf）（NetdevConf，2016）"
date      : 2021-03-05
lastupdate: 2021-03-05
categories: bpf tc
---

### 译者序

本文翻译自 2016 年 Daniel Borkman 在 `NetdevConf` 大会上的一篇文章：
[On getting tc classifier fully programmable with cls_bpf](https://www.netdevconf.org/1.1/proceedings/papers/On-getting-tc-classifier-fully-programmable-with-cls-bpf.pdf)。

Daniel 是 eBPF 的核心开发之一，
文章从技术层面介绍了 eBPF 的发展历史、核心设计，以及更重要的 —— 在 eBPF 基础之上
，`cls_bpf` 如何使 tc 分类器变得完全可编程。

由于 eBPF 发展很快，文中有些描述今天已经过时（例如单个 eBPF 程序允许的最大指令数量），
因此翻译时以译注的形式做了适当更新。插入的一些内核代码基于 4.19。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 摘要

Berkely Packet Filter（BPF）是 1993 年设计的一种**指令集架构**（instruction set architecture）[18] [1] ——
作为一种<mark>通用数据包过滤方案</mark>（generic packet filtering solution），
提供给 `libpcap`/`tcpdump` 等上层应用使用。
BPF 很早就已经出现在 **Linux 内核**中，并且使用场景也**不再仅限于网络方面**，
例如有**对系统调用进行过滤**（system call filtering）的 `seccomp` BPF [15]。

近几年，Linux 社区将这种经典 BPF（classic BPF, cBPF）做了升级，形成一个新的指令集架构，
称为 “extended BPF” (eBPF) [21] [23] [22] [24]。与 cBPF 相比，eBPF 带了
**更大的灵活性和可编程性**，也带来了一些**新的使用场景**，例如跟踪（tracing）[27]、
KCM（Kernel Connection Multiplexor）[17] 等。

> Kernel Connection Multiplexor (KCM) is a facility that provides a
> message based interface over TCP for generic application protocols.
> With KCM an application can efficiently send and receive application
> protocol messages over TCP using datagram sockets.
>
> For more information see the included Documentation/networking/kcm.txt

除了<mark>替换掉解释器</mark>之外，<mark>JIT 编译器也进行了升级</mark>，使
eBPF [25] 程序能达到**平台原生的执行性能**。

内核流量控制层的 **<mark>cls_bpf 分类器添加了对 eBPF 的支持之后</mark>** [8]，
<mark>tc 对 Linux 数据平面进行编程的能力更加强大</mark>，并且该过程与
内核网络栈、相关工具及底层编程范式的联系也更紧密。

本文将介绍 **eBPF、eBPF 与 tc 的交互**及内核网络社区在 eBPF 领域的一些最新工作。

本文内容不求大而全，而是希望作为一份入门材料，供那些对 **eBPF 架构及其与 tc 关系**感兴趣的人参考。

**<mark>关键字</mark>**：eBPF, cls_bpf, tc, programmable datapath, Linux kernel

# 1 引言

经典 BPF（cBPF）多年前就已经在 Linux 内核中实现了，<mark>主要用户是 PF_PACKET sockets</mark>。
在该场景中，cBPF 作为一种**通用、快速且安全**的方案，在 PF_PACKET
<mark>收包路径的早期位置（early point）解析数据包</mark>（packet parsing）。
其中，与安全执行（safe execution）相关的一个目标是：**从用户程序向内核注入
非受信代码，但不能因此破坏内核的稳定性**。

## 1.1 cBPF 架构

cBPF 是 **<mark>32bit 架构</mark>** [18]，<mark>主要针对包解析（packet parsing）场景设计</mark>：

* 两个主寄存器 A 和 X
    * A 是主寄存器（main register），也称作累加器（accumulator）。这里执行大部分操作，例如 alu、load、store、comparison-for-jump 等。
    * X 主要用作临时寄存器，也用于加载包内容（relative loads of packet contents）。
* 一个 16word scratch space（存放临时数据），通常称为 M
* 一个隐藏的程序计数器（PC）

**<mark>使用 cBPF 时，包的内容只能读取，不能修改</mark>。**

cBPF 有 8 种的指令类型：

1. ld
1. ldx
1. st 
1. stx 
1. alu 
1. jmp
1. ret
1. 其他一些指令：用于传递 A 和 X 中的内容。

几点解释：

* 前四个是加载相关的指令，**load 和 store 类型分别会用到寄存器 A 和 X**。
* `jump` 只支持前向跳转（forward jump）。
* `ret` 结束 cBPF 程序执行，从程序返回。

<mark>每个 cBPF 程序最多只能包含 4096 条指令</mark>（max instructions/programm），
代码在加载到内核执行之前，校验器会对其进行静态验证（statically verify）。

具体到 `bpf_asm` 工具 [5]，它包含 33 条指令、11 种寻址模式和 16 个 Linux 相关的 cBPF 扩展（extensions）。

## 1.2 cBPF 使用场景

cBPF 程序的语义是由使用它的子系统定义的。由于其通用、最小化和快速执行的特点，如
今 cBPF 已经在 PF_PACKET socket **<mark>之外的一些场景找到了用武之地</mark>**：

* `seccomp` BPF [15] 于 2012 年添加到内核，目的是提供一种**安全和快速的系统调用过滤**方式。
* 网络领域，cBPF 已经能

    * 用作大部分协议（TCP、UDP、netlink 等）的 socket filter；
    * 用作 PF_PACKET socket 的 fanout demuxing facility [14] [13]
    * 用于 socket demuxing with SO REUSEPORT [16]
    * 用于 load balancing in team driver [19]
    * 用于本文将介绍的 tc 子系统中，作为 classifier [6] and action [20]

* 其他一些场景

eBPF 作为对 cBPF 的扩展，**第一个 commit 于 2014 年合并到内核**。从那之后，
BPF 的可编程特性已经发生了巨大变化。

# 2 eBPF 架构

与 cBPF 类似，eBPF 也可以被视为一个最小“虚拟”机（minimalistic ”virtual” machine construct）[21]。
eBPF 抽象的机器只有少量寄存器、很小的栈空间、一个隐藏的程序计数器以及一个所谓的辅助函数
（helper function）的概念。

在内核其他基础设施的配合下，**eBPF 能做一些有副作用（side effects）的事情**。

> 这里的副作用是指：eBPF 程序能够对拦截到的东西做（安全的）修改，而 cBPF 对拦截到的东西都是只能读、不能改的。译注。

eBPF 程序是事件驱动的，触发执行时，系统会传给它一些参数，<mark>这些输入（inputs）称为“上下文”（context）</mark>。
对于 **tc eBPF 程序来说，传递的上下文是 `skb`**，即网络设备 tc 层的 ingress 或 egress 路径上正在经过的数据包。

## 2.0 指令集架构

### 寄存器设计

eBPF 有

* 11 个寄存器 (R0 ~ R10)
* 每个寄存器都是 64bit，有相应的 32bit 子寄存器
* 指令集是固定的 64bit 位宽，**<mark>参考了 cBPF、x86_64、arm64 和 risc 指令集的设计</mark>**，
  目的是**方便 JIT 编译**（将 eBPF 指令编译成平台原生指令）。

**eBPF 兼容 cBPF**，并且与后者一样，给用户空间程序提供稳定的 ABI。

### 解释器 和 JIT 编译器

目前，x86_64、s390 和 arm64 平台的 Linux 内核都自带了 eBPF 解释器和 JIT 编译器
。还没有将 cBPF JIT 转换成 eBPF JIT 的平台，只能通过解释器执行。

此外，原来某些不支持 JIT 编译的 cBPF 代码，现在也能够在加载时自动转换成 eBPF 指
令，接下来或者通过解释器执行，或者通过 eBPF JIT 执行。一个例子就是 seccom BPF：
引入了 eBPF 指令之后，原来的 cBPF seccom 指令就自动被转换成 eBPF 指令了。

### 指令编码格式

eBPF <mark>指令编码格式</mark>：

* 8 bit code：存放真正的指令码（instruction code）
* 8 bit dst reg：存放指令用到的寄存器号（R0~R10）
* 8 bit src reg：同上，存放指令用到的寄存器号（R0~R10）
* 16 bit signed offset：取决于指令类型，可能是
    * a jump offset：in case the related condition is evaluated as true
    * a relative stack buffer offset for load/stores of registers into the stack
    * a increment offset：in case of an xadd alu instruction, it can be an 
* 32 bit signed imm：存放立即值（carries the immediate value）

### 新指令

eBPF 带来了几个新指令，例如

1. 工作在 64 位模式的 alu 操作
2. 有符号移位（signed shift）操作
3. load/store of double words
4. a generic move operation for registers and immediate values
5. operators for endianness conversion,
6. a call operation for invoking helper functions
7. an atomic add (xadd) instruction.

### 单个程序的指令数限制

与 cBPF 类似，eBPF 中单个程序的最大指令数（instructions/programm）是 4096。

> 译注：<mark>现在已经放大到了 100 万条</mark>。

这些指令序列（instruction sequence）在加载到内核之前会进行静态校验（statically verified），
以确保它们不会包含破坏内核稳定性的代码，例如无限循环、指针或数据泄露、非法内存访问等等。
cBPF 只支持前向跳转，而 eBPF <mark>额外支持了受限的后向跳转</mark> ——
只要后向跳转不会产生循环，即保证程序能在有限步骤内结束。

除此之外，eBPF 还引入了一些新的概念，例如 helper functions、maps、tail calls、object pinning。
接下来分别详细讨论。

## 2.1 辅助函数（Helper Functions）

辅助函数是一组<mark>内核定义的函数集</mark>，**使 eBPF 程序能从内核读取数据，
或者向内核写入数据**（retrieve/push data from/to the kernel）。

<mark>不同类型的 eBPF 程序能用到的 helper function 集合是不同的</mark>，例如，

* socket 层 eBPF 能使用的辅助函数，只是 tc 层 eBPF 能使用的辅助函数的一个子集。
* flow-based tunneling 场景中，封装/解封装用的辅助函数只能用在比较低层的 tc ingress/egress 层。

### 函数签名

与系统调用类似，**<mark>所有辅助函数的签名是一样的</mark>**，格式为：
`u64 foo(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)`。

### 调用约定

辅助函数的<mark>调用约定</mark>（calling convention）也是固定的：

* R0：存放程序返回值
* R1 ~ R5：存放函数参数（function arguments）
* R6 ~ R9：**被调用方**（callee）负责保存的寄存器
* R10：栈空间 load/store 操作用的只读 frame pointer

### 带来的好处

这样的设计有几方面好处：

* <mark>JIT 更加简单、高效</mark>。

    cBPF 中，为了调用某些特殊功能的辅助函数（auxiliary helper functions），对 load 指令进行了重载（overload），
    在数据包的某个看似不可能的位置（impossible packet offset）加载数据，以这种方式调用到辅助函数；每个 cBPF
    JIT 都需要实现对这样的 cBPF 扩展的支持。

    而在 eBPF 中，每个辅助函数都是以透明和高效地方式进行 JIT 编译的，这意味着
    JIT 编译器只需要 emit 一个 `call` 指令 —— 因为寄存器映射（register mapping）
    的设计中，eBPF 已经和底层架构的调用约定是匹配的了。

* <mark>函数签名使校验器能执行类型检查</mark>（type checks）。

    每个辅助函数都有一个配套的 `struct bpf_func_proto` 类型变量，

    ```c
    /* eBPF function prototype used by verifier to allow BPF_CALLs from eBPF programs
     * to in-kernel helper functions and for adjusting imm32 field in BPF_CALL instructions after verifying */
    struct bpf_func_proto {
    	u64 (*func)(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);
    	bool gpl_only;
    	bool pkt_access;
    	enum bpf_return_type ret_type;
    	enum bpf_arg_type arg1_type;
    	enum bpf_arg_type arg2_type;
    	enum bpf_arg_type arg3_type;
    	enum bpf_arg_type arg4_type;
    	enum bpf_arg_type arg5_type;
    };
    ```

    一个例子：

    ```c
    // net/core/filter.c

    BPF_CALL_2(bpf_redirect, u32, ifindex, u64, flags)
    {
    	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);
    	if (unlikely(flags & ~(BPF_F_INGRESS)))
    		return TC_ACT_SHOT;
    
    	ri->ifindex = ifindex;
    	ri->flags = flags;
    	return TC_ACT_REDIRECT;
    }

    static const struct bpf_func_proto bpf_redirect_proto = {
    	.func           = bpf_redirect,
    	.gpl_only       = false,
    	.ret_type       = RET_INTEGER,
    	.arg1_type      = ARG_ANYTHING,
    	.arg2_type      = ARG_ANYTHING,
    };
    ```

    <mark>校验器据此就能知道该 helper 函数的详细信息，
    进而确保该 helper 的类型与当前 eBPF 程序用到的寄存器内的内容是匹配的</mark>。

    helper 函数的参数类型有很多种，如果是指针类型（例如 `ARG_PTR_TO_MEM`），
    校验器还可以执行进一步的检查，例如判断这个缓冲区之前是否已经初始化了。

## 2.2 Maps

Map 是 eBPF 的另一个重要组成部分。
它是一种<mark>高效的 key/value 存储</mark>，map 的<mark>内容驻留在内核空间</mark>，
但可以**<mark>在用户空间通过文件描述符访问</mark>**。

Map 可以在多个 eBPF 程序之间共享，而且没有什么限制，例如，可以在一个 tc eBPF
程序和一个 tracing eBPF 程序之间共享。

### map 类型

Map 后端是由<mark>核心内核（the core kernel）提供</mark>的，可能是通用类型
（generic），也可能是专用类型（specialized type）；
**某些专业类型的 map 只能用于特定的子系统**，例如 [28]。

<mark>通用类型 map 当前是数组或哈希表结构</mark>（array or hash table），
可以是 per-CPU 的类型，也可以是 non-per-CPU 类型。

### 创建和访问 map

1. <mark>创建 map：只能从用户空间操作</mark>，通过 `bpf(2)` 系统调用完成。
1. 从 **eBPF 程序中**访问 map：<mark>通过辅助函数</mark>。
1. 从**用户空间**访问 map：通过 `bpf(2)` 系统调用。

### map 相关辅助函数调用

以上设计意味着，如果 eBPF 程序想调用某个 map 相关的辅助函数，
它需要将文件描述符编码到指令中 —— 文件描述符会进一步对应到 map 引用，
并放到正确的寄存器 —— `BPF_LD_MAP_FD(BPF_REG_1, fd)` 就是一个例子。
内核能识别出这种特殊 src 寄存器的情况，然后从文件描述符表中查找该 fd，进而找到真
正的 eBPF map，然后在内部对指令进行重写（rewrite the instruction）。

## 2.3 Object Pinning（目标文件锚定）

eBPF map 和 eBPF program 都是<mark>内核资源</mark>（kernel resource），
**只能通过文件描述符（file descriptor）访问**；而<mark>文件描述符背后是内核中的匿名 inode</mark>
（backed by anonymous inodes in the kernel）。

### 文件描述符方式的限制

以上这种方式有优点，例如：

* **用户空间程序**能使用**大部分文件描述符相关的 API**
* 在 Unix domain socket 传递文件描述符是**透明**的

但也有缺点：<mark>文件描述符的生命周期在进程生命周期之内</mark>，因此不同进程之间**共享 map 之类的东西就比较困难**。

* 这给 tc 等应用带来了很多不便。因为 tc 的工作方式是：**将程序加载到内核之后就退出**（而不是持续运行的进程）。
* 此外，从用户空间也无法**直接访问** map（`bpf(2)` 系统调用不算），否则这会很有用。
  例如，第三方应用可能希望在 eBPF 程序运行时（runtime）监控或更新 map 的内容。

针对这些问题，提出了几种**保持文件描述符 alive 的设想**，其中之一是重用 fuse，作为 tc 的 proxy。
这种情况下，文件描述符被 fuse implementation 所拥有，tc 之类的工具可以通过
unix domain sockets 来获取这些文件描述符。但又也带来了很大的新问题：
增加了新的依赖 fuse，而且需要作为额外的守护进程安装和启动。
大型部署中，都希望保持用户空间最小化（maintain a minimalistic user space）以节省资源。
因此这样的额外依赖难以让用户接受。

### BPF 文件系统（bpffs）

为了更好的解决以上问题，我们**在内核中实现了一个最小文件系统**（a minimal kernel space file system）[4]。

eBPF map 和 eBPF program 可以 pin（固定）到这个文件系统，这个过程称为 object pinning。
`bpf(2)` 系统调用也新加了两个命令用来 pin 或获取一个已经 pinned 的 object。
例如，<mark>tc 之类的工具利用这个新功能 [9] 就能在 ingress 或 egress 上共享 map</mark>。

eBPF 文件系统在每个 mount 命名空间创建一个挂载实例（keep an instance per mount namespace），
并支持 bind mounts、hard links 等功能，并与网络命令空间无缝集成。

## 2.4 尾调用（Tail Calls）

eBPF 的另一个概念是尾调用 [26]：从一个程序调用到另一个程序，且后者执行完之后不再
返回到前者。

* 不同于普通的函数调用，尾调用的开销最小；
* 底层<mark>通过 long jump 实现</mark>，复用原来是栈帧（reusing the same stack frame）。

### 程序之间传递状态

**<mark>尾调用的程序是独立验证的</mark>**（verified independently），
因此<mark>要在两个程序之间传递状态</mark>，就需要用到：

1. per-CPU maps，作为自定义数据的存储区（as scratch buffers），或者
2. skb 的某些可以存储自定义数据的字段，例如 `cb`（control buffer）字段

<mark>只有同类型的程序之间才可以尾调用</mark>，而且它们**要么都是通过解释器执行，
要么都是通过 JIT 编译之后执行**，不支持混合两种模式。

### 底层实现

尾调用涉及两个步骤：

1. 首先设置一个特殊的、称为<mark>程序数组</mark>（program array）的 map。

    这个 map 可以从用户空间通过 key/value 操作，其中 **value 是各个 eBPF 程序的文件描述符**。

2. 第二步是执行 `bpf_tail_call(void *ctx, struct bpf_map *prog_array_map, u32 index)` 辅助函数，其中

    * `prog_array_map` 就是前面提到的程序数组，
    * `index` 是程序数组的索引，表示希望跳转到这个位置的文件描述符所指向的程序。

    下面是这个辅助函数的进一步说明：

    ```c
    // include/uapi/linux/bpf.h

     * int bpf_tail_call(void *ctx, struct bpf_map *prog_array_map, u32 index)
     * 	Description
     * 		This special helper is used to trigger a "tail call", or in
     * 		other words, to jump into another eBPF program. The same stack
     * 		frame is used (but values on stack and in registers for the
     * 		caller are not accessible to the callee). This mechanism allows
     * 		for program chaining, either for raising the maximum number of
     * 		available eBPF instructions, or to execute given programs in
     * 		conditional blocks. For security reasons, there is an upper
     * 		limit to the number of successive tail calls that can be
     * 		performed.
     *
     * 		Upon call of this helper, the program attempts to jump into a
     * 		program referenced at index *index* in *prog_array_map*, a
     * 		special map of type **BPF_MAP_TYPE_PROG_ARRAY**, and passes
     * 		*ctx*, a pointer to the context.
     *
     * 		If the call succeeds, the kernel immediately runs the first
     * 		instruction of the new program. This is not a function call,
     * 		and it never returns to the previous program. If the call
     * 		fails, then the helper has no effect, and the caller continues
     * 		to run its subsequent instructions. A call can fail if the
     * 		destination program for the jump does not exist (i.e. *index*
     * 		is superior to the number of entries in *prog_array_map*), or
     * 		if the maximum number of tail calls has been reached for this
     * 		chain of programs. This limit is defined in the kernel by the
     * 		macro **MAX_TAIL_CALL_CNT** (not accessible to user space),
     * 		which is currently set to 32.
     * 	Return
     * 		0 on success, or a negative error in case of failure.
    ```

**<mark>内核会将这个辅助函数调用转换成一个特殊的 eBPF 指令</mark>**。另外，这个
program array 对于用户空间是只读的。

内核根据文件描述符（`fd = prog_array_map[index]`）查找相关的 eBPF 程序，然后自动将相应 map slot 程序指针
进行替换。如果 `prog_array_map[index]` 为空，内核就继续在原来的 eBPF 程序中继续执行 `bpf_tail_call()` 之后的指令。

尾调用是一个非常强大的功能，例如，解析网络头（network headers）可以通过 尾调用实现（
因为每解析一层就可以丢弃一层，没有再返回来的需求）。
另外，尾调用还能够在运行时（runtime）原子地添加或替换功能，改变执行行为。

## 2.5 安全：锁定镜像为只读模式、地址随机化

eBPF 有几种**防止有意或无意的内核 bug 导致程序镜像（program images）损坏**的技术 —— 即便这些 bug 跟 BPF 无关。

支持 `CONFG_DEBUG_SET_MODULE_RONX` 配置选项的平台，**<mark>启用这个配置后，
内核会将 eBPF 解释器的镜像设置为只读的</mark>** [2]。

当启用 JIT 编译之后，内核还会将生成的**可执行镜像**（generated executable images）
锁定为只读的，并且**对其地址进行随机化**，以使猜测更加困难。
镜像中的缝隙（gaps in the images）会填充 trap 指令（例如，x86_64 平台上填充的是 int3 opcode）
，用来捕获跳转探测（catching such jump probes）。

对于非特权程序（unprivileged programs），校验器还会对能使用的 helper 函数、指针
等施加额外的限制，以确保不会发生数据泄露。

## 2.6 LLVM

至此，还有一个重要方面一直没有讨论：<mark>如何编写 eBPF 程序</mark>。

cBPF 提供的选择很少：libpcap 里面的 cBPF compiler，bpf_asm，或者手写 cBPF 程序；
相比之下，eBPF 支持使用更更高层的语言（例如 C 和 P4）来编写，大大方便了 eBPF 程序的开发。

LLVM 有一个 **eBPF 后端**（back end），能生成（emit）包含 eBPF 指令的 ELF 文件。
Clang 这样的前端（front ends）能用来编译 eBPF 程序。

用 clang 来编译 eBPF 程序非常简单：`clang -O2 -target bpf -o bpf prog.o -c bpf prog.c`。
一个很有用的选项是指定输出汇编代码：`clang -O2 -target bpf -o - -S -c bpf prog.c` or
，或者用 readelf 之类的工具 dump 和 分析 ELF sections 和 relocations。

典型的工作流：

1. 用 C 编写 eBPF 代码
2. 用 clang/llvm 编译成目标文件
3. 用 tc 之类的加载器（能与 cls_bpf 分类器交互）将目标文件加载到内核

# 3 tc `cls_bpf` 和 eBPF

## 3.0 `cls_bpf` 和 `act_bpf`

### 可编程 tc 分类器 `cls_bpf`

`cls_bpf` 作为一种分类器（classifier，也叫 filter），<mark>2013 年就出现在了 cBPF 中</mark> [6]。
通过 **`bpf_asm`、libpcap/tcpdump** 或其他一些 cBPF 字节码生成器能对它进行编程。
步骤：

1. 使用工具生成字节码（byte code）
2. 将字节码传递给 tc 前端
3. tc 前端**<mark>通过 netlink 消息将字节码下发到 tc cls_bpf 分类器</mark>**

### 可编程 tc 动作（action）`act_bpf`

后来又出现 act_bpf [20]，这是一种 tc action，因此与其他 tc action 一样，act_bpf
能被 **attach 到 tc 分类器**，作为分类器执行完之后对包要执行的动作（即，
分类器执行完之后返回一个 action code，act_bpf 能根据这个 code 执行相应的行为，
例如丢弃包）。

<mark>act_bpf 功能与 cls_bpf 几乎相同，区别在于二者的返回码类型</mark>：

* cls_bpf 返回的是 tc classid (major/minor)
* act bpf 返回的是 tc action opcode

> 这里对 cls_bpf/act_bpf 的解释太简单。想进一步了解，可参考：
> [(译) 深入理解 tc ebpf 的 direct-action (da) 模式（2020）]({% link _posts/2021-02-21-understanding-tc-da-mode-zh.md %}) 译注。

**act_bpf 的缺点是**：

1. 只适用用于 cBPF
2. **无法对包进行修改（mangle）**

因此通常需要用 action pipeline 做进一步处理，例如 `act_pedit`，代价是
**额外的包级别（packet-level）的性能开销**。

### eBPF 对 cls_bpf 的支持

eBPF 引入 `BPF_PROG_TYPE_SCHED_CLS` [8] 和 `BPF_PROG_TYPE_SCHED_ACT` [7] 之后也
**支持了 `cls_bpf` 和 `act_bpf`**。

* 这两种类型的 fast path 都在 RCU 内运行（run under RCU）
* 二者做的主要事情也就是**<mark>调用 BPF_PROG_RUN()</mark>**，后者会解析到
  `(*filter->bpf_func)(ctx, filter->insnsi)`，其中 `ctx` 参数包含了 skb 信息
* `bpf_func()` 里对 skb 进行处理，<mark>接下来可能会执行</mark>：

    * eBPF 解释器（`bpf_prog_run()`）
    * JIT 编译器生成的 JIT image

### eBPF cls_bpf 带来的好处

`cls_bpf_classify()` 之类的函数感知不到底层 BPF 类型（eBPF 还是 cBPF），
因此对于 cBPF 和 eBPF，skb 的穿梭路径是一样的。

`cls_bpf` 相比于其他类型 tc 分类器的一个优势：<mark>能实现高效、非线性分类功能</mark>（以及
direct actions，后面会介绍），这意味着 **BPF 程序可以得到简化，只解析一遍就能处理不同类型的 skb**
（a single parsing pass is enough to process skbs of different types）。

历史上，tc 支持 attach 多个分类器 —— 前面的没有匹配成功时，接着匹配下一个。
因此，如果一个包要经过多个分类器，那它的某些字段就会在每个分类器中都要解析一遍，这显然是非常低效的。

有了 `cls_bpf`，使用单个 eBPF 程序（用作分类器）就可以轻松地避免这个问题，
或者是使用 eBPF 尾调用结构，后者支持 packet parser 的某些部分进行原子替换。
此时，eBPF 程序就能根据分类或动作结果（classification or action outcome），
来返回不同的 classid 或 opcodes 了，下面进一步介绍。

## 3.1 工作模式：传统模式和 direct-action 模式

cls_bpf 在处理 action 方面有两种工作模式：

* 传统模式：分类之后执行 `tcf_exts_exec()`
* direct-action 模式

    随着 eBPF 功能越来越强大，它能做的事情不止是分类，例如，分类器自己就
    能够（无需 action 参与）修改包的内容（mangle packet contents）、更新校验和
    （update checksums）等。

    因此，社区决定引入一个 direct action (da) mode [3]。使用 cls_bpf 时，这是推荐的模式。

在 da 模式中，cls_bpf 对 skb 执行 action，返回的是 tc opcode，
最终形成一个紧凑、轻量级的镜像（compact, lightweight image）。
而在此之前，需要使用 tc action 引擎，必须穿越多层 indirection 和 list handling。
对于 eBPF 来说，classid 可以存储在 `skb->tc_classid`，然后返回 action opcode。
这个 opcode 对于 cBPF drop action 这样的简单场景也是适用的。

> 这里对 da 的解释过于简单，很难理解。可参考
> 下面这篇文章，其对 da 模式的来龙去脉、工作原理和内核实现有更深入介绍：
> [(译) 深入理解 tc ebpf 的 direct-action (da) 模式（2020）]({% link _posts/2021-02-21-understanding-tc-da-mode-zh.md %}) 译注。

此外，cls_bpf 也支持多个分类器，每个分类器可以工作在不同模式（da 和 non-da） —— 只要你有这个需要。
但建议 fast path 越紧凑越好，<mark>对应高性能应用，推荐使用单个 cls_bpf 分类器
并且工作在 da 模式</mark>，这足以满足大部分需求了。

## 3.2 特性

eBPF cls_bpf 带来了很多新特性，例如可以读写包的很多字段、一些新的辅助函数。
这些特性或功能可以组合使用，产生强大的效果。

### skb 可读/写字段

For the context (skb here is of type `struct sk_buff`), cls_bpf 允许<mark>读写</mark>下列字段：

* skb->mark
* skb->priority 
* skb->tc_index
* skb->cb[5]
* skb->tc_classid members

允许<mark>读</mark>下列字段：

* skb->len
* skb->pkt type
* skb->queue mapping
* skb->protocol
* skb->vlan tci
* skb->vlan proto
* skb->vlan present
* skb->ifindex (translates to netdev’s ifindex)
* skb->hash

### 辅助函数

cls_bpf 程序类型中有很多的 helper 函数可供使用。包括

* 对 map 进行操作（get/update/delete）的辅助函数
* 尾调用辅助函数
* 对 skb 进行 mangle 的辅助函数（storing and loading bytes into the skb for parsing and packet mangling）
* **<mark>重新计算 L3/L4 checksum</mark>** 的辅助函数
* 封装/解封装（VLAN、VxLAn 等隧道）相关辅助函数

### 重定向（redirection）

cls_bpf 还能对 skb 进行重定向，包括，

* 通过 `dev_queue_xmit()` 在 egress 路径中重定向，或者
* 在 `dev_forward_skb()` 中重定向回 ingress path。

重定向有两种可能的方式：

* 方式一：在 eBPF 程序运行时（runtime）复制一份数据包（clone skb）
* 方式二：<mark>无需复制数据包</mark>，性能更好

    需要 cls_bpf 运行在 da 模式，并且**返回值为 `TC_ACT_REDIRECT`**。
    <mark>sch_clsact 等 qdisc 在 ingress/egress path 上支持这种这种 action</mark>。

    eBPF 程序在 runtime 将必要的重定向信息放到一个 per-CPU scratch buffer，
    然后返回相关的 opcode，接下来内核会通过 `skb_do_redirect()` 来完成重定向。
    这种是一种性能优化方式，能显著提升转发性能。

### 调试（Debug）

可以使用 `bpf_trace_printk()` 辅助函数，它能将消息打印到 trace pipe，格式与 `printk()` 类似，
然后可以通过 <mark>tc exec bpf dbg</mark> 等命令读取。

虽然它作为 helper 函数有一些限制，
能传递五个参数，其中前两个是格式字符串，但这个功能还是给编写和调试 eBPF 程序带来了很大便利。

还有其他一些 helper 函数，例如，

* 读取 skb 的 cgroup classid（`net_cls` cgroup），
* 读取 dst 的 routing realm (`dst->tclassid`)
* 获取一个随机数（例如用于采样）
* 获取当前包正在被哪个 CPU 处理
* 获取纳秒为单位的当前时间（`ktime_t`）

### 可以 attach 到的 tc hooks

cls_bpf 能 attach 到许多与 tc 相关的 hook 点。这些 <mark>hook 点可分为三类</mark>：

1. ingress hook
1. egress hook，这是最近才引入的
1. classification hook inside classful qdiscs on egress.

前两种可以通过 `sch_clsact` qdisc (或 sch_ingress
for the ingress-only part) 配置，而且是在 RCU 上下文中无锁运行的 [12]。

> 可进一步参考：
>
> * [(译) 深入理解 tc ebpf 的 direct-action (da) 模式（2020）]({% link _posts/2021-02-21-understanding-tc-da-mode-zh.md %})
> * [(译) 为容器时代设计的高级 eBPF 内核特性（FOSDEM, 2021）]({% link _posts/2021-02-13-advanced-bpf-kernel-features-for-container-age-zh.md %})
>
> 译注。

egress hook 在 `dev_queue_xmit()` 中执行（before fetching the transmit queue
from the device）。

## 3.3 前端（Front End）

tc `cls_bpf` 的 iproute2 前端 [10] [11] [9]
在将 cls_bpf 数据通过 netlink 发送到内核之前，在背后做了很多工作。
iproute2 包含了一个通用 ELF 加载器后端，适用于下面几个部分，实现了通用代码的共享：

* f_bpf (classifier)
* m_bpf (action)
* e_bpf (exec)

编译和加载所涉及到的 iproute2/tc 内部工作：

* 当用 clang 编译 eBPF 代码时，它会生成一个 ELF 格式的目标文件，
  接下来通过 tc 加载到内核。这个<mark>目标文件就是一个容器（container）</mark>，
  其中包含了 tc 所需的所有数据：它会<mark>从中提取数据、重定位（relocate）并加载到 cls_bpf hook 点</mark>。

* 在启动时，<mark>tc 会检查（如果有必要还会 mount）bpf 文件系统</mark>，用于 object pinning。
  默认目录是  `/sys/fs/bpf`。然后会加载和生成一个 pinning 配置用的哈希表，给 map
  共享用。

* 之后，tc 会<mark>扫描目标文件中的 ELF sections</mark>。一些预留的 section 名，

    * `maps`：for eBPF map specifications (e.g. map type, key and value size, maximum elements, pinning, etc)
    * `license`：for the licence string, specified similarly as in Linux kernel modules.
    * `classifier`：默认情况下，<mark>cls_bpf 分类器所在的 section</mark>
    * `act`：默认情况下，<mark>act_bpf 所在的 section</mark>

* tc 首先读取辅助功能区（ancillary sections），这包括 **ELF 的符号表 `.symtab` 和字符串表 `.strtab`**。

  由于 eBPF 中的所有东西都是通过文件描述符来从用户空间访问的，
  因此 <mark>tc 前端首先需要基于 ELF 的 relocation entries 生成 maps</mark>，
  它将文件描述符作为立即值（immediate value）插入相应的指令。

  取决于 map 是否是 pinned，tc 或者<mark>从 bpffs 的指定位置加载一个 map 文件描述符</mark>，
  或者<mark>生成一个新的</mark>，并且如果有需要，将它 pin 到 bpffs。

### 处理 Object pinning

sharing maps 有<mark>三种不同的 scope</mark>：

1. `/sys/fs/bpf/tc/globals`：全局命名空间
2. `/sys/fs/bpf/tc/<obj-sha>`：对象命名空间（object namespace）
3. 自定义位置

eBPF maps 可以在不同的 cls_bpf 实例之间共享。
不止通用类型 map（例如 array、hash table）可以共享，专业类型的 map，例如 tracing
eBPF 程序（kprobes）使用的 eBPF maps 也与 cls_bpf/act_bpf 使用的 eBPF maps 实现共享。

Object pinning 时，tc 会在 ELF 的符号表和字符串表中寻找 map name。
map 创建完成后，tc 会找到程序代码所在的 section，然后带着 map 的文件描述符信
息<mark>执行重定位</mark>，并将程序代码加载到内核。

### 处理尾调用

当用到了尾调用且尾调用 subsection 也在 ELF 文件中时，tc 也会将它们加载到内核。
从 tc 加载器的角度看，尾调用可以任意嵌套，但内核运行时对嵌套是有限制的。
另外，**尾调用用到的程序数组（program array）也能被 pin**，
这样能在用户空间根据程序的运行时行为来修改这个数组（决定尾调用到哪个程序）。

### `tc exec bpf graft`

tc 有个 graft（嫁接） 选项，

`tc exec bpf [ graft MAP_FILE ] [ key KEY ]`

它能**在运行时替换 section**（replacing such sections during runtime）。
Grafting 实际上**所做的事情和加载一个 `cls_bpf` 分类器差不多**，区别在于
产生的<mark>文件描述符并不是通过 netlink —— 而是通过相应的 map —— push 到内核</mark>。

tc cls_bpf 前端还允许通过 `execvpe()` 将新生成的 map 的文件描述符传递给新创建的 shell，
这样程序就能像 stdin、stdout、stderr 一样全局地使用它；或者，文件描述符集合还能通过 Unix domain socket 传递给其他进程。
在这两种情况下，cloned 文件描述符的生命周期仍然与进程的生命周期紧密相连。
<mark>通过 bpf fs 获取文件描述符</mark>是最灵活也是最推荐的方式，[9]
也适用于第三方用户空间程序管理 eBPF map 的内容。

### `tc exec bpf dbg`

tc 前端提供了打印 trace pipe 的命令行工具：`tc exec bpf dbg`。这个命令
**会用到 trace fs**，它会自动定位 trace fs 的挂载点。

## 3.4 工作流（Workflow）

一个典型的工作流是：**将 `cls_bpf` 分类器以 da 模式加载到内核**，整个过程简单直接。

来看下面的例子：

* 用 clang 编译源文件 foo.c，生成的目标文件 foo.o；foo.o 中包含两个 section `p1` 和 `p2`
* 启用内核的 JIT 编译功能
* 给网络设备 em1 添加一个 clsact qdisc
* 将目标文件分别加载到 em1 的 ingress 和 egress 路径上

```shell
$ clang -O2 -target bpf -o foo.o -c foo.c
$ sysctl -w net.core.bpf_jit_enable=1
$ tc qdisc add dev em1 clsact
$ tc qdisc show dev em1
[...]
qdisc clsact ffff: parent ffff:fff1

$ tc filter add dev em1 ingress bpf da obj foo.o sec p1
$ tc filter add dev em1 egress bpf da obj foo.o sec p2
```

```shell
$ tc filter show dev em1 ingress
filter protocol all pref 49152 bpf
filter protocol all pref 49152 bpf handle
0x1 foo.o:[p1] direct-action

$ tc filter show dev em1 egress
filter protocol all pref 49152 bpf
filter protocol all pref 49152 bpf handle
0x1 foo.o:[p2] direct-action
```


最后将它们删除：

```shell
$ tc filter del dev em1 ingress pref 49152
$ tc filter del dev em1 egress pref 49152
```

## 3.5 编程

iproute2 源码中 `examples/bpf/` 目录下包含很多入门示例，是用 restricted C 编写的 eBPF 代码。
实现这样的分类器还是比较简单的。

与传统用户空间 C 程序相比，eBPF 程序在某些地方是受限的。
每个这样的分类器都必须放到 ELF sections。因此，<mark>一个目标文件会包含一个或多个 eBPF 分类器</mark>。

### 代码共享：内联函数或尾调用

**分类器之间共享代码**有两种方式：

1. `__always_inline` 声明的内联函数

    clang 需要将整个扁平程序（the whole, flat program）编程成 eBPF 指令流，
    分别放到各自的 ELF section。

    eBPF 不支持共享库（shared libraries）或**可重入 eBPF 函数**（eBPF functions as relocation entries）。
    像 tc 这样的 eBPF **加载器**，是无法将多个库拼装成单个扁平 eBPF 指令流数组的
    （a single flat array of eBPF instructions） —— 除非它实现**编译器**的大部分功能。

    因此，加载器和 clang 之间有一份“契约”（contract），其中明确规定了生成的 ELF 文件中，
    特定 section 中必须包含什么样的 eBPF 指令。

    唯一允许的重定位项（relocation entries）是与 map 相关的，这种情况下需要先确定文件描述符。

2. 尾调用

    前面已经介绍过了。

### 有限栈空间和全局变量

eBPF 程序的栈空间非常有限，只有 512KB，因此用 C 实现 eBPF 程序时需要特别注意这一点。
常规 C 程序中常见的<mark>全局变量在这里不支持的</mark>。

eBPF maps（在 tc 中对应的是 `struct bpf_elf_map`）定义在各自的 ELF sections
中，但可以在程序 sections 中访问到。
因此，如果真的需要全局“变量”，可以这样实现：创建一个 per-CPU 或 non-per-CPU array map，
但其中只存储有一个值，这样这个变量就能被多个 section 中的程序访问，例如
entry point sections、tail called sections 等。

### 动态循环

另一个限制是：eBPF 程序不支持动态循环（dynamic looping），只支持编译时已知的常量循环
（compile-time known constant bounds），后者能被 clang 展开。

编译时不能确定是否为常量次数的循环会被校验器拒绝，因为这样的程序无法静态验证
（statically verify）它们是否确定会终止。

# 4 总结及未来展望

<mark>cls_bpf 是 tc 家族中的一个灵活高效的分类器（及 action）</mark>，
它提供了强大的**数据平面可编程能力**，适用于大量不同场景，例如解析、查找或更新
（例如 map state），以及对网络包进行修改（mangling）等。
当使用底层平台的 eBPF JIT 后端进行编译之后，这些 eBPF 程序能以<mark>平台原生性能执行</mark>。
eBPF 是为**既要求高性能又要求高灵活性**的场景设计的。

虽然一些内部细节看上去有点复杂，让人望而生畏，但了解了 eBPF 的限制条件之后，
编写 cls_bpf eBPF 程序其实与编写普通用户空间程序并不会复杂多少。
另外，tc 命令行在设计时也考虑到了易用性，例如用 tc 处理 cls_bpf 前端只需要几条命令。

cls_bpf 代码 及其 tc 前端、eBPF 内部实现及其 clang 编译器后端全部都是开源的，
由社区开发和维护。

目前还有很多的增强特性和想法正在讨论和评估之中，例如将 cls_bpf offload 到可编程网卡上。
[CRIU](https://criu.org/Main_Page)（checkpoint restore in user space）
目前还只支持 cBPF，如果实现了对 eBPF 的支持，对容器迁移将非常有用。

# 参考资料

1. Begel, A.; Mccanne, S.; and Graham, S. L. 1999. Bpf+: Exploiting global data-flow optimization in a generalized packet filter architecture. In In SIGCOMM, 123–134.
2. Borkmann, D., and Sowa, H. F. 2014. net: bpf: make ebpf interpreter images read-only. Linux kernel, commit [60a3b2253c41](https://github.com/torvalds/linux/commit/60a3b2253c41).
3. Borkmann, D., and Starovoitov, A. 2015. cls_bpf: introduce integrated actions. Linux kernel, commit 045efa82ff56.
4. Borkmann, D.; Starovoitov, A.; and Sowa, H. F. 2015.  bpf: add support for persistent maps/progs. Linux kernel, commit [b2197755b263](https://github.com/torvalds/linux/commit/b2197755b263).
5. Borkmann, D. 2013a. filter: bpf_asm: add minimal bpf asm tool. Linux kernel, commit [3f356385e8a4](https://github.com/torvalds/linux/commit/3f356385e8a4).
6. Borkmann, D. 2013b. net: sched: cls_bpf: add bpf-based classifier. Linux kernel, commit [7d1d65cb84e1](https://github.com/torvalds/linux/commit/7d1d65cb84e1).
7. Borkmann, D. 2015a. act bpf: add initial ebpf support for actions. Linux kernel, commit [a8cb5f556b56](https://github.com/torvalds/linux/commit/a8cb5f556b56).
8. Borkmann, D. 2015b. cls bpf: add initial ebpf support for programmable classifiers. Linux kernel, commit [e2e9b6541dd4](https://github.com/torvalds/linux/commit/e2e9b6541dd4).
9. Borkmann, D. 2015c. ff,mg bpf: allow for sharing maps.  iproute2, commit 32e93fb7f66d.
10. Borkmann, D. 2015d. tc: add ebpf support to f_bpf.
11. Borkmann, D. 2015e. tc, bpf: finalize ebpf support for cls and act front-end. iproute2, commit 6256f8c9e45f.
12. Borkmann, D. 2016. net, sched: add clsact qdisc. Linux kernel, commit [1f211a1b929c](https://github.com/torvalds/linux/commit/1f211a1b929c).
13. de Bruijn, W. 2015a. packet: add classic bpf fanout mode. Linux kernel, commit 47dceb8ecdc1.
14. de Bruijn, W. 2015b. packet: add extended bpf fanout mode. Linux kernel, commit f2e520956a1a.
15. Drewry, W. 2012. seccomp: add system call filtering using bpf. Linux kernel, commit e2cfabdfd075.
16. Gallek, C. 2016. soreuseport: setsockopt so attach reuseport [ce]bpf. Linux kernel, commit 538950a1b752.
17. Herbert, T. 2016. kcm: Kernel connection multiplexor module. Linux kernel, commit [ab7ac4eb9832](https://github.com/torvalds/linux/commit/ab7ac4eb9832).
18. Mccanne, S., and Jacobson, V. 1992. The bsd packet filter: A new architecture for user-level packet capture. 259–269.
19. Pirko, J. 2012. team: add loadbalance mode. Linux kernel, commit 01d7f30a9f96.
20. Pirko, J. 2015. tc: add bpf based action. Linux kernel, commit [d23b8ad8ab23](https://github.com/torvalds/linux/commit/d23b8ad8ab23).
21. Starovoitov, A., and Borkmann, D. 2014. net: filter: rework/optimize internal bpf interpreter’s instruction set.  Linux kernel, commit [bd4cf0ed331a](https://github.com/torvalds/linux/commit/bd4cf0ed331a).
22. Starovoitov, A. 2014a. bpf: expand bpf syscall with program load/unload. Linux kernel, commit [09756af46893](https://github.com/torvalds/linux/commit/09756af46893).
23. Starovoitov, A. 2014b. bpf: introduce bpf syscall and maps. Linux kernel, commit [99c55f7d47c0](https://github.com/torvalds/linux/commit/99c55f7d47c0).
24. Starovoitov, A. 2014c. bpf: verifier (add verifier core).  Linux kernel, commit [17a5267067f3](https://github.com/torvalds/linux/commit/17a5267067f3).
25. Starovoitov, A. 2014d. net: filter: x86: internal bpf jit.  Linux kernel, commit 622582786c9e.
26. Starovoitov, A. 2015a. bpf: allow bpf programs to tail-call other bpf programs. Linux kernel, commit [04fd61ab36ec](https://github.com/torvalds/linux/commit/04fd61ab36ec).
27. Starovoitov, A. 2015b. tracing, perf: Implement bpf programs attached to kprobes. Linux kernel, commit [2541517c32be](https://github.com/torvalds/linux/commit/2541517c32be).
28. Starovoitov, A. 2016. bpf: introduce bpf map type stack trace. Linux kernel, commit [d5a3b1f69186](https://github.com/torvalds/linux/commit/d5a3b1f69186).
