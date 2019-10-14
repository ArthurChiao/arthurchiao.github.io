---
layout    : post
title     : "[译] Cilium：BPF 和 XDP 参考指南"
date      : 2019-10-09
lastupdate: 2019-10-09
categories: cilium bpf xdp
---

### 译者序

本文翻译自 Cilium 1.6 的官方文档：[**BPF and XDP Reference Guide**](
https://docs.cilium.io/en/v1.6/bpf/)。

本文对排版做了一些调整，以更适合网页阅读。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

---

> 本文的目标读者是 **希望在技术层面对 BPF 和 XDP 有更深入理解的开发者和用户**。虽
> 然阅读本文有助于拓宽读者对 Cilium 的认识，但这并不是使用 Cilium 的前提条件。

BPF 是 **Linux 内核中**一个高度灵活与高效的**类虚拟机**（virtual machine-like）
组件，它以一种安全的方式在许多 hook 点执行字节码（bytecode ）。很多**内核子系统**
都已经使用了 BPF，比如常见的**网络**（networking）、**跟踪**（tracing）与**安全**
（security ，例如沙盒）。

BPF 1992 年就出现了，但本文介绍的是**扩展的 BPF**（extended Berkeley Packet
Filter，eBPF）。eBPF 最早出现在 3.18 内核中，此后原来的 BPF 就被称为 **“经典”
BPF**（classic BPF, cBPF），cBPF 现在基本已经废弃了。很多人知道 cBPF 是因为它是
`tcpdump` 的包过滤语言。**现在，Linux 内核只运行 eBPF，内核会将加载的 cBPF 字节码
透明地转换成 eBPF 再执行**。如无特殊说明，本文中所说的 BPF 都是泛指 BPF 技术。

虽然“伯克利包过滤器”（Berkeley Packet Filter）这个名字听起来像是专用于数据包过
滤的，但现今这个指令集已经足够通用和灵活，因此现在 BPF 也有很多网络之外的使用案例，
下文中会列出一些使用 BPF 的项目。

**Cilium 在它的 datapath 中重度使用了 BPF 技术**，更多信息请参考 Cilium 的
[datapath 架构](https://docs.cilium.io/en/v1.6/architecture/#datapath)
文档。本文的目标是提供一份 BPF 参考指南，以帮助读者理解 BPF 本身、BPF 网络相关的
使用方式（包括利用 `tc` 和 XDP 加载 BPF 程序），以及协助开发 Cilium 的 BPF 模板。

### 目录

1. [BPF 架构](#bpf_arch)
    * [1.1 指令集](#bpf_instruction)
    * [1.2 辅助函数](#bpf_helper)
    * [1.3 BPF Map](#bpf_maps)
    * [1.4 Object Pinning](#bpf_obj_pinning)
    * [1.5 尾调用](#bpf_tail_call)
    * [1.6 BPF-to-BPF 函数调用](#bpf_to_bpf_call)
    * [1.7 JIT](#bpf_jit)
    * [1.8 加固](#bpf_hardening)
    * [1.9 Offload](#bpf_offloads)
2. [工具链](#toolchain)
    * [2.1 开发环境](#tool_dev_env)
    * [2.2 LLVM](#tool_llvm)
        * [2.2.1 BPF Target（目标平台）](#ch_2.2.1)
        * [2.2.2 调试信息（DWARF、BTF）](#ch_2.2.2)
        * [2.2.3 BPF 指令集](#ch_2.2.3)
        * [2.2.4 指令和寄存器位宽（64/32 位）](#ch_2.2.4)
        * [2.2.5 C BPF 代码注意事项](#ch_2.2.5)
    * [2.3 iproute2](#tool_iproute2)
        * [2.3.1 加载 XDP BPF 对象文件](#ch_2.3.1)
        * [2.3.2 加载 tc BPF 对象文件](#ch_2.3.2)
        * [2.3.3 使用 netdevsim 驱动测试 BPF offload](#ch_2.3.3)
    * [2.4 bpftool](#tool_bpftool)
    * [2.5 BPF 相关的 sysctl 参数](#tool_bpf_sysctls)
    * [2.6 内核测试](#tool_kernel_testing)
    * [2.7 JIT 调试](#tool_jit_debug)
    * [2.8 内省（Introspection）](#tool_introspection)
    * [2.9 其他（Misc）](#tool_misc)
3. [程序类型](#prog_type)
    * [3.1 XDP](#prog_type_xdp)
    * [3.2 tc](#prog_type_tc)

<a name="bpf_arch"></a>

# 1 BPF 架构

**BPF 不仅仅是一个指令集，它还提供了围绕自身的一些基础设施**，例如：

1. **BPF map**：高效的 key/value 仓库
1. **辅助函数**（helper function）：可以更方便地利用内核功能或与内核交互
1. **尾调用**（tail call）：高效地调用其他 BPF 程序
1. **安全加固原语**（security hardening primitives）
1. 用于钉住（pin）对象（例如 map、程序）的**伪文件系统**（`bpffs`）
1. 支持 BPF **offload**（例如 offload 到网卡）的基础设施

LLVM 提供了一个 **BPF 后端**（back end），因此使用 clang 这样的工具就可以将 C 代
码编译成 BPF **对象文件**（object file），然后再加载到内核。BPF 深度绑定 Linux
内核，可以在 **不牺牲原生内核性能的前提下实现（对内核的）完全可编程**（full
programmability）。

另外， **使用 BPF 的内核子系统也是 BPF 基础设施的一部分**。本文将主要讨论 **tc
和 XDP** 这两个子系统，它们都支持 attach（附着）BPF 程序。

* **XDP BPF 程序**会被 attach 到**网络驱动的最早阶段**（earliest networking driver
  stage），**驱动收到包之后就会触发 BPF 程序的执行**。从定义上来说，这**可以取得
  最好的包处理性能**，因为这已经是**软件中最早可以处理包的位置**了。但也正是因为
  这一步的处理在网络栈中是如此之早，**协议栈此时还没有从包中提取出元数据**，（因此
  XDP BPF 程序无法利用这些元数据）。
* **tc BPF 程序在内核栈中稍后面的一些地方执行，因此它们可以访问更多的元数据和一
  些核心的内核功能**。

除了 tc 和 XDP 程序之外，还有很多其他内核子系统也在使用 BPF，例如跟踪子系统（
kprobes、uprobes、tracepoints 等等）。

下面的各小节进一步介绍 BPF 架构。

<a name="bpf_instruction"></a>

## 1.1 指令集

### 1.1.1 指令集

BPF 是一个通用目的 RISC 指令集，其**最初的设计目标**是：用 C 语言的一个子集**编
写**程序，然后用一个编译器后端（例如 LLVM）将其**编译**成 BPF 指令，稍后内核再通
过一个位于内核中的（in-kernel）即时编译器（JIT Compiler）将 BPF 指令**映射**成处
理器的原生指令（opcode ），以取得在内核中的最佳执行性能。

将这些指令下放到内核中可以带来如下好处：

* **无需在内核/用户空间切换**就可以实现内核的可编程。例如，Cilium 这种和网络相关
  的 BPF 程序能直接在内核中实现灵活的容器策略、负载均衡等功能，而无需将包送先
  到用户空间，处理之后再送回内核。需要在 **BPF 程序之间或内核/用户空间之间共享状
  态**时，可以使用 BPF map。
* **可编程 datapath** 具有很大的灵活性，因此程序能**在编译时将不需要的特性禁用掉，
  从而极大地优化程序的性能**。例如，如果容器不需要 IPv4，那编写 BPF 程序时就可以
  只处理 IPv6 的情况，从而节省了快速路径（fast path）中的资源。
* 对于网络场景（例如 tc 和 XDP），BPF 程序可以在**无需重启内核、系统服务或容器的
  情况下实现原子更新，并且不会导致网络中断**。另外，**更新 BPF map 不会导致程序
  状态（program state）的丢失**。
* BPF 给用户空间**提供了一个稳定的 ABI**，而且**不依赖任何第三方内核模块**。BPF
  是 Linux 内核的一个核心组成部分，而 Linux 已经得到了广泛的部署，因此可以保证现
  有的 BPF 程序能在新的内核版本上继续运行。这种保证与**系统调用**（内核提供给用
  户态应用的接口）是同一级别的。另外，BPF 程序**在不同平台上是可移植的**。
* BPF 程序**与内核协同工作**，**复用已有的内核基础设施**（例如驱动、netdevice、
  隧道、协议栈和 socket）和工具（例如 iproute2），以及内核提供的安全保证。**和内
  核模块不同，BPF 程序会被一个位于内核中的校验器（in-kernel verifier）进行校验，
  以确保它们不会造成内核崩溃、程序永远会终止等等**。例如，XDP 程序会复用已有的内
  核驱动，能够直接操作存放在 DMA 缓冲区中的数据帧，而不用像某些模型（例如 DPDK）
  那样将这些数据帧甚至整个驱动暴露给用户空间。而且，XDP 程序**复用**内核协议栈而
  不是绕过它。BPF 程序可以看做是内核设施之间的通用“胶水代码”，用于设计巧妙的程序
  ，解决特定的问题。

BPF 程序在内核中的执行总是**事件驱动**的！例如：

* 如果网卡的 ingress 路径上 attach 了 BPF 程序，那当网卡收到包之后就会触发这
  个 BPF 程序的执行。
* 在某个**有 kprobe 探测点的内核地址** attach 一段 BPF 程序后，当
  内核执行到这个地址时会发生**陷入**（trap），进而唤醒 **kprobe 的回调函数**，后
  者又会触发 attach 的 BPF 程序的执行。

### 1.1.2 BPF 寄存器和调用约定

BPF 由下面几部分组成：

1. 11 个 64 位寄存器（这些寄存器包含 32 位子寄存器）
1. 一个程序计数器（program counter，PC）
1. 一个 512 字节大小的 BPF 栈空间

寄存器的名字从 **`r0` 到 `r10`**。**默认的运行模式是 64 位**，32 位子寄存器只能
通过特殊的 ALU（arithmetic logic unit）访问。向 32 位子寄存器写入时，会用 0 填充
到 64 位。

`r10` 是唯一的只读寄存器，其中存放的是访问 BPF 栈空间的栈帧指针（frame pointer）
地址。`r0` - `r9` 是可以被读/写的通用目的寄存器。

BPF 程序可以调用核心内核（而不是内核模块）预定义的一些辅助函数。**BPF 调用约定**
定义如下：

* `r0` 存放被调用的辅助函数的返回值
* `r1` - `r5` 存放 BPF 调用内核辅助函数时传递的参数
* `r6` - `r9` 由被调用方（callee）保存，在函数返回之后调用方（caller）可以读取

BPF 调用约定足够通用，**能够直接映射到 `x86_64`、`arm64` 和其他 ABI**，因此所有
的 **BPF 寄存器可以一一映射到硬件 CPU 寄存器**，JIT 只需要发出一条调用指令，而不
需要额外的放置函数参数（placing function arguments）动作。这套约定在不牺牲性能的
前提下，考虑了尽可能通用的调用场景。目前不支持 6 个及以上参数的函数调用，内核中
BPF 相关的辅助函数（从 `BPF_CALL_0()` 到 `BPF_CALL_5()` 函数）也特意设计地与此相
匹配。

`r0` 寄存器还用于保存 **BPF 程序的退出值**。退出值的语义由程序类型决定。另外，
当将执行权交回内核时，退出值是以 32 位传递的。

`r1` - `r5` 寄存器是 **scratch registers**，意思是说，如果要在多次辅助函数调用之
间重用这些寄存器内的值，那 BPF 程序需要负责将这些值临时转储（spill）到 BPF 栈上
，或者保存到被调用方（callee）保存的寄存器中。**Spilling**（倒出/泼出/溅出/涌出）
的意思是这些寄存器内的变量被移到了 BPF 栈中。相反的操作，即将变量从 BPF 栈移回寄
存器，称为 **filling**（填充）。**spilling/filling 的原因是寄存器数量有限**。

BPF 程序开始执行时，**`r1` 寄存器中存放的是程序的上下文**（context）。上下文就是
**程序的输入参数**（和典型 C 程序的 `argc/argv` 类似）。**BPF 只能在单个上下文中
工作**（restricted to work on a single context）。这个上下文是由程序类型定义的，
例如，网络程序可以将网络包的内核表示（`skb`）作为输入参数。

**BPF 的通用操作都是 64 位的**，这和默认的 64 位架构模型相匹配，这样可以对指针进
行算术操作，以及在调用辅助函数时传递指针和 64 位值；另外，BPF 还支持 64 位原子操
作。

**每个 BPF 程序的最大指令数限制在 4096 条以内**，这意味着从设计上就可以保证**每
个程序都会很快结束**。虽然指令集中包含前向和后向跳转，但内核中的 BPF 校验器禁止
程序中有循环，因此可以永远保证程序会终止。因为 BPF 程序运行在内核，校验器的工作
是保证这些程序在运行时是安全的，不会影响到系统的稳定性。这意味着，从指令集的角度
来说循环是可以实现的，但校验器会对其施加限制。另外，BPF 中有尾调用的概念，允许一
个 BPF 程序调用另一个 BPF 程序。类似地，这种调用也是有限制的，目前上限是 32 层调
用；现在这个功能常用来对程序逻辑进行解耦，例如解耦成几个不同阶段。

### 1.1.3 BPF 指令格式

BPF 指令格式（instruction format）建模为两操作数指令（two operand instructions），
这种格式可以在 JIT 阶段将 BPF 指令**映射**（mapping）为原生指令。指令集是固定长
度的，这意味着每条指令都是 64 比特编码的。目前已经实现了 87 条指令，并且在需要时
可以对指令集进行进一步扩展。一条 64 位指令在大端机器上的编码格式如下，从重要性最
高比特（most significant bit，MSB）到重要性最低比特（least significant bit，LSB）：

```shell
op:8, dst_reg:4, src_reg:4, off:16, imm:32
```

`off` 和 `imm` 都是有符号类型。编码信息定义在内核头文件 `linux/bpf.h` 中，这个头
文件进一步 `include` 了 `linux/bpf_common.h`。

`op` 定了将要执行的操作。`op` 复用了大部分 cBPF 的编码定义。操作可以基于寄存器值
，也可以基于立即操作数（immediate operands）。`op` 自身的编码信息中包含了应该使
用的模式类型：

* `BPF_X` 指基于寄存器的操作数（register-based operations）
* `BPF_K` 指基于立即操作数（immediate-based operations）

对于后者，目的操作数永远是一个寄存器（destination operand is always a register）。
`dst_reg` 和 `src_reg` 都提供了寄存器操作数（register operands，例如
`r0` - `r9`）的额外信息。在某些指令中，`off` 用于表示一个相对偏移量（offset），
例如，对那些 BPF 可用的栈或缓冲区（例如 map values、packet data 等等）进行寻
址，或者跳转指令中用于跳转到目标。`imm` 存储一个常量/立即值。

所有的 `op` 指令可以分为若干类别。类别信息也编码到了 `op` 字段。`op` 字段分为（
从 MSB 到 LSB）：`code:4`, `source:1` 和 `class:3`。

* `class` 是指令类型
* `code` 指特定类型的指令中的某种特定操作码（operational code）
* `source` 可以告诉我们源操作数（source operand）是一个寄存器还是一个立即数

可能的指令类别包括：

* `BPF_LD`, `BPF_LDX`：这两种都是**加载操作**（load operations）。`BPF_LD` is
  used for loading a double word as a special instruction spanning two instructions
  due to the `imm:32` split, and for byte / half-word / word loads of packet data.
  The latter was carried over from cBPF mainly in order to keep cBPF to BPF
  translations efficient, since they have optimized JIT code. For native BPF
  these packet load instructions are less relevant nowadays. `BPF_LDX` class
  holds instructions for byte / half-word / word / double-word loads out of
  memory. Memory in this context is generic and could be stack memory, map value
  data, packet data, etc.

* `BPF_ST`, `BPF_STX`：这两种都是**存储操作**（store operations）。Similar to `BPF_LDX`
  the `BPF_STX` is the store counterpart and is used to store the data from a
  register into memory, which, again, can be stack memory, map value, packet data,
  etc. `BPF_STX` also holds special instructions for performing word and double-word
  based atomic add operations, which can be used for counters, for example. The
  `BPF_ST` class is similar to `BPF_STX` by providing instructions for storing
  data into memory only that the source operand is an immediate value.

* `BPF_ALU`, `BPF_ALU64`：这两种都是 **逻辑运算操作**（ALU operations）。Generally,
  `BPF_ALU` operations are in 32 bit mode and `BPF_ALU64` in 64 bit mode.
  Both ALU classes have basic operations with source operand which is register-based
  and an immediate-based counterpart. Supported by both are add (`+`), sub (`-`),
  and (`&`), or (`|`), left shift (`<<`), right shift (`>>`), xor (`^`),
  mul (`*`), div (`/`), mod (`%`), neg (`~`) operations. Also mov (`<X> := <Y>`)
  was added as a special ALU operation for both classes in both operand modes.
  `BPF_ALU64` also contains a signed right shift. `BPF_ALU` additionally
  contains endianness conversion instructions for half-word / word / double-word
  on a given source register.

* `BPF_JMP`：这种专用于**跳转操作**（jump operations）。Jumps can be unconditional
  and conditional. Unconditional jumps simply move the program counter forward, so
  that the next instruction to be executed relative to the current instruction is
  `off + 1`, where `off` is the constant offset encoded in the instruction. Since
  `off` is signed, the jump can also be performed backwards as long as it does not
  create a loop and is within program bounds. Conditional jumps operate on both,
  register-based and immediate-based source operands. If the condition in the jump
  operations results in `true`, then a relative jump to `off + 1` is performed,
  otherwise the next instruction (`0 + 1`) is performed. This fall-through
  jump logic differs compared to cBPF and allows for better branch prediction as it
  fits the CPU branch predictor logic more naturally. Available conditions are
  jeq (`==`), jne (`!=`), jgt (`>`), jge (`>=`), jsgt (signed `>`), jsge
  (signed `>=`), jlt (`<`), jle (`<=`), jslt (signed `<`), jsle (signed
  `<=`) and jset (jump if `DST & SRC`). Apart from that, there are three
  special jump operations within this class: the exit instruction which will leave
  the BPF program and return the current value in `r0` as a return code, the call
  instruction, which will issue a function call into one of the available BPF helper
  functions, and a hidden tail call instruction, which will jump into a different
  BPF program.

**Linux 内核中内置了一个 BPF 解释器**，这个解释器可以执行 BPF 指令组成的程序。即
使是 cBPF 程序，也可以在内核中透明地转换成 eBPF 程序，除非该架构仍然内置了 cBPF
JIT，还没有迁移到 eBPF JIT。

目前下列架构都内置了内核 eBPF JIT 编译器：`x86_64`、`arm64`、`ppc64`、`s390x`
、`mips64`、`sparc64` 和 `arm`。

所有的 BPF 操作，例如加载程序到内核，或者创建 BPF map，都是通过核心的 `bpf()` 系
统调用完成的。它还用于管理 map 表项（查找/更新/删除），以及通过 pinning（钉住
）将程序和 map 持久化到 BPF 文件系统。

<a name="bpf_helper"></a>

## 1.2 辅助函数

辅助函数（Helper functions）使得 BPF 能够通过一组内核定义的函数调用（function
call）来从内核中查询数据，或者将数据推送到内核。每种类型的 BPF 程序可以使用的辅
助函数可能不同，例如，与 attach 到 tc 层的 BPF 程序相比，attach 到 socket 的 BPF
程序只允许调用前者可以调用的辅助函数的一个子集。另外一个例子是，轻量级隧道（
lightweight tunneling ）使用的封装和解封装（Encapsulation and decapsulation）辅
助函数，只能被更低的 tc 层（lower tc layers）使用；而推送通知到用户态所使用的事
件输出辅助函数，既可以被 tc 程序使用也可以被 XDP 程序使用。

所有辅助函数的实现都共享一个通用的、和系统调用类似的函数签名。签名定义如下：

```c
u64 fn(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
```

前一节介绍的调用约定适用于所有的 BPF 辅助函数。

内核将辅助函数抽象成 `BPF_CALL_0()` 到 `BPF_CALL_5()` 几个宏，形式和相应类型的系
统调用类似。下面的例子是从某个辅助函数中抽取出来的，可以看到它通过调用相应 map
的回调函数完成更新 map 元素的操作：

```c
BPF_CALL_4(bpf_map_update_elem, struct bpf_map *, map, void *, key,
           void *, value, u64, flags)
{
    WARN_ON_ONCE(!rcu_read_lock_held());
    return map->ops->map_update_elem(map, key, value, flags);
}

const struct bpf_func_proto bpf_map_update_elem_proto = {
    .func           = bpf_map_update_elem,
    .gpl_only       = false,
    .ret_type       = RET_INTEGER,
    .arg1_type      = ARG_CONST_MAP_PTR,
    .arg2_type      = ARG_PTR_TO_MAP_KEY,
    .arg3_type      = ARG_PTR_TO_MAP_VALUE,
    .arg4_type      = ARG_ANYTHING,
};
```

这种方式有很多优点：虽然 cBPF 允许其加载指令（load instructions）进行超出范围的
访问（overload），以便从一个看似不可能的包偏移量（packet offset）获取数据以唤醒
多功能辅助函数，但每个 cBPF JIT 仍然需要为这个 cBPF 扩展实现对应的支持。而在
eBPF 中，JIT 编译器会以一种透明和高效的方式编译新加入的辅助函数，这意味着 JIT 编
译器只需要发射（emit）一条调用指令（call instruction），因为寄存器映射的方式使得
BPF 排列参数的方式（assignments）已经和底层架构的调用约定相匹配了。这使得基于辅
助函数扩展核心内核（core kernel）非常方便。**所有的 BPF 辅助函数都是核心内核的一
部分，无法通过内核模块（kernel module）来扩展或添加**。

前面提到的函数签名还允许校验器执行类型检测（type check）。上面的
`struct bpf_func_proto` 用于存放**校验器必需知道的所有关于该辅助函数的信息**，这
样校验器可以确保辅助函数期望的类型和 BPF 程序寄存器中的当前内容是匹配的。

参数类型范围很广，从任意类型的值，到限制只能为特定类型，例如 BPF 栈缓冲区（stack
buffer）的 `pointer/size` 参数对，辅助函数可以从这个位置读取数据或向其写入数据。
对于这种情况，校验器还可以执行额外的检查，例如，缓冲区是否已经初始化过了。

可用的 BPF 辅助函数很多，并且还在不断增加，例如，写作本文时，tc BPF 程序可以使用
38 种不同的 BPF 辅助函数。对于一个给定的 BPF 程序类型，内核的 `struct
bpf_verifier_ops` 包含了 `get_func_proto` 回调函数，这个函数提供了从某个特定的
`enum bpf_func_id` 到一个可用的辅助函数的映射。

<a name="bpf_maps"></a>

## 1.3 Maps

<p align="center"><img src="/assets/img/cilium-bpf-xdp-guide/bpf_map.png" width="60%" height="60%"></p>

map 是**驻留在内核空间**中的高效键值仓库（key/value store）。map 中的数据可以被
BPF 程序访问，如果想在 **多次 BPF 程序调用（invoke）之间保存状态**，可以将状态信
息放到 map。map 还可以**从用户空间通过文件描述符访问**，可以在任意 BPF 程序以及用
户空间应用之间共享。

共享 map 的 BPF 程序不要求是相同的程序类型，例如 tracing 程序可以和网络程序共享
map。**单个 BPF 程序目前最多可直接访问 64 个不同 map**。

**map 的实现由核心内核（core kernel）提供**。有 per-CPU 及 non-per-CPU 的通用
map，这些 map 可以读/写任意数据，也有一些和辅助函数一起使用的非通用 map。

当前可用的 **通用 map** 有：

* `BPF_MAP_TYPE_HASH`
* `BPF_MAP_TYPE_ARRAY`
* `BPF_MAP_TYPE_PERCPU_HASH`
* `BPF_MAP_TYPE_PERCPU_ARRAY` 
* `BPF_MAP_TYPE_LRU_HASH`
* `BPF_MAP_TYPE_LRU_PERCPU_HASH`
* `BPF_MAP_TYPE_LPM_TRIE`

以上 map 都使用相同的一组 BPF 辅助函数来执行查找、更新或删除操作，但各自实现了不
同的后端，这些后端各有不同的语义和性能特点。

当前内核中的 **非通用 map** 有：

* `BPF_MAP_TYPE_PROG_ARRAY`
* `BPF_MAP_TYPE_PERF_EVENT_ARRAY`
* `BPF_MAP_TYPE_CGROUP_ARRAY`
* `BPF_MAP_TYPE_STACK_TRACE`
* `BPF_MAP_TYPE_ARRAY_OF_MAPS`
* `BPF_MAP_TYPE_HASH_OF_MAPS`

例如，`BPF_MAP_TYPE_PROG_ARRAY` 是一个数组 map，用于持有（hold）其他的 BPF 程序
。`BPF_MAP_TYPE_ARRAY_OF_MAPS` 和 `BPF_MAP_TYPE_HASH_OF_MAPS` 都用于持有（hold）
其他 map 的指针，这样整个 map 就可以在运行时实现原子替换。这些类型的 map 都针对
特定的问题，不适合单单通过一个 BPF 辅助函数实现，因为它们需要在各次 BPF 程序调用
（invoke）之间时保持额外的（非数据）状态。

<a name="bpf_obj_pinning"></a>

## 1.4 Object Pinning（钉住对象）

<p align="center"><img src="/assets/img/cilium-bpf-xdp-guide/bpf_fs.png" width="60%" height="60%"></p>

**BPF map 和程序作为内核资源只能通过文件描述符访问，其背后是内核中的匿名
inode。**这带来了很多优点，但同时也有很多缺点：

优点包括：用户空间应用能够使用大部分文件描述符相关的 API，传递给 Unix socket 的文
件描述符是透明工作的等等。但同时，**文件描述符受限于进程的生命周期，使得 map
共享之类的操作非常笨重**。

因此，这给某些特定的场景带来了很多复杂性，例如 iproute2，其中的 tc 或 XDP 在准备
环境、加载程序到内核之后最终会退出。在这种情况下，从用户空间也无法访问这些 map
了，而本来这些 map 其实是很有用的，例如，在 data path 的 ingress 和 egress 位置共
享的 map（可以统计包数、字节数、PPS 等信息）。另外，第三方应用可能希望在 BPF 程
序运行时监控或更新 map。

**为了解决这个问题，内核实现了一个最小内核空间 BPF 文件系统，BPF map 和 BPF 程序
都可以钉到（pin）这个文件系统内**，这个过程称为 object pinning（钉住对象）。相应
地，BPF 系统调用进行了扩展，添加了两个新命令，分别用于钉住（`BPF_OBJ_PIN`）一个
对象和获取（`BPF_OBJ_GET`）一个被钉住的对象（pinned objects）。

例如，tc 之类的工具可以利用这个基础设施在 ingress 和 egress 之间共享 map。BPF
相关的文件系统**不是单例模式**（singleton），它支持多挂载实例、硬链接、软连接等
等。

<a name="bpf_tail_call"></a>

## 1.5 尾调用（Tail Calls）

<p align="center"><img src="/assets/img/cilium-bpf-xdp-guide/bpf_tailcall.png" width="60%" height="60%"></p>

BPF 相关的另一个概念是尾调用（tail calls）。尾调用的机制是：一个 BPF 程序可以调
用另一个 BPF 程序，并且调用完成后不用返回到原来的程序。和普通函数调用相比，这种
调用方式开销最小，因为它是**用长跳转（long jump）实现的，复用了原来的栈帧**
（stack frame）。

BPF 程序都是独立验证的，因此要传递状态，要么使用 per-CPU map 作为 scratch 缓冲区
，要么如果是 tc 程序的话，还可以使用 `skb` 的某些字段（例如 `cb[]`）。

**相同类型的程序才可以尾调用**，而且它们还要与 JIT 编译器相匹配，因此要么是 JIT
编译执行，要么是解释器执行（invoke interpreted programs），但不能同时使用两种方
式。

尾调用执行涉及**两个步骤**：

1. 设置一个称为“程序数组”（program array）的特殊 map（`BPF_MAP_TYPE_PROG_ARRAY`
   ），这个 map 可以从用户空间通过 key/value 操作
1. 调用辅助函数 `bpf_tail_call()`。两个参数：一个对程序数组的引用（a reference
   to the program array），一个查询 map 所用的 key。内核将这个辅助函数调用内联（
   inline）到一个特殊的 BPF 指令内。目前，这样的程序数组在用户空间侧是只写模式（
   write-only from user space side）。

内核根据传入的文件描述符查找相关的 BPF 程序，自动替换给定的 map slot（槽） 处的
程序指针。如果没有找到给定的 key 对应的 value，内核会跳过（fall through）这一步
，继续执行 `bpf_tail_call()` 后面的指令。**尾调用是一个强大的功能，例如，可以通
过尾调用结构化地解析网络头**（network headers）。还可以在运行时（runtime）原子地
添加或替换功能，即，动态地改变 BPF 程序的执行行为。

<a name="bpf_to_bpf_call"></a>

## 1.6 BPF to BPF Calls

<p align="center"><img src="/assets/img/cilium-bpf-xdp-guide/bpf_call.png" width="45%" height="45%"></p>

除了 BPF 辅助函数和 BPF 尾调用之外，BPF 核心基础设施最近刚加入了一个新特性：BPF
到 BPF 调用（BPF to BPF calls）。**在这个特性引入内核之前，典型的 BPF C 程序必须
将所有需要复用的代码进行特殊处理，例如，在头文件中声明为 `always_inline`**。当
LLVM 编译和生成 BPF 对象文件时，所有这些函数将被内联，因此会在生成的对象文件中重
复多次，导致代码尺寸膨胀：

```c
#include <linux/bpf.h>

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

#ifndef __inline
# define __inline                         \
   inline __attribute__((always_inline))
#endif

static __inline int foo(void)
{
    return XDP_DROP;
}

__section("prog")
int xdp_drop(struct xdp_md *ctx)
{
    return foo();
}

char __license[] __section("license") = "GPL";
```

之所以要这样做是因为 **BPF 程序的加载器、校验器、解释器和 JIT 中都缺少对函数调用的
支持**。从 `Linux 4.16` 和 `LLVM 6.0` 开始，这个限制得到了解决，BPF 程序不再需
要到处使用 `always_inline` 声明了。因此，上面的代码可以更自然地重写为：

```c
#include <linux/bpf.h>

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

static int foo(void)
{
    return XDP_DROP;
}

__section("prog")
int xdp_drop(struct xdp_md *ctx)
{
    return foo();
}

char __license[] __section("license") = "GPL";
```

BPF 到 BPF 调用是一个重要的性能优化，极大减小了生成的 BPF 代码大小，因此**对 CPU
指令缓存（instruction cache，i-cache）更友好**。

BPF 辅助函数的调用约定也适用于 BPF 函数间调用，即 `r1` - `r5` 用于传递参数，返回
结果放到 `r0`。`r1` - `r5` 是 scratch registers，`r6` - `r9` 像往常一样是保留寄
存器。最大嵌套调用深度是 `8`。调用方可以传递指针（例如，指向调用方的栈帧的指针）
给被调用方，但反过来不行。

**当前，BPF 函数间调用和 BPF 尾调用是不兼容的**，因为后者需要复用当前的栈设置（
stack setup），而前者会增加一个额外的栈帧，因此不符合尾调用期望的布局。

BPF JIT 编译器为每个函数体发射独立的镜像（emit separate images for each function
body），稍后在最后一通 JIT 处理（final JIT pass）中再修改镜像中函数调用的地址
。已经证明，这种方式需要对各种 JIT 做最少的修改，因为在实现中它们可以将 BPF 函数
间调用当做常规的 BPF 辅助函数调用。

<a name="bpf_jit"></a>

## 1.7 JIT

<p align="center"><img src="/assets/img/cilium-bpf-xdp-guide/bpf_jit.png" width="60%" height="60%"></p>

64 位的 `x86_64`、`arm64`、`ppc64`、`s390x`、`mips64`、`sparc64` 和 32 位的 `arm`
、`x86_32` 架构都内置了 in-kernel eBPF JIT 编译器，它们的功能都是一样的，可
以用如下方式打开：

```shell
$ echo 1 > /proc/sys/net/core/bpf_jit_enable
```

32 位的 `mips`、`ppc` 和 `sparc` 架构目前内置的是一个 cBPF JIT 编译器。这些只有
cBPF JIT 编译器的架构，以及那些甚至完全没有 BPF JIT 编译器的架构，需要通过**内核
中的解释器**（in-kernel interpreter）执行 eBPF 程序。

要判断哪些平台支持 eBPF JIT，可以在内核源文件中 grep `HAVE_EBPF_JIT`：

```shell
$ git grep HAVE_EBPF_JIT arch/
arch/arm/Kconfig:       select HAVE_EBPF_JIT   if !CPU_ENDIAN_BE32
arch/arm64/Kconfig:     select HAVE_EBPF_JIT
arch/powerpc/Kconfig:   select HAVE_EBPF_JIT   if PPC64
arch/mips/Kconfig:      select HAVE_EBPF_JIT   if (64BIT && !CPU_MICROMIPS)
arch/s390/Kconfig:      select HAVE_EBPF_JIT   if PACK_STACK && HAVE_MARCH_Z196_FEATURES
arch/sparc/Kconfig:     select HAVE_EBPF_JIT   if SPARC64
arch/x86/Kconfig:       select HAVE_EBPF_JIT   if X86_64
```

JIT 编译器可以极大加速 BPF 程序的执行，因为与解释器相比，它们可以降低每个指令的
开销（reduce the per instruction cost）。通常，指令可以 1:1 映射到底层架构的原生
指令。另外，这也会减少生成的可执行镜像的大小，因此对 CPU 的指令缓存更友好。特别
地，对于 CISC 指令集（例如 `x86`），JIT 做了很多特殊优化，目的是为给定的指令产生
可能的最短操作码（emitting the shortest possible opcodes），以降低程序翻译过程所
需的空间。

<a name="bpf_hardening"></a>

## 1.8 加固（Hardening）

为了避免代码被损坏，BPF 会在程序的生命周期内，在内核中将 BPF 解释器**解释后的整
个镜像**（`struct bpf_prog`）和 **JIT 编译之后的镜像**（`struct
bpf_binary_header`）锁定为只读的（read-only）。在这些位置发生的任何数据损坏（例
如由于某些内核 bug 导致的）会触发通用的保护机制，因此会造成内核崩溃（crash）而不
是允许损坏静默地发生。

查看哪些平台支持将镜像内存（image memory）设置为只读的，可以通过下面的搜索：

```shell
$ git grep ARCH_HAS_SET_MEMORY | grep select
arch/arm/Kconfig:    select ARCH_HAS_SET_MEMORY
arch/arm64/Kconfig:  select ARCH_HAS_SET_MEMORY
arch/s390/Kconfig:   select ARCH_HAS_SET_MEMORY
arch/x86/Kconfig:    select ARCH_HAS_SET_MEMORY
```

`CONFIG_ARCH_HAS_SET_MEMORY` 选项是不可配置的，因此平台要么内置支持，要么不支持
。那些目前还不支持的架构未来可能也会支持。

对于 `x86_64` JIT 编译器，如果设置了 `CONFIG_RETPOLINE`，尾调用的间接跳转（
indirect jump）就会用 `retpoline` 实现。写作本文时，在大部分现代 Linux 发行版上
这个配置都是打开的。

将 `/proc/sys/net/core/bpf_jit_harden` 设置为 `1` 会为非特权用户（
unprivileged users）的 JIT 编译做一些额外的加固工作。这些额外加固会稍微降低程序
的性能，但在有非受信用户在系统上进行操作的情况下，能够有效地减小（潜在的）受攻击
面。但与完全切换到解释器相比，这些性能损失还是比较小的。

当前，启用加固会在 JIT 编译时**盲化**（blind）BPF 程序中用户提供的所有 32 位和
64 位常量，以防御 **JIT spraying（喷射）攻击**，这些攻击会将原生操作码（native
opcodes）作为立即数（immediate values）注入到内核。这种攻击有效是因为：**立即数
驻留在可执行内核内存（executable kernel memory）中**，因此某些内核 bug 可能会触
发一个跳转动作，如果跳转到立即数的开始位置，就会把它们当做原生指令开始执行。

盲化 JIT 常量通过对真实指令进行随机化（randomizing the actual instruction）实现
。在这种方式中，通过对指令进行重写（rewriting the instruction），将原来**基于立
即数的操作**转换成**基于寄存器的操作**。指令重写将加载值的过程分解为两部分：

1. 加载一个盲化后的（blinded）立即数 `rnd ^ imm` 到寄存器
1. 将寄存器和 `rnd` 进行异或操作（xor）

这样原始的 `imm` 立即数就驻留在寄存器中，可以用于真实的操作了。这里介绍的只是加
载操作的盲化过程，实际上所有的通用操作都被盲化了。

下面是加固关闭的情况下，某个程序的 JIT 编译结果：

```shell
$ echo 0 > /proc/sys/net/core/bpf_jit_harden

  ffffffffa034f5e9 + <x>:
  [...]
  39:   mov    $0xa8909090,%eax
  3e:   mov    $0xa8909090,%eax
  43:   mov    $0xa8ff3148,%eax
  48:   mov    $0xa89081b4,%eax
  4d:   mov    $0xa8900bb0,%eax
  52:   mov    $0xa810e0c1,%eax
  57:   mov    $0xa8908eb4,%eax
  5c:   mov    $0xa89020b0,%eax
  [...]
```

加固打开之后，以上程序被某个非特权用户通过 BPF 加载的结果（这里已经进行了常
量盲化）：

```shell
$ echo 1 > /proc/sys/net/core/bpf_jit_harden

  ffffffffa034f1e5 + <x>:
  [...]
  39:   mov    $0xe1192563,%r10d
  3f:   xor    $0x4989b5f3,%r10d
  46:   mov    %r10d,%eax
  49:   mov    $0xb8296d93,%r10d
  4f:   xor    $0x10b9fd03,%r10d
  56:   mov    %r10d,%eax
  59:   mov    $0x8c381146,%r10d
  5f:   xor    $0x24c7200e,%r10d
  66:   mov    %r10d,%eax
  69:   mov    $0xeb2a830e,%r10d
  6f:   xor    $0x43ba02ba,%r10d
  76:   mov    %r10d,%eax
  79:   mov    $0xd9730af,%r10d
  7f:   xor    $0xa5073b1f,%r10d
  86:   mov    %r10d,%eax
  89:   mov    $0x9a45662b,%r10d
  8f:   xor    $0x325586ea,%r10d
  96:   mov    %r10d,%eax
  [...]
```

两个程序在语义上是一样的，但在第二种方式中，原来的立即数在反汇编之后的程序中不再
可见。

同时，加固还会禁止任何 JIT 内核符合（kallsyms）暴露给特权用户，JIT 镜像地址不再
出现在 `/proc/kallsyms` 中。

另外，Linux 内核提供了 `CONFIG_BPF_JIT_ALWAYS_ON` 选项，打开这个开关后 BPF 解释
器将会从内核中完全移除，永远启用 JIT 编译器。此功能部分是为防御 Spectre v2 
攻击开发的，如果应用在一个基于虚拟机的环境，客户机内核（guest kernel）将不会复用
内核的 BPF 解释器，因此可以避免某些相关的攻击。如果是基于容器的环境，这个配置是
可选的，如果 JIT 功能打开了，解释器仍然可能会在编译时被去掉，以降低内核的复杂度
。因此，对于主流架构（例如 `x86_64` 和 `arm64`）上的 JIT 通常都建议打开这个开关
。

另外，内核提供了一个配置项 `/proc/sys/kernel/unprivileged_bpf_disabled` 来禁止非
特权用户使用 `bpf(2)` 系统调用，可以通过 `sysctl` 命令修改。
比较特殊的一点是，这个配置项特意设计为**“一次性开关”**（one-time kill switch），
这意味着一旦将它设为 `1`，就没有办法再改为 `0` 了，除非重启内核。一旦设置为 `1`
之后，只有初始命名空间中有 `CAP_SYS_ADMIN` 特权的进程才可以调用 `bpf(2)` 系统调用
。 Cilium 启动后也会将这个配置项设为 1：

```shell
$ echo 1 > /proc/sys/kernel/unprivileged_bpf_disabled
```

<a name="bpf_offloads"></a>

## 1.9 Offloads

<p align="center"><img src="/assets/img/cilium-bpf-xdp-guide/bpf_offload.png" width="60%" height="60%"></p>

BPF 网络程序，尤其是 tc 和 XDP BPF 程序在内核中都有一个 offload 到硬件的接口，这
样就可以直接在网卡上执行 BPF 程序。

当前，Netronome 公司的 `nfp` 驱动支持通过 JIT 编译器 offload BPF，它会将 BPF 指令
翻译成网卡实现的指令集。另外，它还支持将 BPF maps offload 到网卡，因此 offloaded
BPF 程序可以执行 map 查找、更新和删除操作。

<a name="toolchain"></a>

# 2 工具链

本节介绍 BPF 相关的用户态工具、内省设施（introspection facilities）和内核控制选项。
注意，围绕 BPF 的工具和基础设施还在快速发展当中，因此本文提供的内容可能只覆
盖了其中一部分。

<a name="tool_dev_env"></a>

## 2.1 开发环境

#### Fedora

Fedora `25+`：

```shell
$ sudo dnf install -y git gcc ncurses-devel elfutils-libelf-devel bc \
  openssl-devel libcap-devel clang llvm graphviz bison flex glibc-static
```

#### Ubuntu

Ubuntu `17.04+`：

```shell
$ sudo apt-get install -y make gcc libssl-dev bc libelf-dev libcap-dev \
  clang gcc-multilib llvm libncurses5-dev git pkg-config libmnl-dev bison flex \
  graphviz
```

#### openSUSE Tumbleweed

openSUSE Tumbleweed 和 openSUSE Leap `15.0+`：

```shell
$ sudo zypper install -y git gcc ncurses-devel libelf-devel bc libopenssl-devel \
       libcap-devel clang llvm graphviz bison flex glibc-devel-static
```

<a name="tool_llvm"></a>

## 2.2 LLVM

写作本文时，LLVM 是唯一提供 BPF 后端的编译器套件。gcc 目前还不支持。

主流的发行版在对 LLVM 打包的时候就默认启用了 BPF 后端，因此，在大部分发行版上安
装 clang 和 llvm 就可以将 C 代码编译为 BPF 对象文件了。

典型的工作流是：

1. 用 C 编写 BPF 程序
1. 用 LLVM 将 C 程序编译成对象文件（ELF）
1. 用户空间 BPF ELF 加载器（例如 iproute2）解析对象文件
1. 加载器通过 `bpf()` 系统调用将解析后的对象文件注入内核
1. 内核验证 BPF 指令，然后对其执行即时编译（JIT），返回程序的一个新文件描述符
1. 利用文件描述符 attach 到内核子系统（例如网络子系统）

某些子系统还支持将 BPF 程序 offload 到硬件（例如网卡）。

<a name="ch_2.2.1"></a>

### 2.2.1 BPF Target（目标平台）

查看 LLVM 支持的 BPF target：

```shell
$ llc --version
LLVM (http://llvm.org/):
LLVM version 3.8.1
Optimized build.
Default target: x86_64-unknown-linux-gnu
Host CPU: skylake

Registered Targets:
  [...]
  bpf        - BPF (host endian)
  bpfeb      - BPF (big endian)
  bpfel      - BPF (little endian)
  [...]
```

**默认情况下，`bpf` target 使用编译时所在的 CPU 的大小端格式**，即，如果 CPU 是小
端，BPF 程序就会用小端表示；如果 CPU 是大端，BPF 程序就是大端。这也和 BPF 的运
行时行为相匹配，这样的行为比较通用，而且大小端格式一致可以避免一些因为格式导致的
架构劣势。

BPF 程序可以在大端节点上编译，在小端节点上运行，或者相反，因此对于**交叉编译**，
引入了两个新目标 `bpfeb` 和 `bpfel`。注意前端也需要以相应的大小端方式运行。

在不存在大小端混用的场景下，建议使用 `bpf` target。例如，在 `x86_64` 平台上（小端
），指定 `bpf` 和 `bpfel` 会产生相同的结果，因此触发编译的脚本不需要感知到大小端
。

下面是**一个最小的完整 XDP 程序**，实现丢弃包的功能（`xdp-example.c`）：

```c
#include <linux/bpf.h>

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

__section("prog")
int xdp_drop(struct xdp_md *ctx)
{
    return XDP_DROP;
}

char __license[] __section("license") = "GPL";
```

用下面的命令编译并加载到内核：

```shell
$ clang -O2 -Wall -target bpf -c xdp-example.c -o xdp-example.o
$ ip link set dev em1 xdp obj xdp-example.o
```

> 以上命令将一个 XDP 程序 attach 到一个网络设备，这需要 Linux 4.11 内核中支持
> XDP 的设备，或者 4.12+ 版本的内核。

LLVM（>= 3.9） 使用**正式的 BPF 机器值**（machine value），即 `EM_BPF`（十进制 `247`
，十六进制 `0xf7`），来**生成对象文件**。在这个例子中，程序是用 `bpf` target 在
`x86_64` 平台上编译的，因此下面显示的大小端标识是 `LSB` (和 `MSB` 相反)：

```shell
$ file xdp-example.o
xdp-example.o: ELF 64-bit LSB relocatable, *unknown arch 0xf7* version 1 (SYSV), not stripped
```

**`readelf -a xdp-example.o` 能够打印 ELF 文件的更详细信息**，有时在检查生成的
section header、relocation entries 和符号表时会比较有用。

<a name="ch_2.2.2"></a>

### 2.2.2 调试信息（DWARF、BTF）

若是要 debug，clang 可以生成下面这样的汇编器输出：

```shell
$ clang -O2 -S -Wall -target bpf -c xdp-example.c -o xdp-example.S
$ cat xdp-example.S
    .text
    .section    prog,"ax",@progbits
    .globl      xdp_drop
    .p2align    3
xdp_drop:                             # @xdp_drop
# BB#0:
    r0 = 1
    exit

    .section    license,"aw",@progbits
    .globl    __license               # @__license
__license:
    .asciz    "GPL"
```

LLVM 从 6.0 开始，还包括了汇编解析器（assembler parser）的支持。你可以**直接使用
BPF 汇编指令编程**，然后使用 llvm-mc 将其汇编成一个目标文件。例如，你可以将前面
的 `xdp-example.S` 重新变回对象文件：

```
$ llvm-mc -triple bpf -filetype=obj -o xdp-example.o xdp-example.S
```

#### DWARF 格式和 `llvm-objdump`

另外，较新版本（>= 4.0）的 LLVM 还可以将调试信息以 dwarf 格式存储到对象文件中。
只要在编译时加上 `-g`：

```shell
$ clang -O2 -g -Wall -target bpf -c xdp-example.c -o xdp-example.o
$ llvm-objdump -S -no-show-raw-insn xdp-example.o

xdp-example.o:        file format ELF64-BPF

Disassembly of section prog:
xdp_drop:
; {
    0:        r0 = 1
; return XDP_DROP;
    1:        exit
```

`llvm-objdump` 工具可以用编译的 C 源码对汇编输出添加注解（annotate ）。这里的例
子过于简单，没有几行 C 代码；但注意上面的 `0` 和 `1` 行号，它们直接对应到内核的校
验器日志（见下面的输出）。这意味着假如 BPF 程序被校验器拒绝了，`llvm-objdump` 可
以帮助你将 BPF 指令关联到原始的 C 代码，对于分析来说非常有用。

```shell
$ ip link set dev em1 xdp obj xdp-example.o verb

Prog section 'prog' loaded (5)!
 - Type:         6
 - Instructions: 2 (0 over limit)
 - License:      GPL

Verifier analysis:

0: (b7) r0 = 1
1: (95) exit
processed 2 insns
```

从上面的校验器分析可以看出，`llvm-objdump` 的输出和内核中的 BPF 汇编是相同的。

去掉 `-no-show-raw-insn` 选项还可以以十六进制格式在每行汇编代码前面打印原始的
`struct bpf_insn`：

```shell
$ llvm-objdump -S xdp-example.o

xdp-example.o:        file format ELF64-BPF

Disassembly of section prog:
xdp_drop:
; {
   0:       b7 00 00 00 01 00 00 00     r0 = 1
; return foo();
   1:       95 00 00 00 00 00 00 00     exit
```

#### LLVM IR

对于 LLVM IR 调试，BPF 的编译过程可以分为两个步骤：首先生成一个二进制 LLVM IR 临
时文件 `xdp-example.bc`，然后将其传递给 `llc`：

```shell
$ clang -O2 -Wall -target bpf -emit-llvm -c xdp-example.c -o xdp-example.bc
$ llc xdp-example.bc -march=bpf -filetype=obj -o xdp-example.o
```

生成的 LLVM IR 还可以 dump 成人类可读的格式：

```shell
$ clang -O2 -Wall -emit-llvm -S -c xdp-example.c -o -
```

#### BTF

LLVM 能将调试信息（例如对程序使用的数据的描述）attach 到 BPF 对象文件。默认情况
下使用 DWARF 格式。

BPF 使用了一个高度简化的版本，称为 **BTF** (BPF Type Format)。生成的 DWARF 可以
转换成 BTF 格式，然后通过 BPF 对象加载器加载到内核。内核验证 BTF 数据的正确性，
并跟踪 BTF 数据中包含的数据类型。

这样的话，就可以用键和值对 BPF map 打一些注解（annotation）存储到 BTF 数据中，这
样下次 dump map 时，除了 map 内的数据外还会打印出相关的类型信息。这对内省（
introspection）、调试和格式良好的打印都很有帮助。注意，BTF 是一种通用的调试数据
格式，因此任何从 DWARF 转换成的 BTF 数据都可以被加载（例如，内核 vmlinux DWARF 数
据可以转换成 BTF 然后加载）。后者对于未来 BPF 的跟踪尤其有用。

将 DWARF 格式的调试信息转换成 BTF 格式需要用到 `elfutils` (>= 0.173) 工具。
如果没有这个工具，那需要在 `llc` 编译时打开 `-mattr=dwarfris` 选项：

```shell
$ llc -march=bpf -mattr=help |& grep dwarfris
dwarfris - Disable MCAsmInfo DwarfUsesRelocationsAcrossSections.
[...]
```

使用 `-mattr=dwarfris` 是因为 `dwarfris` (`dwarf relocation in section`) 选项禁
用了 DWARF 和 ELF 的符号表之间的 DWARF cross-section 重定位，因为 libdw 不支持
BPF 重定位。不打开这个选项的话，`pahole` 这类工具将无法正确地从对象中 dump 结构。

`elfutils` (>= 0.173) 实现了合适的 BPF 重定位，因此没有打开 `-mattr=dwarfris` 选
项也能正常工作。它可以从对象文件中的 DWARF 或 BTF 信息 dump 结构。目前 `pahole`
使用 LLVM 生成的 DWARF 信息，但未来它可能会使用 BTF 信息。

#### `pahole`

将 DWARF 转换成 BTF 格式需要使用较新的 `pahole` 版本（>= 1.12），然后指定 `-J` 选项。
检查所用的 `pahole` 版本是否支持 BTF（注意，`pahole` 会用到 `llvm-objcopy`，因此
也要检查后者是否已安装）：

```
$ pahole --help | grep BTF
-J, --btf_encode           Encode as BTF
```

生成调试信息还需要前端的支持，在 `clang` 编译时指定 `-g` 选项，生成源码级别的调
试信息。注意，不管 `llc` 是否指定了 `dwarfris` 选项，`-g` 都是需要指定的。生成目
标文件的完整示例：

```
$ clang -O2 -g -Wall -target bpf -emit-llvm -c xdp-example.c -o xdp-example.bc
$ llc xdp-example.bc -march=bpf -mattr=dwarfris -filetype=obj -o xdp-example.o
```

或者，只使用 clang 这一个工具来编译带调试信息的 BPF 程序（同样，如果有合适的
elfutils 版本，`dwarfris` 选项可以省略）：

```
$ clang -target bpf -O2 -g -c -Xclang -target-feature -Xclang +dwarfris -c xdp-example.c -o xdp-example.o
```

基于 DWARF 信息 dump BPF 程序的数据结构：

```
$ pahole xdp-example.o
struct xdp_md {
        __u32                      data;                 /*     0     4 */
        __u32                      data_end;             /*     4     4 */
        __u32                      data_meta;            /*     8     4 */

        /* size: 12, cachelines: 1, members: 3 */
        /* last cacheline: 12 bytes */
};
```

在对象文件中，DWARF 数据将仍然伴随着新加入的 BTF 数据一起保留。完整的 `clang` 和
`pahole` 示例：

```
$ clang -target bpf -O2 -Wall -g -c -Xclang -target-feature -Xclang +dwarfris -c xdp-example.c -o xdp-example.o
$ pahole -J xdp-example.o
```

#### `readelf`

通过 `readelf` 工具可以看到多了一个 `.BTF` section：

```shell
$ readelf -a xdp-example.o
[...]
  [18] .BTF              PROGBITS         0000000000000000  00000671
[...]
```

BPF 加载器（例如 iproute2）会检测和加载 BTF section，因此给 BPF map 注释（
annotate）类型信息。

<a name="ch_2.2.3"></a>

### 2.2.3 BPF 指令集

LLVM 默认用 BPF 基础指令集（base instruction set）来生成代码，以确保这些生成的对
象文件也能够被稍老的 LTS 内核（例如 4.9+）加载。

但是，LLVM 提供了一个 BPF 后端选项 `-mcpu`，可以指定不同版本的 BPF 指令集，即
BPF 基础指令集之上的指令集扩展（instruction set extensions），以生成更高效和体积
更小的代码。

可用的 `-mcpu` 类型：

```shell
$ llc -march bpf -mcpu=help
Available CPUs for this target:

  generic - Select the generic processor.
  probe   - Select the probe processor.
  v1      - Select the v1 processor.
  v2      - Select the v2 processor.
[...]
```

* `generic` processor 是默认的 processor，也是 BPF `v1` 基础指令集。
* `v1` 和 `v2` processor 通常在交叉编译 BPF 的环境下比较有用，即编译 BPF 的平台
  和最终执行 BPF 的平台不同（因此 BPF 内核特性可能也会不同）。

**推荐使用 `-mcpu=probe` ，这也是 Cilium 内部在使用的类型**。使用这种类型时，
LLVM BPF 后端会向内核询问可用的 BPF 指令集扩展，如果找到可用的，就会使用相应的指
令集来编译 BPF 程序。

使用 `llc` 和 `-mcpu=probe` 的完整示例：

```shell
$ clang -O2 -Wall -target bpf -emit-llvm -c xdp-example.c -o xdp-example.bc
$ llc xdp-example.bc -march=bpf -mcpu=probe -filetype=obj -o xdp-example.o
```

<a name="ch_2.2.4"></a>

### 2.2.4 指令和寄存器位宽（64/32 位）

通常来说，LLVM IR 生成是架构无关的。但使用 `clang` 编译时是否指定 `-target bpf`
是有几点小区别的，取决于不同的平台架构（`x86_64`、`arm64` 或其他），`-target` 的
默认配置可能不同。

引用内核文档 `Documentation/bpf/bpf_devel_QA.txt`：

* BPF 程序可以嵌套 include 头文件，只要头文件中都是文件作用域的内联汇编代码（
  file scope inline assembly codes）。大部分情况下默认 target 都可以处理这种情况，
  但如果 BPF 后端汇编器无法理解这些汇编代码，那 `bpf` target 会失败。

* 如果编译时没有指定 `-g`，那额外的 elf sections（例如 `.eh_frame`
  和 `.rela.eh_frame`）可能会以默认 target 格式出现在对象文件中，但不会是 `bpf`
  target。

* 默认 target 可能会将一个 C `switch` 声明转换为一个 `switch` 表的查找和跳转操作。
  由于 switch 表位于全局的只读 section，因此 BPF 程序的加载会失败。 `bpf` target
  不支持 switch 表优化。clang 的 `-fno-jump-tables` 选项可以禁止生成 switch 表。

* 如果 clang 指定了 `-target bpf`，那指针或 `long`/`unsigned long` 类型将永远
  是 64 位的，不管底层的 clang 可执行文件或默认的 target（或内核）是否是 32
  位。但如果使用的是 native clang target，那 clang 就会根据底层的架构约定（
  architecture's conventions）来编译这些类型，这意味着对于 32 位的架构，BPF 上下
  文中的指针或 `long`/`unsigned long` 类型会是 32 位的，但此时的 BPF LLVM 后端仍
  然工作在 64 位模式。

`native` target 主要用于跟踪（tracing）内核中的 `struct pt_regs`，这个结构体对
CPU 寄存器进行映射，或者是跟踪其他一些能感知 CPU 寄存器位宽（CPU's register
width）的内核结构体。除此之外的其他场景，例如网络场景，都建议使用 `clang -target
bpf`。

另外，LLVM 从 7.0 开始支持 32 位子寄存器和 BPF ALU32 指令。另外，新加入了一个代
码生成属性 `alu32`。当指定这个参数时，LLVM 会尝试尽可能地使用 32 位子寄存器，例
如当涉及到 32 位操作时。32 位子寄存器及相应的 ALU 指令组成了 ALU32 指令。例如，
对于下面的示例代码：

```shell
$ cat 32-bit-example.c
void cal(unsigned int *a, unsigned int *b, unsigned int *c)
{
  unsigned int sum = *a + *b;
  *c = sum;
}
```

使用默认的代码生成选项，产生的汇编代码如下：

```
$ clang -target bpf -emit-llvm -S 32-bit-example.c
$ llc -march=bpf 32-bit-example.ll
$ cat 32-bit-example.s
cal:
  r1 = *(u32 *)(r1 + 0)
  r2 = *(u32 *)(r2 + 0)
  r2 += r1
  *(u32 *)(r3 + 0) = r2
  exit
```

可以看到默认使用的是 `r` 系列寄存器，这些都是 64 位寄存器，这意味着其中的加法都
是 64 位加法。现在，如果指定 `-mattr=+alu32` 强制要求使用 32 位，生成的汇编代码
如下：

```
$ llc -march=bpf -mattr=+alu32 32-bit-example.ll
$ cat 32-bit-example.s
cal:
  w1 = *(u32 *)(r1 + 0)
  w2 = *(u32 *)(r2 + 0)
  w2 += w1
  *(u32 *)(r3 + 0) = w2
  exit
```

可以看到这次使用的是 `w` 系列寄存器，这些是 32 位子寄存器。

使用 32 位子寄存器可能会减小（最终生成的代码中）**类型扩展指令**（type extension
instruction）的数量。另外，它对 32 位架构的内核 eBPF JIT 编译器也有所帮助，因为
原来这些编译器都是用 32 位模拟 64 位 eBPF 寄存器，其中使用了很多 32 位指令来操作
高 32 bit。即使写 32 位子寄存器的操作仍然需要对高 32 位清零，但只要确保从 32 位
子寄存器的读操作只会读取低 32 位，那只要 JIT 编译器已经知道某个寄存器的定义只有
子寄存器读操作，那对高 32 位的操作指令就可以避免。

<a name="ch_2.2.5"></a>

### 2.2.5 C BPF 代码注意事项

用 C 语言编写 BPF 程序不同于用 C 语言做应用开发，有一些陷阱需要注意。本节列出了
二者的一些不同之处。

#### 1. 所有函数都需要内联（inlined）、没有函数调用（对于老版本 LLVM）或共享库调用

BPF 不支持共享库（Shared libraries）。但是，可以将常规的库代码（library code）放
到头文件中，然后在主程序中 include 这些头文件，例如 Cilium 就大量使用了这种方式
（可以查看 `bpf/lib/` 文件夹）。另外，也可以 include 其他的一些头文件，例如内核
或其他库中的头文件，复用其中的静态内联函数（static inline functions）或宏/定义（
macros / definitions）。

内核 4.16+ 和 LLVM 6.0+ 之后已经支持 BPF-to-BPF 函数调用。对于任意跟定的程序片段
，在此之前的版本只能将全部代码编译和内联成一个扁平的 BPF 指令序列（a flat
sequence of BPF instructions）。在这种情况下，最佳实践就是为每个库函数都使用一个
像 `__inline` 一样的注解（annotation ），下面的例子中会看到。推荐使用
`always_inline`，因为编译器可能会对只注解为 `inline` 的长函数仍然做 uninline 操
作。

如果是后者，LLVM 会在 ELF 文件中生成一个**重定位项**（relocation entry），BPF
ELF 加载器（例如 iproute2）无法解析这个重定位项，因此会产生一条错误，因为对加载器
来说只有 BPF maps 是合法的、能够处理的重定位项。

```c
#include <linux/bpf.h>

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

#ifndef __inline
# define __inline                         \
   inline __attribute__((always_inline))
#endif

static __inline int foo(void)
{
    return XDP_DROP;
}

__section("prog")
int xdp_drop(struct xdp_md *ctx)
{
    return foo();
}

char __license[] __section("license") = "GPL";
```

#### 2. 多个程序可以放在同一 C 文件中的不同 section

**BPF C 程序大量使用 section annotations**。一个 C 文件典型情况下会分为 3 个或更
多个 section。BPF ELF 加载器利用这些名字来提取和准备相关的信息，以通过 `bpf()`系
统调用加载程序和 maps。例如，查找创建 map 所需的元数据和 BPF 程序的 license 信息
时，iproute2 会分别使用 `maps` 和 `license` 作为默认的 section 名字。注意在程序
创建时 `license` section 也会加载到内核，如果程序使用的是兼容 GPL 的协议，这些信
息就可以启用那些 GPL-only 的辅助函数，例如 `bpf_ktime_get_ns()` 和
`bpf_probe_read()` 。

其余的 section 名字都是和特定的 BPF 程序代码相关的，例如，下面经过修改之后的代码
包含两个程序 section：`ingress` 和 `egress`。这个非常简单的示例展示了不同 section
（这里是 `ingress` 和 `egress`）之间可以共享 BPF map 和常规的静态内联辅助函数（
例如 `account_data()`）。

##### 示例程序

原来的 `xdp-example.c` 已经修改为 `tc-example.c`，后者可以被 tc 加载，attach 到
一个 netdevice 的 ingress 或 egress hook。这个程序对传输的字节进行计数，存储在一
个名为 `acc_map` 的 BPF map 中，这个 map 有两个槽（slot），分别用于 ingress hook
和 egress hook 的流量统计。

```c
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <stdint.h>
#include <iproute2/bpf_elf.h>

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

#ifndef __inline
# define __inline                         \
   inline __attribute__((always_inline))
#endif

#ifndef lock_xadd
# define lock_xadd(ptr, val)              \
   ((void)__sync_fetch_and_add(ptr, val))
#endif

#ifndef BPF_FUNC
# define BPF_FUNC(NAME, ...)              \
   (*NAME)(__VA_ARGS__) = (void *)BPF_FUNC_##NAME
#endif

static void *BPF_FUNC(map_lookup_elem, void *map, const void *key);

struct bpf_elf_map acc_map __section("maps") = {
    .type           = BPF_MAP_TYPE_ARRAY,
    .size_key       = sizeof(uint32_t),
    .size_value     = sizeof(uint32_t),
    .pinning        = PIN_GLOBAL_NS,
    .max_elem       = 2,
};

static __inline int account_data(struct __sk_buff *skb, uint32_t dir)
{
    uint32_t *bytes;

    bytes = map_lookup_elem(&acc_map, &dir);
    if (bytes)
            lock_xadd(bytes, skb->len);

    return TC_ACT_OK;
}

__section("ingress")
int tc_ingress(struct __sk_buff *skb)
{
    return account_data(skb, 0);
}

__section("egress")
int tc_egress(struct __sk_buff *skb)
{
    return account_data(skb, 1);
}

char __license[] __section("license") = "GPL";
```

##### 其他程序说明

这个例子还展示了其他的一些很有用的地方，在开发程序的过程中需要引起注意。这段代码
include 了内核头文件、标准 C 头文件和一个特定的 iproute2 头文件，后者定义了
`struct bpf_elf_map`。iproute2 有一个通用的 BPF ELF 加载器，因此 `struct bpf_elf_map`
的定义对于 XDP 和 tc 类型的程序是完全一样的。

程序中每条 `struct bpf_elf_map` 记录（entry）定义一个 map，这个记录包含了生成一
个（ingress 和 egress 程序需要用到的）map 所需的全部信息（例如 key/value 大
小）。这个结构体的定义必须放在 `maps` section，这样加载器才能找到它。可以用这个
结构体声明很多名字不同的变量，但这些声明前面必须加上 `__section("maps")` 注解。

结构体 `struct bpf_elf_map` 是特定于 iproute2 的。**不同的 BPF ELF 加载器有不同
的格式**，例如，内核源码树中的 `libbpf`（主要是 `perf` 在用）就有一个不同的规范
（结构体定义）。iproute2 保证 `struct bpf_elf_map` 的后向兼容性。**Cilium 采用的
是 iproute2 模型**。

这个例子还展示了 BPF 辅助函数是如何映射到 C 代码以及如何被使用的。这里首先定义了
一个宏 `BPF_FUNC`，接受一个函数名 `NAME` 以及其他的任意参数。然后用这个宏声明了一
个 `NAME` 为 `map_lookup_elem` 的函数，经过宏展开后会变成
`BPF_FUNC_map_lookup_elem` 枚举值，后者以辅助函数的形式定义在 `uapi/linux/bpf.h`
。当随后这个程序被加载到内核时，校验器会检查传入的参数是否是期望的类型，如果是，
就将辅助函数调用重新指向（re-points）某个真正的函数调用。另外，
`map_lookup_elem()` 还展示了 map 是如何传递给 BPF 辅助函数的。这里，`maps`
section 中的  `&acc_map` 作为第一个参数传递给 `map_lookup_elem()`。

由于程序中定义的数组 map （array map）是全局的，因此计数时需要使用原子操作，这里
是使用了 `lock_xadd()`。LLVM 将 `__sync_fetch_and_add()` 作为一个内置函数映射到
BPF 原子加指令，即 `BPF_STX | BPF_XADD | BPF_W`（for word sizes）。

另外，`struct bpf_elf_map` 中的 `.pinning` 字段初始化为 `PIN_GLOBAL_NS`，这意味
着 tc 会将这个 map 作为一个节点（node）钉（pin）到 BPF 伪文件系统。默认情况下，
这个变量 `acc_map` 将被钉到 `/sys/fs/bpf/tc/globals/acc_map`。

* **如果指定的是 `PIN_GLOBAL_NS`，那 map 会被放到 `/sys/fs/bpf/tc/globals/`**。
  `globals` 是一个跨对象文件的全局命名空间。
* 如果指定的是 `PIN_OBJECT_NS`，tc 将会为对象文件创建一个它的本地目录（local to
  the object file）。例如，只要指定了 `PIN_OBJECT_NS`，不同的 C 文件都可以像上
  面一样定义各自的 `acc_map`。在这种情况下，这个 map 会在不同 BPF 程序之间共享。
* `PIN_NONE` 表示 map 不会作为节点（node）钉（pin）到 BPF 文件系统，因此当 tc 退
  出时这个 map 就无法从用户空间访问了。同时，这还意味着独立的 tc 命令会创建出独
  立的 map 实例，因此后执行的 tc 命令无法用这个 map 名字找到之前被钉住的 map。
  在路径 `/sys/fs/bpf/tc/globals/acc_map` 中，map 名是 `acc_map`。

因此，在加载 `ingress` 程序时，tc 会先查找这个 map 在 BPF 文件系统中是否存在，不
存在就创建一个。创建成功后，map 会被钉（pin）到 BPF 文件系统，因此当 `egress` 程
序通过 tc 加载之后，它就会发现这个 map 存在了，接下来会复用这个 map 而不是再创建
一个新的。在 map 存在的情况下，加载器还会确保 map 的属性（properties）是匹配的，
例如 key/value 大小等等。

就像 tc 可以从同一 map 获取数据一样，第三方应用也可以用 `bpf` 系统调用中的
`BPF_OBJ_GET` 命令创建一个指向某个 map 实例的新文件描述符，然后用这个描述
符来查看/更新/删除 map 中的数据。

通过 clang 编译和 iproute2 加载：

```shell
$ clang -O2 -Wall -target bpf -c tc-example.c -o tc-example.o

$ tc qdisc add dev em1 clsact
$ tc filter add dev em1 ingress bpf da obj tc-example.o sec ingress
$ tc filter add dev em1 egress bpf da obj tc-example.o sec egress

$ tc filter show dev em1 ingress
filter protocol all pref 49152 bpf
filter protocol all pref 49152 bpf handle 0x1 tc-example.o:[ingress] direct-action id 1 tag c5f7825e5dac396f

$ tc filter show dev em1 egress
filter protocol all pref 49152 bpf
filter protocol all pref 49152 bpf handle 0x1 tc-example.o:[egress] direct-action id 2 tag b2fd5adc0f262714

$ mount | grep bpf
sysfs on /sys/fs/bpf type sysfs (rw,nosuid,nodev,noexec,relatime,seclabel)
bpf on /sys/fs/bpf type bpf (rw,relatime,mode=0700)

$ tree /sys/fs/bpf/
/sys/fs/bpf/
+-- ip -> /sys/fs/bpf/tc/
+-- tc
|   +-- globals
|       +-- acc_map
+-- xdp -> /sys/fs/bpf/tc/

4 directories, 1 file
```

以上步骤指向完成后，当包经过 `em` 设备时，BPF map 中的计数器就会递增。

#### 3. 不允许全局变量

出于第 1 条中提到的原因（只支持 BPF maps 重定位，译者注），BPF 不能使用全局变量
，而常规 C 程序中是可以的。

但是，我们有**间接的方式**实现全局变量的效果：BPF 程序可以使用一个
`BPF_MAP_TYPE_PERCPU_ARRAY` 类型的、只有一个槽（slot）的、可以存放任意类型数据（
arbitrary value size）的 **BPF map**。这可以实现全局变量的效果**原因是**，**BPF
程序在执行期间不会被内核抢占**，因此可以用单个 map entry 作为一个 scratch buffer
使用，存储临时数据，例如扩展 BPF 栈的限制（512 字节）。这种方式在尾调用中也是可
以工作的，因为**尾调用执行期间也不会被抢占**。

另外，如果要在不同次 BPF 程序执行之间保持状态，使用常规的 BPF map 就可以了。

#### 4. 不支持常量字符串或数组（const strings or arrays）

BPF C 程序中不允许定义 `const` 字符串或其他数组，原因和第 1 点及第 3 点一样，即
，ELF 文件中生成的**重定位项（relocation entries）会被加载器拒绝**，因为不符合加
载器的 ABI（加载器也无法修复这些重定位项，因为这需要对已经编译好的 BPF 序列进行
大范围的重写）。

将来 LLVM 可能会检测这种情况，提前将错误抛给用户。现在可以用下面的辅助函数来作为
短期解决方式（work around）：

```c
static void BPF_FUNC(trace_printk, const char *fmt, int fmt_size, ...);

#ifndef printk
# define printk(fmt, ...)                                      \
    ({                                                         \
        char ____fmt[] = fmt;                                  \
        trace_printk(____fmt, sizeof(____fmt), ##__VA_ARGS__); \
    })
#endif
```

有了上面的定义，程序就可以自然地使用这个宏，例如 `printk("skb len:%u\n", skb->len);`。
**输出会写到 trace pipe，用 `tc exec bpf dbg` 命令可以获取这些打印的消息。**

不过，使用 `trace_printk()` 辅助函数也有一些不足，因此不建议在生产环境使用。每次
调用这个辅助函数时，常量字符串（例如 `"skb len:%u\n"`）都需要加载到 BPF 栈，但这
个辅助函数最多只能接受 5 个参数，因此使用这个函数输出信息时只能传递三个参数。

因此，虽然这个辅助函数对快速调试很有用，但（对于网络程序）还是推荐使用
`skb_event_output()` 或 `xdp_event_output()` 辅助函数。这两个函数接受从 BPF 程序
传递自定义的结构体类型参数，然后将参数以及可选的包数据（packet sample）放到 perf
event ring buffer。例如，Cilium monitor 利用这些辅助函数实现了一个调试框架，以及
在发现违反网络策略时发出通知等功能。这些函数通过一个无锁的、内存映射的、
per-CPU 的 `perf` ring buffer 传递数据，因此要远快于 `trace_printk()`。

#### 5. 使用 LLVM 内置的函数做内存操作

因为 BPF 程序除了调用 BPF 辅助函数之外无法执行任何函数调用，因此常规的库代码必须
实现为内联函数。另外，LLVM 也提供了一些可以用于特定大小（这里是 `n`）的内置函数
，这些函数永远都会被内联：

```c
#ifndef memset
# define memset(dest, chr, n)   __builtin_memset((dest), (chr), (n))
#endif

#ifndef memcpy
# define memcpy(dest, src, n)   __builtin_memcpy((dest), (src), (n))
#endif

#ifndef memmove
# define memmove(dest, src, n)  __builtin_memmove((dest), (src), (n))
#endif
```

LLVM 后端中的某个问题会导致内置的 `memcmp()` 有某些边界场景下无法内联，因此在这
个问题解决之前不推荐使用这个函数。

#### 6. （目前还）不支持循环

内核中的 BPF 校验器除了对其他的控制流进行图验证（graph validation）之外，还会对
所有程序路径执行深度优先搜索（depth first search），确保其中不存在循环。这样做的
目的是确保程序永远会结束。

但可以使用 `#pragma unroll` 指令实现常量的、不超过一定上限的循环。下面是一个例子
：


```c
#pragma unroll
    for (i = 0; i < IPV6_MAX_HEADERS; i++) {
        switch (nh) {
        case NEXTHDR_NONE:
            return DROP_INVALID_EXTHDR;
        case NEXTHDR_FRAGMENT:
            return DROP_FRAG_NOSUPPORT;
        case NEXTHDR_HOP:
        case NEXTHDR_ROUTING:
        case NEXTHDR_AUTH:
        case NEXTHDR_DEST:
            if (skb_load_bytes(skb, l3_off + len, &opthdr, sizeof(opthdr)) < 0)
                return DROP_INVALID;

            nh = opthdr.nexthdr;
            if (nh == NEXTHDR_AUTH)
                len += ipv6_authlen(&opthdr);
            else
                len += ipv6_optlen(&opthdr);
            break;
        default:
            *nexthdr = nh;
            return len;
        }
    }
```

另外一种实现循环的方式是：用一个 `BPF_MAP_TYPE_PERCPU_ARRAY` map 作为本地 scratch
space（存储空间），然后用尾调用的方式调用函数自身。虽然这种方式更加动态，但目前
最大只支持 32 层嵌套调用。

将来 BPF 可能会提供一些更加原生、但有一定限制的循环。

#### 7. 尾调用的用途

尾调用能够从一个程序调到另一个程序，提供了**在运行时（runtime）原子地改变程序行
为**的灵活性。为了选择要跳转到哪个程序，尾调用使用了 **程序数组 map**（
`BPF_MAP_TYPE_PROG_ARRAY`），将 map 及其索引（index）传递给将要跳转到的程序。跳
转动作一旦完成，就没有办法返回到原来的程序；但如果给定的 map 索引中没有程序（无
法跳转），执行会继续在原来的程序中执行。

例如，可以用尾调用实现解析器的不同阶段，可以在运行时（runtime）更新这些阶段的新
解析特性。

尾调用的另一个用处是**事件通知**，例如，Cilium 可以在运行时（runtime）开启或关闭丢弃
包的通知（packet drop notifications），其中对 `skb_event_output()` 的调用就是发
生在被尾调用的程序中。因此，在常规情况下，执行的永远是从上到下的路径（
fall-through path），当某个程序被加入到相关的 map 索引之后，程序就会解析元数据，
触发向用户空间守护进程（user space daemon）发送事件通知。

程序数组 map 非常灵活， map 中每个索引对应的程序可以实现各自的动作（actions）。
例如，attach 到 tc 或 XDP 的 root 程序执行初始的、跳转到程序数组 map 中索引为 0
的程序，然后执行流量抽样（traffic sampling），然后跳转到索引为 1 的程序，在那个
程序中应用防火墙策略，然后就可以决定是丢地包还是将其送到索引为 2 的程序中继续
处理，在后者中，可能可能会被 mangle 然后再次通过某个接口发送出去。在程序数据 map
之中是可以随意跳转的。当达到尾调用的最大调用深度时，内核最终会执行 fall-through
path。

一个使用尾调用的最小程序示例：

```c
[...]

#ifndef __stringify
# define __stringify(X)   #X
#endif

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

#ifndef __section_tail
# define __section_tail(ID, KEY)          \
   __section(__stringify(ID) "/" __stringify(KEY))
#endif

#ifndef BPF_FUNC
# define BPF_FUNC(NAME, ...)              \
   (*NAME)(__VA_ARGS__) = (void *)BPF_FUNC_##NAME
#endif

#define BPF_JMP_MAP_ID   1

static void BPF_FUNC(tail_call, struct __sk_buff *skb, void *map,
                     uint32_t index);

struct bpf_elf_map jmp_map __section("maps") = {
    .type           = BPF_MAP_TYPE_PROG_ARRAY,
    .id             = BPF_JMP_MAP_ID,
    .size_key       = sizeof(uint32_t),
    .size_value     = sizeof(uint32_t),
    .pinning        = PIN_GLOBAL_NS,
    .max_elem       = 1,
};

__section_tail(JMP_MAP_ID, 0)
int looper(struct __sk_buff *skb)
{
    printk("skb cb: %u\n", skb->cb[0]++);
    tail_call(skb, &jmp_map, 0);
    return TC_ACT_OK;
}

__section("prog")
int entry(struct __sk_buff *skb)
{
    skb->cb[0] = 0;
    tail_call(skb, &jmp_map, 0);
    return TC_ACT_OK;
}

char __license[] __section("license") = "GPL";
```

加载这个示例程序时，tc 会创建其中的程序数组（`jmp_map` 变量），并将其钉（pin）到
BPF 文件系统中全局命名空间下名为的 `jump_map` 位置。而且，iproute2 中的 BPF ELF
加载器也会识别出标记为 `__section_tail()` 的 section。 `jmp_map` 的 `id` 字段会
跟`__section_tail()` 中的 id 字段（这里初始化为常量 `JMP_MAP_ID`）做匹配，因此程
序能加载到用户指定的索引（位置），在上面的例子中这个索引是 0。然后，所有的尾调用
section 将会被 iproute2 加载器处理，关联到 map 中。这个机制并不是 tc 特有的，
iproute2 支持的其他 BPF 程序类型（例如 XDP、lwt）也适用。

生成的 elf 包含 section headers，描述 map id 和 map 内的条目：

```shell
$ llvm-objdump -S --no-show-raw-insn prog_array.o | less
prog_array.o:   file format ELF64-BPF

Disassembly of section 1/0:
looper:
       0:       r6 = r1
       1:       r2 = *(u32 *)(r6 + 48)
       2:       r1 = r2
       3:       r1 += 1
       4:       *(u32 *)(r6 + 48) = r1
       5:       r1 = 0 ll
       7:       call -1
       8:       r1 = r6
       9:       r2 = 0 ll
      11:       r3 = 0
      12:       call 12
      13:       r0 = 0
      14:       exit
Disassembly of section prog:
entry:
       0:       r2 = 0
       1:       *(u32 *)(r1 + 48) = r2
       2:       r2 = 0 ll
       4:       r3 = 0
       5:       call 12
       6:       r0 = 0
       7:       exi
```

在这个例子中，`section 1/0` 表示 `looper()` 函数位于 map `1` 中，在 map `1` 内的
位置是 `0`。

被钉住（pinned）map 可以被用户空间应用（例如 Cilium daemon）读取，也可以被 tc 本
身读取，因为 tc 可能会用新的程序替换原来的程序，此时可能需要读取 map 内容。
更新是原子的。

**tc 执行尾调用 map 更新（tail call map updates）的例子**：

```shell
$ tc exec bpf graft m:globals/jmp_map key 0 obj new.o sec foo
```

如果 iproute2 需要更新被钉住（pinned）的程序数组，可以使用 `graft` 命令。上面的
例子中指向的是 `globals/jmp_map`，那 tc 将会用一个新程序更新位于 index/key 为 `0` 的 map，
这个新程序位于对象文件 `new.o` 中的 `foo` section。

#### 8. BPF 最大栈空间 512 字节

BPF 程序的最大栈空间是 512 字节，在使用 C 语言实现 BPF 程序时需要考虑到这一点。
但正如在第 3 点中提到的，可以通过一个只有一条记录（single entry）的
`BPF_MAP_TYPE_PERCPU_ARRAY` map 来绕过这限制，增大 scratch buffer 空间。

#### 9. 尝试使用 BPF 内联汇编

LLVM 6.0 以后支持 BPF 内联汇编，在某些场景下可能会用到。下面这个玩具示例程序（
没有实际意义）展示了一个 64 位原子加操作。

由于文档不足，要获取更多信息和例子，目前可能只能参考 LLVM 源码中的
`lib/Target/BPF/BPFInstrInfo.td` 以及 `test/CodeGen/BPF/`。测试代码：

```c
#include <linux/bpf.h>

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

__section("prog")
int xdp_test(struct xdp_md *ctx)
{
    __u64 a = 2, b = 3, *c = &a;
    /* just a toy xadd example to show the syntax */
    asm volatile("lock *(u64 *)(%0+0) += %1" : "=r"(c) : "r"(b), "0"(c));
    return a;
}

char __license[] __section("license") = "GPL";
```

上面的程序会被编译成下面的 BPF 指令序列：

```shell
Verifier analysis:

0: (b7) r1 = 2
1: (7b) *(u64 *)(r10 -8) = r1
2: (b7) r1 = 3
3: (bf) r2 = r10
4: (07) r2 += -8
5: (db) lock *(u64 *)(r2 +0) += r1
6: (79) r0 = *(u64 *)(r10 -8)
7: (95) exit
processed 8 insns (limit 131072), stack depth 8
```

#### 10. 用 `#pragma pack` 禁止结构体填充（struct padding）

现代编译器默认会对数据结构进行**内存对齐**（align），以实现更加高效的访问。结构
体成员会被对齐到数倍于其自身大小的内存位置，不足的部分会进行填充（padding），因
此结构体最终的大小可能会比预想中大。

```c
struct called_info {
    u64 start;  // 8-byte
    u64 end;    // 8-byte
    u32 sector; // 4-byte
}; // size of 20-byte ?

printf("size of %d-byte\n", sizeof(struct called_info)); // size of 24-byte

// Actual compiled composition of struct called_info
// 0x0(0)                   0x8(8)
//  ↓________________________↓
//  |        start (8)       |
//  |________________________|
//  |         end  (8)       |
//  |________________________|
//  |  sector(4) |  PADDING  | <= address aligned to 8
//  |____________|___________|     with 4-byte PADDING.
```

内核中的 BPF 校验器会检查栈边界（stack boundary），BPF 程序不会访问栈边界外的空
间，或者是未初始化的栈空间。如果将结构体中填充出来的内存区域作为一个 map 值进行
访问，那调用 `bpf_prog_load()` 时就会报 `invalid indirect read from stack` 错误。

示例代码：

```c
struct called_info {
    u64 start;
    u64 end;
    u32 sector;
};

struct bpf_map_def SEC("maps") called_info_map = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(long),
    .value_size = sizeof(struct called_info),
    .max_entries = 4096,
};

SEC("kprobe/submit_bio")
int submit_bio_entry(struct pt_regs *ctx)
{
    char fmt[] = "submit_bio(bio=0x%lx) called: %llu\n";
    u64 start_time = bpf_ktime_get_ns();
    long bio_ptr = PT_REGS_PARM1(ctx);
    struct called_info called_info = {
            .start = start_time,
            .end = 0,
            .bi_sector = 0
    };

    bpf_map_update_elem(&called_info_map, &bio_ptr, &called_info, BPF_ANY);
    bpf_trace_printk(fmt, sizeof(fmt), bio_ptr, start_time);
    return 0;
}

// On bpf_load_program
bpf_load_program() err=13
0: (bf) r6 = r1
...
19: (b7) r1 = 0
20: (7b) *(u64 *)(r10 -72) = r1
21: (7b) *(u64 *)(r10 -80) = r7
22: (63) *(u32 *)(r10 -64) = r1
...
30: (85) call bpf_map_update_elem#2
invalid indirect read from stack off -80+20 size 24
```

在 `bpf_prog_load()` 中会调用 BPF 校验器的 `bpf_check()` 函数，后者会调用
`check_func_arg() -> check_stack_boundary()` 来检查栈边界。从上面的错误可以看出
，`struct called_info` 被编译成 24 字节，错误信息提示从 `+20` 位置读取数据是“非
法的间接读取”（invalid indirect read）。从我们更前面给出的内存布局图中可以看到，
地址 `0x14(20)` 是填充（PADDING ）开始的地方。这里再次画出内存布局图以方便对比：

```c
// Actual compiled composition of struct called_info
// 0x10(16)    0x14(20)    0x18(24)
//  ↓____________↓___________↓
//  |  sector(4) |  PADDING  | <= address aligned to 8
//  |____________|___________|     with 4-byte PADDING.
```

`check_stack_boundary()` 会遍历每一个从开始指针出发的 `access_size` (24) 字节，
确保它们位于栈边界内部，并且栈内的所有元素都初始化了。因此填充的部分是不允许使用
的，所以报了 “invalid indirect read from stack” 错误。要避免这种错误，需要将结
构体中的填充去掉。这是通过 `#pragma pack(n)` 原语实现的：

```c
#pragma pack(4)
struct called_info {
    u64 start;  // 8-byte
    u64 end;    // 8-byte
    u32 sector; // 4-byte
}; // size of 20-byte ?

printf("size of %d-byte\n", sizeof(struct called_info)); // size of 20-byte

// Actual compiled composition of packed struct called_info
// 0x0(0)                   0x8(8)
//  ↓________________________↓
//  |        start (8)       |
//  |________________________|
//  |         end  (8)       |
//  |________________________|
//  |  sector(4) |             <= address aligned to 4
//  |____________|                 with no PADDING.
```

在 `struct called_info` 前面加上 `#pragma pack(4)` 之后，编译器会以 4 字节为单位
进行对齐。上面的图可以看到，这个结构体现在已经变成 20 字节大小，没有填充了。

但是，去掉填充也是有弊端的。例如，编译器产生的代码没有原来优化的好。去掉填充之后
，处理器访问结构体时触发的是非对齐访问（unaligned access），可能会导致性能下降。
并且，某些架构上的校验器可能会直接拒绝非对齐访问。

不过，我们也有一种方式可以避免产生自动填充：手动填充。我们简单地在结构体中加入一
个 `u32 pad` 成员来显式填充，这样既避免了自动填充的问题，又解决了非对齐访问的问
题。

```c
struct called_info {
    u64 start;  // 8-byte
    u64 end;    // 8-byte
    u32 sector; // 4-byte
    u32 pad;    // 4-byte
}; // size of 24-byte ?

printf("size of %d-byte\n", sizeof(struct called_info)); // size of 24-byte

// Actual compiled composition of struct called_info with explicit padding
// 0x0(0)                   0x8(8)
//  ↓________________________↓
//  |        start (8)       |
//  |________________________|
//  |         end  (8)       |
//  |________________________|
//  |  sector(4) |  pad (4)  | <= address aligned to 8
//  |____________|___________|     with explicit PADDING.
```

#### 11. 通过未验证的引用（invalidated references）访问包数据

某些网络相关的 BPF 辅助函数，例如 `bpf_skb_store_bytes`，可能会修改包的大小。校验
器无法跟踪这类改动，因此它会将所有之前对包数据的引用都视为过期的（未验证的）
。因此，为避免程序被校验器拒绝，在访问数据之外需要先更新相应的引用。

来看下面的例子：

```c
struct iphdr *ip4 = (struct iphdr *) skb->data + ETH_HLEN;

skb_store_bytes(skb, l3_off + offsetof(struct iphdr, saddr), &new_saddr, 4, 0);

if (ip4->protocol == IPPROTO_TCP) {
    // do something
}
```

校验器会拒绝这段代码，因为它认为在 `skb_store_bytes` 执行之后，引用
`ip4->protocol` 是未验证的（invalidated）:

```shell
  R1=pkt_end(id=0,off=0,imm=0) R2=pkt(id=0,off=34,r=34,imm=0) R3=inv0
  R6=ctx(id=0,off=0,imm=0) R7=inv(id=0,umax_value=4294967295,var_off=(0x0; 0xffffffff))
  R8=inv4294967162 R9=pkt(id=0,off=0,r=34,imm=0) R10=fp0,call_-1
  ...
  18: (85) call bpf_skb_store_bytes#9
  19: (7b) *(u64 *)(r10 -56) = r7
  R0=inv(id=0) R6=ctx(id=0,off=0,imm=0) R7=inv(id=0,umax_value=2,var_off=(0x0; 0x3))
  R8=inv4294967162 R9=inv(id=0) R10=fp0,call_-1 fp-48=mmmm???? fp-56=mmmmmmmm
  21: (61) r1 = *(u32 *)(r9 +23)
  R9 invalid mem access 'inv'
```

要解决这个问题，必须更新（重新计算） `ip4` 的地址：

```c
struct iphdr *ip4 = (struct iphdr *) skb->data + ETH_HLEN;

skb_store_bytes(skb, l3_off + offsetof(struct iphdr, saddr), &new_saddr, 4, 0);

ip4 = (struct iphdr *) skb->data + ETH_HLEN;

if (ip4->protocol == IPPROTO_TCP) {
    // do something
}
```

<a name="tool_iproute2"></a>

## 2.3 iproute2

**很多前端工具，例如 bcc、perf、iproute2，都可以将 BPF 程序加载到内核**。Linux
内核源码树中还提供了一个用户空间库 `tools/lib/bpf/`，目前主要是 perf 在使用，用
于加载 BPF 程序到内核，这个库的开发也主要是由 perf 在驱动。但这个库是通用的，并非
只能被 perf 使用。bcc 是一个 BPF 工具套件，里面提供了很多有用的 BPF 程序，主要用
于跟踪（tracing）；这些程序通过一个专门的 Python 接口加载，Python 代码中内嵌了
BPF C 代码。

但通常来说，不同前端在实现 BPF 程序时，语法和语义稍有不同。另外，内核源码树（
`samples/bpf/`）中也有一些示例程序，它们解析生成的对象文件，通过系统调用直接
加载代码到内核。

本节和前一节主要关注**如何使用 `iproute2` 提供的 BPF 前端加载 XDP、`tc` 或 `lwt`
类型的网络程序**，因为 **Cilium 的 BPF 程序就是面向这个加载器实现的**。将来
Cilium 会实现自己原生的 BPF 加载器，但为了开发和调试方便，程序仍会保持与
iproute2 套件的兼容性。

所有 iproute2 支持的 BPF 程序都共享相同的 BPF 加载逻辑，因为它们使用相同的加载器
后端（以函数库的形式，在 iproute2 中对应的代码是 `lib/bpf.c`）。

前面 LLVM 小节介绍了一些和编写 BPF C 程序相关的 iproute2 内容，本文接下来将关注
编写这些程序时，和 tc 与 XDP 特定的方面。因此，本节将关注焦点放置使用例子上，展示
**如何使用 iproute2 加载对象文件，以及加载器的一些通用机制**。本节不会覆盖所有细
节，但对于入门来说足够了。

<a name="ch_2.3.1"></a>

### 2.3.1 加载 XDP BPF 对象文件

给定一个为 XDP 编译的 BPF 对象文件 `prog.o`，可以用 `ip` 命令加载到支持 XDP 的
netdevice `em1`：

```shell
$ ip link set dev em1 xdp obj prog.o
```

以上命令假设程序代码存储在默认的 section，在 XDP 的场景下就是 `prog` section。如
果是在其他 section，例如 `foobar`，那就需要用如下命令：

```shell
$ ip link set dev em1 xdp obj prog.o sec foobar
```

注意，我们还可以将程序加载到 `.text` section。修改程序，从 `xdp_drop` 入口去掉
`__section()` 注解：

```c
#include <linux/bpf.h>

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

int xdp_drop(struct xdp_md *ctx)
{
    return XDP_DROP;
}

char __license[] __section("license") = "GPL";
```

然后通过如下命令加载：

```shell
$ ip link set dev em1 xdp obj prog.o sec .text
```

默认情况下，如果 XDP 程序已经 attach 到网络接口，那再次加载会报错，这样设计是为
了防止程序被无意中覆盖。要强制替换当前正在运行的 XDP 程序，必须指定 `-force` 参数：

```shell
$ ip -force link set dev em1 xdp obj prog.o
```

今天，大部分支持 XDP 的驱动都支持**在不会引起流量中断（traffic interrupt）的前提
下原子地替换运行中的程序**。出于性能考虑，支持 XDP 的驱动只允许 attach 一个程序
，不支持程序链（a chain of programs）。但正如上一节讨论的，如果有必要的话，可以
通过尾调用来对程序进行拆分，以达到与程序链类似的效果。

如果一个接口上有 XDP 程序 attach，`ip link` 命令会显示一个 **`xdp` 标记**。因
此可以用 `ip link | grep xdp` 查看所有有 XDP 程序运行的接口。`ip -d link` 可以查
看进一步信息；另外，`bpftool` 指定 BPF 程序 ID 可以获取 attached 程序的信息，其
中程序 ID 可以通过 `ip link` 看到。

要从接口**删除 XDP 程序**，执行下面的命令：

```shell
$ ip link set dev em1 xdp off
```

要将驱动的工作模式从 non-XDP 切换到 native XDP ，或者相反，通常情况下驱动都需要
重新配置它的接收（和发送）环形缓冲区，以保证接收的数据包在单个页面内是线性排列的，
这样 BPF 程序才可以读取或写入。一旦完成这项配置后，大部分驱动只需要执行一次原子
的程序替换，将新的 BPF 程序加载到设备中。

#### XDP 工作模式

XDP 总共支持三种工作模式（operation mode），这三种模式 `iproute2` 都实现了：

* `xdpdrv`

    `xdpdrv` 表示 **native XDP**（原生 XDP）, 意味着 BPF 程序**直接在驱动的接收路
    径上运行**，理论上这是软件层最早可以处理包的位置（the earliest possible
    point）。这是**常规/传统的 XDP 模式，需要驱动实现对 XDP 的支持**，目前 Linux
    内核中主流的 10G/40G 网卡都已经支持。

* `xdpgeneric`

    `xdpgeneric` 表示 **generic XDP**（通用 XDP），用于给那些还没有原生支持 XDP
    的驱动进行试验性测试。generic XDP hook 位于内核协议栈的主接收路径（main
    receive path）上，接受的是 `skb` 格式的包，但由于 **这些 hook 位于 ingress 路
    径的很后面**（a much later point），因此与 native XDP 相比性能有明显下降。因
    此，`xdpgeneric` 大部分情况下只能用于试验目的，很少用于生产环境。

* `xdpoffload`

    最后，一些智能网卡（例如支持 Netronome's nfp 驱动的网卡）实现了 `xdpoffload` 模式
    ，允许将整个 BPF/XDP 程序 offload 到硬件，因此程序在网卡收到包时就直接在网卡进行
    处理。这提供了比 native XDP 更高的性能，虽然在这种模式中某些 BPF map 类型
    和 BPF 辅助函数是不能用的。BPF 校验器检测到这种情况时会直
    接报错，告诉用户哪些东西是不支持的。除了这些不支持的 BPF 特性之外，其他方面与
    native XDP 都是一样的。

执行 `ip link set dev em1 xdp obj [...]` 命令时，**内核会先尝试以 native XDP 模
式加载程序，如果驱动不支持再自动回退到 generic XDP 模式**。如果显式指定了
`xdpdrv` 而不是 `xdp`，那驱动不支持 native XDP 时加载就会直接失败，而不再尝试
generic XDP 模式。

一个例子：以 native XDP 模式强制加载一个 BPF/XDP 程序，打印链路详情，最后再卸载程序：

```shell
$ ip -force link set dev em1 xdpdrv obj prog.o
$ ip link show
[...]
6: em1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 xdp qdisc mq state UP mode DORMANT group default qlen 1000
    link/ether be:08:4d:b6:85:65 brd ff:ff:ff:ff:ff:ff
    prog/xdp id 1 tag 57cd311f2e27366b
[...]
$ ip link set dev em1 xdpdrv off
```

还是这个例子，但强制以 generic XDP 模式加载（即使驱动支持 native XDP），另外用
bpftool 打印 attached 的这个 dummy 程序内具体的 BPF 指令：

```shell
$ ip -force link set dev em1 xdpgeneric obj prog.o
$ ip link show
[...]
6: em1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 xdpgeneric qdisc mq state UP mode DORMANT group default qlen 1000
    link/ether be:08:4d:b6:85:65 brd ff:ff:ff:ff:ff:ff
    prog/xdp id 4 tag 57cd311f2e27366b                <-- BPF program ID 4
[...]
$ bpftool prog dump xlated id 4                       <-- Dump of instructions running on em1
0: (b7) r0 = 1
1: (95) exit
$ ip link set dev em1 xdpgeneric off
```

最后卸载 XDP，用 bpftool 打印程序信息，查看其中的一些元数据：

```
$ ip -force link set dev em1 xdpoffload obj prog.o
$ ip link show
[...]
6: em1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 xdpoffload qdisc mq state UP mode DORMANT group default qlen 1000
    link/ether be:08:4d:b6:85:65 brd ff:ff:ff:ff:ff:ff
    prog/xdp id 8 tag 57cd311f2e27366b
[...]

$ bpftool prog show id 8
8: xdp  tag 57cd311f2e27366b dev em1                  <-- Also indicates a BPF program offloaded to em1
    loaded_at Apr 11/20:38  uid 0
    xlated 16B  not jited  memlock 4096B

$ ip link set dev em1 xdpoffload off
```

注意，每个程序只能选择用一种 XDP 模式加载，无法同时使用多种模式，例如 `xdpdrv`
和 `xdpgeneric`。

**无法原子地在不同 XDP 模式之间切换**，例如从 generic 模式切换到 native 模式。但
重复设置为同一种模式是可以的：

```shell
$ ip -force link set dev em1 xdpgeneric obj prog.o
$ ip -force link set dev em1 xdpoffload obj prog.o
RTNETLINK answers: File exists

$ ip -force link set dev em1 xdpdrv obj prog.o
RTNETLINK answers: File exists

$ ip -force link set dev em1 xdpgeneric obj prog.o    <-- Succeeds due to xdpgeneric
```

在不同模式之间切换时，需要先退出当前的操作模式，然后才能进入新模式：

```shell
$ ip -force link set dev em1 xdpgeneric obj prog.o
$ ip -force link set dev em1 xdpgeneric off
$ ip -force link set dev em1 xdpoffload obj prog.o

$ ip l
[...]
6: em1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 xdpoffload qdisc mq state UP mode DORMANT group default qlen 1000
    link/ether be:08:4d:b6:85:65 brd ff:ff:ff:ff:ff:ff
    prog/xdp id 17 tag 57cd311f2e27366b
[...]

$ ip -force link set dev em1 xdpoffload off
```

<a name="ch_2.3.2"></a>

### 2.3.2 加载 tc BPF 对象文件

#### 用 tc 加载 BPF 程序

给定一个为 tc 编译的 BPF 对象文件 `prog.o`， 可以通过 `tc` 命令将其加载到一个网
络设备（netdevice）。但**与 XDP 不同，设备是否支持 attach BPF 程序并不依赖驱动**
（即任何网络设备都支持 tc BPF）。下面的命令可以将程序 attach 到 `em1` 的
`ingress` 网络：

```shell
$ tc qdisc add dev em1 clsact
$ tc filter add dev em1 ingress bpf da obj prog.o
```

第一步创建了一个 `clsact` qdisc (Linux 排队规则，Linux **queueing discipline**)。

**`clsact` 是一个 dummy qdisc，和 `ingress` qdisc 类似，可以持有（hold）分类器和
动作（classifier and actions），但不执行真正的排队（queueing）**。后面 attach
`bpf` 分类器需要用到它。`clsact` qdisc 提供了两个特殊的 hook：`ingress` and
`egress`，分类器可以 attach 到这两个 hook 点。这两个 hook 都位于 datapath 的
关键收发路径上，设备 `em1` 的每个包都会经过这两个点。这两个 hook 分别会被下面的内
核函数调用：

* `ingress` hook：`__netif_receive_skb_core() -> sch_handle_ingress()`
* `egress` hook：`__dev_queue_xmit() -> sch_handle_egress()`

类似地，将程序 attach 到 `egress` hook：

```shell
$ tc filter add dev em1 egress bpf da obj prog.o
```

`clsact` qdisc **在 `ingress` 和 `egress` 方向以无锁（lockless）方式执行**，而且
可以 attach 到虚拟的、无队列的设备（virtual, queue-less devices），例如连接容器和
宿主机的 `veth` 设备。

第二条命令，`tc filter` 选择了在 `da`（direct-action）模式中使用 `bpf`。`da` 是
推荐的模式，并且应该永远指定这个参数。粗略地说，**`da` 模式表示 `bpf` 分类器不需
要调用外部的 `tc` action 模块**。事实上 `bpf` 分类器也完全不需要调用外部模块，因
为所有的 packet mangling、转发或其他类型的 action 都可以在这单个 BPF 程序内完成
，因此执行会明显更快。

配置了这两条命令之后，程序就 attach 完成了，接下来只要有包经过这个设备，就会触发
这个程序执行。和 XDP 类似，如果没有使用默认 section 名字，那可以在加载时指定，例
如指定 section 为 `foobar`：

```shell
$ tc filter add dev em1 egress bpf da obj prog.o sec foobar
```

iproute2 BPF 加载器的命令行语法对不同的程序类型都是一样的，因此
`obj prog.o sec foobar` 命令行格式和前面看到的 XDP 的加载是类似的。

查看已经 attach 的程序：

```shell
$ tc filter show dev em1 ingress
filter protocol all pref 49152 bpf
filter protocol all pref 49152 bpf handle 0x1 prog.o:[ingress] direct-action id 1 tag c5f7825e5dac396f

$ tc filter show dev em1 egress
filter protocol all pref 49152 bpf
filter protocol all pref 49152 bpf handle 0x1 prog.o:[egress] direct-action id 2 tag b2fd5adc0f262714
```

输出中的 `prog.o:[ingress]` 表示 section `ingress` 中的程序是从 文件 `prog.o` 加
载的，而且 `bpf` 工作在 `direct-action` 模式。上面还打印了程序的 `id` 和 `tag`，
其中 `tag` 是指令流（instruction stream）的哈希，可以**关联到对应的对象文件或用
`perf` 查看调用栈信息**。`id` 是一个操作系统层唯一的 BPF 程序标识符，可以**用
`bpftool` 进一步查看或 dump 相关的程序信息**。

tc 可以 attach 多个 BPF 程序，并提供了其他的一些分类器，这些分类器可以 chain 到
一起使用。但是，attach 单个 BPF 程序已经完全足够了，因为有了 `da` 模式，所有的包
操作都可以放到同一个程序中，这意味着 BPF 程序自身将会返回 tc action verdict，例
如 `TC_ACT_OK`、`TC_ACT_SHOT` 等等。出于最佳性能和灵活性考虑，这（`da` 模式）是推
荐的使用方式。

#### 程序优先级（`pref`）和句柄（`handle`）

在上面的 `show` 命令中，tc 还打印出了 `pref 49152` 和 `handle 0x1`。如果之前没有
通过命令行显式指定，这两个数据就会自动生成。`pref` 表示优先级，如果指定了多个分
类器，它们会按照优先级从高到低依次执行；`handle` 是一个标识符，在加载了同一分类器的多
个实例并且它们的优先级（`pref`）都一样的情况下会用到这个标识符。因为
**在 BPF 的场景下，单个程序就足够了，因此 `pref` 和 `handle` 通常情况下都可以忽略**。

除非打算后面原子地替换 attached BPF 程序，否则不建议在加载时显式指定 `pref` 和
`handle`。显式指定这两个参数的好处是，后面执行 `replace` 操作时，就不需要再去动
态地查询这两个值。显式指定 `pref` 和 `handle` 时的加载命令：

```
$ tc filter add dev em1 ingress pref 1 handle 1 bpf da obj prog.o sec foobar

$ tc filter show dev em1 ingress
filter protocol all pref 1 bpf
filter protocol all pref 1 bpf handle 0x1 prog.o:[foobar] direct-action id 1 tag c5f7825e5dac396f
```

对应的原子 `replace` 命令：将 `ingress` hook 处的已有程序替换为 `prog.o` 文件中
`foobar` section 中的新 BPF 程序，

```
$ tc filter replace dev em1 ingress pref 1 handle 1 bpf da obj prog.o sec foobar
```

#### 用 tc 删除 BPF 程序

最后，要分别从 `ingress` 和 `egress` 删除所有 attach 的程序，执行：

```shell
$ tc filter del dev em1 ingress
$ tc filter del dev em1 egress
```

要从 netdevice 删除整个 `clsact` qdisc（会隐式地删除 attach 到 `ingress` 和
`egress` hook 上面的所有程序），执行：

```shell
$ tc qdisc del dev em1 clsact
```

#### offload 到网卡

和 XDP BPF 程序类似，如果网卡驱动支持 tc BPF 程序，那也可以将它们 offload 到网卡
。Netronome 的 nfp 网卡对 XDP 和 tc BPF 程序都支持 offload。

```
$ tc qdisc add dev em1 clsact
$ tc filter replace dev em1 ingress pref 1 handle 1 bpf skip_sw da obj prog.o
Error: TC offload is disabled on net device.
We have an error talking to the kernel
```

如果显式以上错误，那需要先启用网卡的 `hw-tc-offload` 功能：

```
$ ethtool -K em1 hw-tc-offload on

$ tc qdisc add dev em1 clsact
$ tc filter replace dev em1 ingress pref 1 handle 1 bpf skip_sw da obj prog.o
$ tc filter show dev em1 ingress
filter protocol all pref 1 bpf
filter protocol all pref 1 bpf handle 0x1 prog.o:[classifier] direct-action skip_sw in_hw id 19 tag 57cd311f2e27366b
```

其中的 `in_hw` 标志表示这个程序已经被 offload 到网卡了。

注意，tc 和 XDP offload 无法同时加载，因此必须要指明是 tc 还是 XDP offload 选项
。

<a name="ch_2.3.3"></a>

### 2.3.3 通过 netdevsim 驱动测试 BPF offload

netdevsim 驱动是 Linux 内核的一部分，它是一个 dummy driver，实现了 XDP BPF 和 tc
BPF 程序的 offload 接口，以及其他一些设施，这些设施可以用来测试内核的改动，或者
某些利用内核的 UAPI 实现了一个控制平面功能的底层用户空间程序。

可以用如下命令创建一个 netdevsim 设备：

```
$ modprobe netdevsim
// [ID] [PORT_COUNT]
$ echo "1 1" > /sys/bus/netdevsim/new_device

$ devlink dev
netdevsim/netdevsim1

$ devlink port
netdevsim/netdevsim1/0: type eth netdev eth0 flavour physical

$ ip l
[...]
4: eth0: <BROADCAST,NOARP,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
    link/ether 2a:d5:cd:08:d1:3f brd ff:ff:ff:ff:ff:ff
```

然后就可以加载 XDP 或 tc BPF 程序，命令和前面的一些例子一样：

```
$ ip -force link set dev eth0 xdpoffload obj prog.o
$ ip l
[...]
4: eth0: <BROADCAST,NOARP,UP,LOWER_UP> mtu 1500 xdpoffload qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
    link/ether 2a:d5:cd:08:d1:3f brd ff:ff:ff:ff:ff:ff
    prog/xdp id 16 tag a04f5eef06a7f555
```

这是用 iproute2 加载 XDP/tc BPF 程序的两个标准步骤。

还有很多对 XDP 和 `tc` 都适用的 **BPF 加载器高级选项**，下面列出其中一些。为简单
起见，这里只列出了 XDP 的例子。

1. **打印更多 log（Verbose），即使命令执行成功**

    在命令最后加上 `verb` 选项可以打印校验器的日志：

    ```shell
    $ ip link set dev em1 xdp obj xdp-example.o verb

    Prog section 'prog' loaded (5)!
     - Type:         6
     - Instructions: 2 (0 over limit)
     - License:      GPL

    Verifier analysis:

    0: (b7) r0 = 1
    1: (95) exit
    processed 2 insns
    ```

2. **加载已经 pin 在 BPF 文件系统中的程序**

    除了从对象文件加载程序之外，iproute2 还可以从 BPF 文件系统加载程序。在某些场
    景下，一些外部实体会将 BPF 程序 pin 在 BPF 文件系统并 attach 到设备。加载命
    令：

    ```shell
    $ ip link set dev em1 xdp pinned /sys/fs/bpf/prog
    ```

    iproute2 还可以使用更简短的相对路径方式（相对于 BPF 文件系统的挂载点）：

    ```shell
    $ ip link set dev em1 xdp pinned m:prog
    ```

在加载 BPF 程序时，iproute2 会自动检测挂载的文件系统实例。如果发现还没有挂载，tc
就会自动将其挂载到默认位置 `/sys/fs/bpf/`。

如果发现已经挂载了一个 BPF 文件系统实例，接下来就会使用这个实例，不会再挂载新的
了：

```shell
$ mkdir /var/run/bpf
$ mount --bind /var/run/bpf /var/run/bpf
$ mount -t bpf bpf /var/run/bpf

$ tc filter add dev em1 ingress bpf da obj tc-example.o sec prog

$ tree /var/run/bpf
/var/run/bpf
+-- ip -> /run/bpf/tc/
+-- tc
|   +-- globals
|       +-- jmp_map
+-- xdp -> /run/bpf/tc/

4 directories, 1 file
```

**默认情况下，`tc` 会创建一个如上面所示的初始目录，所有子系统的用户都会通过符号
链接（symbolic links）指向相同的位置，也是就是 `globals` 命名空间**，因此 pinned
BPF maps 可以被 `iproute2` 中不同类型的 BPF 程序使用。如果文件系统实例已经挂载、
目录已经存在，那 tc 是不会覆盖这个目录的。因此对于 `lwt`, `tc` 和 `xdp` 这几种类
型的 BPF maps，可以从 `globals` 中分离出来，放到各自的目录存放。

在前面的 LLVM 小节中简要介绍过，安装 iproute2 时会向系统中安装一个头文件，BPF 程
序可以直接以标准路（standard include path）径来 include 这个头文件：

```c
#include <iproute2/bpf_elf.h>
```

这个头文件中提供的 API 可以让程序使用 maps 和默认 section 名字。它是 **`iproute2`
和 BPF 程序之间的一份稳定契约**（contract ）。

**iproute2 中 map 的定义是 `struct bpf_elf_map`**。这个结构体内的成员变量已经在
LLVM 小节中介绍过了。

When parsing the BPF object file, the iproute2 loader will walk through
all ELF sections. It initially fetches ancillary sections like `maps` and
`license`. For `maps`, the `struct bpf_elf_map` array will be checked
for validity and whenever needed, compatibility workarounds are performed.
Subsequently all maps are created with the user provided information, either
retrieved as a pinned object, or newly created and then pinned into the BPF
file system. Next the loader will handle all program sections that contain
ELF relocation entries for maps, meaning that BPF instructions loading
map file descriptors into registers are rewritten so that the corresponding
map file descriptors are encoded into the instructions immediate value, in
order for the kernel to be able to convert them later on into map kernel
pointers. After that all the programs themselves are created through the BPF
system call, and tail called maps, if present, updated with the program's file
descriptors.

<a name="tool_iproute2"></a>

## 2.4 bpftool

bpftool 是**查看和调试 BPF 程序**的主要工具。它随内核一起开发，在内核中的路径是
**`tools/bpf/bpftool/`**。

**这个工具可以完成**：

1. **dump 当前已经加载到系统中的所有 BPF 程序和 map**
1. 列出和指定程序相关的所有 BPF map
1. dump 整个 map 中的 key/value 对
1. 查看、更新、删除特定 key
1. 查看给定 key 的相邻 key（neighbor key）

要执行这些操作可以指定 BPF 程序、map ID，或者指定 BPF 文件系统中程序或 map 的位
置。另外，这个工具还提供了将 map 或程序钉（pin）到 BPF 文件系统的功能。

查看系统当前已经加载的 BPF 程序：

```shell
$ bpftool prog
398: sched_cls  tag 56207908be8ad877
   loaded_at Apr 09/16:24  uid 0
   xlated 8800B  jited 6184B  memlock 12288B  map_ids 18,5,17,14
399: sched_cls  tag abc95fb4835a6ec9
   loaded_at Apr 09/16:24  uid 0
   xlated 344B  jited 223B  memlock 4096B  map_ids 18
400: sched_cls  tag afd2e542b30ff3ec
   loaded_at Apr 09/16:24  uid 0
   xlated 1720B  jited 1001B  memlock 4096B  map_ids 17
401: sched_cls  tag 2dbbd74ee5d51cc8
   loaded_at Apr 09/16:24  uid 0
   xlated 3728B  jited 2099B  memlock 4096B  map_ids 17
[...]
```

类似地，查看所有的 active maps：

```shell
$ bpftool map
5: hash  flags 0x0
    key 20B  value 112B  max_entries 65535  memlock 13111296B
6: hash  flags 0x0
    key 20B  value 20B  max_entries 65536  memlock 7344128B
7: hash  flags 0x0
    key 10B  value 16B  max_entries 8192  memlock 790528B
8: hash  flags 0x0
    key 22B  value 28B  max_entries 8192  memlock 987136B
9: hash  flags 0x0
    key 20B  value 8B  max_entries 512000  memlock 49352704B
[...]
```

bpftool 的每个命令都提供了以 json 格式打印的功能，在命令末尾指定 `--json` 就行了。
另外，`--pretty` 会使得打印更加美观，看起来更清楚。

```shell
$ bpftool prog --json --pretty
```

要 dump 特定 BPF 程序的 post-verifier BPF 指令镜像（instruction image），可以先
从查看一个具体程序开始，例如，查看 attach 到 `tc` `ingress` hook 上的程序：

```shell
$ tc filter show dev cilium_host egress
filter protocol all pref 1 bpf chain 0
filter protocol all pref 1 bpf chain 0 handle 0x1 bpf_host.o:[from-netdev] \
                    direct-action not_in_hw id 406 tag e0362f5bd9163a0a jited
```

这个程序是从对象文件 `bpf_host.o` 加载来的，程序位于对象文件的 `from-netdev`
section，程序 ID 为 `406`。基于以上信息 bpftool 可以提供一些关于这个程序的上层元
数据：

```shell
$ bpftool prog show id 406
406: sched_cls  tag e0362f5bd9163a0a
     loaded_at Apr 09/16:24  uid 0
     xlated 11144B  jited 7721B  memlock 12288B  map_ids 18,20,8,5,6,14
```

从上面的输出可以看到：

* 程序 ID 为 406，类型是 `sched_cls`（`BPF_PROG_TYPE_SCHED_CLS`），有一个 `tag`
  为 `e0362f5bd9163a0a`（指令序列的 SHA sum）
* 这个程序被 root `uid 0` 在 `Apr 09/16:24` 加载
* BPF 指令序列有 `11,144 bytes` 长，JIT 之后的镜像有 `7,721 bytes`
* 程序自身（不包括 maps）占用了 `12,288 bytes`，这部分空间使用的是 `uid 0` 用户
  的配额
* BPF 程序使用了 ID 为 `18`、`20` `8` `5` `6` 和 `14` 的 BPF map。可以用这些 ID
  进一步 dump map 自身或相关信息

另外，bpftool 可以 dump 出运行中程序的 BPF 指令：

```shell
$ bpftool prog dump xlated id 406
 0: (b7) r7 = 0
 1: (63) *(u32 *)(r1 +60) = r7
 2: (63) *(u32 *)(r1 +56) = r7
 3: (63) *(u32 *)(r1 +52) = r7
[...]
47: (bf) r4 = r10
48: (07) r4 += -40
49: (79) r6 = *(u64 *)(r10 -104)
50: (bf) r1 = r6
51: (18) r2 = map[id:18]                    <-- BPF map id 18
53: (b7) r5 = 32
54: (85) call bpf_skb_event_output#5656112  <-- BPF helper call
55: (69) r1 = *(u16 *)(r6 +192)
[...]
```

如上面的输出所示，bpftool 将指令流中的 BPF map ID、BPF 辅助函数或其他 BPF 程序都
做了关联。

和内核的 BPF 校验器一样，bpftool dump 指令流时复用了同一个使输出更美观的打印程序
（pretty-printer）。

由于程序被 JIT，因此真正执行的是生成的 JIT 镜像（从上面 `xlated` 中的指令生成的
），这些指令也可以通过 bpftool 查看：

```
$ bpftool prog dump jited id 406
 0:        push   %rbp
 1:        mov    %rsp,%rbp
 4:        sub    $0x228,%rsp
 b:        sub    $0x28,%rbp
 f:        mov    %rbx,0x0(%rbp)
13:        mov    %r13,0x8(%rbp)
17:        mov    %r14,0x10(%rbp)
1b:        mov    %r15,0x18(%rbp)
1f:        xor    %eax,%eax
21:        mov    %rax,0x20(%rbp)
25:        mov    0x80(%rdi),%r9d
[...]
```

另外，还可以指定在输出中将反汇编之后的指令关联到 opcodes，这个功能主要对 BPF JIT
开发者比较有用：

```
$ bpftool prog dump jited id 406 opcodes
 0:        push   %rbp
           55
 1:        mov    %rsp,%rbp
           48 89 e5
 4:        sub    $0x228,%rsp
           48 81 ec 28 02 00 00
 b:        sub    $0x28,%rbp
           48 83 ed 28
 f:        mov    %rbx,0x0(%rbp)
           48 89 5d 00
13:        mov    %r13,0x8(%rbp)
           4c 89 6d 08
17:        mov    %r14,0x10(%rbp)
           4c 89 75 10
1b:        mov    %r15,0x18(%rbp)
           4c 89 7d 18
[...]
```

同样，也可以将常规的 BPF 指令关联到 opcodes，有时在内核中进行调试时会比较有用：

```
$ bpftool prog dump xlated id 406 opcodes
 0: (b7) r7 = 0
    b7 07 00 00 00 00 00 00
 1: (63) *(u32 *)(r1 +60) = r7
    63 71 3c 00 00 00 00 00
 2: (63) *(u32 *)(r1 +56) = r7
    63 71 38 00 00 00 00 00
 3: (63) *(u32 *)(r1 +52) = r7
    63 71 34 00 00 00 00 00
 4: (63) *(u32 *)(r1 +48) = r7
    63 71 30 00 00 00 00 00
 5: (63) *(u32 *)(r1 +64) = r7
    63 71 40 00 00 00 00 00
 [...]
```

此外，还可以用 `graphviz` 以可视化的方式展示程序的基本组成部分。bpftool 提供了一
个 `visual` dump 模式，这种模式下输出的不是 BPF `xlated` 指令文本，而是一张点图（
dot graph），后者可以转换成 png 格式的图片：

```
$ bpftool prog dump xlated id 406 visual &> output.dot

$ dot -Tpng output.dot -o output.png
```

也可以用 dotty 打开生成的点图文件：`dotty output.dot`，`bpf_host.o` 程序的效果如
下图所示（一部分）：

<p align="center"><img src="/assets/img/cilium-bpf-xdp-guide/bpf_dot.png" width="40%" height="40%"></p>

注意，`xlated` 中 dump 出来的指令是经过校验器之后（post-verifier）的 BPF 指令镜
像，即和 BPF 解释器中执行的版本是一样的。

在内核中，校验器会对 BPF 加载器提供的原始指令执行各种重新（rewrite）。一个例子就
是对辅助函数进行内联化（inlining）以提高运行时性能，下面是对一个哈希表查找的优化：

```
$ bpftool prog dump xlated id 3
 0: (b7) r1 = 2
 1: (63) *(u32 *)(r10 -4) = r1
 2: (bf) r2 = r10
 3: (07) r2 += -4
 4: (18) r1 = map[id:2]                      <-- BPF map id 2
 6: (85) call __htab_map_lookup_elem#77408   <-+ BPF helper inlined rewrite
 7: (15) if r0 == 0x0 goto pc+2                |
 8: (07) r0 += 56                              |
 9: (79) r0 = *(u64 *)(r0 +0)                <-+
10: (15) if r0 == 0x0 goto pc+24
11: (bf) r2 = r10
12: (07) r2 += -4
[...]
```

bpftool 通过 kallsyms 来对辅助函数或 BPF-to-BPF 调用进行关联。因此，确保 JIT 之
后的 BPF 程序暴露到了 kallsyms（`bpf_jit_kallsyms`），并且 kallsyms 地址是明确的
（否则调用显示的就是 `call bpf_unspec#0`）：

```
$ echo 0 > /proc/sys/kernel/kptr_restrict
$ echo 1 > /proc/sys/net/core/bpf_jit_kallsyms
```

BPF-to-BPF 调用在解释器和 JIT 镜像中也做了关联。对于后者，子程序的 tag 会显示为
调用目标（call target）。在两种情况下，`pc+2` 都是调用目标的程序计数器偏置（
pc-relative offset），表示就是子程序的地址。

```shell
$ bpftool prog dump xlated id 1
0: (85) call pc+2#__bpf_prog_run_args32
1: (b7) r0 = 1
2: (95) exit
3: (b7) r0 = 2
4: (95) exit
```

对应的 JIT 版本：

```shell
$ bpftool prog dump xlated id 1
0: (85) call pc+2#bpf_prog_3b185187f1855c4c_F
1: (b7) r0 = 1
2: (95) exit
3: (b7) r0 = 2
4: (95) exit
```

在尾调用中，内核会将它们映射为同一个指令，但 bpftool 还是会将它们作为辅助函数进
行关联，以方便调试：

```shell
$ bpftool prog dump xlated id 2
[...]
10: (b7) r2 = 8
11: (85) call bpf_trace_printk#-41312
12: (bf) r1 = r6
13: (18) r2 = map[id:1]
15: (b7) r3 = 0
16: (85) call bpf_tail_call#12
17: (b7) r1 = 42
18: (6b) *(u16 *)(r6 +46) = r1
19: (b7) r0 = 0
20: (95) exit

$ bpftool map show id 1
1: prog_array  flags 0x0
      key 4B  value 4B  max_entries 1  memlock 4096B
```

`map dump` 子命令可以 dump 整个 map，它会遍历所有的 map 元素，输出 key/value。

如果 map 中没有可用的 BTF 数据，那 key/value 会以十六进制格式输出：

```shell
$ bpftool map dump id 5
key:
f0 0d 00 00 00 00 00 00  0a 66 00 00 00 00 8a d6
02 00 00 00
value:
00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
key:
0a 66 1c ee 00 00 00 00  00 00 00 00 00 00 00 00
01 00 00 00
value:
00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
[...]
Found 6 elements
```

如果有 BTF 数据，map 就有了关于 key/value 结构体的调试信息。例如，BTF 信息加上 BPF
map 以及 iproute2 中的 `BPF_ANNOTATE_KV_PAIR()` 会产生下面的输出（内核 selftests
中的 `test_xdp_noinline.o`）：

```shell
$ cat tools/testing/selftests/bpf/test_xdp_noinline.c
  [...]
   struct ctl_value {
         union {
                 __u64 value;
                 __u32 ifindex;
                 __u8 mac[6];
         };
   };

   struct bpf_map_def __attribute__ ((section("maps"), used)) ctl_array = {
          .type		= BPF_MAP_TYPE_ARRAY,
          .key_size	= sizeof(__u32),
          .value_size	= sizeof(struct ctl_value),
          .max_entries	= 16,
          .map_flags	= 0,
   };
   BPF_ANNOTATE_KV_PAIR(ctl_array, __u32, struct ctl_value);

   [...]
```

`BPF_ANNOTATE_KV_PAIR()` 宏强制每个 map-specific ELF section 包含一个空的
key/value，这样 iproute2 BPF 加载器可以将 BTF 数据关联到这个 section，因此在加载
map 时可用从 BTF 中选择响应的类型。

使用 LLVM 编译，并使用 `pahole` 基于调试信息产生 BTF：

```shell
$ clang [...] -O2 -target bpf -g -emit-llvm -c test_xdp_noinline.c -o - |
  llc -march=bpf -mcpu=probe -mattr=dwarfris -filetype=obj -o test_xdp_noinline.o

$ pahole -J test_xdp_noinline.o
```

加载到内核，然后使用 bpftool dump 这个 map：

```shell
$ ip -force link set dev lo xdp obj test_xdp_noinline.o sec xdp-test
$ ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 xdpgeneric/id:227 qdisc noqueue state UNKNOWN group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host
       valid_lft forever preferred_lft forever
[...]

$ bpftool prog show id 227
227: xdp  tag a85e060c275c5616  gpl
    loaded_at 2018-07-17T14:41:29+0000  uid 0
    xlated 8152B  not jited  memlock 12288B  map_ids 381,385,386,382,384,383

$ bpftool map dump id 386
 [{
      "key": 0,
      "value": {
          "": {
              "value": 0,
              "ifindex": 0,
              "mac": []
          }
      }
  },{
      "key": 1,
      "value": {
          "": {
              "value": 0,
              "ifindex": 0,
              "mac": []
          }
      }
  },{
[...]
```

针对 map 的某个 key，也可用通过 bpftool 查看、更新、删除和获取下一个 key（'get
next key'）。

<a name="tool_bpf_sysctls"></a>

## 2.5 BPF sysctls

Linux 内核提供了一些 BPF 相关的 sysctl 配置。

* `/proc/sys/net/core/bpf_jit_enable`：启用或禁用 BPF JIT 编译器。

    ```
    +-------+-------------------------------------------------------------------+
    | Value | Description                                                       |
    +-------+-------------------------------------------------------------------+
    | 0     | Disable the JIT and use only interpreter (kernel's default value) |
    +-------+-------------------------------------------------------------------+
    | 1     | Enable the JIT compiler                                           |
    +-------+-------------------------------------------------------------------+
    | 2     | Enable the JIT and emit debugging traces to the kernel log        |
    +-------+-------------------------------------------------------------------+
    ```

    后面会介绍到，当 JIT 编译设置为调试模式（option `2`）时，`bpf_jit_disasm` 工
    具能够处理调试跟踪信息（debugging traces）。

* `/proc/sys/net/core/bpf_jit_harden`：启用会禁用 BPF JIT 加固。

    注意，启用加固会降低性能，但能够降低 JIT spraying（喷射）攻击，因为它会禁止
    （blind）BPF 程序使用立即值（immediate values）。对于通过解释器处理的程序，
    禁用（blind）立即值是没有必要的（也是没有去做的）。

    ```
    +-------+-------------------------------------------------------------------+
    | Value | Description                                                       |
    +-------+-------------------------------------------------------------------+
    | 0     | Disable JIT hardening (kernel's default value)                    |
    +-------+-------------------------------------------------------------------+
    | 1     | Enable JIT hardening for unprivileged users only                  |
    +-------+-------------------------------------------------------------------+
    | 2     | Enable JIT hardening for all users                                |
    +-------+-------------------------------------------------------------------+
    ```

* `/proc/sys/net/core/bpf_jit_kallsyms`：是否允许 JIT 后的程序作为内核符号暴露到
  `/proc/kallsyms`。

    启用后，这些符号可以被 `perf` 这样的工具识别，使内核在做 stack unwinding 时
    能感知到这些地址，例如，在 dump stack trace 的时候，符合名中会包含 BPF 程序
    tag（`bpf_prog_<tag>`）。如果启用了 `bpf_jit_harden`，这个特性就会自动被禁用
    。

    ```
    +-------+-------------------------------------------------------------------+
    | Value | Description                                                       |
    +-------+-------------------------------------------------------------------+
    | 0     | Disable JIT kallsyms export (kernel's default value)              |
    +-------+-------------------------------------------------------------------+
    | 1     | Enable JIT kallsyms export for privileged users only              |
    +-------+-------------------------------------------------------------------+
    ```

* `/proc/sys/kernel/unprivileged_bpf_disabled`：是否允许非特权用户使用 `bpf(2)`
  系统调用。

    内核默认允许非特权用户使用 `bpf(2)` 系统调用，但一旦将这个开关关闭，必须重启
    内核才能再次将其打开。因此这是一个一次性开关（one-time switch），一旦关闭，
    不管是应用还是管理员都无法再次修改。这个开关不影响 **cBPF 程序**（例如 seccomp）
    或 **传统的没有使用 `bpf(2)` 系统调用的 socket 过滤器** 加载程序到内核。

    ```
    +-------+-------------------------------------------------------------------+
    | Value | Description                                                       |
    +-------+-------------------------------------------------------------------+
    | 0     | Unprivileged use of bpf syscall enabled (kernel's default value)  |
    +-------+-------------------------------------------------------------------+
    | 1     | Unprivileged use of bpf syscall disabled                          |
    +-------+-------------------------------------------------------------------+
    ```

<a name="tool_kernel_testing"></a>

## 2.6 内核测试

Linux 内核自带了一个 selftest 套件，在内核源码树中的路径是
`tools/testing/selftests/bpf/`。

```shell
$ cd tools/testing/selftests/bpf/
$ make
$ make run_tests
```

测试用例包括：

* BPF 校验器、程序 tags、BPF map 接口和 map 类型的很多测试用例
* 用于 LLVM 后端的运行时测试，用 C 代码实现
* 用于解释器和 JIT 的测试，运行在内核，用 eBPF 和 cBPF 汇编实现

<a name="tool_jit_debug"></a>

## 2.7 JIT Debugging

For JIT developers performing audits or writing extensions, each compile run
can output the generated JIT image into the kernel log through:

```shell
$ echo 2 > /proc/sys/net/core/bpf_jit_enable
```

Whenever a new BPF program is loaded, the JIT compiler will dump the output,
which can then be inspected with `dmesg`, for example:

```shell
[ 3389.935842] flen=6 proglen=70 pass=3 image=ffffffffa0069c8f from=tcpdump pid=20583
[ 3389.935847] JIT code: 00000000: 55 48 89 e5 48 83 ec 60 48 89 5d f8 44 8b 4f 68
[ 3389.935849] JIT code: 00000010: 44 2b 4f 6c 4c 8b 87 d8 00 00 00 be 0c 00 00 00
[ 3389.935850] JIT code: 00000020: e8 1d 94 ff e0 3d 00 08 00 00 75 16 be 17 00 00
[ 3389.935851] JIT code: 00000030: 00 e8 28 94 ff e0 83 f8 01 75 07 b8 ff ff 00 00
[ 3389.935852] JIT code: 00000040: eb 02 31 c0 c9 c3
```

`flen` is the length of the BPF program (here, 6 BPF instructions), and `proglen`
tells the number of bytes generated by the JIT for the opcode image (here, 70 bytes
in size). `pass` means that the image was generated in 3 compiler passes, for
example, `x86_64` can have various optimization passes to further reduce the image
size when possible. `image` contains the address of the generated JIT image, `from`
and `pid` the user space application name and PID respectively, which triggered the
compilation process. The dump output for eBPF and cBPF JITs is the same format.

In the kernel tree under `tools/bpf/`, there is a tool called `bpf_jit_disasm`. It
reads out the latest dump and prints the disassembly for further inspection:

```shell
$ ./bpf_jit_disasm
70 bytes emitted from JIT compiler (pass:3, flen:6)
ffffffffa0069c8f + <x>:
   0:       push   %rbp
   1:       mov    %rsp,%rbp
   4:       sub    $0x60,%rsp
   8:       mov    %rbx,-0x8(%rbp)
   c:       mov    0x68(%rdi),%r9d
  10:       sub    0x6c(%rdi),%r9d
  14:       mov    0xd8(%rdi),%r8
  1b:       mov    $0xc,%esi
  20:       callq  0xffffffffe0ff9442
  25:       cmp    $0x800,%eax
  2a:       jne    0x0000000000000042
  2c:       mov    $0x17,%esi
  31:       callq  0xffffffffe0ff945e
  36:       cmp    $0x1,%eax
  39:       jne    0x0000000000000042
  3b:       mov    $0xffff,%eax
  40:       jmp    0x0000000000000044
  42:       xor    %eax,%eax
  44:       leaveq
  45:       retq
```

Alternatively, the tool can also dump related opcodes along with the disassembly.

```shell
$ ./bpf_jit_disasm -o
70 bytes emitted from JIT compiler (pass:3, flen:6)
ffffffffa0069c8f + <x>:
   0:       push   %rbp
    55
   1:       mov    %rsp,%rbp
    48 89 e5
   4:       sub    $0x60,%rsp
    48 83 ec 60
   8:       mov    %rbx,-0x8(%rbp)
    48 89 5d f8
   c:       mov    0x68(%rdi),%r9d
    44 8b 4f 68
  10:       sub    0x6c(%rdi),%r9d
    44 2b 4f 6c
  14:       mov    0xd8(%rdi),%r8
    4c 8b 87 d8 00 00 00
  1b:       mov    $0xc,%esi
    be 0c 00 00 00
  20:       callq  0xffffffffe0ff9442
    e8 1d 94 ff e0
  25:       cmp    $0x800,%eax
    3d 00 08 00 00
  2a:       jne    0x0000000000000042
    75 16
  2c:       mov    $0x17,%esi
    be 17 00 00 00
  31:       callq  0xffffffffe0ff945e
    e8 28 94 ff e0
  36:       cmp    $0x1,%eax
    83 f8 01
  39:       jne    0x0000000000000042
    75 07
  3b:       mov    $0xffff,%eax
    b8 ff ff 00 00
  40:       jmp    0x0000000000000044
    eb 02
  42:       xor    %eax,%eax
    31 c0
  44:       leaveq
    c9
  45:       retq
    c3
```

More recently, `bpftool` adapted the same feature of dumping the BPF JIT
image based on a given BPF program ID already loaded in the system (see
bpftool section).

For performance analysis of JITed BPF programs, `perf` can be used as
usual. As a prerequisite, JITed programs need to be exported through kallsyms
infrastructure.

```shell
$ echo 1 > /proc/sys/net/core/bpf_jit_enable
$ echo 1 > /proc/sys/net/core/bpf_jit_kallsyms
```

Enabling or disabling `bpf_jit_kallsyms` does not require a reload of the
related BPF programs. Next, a small workflow example is provided for profiling
BPF programs. A crafted tc BPF program is used for demonstration purposes,
where perf records a failed allocation inside `bpf_clone_redirect()` helper.
Due to the use of direct write, `bpf_try_make_head_writable()` failed, which
would then release the cloned `skb` again and return with an error message.
`perf` thus records all `kfree_skb` events.

```shell
$ tc qdisc add dev em1 clsact
$ tc filter add dev em1 ingress bpf da obj prog.o sec main
$ tc filter show dev em1 ingress
filter protocol all pref 49152 bpf
filter protocol all pref 49152 bpf handle 0x1 prog.o:[main] direct-action id 1 tag 8227addf251b7543

$ cat /proc/kallsyms
[...]
ffffffffc00349e0 t fjes_hw_init_command_registers    [fjes]
ffffffffc003e2e0 d __tracepoint_fjes_hw_stop_debug_err    [fjes]
ffffffffc0036190 t fjes_hw_epbuf_tx_pkt_send    [fjes]
ffffffffc004b000 t bpf_prog_8227addf251b7543

$ perf record -a -g -e skb:kfree_skb sleep 60
$ perf script --kallsyms=/proc/kallsyms
[...]
ksoftirqd/0     6 [000]  1004.578402:    skb:kfree_skb: skbaddr=0xffff9d4161f20a00 protocol=2048 location=0xffffffffc004b52c
   7fffb8745961 bpf_clone_redirect (/lib/modules/4.10.0+/build/vmlinux)
   7fffc004e52c bpf_prog_8227addf251b7543 (/lib/modules/4.10.0+/build/vmlinux)
   7fffc05b6283 cls_bpf_classify (/lib/modules/4.10.0+/build/vmlinux)
   7fffb875957a tc_classify (/lib/modules/4.10.0+/build/vmlinux)
   7fffb8729840 __netif_receive_skb_core (/lib/modules/4.10.0+/build/vmlinux)
   7fffb8729e38 __netif_receive_skb (/lib/modules/4.10.0+/build/vmlinux)
   7fffb872ae05 process_backlog (/lib/modules/4.10.0+/build/vmlinux)
   7fffb872a43e net_rx_action (/lib/modules/4.10.0+/build/vmlinux)
   7fffb886176c __do_softirq (/lib/modules/4.10.0+/build/vmlinux)
   7fffb80ac5b9 run_ksoftirqd (/lib/modules/4.10.0+/build/vmlinux)
   7fffb80ca7fa smpboot_thread_fn (/lib/modules/4.10.0+/build/vmlinux)
   7fffb80c6831 kthread (/lib/modules/4.10.0+/build/vmlinux)
   7fffb885e09c ret_from_fork (/lib/modules/4.10.0+/build/vmlinux)
```

The stack trace recorded by `perf` will then show the `bpf_prog_8227addf251b7543()`
symbol as part of the call trace, meaning that the BPF program with the
tag `8227addf251b7543` was related to the `kfree_skb` event, and
such program was attached to netdevice `em1` on the ingress hook as
shown by tc.

<a name="tool_introspection"></a>

## 2.8 内省（Introspection）

Linux 内核围绕 BPF 和 XDP 提供了多种 tracepoints，这些 tracepoints 可以用于进一
步查看系统内部行为，例如，跟踪用户空间程序和 bpf 系统调用的交互。

BPF 相关的 tracepoints：

```shell
$ perf list | grep bpf:
bpf:bpf_map_create                                 [Tracepoint event]
bpf:bpf_map_delete_elem                            [Tracepoint event]
bpf:bpf_map_lookup_elem                            [Tracepoint event]
bpf:bpf_map_next_key                               [Tracepoint event]
bpf:bpf_map_update_elem                            [Tracepoint event]
bpf:bpf_obj_get_map                                [Tracepoint event]
bpf:bpf_obj_get_prog                               [Tracepoint event]
bpf:bpf_obj_pin_map                                [Tracepoint event]
bpf:bpf_obj_pin_prog                               [Tracepoint event]
bpf:bpf_prog_get_type                              [Tracepoint event]
bpf:bpf_prog_load                                  [Tracepoint event]
bpf:bpf_prog_put_rcu                               [Tracepoint event]
```

使用 `perf` 跟踪 BPF 系统调用（这里用 `sleep` 只是展示用法，实际场景中应该
执行 tc 等命令）：

```
$ perf record -a -e bpf:* sleep 10
$ perf script
sock_example  6197 [005]   283.980322: bpf:bpf_map_create: map type=ARRAY ufd=4 key=4 val=8 max=256 flags=0
sock_example  6197 [005]   283.980721: bpf:bpf_prog_load: prog=a5ea8fa30ea6849c type=SOCKET_FILTER ufd=5
sock_example  6197 [005]   283.988423: bpf:bpf_prog_get_type: prog=a5ea8fa30ea6849c type=SOCKET_FILTER
sock_example  6197 [005]   283.988443: bpf:bpf_map_lookup_elem: map type=ARRAY ufd=4 key=[06 00 00 00] val=[00 00 00 00 00 00 00 00]
[...]
sock_example  6197 [005]   288.990868: bpf:bpf_map_lookup_elem: map type=ARRAY ufd=4 key=[01 00 00 00] val=[14 00 00 00 00 00 00 00]
     swapper     0 [005]   289.338243: bpf:bpf_prog_put_rcu: prog=a5ea8fa30ea6849c type=SOCKET_FILTER
```

对于 BPF 程序，以上命令会打印出每个程序的 tag。

对于调试，XDP 还有一个 `xdp:xdp_exception` tracepoint，在抛异常的时候触发：

```
$ perf list | grep xdp:
xdp:xdp_exception                                  [Tracepoint event]
```

异常在下面情况下会触发：

* BPF 程序返回一个非法/未知的 XDP action code.
* BPF 程序返回 `XDP_ABORTED`，这表示非优雅的退出（non-graceful exit）
* BPF 程序返回 `XDP_TX`，但发送时发生错误，例如，由于端口没有启用、发送缓冲区已
  满、分配内存失败等等

这两类 tracepoint 也都可以通过 attach BPF 程序，用这个 BPF 程序本身来收集进一步
信息，将结果放到一个 BPF map 或以事件的方式发送到用户空间收集器，例如利用
`bpf_perf_event_output()` 辅助函数。

<a name="tool_misc"></a>

## 2.9 其他（Miscellaneous）

和 `perf` 类似，BPF 程序和 map 占用的内存是算在 `RLIMIT_MEMLOCK` 中的。可以用
`ulimit -l` 查看当前锁定到内存中的页面大小。`setrlimit()` 系统调用的 man page 提
供了进一步的细节。

默认的限制通常导致无法加载复杂的程序或很大的 BPF map，此时 BPF 系统调用会返回
`EPERM` 错误码。这种情况就需要将限制调大，或者用 `ulimit -l unlimited` 来临时解
决。**`RLIMIT_MEMLOCK` 主要是针对非特权用户施加限制**。根据实际场景不同，为特权
用户设置一个较高的阈值通常是可以接受的。

<a name="prog_type"></a>

# 3 程序类型

写作本文时，一共有 **18 种**不同的 BPF 程序类型，本节接下来进一步介绍其中两种和
网络相关的类型，即 XDP BPF 程序和 `tc` BPF 程序。这两种类型的程序在 LLVM、
iproute2 和其他工具中使用的例子已经在前一节“工具链”中介绍过了。本节将关注其架
构、概念和使用案例。

<a name="prog_type_xdp"></a>

## 3.1 XDP

XDP（eXpress Data Path）提供了一个**内核态、高性能、可编程 BPF 包处理框架**（a
framework for BPF that enables high-performance programmable packet processing
in the Linux kernel）。这个框架在软件中最早可以处理包的位置（即网卡驱动收到包的
时刻）运行 BPF 程序。

XDP hook 位于网络驱动的快速路径上，XDP 程序直接从接收缓冲区（receive ring）中将
包拿下来，无需执行任何耗时的操作，例如分配 `skb` 然后将包推送到网络协议栈，或者
将包推送给 GRO 引擎等等。因此，只要有 CPU 资源，XDP BPF 程序就能够在最早的位置执
行处理。

XDP 和 Linux 内核及其基础设施协同工作，这意味着 **XDP 并不会绕过（bypass）内核**
；作为对比，很多完全运行在用户空间的网络框架（例如 DPDK）是绕过内核的。将包留在
内核空间可以带来几方面重要好处：

* XDP 可以**复用所有上游开发的内核网络驱动、用户空间工具，以及其他一些可用的内核
  基础设施**，例如 BPF 辅助函数在调用自身时可以使用系统路由表、socket 等等。
* 因为驻留在内核空间，因此 XDP 在**访问硬件时与内核其他部分有相同的安全模型**。
* **无需跨内核/用户空间边界**，因为正在被处理的包已经在内核中，因此可以灵活地将
  其转发到内核内的其他实体，例如容器的命名空间或内核网络栈自身。Meltdown 和
  Spectre 漏洞尤其与此相关（**Spectre 论文中一个例子就是用 ebpf 实现的**，译者注
  ）。
* 将包从 XDP 送到内核中非常简单，可以**复用内核**中这个健壮、高效、使用广泛的
  TCP/IP **协议栈**，而不是像一些用户态框架一样需要自己维护一个独立的 TCP/IP 协
  议栈。
* 基于 BPF 可以**实现内核的完全可编程**，保持 ABI 的稳定，保持内核的系统调用 ABI
  “永远不会破坏用户空间的兼容性”（never-break-user-space）的保证。而且，**与内核
  模块（modules）方式相比，它还更加安全**，这来源于 BPF 校验器，它能保证内核操作
  的稳定性。
* XDP 轻松地**支持在运行时（runtime）原子地创建（spawn）新程序，而不会导致任何网
  络流量中断**，甚至不需要重启内核/系统。
* XDP 允许对负载进行灵活的结构化（structuring of workloads），然后集成到内核。例
  如，它可以工作在**“不停轮询”（busy polling）或“中断驱动”（interrupt driven）模
  式**。不需要显式地将专门 CPU 分配给 XDP。没有特殊的硬件需求，它也不依赖
  hugepage（大页）。
* XDP **不需要任何第三方内核模块或许可**（licensing）。它是一个长期的架构型解决
  方案（architectural solution），**是 Linux 内核的一个核心组件，而且是由内核社
  区开发**的。
* 主流发行版中，4.8+ 的内核已经内置并启用了 XDP，并**支持主流的 10G 及更高速网络
  驱动**。

作为一个**在驱动中运行 BPF 的框架**，XDP 还保证了**包是线性放置并且可以匹配到单
个 DMA 页面**，这个页面对 BPF 程序来说是可读和可写的。XDP 还提供了额外的 256 字
节 headroom 给 BPF 程序，后者可以利用 `bpf_xdp_adjust_head()` 辅助函数实现自定义
封装头，或者通过 `bpf_xdp_adjust_meta()` 在包前面添加自定义元数据。

下一节会深入介绍 XDP 动作码（action code），BPF 程序会根据返回的动作码来指导驱动
接下来应该对这个包做什么，而且它还使得我们可以原子地替换运行在 XDP 层的程序。XDP
在设计上就是定位于高性能场景的。BPF 允许以“直接包访问”（direct packet access）的
方式访问包中的数据，这意味着**程序直接将数据的指针放到了寄存器中，然后将内容加载
到寄存器，相应地再将内容从寄存器写到包中**。

数据包在 XDP 中的表示形式是 `xdp_buff`，这也是传递给 BPF 程序的结构体（BPF 上下
文）：

```c
struct xdp_buff {
    void *data;
    void *data_end;
    void *data_meta;
    void *data_hard_start;
    struct xdp_rxq_info *rxq;
};
```

`data` 指向页面（page）中包数据的起始位置，从名字可以猜出，`data_end` 执行包数据
的结尾位置。XDP 支持 headroom，因此 `data_hard_start` 指向页面中最大可能的
headroom 开始位置，即，当对包进行封装（加 header）时，`data` 会逐渐向
`data_hard_start` 靠近，这是通过 `bpf_xdp_adjust_head()` 实现的，该辅助函数还支
持解封装（去 header）。

`data_meta` 开始时指向与 `data` 相同的位置，`bpf_xdp_adjust_meta()` 能够将其朝着
`data_hard_start` 移动，这样可以给自定义元数据提供空间，这个空间对内核网
络栈是不可见的，但对 tc BPF 程序可见，因为 tc 需要将它从 XDP 转移到 `skb`。
反之亦然，这个辅助函数也可以将 `data_meta` 移动到离 `data_hard_start` 比较远的位
置，这样就可以达到删除或缩小这个自定义空间的目的。
`data_meta` 还可以单纯用于在尾调用时传递状态，和 tc BPF 程序中用 `skb->cb[]` 控
制块（control block）类似。

这样，我们就可以得到这样的结论，对于 `struct xdp_buff` 中数据包的指针，有：
`data_hard_start` <= `data_meta` <= `data` < `data_end`.

`rxq` 字段指向某些额外的、和每个接收队列相关的元数据：

```c
struct xdp_rxq_info {
    struct net_device *dev;
    u32 queue_index;
    u32 reg_state;
} ____cacheline_aligned;
```

这些元数据是在缓冲区设置时确定的（并不是在 XDP 运行时）。

BPF 程序可以从 netdevice 自身获取 `queue_index` 以及其他信息，例如 `ifindex`。

### BPF 程序返回码

XDP BPF 程序执行结束后会返回一个判决结果（verdict），告诉驱动接下来如何处理这个
包。在系统头文件 `linux/bpf.h` 中列出了所有的判决类型。

```c
enum xdp_action {
    XDP_ABORTED = 0,
    XDP_DROP,
    XDP_PASS,
    XDP_TX,
    XDP_REDIRECT,
};
```

* `XDP_DROP` 表示立即在驱动层将包丢弃。这样可以节省很多资源，对于 DDoS
  mitigation 或通用目的防火墙程序来说这尤其有用。
* `XDP_PASS` 表示允许将这个包送到内核网络栈。同时，当前正在处理这个包的 CPU 会
  **分配一个 `skb`**，做一些初始化，然后将其**送到 GRO 引擎**。这是没有 XDP 时默
  认的包处理行为是一样的。
* `XDP_TX` 是 BPF 程序的一个高效选项，能够在收到包的网卡上直接将包再发送出去。对
  于实现防火墙+负载均衡的程序来说这非常有用，因为这些部署了 BPF 的节点可以作为一
  个 hairpin （发卡模式，从同一个设备进去再出来）模式的负载均衡器集群，将收到的
  包在 XDP BPF 程序中重写（rewrite）之后直接发送回去。
* `XDP_REDIRECT` 与 `XDP_TX` 类似，但是通过另一个网卡将包发出去。另外，
  `XDP_REDIRECT` 还可以将包重定向到一个 BPF cpumap，即，当前执行 XDP 程序的 CPU
  可以将这个包交给某个远端 CPU，由后者将这个包送到更上层的内核栈，当前 CPU 则继
  续在这个网卡执行接收和处理包的任务。这**和 `XDP_PASS` 类似，但当前 CPU 不用去
  做将包送到内核协议栈的准备工作（分配 `skb`，初始化等等），这部分开销还是很大的**。
* `XDP_ABORTED` 表示程序产生异常，其行为和 `XDP_DROP`，但 `XDP_ABORTED` 会经过
  `trace_xdp_exception` tracepoint，因此可以通过 tracing 工具来监控这种非正常行为。

### XDP 使用案例

本节列出了 XDP 的几种主要使用案例。这里列出的并不全，而且考虑到 XDP 和 BPF 的可
编程性和效率，人们能容易地将它们适配到其他领域。

* **DDoS 防御、防火墙**

    XDP BPF 的一个基本特性就是用 `XDP_DROP` 命令驱动将包丢弃，由于这个丢弃的位置
    非常早，因此这种方式可以实现高效的网络策略，平均到每个包的开销非常小（
    per-packet cost）。这对于那些需要处理任何形式的 DDoS 攻击的场景来说是非常理
    想的，而且由于其通用性，使得它能够在 BPF 内实现任何形式的防火墙策略，开销几乎为零，
    例如，作为 standalone 设备（例如通过 `XDP_TX` 清洗流量）；或者广泛部署在节点
    上，保护节点的安全（通过 `XDP_PASS` 或 cpumap `XDP_REDIRECT` 允许“好流量”经
    过）。

    Offloaded XDP 更进一步，将本来就已经很小的 per-packet cost 全部下放到网卡以
    线速（line-rate）进行处理。

* **转发和负载均衡**

    XDP 的另一个主要使用场景是包转发和负载均衡，这是通过 `XDP_TX` 或
    `XDP_REDIRECT` 动作实现的。

    XDP 层运行的 BPF 程序能够任意修改（mangle）数据包，即使是 BPF 辅助函数都能增
    加或减少包的 headroom，这样就可以在将包再次发送出去之前，对包进行任何的封装/解封装。

    利用 `XDP_TX` 能够实现 hairpinned（发卡）模式的负载均衡器，这种均衡器能够
    在接收到包的网卡再次将包发送出去，而 `XDP_REDIRECT` 动作能够将包转发到另一个
    网卡然后发送出去。

    `XDP_REDIRECT` 返回码还可以和 BPF cpumap 一起使用，对那些目标是本机协议栈、
    将由 non-XDP 的远端（remote）CPU 处理的包进行负载均衡。

* **栈前（Pre-stack）过滤/处理**

    除了策略执行，XDP 还可以用于加固内核的网络栈，这是通过 `XDP_DROP` 实现的。
    这意味着，XDP 能够在可能的最早位置丢弃那些与本节点不相关的包，这个过程发生在
    内核网络栈看到这些包之前。例如假如我们已经知道某台节点只接受 TCP 流量，那任
    何 UDP、SCTP 或其他四层流量都可以在发现后立即丢弃。

    这种方式的好处是包不需要再经过各种实体（例如 GRO 引擎、内核的
    flow dissector 以及其他的模块），就可以判断出是否应该丢弃，因此减少了内核的
    受攻击面。正是由于 XDP 的早期处理阶段，这有效地对内核网络栈“假装”这些包根本
    就没被网络设备看到。

    另外，如果内核接收路径上某个潜在 bug 导致 ping of death 之类的场景，那我们能
    够利用 XDP 立即丢弃这些包，而不用重启内核或任何服务。而且由于能够原子地替换
    程序，这种方式甚至都不会导致宿主机的任何流量中断。

    栈前处理的另一个场景是：在内核分配 `skb` 之前，XDP BPF 程序可以对包进行任意
    修改，而且对内核“假装”这个包从网络设备收上来之后就是这样的。对于某些自定义包
    修改（mangling）和封装协议的场景来说比较有用，在这些场景下，包在进入 GRO 聚
    合之前会被修改和解封装，否则 GRO 将无法识别自定义的协议，进而无法执行任何形
    式的聚合。

    XDP 还能够在包的前面 push 元数据（非包内容的数据）。这些元数据对常规的内核栈
    是不可见的（invisible），但能被 GRO 聚合（匹配元数据），稍后可以和 tc ingress BPF 程
    序一起处理，tc BPF 中携带了 `skb` 的某些上下文，例如，设置了某些 skb 字段。

* **流抽样（Flow sampling）和监控**

    XDP 还可以用于包监控、抽样或其他的一些网络分析，例如作为流量路径中间节点
    的一部分；或运行在终端节点上，和前面提到的场景相结合。对于复杂的包分析，XDP
    提供了设施来高效地将网络包（截断的或者是完整的 payload）或自定义元数据 push
    到 perf 提供的一个快速、无锁、per-CPU 内存映射缓冲区，或者是一
    个用户空间应用。

    这还可以用于流分析和监控，对每个流的初始数据进行分析，一旦确定是正常流量，这个流随
    后的流量就会跳过这个监控。感谢 BPF 带来的灵活性，这使得我们可以实现任何形式
    的自定义监控或采用。

XDP BPF 在生产环境使用的一个例子是 Facebook 的 SHIV 和 Droplet 基础设施，实现了
它们的 L4 负载均衡和 DDoS 测量。从基于 netfilter 的 IPV（IP
Virtual Server）迁移到 XDP BPF 使它们的生产基础设施获得了 10x 的性能提升。这方面
的工作最早在 netdev 2.1 大会上做了分享：

* [演讲 Slides](https://www.netdevconf.org/2.1/slides/apr6/zhou-netdev-xdp-2017.pdf)
* [演讲视频](https://youtu.be/YEU2ClcGqts)

另一个例子是 Cloudflare 将 XDP 集成到它们的 DDoS 防御流水线中，替换了原来基于
cBPF 加 iptables 的 `xt_bpf` 模块所做的签名匹配（signature matching）。
基于 iptables 的版本在发生攻击时有严重的性能问题，因此它们考虑了基于用户态、
bypass 内核的一个方案，但这种方案也有自己的一些缺点，并且需要不停轮询（busy poll
）网卡，并且在将某些包重新注入内核协议栈时代价非常高。迁移到 eBPF/XDP 之后，两种
方案的优点都可以利用到，直接在内核中实现了高性能、可编程的包处理过程：

* [Slides](https://www.netdevconf.org/2.1/slides/apr6/bertin_Netdev-XDP.pdf)
* [Video](https://youtu.be/7OuOukmuivg)

### XDP 工作模式

XDP 有三种工作模式，默认是 `native`（原生）模式，当讨论 XDP 时通常隐含的都是指这
种模式。

* **Native XDP**

    默认模式，在这种模式中，XDP BPF 程序直接运行在网络驱动的早期接收路径上（
    early receive path）。大部分广泛使用的 10G 及更高速的网卡都已经支持这种模式
    。

* **Offloaded XDP**

    在这种模式中，XDP BPF 程序直接 offload 到网卡，而不是在主机的 CPU 上执行。
    因此，本来就已经很低的 per-packet 开销完全从主机下放到网卡，能够比运行在
    native XDP 模式取得更高的性能。这种 offload 通常由智能网卡实现，这些网卡有多
    线程、多核流处理器（flow processors），一个位于内核中的 JIT 编译器（
    in-kernel JIT compiler）将 BPF 翻译成网卡的原生指令。

    支持 offloaded XDP 模式的驱动通常也支持 native XDP 模式，因为 BPF 辅助函数可
    能目前还只支持后者。

* **Generic XDP**

    对于还没有实现 native 或 offloaded XDP 的驱动，内核提供了一个 generic XDP 选
    项，这种模式不需要任何驱动改动，因为相应的 XDP 代码运行在网络栈很后面的一个
    位置（a much later point）。

    这种设置主要面向的是用内核的 XDP API 来编写和测试程序的开发者，并且无法达到
    前面两种模式能达到的性能。对于在生产环境使用 XDP，推荐要么选择 native 要么选择
    offloaded 模式。

### 驱动支持

由于 BPF 和 XDP 的特性和驱动支持还在快速发展和变化，因此这里的列表只统计到了
4.17 内核支持的 native 和 offloaded XDP 驱动。

#### 支持 native XDP 的驱动

* **Broadcom**

  * bnxt

* **Cavium**

  * thunderx

* **Intel**

  * ixgbe
  * ixgbevf
  * i40e

* **Mellanox**

  * mlx4
  * mlx5

* **Netronome**

  * nfp

* **Others**

  * tun
  * virtio_net

* **Qlogic**

  * qede

* **Solarflare**

  * sfc （XDP for sfc available via out of tree driver as of kernel 4.17, but
   will be upstreamed soon）

#### 支持 offloaded XDP 的驱动

* **Netronome**

  * nfp （Some BPF helper functions such as retrieving the current CPU number
    will not be available in an offloaded setting）

<a name="prog_type_tc"></a>

## 3.2 tc 

除了 XDP 等类型的程序之外，BPF 还可以用于内核数据路径的 tc (traffic control，流
量控制)层。

### tc 和 XDP BPF 程序的不同

从高层看，tc BPF 程序和 XDP BPF 程序有三点主要不同：

#### 1. 输入上下文

**BPF 的输入上下文（input context）是一个 `sk_buff` 而不是 `xdp_buff`**。当内核
协议栈收到一个包时（说明包通过了 XDP 层），它会分配一个缓冲区，解析包，并存储包
的元数据。表示这个包的结构体就是 `sk_buff`。这个结构体会暴露给 BPF 输入上下文，
因此 tc ingress 层的 BPF 程序就可以利用这些（由协议栈提取的）包的元数据。这些元
数据很有用，但在包达到 tc 的 hook 点之前，**协议栈执行的缓冲区分配、元数据提取和
其他处理等过程也是有开销的**。从定义来看，`xdp_buff` 不需要访问这些元数据，因为
**XDP hook 在协议栈之前就会被调用。这是 XDP 和 tc hook 性能差距的重要原因之一**
。

因此，attach 到 tc BPF hook 的 BPF 程序可以读取 skb 的 `mark`、`pkt_type`、
`protocol`、`priority`、`queue_mapping`、`napi_id`、`cb[]`、`hash`、`tc_classid`
、`tc_index`、vlan 元数据、XDP 层传过来的自定义元数据以及其他信息。
tc BPF 的 BPF 上下文中使用了 `struct __sk_buff`，这个结构体中的所有成员字段都定
义在 `linux/bpf.h` 系统头文件。

通常来说，`sk_buff` 和 `xdp_buff` 完全不同，二者各有优缺点。例如，`sk_buff` 修改
与其关联的元数据（its associated metadata）非常方便，但它包含了大量协议相关的信
息（例如 GSO 相关的状态），这使得无法仅仅通过重新包数据来切换协议（switch
protocols by solely rewriting the packet data）。这是因为**协议栈是基于元数据处
理包的，而不是每次都去读包的内容**。因此，BPF 辅助函数需要额外的转换，并且还要正
确处理 `sk_buff` 内部信息。`xdp_buff` 没有这些问题，因为它所处的阶段非常早，此时
内核还没有分配 `sk_buff`，因此很容易实现各种类型的数据包重写（packet rewrite）。
但是，`xdp_buff` 的缺点是在它这个阶段进行 mangling 的时候，无法利用到 `sk_buff`
元数据。解决这个问题的方式是从 XDP BPF 传递自定义的元数据到 tc BPF。这样，根据使
用场景的不同，可以同时利用这两者 BPF 程序，以达到互补的效果。

#### 2. hook 触发点

tc BPF 程序在网络数据路径上的 ingress 和 egress 点都可以触发；而 **XDP BPF 程序
只能在 ingress 点触发**。

内核两个 hook 点：

1. ingress hook `sch_handle_ingress()`：由 `__netif_receive_skb_core()` 触发
1. egress hook `sch_handle_egress()`：由 `__dev_queue_xmit()` 触发

`__netif_receive_skb_core()` 和 `__dev_queue_xmit()` 是 **data path 的主要接收和
发送函数**，不考虑 XDP 的话（XDP 可能会拦截或修改，导致不经过这两个 hook 点），
每个网络进入或离开系统的网络包都会经过这两个点，从而使得 **tc BPF 程序具备完全可
观测性**。

#### 3. 是否依赖驱动支持

tc BPF 程序不需要驱动做任何改动，因为它们运行在**网络栈通用层**中的 hook 点。因
此，它们**可以 attach 到任何类型的网络设备上**。

##### Ingress

这提供了很好的灵活性，但跟运行在原生 XDP 层的程序相比，性能要差一些。然而，tc
BPF 程序仍然是内核的通用 data path **做完 GRO 之后、且处理任何协议之前** 最早的
处理点。**传统的 iptables 防火墙也是在这里处理的**，例如 iptables PREROUTING 或
nftables ingress hook 或其他数据包包处理过程。

> However, tc BPF programs still come at the earliest point in the generic
> kernel's networking data path after GRO has been run but **before** any
> protocol processing, traditional iptables firewalling such as iptables
> PREROUTING or nftables ingress hooks or other packet processing takes place.

##### Egress

类似的，对于 egress，tc BPF 程序在将包交给驱动之前的最晚的地方（latest point）执
行，这个地方在**传统 iptables 防火墙 hook 之后**（例如 iptables POSTROUTING），
但在**内核 GSO 引擎之前**。

> Likewise on egress, tc BPF programs execute
> at the latest point before handing the packet to the driver itself for
> transmission, meaning **after** traditional iptables firewalling hooks like
> iptables POSTROUTING, but still before handing the packet to the kernel's
> GSO engine.

唯一需要驱动做改动的场景是：将 tc BPF 程序 offload 到网卡。形式通常和 XDP
offload 类似，只是特性列表不同，因为二者的 BPF 输入上下文、辅助函数和返回码（
verdict）不同。

### `cls_bpf` 分类器

运行在 tc 层的 BPF 程序使用的是 `cls_bpf` 分类器。**在 tc 术语中 “BPF 附着点”被
称为“分类器”**，但这个词其实有点误导，因为它少描述了（under-represent）前者可以
做的事情。attachment point 是一个完全可编程的包处理器，不仅能够读取 `skb` 元数据
和包数据，还可以任意 mangle 这两者，最后结束 tc 处理过程，返回一个裁定结果（
verdict）。因此，`cls_bpf` 可以认为是一个**管理和执行 tc BPF 程序的自包含实体**（
self-contained entity）。

`cls_bpf` 可以持有（hold）一个或多个 tc BPF 程序。Cilium 在部署 `cls_bpf` 程序时
，**对于一个给定的 hook 点只会附着一个程序**，并且用的是 `direct-action` 模式。
典型情况下，在传统 tc 方案中，分类器（classifier ）和动作模块（action modules）
之间是分开的，每个分类器可以 attach 多个 action，当匹配到这个分类器时这些 action
就会执行。在现代世界，在软件 data path 中使用 tc 做复杂包处理时这种模型扩展性不好。
考虑到附着到 `cls_bpf` 的 tc BPF 程序
是完全自包含的，因此它们有效地将解析和 action 过程融合到了单个单元（unit）中。得
益于 `cls_bpf` 的 `direct-action` 模式，它只需要返回 tc action 判决结果，然后立即
终止处理流水线。这使得能够在网络 data path 中实现可扩展可编程的包处理，避免动作
的线性迭代。`cls_bpf` 是 tc 层中唯一支持这种快速路径（fast-path）的一个分类器模块。

和 XDP BPF 程序类似，tc BPF 程序能在运行时（runtime）通过 `cls_bpf` 原子地更新，
而不会导致任何网络流量中断，也不用重启服务。

`cls_bpf` 可以附着的 tc ingress 和 egress hook 点都是由一个名为 **`sch_clsact` 的
伪 qdisc** 管理的，它**是 ingress qdisc 的一个超集**（superset），可以无缝替换后
者，因为它既可以管理 ingress tc hook 又可以管理 egress tc hook。对于
`__dev_queue_xmit()` 内的 tc egress hook，需要注意的是这个 hook 并不是在内核的
qdisc root lock 下执行的。因此，ingress 和 egress hook 都是在快速路径中以无锁（
lockless）方式执行的。不管是 ingress 还是 egress，抢占（preemption ）都被关闭，
执行发生在 RCU 读侧（execution happens under RCU read side）。

通常在 egress 的场景下，有很多类型的 qdisc 会 attach 到 netdevice，例如 `sch_mq`,
`sch_fq`, `sch_fq_codel` or `sch_htb`，其中某些是 classful qdiscs，这些 qdisc 包
含 subclasses 因此需要一个对包进行分类的机制，决定将包 demux 到哪里。这个机制是
由调用 `tcf_classify()` 实现的，这个函数会进一步调用 tc 分类器（如果提供了）。在
这种场景下， `cls_bpf` 也可以被 attach 和使用。这种操作通常发生在 qdisc root
lock 下面，因此会面临锁竞争的问题。`sch_clsact` qdisc 的 egress hook 点位于更前
面，没有落入这个锁的范围内，因此完全独立于常规 egress qdisc 而执行。
因此对于 `sch_htb` 这种场景，`sch_clsact` qdisc 可以将繁重的包分类工作放到 tc
BPF 程序，在 qdisc root lock 之外执行，在这些 tc BPF 程序中设置 `skb->mark` 或
`skb->priority` ，因此随后 `sch_htb` 只需要一个简单的映射，没有原来在 root lock
下面昂贵的包分类开销，还减少了锁竞争。

在 `sch_clsact` in combination with `cls_bpf` 场景下支持 Offloaded tc BPF 程序，
在这种场景下，原来加载到智能网卡驱动的 BPF 程序被 JIT，在网卡原生执行。
只有工作在 `direct-action` 模式的 `cls_bpf` 程序支持 offload。
`cls_bpf` 只支持 offload 单个程序，不支持同时 offload 多个程序。另外，只有
ingress hook 支持 offloading BPF 程序。

一个 `cls_bpf` 实例内部可以 hold 多个 tc BPF 程序。如果由多个程序，
`TC_ACT_UNSPEC` 程序返回码就是让继续执行列表中的下一个程序。但这种方式的缺点是：
每个程序都需要解析一遍数据包，性能会下降。

### tc BPF 程序返回码

tc ingress 和 egress hook 共享相同的返回码（动作判决），定义在 `linux/pkt_cls.h`
系统头文件：

```c
#define TC_ACT_UNSPEC         (-1)
#define TC_ACT_OK               0
#define TC_ACT_SHOT             2
#define TC_ACT_STOLEN           4
#define TC_ACT_REDIRECT         7
```

系统头文件中还有一些 `TC_ACT_*` 动作判决，也用在了这两个 hook 中。但是，这些判决
和上面列出的那几个共享相同的语义。这意味着，从 tc BPF 的角度看，
`TC_ACT_OK` 和 `TC_ACT_RECLASSIFY` 有相同的语义，
`TC_ACT_STOLEN`, `TC_ACT_QUEUED` and `TC_ACT_TRAP` 返回码也是类似的情况。因此，
对于这些情况，我们只描述 `TC_ACT_OK` 和 `TC_ACT_STOLEN` 操作码。

#### `TC_ACT_UNSPEC` 和 `TC_ACT_OK`

`TC_ACT_UNSPEC` 表示“未指定的动作”（unspecified action），在三种情况下会用到：

1. attach 了一个 offloaded tc BPF 程序，tc ingress hook 正在运行，被 offload 的
   程序的 `cls_bpf` 表示会返回 `TC_ACT_UNSPEC`
2. 为了在 `cls_bpf` 多程序的情况下，继续下一个 tc BPF 程序。这种情况可以和
   第一种情况中提到的 offloaded tc BPF 程序一起使用，此时第一种情况返回的
  `TC_ACT_UNSPEC` 继续执行下一个没有被 offloaded BPF 程序？
3. `TC_ACT_UNSPEC` 还用于单个程序从场景，只是通知内核继续执行 skb 处理，但不要带
   来任何副作用（without additional side-effects）。

`TC_ACT_UNSPEC` 在某些方面和 `TC_ACT_OK` 非常类似，因为二者都是将 `skb` 向下一个
处理阶段传递，在 ingress 的情况下是传递给内核协议栈的更上层，在 egress 的情况下
是传递给网络设备驱动。唯一的不同是 `TC_ACT_OK` 基于 tc BPF 程序设置的 classid 来
设置 `skb->tc_index`，而 `TC_ACT_UNSPEC` 是通过 tc BPF 程序之外的 BPF 上下文中的
`skb->tc_classid` 设置。

#### `TC_ACT_SHOT` 和 `TC_ACT_STOLEN`

这两个返回码指示内核将包丢弃。这两个返回码很相似，只有少数几个区别：

* `TC_ACT_SHOT` 提示内核 `skb` 是通过 `kfree_skb()` 释放的，并返回
  `NET_XMIT_DROP` 给调用方，作为立即反馈
* `TC_ACT_STOLEN` 通过 `consume_skb()` 释放 `skb`，返回 `NET_XMIT_SUCCESS` 给上
  层假装这个包已经被正确发送了

perf 的丢包监控（drop monitor）是跟踪的 `kfree_skb()`，因此在 `TC_ACT_STOLEN` 的
场景下它无法看到任何丢包统计，因为从语义上说，此时这些 `skb` 是被"consumed" 或
queued 而不是被 dropped。

#### `TC_ACT_REDIRECT`

这个返回码加上 `bpf_redirect()` 辅助函数，允许重定向一个 `skb` 到同一个或另一个
设备的 ingress 或 egress 路径。能够将包注入另一个设备的 ingress 或 egress 路径使
得基于 BPF 的包转发具备了完全的灵活性。对目标网络设备没有额外的要求，只要本身是
一个网络设备就行了，在目标设备上不需要运行 `cls_bpf` 实例或其他限制。

### tc BPF FAQ

本节列出一些经常被问的、与 tc BPF 程序有关的问题。

* **用 `act_bpf` 作为 tc action module 怎么样，现在用的还多吗？**

    不多。虽然对于 tc BPF 程序来说 `cls_bpf` 和 `act_bpf` 有相同的功能
    ，但前者更加灵活，因为它是后者的一个超集（superset）。tc 的工作原理是将 tc
    actions attach 到 tc 分类器。要想实现与 `cls_bpf` 一样的灵活性，`act_bpf` 需要
    被 attach 到 `cls_matchall` 分类器。如名字所示，为了将包传递给 attached tc
    action 去处理，这个分类器会匹配每一个包。相比于工作在 `direct-action` 模式的
    `cls_bpf`，`act_bpf` 这种方式会导致较低的包处理性能。如果 `act_bpf` 用在
    `cls_bpf` or `cls_matchall` 之外的其他分类器，那性能会更差，这是由 tc 分类器的
    操作特性（nature of operation of tc classifiers）决定的。同时，如果分类器 A 未
    匹配，那包会传给分类器 B，B 会重新解析这个包以及重复后面的流量，因此这是一个线
    性过程，在最坏的情况下需要遍历 N 个分类器才能匹配和（在匹配的分类器上）执行
    `act_bpf`。因此，`act_bpf` 从未大规模使用过。另外，和 `cls_bpf` 相比，
    `act_bpf` 也没有提供 tc offload 接口。

* **是否推荐在使用 `cls_bpf` 时选择 `direct-action` 之外的其他模式?**

    不推荐。原因和上面的问题类似，选择其他模式无法应对更加复杂的处理情况。tc BPF
    程序本身已经能以一种高效的方式做任何处理，因此除了 `direct-action` 这个模式
    之外，不需要其他的任何东西了。

* **offloaded `cls_bpf` 和 offloaded XDP 有性能差异吗？**

    没有。二者都是由内核内的同一个编译器 JIT 的，这个编译器负责 offload 到智能网
    卡以及，并且对二者的加载机制是非常相似的。因此，要在 NIC 上原生执行，BPF 程
    序会被翻译成相同的目标指令。

    tc BPF 和 XDP BPF 这两种程序类型有不同的特性集合，因此根据使用场景的不同，你
    可以选择 tc BPF 或者是 XDP BPF，例如，二者的在 offload 场景下的辅助函数可能
    会有差异。

### tc BPF 使用案例

本节列出了 tc BPF 程序的主要使用案例。但要注意，这里列出的并不是全部案例，而且考
虑到 tc BPF 的可编程性和效率，人们很容易对它进行定制化（tailor）然后集成到编排系
统，用来解决特定的问题。XDP 的一些案例可能有重叠，但 tc BPF 和 XDP BPF 大部分情
况下都是互补的，可以单独使用，也可以同时使用，就看哪种情况更适合解决给定的问题了
。

* **为容器落实策略（Policy enforcement）**

    tc BPF 程序适合用来给容器实现安全策略、自定义防火墙或类似的安全工具。在传统方
    式中，容器隔离是通过网络命名空间时实现的，veth pair 的一端连接到宿主机的初始命
    名空间，另一端连接到容器的命名空间。因为 veth pair 的
    一端移动到了容器的命名空间，而另一端还留在宿主机上（默认命名空间），容器所有的
    网络流量都需要经过主机端的 veth 设备，因此可以在这个 veth 设备的 tc ingress 和
    egress hook 点 attach tc BPF 程序。目标地址是容器的网络流量会经过主机端的 veth
    的 tc egress hook，而从容器出来的网络流量会经过主机端的 veth 的 tc ingress
    hook。

    对于像 veth 这样的虚拟设备，XDP 在这种场景下是不合适的，因为内核在这里只操作
    `skb`，而通用 XDP 有几个限制，导致无法操作克隆的 `skb`。而克隆 `skb` 在 TCP/IP
    协议栈中用的非常多，目的是持有（hold）准备重传的数据片（data segments），而通
    用 XDP hook 在这种情况下回被直接绕过。另外，generic XDP 需要顺序化（linearize ）整个
    `skb` 导致严重的性能下降。相比之下， tc BPF 非常灵活，因为设计中它就是工作在接
    收 `skb` 格式的输入上下文中，因此没有 generic XDP 遇到的那些问题。

* **转发和负载均衡**

    转发和负载均衡的使用场景和 XDP 很类似，只是目标更多的是在东西向容器流量而不是
    南北向（虽然两者都可以用于东西向或南北向场景）。XDP 只能在 ingress 方向使用，
    tc BPF 程序还可以在 egress 方向使用，例如，可以在初始命名空间内（宿主机上的
    veth 设备上），通过 BPF 对容器的 egress 流量同时做地址转化（NAT）和负载均衡，
    整个过程对容器是透明的。由于在内核网络栈的实现中，egress 流量已经是 `sk_buff`
    形式的了，因此很适合 tc BPF 对其进行重写（rewrite）和重定向（redirect）。
    使用 `bpf_redirect()` 辅助函数，BPF 就可以接管转发逻辑，将包推送到另一个网络设
    备的 ingress 或 egress 路径上。因此，有了 tc BPF 程序实现的转发网格（
    forwarding fabric），网桥设备都可以不用了。

* **流抽样（Flow sampling）、监控**

    和 XDP 类似，可以通过高性能无锁 per-CPU 内存映射 perf 环形缓冲区（ring buffer
    ）实现流抽样（flow sampling）和监控，在这种场景下，BPF 程序能够将自定义数据、
    全部或截断的包内容或者二者同时推送到一个用户空间应用。在 tc BPF 程序中这是通过
    `bpf_skb_event_output()` BPF 辅助函数实现的，它和 `bpf_xdp_event_output()` 有相
    同的函数签名和语义。

    考虑到 tc BPF 程序可以同时 attach 到 ingress 和 egress，而 XDP 只能 attach 到
    ingress，另外，这两个 hook 都在（通用）网络栈的更低层，这使得可以监控每台节点
    的所有双向网络流量。这和 tcpdump 和 Wireshark 使用的 cBPF 比较相关，但是，不
    需要克隆 `skb`，而且因为其可编程性而更加灵活，例如。BPF 能够在内核中完成聚合
    ，而不用将所有数据推送到用户空间；也可以对每个放到 ring buffer 的包添加自定义
    的 annotations。Cilium 大量使用了后者，对被 drop 的包进一步 annotate，关联到
    容器标签以及 drop 的原因（例如因为违反了安全策略），提供了更丰富的信息。

* **包调度器预处理**（Packet scheduler pre-processing）

    `sch_clsact`'s egress hook 被 `sch_handle_egress()` 调用，在获得内核的 qdisc
    root lock 之前执行，因此 tc BPF 程序可以在包被发送到一个真实的 full blown qdis
    （例如 `sch_htb`）之前，用来执行包分类和 mangling 等所有这些高开销工作。
    这种 `sch_clsact` 和后面的发送阶段的真实 qdisc（例如 `sch_htb`） 之间的交互，
    能够减少发送时的锁竞争，因为 `sch_clsact` 的 egress hook 是在无锁的上下文中执行的。

同时使用 tc BPF 和 XDP BPF 程序的一个具体例子是 Cilium。Cilium 是一个开源软件，
透明地对（K8S 这样的容器编排平台中的）容器之间的网络连接进行安全保护，工作在
L3/L4/L7。Cilium 的核心基于 BPF，用来实现安全策略、负载均衡和监控。

* [Slides](https://www.slideshare.net/ThomasGraf5/dockercon-2017-cilium-network-and-application-security-with-bpf-and-xdp)
* [Video](https://youtu.be/ilKlmTDdFgk)
* [Github](https://github.com/cilium/cilium)

### 驱动支持

由于 tc BPF 程序是从内核网络栈而不是直接从驱动触发的，因此它们不需要任何额外的驱
动改动，因此可以运行在任何网络设备之上。唯一的例外是当需要将 tc BPF 程序 offload
到网卡时。

#### 支持 offload tc BPF 程序的驱动

* **Netronome**

  * nfp
