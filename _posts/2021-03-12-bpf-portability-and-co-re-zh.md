---
layout    : post
title     : "[译] BPF 可移植性和 CO-RE（一次编译，到处运行）（Facebook，2020）"
date      : 2021-03-12
lastupdate: 2021-03-12
categories: bpf
---

### 译者序

本文翻译自 2020 年 Facebook 的一篇博客：
[BPF Portability and CO-RE](https://facebookmicrosites.github.io/bpf/blog/2020/02/19/bpf-portability-and-co-re.html)，
作者 Andrii Nakryiko。


关于 BPF CO-RE 的目标，引用文中的一段总结就是：

> * 作为一种**<mark>简单的方式</mark>**，帮助 BPF 开发者解决**<mark>简单的移植性问题</mark>**（例如读取结构体的字段），并且
> * 作为一种**<mark>不是最优，但可用的方式</mark>**，帮助 BPF 开发者
>   解决**<mark>复杂的移植性问题</mark>**（例如不兼容的数据结构改动、复杂的用户空间控制条件等）。
> * 使开发者能遵循”一次编译、到处运行“（Compile Once – Run Everywhere）范式。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

本文介绍 BPF 可移植性面临的问题，以及 BPF CO-RE（Compile Once – Run Everywhere）
是如何解决这些问题的。

# 1 BPF 现状

(e)BPF 出来之后，社区一直在试图**简化 BPF 程序的开发过程** —— 最好能像开发
用户空间应用程序（userspace application）一样简单直接 —— 可惜这个目标从未实现。
具体来说，在使用性（usability）方面确实有很大进步，但另一个重要方面却被忽略了
（**大部分出于技术原因**）：可移植性。

那么，什么是 <mark>”BPF 可移植性“</mark>（BPF portability）？我们定义它是
**这样一种能力**：编写的程序**通过编译和内核校验之后**，能<mark>正确地</mark>在
**不同版本的内核**上运行 —— 而**无需针对不同内核重新编译**。

本文首先介绍 BPF 可移植性面临的问题，然后介绍我们的**<mark>解决方案：BPF CO-RE</mark>**
（Compile Once – Run Everywhere）。接下来内容如下：

* 首先讨论 BPF 可移植性问题本身，分析它所面临的挑战，以及为什么解决这些挑战如此重要；
* 然后从较高层次查看 BPF CO-RE 的各组件，以及它们是如何组织成一个完整方案解决这个问题的；
* 最后以一些例子（BPF 代码片段）结束本文，这些例子展示了 BPF CO-RE 中用户可见 API。

# 2 BPF 可移植性面临的问题

BPF 程序是**由用户提供的**、经过验证之后**<mark>在内核上下文中执行</mark>**的程序。
BPF <mark>运行在内核内存空间</mark>（kernel memory space）执行，<mark>能访问大量的
内核内部状态</mark>（internal kernel state）。
这使得 BPF 程序功能极其强大，也是为什么它能成功地应用在大量不同场景的原因之一。

但另一方面，与强大能力相伴而生的是我们如今面临的可移植性问题：<mark>BPF 程序
并不控制它<strong>运行时所在内核</strong>的内存布局</mark>（memory layout）。
因此，BPF 程序只能运行在**开发和编译这些程序时**所在的内核。

另外，**内核类型（kernel types）和数据结构（data structures）也在不断变化**。
不同的内核版本中，同一结构体的同一字段所在的位置可能会不同 —— 甚至已经
移到一个新的内部结构体（inner struct）中。此外，字段还可能会被重命名、删除、
改变类型，或者（根据不同内核配置）被条件编译掉。

## 2.1 可移植：为什么理论上可行

以上分析可知，内核版本升级时很多东西都会发生变化，而 BPF 开发者希望能够<mark>
避免这些变化对 BPF 程序造成影响</mark>。这听上去似乎不可能 —— 内核环境都在不断变化，
依赖内核环境执行的 BPF 程序又如何能幸免于难呢？

但实际上，这是可能的：

* 首先，**<mark>不是所有 BPF 程序都依赖内核内部数据结构</mark>**。

    一个例子是 BPF 工具 `opensnoop`，它基于 kprobes/tracepoints 跟踪进程打开的文件，
    因此**<mark>只要能拦截到少数几个系统调用</mark>**就能工作。由于系统调用接口
    提供稳定的 ABI，不会随着内核版本而变，因此这样的 BPF 程序做到可移植是问题不大的。

    不幸的是，这种类型的 BPF 程序很少，而且它们能做的事情通常也是非常有限的。

* 其次，内核 BPF 基础设施**<mark>提供了一组有限的”稳定接口“</mark>**（stable interfaces），
  内核版本升级时保证稳定，因此 BPF 程序可以依赖这组接口。

  实际上，底层结构体和工作机制都可能发生变化，但这组稳定接口向用户程序屏蔽了这些变动。
  一个例子是网络应用中的 `struct sk_buff` 和 `struct __sk_buff`。

  * `struct sk_buff` 是内核中的数据包表示，字段非常多，并且经常发生变化；
  * `struct __sk_buff` 是 **<mark>BPF 校验器提供的一个 sk_buff 的稳定接口</mark>**，
    或者说一组属性集合。将用户程序与底层的 `struct sk_buff` 解耦开来，
    因此后者内存布局发生变化时，不会影响 BPF 程序。 
  * 所有对 `struct __sk_buff` 字段的访问都会被**透明地转换成对 `struct sk_buff` 的访问**。

  很多 BPF 程序类型都有类似的机制，**这种封装在 BPF 中称为上下文（context）**，触发
  BPF 程序执行时，一般传递的就是这样的上下文（指针类型，例如 `struct __sk_buff
  *ctx`）。因此，如果开发 BPF 程序时使用的是这些结构体，那这样的程序大概率是可移
  植的。

## 2.2 可移植：挑战

但是，一旦需要查看原始的内核内部数据（raw internal kernel data）—— 例如
常见的表示进程或线程的 `struct task_struct`，这个结构体中有非常详细的进程信息 —— 
那你就只能靠自己了。对于 **tracing、monitoring 和 profiling 应用**来说这个需求
非常常见，而这类 BPF 程序也是极其有用的。

### 内核版本不同：字段被重命名或移动位置

在这种情况下，**<mark>如何保证读到的一定是我们期望读的那个字段呢</mark>** —— 例如，

* 原来的程序是从 `struct task_struct` offset 8 地址读取数据，
* 由于新内核加个了 16 字节新字段，那此时正确的方式应该是从 offset 24 地址读，

这还没完：如果这个字段被改名了呢？例如，`thread_struct` 的 `fs` 字段（获取 thread-local storage 用），
在 4.6 到 4.7 内核升级时就被重命名为了 `fsbase`。

### 内核版本相同但配置不同：字段在编译时被移除（compile out）

另一种情况：内核版本相同，但内核编译时的配置不同，导致
结构体的某些字段在编译器时被完全移除了。

具体例子：某些可选的<mark>审计字段</mark>。

### 小结

所有这些都意味着：依赖**<mark>开发环境本地的内核头文件</mark>**编译的 BPF 程序，
是无法直接分发到其他机器运行 —— 然后期待它们返回正确结果的。
这是由于不同版本的内核头文件所假设的内存布局是不同的。

## 2.3 可移植：BCC 方式

目前，人们可以用 <a href="https://github.com/iovisor/bcc/">BCC</a> (BPF Compiler Collection)
解决这个问题，使用方式如下：

1. 开发：将 BPF C 源码以**<mark>文本字符串</mark>**形式，**嵌入（Python 编写的）用户空间控制应用**（control application）；
2. 部署：将控制应用以源码的形式拷贝到目标机器；
3. 执行：在目标机器上，BCC 调用它内置的 Clang/LLVM，然后 include 本地内核头文件
  （**需要确保本机已经安装了正确版本的 `kernel-devel` 包**）然后<mark>现场执行编译、加载、运行</mark>。

这种方式能确保 BPF 程序期望的内存布局与目标机器内核的内存布局是完全一致的。

对于那些**内核版本相关的可选字段或条件编译相关的配置代码**，只需要在源代码中
用 `#ifdef`/`#else` 做处理，BCC 内置的 Clang 能正确处理这些宏，最终剩下的就是与
当前内核相匹配的源代码。这就是 BCC 解决内核版本差异的方式。

## 2.4 BCC 方式的缺点

BCC 方式可行，但存在一些很大的缺点：

1. **<mark>Clang/LLVM 是一个庞大的库</mark>**，在部署时除了要分发 BPF 程序，还必须一起分发这个大库。
1. Clang/LLVM 这两个庞然大物**非常消耗资源**，因此**<mark>每次在目标机器上编译 BPF 代码，都将消耗大量系统资源</mark>**。

    * 尤其在线上的生产机器，现场编译可能会使机器负载瞬间飙高，导致生产问题。
    * 同样，如果机器本身已经负载很高，那编译一段很小的 BPF 程序可能都要几分钟。

1. 此外，这里有个很强的**<mark>前提：内核头文件在目标机器上一定存在</mark>**。
   在大部分情况下这都不是问题，但有时可能会带来麻烦。

    这对内核开发者来说也尤其头疼，因为他们经常要编译和部署一次性的内核，用于在
    开发过程中验证某些问题。而机器上没有指定的、版本正确的内核头文件包，基于 BCC
    的应用就无法正常工作。

1. 这种方式会拖慢开发和迭代速度。

    BPF 程序的**<mark>测试和开发过程也非常繁琐</mark>**，很多错误只有到了运行时
   （runtime）才会出现，而一旦出现就只能重启用户空间控制应用。

总体来说，虽然 bcc 是一个很伟大的工具 —— 尤其是用于快速原型、实验和开发小工具 —— 但
当用于广泛部署生产 BPF 应用时，它存在非常明显的不足。

为了更彻底地解决 BPF 移植性问题，我们**设计了 BPF CO-RE**，并相信这是
<mark>BPF 程序的未来开发方式</mark>，尤其适用于开发复杂、真实环境中的 BPF 应用。

# 3 BPF CO-RE：高层机制

BPF CO-RE 将它所依赖的如下软件栈和它的数据集中到了一起，

* 内核
* <mark>用户空间 BPF 加载器库（libbpf）</mark>
* 编译器（clang）

使得我们能以一种轻松的方式编写可移植 BPF 程序，在**单个预编译的 BPF 程序内
（pre-compiled BPF program）处理不同内核之间的差异**。

BPF CO-RE 需要下列组件之间的紧密合作：

1. BTF 类型信息：使得我们能获取<mark>内核、BPF 程序类型及 BPF 代码的关键信息</mark>，
   这也是下面其他部分的基础；
1. 编译器（clang）：给 BPF C 代码提供了<mark>表达能力和记录重定位（relocation）信息的能力</mark>；
1. BPF loader (<a href="https://github.com/libbpf/libbpf">libbpf</a>)：将内核的 BTF 与 BPF 程序联系起来，
   <mark>将编译之后的 BPF 代码适配到目标机器的特定内核</mark>；
1. 内核：虽然**<mark>对 BPF CO-RE 完全不感知</mark>**，但提供了一些 BPF 高级特性，使某些高级场景成为可能。

以上几部分相结合，提供了一种开发可移植 BPF 程序的**史无前例的能力**：这个开发
过程不仅方便（ease），而且具备很强的适配性（adaptability）和<mark>表达能力</mark>（expressivity）。
在此之前，实现同样的可移植效果只能通过 BCC 在运行时编译 BPF C 程序，而前面也分析了，
BCC 开销非常高。

## 3.1 BTF（BPF Type Format）

[BTF](https://www.kernel.org/doc/html/latest/bpf/btf.html) 是 BPF CO-RE 的核心之一，
它是是一种与 DWARF 类似的调试信息，但

* 更通用、表达更丰富，用于描述 C 程序的所有类型信息。
* 更简单，空间效率更高（使用 <a href="https://facebookmicrosites.github.io/bpf/blog/2018/11/14/btf-enhancement.html">BTF 去重算法</a>），
  占用空间比 DWARF 低 100x。

如今，让 Linux **内核在运行时（runtime）一直携带 BTF 信息**是可行的，
只需在编译时指定 `CONFIG_DEBUG_INFO_BTF=y`。内核的 BTF 除了被内核自身使用，
现在还用于增强 BPF 校验器自身的能力 —— 某些能力甚至超越了一年之前我们的想象力所及（例
如，已经有了直接读取内核内存的能力，不再需要通过 `bpf_probe_read()` 间接读取了）。

更重要的是，内核已经将这个<mark>自描述的权威 BTF 信息</mark>（定义结构体的精确内存布局等信息）
<mark>通过 sysfs 暴露出来</mark>，在 `/sys/kernel/btf/vmlinux`。
下面的命令将生成一个**<mark>与所有内核类型兼容的 C 头文件</mark>**（通常称为 "<strong>vmlinux.h</strong>"）：

```shell
$ bpftool btf dump file /sys/kernel/btf/vmlinux format c
```

这里说的”所有“真的是**”所有“**：<mark>包括那些并未通过 kernel-devel package 导出的类型</mark>！

## 3.2 编译器支持

为了让 BPF 加载器（例如 libbpf）将 BPF 程序适配到目标机器所运行的内核上，
**Clang 增加了几个新的 built-in**。它们的功能是导出（emit）
<mark>BTF relocations</mark>（重定位信息），后者是对 **BPF 程序想读取的那些信息的高层描述**。

例如，如果想访问 `task_struct->pid`，那 <mark>clang 将做如下记录</mark>：这是一个
**位于结构体 `struct task_struct` 中、类型为 `pid_t`、名为 `pid` 的字段**。

有了这种方式，即使目标内核的 `task_struct` 结构体中，`pid` 字段位置已经发生了变
化（例如，由于这个字段前面加了新字段），甚至已经移到了某个内部嵌套的匿名结构体
或 union 中（在 C 语言中这种行为是完全透明的，因此内核开发者这样做时并不会有特别
的顾虑），我们<mark>仍然能通过名字和类型信息找到这个字段</mark>。这称为
**<mark>field offset relocation</mark>**（字段偏置重定位）。

除了字段重定位，其他一些字段相关的操作，例如判断 <mark>field existence</mark>（
字段是否存在）或者 <mark>field size（字段长度）</mark>都是支持的。
甚至对 bitfields（比特位字段，在 C 语言中是出了名的”难处理“的类型，C 社区一直在努力让它们变得可重定位）
，我们仍然能基于 BTF 信息来使它们可重定位（relocatable），并且整个过程对 BPF 开
发者透明。

## 3.3 BPF 加载器（libbpf）

<a href="https://github.com/libbpf/libbpf">libbpf</a> 作为一个 <mark>BPF 程序加载器</mark>（loader），
处理前面介绍的内核 BTF 和 clang 重定位信息。它

1. 读取编译之后得到的 BPF ELF 目标文件，
2. 进行一些必要的后处理，
3. <mark>设置各种内核对象</mark>（bpf maps、bpf 程序等），然后
4. 将 BPF 程序加载到内核，然后触发校验器的验证过程。

**<mark>libbpf 知道如何对 BPF 程序进行裁剪，以适配到目标机器的内核上</mark>**。

* 它会查看 BPF 程序记录的 BTF 和重定位信息，然后
* 拿这些信息跟当前内核提供的 BTF 信息相匹配。然后
* 解析和匹配所有的类型和字段，更新所有必要的 offsets 和其他可重定位数据。

最终确保 BPF 程序在这个特定的内核上是能正确工作的。

如果一切顺利，你（作为 BPF 应用开发者）将得到一个针对目标机器”定制化裁剪“的 BPF
程序，就像这个程序是专门针对这个内核编译的一样。但这种工作方式无需将 clang 与
BPF 一起打包部署，也没有在目标机器上运行时编译（runtime）的开销。

## 3.4 内核

**<mark>内核无需太多改动就能支持 BPF CO-RE</mark>**，这一点可能令很多人感到惊讶。
由于设计合理，因此**对于内核来说，libbpf 处理之后的 BPF 程序，与
其他任何合法的 BPF 程序是一样的** —— 与在这台机器上依赖最新内核头文件编译出的
BPF 程序并无区别。这意味要 <mark>BPF CO-RE 并不依赖最新的内核功能</mark>，因此
应用范围更广，适配速度更快。

某些高级场景可能会需要更新的内核，但这些场景很少。接下来介绍 BPF CO-RE 用户侧机制
时会讨论到这样的场景。

# 4 BPF CO-RE：用户侧经验

接下来看几个真实世界中 BPF CO-RE 的典型场景，以及它是如何解决面临的一些问题的。
我们将看到，

* 一些可移植性问题（例如，兼容 struct 内存布局差异）能够处理地非常透明和自然，
* 而另一些则需要通过显式处理的，具体包括，

    * 通过 `if`/`else` 条件判断（**而不是 BCC 中的那种条件编译 `#ifdef`/`#else`**）。
    * BPF CO-RE 提供的其他一些额外机制。

## 4.1 摆脱内核头文件依赖

内核 BTF 信息除了用来做字段重定位之外，还可以用来<mark>生成一个大的头文件</mark>（"`vmlinux.h`"），
这个头文件中**<mark>包含了所有的内部内核类型，从而避免了依赖系统层面的内核头文件</mark>**。

通过 `bpftool` 获得 `vmlinux.h`：

```shell
$ bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
```

有了 `vmlinux.h`，就**无需再像通常的 BPF 程序那样 `#include <linux/sched.h>`、`#include <linux/fs.h>` 等等头文件**，
现在只需要 `#include "vmlinux.h"`，**<mark>也不用再安装 kernel-devel </mark>**了。

`vmlinux.h` 包含了**所有的内核类型**：

* 作为 UAPI 的一部分暴露的 API
* 通过 `kernel-devel` 暴露的内部类型
* 其他一些**通过任何其他方式都无法获取的内部内核类型**

不幸的是，<mark>BPF（以及 DWARF）并不记录 <code>#define</code> 宏，因此某些常用
的宏可能在 vmlinux.h 中是缺失的</mark>。但这些没有记录的宏中
，最常见的一些已经在 <a href="https://github.com/libbpf/libbpf/blob/master/src/bpf_helpers.h">bpf_helpers.h</a>
（libbpf 提供的内核侧”库“）提供了。

## 4.2 读取内核结构体字段

最常见和最典型的场景就是从某些内核结构体中读取一个字段。

### 4.2.1 例子：读取 `task_struct->pid` 字段

假设我们想**读取 `task_struct` 中的 `pid` 字段**。

#### 方式一：BCC（可移植）

用 BCC 实现，代码很简单：

```c
    pid_t pid = task->pid;
```

BCC 有强大的<mark>代码重写（rewrite）</mark>能力，能自动将以上代码**<mark>转换成一次 bpf_probe_read() 调用</mark>**
（但**有时重写之后的代码并不能正确**，具体取决于表达式的复杂程度）。

`libbpf` 没有 BCC 的代码重写魔法（code-rewriting magic），但提供了几种其他方式来
实现同样的目的。

#### 方式二：`libbpf` + `BPF_PROG_TYPE_TRACING`（不可移植）

如果使用的是最近新加的 `BTF_PROG_TYPE_TRACING` 类型 BPF 程序，那校验器已经足够智
能了，能<mark>原生地理解和记录 BTF 类型、跟踪指针，直接（安全地）读取内核内存
</mark>，

```c
    pid_t pid = task->pid;
```

从而**避免了调用 `bpf_probe_read()`**，格式和语法更为自然，而且**<mark>无需编译器重写</mark>**（rewrite）。
但此时，这段代码还不是可移植的。

#### 方式三：`BPF_PROG_TYPE_TRACING` + CO-RE（可移植）

要将以上 `BPF_PROG_TYPE_TRACING` 代码其变成可移植的，只需将待访问字段 `task->pid`
放到编译器内置的一个名为
<mark><code>__builtin_preserve_access_index()</code></mark> 的宏中：

```c
    pid_t pid = __builtin_preserve_access_index(({ task->pid; }));
```

这就是全部工作了：这样的程序<mark>在不同内核版本之间是可移植的</mark>。

#### 方式四：libbpf + CO-RE `bpf_core_read()`（可移植）

如果使用的内核版本还没支持 `BPF_PROG_TYPE_TRACING`，就必须显式地使用 `bpf_probe_read()`
来读取字段。

Non-CO-RE libbpf 方式：

```c
    pid_t pid;
    bpf_probe_read(&pid, sizeof(pid), &task->pid);
```

有了 CO-RE+libbpf，我们有<mark>两种方式实现这个目的</mark>。

第一种，直接将 `bpf_probe_read()` 替换成 `bpf_core_read()`：

```c
    pid_t pid;
    bpf_core_read(&pid, sizeof(pid), &task->pid);
```

`bpf_core_read()` 是一个很简单的宏，直接展开成以下形式：

```c
    bpf_probe_read(&pid, sizeof(pid), __builtin_preserve_access_index(&task->pid));
```

可以看到，第三个参数（`&task->pid`）放到了前面已经介绍过的编译器 built-int 中，
这样 clang 就能记录该字段的重定位信息，实现可移植。

第二种方式是使用 `BPF_CORE_READ()` 宏，我们通过下面的例子来看。

### 4.2.2 例子：读取 `task->mm->exe_file->f_inode->i_ino` 字段

这个字段表示的是当前进程的可执行文件的 inode。
来看一下访问嵌套层次如此深的结构体字段时，面临哪些问题。

#### 方式一：BCC（可移植）

用 BCC 实现的话可能是下面这样：

```c
    u64 inode = task->mm->exe_file->f_inode->i_ino;
```

BCC 会对这个表达式进行重写（rewrite），<mark>转换成 4 次 bpf_probe_read()/bpf_core_read() 调用</mark>，
并且每个中间指针都需要一个<mark>额外的临时变量</mark>来存储。

#### 方式二：BPF CO-RE（可移植）

下面是 BPF CO-RE 的方式，仍然很简洁，但无需 BCC 的代码重写（code-rewriting magic）：

```c
    u64 inode = BPF_CORE_READ(task, mm, exe_file, f_inode, i_ino);
```

另外一个变种是：

```c
    u64 inode;
    BPF_CORE_READ_INTO(&inode, task, mm, exe_file, f_inode, i_ino);
```

### 4.2.3 其他与字段读取相关的 CO-RE 宏

* `bpf_core_read_str()`：可以直接替换 Non-CO-RE 的 `bpf_probe_read_str()`。
* `BPF_CORE_READ_STR_INTO()`：与 `BPF_CORE_READ_INTO()` 类似，但会对最后一个字段执行 `bpf_probe_read_str()`。
* `bpf_core_field_exists()`：判断字段是否存在，

    ```c
        pid_t pid = bpf_core_field_exists(task->pid) ? BPF_CORE_READ(task, pid) : -1;
    ```

* `bpf_core_field_size()`：判断字段大小，同一字段在不同版本的内核中大小可能会发生变化，

    ```c
        u32 comm_sz = bpf_core_field_size(task->comm); /* will set comm_sz to 16 */
    ```

* `BPF_CORE_READ_BITFIELD()`：通过**直接内存读取**（direct memory read）方式，读取比特位字段
* `BPF_CORE_READ_BITFIELD_PROBED()`：底层会调用 `bpf_probe_read()`

    ```c
    struct tcp_sock *s = ...;
    
    /* with direct reads */
    bool is_cwnd_limited = BPF_CORE_READ_BITFIELD(s, is_cwnd_limited);
    
    /* with bpf_probe_read()-based reads */
    u64 is_cwnd_limited;
    BPF_CORE_READ_BITFIELD_PROBED(s, is_cwnd_limited, &is_cwnd_limited);
    ```

## 4.3 处理内核版本和配置差异

某些情况下，BPF 程序必须处理不同内核版本之间**常用内核结构体的非细微差异**。例如，

* 字段<mark>被重命名了</mark>：对依赖这个字段的调用方来说，这其实变成了一个新字段（但语义没变）。
* 字段名字没变，但<mark>表示的意思变了</mark>：例如，从 4.6 之后的某个内核版本开始，
  `task_struct` 的 `utime` 和 `stime` 字段，原来单位是 jiffies，现在变成了 nanoseconds，因此
  调用方必须自己转换单位。
* 需要从内核提取的某些数据是**与内核配置有直接关系**，某些内核在编译时并<mark>没有将相关代码编译进来</mark>。
* 其他一些无法用单个、通用的类型定义来适用于所有内核版本的场景。

对于这些场景，BPF CO-RE 提供了两种互补的解决方式；

* libbpf 提供的 <mark>extern Kconfig 变量</mark>
* <mark>struct flavors</mark>

### libbpf 提供的 `externs` Kconfig 全局变量

* 系统中已经有一些”知名的“变量，例如 `LINUX_KERNEL_VERSION`，表示当前内核的版本。
  BPF 程序能<mark>用 extern 关键字声明这些变量</mark>。
* 另外，BPF 还能用 extern 的方式声明 <mark>Kconfig 的某些 key 的名字</mark>（例如
  `CONFIG_HZ`，表示内核的 HZ 数）。

接下来的事情交给 libbpf，它会将这些变量<mark>分别匹配到系统中相应的值</mark>（都是常量），
并保证这些 extern 变量<mark>与全局变量的效果是一样的</mark>。

此外，由于这些 extern ”变量“都是常量，因此 **BPF 校验器**能用它们来做一些
<mark>高级控制流分析和死代码消除</mark>。

下面是个例子，如何用 BPF CO-RE 来提取线程的 CPU user time：

```c
    extern u32 LINUX_KERNEL_VERSION __kconfig;
    extern u32 CONFIG_HZ __kconfig;
    
    u64 utime_ns;
    
    if (LINUX_KERNEL_VERSION >= KERNEL_VERSION(4, 11, 0))
        utime_ns = BPF_CORE_READ(task, utime);
    else
        /* convert jiffies to nanoseconds */
        utime_ns = BPF_CORE_READ(task, utime) * (1000000000UL / CONFIG_HZ);
```

### struct flavors

有些场景中，**不同版本的内核中有不兼容的类型**，无法用单个通用结构体来为所有内核
编译同一个 BPF 程序。struct flavor 在这种情况下可以派上用场。

下面是一个例子，提取 `fs`/`fsbase`（前面提到过，字段名字在内核版本升级时改了）来
做一些 thread-local 的数据处理：

```c
/* up-to-date thread_struct definition matching newer kernels */
struct thread_struct {
    ...
    u64 fsbase;
    ...
};

/* legacy thread_struct definition for <= 4.6 kernels */
struct thread_struct___v46 {   /* ___v46 is a "flavor" part */
    ...
    u64 fs;
    ...
};

extern
int LINUX_KERNEL_VERSION __kconfig;
...

struct thread_struct *thr = ...;
u64 fsbase;
if (LINUX_KERNEL_VERSION > KERNEL_VERSION(4, 6, 0))
    fsbase = BPF_CORE_READ((struct thread_struct___v46 *)thr, fs);
else
    fsbase = BPF_CORE_READ(thr, fsbase);
```

在这个例子中，对于 `<=4.6` 的内核，我们将原来的 `thread_struct` 定义为了 `struct thread_struct___v46`。
<mark>双下划线及其之后的部分</mark>，即 `___v46`，**<mark>称为这个 struct 的 “flavor”</mark>**。

<mark>flavor 部分会被 libbpf 忽略</mark>，这意味着**在目标机器上执行字段重定位时，
`struct thread_struct__v46` 匹配的仍然是真正的 `struct thread_struct`**。

这种方式使得我们能在单个 C 程序内，为同一个内核类型定义不同的（而且是不兼容的）
类型，然后在运行时（runtime）取出最合适的一个，这就是用
<mark>type cast to a struct flavor</mark> 来提取字段的方式。

没有 struct flavor 的话，就无法真正实现像上面那样“编译一次”，然后就能在不同内核
上都能运行的 BPF 程序 —— 而只能用`#ifdef` 来控制源代码，编译成两个独立的 BPF
程序变种，在运行时（runtime）由控制应用根据所在机器的内核版本选择其中某个变种。
所有这些都添加了不必要的复杂性和痛苦。
相比之下，**以上 BPF CO-RE 方式虽然不是透明的**（上面的代码中也包含了内核
版本相关的逻辑），但允许用熟悉的 C 代码结构解决即便是这样的高级场景的问题。

## 4.4 根据用户提供的配置修改程序行为

BPF 程序**知道内核版本和配置信息**，有时还不足以判断如何 —— 以及以何种方式 —— 从该版本的内核获取数据。
在这些场景中，<mark>用户空间控制应用</mark>（control application）可能是唯一知道
究竟需要做哪些事情，以及需要启用或禁用哪些特性的主体。
这通常是**在用户空间和 BPF 程序之间**<mark>通过某种形式的配置数据来通信</mark>的。

### BPF map 方式

要实现这种目的，一种不依赖 BPF CO-RE 的方式是：<mark>将 BPF map 作为一个存储配置
数据的地方</mark>。BPF 程序**从 map 中提取配置信息，然后基于这些信息改变它的控制流**。

但这种方式有几个主要的缺点：

1. BPF 程序每次执行 <mark>map 查询操作，都需要运行时开销</mark>（runtime overhead）。

    多次查询累积起来，开销就会比较比较明显，尤其在一些高性能 BPF 应用的场景。

1. <mark>配置内容</mark>（config value），虽然在 **BPF 程序启动之后就是不可变和只读**
  （immutable and read-only）的了，但 <mark>BPF 校验器在校验时扔把它们当作未知的黑盒值</mark>。

    这意味着校验器<mark>无法消除死代码，也无法执行其他高级代码分析</mark>。进一步，
    这意味着我们无法将代码逻辑放到 map 中，例如，能处理不同内核版本差异的 BPF 代
    码，因为 map 中的内容对校验器都是黑盒，因此校验器对它们是不信任的 ——
    即使用户配置信息是安全的。

### 只读的全局数据方式

这种（确实复杂的）场景的<mark>解决方案：使用只读的全局数据</mark>（read-only global data）。
这些数据是在 **BPF 程序加载到内核之前，由控制应用设置**的。

* 从 <mark>BPF 程序的角度</mark>看，这就是**正常的全局变量访问，没有任何 BPF map lookup 开销** ——
  全局变量<mark>实现为一次直接内存访问</mark>。
* 控制应用方面，在 BPF 程序加载到内核之前设置初始的配置值，此后配置值就是全局可
  访问且只读（well known and read-only）的了。

    这<mark>使得 BPF 校验器能将它们作为常量对待</mark>，然后就能执行**高级控制流分析**
    （advanced control flow analysis）来消除死代码。

因此，针对上面那个例子，

* 某些老内核的 BPF 校验器就能推断出，例如，代码中某个未知的 BPF helper 不可能会用到，接下来就可以将相关代码直接移除。
* 而对于新内核来说，应用提供的配置（application-provided configuration）会所有不
  同，因此 BPF 程序就能用到功能更强大的 BPF helper，而且这个逻辑能成功通过 BPF 校验器的验证。

下面的 BPF 代码例子展示了这种用法：

```c
/* global read-only variables, set up by control app */
const bool use_fancy_helper;
const u32 fallback_value;

...

u32 value;
if (use_fancy_helper)
    value = bpf_fancy_helper(ctx);
else
    value = bpf_default_helper(ctx) * fallback_value;
```

从用户空间方面，通过 BPF skeleton 可以很方便地做这种配置。BPF skeleton 的讨论不在
本文讨论范围内，使用它来简化 BPF 应用的例子，可参考内核源码中的
<a href="https://github.com/torvalds/linux/tree/master/tools/bpf/runqslower">runqslower tool</a>。

# 5 总结

BPF CO-RE 的目标是：

* 作为一种**<mark>简单的方式</mark>**帮助 BPF 开发者解决**<mark>简单的移植性问题</mark>**（例如读取结构体的字段），并且
* 作为一种**<mark>仍然可行（不是最优，但可容忍）的方式</mark>**
  解决**<mark>复杂的移植性问题</mark>**（例如不兼容的数据结构改动、复杂的用户空间控制条件等）。
* 使得开发者能遵循”一次编译、到处运行“（Compile Once – Run Everywhere）范式。

这是通过几个 BPF CO-RE 模块的组合实现的：

1. `vmlinux.h` <mark>消除了对内核头文件的依赖</mark>；
1. <mark>字段重定位信息</mark>（字段偏置、字段是否存在、字段大小等等）使得<mark>从内核提取数据这个过程变得可移植</mark>；
1. libbpf 提供的 <mark>Kconfig extern 变量</mark>允许 BPF 程序适应不同的内核版本 —— 以及配置相关的差异；
1. 当其他方式都失效时，应用提供的<mark>只读配置和 struct flavor 最终救场</mark>，能解决
   任何需要复杂处理的场景。

**要成功地编写、部署和维护可移植 BPF 程序，并不是必须用到所有这些 CO-RE 特性**。
只需选择若干，用最简单的方式解决你的问题。

BPF CO-RE 使我们回到了熟悉、自然的工作流程：将 BPF C 源码编译成二进制，然后将
二进制文件分发到目标机器进行部署和运行 —— 
无需再随着应用一起分发重量级的编译器库、无需消耗宝贵的运行时资源做运行时编
译（runtime compilation），也无需等到运行之前才能捕捉一些细微的编译时错误（
compilation errors in runtime）了。

# 参考资料

1. BPF CO-RE presentation from LSF/MM2019 conference: <a href="http://vger.kernel.org/bpfconf2019.html#session-2">summary</a>, <a href="http://vger.kernel.org/bpfconf2019_talks/bpf-core.pdf">slides</a>.
1. Arnaldo Carvalho de Melo’s presentation <a href="http://vger.kernel.org/~acme/bpf/devconf.cz-2020-BPF-The-Status-of-BTF-producers-consumers/#/29">"BPF: The Status of BTF"</a> dives deep into BPF CO-RE and dissects the runqslower tool quite nicely.
1. <a href="https://facebookmicrosites.github.io/bpf/blog/2018/11/14/btf-enhancement.html">BTF deduplication algorithm</a>
