---
layout: post
title:  "[译] strace 是如何工作的"
date:   2019-02-02
categories: strace system-call
---

### 译者序

本文翻译自2016年的一篇英文博客 [How Does strace Work
](https://blog.packagecloud.io/eng/2016/02/29/how-does-strace-work/)
。**如果能看懂英文，我建议你阅读原文，或者和本文对照看。**

阅读本文之前，强烈建议先阅读这篇之前的文章：

1. [(译) Linux系统调用权威指南]({% link _posts/2019-01-30-system-call-definitive-guide-zh.md %})

其中包含了本文所需的部分预备知识。

以下是译文。

----

### 太长不读（TL;DR）

本文介绍 `strace` 内部是如何工作的。我们会研究 `strace` 工具内部所依赖的
`ptrace` 系统调用，对其 API 层及内部实现进行分析，以弄清楚 `strace`
是如何获取被跟踪进程的（系统调用相关的）详细信息的。

## 1 `ptrace` 是什么

`ptrace`是一个系统调用，可以用来：

1. 跟踪（其他）系统调用
2. 读写内存和寄存器
3. 控制（manipulate）被跟踪进程的信号传送（signal delivery）

以上可以看出，`ptrace` 在跟踪和控制程序方面非常有用。`strace`、
[GDB](https://www.gnu.org/software/gdb/)等工具内部都用到了它。

可以通过它的 [man page](http://man7.org/linux/man-pages/man2/ptrace.2.html) 查看更多信息。

## 2 跟踪过程

本文将使用如下两个术语：

1. tracer：跟踪（其他程序的）程序
1. tracee：被跟踪程序

tracer 跟踪 tracee的过程：

首先，**attach 到 tracee 进程**：调用 `ptrace`，带 `PTRACE_ATTACH` 及 tracee 进程ID 作为参数。

之后当 **tracee 运行到系统调用函数时就会被内核暂停**；对 tracer 来说，就像
tracee 收到了`SIGTRAP` 信号而停下来一样。接下来 tracer 就可以查看这次系统调
用的参数，打印相关的信息。

然后，**恢复 tracee 执行**：再次调用 `ptrace`，带 `PTRACE_SYSCALL` 和 tracee 进程 ID。
tracee会继续运行，进入到系统调用；在退出系统调用之前，再次被内核暂停。

以上“暂停-采集-恢复执行”过程不断重复，tracer 就可以获取每次系统调用的信息，打印
出参数、返回值、时间等等。

以上就是 ptrace 跟踪其他系统调用的大致过程，接下来看它在内核中具体是如何工作的。

## 3 内核实现

内核的 `ptrace` 系统调用是一个很好的起点。接下来的代码会基于内核 3.13，并提供
github 的代码连接。

整个 `ptrace` 系统的代码见
[kernel/ptrace.c](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L1036)。

### 3.1 `PTRACE_ATTACH` 代码流程

首先看 `PTRACE_ATTACH` 干了什么事情。

[检查 `request` 参数](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L1055-L1064)，然后调用 `ptrace_attach`：

```c
if (request == PTRACE_ATTACH || request == PTRACE_SEIZE) {
  ret = ptrace_attach(child, request, addr, data);
  /*
   * Some architectures need to do book-keeping after
   * a ptrace attach.
   */
  if (!ret)
    arch_ptrace_attach(child);
  goto out_put_task_struct;
}
```

#### 3.1.1 `ptrace_attach`

这个[函数](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L279)做的事情：

1. 初始化一个 `ptrace` `flags` 变量
1. 确保 tracee 不是内核线程
1. 确保 tracee 不是当前进程的一个线程
1. 通过 `__ptrace_may_access` 做一些安全检查

然后，将 `flags` 赋值给 tracee 进程的内核结构体变量上（`struct task_struct
*task`），并[停止
tracee](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L339)。

在我们的例子中，这个 `flags` 的值为 `PT_PTRACED`。

函数结束后，执行回到 `ptrace`。

#### 3.1.2 从 `ptrace_attach` 返回到 `ptrace`

接下来，`ptrace` 调用
[ptrace_check_attach](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L1066)
来检查是否可以操作 tracee 了。

最后， ptrace 调用CPU相关的
[arch_ptrace](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L1071)
函数。对于 x86 平台，这个函数在 `arch/x86/kernel/ptrace.c`
中，见[这里](https://github.com/torvalds/linux/blob/v3.13/arch/x86/kernel/ptrace.c#L821)。如果你看完了代码里巨长的
`switch` 语句，会发现并没有对应 `PTRACE_ATTACH` 的 case，这说明这种 case 走的是
default 分支。default 分支做的事情就是调用 `ptrace_request` 函数，然后回到
`ptrace` 代码。

[ptrace_request](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L803)
也没有对 `PTRACE_ATTACH` 做特殊处理，接下来的代码就是一路返回到 `ptrace`
系统调用，然后再从 `ptrace` 函数返回。

以上就是 `PTRACE_ATTACH` 的工作流程。接下来看 `PTRACE_SYSCALL`。

### 3.2 `PTRACE_SYSCALL` 代码流程

首先会调用
[ptrace_check_attach](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L1066)
以确保可以对 tracee 进程进行操作。

接下来和attach 部分类似，调用 `arch_ptrace`
函数，里面包含CPU相关的代码。同样的， `arch_ptrace` 也没有什么需要为
`PTRACE_SYSCALL` 做的，直接调用到 `ptrace_request`。

到目前为止，流程和 attach 过程都是类似的，但接下来就不一样了。在
[ptrace_request](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L982-L984)
中，针对 `PTRACE_SYSCALL`，调用了 `ptrace_resume` 函数。

[该函数](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L720)首先给
tracee 的内核结构体变量 `task` 设置 `TIF_SYSCALL_TRACE` flag。

接下来检查几种可能的状态（因为其他函数可能也在调用 `ptrace_resume`），最后
tracee
[被唤醒](https://github.com/torvalds/linux/blob/v3.13/kernel/ptrace.c#L751)，直到它遇到下一个系统调用。

## 4 进入系统调用

到目前为止，我们已经通过设置内核结构体变量 `struct task_struct *task` 的
`TIF_SYSCALL_TRACE` 来使内核跟踪指定进程的系统调用。

那么：**设置的参数是何时被检查和使用的呢？**

程序发起一个系统调用时，在系统调用执行之前，会执行一段 CPU 相关的内核代码。在
x86 平台上，这段代码位于
[arch/x86/kernel/entry_64.S](https://github.com/torvalds/linux/blob/v3.13/arch/x86/kernel/entry_64.S#L593)。

### 4.1 `_TIF_WORK_SYSCALL_ENTRY`

如果查看汇编函数 `system_call`，会看到它会检查一个 `_TIF_WORK_SYSCALL_ENTRY` flag：

```c
  testl $_TIF_WORK_SYSCALL_ENTRY,TI_flags+THREAD_INFO(%rsp,RIP-ARGOFFSET)
  jnz tracesys
```

如果设置了这个 flag，执行会转到 `tracesys` 函数。

这个 flag 的定义：

```c
/* work to do in syscall_trace_enter() */
#define _TIF_WORK_SYSCALL_ENTRY \
        (_TIF_SYSCALL_TRACE | _TIF_SYSCALL_EMU | _TIF_SYSCALL_AUDIT |   \
         _TIF_SECCOMP | _TIF_SINGLESTEP | _TIF_SYSCALL_TRACEPOINT |     \
         _TIF_NOHZ)
```

可以看到这个 flag 其实就是多个 flag
的组合，其中包括我们之前设置的那个：`_TIF_SYSCALL_TRACE`。

到这里就明白了，如果进程的内核结构体变量设置了
`_TIF_SYSCALL_TRACE`，到这里就会检测到，然后执行转到 `tracesys`。

### 4.2 `tracesys`

[代码](https://github.com/torvalds/linux/blob/v3.13/arch/x86/kernel/entry_64.S#L723)会调用
`syscall_trace_enter`。这个函数定义在 `arch/x86/kernel/ptrace.c`，是 CPU
相关的代码，可以查看[这里](https://github.com/torvalds/linux/blob/v3.13/arch/x86/kernel/ptrace.c#L1457)。

代码如果检测到设置了 `_TIF_SYSCALL_TRACE` flag，就会调用 `tracehook_report_syscall_entry`：

```c
  if ((ret || test_thread_flag(TIF_SYSCALL_TRACE)) &&
      tracehook_report_syscall_entry(regs))
    ret = -1L;
```

`tracehook_report_syscall_entry` 是一个静态内联函数，定义在
`include/linux/tracehook.h`，有很好的[文档](https://github.com/torvalds/linux/blob/v3.13/include/linux/tracehook.h#L80-L103)。

它接下来又调用了 [ptrace_report_syscall](https://github.com/torvalds/linux/blob/v3.13/include/linux/tracehook.h#L58)。

### 4.3 `ptrace_report_syscall`

这个函数符合之前我们描述过的：当 tracee 进入系统调用时生成一个 SIGTRAP 信号：

```c
ptrace_notify(SIGTRAP | ((ptrace & PT_TRACESYSGOOD) ? 0x80 : 0));
```

其中 `ptrace_notify` 定义在
[kernel/signal.c](https://github.com/torvalds/linux/blob/v3.13/kernel/signal.c#L1975)。
它会进一步调用 `ptrace_do_notify`，后者会初始化一个`siginfo_t info`变量，交给 `ptrace_stop`。

### 4.4 `SIGTRAP`

tracee 一旦收到 SIGTRAP 信号就停止执行，tracer 会收到通知说有信号待处理。接下来
tracer 就可以查看 tracee 的状态，打印寄存器的值、时间戳等等信息。

当你用 `strace` 工具跟踪进程时，屏幕上的输出就是这么来的。

### 4.5 `syscall_trace_leave`

退出系统调用的过程与此类似：

1. [汇编代码](https://github.com/torvalds/linux/blob/v3.13/arch/x86/kernel/entry_64.S#L796) 调用 `syscall_trace_leave`
1. [这个函数](https://github.com/torvalds/linux/blob/v3.13/arch/x86/kernel/ptrace.c#L1507) 调用 `tracehook_report_syscall_exit`
1. [继续调用](https://github.com/torvalds/linux/blob/v3.13/include/linux/tracehook.h#L122) `ptrace_report_syscall`

这就是 tracee 的系统调用完成时，tracer 如何获取返回值、时间戳等等信息以打印输出的。

## 6 结束语

`ptrace`
系统调用对调试器、跟踪器和其他的从进程中提取信息的程序非常有用，`strace`
主要就是基于 `ptrace` 实现的。

`ptrace`
内部略微有些复杂，因为执行过程在一些文件之间跳来跳去，但总体来说，实现还是挺简单直接的。

我建议你也看一看你最喜欢的调试器的源码，看它是如何基于 `ptrace`
来完成检查程序状态、修改寄存器和内存等工作的。

## 7 我们的相关文章

如果对本文感兴趣，那么你可能对我们的以下文章也感兴趣：

1. [(译) Linux系统调用权威指南]({% link _posts/2019-01-30-system-call-definitive-guide-zh.md %})
1. [(译) ltrace 是如何工作的]({% link _posts/2019-02-07-how-does-ltrace-work-zh.md %})
