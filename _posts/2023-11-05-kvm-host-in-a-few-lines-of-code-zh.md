---
layout    : post
title     : "[译] 100 行 C 代码创建一个 KVM 虚拟机（2019）"
date      : 2023-11-05
lastupdate: 2023-11-05
categories: kvm virtualization vm
---

### 译者序

本文核心内容来自 2019 年的一篇英文博客：
[KVM HOST IN A FEW LINES OF CODE](https://zserge.com/posts/kvm/)，

1. 首先基于 KVM API 用 **<mark>100 来行 C 代码</mark>**实现一个极简**<mark>虚拟机管理程序</mark>**（类比 VirtualBox）；
2. 然后用 **<mark>10 来行汇编代码</mark>**编写一个**<mark>极简内核</mark>**，然后将其制作成**<mark>虚拟机镜像</mark>**（类比 Ubuntu/Linux）；
3. 然后把 2 作为输入传给 1，就能**<mark>创建出一个虚拟机</mark>**并运行。

本文重新组织和注释了原文核心部分，并做了一些内容扩展，供个人学习参考。为尊重原作者劳动，
本文仍以 [译] 作为标题开头，但注意内容和顺序已经和原文不太对得上。
本文所用代码见 [github](https://github.com/ArthurChiao/arthurchiao.github.io/tree/master/assets/code/kvm-host-in-a-few-lines-of-code)。

**由于译者水平有限，本文不免存在错误之处。如有疑问，请查阅原文。**

----

* TOC
{:toc}

----

KVM (**<mark><code>Kernel</code></mark>** Virtual Machine) 是 Linux 内核提供的一种虚拟化技术，
允许用户在单个 Linux 主机上运行多个虚拟机（VM），OpenStack/kubevirt 等等开源 VM 编排系统的底层就是基于 KVM。
那 KVM 是如何工作的呢？

# 1 内核 KVM 子系统

## 1.1 交互：字符设备 `/dev/kvm`

KVM 通过一个特殊（字符）设备 **<mark><code>/dev/kvm</code></mark>** 供用户空间操作，

```shell
$ file /dev/kvm
/dev/kvm: character special
```

> Character devices in Linux provide unbuffered access to data. It is used to communicate with devices that
> transfer data character by character, such as keyboards, mice, serial ports,
> and terminals. Character devices allow data to be read from or written to the
> device one character at a time, without any buffering or formatting.

KVM 的整套 API 都是基于文件描述符的。

## 1.2 接口：KVM API

KVM API 是一系列控制 VM 行为的 **<mark><code>ioctl()</code></mark>** get/set 操作，
按功能层次分为下面几个级别：

| 级别 | 说明 | 备注 |
|:----|:----|:-----|
| System | **<mark>KVM 子系统级别</mark>**的操作；另外还包括一个创建 VM 的 ioctl 操作。 |   |
| VM     | **<mark>VM 级别</mark>**的操作，例如设置内存布局；另外还包括一个创建 VCPU 和 device 的 ioctl 操作。 | 必须从创建该 VM 的那个进程（地址空间）发起。 |
| VCPU   | **<mark>VCPU 级别</mark>**的操作。 | 必须从创建该 VCPU 那个线程发起。异步 VCPU ioctl 操作除外。 |
| Device | **<mark>设备级别</mark>**的操作。 | 必须从创建该 VM 的那个进程（地址空间）发起。 |

## 1.3 操作：`ioctl()` 系统调用

`open("/dev/kvm")` 获得一个 KVM 子系统的 fd，
就可以通过 **<mark><code>ioctl(kvm_fd, ...)</code></mark>** 系统调用来分配资源、启动和管理 VM 了。

# 2 `100` 来行 C 代码创建一个 KVM 虚拟机

接下来看一个完整例子：如何基于 KVM 提供的 API 来创建和运行一个虚拟机。

## 2.1 打开 KVM 设备：`kvm_fd = open("/dev/kvm")`

与 KVM 子系统交互，需要以读写方式打开 `/dev/kvm`，获取一个文件描述符：

```c
    if ((kvm_fd = open("/dev/kvm", O_RDWR)) < 0) {
        fprintf(stderr, "failed to open /dev/kvm: %d\n", errno);
        return 1;
    }
```

这个文件描述符 `kvm_fd` 在系统中是唯一的，它会将我们接下来的 KVM 操作与主机上其他用户的
KVM 操作区分开（例如，系统上可能同时有多个用户或进程在创建和管理各自的虚拟机）。

## 2.2 创建 VM 外壳：`vm_fd = ioctl(kvm_fd, KVM_CREATE_VM)`

有了 kvm_fd 之后，就可以向内核 KVM 子系统发起一个**<mark>创建虚拟机</mark>**的 ioctl 请求了：

```c
    if ((vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0)) < 0) {
        fprintf(stderr, "failed to create vm: %d\n", errno);
        return 1;
    }
```

返回的文件描述符唯一标识这个虚拟机。
不过，此时这个“虚拟机”还仅仅是一个**<mark>“机箱”</mark>**，没有 CPU，也没有内存。

## 2.3 分配 VM 内存：`mmap()`

“虚拟机”是用**<mark>用户空间进程</mark>**来模拟一台完整的机器，
因此给“虚拟机”分配的内存也需要来用户空间，具体来说就是宿主机上的**<mark>用户空间内存</mark>**（userspace memory）。
分配用户空间内存有多种方式，这里我们用效率比较高的 `mmap()`：

```c
    if ((mem = mmap(NULL, 1 << 30, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0)) == NULL) {
        fprintf(stderr, "mmap failed: %d\n", errno);
        return 1;
    }
```

成功后，返回映射内存区域的起始地址 `mem`。

## 2.4 初始化 VM 内存：`ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION)`

初始化这片内存区域：

```c
    struct kvm_userspace_memory_region region;
    memset(&region, 0, sizeof(region));
    region.slot = 0;
    region.guest_phys_addr = 0;
    region.memory_size = 1 << 30;
    region.userspace_addr = (uintptr_t)mem;
    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        fprintf(stderr, "ioctl KVM_SET_USER_MEMORY_REGION failed: %d\n", errno);
        return 1;
    }
```

接下来就可以将虚拟机镜像加载到这片内存区域了。

## 2.5 加载 VM 镜像：`open() + read()`

这里假设命令行第一个参数指定的是**<mark>虚拟机镜像的文件路径</mark>**，

```c
    int img_fd = open(argv[1], O_RDONLY);
    if (img_fd < 0) {
        fprintf(stderr, "can not open binary guest file: %d\n", errno);
        return 1;
    }
    char *p = (char *)mem;
    for (;;) {
        int r = read(img_fd, p, 4096);
        if (r <= 0) {
            break;
        }
        p += r;
    }
    close(img_fd);
```

以 4KB 为单位，通过一个循环将整个镜像文件内容复制到 **<mark>VM 的内存地址空间</mark>**。

> KVM **<mark>并非逐个解释执行 CPU 指令</mark>**，而是让真实 CPU 直接执行，
> 因此要求**<mark>镜像（字节码）与当前 CPU 架构相符</mark>**，KVM 自己只拦截 I/O 请求。
> 因此，KVM 性能很好，除非 VM 有大量 IO 操作。

至此，VM 内存部分的虚拟化和初始化就完成了。

## 2.6 创建 VCPU：`ioctl(vm_fd, KVM_CREATE_VCPU)`

接下来给 VM 创建虚拟机处理器，即 VCPU：

```c
    if ((vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0)) < 0) {
        fprintf(stderr, "can not create vcpu: %d\n", errno);
        return 1;
    }
```

成功后，返回一个非负的 VCPU 文件描述符。
这个 VCPU 有自己的寄存器、内存，将模拟一个物理 CPU 的执行。

## 2.7 初始化 VCPU 控制区域：`ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE) + mmap`

VCPU 运行结束后，需要将一些运行状态（**<mark><code>"run state"</code></mark>**）返回给我们的控制程序。
KVM 的实现方式是提供一段特殊的内存区域，称为 KVM_RUN，来存储和传递这些状态。

通过 ioctl 可以获取这段内存的大小：

```c
    int kvm_run_mmap_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (kvm_run_mmap_size < 0) {
        fprintf(stderr, "ioctl KVM_GET_VCPU_MMAP_SIZE: %d\n", errno);
        return 1;
    }
```

然后通过 mmap 分配内存：

```c
    struct kvm_run *run = (struct kvm_run *)mmap(NULL, kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu_fd, 0);
    if (run == NULL) {
        fprintf(stderr, "mmap kvm_run: %d\n", errno);
        return 1;
    }
```

VCPU 退出运行时，将把退出原因（例如需要 IO）等状态信息写入这里。

## 2.8 设置 VCPU 寄存器：`ioctl(vcpu_fd, KVM_SET_SREGS/KVM_SET_REGS)`

接下来需要初始化这个 VCPU 的寄存器。首先拿到这些寄存器，

```c
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    if (ioctl(vcpu_fd, KVM_GET_SREGS, &(sregs)) < 0) {
        perror("can not get sregs\n");
        exit(1);
    }
```

为简单起见，我们这里要求虚拟机镜像是 **<mark><code>16bit</code></mark>** 模式，
也就是内存地址和寄存器都是 16 位的。

设置**<mark>特殊目的寄存器</mark>**（special registers）：
初始化几个 segment pointers（段指针），它们表示的是内存偏置（memory offset） [2]，

* CS：**<mark>代码段</mark>**（code segment）
* SS：**<mark>栈段</mark>**（stack segment）
* DS：**<mark>数据段</mark>**（data segment）
* ES：**<mark>额外段</mark>**（extra segment）

```c
#define CODE_START 0x0000

    sregs.cs.selector = CODE_START;  // 代码
    sregs.cs.base = CODE_START * 16;
    sregs.ss.selector = CODE_START;  // 栈
    sregs.ss.base = CODE_START * 16;
    sregs.ds.selector = CODE_START;  // 数据
    sregs.ds.base = CODE_START * 16;
    sregs.es.selector = CODE_START;  // 额外
    sregs.es.base = CODE_START * 16;
    sregs.fs.selector = CODE_START;  //
    sregs.fs.base = CODE_START * 16;
    sregs.gs.selector = CODE_START;  //

    if (ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("can not set sregs");
        return 1;
    }
```

设置**<mark>通用目的寄存器</mark>**：

```c
    regs.rflags = 2;
    regs.rip = 0;

    if (ioctl(vcpu_fd, KVM_SET_REGS, &(regs)) < 0) {
        perror("KVM SET REGS\n");
        return 1;
    }
```

至此，所有初始化工作都做完了，接下来就可以启动这个虚拟机了。

## 2.9 启动 VM：`ioctl(vcpu_fd, KVM_RUN)`

启动一个无限循环，在里面做两件事情：

1. 调用 `ioctl(vcpu_fd, KVM_RUN, 0)` **<mark>让 VCPU 运行</mark>**，直到它主动退出；
2. VCPU 退出之后，读取 **<mark><code>KVM_RUN</code></mark>** **<mark>控制区域，判断退出原因</mark>**，然后执行相应的操作；

```c
    for (;;) {
        int ret = ioctl(vcpu_fd, KVM_RUN, 0);
        if (ret < 0) {
            fprintf(stderr, "KVM_RUN failed\n");
            return 1;
        }

        switch (run->exit_reason) {
            case KVM_EXIT_IO:
                printf("IO port: %x, data: %x\n", run->io.port,
                        *(int *)((char *)(run) + run->io.data_offset));
                sleep(1);
                break;
            case KVM_EXIT_SHUTDOWN:
                goto exit;
        }
    }
```

这里只判断两种状态：

1. 如果 VCPU 是因为要执行 IO 操作而退出，那就从 KVM_RUN 区域读取它想输入/输出的数据，然后替它执行 —— 这里就是打印出来；
2. 如果是正常退出，就退出这个无限循环 —— 对我们这个简单程序来说，实际效果就是关闭并销毁这个虚拟机。

## 2.10 小结

以上就是创建、初始化并运行一个 VM 的代码，总共 130 行左右（如果不算头文件引用和一些打印代码，不到 100 行）。
要测试运行，现在唯一还缺的就是一个**<mark>虚拟机镜像</mark>**。

为了深入理解，下面我们自己用汇编代码来写一个极简虚拟机（内核），并做成镜像。

# 3 极简 VM 镜像

## 3.1 极简内核：8 行汇编代码

我们将用 16bit 汇编代码实现一个袖珍 guest VM “kernel”，效果是

1. 初始化一个变量为 0，
2. 进入一个无限循环，首先将变量值输出到 debug 端口 0x10，然后变量值加 1，进入下次循环；

代码如下，每行都做了注释，

```shell
# A tiny 16-bit guest "kernel" that infinitely prints an incremented number to the debug port

.globl _start
.code16          # 16bit 模式，让 KVM 用 "real" mode 运行
_start:          # 代码开始
  xorw %ax, %ax  # 设置 %ax = 0。对同一个寄存器做异或操作，结果为 0，所以这个操作就是重置寄存器 ax。
loop:            # 开始一个循环
  out %ax, $0x10 # 将 ax 寄存器的值输出到 0x10 I/O port
  inc %ax        # 将 ax 寄存器的值加 1
  jmp loop       # 跳到下一次循环
```

> 基础 x86 汇编语法可参考 [(译) 简明 x86 汇编指南（2017）]({% link _posts/2017-08-14-x86-asm-guide-zh.md %})。

KVM VCPU 支持运行多种模式（16/32 bit 等），这里用 16bit 是因为这种模式最简单。
另外，Real mode 是直接内存寻址的，不需要 descriptor tables，因此初始化寄存器非常方便。

## 3.2 制作成虚拟机镜像

只需汇编（assemble）和链接：

```shell
$ make image
as -32 guest.S -o guest.o
ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o guest guest.o
```

* 汇编（assemble）：将**<mark>汇编代码</mark>**（assembly code）转成**<mark>目标文件</mark>**（object file）
* 链接（linking）：将**<mark>目标文件</mark>**及其依赖链接为**<mark>ELF 文件</mark>**

最终得到的是一个**<mark>与当前 CPU 架构相同</mark>**的二进制文件（**<mark>字节码</mark>**），

```shell
$ file guest
guest: data
```

当前宿主机的 CPU 可以直接执行这些指令。

# 4 测试

## 4.1 编译

我们的 C 代码只依赖内核头文件，如果用的 centos，如下安装：

```shell
$ yum install kernel-headers
```

然后就可以用 gcc 或 clang 编译了：

```shell
$ make kvm
gcc kvm-vmm.c

$ ls
a.out  guest  guest.o  guest.S  kvm-vmm.c  Makefile
```

## 4.2 运行

```shell
$ ./a.out guest
IO port: 10, data: 0
IO port: 10, data: 1
IO port: 10, data: 2
^C
```

# 5 扩展阅读

如何让虚拟机内核更接近现实，[原文](https://zserge.com/posts/kvm/) 有进一步讨论和部分验证：

* 方向

    * 通过 ioctl 增加定时器、中断控制器等；
    * bzImage 格式；
    * boot 协议
    * 磁盘、键盘、图形处理器等 I/O driver 支持

* 实际上不会直接使用 KVM API，而是使用更上层的[**<mark><code>libvirt</code></mark>**](https://libvirt.org/)，封装了 KVM/BHyve 等底层虚拟化技术；
* 想更深入学习 KVM，推荐阅读 [kvmtool](https://git.kernel.org/pub/scm/linux/kernel/git/will/kvmtool.git/tree/) 源码；代码不算多，比 QEMU 更容易理解；

相关主题：

* [(译) 简明 x86 汇编指南（2017）]({% link _posts/2017-08-14-x86-asm-guide-zh.md %})
* [(译) 400 行 C 代码实现一个虚拟机（2018）]({% link _posts/2019-10-16-write-your-own-virtual-machine-zh.md %})
* [(译) (论文) 可虚拟化第三代（计算机）架构的规范化条件（ACM, 1974）]({% link _posts/2021-11-19-formal-requirements-for-virtualizable-arch-zh.md %})

# 译文参考资料

1. [The Definitive KVM (Kernel-based Virtual Machine) API Documentation](https://docs.kernel.org/virt/kvm/api.html), kernel.org
2. [x86 Assembly/16, 32, and 64 Bits](https://en.wikibooks.org/wiki/X86_Assembly/16,_32,_and_64_Bits#16-bit), wikipedia
