---
layout    : post
title     : "[译] LLVM eBPF 汇编编程（2020）"
date      : 2021-08-15
lastupdate: 2021-08-15
categories: bpf assembly
---

### 译者序

本文翻译自 2020 年 Quentin Monnet 的一篇英文博客：
[eBPF assembly with LLVM](https://qmonnet.github.io/whirl-offload/2020/04/12/llvm-ebpf-asm/)。
Quentin Monnet 是 Cilium 开发者之一，此前也在从事网络、eBPF 相关的开发。

文章介绍了如何直接**<mark>基于 LLVM eBPF 汇编开发 BPF 程序</mark>**，虽然给出的
两个例子极其简单，但开发更大的程序，流程也是类似的。**<mark>为什么不用 C，而用汇编</mark>**
这么不友好的编程方式呢？至少有两个特殊场景：

1. **<mark>测试特定的 eBPF 指令流</mark>**
1. 对程序的某个特定部分进行**<mark>深度调优</mark>**

原文历时（开头之后拖延）了好几年，因此文中存在一些（文件名等）前后不一致之处，翻译时已经改正；
另外，译文基于 clang/llvm 10.0 验证了其中的每个步骤，因此代码、输出等与原文不完全一致。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 1 引言

## 1.1 主流开发方式：从 C 代码直接生成 eBPF 字节码

eBPF 相比于 cBPF（经典 BPF）的优势之一是：Clang/LLVM 为它提供了一个**<mark>编译后端</mark>**，
能从 C 源码直接生成 eBPF 字节码（bytecode）。（写作本文时，GCC 也提供了一个类似
的后端，但各方面都没有 Clang/LLVM 完善，因此后者仍然是**<mark>生成 eBPF 字节码
的最佳参考工具</mark>**）。

将 C 代码编译成 eBPF 目标文件非常有用，因为
**<mark>直接用字节码编写高级程序</mark>**是非常耗时的。此外，截至本文写作时，
还无法直接编写字节码程序来使用 [CO-RE](https://facebookmicrosites.github.io/bpf/blog/2020/02/19/bpf-portability-and-co-re.html) 
等复杂特性。

> [<mark>(译) BPF 可移植性和 CO-RE（一次编译，到处运行）</mark>（Facebook，2020）]({ % link _posts/2021-03-12-bpf-portability-and-co-re-zh.md %})。
> 译注。

因此，Clang 和 LLVM 仍然是 eBPF 工作流不可或缺的部分。

## 1.2 特殊场景需求：eBPF 汇编编程更合适

但是，C 方式不适用于某些特殊的场景，例如：

1. 只是想**<mark>测试特定的 eBPF 指令流</mark>**
1. 对程序的某个特定部分进行**<mark>深度调优</mark>**

在这些情况下，就需要直接编写或修改 eBFP 汇编程序。

## 1.3 几种 eBPF 汇编编程方式

1. **<mark>直接编写 eBPF 字节码程序</mark>**。也就是编写**<mark>可直接加载运行</mark>**的
   二进制 eBPF 程序，

   * 这肯定是可行的，但过程非常冗长无聊，对开发者极其不友好。
   * 此外，为**<mark>保证与 tc 等工具的兼容</mark>**，还要将写好的程序转换成目标文件（object file），因此工作量又多了一些。

2. **<mark>直接用 eBPF 汇编语言编写</mark>**，然后用**<mark>专门的汇编器</mark>**
  （例如 [`ebpf_asm`](https://github.com/solarflarecom/ebpf_asm)）将其汇编（assemble）成字节码。

   * 相比字节码（二进制），汇编语言（文本）至少可读性还是好很多的。

3. 用 LLVM 将 C 编译成 eBPF 汇编，然后**<mark>手动修改生成的汇编程序</mark>**，
   最后再将其汇编（assemble）成字节码放到对象文件。

4. 在 C 中插入内联汇编，然后统一用 clang/llvm 编译。

以上几种方式 **<mark>Clang/LLVM 都支持</mark>**！先用可读性比较好的方式写，
然后再将其汇编（assembling）成另字节码程序。此外，甚至能 dump 对象文件中包含的程序。

本文将会展示第三种和第四种方式，第二种可以认为是第三种的更加彻底版，开发的流程
、步骤等已经包括在第三种了。

# 2 Clang/LLVM 编译 eBPF 基础

在开始汇编编程之前，先来熟悉一下 clang/llvm 将 C 程序编译成 eBPF 程序的过程。

## 2.1 将 C 程序编译成 BPF 目标文件

下面是个 **<mark>eBPF 程序</mark>**：没做任何事情，直接返回零，

```shell
$ cat bpf.c
int func() {
    return 0;
}
```

如下命令可以将其编译成**<mark>对象文件（目标文件）</mark>**：

```shell
# 注意 target 类型指定为 `bpf`
$ clang -target bpf -Wall -O2 -c bpf.c -o bpf.o
```

> 某些**<mark>复杂的程序</mark>**可能需要用下面的命令来编译：
>
> ```shell
> $ clang -O2 -emit-llvm -c bpf.c -o - | \
> 	llc -march=bpf -mcpu=probe -filetype=obj -o bpf.o
> ```

以上命令会将 C 源码**<mark>编译成字节码</mark>**，然后生成一个 **<mark>ELF 格式</mark>**的目标文件。

## 1.2 查看 ELF 文件中的 eBPF 字节码

**<mark>默认情况下，代码位于 ELF 的 <code>.text</code> 区域</mark>**（section）：

```shell
$ readelf -x .text bpf.o
Hex dump of section '.text':
  0x00000000 b7000000 00000000 95000000 00000000 ................
```

这就是**<mark>编译生成的字节码</mark>**！

以上字节码包含了**<mark>两条 eBPF 指令</mark>**：

```
b7 0 0 0000 00000000    # r0 = 0
95 0 0 0000 00000000    # exit and return r0
```

如果对 **<mark>eBPF 汇编语法</mark>**不熟悉，可参考：

1. iovisor/bpf-docs 中的[简洁文档](https://github.com/iovisor/bpf-docs/blob/master/eBPF.md)
2. 更详细的内核文档 [<mark>networking/filter.txt</mark>](https://www.kernel.org/doc/Documentation/networking/filter.txt)。

有了以上基础，接下来看如何开发 eBPF 汇编程序。

# 3 方式一：C 生成 eBPF 汇编 + 手工修改汇编

本节需要 Clang/LLVM 6.0+ 版本（`clang -v`）。

> 译文基于 10.0，结果与原文略有差异。

C 源码：

```shell
$ cat bpf.c
int func() {
	return 0;
}
```

## 3.1 将 C 编译成 eBPF 汇编（`clang`）

其实前面已经看到了，与将普通 C 程序编译成汇编类似，只是这里指定 `target` 类型是 `bpf`
（`bpf` target 与默认 target 的不同，见 Cilium 文档 [BPF and XDP Reference Guide](http://docs.cilium.io/en/latest/bpf/#llvm) ）：

> [<mark>(译） Cilium：BPF 和 XDP 参考指南（2021）</mark>]({% link _posts/2021-07-18-cilium-bpf-xdp-reference-guide-zh.md %})。
> 译注。

```shell
$ clang -target bpf -S -o bpf.s bpf.c
```

查看生成的汇编代码：

```shell
$ cat bpf.s
        .text
        .file   "bpf.c"
        .globl  func                    # -- Begin function func
        .p2align        3
        .type   func,@function
func:                                   # @func
# %bb.0:
        r0 = 0
        exit
.Lfunc_end0:
        .size   func, .Lfunc_end0-func
                                        # -- End function
        .addrsig
```

接下来就可以修改这段汇编代码了。

## 3.2 手工修改汇编程序

因为汇编程序是**<mark>文本文件</mark>**，因此编辑起来很容易。
作为练手，我们在程序最后加上一行汇编指令 `r0 = 3`：

```shell
$ cat bpf.s
        .text
        .file   "bpf.c"
        .globl  func                    # -- Begin function func
        .p2align        3
        .type   func,@function
func:                                   # @func
# %bb.0:
        r0 = 0
        exit
        r0 = 3                          # -- 这行是我们手动加的
.Lfunc_end0:
        .size   func, .Lfunc_end0-func
                                        # -- End function
        .addrsig
```

这行放在了 `exit` 之后，因此实际上没任何作用。

## 3.3 将汇编程序 assemble 成 ELF 对象文件（`llvm-mc`）

接下来将 bpf.s 汇编（assemble）成包含**<mark>字节码</mark>**的 ELF 对象文件。这
里需要用到 LLVM 自带的与**<mark>机器码</mark>**（machine code，mc）打交道的工具
`llvm-mc`：

```shell
$ llvm-mc -triple bpf -filetype=obj -o bpf.o bpf.s
```

bpf.o 就是生成的 ELF 文件！

## 3.4 查看对象文件中的 eBPF 字节码（`readelf`）

查看 bpf.o 中的**<mark>字节码</mark>**：

```shell
$ readelf -x .text bpf.o

Hex dump of section '.text':
  0x00000000 b7000000 00000000 95000000 00000000 ................
  0x00000010 b7000000 03000000                   ........
```

看到和之前相比，

* 第一行（包含前两条指令）一样，
* 第二行是新多出来的（对应的正是我们新加的一行汇编指令），作用：将常量 `3` load 到寄存器 `r0` 中。

至此，我们已经**<mark>成功地修改了指令流</mark>**。接下来就可以用 `bpftool` 之
类的工具将这个程序加载到内核，任务完成！

## 3.5 以更加人类可读的方式查看 eBPF 字节码（`llvm-objdump -d`）

LLVM 还能以人类可读的方式 dump eBPF 对象文件中的指令，这里就要用到
`llvm-objdump`：

```shell
# -d           : alias for --disassemble
# --disassemble: display assembler mnemonics for the machine instructions
$ llvm-objdump -d bpf.o
bpf.o:  file format ELF64-BPF

Disassembly of section .text:

0000000000000000 func:
       0:       b7 00 00 00 00 00 00 00 r0 = 0
       1:       95 00 00 00 00 00 00 00 exit
       2:       b7 00 00 00 03 00 00 00 r0 = 3
```

最后一列显示了**<mark>对应的 LLVM 使用的汇编指令</mark>**（也是前面我们手工编辑时使用的 eBPF 指令）。

## 3.6 编译时嵌入调试符号或 C 源码（`clang -g` + `llvm-objdump -S`）

除了字节码和汇编指令，LLVM 还能将**<mark>调试信息</mark>**（debug symbols）嵌入到对象文件，
更具体说就是能**<mark>在字节码旁边同时显示对应的 C 源码</mark>**，对调试非常有用，也是
**<mark>观察 C 指令如何映射到 eBPF 指令</mark>**的好机会。

在 clang 编译时加上 `-g` 参数：

```shell
# -g: generate debug information.
$ clang -target bpf -g -S -o bpf.s bpf.c
$ llvm-mc -triple bpf -filetype=obj -o bpf.o bpf.s
```

```shell
# -S      : alias for --source
# --source: display source inlined with disassembly. Implies disassemble object
$ llvm-objdump -S bpf.o
Disassembly of section .text:

0000000000000000 func:
; int func() {
       0:       b7 00 00 00 00 00 00 00 r0 = 0
;     return 0;
       1:       95 00 00 00 00 00 00 00 exit
```

注意这里**<mark>用的是 -S（显示源码），不是 -d（反汇编）</mark>**。

# 4 方式二：内联汇编（inline assembly）

接下来看另一种生成和编译 eBPF 汇编的方式：**<mark>直接在 C 程序中嵌入 eBPF 汇编</mark>**。

## 4.1 C 内联汇编示例

下面是个非常简单的例子，受 Cilium 文档 [BPF and XDP Reference Guide](http://docs.cilium.io/en/latest/bpf/#llvm) 的启发：

> [<mark>(译） Cilium：BPF 和 XDP 参考指南（2021）</mark>]({% link _posts/2021-07-18-cilium-bpf-xdp-reference-guide-zh.md %})。
> 译注。

```shell
$ cat inline_asm.c
int func() {
    unsigned long long foobar = 2, r3 = 3, *foobar_addr = &foobar;

    asm volatile("lock *(u64 *)(%0+0) += %1" : // 等价于：foobar += r3
         "=r"(foobar_addr) :
         "r"(r3), "0"(foobar_addr));

    return foobar;
}
```

**<mark>关键字</mark>** `asm` 用于插入汇编代码。

## 4.2 编译及查看生成的字节码

```
$ clang -target bpf -Wall -O2 -c inline_asm.c -o inline_asm.o
```

反汇编：

```shell
$ llvm-objdump -d inline_asm.o
Disassembly of section .text:

0000000000000000 func:
       0:       b7 01 00 00 02 00 00 00 r1 = 2
       1:       7b 1a f8 ff 00 00 00 00 *(u64 *)(r10 - 8) = r1
       2:       b7 01 00 00 03 00 00 00 r1 = 3
       3:       bf a2 00 00 00 00 00 00 r2 = r10
       4:       07 02 00 00 f8 ff ff ff r2 += -8
       5:       db 12 00 00 00 00 00 00 lock *(u64 *)(r2 + 0) += r1
       6:       79 a0 f8 ff 00 00 00 00 r0 = *(u64 *)(r10 - 8)
       7:       95 00 00 00 00 00 00 00 exit
```

对应到最后一列的汇编，大家应该大致能看懂。

## 4.3 小结

这种方式的好处是：源码仍然是 C，因此**<mark>无需像前一种方式那样必须手动执行编译（
compile）和汇编（assemble）两个分开的过程</mark>**。

# 5 结束语

本文通过两个极简的例子展示了两种 eBPF 汇编编程方式：

1. 手动生成并修改一段特定的指令流
1. 在 C 中插入内联汇编

这两种方式我认为都是有用的，比如在 Netronome，我们经常用前一种方式做单元测试，
检查 nfp 驱动中的 eBPF hw offload 特性。

LLVM 支持编写任意的 eBPF 汇编程序（但提醒一下：编译能通过是一回事，能不能通过校验器是另一回事）。
有兴趣自己试试吧！
