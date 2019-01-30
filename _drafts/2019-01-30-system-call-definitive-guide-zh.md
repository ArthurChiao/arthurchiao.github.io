---
layout: post
title:  "[译] Linux系统调用权威指南"
date:   2019-01-30
categories: system-call
---

## 1 What is a system call?
## 2 Prerequisite information
### 2.1 Hardware and software
### 2.2 User programs, the kernel, and CPU privilege levels
### 2.3 Interrupts

可以想象一个内存中的数组。数组中的每项（entry）分别对应一个中断号。每项的内容包
括中断发生时CPU需要执行的函数的地址以及其他一些选项，例如以哪个特权等级执行中断
函数。

下面是Intel CPU手册中提供的关于一个entry的内存布局图：

<p align="center"><img src="/assets/img/system-call-definitive-guide/idt.png" width="60%" height="60%"></p>

注意其中有一个2bit的DPL（Descriptor Privilege Level）字段。这个字段的值决定了执
行中断函数时CPU所应在的最小特权等级。

这就是当一个特定类型的中断事件发生时，CPU如何知道中断函数的地址，已经它应该以哪
个特权等级执行中断函数的原理。

实际上，处理x86-64系统的中断还有很多其他方式。如果你想了解更多，可以阅读8259
Programmable Interrupt Controller, Advanced Interrupt Controllers, and IO
Advanced Interrupt Controllers.

处理硬件和软件中断时还有其他的一些复杂之处，例如中断号冲突（collision）和重映射
（remapping）。在本篇中我们不考虑这些方面。

### 2.4 型号特定寄存器（MSR）

型号特定寄存器（Model Specific Registers， MSR）是用于特殊目的的控制寄存器，可以
控制CPU的特定特性。CPU文档里有列出每个MSR的地址。

可以使用CPU的rdmsr和wrmsr来分别读写MSR。也有命令行工具可以读写MSR，但是不推荐这
样做，因为改变这些值（尤其是在系统正在运行的时候）是非常危险的，除非你非常小心知
道自己在做什么。

如果你不介意潜在的系统不稳定，或者不可逆的数据损坏，那你可以安装msr-tools然后加
载msr内核模块来读写MSR：

```shell
% sudo apt-get install msr-tools
% sudo modprobe msr
% sudo rdmsr
```

接下来的一些系统调用使用了MSR，我们稍后会看到。

### 2.5 不要写汇编代码调系统调用

自己写汇编代码来调用系统调用并不是一个好主意。

其中一个重要原因是，一些系统调用在glibc中有一些额外，在系统调用之前或之后执行。

接下来的例子中我们使用exit系统调用。事实上你可以用atexit函数向exit注册回调函数，
在它退出的时候就会执行。这些函数是从glibc里调用的，而不是内核。因此，如果你自己
写了汇编代码调用exit，那你注册的回调函数就不会被执行，因为你绕过了glibc。

然而，徒手写汇编来调系统调用是一次很好的学习经历。

## 3 传统(Legacy)系统调用

根据前面的知识我们知道了两件事情：

1. 我们可以通过产生软中断触发内核执行
1. 我们可以通过int汇编指令产生软中断

将两者结合，我们就来到了Linux传统的系统调用接口。

Linux内核预留了一个特殊的软中断号128 (0x80)，用户空间程序可以使用这个软中断进入内核执行系
统调用。对应的中断处理函数是ia32_syscall。接下来看一下代码实现。

从 中的trap_init函数开始：

```shell
void __init trap_init(void)
{
        /* ..... other code ... */

        set_system_intr_gate(IA32_SYSCALL_VECTOR, ia32_syscall);
```

其中IA32_SYSCALL_VECTOR在 中定义为0x80.

但是，如果内核只给用户空间程序预留了一个软中断，内核如何知道中断触发的时候，该去
执行哪个系统调用呢？

答案是，用户程序会将系统调用号放到eax寄存器。系统调用所需的参数放到其他的通用寄
存器上。

xxx对这个过程做了注释：

```c
* Emulated IA32 system calls via int 0x80.
 *
 * Arguments:
 * %eax System call number.
 * %ebx Arg1
 * %ecx Arg2
 * %edx Arg3
 * %esi Arg4
 * %edi Arg5
 * %ebp Arg6    [note: not saved in the stack frame, should not be touched]
 *
```

现在我们知道了如何调系统调用，也知道了系统调用的参数应该放到哪里，那接下来我们就
写一些内联汇编来调用试试。

### 3.1 用户端：自己写汇编完成传统系统调用

完成一次传统系统调用只需要少量内联汇编。虽然从学习的角度来说很有趣，但是我建议读
者永远不要（在生产环境）这样做。

在这个例子中，我们将调用exit系统调用，它只有一个参数：返回值。

首先，我们要找到exit的系统调用号。Linux内核里有一个文件列出了所有的系统调用号。
在build期间，这个文件会被多个脚本处理，最后生成用户空间会用到的头文件。

这个列表的位于xxx：

```c
1 i386  exit      sys_exit
```

exit的系统调用号是1。根据我们前面的信息，我们只需要将系统调用号放到eax寄存器，然
后将第一个参数（返回值）放到ebx。

如下是实现这个功能的简单C代码，其中包括几行内联汇编。这里将返回值设置为42。
(这个程序其实还可以简化，我是故意让它有一定冗余的，这样对没有GCC内联汇编基础的读
者来说会比较好看懂。)

```c
int
main(int argc, char *argv[])
{
  unsigned int syscall_nr = 1;
  int exit_status = 42;

  asm ("movl %0, %%eax\n"
       "movl %1, %%ebx\n"
       "int $0x80"
    : /* output parameters, we aren't outputting anything, no none */
      /* (none) */
    : /* input parameters mapped to %0 and %1, repsectively */
      "m" (syscall_nr), "m" (exit_status)
    : /* registers that we are "clobbering", unneeded since we are calling exit */
      "eax", "ebx");
}
```

编译运行，查看返回值：

```shell
$ gcc -o test test.c
$ ./test
$ echo $?
42
```

成功！我们通过触发一个软中断完成了一次传统系统调用。

### 3.2 内核端：`int $0x80`进入点

我们已经看到了如果从用户端触发一个系统调用，接下来看内核端是如何实现的。

前面提到内核注册了一个系统调用回调函数ia32_syscall。这个函数定义在xxx。函数里最
重要的一件事事情，就是调用真正的系统调用：

```c
ia32_do_call:
        IA32_ARG_FIXUP
        call *ia32_sys_call_table(,%rax,8) # xxx: rip relative
```

宏`IA32_ARG_FIXUP`的作用是对传入的参数进行重新排列，以便能被当前的系统调用层正确处理。

`ia32_sys_call_table`是一个中断号列表，定义在xxx，注意代码结束处的`#include`：

```c
const sys_call_ptr_t ia32_sys_call_table[__NR_ia32_syscall_max+1] = {
        /*
         * Smells like a compiler bug -- it doesn't work
         * when the & below is removed.
         */
        [0 ... __NR_ia32_syscall_max] = &compat_ni_syscall,
#include <asm/syscalls_32.h>
};
```

回忆前面我们在xxx中看到了系统调用列表的定义。有几个脚本会在build期间运行，通过这
个文件生成syscalls_32.h头文件。后者是合法的C代码文件，通过上面看到的#include插入
到ia32_sys_call_table。

这就是**通过传统系统调用进入内核的过程**。

### 3.3 `iret`: 系统调用返回

至此我们已经看到了如何通过软中断进入内核，那么，系统调用结束后，内核又是如何放
特权等级以及回到用户空间的呢？

如果查看Intel Software Developer's Manual（警告：很大的PDF），能看到如何很有帮助
的一张图，它解释了当特权等级发送改变时，程序栈是如何组织的：

<p align="center"><img src="/assets/img/system-call-definitive-guide/isr_stack.png" width="60%" height="60%"></p>

当执行转交给ia32_syscall时，会发送一次特权等级改变，其结果是进入ia32_syscall时的
栈会变成入上图所示的样子。从中可以看出，返回地址、包含特权等级的CPU flags，以及
其他一些参数都在ia32_syscall执行之前压入到栈顶。

所以，内核只需要从栈里拷贝这些值到它们原来所在的寄存器，程序就可以回到用户空间继续
执行。那么，如何做呢？

有几种方式，其中最简单的是通过iret指令。

Intel指令集手册解释说iret指令从栈上依次pop出返回地址和保存的寄存器值：

> As with a real-address mode interrupt return, the IRET instruction pops the
> return instruction pointer, return code segment selector, and EFLAGS image
> from the stack to the EIP, CS, and EFLAGS registers, respectively, and then
> resumes execution of the interrupted program or procedure.

要在内核中找到相应的代码有点困难，因为它隐藏在多层宏后面，系统要通过这些宏处理很
多事情，比例信号和ptrace系统返回跟踪。

irq_return 定义在xxx：

```c
irq_return:
  INTERRUPT_RETURN
```

其中`INTERRUPT_RETURN`定义在xxx，就是iretq。

**以上就是传统系统调用如何工作的。**

## 4 Fast system calls

### 4.1 32-bit fast system calls
#### 4.1.1 sysenter/sysexit
#### 4.1.2 __kernel_vsyscall internals
#### 4.1.3 Using sysenter system calls with your own assembly
#### 4.1.4 Kernel-side: sysenter entry point
#### 4.1.5 Returning from a sysenter system call with sysexit
### 4.2 64-bit fast system calls
#### 4.2.1 syscall/sysret
#### 4.2.2 Using syscall system calls with your own assembly
#### 4.2.3 Kernel-side: syscall entry point
#### 4.2.4 Returning from a syscall system call with sysret
## 5 Calling a syscall semi-manually with syscall(2)
glibc syscall wrapper internals
## 6 Virtual system calls
### 6.1 vDSO in the kernel
### 6.2 Locating the vDSO in memory
### 6.3 vDSO in glibc
## 7 glibc system call wrappers
## 8 Interesting syscall related bugs
### 8.1 CVE-2010-3301
### 8.2 Android sysenter ABI breakage
## 9 Conclusion
