---
layout    : post
title     : "[译] C 不是一门低层（low-level）语言（acmqueue, 2018）"
date      : 2019-11-23
lastupdate: 2019-11-23
categories: c language
---

### 译者序

本文翻译自 [C Is Not a Low-Level
Language](https://queue.acm.org/detail.cfm?id=3212479)，acmqueue Volume 16，
issue 2（2018.04.30），原文副标题为 **Your computer is not a fast PDP-11**。作者
David Chisnall。

本文观点非常独特，甚至招来了老牌黑客 Eric S. Raymond（开源运动发起者之一，开源
领袖，代表作《大教堂和大集市》等）写了一篇长文作为回帖：[Embrace the
SICK](http://esr.ibiblio.org/?p=7979)。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

## 目录

1. [什么是低层语言？](#ch_1)
    * 1.1 低层语言：定义
    * 1.2 抽象机器
2. [快速 PDP-11 模拟器](#ch_2)
    * 2.1 C 抽象机器
    * 2.2 指令集并行（ILP）
    * 2.3 寄存器重命名引擎
    * 2.4 扁平内存和缓存
3. [优化 C](#ch_3)
    * 3.1 编译器复杂度
    * 3.2 指令矢量化
    * 3.3 结构体填充
    * 3.4 SROA 和 loop unswitching（循环外提）
    * 3.5 未初始化变量、未定义行为和未定义值
    * 3.6 小结
4. [理解 C](#ch_4)
    * 4.1 初始化结构体时，填充部分是否会被初始化？
    * 4.2 内存模型和指针
5. [设想一个 Non-C 处理器](#ch_5)
    * 5.1 Sun/Oracle UltraSPARC
    * 5.2 ARM SVE
    * 5.3 缓存一致性、对象可变/不可变性、GC
    * 5.4 专门为速度设计的处理器

----

我们有必要花一些时间来思考最近的 **Meltdown 和 Spectre** 漏洞产生的根本原因。

这两个漏洞都与处理器的**预测执行**（speculatively executing）相关：预测执行中的
的指令会**经过某些类型的访问检查**（access check），使得攻击者可以通过一个**旁路
（side channel）来观测执行结果**。

导致这些漏洞的特性（features），以及其他一些东西，助长了 C 程序员们的如下观念：
他们是**在用一门低层语言进行编程（programming in a low-level language）**。而事
实上这种观念以及错误了几十年了。

**处理器厂商**并不是唯一需要对此负责的。那些**开发 C/C++ 编译器的人**也助长了
这种错误观念。

<a name="ch_1"></a>

# 1. 什么是低层语言？

## 1.1 低层语言：定义

计算机科学的先驱 Alan Perlis 给低层语言（low-level languages）下的定义是：

> 如果用一门语言编写的程序**需要处理不相关的东西**，那这就是一门低层语言。
>
> "A programming language is low level when its programs require attention to
>  the irrelevant." [5]

虽然 C 语言适用于这个定义，但是，它并没有传达出到**人们期望从一门低层语言中获得
什么**。

**很多语言属性（attributes）都会导致人们认为某种语言是低层语言**。我们不妨想象这
样一个场景，各种编程语言排列在一个 continuum（连续统一体，任何相邻的两个实体都非
常相似，但最两端的两个实体却差异非常大。译者注）之上，continuum 的一端是汇编语
言，另一端是通往 Starship Enterprise 的计算机的接口（interface）。**低层语言“靠近金
属”（close to the metal），而高级语言更靠近人类的思考方式**。

## 1.2 抽象机器

一门语言要“靠近金属”，那它必须提供一种**抽象机器**（abstract machine），该机器能
够方便地映射到**目标平台暴露的抽象**（abstractions exposed by the target platform）。
基于以上分析，我们很容易得出这样一个结论：**对于 PDP-11 来说，C 是一门低层语言**。
二者都描述了这样一个模型：

* 程序是顺序执行的（programs executed sequentially）
* 内存空间是扁平的（memory was a flat space）
 
甚至 C 语言的前递增（`++i`）和后递增（`i++`）运算符都能一个萝卜一个坑地对应到
PDP-11 的不同寻址模式。

<a name="ch_2"></a>

# 2. 快速 PDP-11 模拟器

**Spectre 和 Meltdown 漏洞的根本原因是**：处理器架构（processor architects）不仅在
试图构建**快速处理器**（fast processors），而且是**与 PDP-11 暴露相同抽象机器的快
速处理器**（fast processors that expose the same abstract machine as a PDP-11）。
这是其本质原因，因为它令 C 程序员们继续认为：这是一门靠近底层硬件的编程语言。

## 2.1 C 抽象机器

C 代码提供的是一个**大部分情况下都是串行执行的**（mostly serial）抽象机器（到了 C11
之后已经是一个**完全串行的**机器 —— 如果不包含任何非标准的厂商扩展的话）。

创建新线程是一个库操作（library operation），而且已知开销很大，因此处理器希望依
靠**指令级并行**（ILP，instruction-level parallelism）来使其执行单元（execution
units）持续执行 C 代码。处理器会对相邻的操作（adjacent operations）进行检查，为
其中一些操作发射（issue）独立指令以使其并行执行。在继续允许程序员编写大部分情况
下都是顺序执行的代码（mostly sequential code）的前提下，这显著增加了处理器的复杂
度（和功耗）。与此相反，GPU 取得了极高的性能而没有涉及任何此类逻辑，而它的代价是
需要编写显式的并行程序。

## 2.2 指令集并行（ILP）

对**高度的指令级并行**（high ILP）的追求，是产生 Spectre 和 Meltdown 漏洞的直接
原因。

一个现代 Intel 处理器可以在同一时刻**并行执行多达 180 条指令**（与此形成强烈对比
的是**顺序执行的 C 抽象机器**，该机器期望每个操作执行完成之后，下一个操作才
会执行）。根据一些典型的统计，**C 代码中平均每 7 条指令就会有一个分支**。因此，
在单线程的情况下要**让处理器的流水线（pipeline）达到饱和**，那就需要**猜测**随后
的 25 个分支的目标。

> 180 / 7 ≈ 25 （译者注）

这再一次增加了复杂度；而且这也意味着，那些猜错的结果随后就直接丢弃了，从功耗的角
度来说也不是理想的方式。**这些被丢弃的尝试有着可见的副作用**，Spectre 和
Meltdown 攻击就是利用了这些副作用。

## 2.3 寄存器重命名引擎

在一个现代高端处理器上，**寄存器重命名**（register rename）引擎最消耗 die area
和功耗的。更糟糕的是，只要有任何指令正在执行，就无法将其关闭或降低功能（power
gated），这在硅的灰暗年代 —— 也就是晶体管（transistors）非常便宜，而功率晶体管（
powered transistors）非常昂贵 —— 的年代非常不方便。

寄存器重命名单元在 GPU 上并不
存在，对于后者，并行依靠的是多个线程并行而不是尝试**从本质上是标量的代码（
intrinsically scalar code）中提取指令级并行**。

如果指令之间没有任何关联，无需重排
序（reorder），那寄存器重命名（register renaming）就不需要。

## 2.4 扁平内存和缓存

考虑 C 抽象机器的内存模型中另一个核心部分：**扁平内存**（flat memory）。**在过去
的二十多年中，这一点其实也是不成立的**。

现代处理器通常在寄存器和主存之间有**三级
缓存**（three levels of cache），其目的是为了**隐藏延迟**（hide latency）。

缓存（cache），如其名字所示，对程序员是隐藏的，因此对 C 是不可见的。有效地使用缓
存对于在现代处理器上快速地执行代码非常重要，**抽象机器完全隐藏了缓存**，但是
**程序员必须知道缓存的实现细节**（例如，两个 64 字节对齐的值可能会被放到同一
个缓存行）**才能写出高效的代码**。

<a name="ch_3"></a>

# 3. 优化 C

人们普遍认为，低层语言的一大优势是快（fast）。尤其重要的一点是，将这些语言转换成
能够快速执行的代码，应该不需要特别复杂的编译器。在讨论其他一些语言时，有些人会认
为**一个足够优秀的编译器可以使得一门语言变得快速**，但当讨论到 C 时，这一点经常
不再成立。

## 3.1 编译器复杂度

通过**简单的转换**（simple translation）就能变成**快速代码**（fast code）对 C 是
不成立的。

尽管处理器架构师在设计能够快速运行 C 代码的芯片上投入了艰苦卓绝的
努力，但 C 程序员期望的性能水平并非来源于此，而是来自**复杂地难以想象的编译器变换**（
compiler transforms）。Clang 编译器，连同 LLVM 中相关的部分，大概有 200 万行代码
。即使只统计其中和快速执行 C 相关的分析和变换代码，也有将近 20 万行（不包
含注释和空行）。

## 3.2 指令矢量化

例如，在 C 中，处理大量数据通常是写一个循环，在循环中顺序地处理每个元素。

要在现
代 CPU 上最优地执行这段代码，编译器必须首先判断每次迭代是不是独立的。这里 C 的
`restrict` 关键字可能会排上用场。`restric` 保证一个指针的写操作不会与另一个
指针的读操作彼此产生任何影响（或者如果有，程序就会得到无法预期的结果）。相比于
Fortran 这样的语言，C 中这类信息太不通用了（far more limited），这也是为什么 C
无法在高性能计算领域取代 Fortran 的一个重要原因。

编译器确定循环的每次迭代都是独立的之后，下一步就是尝试对结果进行向量化/矢量化（
vectorize the result），因为相比于标量代码（scalar code），现代处理器的矢量代码
（vector code ）可以取得四倍到八倍的性能。为这种处理器设计的低层语言（low-level
language）能够支持任意长度的原生矢量类型。**LLVM IR**（intermediate representation）
就是一个例子，将一个大矢量的操作拆分成很多小矢量，永远要比构建大矢量操作简单。

## 3.3 结构体填充

但在这里，优化器必须首先保证 C 的内存布局。

C 保证 structures with the same prefix（同一结构体的不同引用？）可以无差别的使用
，而且它将结构体字段的偏置（offset）暴露到语言中。这意味着**编译器无法随意对结构体
字段进行重排序或者插入填充**来提高矢量性（例如，将一个数组结构体转换成一个结构体数
组，或者相反）。对于低层语言来说，这些不一定是一个问题，因为对于低层语言来说，对
数据结构布局的细粒度控制属于特性（feature），但对于 C 来说，这会导致生产快速代码
更加困难。

C 还需要对结构体的结尾处进行填充，因为它不保证对数组进行填充。**在 C 规范中，填
充是一个特别复杂的部分**，而且与这门语言的其他部分衔接地非常差。例如，**要保证程
序员能够用类型无关的（type-oblivious）方式（例如，`memcpy`）比较两个结构体变量**
，因此结构体的拷贝必须要保留它的填充。在一些实验中发现，对于某些 workload，总运
行时的相当一部分都花在拷贝这些填充上（通常都是一些 size 和对齐都很糟糕的结构体）。

## 3.4 SROA 和 loop unswitching（循环外提）

考虑 **C 编译器的两个核心优化**：

1. SROA（scalar replacement of aggregates，聚合体标量替换）
1. loop unswitching（循环外提）

SROA 尝试**将结构体（以及固定长度数组）替换为独立变量**。这使得编译器能够将对结
构体不同字段的访问变成独立的，甚至，如果能证明对结果没有影响，那可以完全去掉去结
构体的访问。在某些情况下，这项优化会涉及删除填充，但并非所有情况。

第二项优化，循环外提，将一个**包含条件判断的循环**转换成一个**包含循环的条件判断
—— 在 `if` 和 `else` 中都复制一份这个循环**。这改变了流的控制（flow control），
与程序员所假设的低层语言执行方式相违背。另外，考虑到 C 的未定义值（unspecified
values）和未定义行为（undefined behavior）概念时，这还会产生严重的问题。

## 3.5 未初始化变量、未定义行为和未定义值

在 C 中，读取未初始化变量（uninitialized variable）得到的是一个未定义值（
unspecified value），并且每次读取时，这个值可以是任意的。

这一点非常重要，因为它
使得惰性回收页面（lazy recycling of pages）之类的行为成为可能：例如在 FreeBSD 中
，`malloc` 的实现通知操作系统这些页面当前是未使用的，而操作系统向这些页面的第一
次写操作则宣告这些页面开始被使用了。对新分配出来的内存（newly malloced memory）
的读操作可能会读到老数据（old value）；之后，操作系统可能会重用底层的物理页；然
后，对这个页面的某个不同位置的一次写操作会将这个页面初始化为零（zeroed page）。
从同一位置的第二次读操作读到的就是零值（zero value）。

如果流控制（flow control）中使用了一个未定义值（例如，`if` 语句中的条件变量
），那结果将是**未定义行为**（undefined behavior）：**任何行为都可能会发生（anything is
allowed to happen）**。考虑循环不会被执行（或者说，执行 0 次）情况下的循环外提优
化：

* 没有优化的版本中，整个循环体都是死代码
* 循环外提优化之后，在循环的外面会有一层条件判断，而这个值可能是未初始化的。原来
  的死代码（dead code）现在变成了未定义行为（undefined behavior）代码

只要深入查看 C 的语义，就会发现还有很多的优化会产生类似的错误结果。

## 3.6 小结

最后总结一下：**让 C 代码快速地运行是可能的，但需要为此投入几千“人·年”（
person-year）的精力开发一个足够智能的编译器 —— 而且即便如此，还得包含一些与 C 语
言规范不兼容的（violate）实现**。

如果编译器开发者想让 C 程序员继续相信自己使用的是
一门快速语言，那他们就需要使后者继续相信自己是在编写“靠近金属”的代码，但同时编
译器又必须生成行为与此迥异的代码。

<a name="ch_4"></a>

# 4. 理解 C

低层语言的最大特点之一是：**程序员能够轻松地理解这门语言的抽象机器是如何映射到底
层的物理机器的**。

在 PDP-11 上，这一点显然是成立的，**每个 C 表达
式都能够映射到一条或两条指令**。类似地，**编译器能够直接将局部变量（local
variable）放到栈槽（stack slot）上，以及将 primitive 类型映射成 PDP-11 能够原
生操作的东西**。

自此之后，为了维持 **C 能够轻松地映射到底层硬件**以及 **C 能够产生快速代码**
的幻象，C 的实现开始变得越来越复杂。

## 4.1 初始化结构体时，填充部分是否会被初始化？

2015 年一份针对 C 程序员、编译器开发者和标准委员
会成员的调查揭示了几个对 C 的理解的问题。

例如，C 允许实现中对结构体插入填充（但
不允许对数组）来保证所有字段在目标平台上都有良好的对齐。如果用 0 初始化结构体
内存，然后设置某些字段的值，那填充的比特也会被 0 初始化吗？

根据调查的结果，36%的被调查者认为会，29% 的回答不知道。答案是，这和编译器的实现
（以及优化级别）相关，有的会被初始化，有的不会。

这是一个非常细节的例子，但很大一部分程序员对此的理解要么是错的，要么是不知道。

## 4.2 内存模型和指针

当介绍指针时，C 的语义会更加令人困惑。BCPL 模型非常简单：values are words。

1. 每个 word 要么是一段数据，要么是一段数据的地址
1. 内存是一段扁平的存储单元数组（flat array of storage cells），用地址索引（
   indexed by address）

相比之下，C 模型在设计中允许在不同平台上实现，包括 segmented
architectures （其中一个指针可能是一个 segment ID 和一个 offset），甚至带垃圾回收
的虚拟机。C 规范中仔细严格地限制了指针操作，以避免在这类系统上发生问题。作为对
Defect Report 260 [1] 的回应，指针的定义中引入了 pointer provenance（来源）的
概念：

> "Implementations are permitted to track the origins of a bit pattern and treat
> those representing an indeterminate value as distinct from those representing a
> determined value. They may also treat pointers based on different origins as
> distinct even though they are bitwise identical."

不幸的是，“provenance” 一词并未出现在 C11 规范中，因此其只能由编译器的实现来
决定。例如，如果一个指针先转换成整形再转换回指针，那最后这个指针是否还保留了它的
provenance？GCC (GNU Compiler Collection) 和 Clang 对此的实现就是不同的。编译器
完全决定了两次不同的 `malloc` 或栈分配（stack allocations）得到的结果是不同的
，即使**按位对比**（bitwise comparison）显式这两个指针指向的是同一个地址。

这个误解本质上并不完全是学术性质的。例如，在有符号整形溢出（signed integer
overflow，这在 C 中是未定义行为）和没有检查指针是否为空就解引用（dereferenced a
pointer before a null
check）的代码中发现了安全漏洞。对于后者（[CVE-2009-1897](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2009-1897)），
代码中没有检查指针是否为空就对其解引用，这给编译器的暗示就是这个指针不可能为空，因为在
C 中空指针的解引用是未定义行为，因此不可能会发生。

> 以上漏洞的修复。译者注。
>
> ```shell
> diff --git a/drivers/net/tun.c b/drivers/net/tun.c
> index b393536..027f7ab 100644
> --- a/drivers/net/tun.c
> +++ b/drivers/net/tun.c
> @@ -486,12 +486,14 @@ static unsigned int tun_chr_poll(struct file *file, poll_table * wait)
>  {
>  	struct tun_file *tfile = file->private_data;
>  	struct tun_struct *tun = __tun_get(tfile);
> -	struct sock *sk = tun->sk;
> +	struct sock *sk;
>  	unsigned int mask = 0;
>
>  	if (!tun)
>  		return POLLERR;
>
> +	sk = tun->sk;
> +
>  	DBG(KERN_INFO "%s: tun_chr_poll\n", tun->dev->name);
> ```

鉴于以上这些问题，**很难说程序员充分理解了他们的 C 程序将如何映射到底层架构**。

<a name="ch_5"></a>

# 5. 设想一个 Non-C 处理器

Spectre 和 Meltdown 漏洞的**修复方案会导致严重的性能下降，抹平了过去十年微架构领
域的一大部分进展**。

也许现在是时候停止继续让 C 代码变快的努力了，而应该思考：
**如果要在处理器上设计一个快速的编程模型，那这个模型应该是什么样子**？

已经有很多没有遵循传统的 C 代码的设计例子，它们可以提供一些 inspiration。 

## 5.1 Sun/Oracle UltraSPARC

高度多线程的芯片（比如 Sun/Oracle 的 UltraSPARC)，不需要大量缓存来使它们的执行单
元保持饱和。研究型处理器 [2] 已经将这种概念扩展到数量非常庞大的的硬件调度线程。

这种设计背后的核心理念是：只要有足够的高级别并行度（high-level parallelism），
就可以将那些等待从内存读取数据的线程挂起，然后用其他线程的指令来填充执行单元。

这种设计的问题是，可能导致 C 程序产生几个繁忙线程（busy threads）。

## 5.2 ARM SVE

ARM 的 SVE (Scalar Vector Extensions) —— 以及 Berkeley 的类似工作 [4] —— 提供了另一种
程序和硬件之间更好的接口的尝试。

传统矢量单元暴露几种固定大小（fixed-sized）的矢量操作，
期望编译器将算法映射到几种可用的单位大小。相比之下，SVE 接口由程序员描述可用的并
行级别，依赖硬件将它们映射到可用的执行单元。在 C 中使用这种接口比较复杂，因为自
动向量化器（autovectorizer ）必须从循环结构中推断可用的并行级别。但在函数式风格
的语言中用 map 操作产生这种代码非常容易：映射之后的数组长度就是可用的并行程度。

## 5.3 缓存一致性、对象可变/不可变性、GC

缓存很大，但大小并不是它们如此复杂的唯一原因。

**缓存一致性协议是现代 CPU 最难的部分**，因为要同时保证缓存的快速和正确。这里涉
及的大部分复杂度来自于要支持这样一种语言：**无条件地期望数据同时是共享的和可变的
（shared and mutable）**。

作为对比，考虑
Erlang 风格的机器，其中的每个对象要么是线程私有的（thread-local），要么是不可变
的（immutable ）（实际上 Erlang 的模型更加简化：每个线程只有一个可变对象）。
为这样的系统设计一套缓存一致性协议需要考虑两方面：可变性和共享性。一个软件线程从
一个处理器迁移到另一个处理器需要将它的缓存显式地置为无效，但这是一个相对不寻常的
操作。

不可变对象能够进一步简化缓存，并且可以使几种操作开销更低。Sub Lab 的
Project Maxwell noted that the objects in the
cache and the objects that would be allocated in a young generation are almost
the same set. 如果对象在需要被驱逐出缓存之前就无效了，那就无需将它们写回内存，这
可以节省大量的功耗。Project Maxwell 提出了一种 young-generation garbage
collector (and allocator)，可以在缓存中运行，使得内存可以被快速回收。有了存储不
可变对象的堆和一个存储可变对象的栈（mutable stack），GC 就变成了一个非常简单的状
态机，很容易在硬件上实现，最终会使得缓存更新，并且使用更加高效。

## 5.4 专门为速度设计的处理器

一个专门为速度 —— 而不是为了在速度和支持 C 之间取得折中 —— 而设计的处理器，可能
会：

* 支持大量的线程
* 有很宽的向量单元（wide vector units）
* 有更简单的内存模型

在这样的系统上执行 C 代码会有问题，考虑到世界上既有的大量 C 代码，这样的处理器
在商业上不太可能会取得成功。

在软件开发领域流传着一个神话：并行编程很难。

Alan Kay 对此可能会很吃惊，因为他能够用一种 actor 模型语言来教很多小孩编程，而且
他们已经写出了已经在使用中的、包含 200 多个线程的程序。Erlang 程序员对此也会很吃
惊，因为他们已经习惯了编程几千个并行组件的程序。因此，更准确说法应该是：**用那些有
着和 C 类似的抽象机器的语言进行并行编程很难**，而且考虑到如今流行的并行硬件，从多
核 CPU 到众核 GPU，也从另一方面说明了 C 并不能很好的映射到现代硬件。

## References

1. C Defect Report 260. 2004; http://www.open-std.org/jtc1/sc22/wg14/www/docs/dr_260.htm.
2. Chadwick, G. A. 2013. Communication centric, multi-core, fine-grained
   processor architecture. Technical Report 832. University of Cambridge,
   Computer Laboratory; http://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-832.pdf.
3. Memarian, K., Matthiesen, J., Lingard, J., Nienhuis, K., Chisnall, D. Watson,
   R. N. M., Sewell, P. 2016. Into the depths of C: elaborating the de facto
   standards. Proceedings of the 37th ACM SIGPLAN Conference on Programming
   Language Design and Implementation: 1-15; http://dl.acm.org/authorize?N04455.
4. Ou, A., Nguyen, Q., Lee, Y., Asanović, K. 2014. A case for MVPs:
   mixed-precision vector processors. Second International Workshop on
   Parallelism in Mobile Platforms at the 41st International Symposium on
   Computer Architecture.
5. Perlis, A. 1982. Epigrams on programming. ACM SIGPLAN Notices 17(9).
