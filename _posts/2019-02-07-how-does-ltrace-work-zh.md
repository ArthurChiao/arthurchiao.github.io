---
layout: post
title:  "[译] ltrace 是如何工作的"
date:   2019-02-07
categories: ltrace system-call
---

### 译者序

本文翻译自2016年的一篇英文博客 [How Does ltrace Work
](https://blog.packagecloud.io/eng/2016/03/14/how-does-ltrace-work/)
。**如果能看懂英文，我建议你阅读原文，或者和本文对照看。**

阅读本文之前，强烈建议先阅读下面几篇之前的文章：

1. [(译) Linux系统调用权威指南]({% link _posts/2019-01-30-system-call-definitive-guide-zh.md %})
1. [(译) strace 是如何工作的]({% link _posts/2019-02-02-how-does-strace-work-zh.md %})

其中包含了本文所需的部分预备知识。

以下是译文。

----

### 太长不读（TL;DR）

本文介绍 `ltrace` 内部是如何工作的，和我们的前一篇文章 [strace 是如何工作的
]({% link _posts/2019-02-02-how-does-strace-work-zh.md %}) 是兄弟篇。

文章首先会对比 `ltrace` 和 `strace` 的异同；然后介绍 `ltrace` 是如何基于
`ptrace` 系统调用获取被跟踪进程的**库函数调用**信息的。

## 1 `ltrace` 和 `strace`

`strace` 是一个系统调用，也是一个信号跟踪器（signal tracer），主要用于跟踪系统
调用，打印系统调用的参数、返回值、时间戳等很多信息。它也可以跟踪和打印进程收到的
信号。

我们在前一篇文章[strace 是如何工作的]({% link _posts/2019-02-02-how-does-strace-work-zh.md %})
中介绍过， `strace` 内部是基于 `ptrace` 系统调用的。

`ltrace`是一个**（函数）库调用跟踪器**（libraray call tracer），顾名思义，主要用
于跟踪程序的函数库调用信息。另外，它也可以像 `strace` 一样跟踪系统调用和信号。它
的命令行参数和 `strace` 很相似。

`ltrace` 也是基于 `ptrace`，但跟踪库函数和跟踪系统调用还是有很大差别的，这就是为
什么会有 `ltrace`的原因。

在介绍细节之前，我们需要先了解几个概念。

## 2 重要概念

### 2.1 程序调用函数库的流程

共享库可以被加载到任意地址。这意味着，共享库内的函数地址只有在运行时加载以后才能确定。
即使重复执行同一程序，加载同一动态库，库内的函数地址也是不同的。

那么，程序是如何调用地址未知的函数的呢？

简短版的回答是：**二进制格式**、**操作系统**，以及**加载器**。在Linux上，这是一
支程序和动态加载器之间的曼妙舞蹈。

下面是详细版的回答。

Linux 程序使用 [ELF binary format](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format)，它提供了
许多特性。出于本文目的，我们这里只介绍两个：

* 过程链接表（Procedire Linkage Table，PLT）
* 全局偏置表（Global Offset Table，GOT）

库函数在 PLT 里都有一组对应的汇编指令，通常称作 trampoline，在函数被调用的时候执行。 

PLT trampoline 都遵循类似的格式，下面是一个例子：

```c
PLT1: jmp *name1@GOTPCREL(%rip)
      pushq $index1
      jmp .PLT0
```


第一行代码跳转到一个地址，这个地址的值存储在 GOT 中。 

GOT 存储了绝对地址。这些地址在程序启动时初始化，指向 PLT `pushq` 指令所在的地址
（第二行代码）。

第三行 `pushq $index1` 为动态连接器准备一些数据，然后通过 `jmp .PLT0` 跳转到另一
段代码，后者会进而调用动态链接器。

动态链接器通过 `$index1` 和其他一些数据来判断程序想调用的是哪个库函数，然后定位
到函数地址，并将其写入 GOT，覆盖之前初始化时的默认值。

当后面再次调用到这个函数时，就会直接找到函数地址，而不需再经过以上的动态链接器查
找过程。

想更详细地了解这个过程，可以查看 [System V AMD64
ABI](http://www.x86-64.org/documentation/abi.pdf)，从 75 页开始。

总结起来：

1. 程序加载到内存时，程序和每个动态共享库（例如DSO）通过 PLT 和 GOT 映射到内存
1. 程序开始执行时，动态共享库里的函数的内存地址是未知的，因为动态库可以被加载到程序地址空间的任意地址
1. 首次执行到一个函数的时候，执行过程转到函数的 PLT，里面是一些汇编代码（trampoline）
1. trampoline 组织数据，然后调用动态链接器
1. 动态链接器通过 PLT 准备的数据找到函数地址
1. 将地址写入 GOT 表，然后执行转到该函数
1. 后面再次调用到这个函数时，不再经过动态链接器，因为 GOT 里已经存储了函数地址，PLT 可以直接调用

为了能够 hook 库函数调用，`ltrace` 必须将它自己插入以上的流程。它的实现方式：
**在函数的 PLT 表项里设置一个软件断点**。

### 2.2 断点的工作原理

断点（breakpoint）是使函数在特定的地方停止执行，然后让另一个程序（例如调试器，跟踪器）介入的方式。

有两类断点：硬件断点和软件断点。

硬件断点是 CPU 特性，数量比较有限。在 amd64 CPU上有 4 个特殊的寄存器，可以设置让程序停止执行的地址。

软件断点通过特殊的汇编指令触发，数量不受限制。在 amd64 CPU上，通过如下汇编指令触发软件断点：

```c
int $3
```

这条指令会使处理器触发编号为 3 的中断，这个中断是专门为调试器准备的，
Linux 内核有对应的中断处理函数，在执行的时候会向被调试程序发送一个 `SIGTRAP` 信号。

回忆我们前一篇讲 `strace` 的[文章](https://blog.packagecloud.io/eng/2016/03/14/how-does-ltrace-work/)，
里面提到跟踪器可以通过 `ptrace` 系统调用 attach 到程序。
所有发送给被跟踪程序的信号会使得程序暂停执行，然后通知跟踪程序。

因此：

1. 程序执行到 `int $3`，执行过程被暂停
1. 触发内核中 3 号中断对应的中断处理函数
1. 中断处理函数经过一些调用，最终向程序发送一个 `SIGTRAP` 信号
1. 如果程序已经被其他（跟踪）程序通过 `ptrace` attach 了，那后者会收到一个 `SIGTRAP` 信号

这和前一篇文章介绍的使用 `PTRACE_SYSCALL` 参数的过程类似。

那么，跟踪器或调试器是**如何将这个 `int $3` 指令插入程序的呢**？

### 2.3 在程序中插入断点的实现

`ptrace` + `PTRACE_POKETEXT`：修改运行程序的内存。

`ptrace` 系统调用接受一个 `request` 参数，当设置为 `PTRACE_POKETEXT` 时，**允许
修改运行中程序的内存**。

调试器和跟踪器可以使用 `PTRACE_POKETEXT` 将 `int $3` 指令在程序运行的时候写到程
序的特定内存。**这就是断点如何设置的。**

## 3 `ltrace`

将以上讲到的所有内容结合起来就得到了 `ltrace`： **`ltrace` = `ptrace` +
`PTRACE_POKETEXT` + `int $3`**

`ltrace` 的工作原理：

1. 通过 `ptrace` attach 到运行中的程序
1. 定位程序的 PLT
1. 通过 `ptrace` 设置 `PTRACE_POKETEXT` 选项，用 `int $3` 指令覆盖库函数的 PLT 中的汇编 trampoline
1. 恢复程序执行

接下来当调用到库函数时，程序会执行 `int $3` 指令：

1. 程序执行 `int $3` 指令
1. 对应的内核中断处理函数开始执行
1. 内核通知 `ltrace` 被跟踪进程有 `SIGTRAP` 信号待处理
1. `ltrace` 查看程序在调用哪个库函数，打印函数名、参数、时间戳等参数

最后，`ltrace` 必须将插入到 PLT 的代码 `int $3` 替换为原来的代码，然后程序就可以
恢复正常执行了：

1. `ltrace` 使用 `PTRACE_POKETEXT` 将 `int $3` 替换原来的指令
1. 程序恢复执行
1. 程序恢复正常执行，因为插入的断点被移除了

这就是 `ltrace` 如何跟踪库函数调用的。

## 4 结束语

`ptrace` 系统调用非常强大，可以跟踪系统调用、重写运行中程序的内存、读取运行中程
序的寄存器等等。

`strace` 和 `ltrace` 都使用 `PTRACE_SYSCALL` 跟踪系统调用。两者的大致工作过程类
似：为被跟踪程序触发 `SIGTRAP` 信号，暂停执行，通知跟踪程序（`strace` 或
`ltrace`），然后跟踪程序被“唤醒”，分析被暂停的程序。

`ltrace` 还会通过`PTRACE_POKETEXT`重写程序内存，以便通过特殊指令中断程序的执行。

想了解更多 `PTRACE_SYSCALL` 的内部细节，可以阅读我们前一篇介绍 `strace` 的[博客
](https://blog.packagecloud.io/eng/2016/03/14/how-does-ltrace-work/)。

## 5 我们的相关文章

如果对本文感兴趣，那么你可能对我们的以下文章也感兴趣：

1. [(译) Linux系统调用权威指南]({% link _posts/2019-01-30-system-call-definitive-guide-zh.md %})
1. [(译) strace 是如何工作的]({% link _posts/2019-02-02-how-does-strace-work-zh.md %})
