---
layout    : post
title     : "[译] 操作系统是什么？1954-1964 历史调查（2019）"
date      : 2020-02-01
lastupdate: 2020-02-01
categories: os
---

### 译者序

本文内容来自一篇调查综述 
[What is an Operating System? A historical investigation (1954–1964)](
https://halshs.archives-ouvertes.fr/halshs-01541602/document)。论文引用信息：

> Maarten Bullynck. What is an Operating System? A historical investigation
> (1954–1964). Reflections on Programming Systems. Historical and Philosophical
> Aspects, 2019. halshs-01541602v2

本文内容仅供学习交流，如有侵权立即删除。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

----
## 目录

1. [背景知识：操作系统及软硬件演变](#ch_1)
    * [1.1 50 年代中期](#ch_1.1)
        * [1.1.1 理念进步](#ch_1.1.1)
        * [1.1.2 内存新技术](#ch_1.1.2)
        * [1.1.3 I/O 新技术](#ch_1.1.3)
        * [1.1.4 软件发展](#ch_1.1.4)
        * [1.1.5 两个创新：硬件中断和 FORTRAN](#ch_1.1.5)
    * [1.2 60 年代中期](#ch_1.2)
        * [1.2.1 多道编程](#ch_1.2.1)
        * [1.2.2 存储进步：随机存储媒介的出现](#ch_1.2.2)
        * [1.2.3 推动者：从企业用户/研究机构转移到计算机制造商](#ch_1.2.3)
2. [操作系统是什么？早期系统分类](#ch_2)
    * [2.1 自动编程系统和操作系统](#ch_2.1)
        * [2.1.1 操作系统](#ch_2.1.1)
        * [2.1.2 编程系统](#ch_2.1.2)
        * [2.1.3 早期自动化编程系统分类](#ch_2.1.3)
    * [2.2 批处理系统](#ch_2.2)
        * [2.2.1 起源：商业数据处理和会计领域](#ch_2.2.1)
        * [2.2.2 IBM 701 Monitor](#ch_2.2.2)
        * [2.2.3 Open Shop vs. Closed Shop](#ch_2.2.3)
        * [2.2.4 1956：“第一个”操作系统诞生](#ch_2.2.4)
        * [2.2.5 IBM IBSYS 系统：监视器的监视器](#ch_2.2.5)
    * [2.3 集成系统（Integrated systems）](#ch_2.3)
        * [2.3.1 MIT Comprehensive System（综合系统）](#ch_2.3.1)
        * [2.3.2 集成系统](#ch_2.3.2)
        * [2.3.3 集成系统与批处理系统的区别](#ch_2.3.3)
        * [2.3.4 解释型系统](#ch_2.3.4)
        * [2.3.5 通用解释例程作为用户和计算机之间的接口](#ch_2.3.5)
    * [2.4 特殊目的系统和实时系统](#ch_2.4)
        * [2.4.1 命令与控制系统（Command and control systems）](#ch_2.4.1)
        * [2.4.2 过程控制系统（Process control systems）](#ch_2.4.2)
        * [2.4.3 远程处理（Teleprocessing）](#ch_2.4.3)
    * [2.5 “第二代”操作系统](#ch_2.5)
        * [2.5.1 特殊目的操作系统的历史影响](#ch_2.5.1)
        * [2.5.2 IBM “第二代”操作系统 OS/360](#ch_2.5.2)
    * [2.6 分时系统的出现](#ch_2.6)
        * [2.6.1 硬件分时（time-sharing in hardware）](#ch_2.6.1)
        * [2.6.2 软件分时思想](#ch_2.6.2)
        * [2.6.3 交互式系统和对话式编程语言](#ch_2.6.3)
        * [2.6.4 分时系统的出现和发展](#ch_2.6.4)
3. [IBM 发明了“操作系统”（这个术语）](#ch_3)
    * [3.1 “操作系统”从众多竞争术语中脱颖而出](#ch_3.1)
    * [3.2 “操作系统”术语历史溯源](#ch_3.2)
    * [3.3 “操作系统”与“编程系统”的彻底区分](#ch_3.3)
    * [3.4 人类操作员消失，操作系统成为用户和计算机之间的主要接口](#ch_3.4)
    * [3.5 操作系统的组织方式：层级结构](#ch_3.5)
4. [总结](#ch_4)
5. [致谢](#ch_ack)
6. [参考文献](#ch_ref)

以下是译文。

----

今天，我们已经很难想象如何使用一台没有操作系统的计算机。操作系统塑造了我们访
问计算机及其外设的方式，并提供了我们与计算机的交互操作。但是，**当第二次世界大战结束之
后，第一批计算机被发明出来时，并没有操作系统这种东西**。实际上，**直到数字计算（
digital computing）出现十来年之后，才有了某些形式的操作系统的尝试**。又过了
大约十年，大家才普遍接受了“操作系统”这个概念，大部分计算机才开始在出租或出售之前预
装操作系统。

随着 20 世纪 60 年代一些雄心勃勃的操作系统的开发 —— 例如为 IBM 机器设计的
OS/360 和为集成分时共享系统（integrated time-sharing system）设计的 Multics ——
业内开始形成一个**更加系统的框架**（systematic framework），这个框架也
**奠定了我们看待操作系统的现代视角**（modern view of the operating system）。

尤其是**分时共享系统（time-sharing systems）的出现，传统上认为这是操作系统开发历史
的一个转折点**。考虑到 20 世纪 60 年代末**分时共享**（time-sharing）和**批处理**（
batch-processing）两种理念各自支持者之间的激烈争论，分时共享系统的出现已经成为计
算行业历史上的一个经典时刻。与这些争论同样重要的是对**计算机的使用**及**计算机用
户**的思考，以及软件行业未来发展的思考，这些理念重心的转变也影响了早期计算机系统的
发展方向。

事实上，1954~1964 这十年不能被简单地称为“经验主义时代”（empirical）或“史前时代”
（prehistoric），也不能被称为批处理系统时代。很多系统都是在这期间内开发的，更重要的
是，这期间**从无到有地创造出了“操作系统”这个概念**。介绍操作系统发展历史时，典型
的时间线都是这样的：从没有操作系统过渡到批处理系统，再过渡到现代多道编程或分时共
享系统【注 1】。这种时间线既**隐藏了早期系统的多样性和复杂性**，又隐藏了**“操作
系统”这个概念仍然须得到业内一致认可这个事实**。

> 【注 1】This storyline captures only one (important) line of development and can be
> found in, e.g., [20, pp. 96-101], [64, pp. 6-18] or [43, 42]. Though also [16]
> follows this chronology, this presentation brings out that there were many
> systems and philosophies developing in parallel.

本文对早期编程和操作系统进行了详尽和系统的研究，时间跨度是 1954-1964，也就是在
**商业分时操作系统即将面世之前**【注 2】。本文**不按时间顺序展开叙述，而是从一般
到具体**，每个章节都会对传统的操作系统时间线进行细化，介绍一些不同时期在同时开发的
系统，并抛出我们的核心问题：**操作系统是什么？**（what is an operating system ?）

> 【注 2】The details of this systematic study cannot be included in this paper but
> will probably be published in book form with Lonely Scholar.

本文内容如下：

第一章介绍操作系统发展的一般背景知识，以及那些使操作系统成为必不可少和有价值的东
西的软硬件演变；

第二章从全局俯瞰各种早期系统，并将它们分成 5 类：

* 批处理系统（batch-processing）
* 集成系统（integrated systems）
* 专用目的系统（special-purpose systems）
* 第二代系统（second-generation systems）
* 试验性的分时系统（experimental time-sharing systems）
 
最后一章跟踪记录了“操作系统”（operating system）这个术语在 IBM 社区的诞生。

<a name="ch_1"></a>

# 1 背景知识：操作系统及软硬件演变

<a name="ch_1.1"></a>

## 1.1 50 年代中期

### 核心要点（译者注）

1. 理念进步：计算过程的控制者
    * 人 -> 计算机：存储式程序（stored program）
    * 设计一个控制其他程序的程序（操作系统前身）
2. 内存新技术：工作内存（working memory）和存储内存（storage memory）
    * 工作内存：磁鼓 -> 铁氧体磁芯
    * 存储内存：穿孔卡片、纸质磁带 -> 磁带。速度 45x；顺序访问
3. I/O 新技术
    * I/O 缓存：机械速度 -> 电子速度
    * 带选择器和多路复用开关的 I/O channel
    * 带处理器的 I/O channel
4. 软件新技术
    * 计算机用户小组：定期举行会议，交换程序和编程信息
    * 第一批大型编程系统：今天回头看，这些系统可以被称为操作系统
    * 用于链接和监控程序序列的系统
    * 引入硬件中断（硬件设备）：自动化原来的人工中断；支持多道程序（multiprogramming）（单核并发）
    * IBM FORTRAN（1955-1957）：第一门 fully developed 编程语言，加上 IBM 统治地位的市场加持，装机必备
    * 各种早期系统为了支持 FORTRAN 都进行了相应开发/更新
5. 两个创新：硬件中断和 FORTRAN

----

<a name="ch_1.1.1"></a>

### 1.1.1 理念进步

**将编程过程中的部分控制权交给计算机**这一精妙思想，是随着数字通用目的计算机（
digital general-purpose computer）的出现而出现的。这是通常被称为**“存储式程序”**
（stored-program）概念的一个方面。**由于计算机执行运算比人类快地多，因此在计算时
，应该让程序来控制计算过程**。

从逻辑上说，这种设想的下一步就是：**设计一个控制其他
程序的程序**（a program would control other programs）。但在早期，这种控制仅仅是简
单的准备例程（preparatory routines）或内务例程（bootstrapping routines）。不过，
从 50 年代中期开始，随着机器、编程和用户的进步，这种局面开始发生变化。

<a name="ch_1.1.2"></a>

### 1.1.2 内存新技术

**新的内存技术在此期间得以实用** —— 包括用于计算（working）和用于存储（storage）的内
存。

* 50 年代早期，价格便宜的磁鼓（magnetic drums）是**扩展工作内存**（working memory
  ）的不错选择，这种内存可以被计算单元（computing unit）直接访问。随着时间的推移，
  由 MIT 开发的铁氧体磁芯（ferrite core magnetic）内存开始取代磁鼓 —— 它们虽然
  更贵但速度更快。
* **外部存储介质**方面，**引入磁带**（magnetic tape）代替了穿孔卡片（punched cards）或
  纸质磁带（paper tape），**这对操作系统的开发至关重要【注 3】**。虽然 50 年代最
  快的穿孔卡片读取器每分钟能读取 `250` 张卡片，磁带系统却以 `15000 字符/秒` 的读
  取速度超越了前者。这个速度相当于 `11250 卡片/分钟`，比卡片读取器快 45
  倍 [32, p. 291]。这种方式使得大程序（larger programs）可以被直接读到内存，而且
  磁带（有时也用外部磁鼓）提供了一种方便、快速地访问例程库（library of routines
  ，函数库）的方式。当然，**这种访问并不是随机访问**。受内存媒介物理特性的限制，
  这种访问要么是顺序访问（磁带），要么是圆形（cyclic）访问（磁鼓）。

> 【注 3】Magnetic tapes were introduced as early as 1951 on the UNIVAC computer, but
> did not become common for other systems until the mid-1950s. It should also be
> remarked that punched cards and paper tape remained in use, mostly in parallel
> with magnetic tape.

<a name="ch_1.1.3"></a>

### 1.1.3 I/O 新技术

另一项技术发明是**输入/输出设备和中央处理器之间通信用的缓存**（buffer memory），
例如 IBM 701、IBM 704 和 ERA 1103 计算机都配备了这种缓存。在此之前，人们使用了很
多策略以便同步地使用计算单元和它的输入/输出外设，这些策略包括：

* spooling：将信息放到磁带而非卡片上，以加速 I/O 通信
* cycle-stealing：在前一个操作还未结束时就开始下一个操作
* read-write interlocks
* moon-lighting：使用一个较小或较慢的计算机作为 I/O 缓存，加速另
  一个较大或较快的计算机（典型部署：一台 IBM 1401 和一台 IBM 7090）

> “在这一时期，随着 I/O 缓存的到来，输入/输出设备开始从**机械速度**（mechanical
> speeds）领域演进到**电子速度**（electronic speeds）领域” [9]

再后来，由于多道程序设计（multiprogram design）的需求越来越多，人们又开发出了特殊
I/O channel，每个channel 有自己的选择器（selector ）和多路复用开关（multiplexing
switches）。低成本、基于半导体的处理器问世后，I/O channel 拥有了自己的处理器，
而不再使用开关。为 IBM Stretch 计算机开发的Exchange 系统（1957），就是最早基于处
理器的 I/O channel 的例子。

<a name="ch_1.1.4"></a>

### 1.1.4 软件发展

用于编程例程（programmed routines）的**快速存储**（硬件），是伴随着软件的发展（
the development of software）而不断扩张的。传统来说，业内通常将 **50 年代后半部
分视为软件开发开始腾飞的年代** [20, pp. 79-108]。大量的**计算机用户组**（
computer user groups）的成立见证了这一历史，例如 IBM 用户的 SHARE 小组，以及
scientific UNIVAC users 的 USR 小组（二者都成立于 1955 年）。这些小组定期举行会
以，大家在会上分享程序，交换编程实践信息 [4]。这一时期还见证了**第一批软件公司的
诞生**，例如

* System Development Corporation (SDC, 1957) that grew out of RAND’s involvement with the SAGE project
* Computer Sciences Corporation (CSC, 1959) etc. [18, pp. 29-56]

在这样的背景下，人们开发出了**第一批大型编程系统**（the first big  programming
systems），**现在回头看，其中的某些系统可以被称为操作系统**。在这批早期系统
中，MIT Lincoln Lab 为其 Whirlwind 计算机开发的 Comprehensive System of Service
Routines (CSSR，服务例程综合系统) 是比较有影响力的。后来 SAGE 项目中
SDC 公司的编程系统就是以 CSSR 作为基础的。

SHARE 社区开发了另一些重要的系统 —— **用于链接（linking）和监控（monitoring）程序
序列（sequences of programs）** —— 这些系统**为后来的批处理系统奠定了基础**，
其中有代表性的是 60 年代 IBM 的一些商业和科学计算系统。

<a name="ch_1.1.5"></a>

### 1.1.5 两个创新：硬件中断和 FORTRAN

两个时间上恰到好处的创新 —— 一个硬件创新，一个软件创新 —— 后来证明对操作系统的进
一步发展至关重要。

首先，1956 年，ERA 1103A（有时也称为 Scientific UNIVAC 1103）**引入了中断**（
interrupt），这是**一个可以中断机器操作、以便与处理器通信的设备** 【注 4】。硬件
中断可以用于**自动化一些之前只能由人来完成的手动中断**（manual interrupts）。它
使得更加复杂的监控系统成为可能，这对开发多道程序（以及后来的分时）系统来说至关重
要。在多道编程（multiprogramming）环境中，内存中能同时存储多个程序，其中一个程序
在等待 I/O 时，另一个程序可以执行。反过来，多道编程发展起来之后，又促使硬件中断
发展出更复杂的捕获（trap）机制。

> 【注 4】There are earlier (or contemporary) instances of an interrupt, in special
> projects such as the DYSEAC, the SAGE system or IBM’s Project Stretch, but its
> introduction on the ERA 1103A was the first ‘commercial’ appearance.

第二，从 1955 到 1957 年，IBM 的一个团队在开发一门科学计算编程语言，后来命名为
**FORTRAN**。作为第一门发展完备的（fully developed）的编程语言 【注 5】，再加上
IBM 当时在计算机市场的统治地位，FORTRAN —— 以及稍后的 FORTRAN II —— 迅速流行起来
，成为大部分计算机**装机必备**的软件之一。FORTRAN 的出现使得很多已有的操作系统进
行了改进，他们希望扩展自家操作系统的功能来引入和适配 FORTRAN 编程语言。例如，下
列系统为了支持 FORTRAN 都进行了相应开发：

* NAA’s Fortran Monitor System (FMS, 1959)
* Bell Labs’ BESYS-3 (1960)
* the University of Michigan’s UMES (1959)
* the RAND-SHARE operating system (1962)

> 【注 5】For the languages preceding FORTRAN, see [41].

<a name="ch_1.2"></a>

## 1.2 60 年代中期的一些变化

### 核心要点（译者注）

1. 转折点：分时系统的出现，操作系统开发的第一阶段结束
2. 第二阶段
    * 多道程序的发展：使得下列特性称为操作系统必备的：
        * 程序调度（scheduling of programs）
        * 内存保护（memory protection）
        * 可编程锁（programmable clock）
    * 新的、更快的内存设备的引入
        * 60 年代最大变革：顺序存储 -> 随机存储
        * 磁带驱动器、磁鼓驱动器 -> 磁盘驱动器：数倍速度提升；对任意扇区有相同的访问时间
        * 加速工作内存和存储内存之间的数据交换
    * 1962-1964 计算机制造商开始开发自己的操作系统和配套软件（目的是卖机器）
        * 操作系统
        * 函数库、（宏）汇编器、编译器、加载器、编程语言、调试工具
        * 1962-1965 主要计算机制造商及其操作系统：分时操作系统还不支持
    * IBM 只卖机器，（企业）用户自己开发操作系统 -> IBM 也开始开发操作系统（1962），用户驱动的系统开发逐渐没落

----

<a name="ch_1.2.1"></a>

### 1.2.1 多道编程

**1962-1964 是一个转折点**，也结束了操作系统开发的第一阶段。

**分时系统的出现代表了这个转折点**，而正从地平线冉冉升起的是“大”操作系统项目
OS/360 和 Multics。但是，这两者只是这一时期众多演进中的操作系统的两个代
表。这次演进由两方面组成：

* 一方面是多道编程的平稳、持续发展
* 另一方面是新的、更快的内存设备的引入

这两方面结合起来，使得更复杂、更灵活的系统成为可能。

**多道编程打破了顺序处理模型，其本质思想是：多个程序可以在同一时间运行**。
在实践中，程序间的这种同步性只是虚拟的。

实际上，虽然 I/O 处理能够与其他程序并行进行，但在主处理器执行一个程序期间，其他
程序只能等待，或者在这期间已经被中断了。硬件中断使得多道程序中的第一个实例变为可
执行状态，而在某些方面来说，I/O 缓存的引入使得当前程序的可执行时间变长。这些技术
受到了同一时期的软件多道编程发展的推动，而反过来，软件发展也推动了硬件创新。特别
地，这种方式使得

* 程序调度（scheduling of programs）
* 内存保护（memory protection）
* 可编程锁（programmable clock）

**成为了（一个操作系统）必备的功能**。从某种程度上说，分时使用一台计算机，以及多
个用户在同一时间执行程序和使用资源【注 6】，可以看做是多道编程的一种特殊形式。

> 【注 6】As a General Electric’s advertisement from the 1960s remarked correctly, “
> time- sharing is actually computer sharing.”

<a name="ch_1.2.2"></a>

### 1.2.2 存储进步：随机存储媒介的出现

虽然多道编程深远地改变了计算机系统的结构，**但在 60 年代，操作系统领域最大的变革
却另有其主：从顺序存储媒介过渡到随机存储媒介**。

IBM RAMAC 计算机的 350 磁盘（1956）是第一个此类随机内存设备。随后用于 IBM 1410
和 IBM 7000 产品线计算机的1405 和 1301 磁盘（1961-1962）更深远地影响了操作系统的
设计。同时代的、同样是由 IBM
开发的超磁带系统（Hypertape-systems）工作在 `170,000 字符/秒`，与前者相比，1301 磁
盘驱动器不仅快数倍，能够读取 `112,000 字符/操作`（每秒可以执行 5~7 次操作），更
重要的是，它对任意扇区的数据都有相同的访问时间（大约 `0.150 秒/操作`）。

**磁盘驱动器使得操作系统可以淘汰基于顺序逻辑（sequence-based logics）的磁带
驱动器和磁鼓驱动器，加速工作内存（working memory）和存储内存（storage memory）
之间的数据传输**。这减少了工作内存和存储内存之间交换数据时的后备队列长度（backlog
）和等待时间，使软件系统能力更强。

<a name="ch_1.2.3"></a>

### 1.2.3 推动者：从企业用户/研究机构转移到计算机制造商

但 60 年代的历史还不止于此。1962-1964 年期间，似乎**每个计算机制造商都对“操作系统”这
个概念闻风而至，并且各家都开发了自己的操作系统**【注 7】。

在 60 年代之前，操作系
统的大部分进展都是由**计算机用户**或**研究机构的成果**（大部分都由军方资助）推动
的。现在，**制造商开始投资组建编程团队**，开发与他们的机器匹配的合适编程软件
。这些软件包括：函数库、（宏）汇编器、编译器、加载器、编程语言、调试工具，以及最
重要的主程序（master routines）和操作系统。

> 【注 7】It is also in the early 1960s that the first overview articles on operating
> systems appear: [55, pp. 290-294] and [46].

表 1 列出的是一些主要计算机制造商【注 8，9】，它们在 1962-1965 年间都推出了
自己的操作系统（见表 19）。这些系统中，

* 某些还比较初级 (GE’s BRIDGE)
* 另外有些是经典的批处理系统 (Philco’s BKS or CDC’s Scope)
* 但大部分在除了支持批处理之外，都支持高级多道程序
* 分时系统还不支持。在 1966 年之前，分时系统还停留在研究阶段；商业上的分时系统
  直到 60 年代末才出现，那时 IBM、GE、DEC、SDS 和其他一些制造商将这个功能集成到了
  它们各自的操作系统（更多细节见 2.6 节）

<p align="center">表 1：1960-1964 美国计算机制造商提供的第一批操作系统</p>
<p align="center"><img src="/assets/img/what-is-an-os-zh/manufacturers.png" width="70%" height="70%"></p>

> 【注 8】We did not include information on computers that were not made in the
> U.S., but the same timeframe seems to be valid. For U.K. computers, e.g., the
> first operating systems appear in the beginning of the 1960s for the LEO III
> (1961) or Ferranti’s Atlas and ORION computers (1962).
>
> 【注 9】Two systems in this table are still the product of user development:
> the BKS system was developed by the Bettis-Knoll power plant; CDC’s CO-OP
> system was the result of the efforts of its user group CO-OP.

从用户到公司的演进显然离不开 IBM 在其中的作用。

* IBM 机器上的第一批操作系统是由（企业）用户开发的，包括 General Motors (GM)、
  North American Aviation (NAA)、Bell Labs、Michigan University、MIT 等等。它们
  依赖 IBM 用户组成的 SHARE 社区提供的信息，但没有接受任何直接来自 IBM 的技术支
  持。
* 但逐渐地，IBM 作为一个公司也开始参与进去。它们投入一部分精力，开发了 Share
  Operating System (1959)，这个系统源自SHARE 社区。
* 后来，IBM 还慢慢地将 NAA 的 FORTAN Monitor System (FMS) 集成到它们的 709/7090
  FORTRAN 编程系统 (1960- 1962) [44, p. 818-819]。**从这时开始，IBM 开始生产它们
  自己的操作系统，先是 IBSYS （1962 往后），后来是 OS/360 (1965 往后）**。在此期
  间，用户驱动的系统开发慢慢衰落，虽然用户还是会对制造商的操作系统进行一些定制化
  修改【注 10】。

> 【注 10】Nearly all operating systems would be customised upto a certain
> extent. An ex- ample of a extensive customisation is Thomson-Ramo-Woolridge’s
> version of IBSYS in 1962 [51].

<a name="ch_2"></a>

# 2 操作系统是什么？早期系统分类

<a name="ch_2.1"></a>

## 2.1 自动编程系统和操作系统

### 核心要点（译者注）

1. 操作系统的哲学含义：自动化人类操作员的工作
    * 人处理停止、中断等信号 -> 操作系统自动处理 -> 多个程序能并发执行，操作系统处理中断和调度 -> 批处理
    * 第一代操作系统称为批处理操作系统的原因
    * 批处理的好处
        * 减少计算机空闲时间
        * 加速程序加载
        * 减少人为操作错误
        * 标准化加载和转换过程
1. 操作系统（operating system）和编程系统（programming system）的联系和区别
1. 五种主要操作系统
    * 批处理系统（batch-processing systems）
    * integrated systems（集成系统）
    * 特殊目的和实时系统（special-purpose and real-time systems）
    * 第二代系统（second-generation systems）
    * 早期实验性质的分时系统（the early, experimental time-sharing systems）

----

<a name="ch_2.1.1"></a>

### 2.1.1 操作系统

当使用“操作系统”这个术语时，已经隐式地包含了这样的哲学：**操作系统能够处理以及部分
地自动化计算机的操作**（handles and partially automates the operation of the
computer）。从这个意义上说，操作系统**代替了人类操作员（human operator）的工作**。
尤其是，**原来需要在“控制面板”、“监控面板”和“监督面板”上执行的部分手工操作，现在都
能够通过“操作系统”实现自动化**。在这些面板上，操作员能够处理下列信号：

* 停止（stop）：**程序执行结束**或**外设停止了它的操作**之后发出的信号
* 中断（interrupts）：程序或外设工作异常、或命令无法执行时发出的信号
* 其他信号

“操作系统” —— 如其名字所暗含的哲学 —— 能够**自动应答这些停止和中断信号**，因此在
两个停止信号之间并不是只能运行一个程序，“一批”（a batch）程序都可以无中断地运行
，这也是**为什么第一代操作系统称为“批处理”（batch-processing）的原因**。这种操作
系统的好处：

1. 减少计算机空闲时间
1. 加速程序加载
1. 减少人为操作错误
1. 标准化加载和转换过程

从这一点来说，**操作系统承担了“内务操作”（housekeeping operations）的职能**。

<a name="ch_2.1.2"></a>

### 2.1.2 编程系统

**不仅操作员（operator）的工作面临自动化，程序员（programmer）的工作同样面临自动化**。
50 年代见证了大量的**自动化编程系统**（automatic programming systems）的诞生。这包括

* 很多的编程语言和编译器，**达到顶峰的时刻是 FORTRAN —— 以及后来的 COBOL 和 ALGOL 的诞生**。
* 其他编程工具，例如汇编器、宏汇编器、解释型例程（interpretative routines）、
  例程库、功能程序（utility programs）。这些工具或程序完成 I/O 通信、进制或编码转
  换等功能。

有了以上这些系统和工具，接下来的要做的就是**自动化加载和链接子例程过程中的那些转
换、搜索、排序、倒带（rewind）等重复和乏味的工作**，以及简化程序的编写。

**由于这么多东西都自动化了，因此在 1962-1964 年之前（而且即使到了现在），很难清
楚地区分下面两个概念**：

* 操作系统（operating system）
* 编程系统（programming system）

例如，当 W.L. Frank 在 1956 年描述一个“程序库”（program library）时，他包含了一
些“监督（或服务）例程”（supervisory or service routines） 作为这个库的一个子集，
这包括 [29, p. 6]：

* 汇编和编译函数（assembly and compiling routines）
* 引导和读取输入程序（bootstrap and read-in routines）
* 代码检查和诊断函数（code checking and diagnostic routines） 
* 事后检查和监控函数（post mortem and monitoring routines）
* 特殊算术函数（special arithmetic routines. e,g floating point, complex numbers, double precision)

虽然以现在的定义看，引导和读取输入，以及事后检查和监控这些函数都明显在“操作系统”
（operating system）的范围内，但其他一些函数库却都是属于“编程系统”（programming
system）范畴的。

从某种角度看，**组成“操作系统”的那些例程（operating routines）其实只是“编程系统”
（programming system）函数库中的一部分**。这也解释了为什么在 60 年代中期之前，一
些书或文章在介绍我们今天称为“操作系统”的东西时，经常把它们放到“编程系统”的类别里
【注 11】。

但从另一方面来说，操作例程（operating routines）监督和控制着编程系统，因此在层级
上来说它们位于编程系统的上层。这后一种观点是“监控”（monitor）或“监督”（
supervisor）概念 —— 也就是“操作系统”概念中，操作员的自动化（automation of the
operator） —— 的效果。这一点会在第三章跟踪“操作系统”这个术语的起源和演变时详细讨
论。

> 【注 11】See, e.g., the classic book by Rosen [57], but also [55], [28] or [35].

<a name="ch_2.1.3"></a>

### 2.1.3 早期自动化编程系统分类

接下来，我们试探性地对早期自动化编程系统进行分门别类 —— 从事后的历史眼光来看，这
些都可以称为操作系统。这里讨论五个主要类别：

1. 批处理系统（batch-processing systems）
1. 集成系统（integrated systems）
1. 特殊目的和实时系统（special-purpose and real-time systems）
1. “第二代”系统（second-generation systems）
1. 早期实验性质的分时系统（the early, experimental time-sharing systems）

<a name="ch_2.2"></a>

## 2.2 批处理系统

### 核心要点（译者注）

1. 起源：商业数据处理和会计领域，因为这些领域的重复性工作比较多
    * 重要思想：对人类操作员的工作进行自动化
1. closed shop vs. open shop
1. 方式：程序员准备程序，操作员操作计算机
1. 1956 年经常被称为“第一个”操作系统（更准确地说，批处理系统）的诞生之年
1. 操作系统的思想开始成熟
1. 批处理的工作过程：输入处理 -> 执行 -> 输出处理
1. 监视器开始承担更多功能：链接、装载、编译 -> 格式转换、I/O 控制、输出报告和错误日志
1. IBSYS：监视器的监视器

----

<a name="ch_2.2.1"></a>

### 2.2.1 起源：商业数据处理和会计领域

批处理系统的概念似乎是从商业数据处理和会计（commercial data-processing and
accounting）领域出现的。相比于科学计算（scientific computing），商业计算（
business computing）和会计领域的计算任务更加重复乏味，因此对机器操作进行自动化、
将任务进行流水线化（streamlining ）的想法看起来就非常有前景。

**对（人类）操作员的工作进行自动化是计算机历史上的一个重要思想**，这也是计算机历史传
统故事线中的中心事件之一。用程序替代人类操作员的哲学，最早是由 Prudential
Insurance Company 的 Bruse Moncreiff（当时还在 RAND 工作）最显式地提出的。
1955 年他写给 C.W. Adams:

> 我已经将注意力转向了**自动数据处理器**（automatic data processor）执行日常操作
> 的问题。最令程序员感到困扰的无疑是操作员，因此我在尝试用程序解决这个问题，这样
> 操作员就可以从世界上消失了。操作员的工作内容分几个特定的阶段，其中大部分都需要
> 熟练的手工操作，并且这些工作也是必不可少的。我已经在尝试将所有需要思考的工作从
> 操作员的角色中移除，因为人类做这些事情最低效。我喜欢将设计的这个例程（routine
> ）当做是一个自动监督员（automatic supervisor）而非操作员，因为它将告诉人类操作
> 员去做什么 [2, p. 78]。

在他发表在期刊 IRE 上的文章《An Automatic Supervisor for the IBM 702》（1956）中，
他提出了运行一个大规模商业计算设施（commercial large-scale computing facilty）的
问题，以及其中 **“利用同一个程序高效地执行日常操作”**（efficient day-after-day
operation of the same routines）的必要性。由于 **“在做出判断和控制操作方面，人类操
作员的速度无法与机器相提并论”**，因此按照 Moncreiff 的观点，最高效的解决方案就是
**“一个监督程序（supervisory routine）[...] 使得即使人类操作员动作较慢并且还易出错
，机器都可以持续高效地运行”** [50, p. 21]。但是，他说道，我们必须首先“对这个问题的
复杂度有一个了解，因为到目前未知，我们还没有对其进行过详尽地调查”  [50]。

<a name="ch_2.2.2"></a>

### 2.2.2 IBM 701 Monitor

大约在同一时间，North American Aviation (NAA) 的 Owen Mock 已经在对它的 IBM 701
monitor 进行编程。NAA 的计算机系统划归在其会计部门的制表团队（Tabulating section），
这不仅使得会计机器（accounting machines）和数字计算机（digital computers）在历史
上具有沿革性，而且使得这个领域的日常实践（everyday practices in this field）的延
续真实可触摸。Mock's Monitor 引入到 IBM 701 后，所有任务分为了几类 [49, p. 793]：

> 将操作与用户分开产生了某些深远影响。其中之一是，它产生了这样一种哲学：用户不应
> 该接触机器；实际上，他们甚至不应该接触卡片穿孔平台。产生的另一种哲学是严格审计
> （strict accountability） [...] 最终的结果是一种用户编制方式（user
> regimentation），这种编制方式初看可能与引言里我们介绍的自由（freedom）相悖，但
> 实际上有益于操作系统概念的引入。

<a name="ch_2.2.3"></a>

### 2.2.3 Open Shop vs. Closed Shop

确实，**批处理系统对人类操作员的工作进行了自动化，但自动化部分却带来了自己的（在
计算机室中的）配置和操作要求**。传统上，这种转变被称为从“开放商店”（open shop）
到“封闭商店”（closed shop）模式。这个术语是从贸易联盟合约中借鉴过来的，closed
shop 模式中：一个工人必须加入一个联盟，而且他能做什么工作在合约中是有限制的【注
12】。

* open shop 模式：将程序带到计算机，运行程序（或者让操作员来运行），最后将打
  印出的执行结果带走
* closed shop 模式：将程序交给操作员，操作员将程序组织成批次（batch）执行，然后
  等到自己的批次执行完成，拿到结果

closed shop 配置方式因此与任务的分割（separation of tasks）更加紧密相关。**程序
员准备程序，操作员操作计算机**。在批处理系统中，这种组织方式与自动化特性形影
相随【注 13】。

> 【注 12】I thank one of the anonymous reviewers for pointing this origin out.
>
> 【注 13】 It should be noted that another interpretation of ‘open shop’ versus
> ‘closed shop’ exists (though it remains compatible with its trade union
> origins). In that interpre- tation, the ‘closed shop’ is the situation
> where only the operators and the machine code programmers can use the
> machine because the other users don’t know how to write in machine code.
> The ‘open shop’ situation then is when other users, now using a
> programming system, can start writing programs. These programs may
> possibly be executed in batches, see e.g. [15] for such an ‘open shop’
> system using FMS where the users are empowered by the FORTRAN
> programming language.

<a name="ch_2.2.4"></a>

### 2.2.4 1956：“第一个”操作系统诞生

**1956 年经常被称为“第一个”操作系统（更准确地说，批处理系统）的诞生之年**，虽然
批处理系统家族的起源要更早一些，即 Owen Mock’s 701 Monitor [49] 或 Moncreiff’s
IBM 702’s Supervisor [50]。

**操作系统的思想开始成熟**是在为 IBM 704（1956）开发的 GM/NAA monitor
（General Motors - North American Aviation Monitor） [56] 面世、并且通过 SHARE
社区分享之后。这个操作系统的核心程序 —— 名为 Mock-Donald monitor —— 后来被重复使
用、升级以及移植到后来其他雄心勃勃的操作系统中，例如为 IBM 709 (SOS 1959) 开发的
SHARE 操心系统，以及为 IBM 7090 (1962) 开发的 RAND-SHARE操作系统。Owen Mock 描述
过为 IBM 701 计算机开发的最原始批处理系统是如何工作的：

> 多个作业（job）放到单个 727 磁带上成为一个批次（batch），一个批次的设计运行时
> 间是 1 小时。这个系统有一个小的核心监视器（core resident monitor），一个系统库
> （a single system library），以及一个存储控制程序的磁带（control program tape
> ），这个磁带也承担监控器的备份功能。所有输出依次堆叠（stacked）在输出磁带（
> output tape）上，如果有需要，这个磁带可以移除或替换。一个批次完成后，输入连同
> 输出磁带都会被移走，换上下一个批次，输出磁带会带到 717 打印 [49, p. 794]。

对于 GM/NAA monitor 来说，这个过程拆分为了三个阶段，因此更加复杂 [56, p. 802]：

* 输入转换阶段：将十进制数据转换成二进制，将源程序转换为对象语言（object language）
* 执行阶段：几乎不受程序员的直接控制
* 输出转换阶段：处理行打印机的输出、穿孔卡片的输出（十进制、二进制都有），以及会
  计记录等等

<a name="ch_2.2.5"></a>

### 2.2.5 IBM IBSYS 系统：监视器的监视器

随着时间推移，监视器能够处理更长的作业批次，承担除了链接、装载和编译之外越来越
复杂和多样的任务，例如**格式转换、I/O 控制、输出报告和错误日志**等等。

监视器的这种越来越复杂的趋势继续延续了一段时间，直到 IBSYS（1962-1965）的出现，
后者作为**“监视器的监视器”**（a monitor of monitors [...] includes several of
the older systems.）[35, p. 24]。图 1 是 IBSYS 各组件框图，从中可以很容易看出操
作系统的监视器确实处在了监督其他子系统的位置，这些子系统包括编程语言、I/O 控制、
一个例程库以及一个文件系统。

<p align="center"><img src="/assets/img/what-is-an-os-zh/ibsys-system-monitor.png" width="85%" height="85%"></p>
<p align="center">图 1. IBSYS 操作系统（1962）的组织结构</p>

<a name="ch_2.3"></a>

## 2.3 集成系统（Integrated systems）

### 核心要点（译者注）

1. MIT Comprehensive System：将各种 utility 程序组织到一起，方便程序员获取和使用
    * 这些程序来源各异，并不统一
1. **操作系统发展历史上的另一条线**：设计一个的**编程系统**，它能够，
    * 轻松访问各种程序资源
    * 便捷化或者部分地自动化（人类）程序员的工作
1. 50 年代末的另一个流行术语是 “integrated system”（集成系统），拥抱了相同的“全面、综合”哲学
1. 集成系统经常会包含批处理相关的功能，但其重点并不是组织和顺序化作业批次（即
   对计算机操作进行自动化），而是协助对计算机进行编程（programming the computer）
1. 相比于批处理系统中典型的“加载、汇编、编译、执行”循环，集成系统经常更多地依赖
   **解释型系统**（interpretative systems）。

----

<a name="ch_2.3.1"></a>

### 2.3.1 MIT Comprehensive System（综合系统）

前面提到，Moncreiff 的 Supervisor 论文发表在 IRE 期刊；这篇文章所在的卷中，还有
另一篇文章 —— 更准确地说是摘要（abstract） —— 讨论的是一个**协助对大规模控制程序**
（large-scale control programs）进行**编码、检查、维护和文档编写**的**“实用函
数程序系统”**（utility program system）[13, p. 21]。这个系统属于
**“Comprehensive System”**（综合系统）的一部分，后者是 MIT 从 1953 年开始为
Whirlwind 计算机开发的。作为**当时最大、最快的计算机之一**，Whirlwind 在使用过程
中积累了很多程序，Comprehensive System 就是将这种团队成果整合起来的结果，以便
这些成果能被程序员更方便地使用。

由于这些**编程系统**（groups of programming systems）和计算机上的**实用函数程序**（
utility programs）来自不同地方，并且这些程序都经过了多次适配（adapted）、重复
利用（recycled）和格式调整（reformatted ），因此它们通常并不统一。
寻找一种方式来统一（streamline）对这些程序的访问，以及能让这些程序协同工作，并
不是一件容易的事。下面这种设计思想，是**操作系统发展历史上的另一条线**：设计一个
**编程系统**，它能够，

* 轻松访问各种程序资源
* 便捷化或者部分地自动化（人类）程序员的工作

<a name="ch_2.3.2"></a>

### 2.3.2 集成系统

**50 年代末的另一个流行术语是 “integrated system”（集成系统）**，它拥抱了与
Whirlwind 团队相同的 **“整合”（comprehensive）哲学**。特别地，与
Ramo-Woolridge 相识的一些人似乎使用了这种系统。

> 程序员会在程序数据（program data）这么低的层面与机器交流信息。在集成计算系统中
> ，这种信息范围又扩大了，将原来通过口头或书面指令传达给机器操作员（machine
> operator）的信息也包括了进来。这里的重要概念是：**所有东西都被集成到一起形成
> 一个计算系统，从而不再像以前一样使用相互毫无关联的不同子系统**。[8, p. 8]

或者，在《Handbook for Automation, Computation and Control》的定义中，**“将部分或
全部这些不同的实用函数程序连接成一个有组织的、由程序员控制的、半自动或全自动的整
体，这种系统通常被称为集成系统（integrated system）”** [33, p. 184]【注 14】
书中引用的例子是：

* MIT’s CSSR (Comprehensive System of Service Routines)
* MAGIC (Michigan Automatic General Integrated Computation)

> 【注 14】This quote comes from a section written by John Carr III.

Ramo-Woolridge 团队也使用了的术语，W.F. Bauer 牵头为 ERA-1103 开发了称为“
integrated computation system” 的系统 (1955)。W.F. Bauer 写到，这是一个“优化计算
机使用的整体系统，减少了将程序交付到生产环节的过程中程序员、计算机和办事员的时间”。
[6, p. 181]

<a name="ch_2.3.3"></a>

### 2.3.3 集成系统与批处理系统的区别

集成系统经常会包含批处理相关的功能（Whirlwind 已经包含了），但它们的重点并不是
**创建和顺序化作业批次**（forming and sequencing batches，即对计算机操作进行自动化
），而是**协助对计算机进行编程（programming the computer）**。

集成系统的**核心是所谓的实用函数程序**（utility programs），而不是例程库（
library of routines）。

集成系统**主要目的是方便获取这些实用程序**，它通过提供
输入-输出函数、转换函数、顺序化函数等等来实现这一目标。正如操作系统老兵 George
H. Mealy 【注 15】后来的思考，集成系统的功能已经被吸收到了现代操作系统：

> 某些被归类为操作系统函数（OS functions）的函数，最早是以实用子例程和程序（
> utility subroutines and programs）的形式出现的 ... 今天，库成为了操作系统不可或
> 缺的一部分 —— 例如，已经到了这样的程度：许多程序员用库来鉴别是否是 UNIX 系统
> ，而不是用内核（nucleus）和 shell。 [48, p. 781]

> 【注 15】 After his involvement with Bell Labs’ BESYS-systems and the SHARE community,
> he went to RAND where he headed the team that made the RAND-SHARE operating
> system. Afterwards, he worked for IBM on the OS/360 system.

<a name="ch_2.3.4"></a>

### 2.3.4 解释型系统

集成系统通常是朝着**塑造用户与计算机交互方式、使交互方便易用**（easing and
shaping the user’s interaction with the computer）的方向发展。相比于批处理系统中
典型的“加载、汇编、编译、执行”循环，集成系统经常更多地依赖**解释型系统**（
interpretative systems）。

相比于编译型系统，解释型系统对收到的每行输入立即进行解释（interpret）。“主程序（
main program）中的跳转（jump）指令去掉了，这类指令原来的功能是将控制转交给子例程
（sub-routines）”，因此现在即使在执行子过程时，控制权还在（主程序），“所有过程都
汇聚为一个解释子例程（interpretive subroutine），这个例程还包含了一个监督部分（
section），用于监督正在执行的各种各样的操作”，因此“机器的指令码（instruction code of
the machine）不是仅仅得到了扩展，而是完全被替换”。[1, p. 16-3]

换用《ACM Glossary》（1954）中的话说，**“解释例程（interpretive routine）本质上
是一个封闭子例程（closed subroutine），它接收无限长的程序参数序列（伪指令和操作
数），然后连续不断地执行操作”**。[37, p. 18]

最早的一批编程方案（programming schemes）中，某些就属于解释型的，例如 

* 701 Speedcode
* Univac’s Shortcode
* MIT’s Summer Session computer

虽然这类系统**更占用机器时间（执行更慢），但它们能有效减少编程花费的时间**。在需
要测试或调试某个程序的场景，或者需要频繁调用某个子例程（subroutines）的场景，这
一点尤其重要。

人们还开发出某些特殊的解释型例程（interpretative routines），例如用于浮点算术、复
数计算或 housekeeping 功能的等等。某些计算机制造商还在市场上售卖一种通用解释例程（
general interpretative routines）系统，例如，

* Bendix G-15 的 Intercom 1000 系统，其中包含了一些 microprogrammed routines
* NCR 304 的 STEP 系统，其中包含了磁带标签自动处理功能（tape label handling）

对于这两种系统，用户都有两种选择：

* 通过解释例程（interpretative routine）对机器进行编程
* 直接在机器层面（machine level）对机器编程

前者使用更方便，后者执行速度更快，就看用户怎么折中了。

<a name="ch_2.3.5"></a>

### 2.3.5 通用解释例程作为用户和计算机之间的接口

至少在某个特殊场景下，**非常通用的解释例程（interpretative routine）可以作为用户
和计算机之间的一种接口，使得对计算机的访问也分层次**。MIT TX-0 和 TX-2 计算机上
都开发了这样的方案，不过在这些计算机上，这个解释过程是与其他交互式东西耦合在一起
的，例如一个电传打字机（flexo writer）和一个带光笔的显式子系统。

> 大容量、高速内存已经到来。[...] 是时候重新审视当前我们**对计算机应用进行设计和编
> 程时**所用的技术和哲学了。[...] 这份备忘录描述了一个实用程序系统（utility system
> ），其虽然形式简单，但意义独特：**能够辅助程序员在控制台（console）上调试和修
> 改程序**。这是通过如下方式实现的：将 utility 程序与需要调试的程序一起加载到内存，
> 然后在 utility 系统和程序员之间提供直接通信。[31, p. i]

这个为 TX-0 开发的 Direct Input Utility System (1958) 有着一致的内在逻辑（
coherent inner logic），它展示了**如何通过一个软件控制台（console）使计算
机设施对用户触手可得**（整体结构见图 2）。它提现出了**“人-机直接交互”哲学**（
direct man-computer interaction），**影响了后来 McCarthy 的分时编程（time-shared
programmning）思想以及 PDP 产品线计算机（见 2.6 节），孕育了操作系统的现代命令
行访问方式**。

<p align="center"><img src="/assets/img/what-is-an-os-zh/2.png" width="100%" height="100%"></p>
<p align="center">图 2. TX-0 Direct Input Utility System (1959) 的组织结构</p>

<a name="ch_2.4"></a>

## 2.4 特殊目的系统和实时系统

<a name="ch_2.4.1"></a>

### 2.4.1 命令与控制系统（Command and control systems）

另外也有，正如 W.F. Bauer 后来回忆，“许多特殊目的系统，尤其是命令与控制系统，这
种系统**使用了一些高级的、超出那个时代的操作系统思想**。” [10, p. 999] 其中**最
著名（也是最有影响力）的是 50 年代开发的 SAGE**（Semi Automatic Ground
Environment）系统。

SAGE 是美国军方资助的一个大项目，目标是建造一个**通过电话线连接起来的计算
机系统**，这个系统会从防御基地获取雷达数据和其他信息，最终计算出一个整体的领空（
airspace）图。在原子突击（atomic strike）时这有助于军方做出判断。

MIT Lincoln Lab —— 及其 Whirlwind、TX-0、TX-2 计算机 —— 参与了这个项目。IBM 也参
与了，建造了大量 AN-FSQ7 计算机。RAND 中的一些程序员建立了最早的软件公司之一 ——
SDC (System Development Corporation) 来给这个项目编写程序。

MIT 和 IBM 开发的系统都具备了**一些新特性**，例如**实时远程处理**（real-time
teleprocessing），以及**显式子系统**（display subsystem），这个子系统**通过中断
来提供用户和计算机之间的交互**。AN-FSQ7 计算机还工作在全双工（duplex）模式，用一
个开关控制两台计算机之间的相互通信，用另一个开关控制计算机与外设的通信【注 16】。
**宽泛地来说，这些系统都可以被称为分布式系统**（distributed systems），因为一个
（或多个）中央控制器绑定一组外设，并能与这些外设实时通信。这些系统中发展出来的某
些思想，后来分别被归类到了多道编程（multiprogramming）、分布式计算（distributed
computing）或并发计算（concurrent computing）的领域，但**在当时，这些系统的最耀眼
、最有辨识度的特性是实时操作**。

> 【注 16】There were other experiments in multi-computer systems around 1960, amongst
> them the Burroughs D-825 (1962) or the NBS’s Pilot computer (1959).

虽然 SAGE 及其相关项目可能是特殊目的系统中最有影响力的，但还有其他一些特殊目的系
统，包括军用的和民用的。当时有很多**数字-模拟系统**（digital-analog systems），其中
有一个数字处理器以及一个或多个模拟机器（analog machines）。为了使设备间的通信
更加高效，开发了特殊的接口（special interfaces）来**处理同步问题，实现方式是通过中
断来对程序和信号进行顺序化**（sequencing programs and signals through interrupts）
。**这类系统的特点**是多道编程、复杂的转换以及一些复杂的调度程序（scheduling
routines）【注 17】。在军事领域，高级数据处理单元，例如

* Ramo-Woolridge 的 “polymorphic data system”（多态数据系统） RW-400 (1960)
* Burroughs D-825 Modular Data Processing System (1962，模块化数据处理系统)

上面两个都设计为**控制与操作处理器和设备组成的网络**（a network of processors
and devices）。另外，对于D-825 计算机，还开发了一个开拓性的操作系统：the
Automatic Operating and Scheduling Program (AOSP，自动操作与调度程序)。

> 【注 17】See chapter 30 in [33] for some examples.

<a name="ch_2.4.2"></a>

### 2.4.2 过程控制系统（Process control systems）

另一个趋势是**工业过程的自动化**（automation of industrial processes）。
50 年代和 60 年代开发了很多特殊目的机器，用来控制工业过程 —— 包括机器厂、炼油厂
、发电厂等等【注 18】。当时已经普遍意识到，这些机器中的控制与伺服系统，最终可能会
被可编程的通用目的数字计算机取代。但 50 年代和 60 年代的大部分系统仍然是混合类型
的：通过所谓的 set points **以数字的方式（digital）控制模拟设备**【注 19】。

> 【注 18】 This technological evolution has to be contextualised socially,
> taking into account the tensions between organised labour, corporate
> management, technology and science, see [53].
>
> 【注 19】 An extensive state-of-the-art anno 1957 can be found in [32].

<p align="center"><img src="/assets/img/what-is-an-os-zh/3.png" width="60%" height="60%"></p>
<p align="center">图 3. 通过一个过程控制计算机实现直接数字控制（direct digital control）</p>

在 50 年代，小型通用目的计算机，例如 Bendix G-15 或 LGP-30，经常用作这类实时
数据处理的前端（front-end for such data-processing in real-time）。

**50 年代后期见证了很多专用的特殊目的系统**（dedicated special-purpose systems）
的诞生，这些系统能够通过自定义编程来实时地控制工厂里的流程或机器。在这个领域，

* Thomson-Ramo-Woolridge 的 RW-300 (1959)，以及后来的 RW-330 (1961) 计算机提供了
  一种工业控制的方法，以编写 executive routines 见长。
* General Electric 也很活跃，推出了自己的 GARDE系统，用 GE-312 计算机控制发电厂
  (1959)【注 20】。
* IBM 进入这个领域比较晚，1961 年，它推出了 IBM 1700。
* 在另一个完全不同的领域，**60 年代早期**，Bell Labs 开始开发它的Electronic
  Switching System (ESS)，通过 stored-program computing 来**对分布式电话网络中
  的交换进行自动化**（automating switching in the distributed telephone network）。

> 【注 20】See also [22] for the transition from analogue to digital computing
> at Leeds & Northrup.

<a name="ch_2.4.3"></a>

### 2.4.3 远程处理（Teleprocessing）

最终，通过**电话线作为一种计算机之间通信和传输的方式** —— IBM 的市场宣传中称为“
远程处理”（teleprocessing） —— 在 60 年代初期也发展非常迅速。IBM 在这个产品线的
最早开发要追溯到产 1941 年的 IBM 057 和 IBM 040。IBM 057 读取卡片和穿孔纸质磁带
，然后通过电话线将信息传给远端的 IBM 040，后者再对卡片进行穿孔。这套系统的最大传
输速率是 `3 卡片/分钟`。

基于在 SAGE 项目上的经验，IBM 在 1960 年推出了一个改进非常大的远程处理形式，使用
磁带作为载体，而 IBM 7701 和 7702 的磁带传输终端（Magnetic Tape Transmission
Terminals）能够处理 `225 卡片/分钟`。后来 IBM/360 产品线又将这个处理速度提高了
100 多倍[40, p. 5-6]。

随着 50 年代磁带的引入，内存的速度取得了显著的提升，这也给 60 年代远程处理的速度
带来巨大提升，这打开了一个充满各种可能性的新世界。IBM 开发了它的实时远程处理系统
，例如著名的 SABRE (1960, with American Airlines for air travel tickets
reservations) 和 TOPS (1962, with Southern Pacific Railways)。其他制造商，例如
Rand-Remington (with their Univac File Computer), Burroughs 和 General Electric
都开创了各自的远程处理品牌。

<a name="ch_2.5"></a>

## 2.5 “第二代”操作系统

<a name="ch_2.5.1"></a>

### 2.5.1 特殊目的操作系统的历史影响

**虽然特殊目的编程系统更像一个大杂烩，但这些系统中涌现出的技术，尤其是多道编程、
实时、分时、分布式计算与网络化，后来都证明对其后操作系统的发展提供了宝贵的经验**
。例子非常多，

* 控制系统 AOSP：最初为 Burroughs 的军事多计算机系统 D-825 设计，为 Burroughs
  后来商业计算机产品线 B5000-B5500 的系统 Master Control Program (MCP) 提供了蓝
  图
* Thomson-Ramo-Woolridge 公司中一些参与了实时处理控制或军事系统 RW-400 开发的工
  程师，后来加入 Honeywell 和 DEC，并将他们的经验用在设计实时系统中
* General Electric 在过程控制和为 GE-312 计算机开发 ECP (Executive Control
  Program) 操作系统的背景，为他们后来开发实时系统提供了宝贵经验。尤其是，他们从
  自己的过程控制计算机 GE-312 上演变而来的 Datanet-30 计算机及其操作系统，用作了
  前端处理器，来处理 GE-255 和 GE-235 计算机的分时。

<a name="ch_2.5.2"></a>

### 2.5.2 IBM “第二代”操作系统 OS/360

当然，在 IBM 为其 IBM/360 计算机产品线开发的 **OS/360（1966）中，IBM 的两个开发
线路合并到了一起**:

1. 一方面是最初由 SHARE 社区开发的批处理系统，后来集成到 IBSYS
1. 另一方面是 IBM 在 SAGE 项目期间积累的经验，后来通过 SABRE 和 MERCURY 系统实现
   了商业化

> 随着计算机的使用场景延伸到通信领域（telecommunication），监督控制程序
> （supervisory control program）的职责也得到了延伸，它开始作为**批处理**（batch
> processing）和**远程服务**（service to remote locations）的一个桥梁。由此开始
> ，监督程序既可以用于控制只做批处理的系统，也可以用于专门控制远程通信设备的系统
> ，还可以同时控制这两类系统。[26]

对于 G.H. Mealy，“第一代”批处理系统 **“致力于在一个顺序执行的作业批次中进行作业
调度”**（overlapped setup in a sequentially executed job batch），当这种思想与
**实时应用专用机器**（dedicated machines of real-time applications）相遇时，就**催
生了“OS /360 最基本的结构”**。这种结构“对批处理作业和实时应用都使用，可以看作‘第
二代操作系统’的一个例子。这类系统的新目标是**提供一个支持不同种类应用和操作模式
的环境**。” [47]

虽然术语“第二代操作系统”这里描述的是 IBM 的演变，但只要对这个术语的意思稍加扩展
，我们就可以将 Burroughs’s MCP or GE’s GECOS（先实时，后批处理，译者注） 也称作“
第二代操作系统”。对于后者，它们先从早期的实时处理中汲取经验，后来又为批处理提供
支持，是一个支持多种多道编程可能性的灵活系统，分时特性就是其中之一。

尤其是，**这些操作系统支持模块化的硬件设计**，如 B5500 在市场宣传中说的，提供“硬
件-软件集成”。这是一种两方集成（two-way integration，作为对比，以前主要是**软件
去适配硬件**，译者注），计算机制造商方面，需要让它们的计算机（IBM/360,
GE-635/625 and B5000/5500）集成中断和捕获机制、内存保护和内存管理方案、定时器等
待，以满足系统的需求。

<a name="ch_2.6"></a>

## 2.6 分时系统的出现

<a name="ch_2.6.1"></a>

### 2.6.1 硬件分时（time-sharing in hardware）

本文没有按时间线介绍分时系统是如何成熟的，但这种系统的概念发展和最早实验开始
于 60 年代左右。“分时”（time-sharing）这个术语在 50 年代被频繁使用，但那时指的都
是**硬件的分时共享**（time-sharing in hardware）。大部分情况下，这表示的是主处理器和
外设同时工作，共享计算机的时间。**I/O 缓冲、I/O 开关和 I/O channel 都是这种硬件分
时的例子**。

最早提出让处理器伪同时地（quasi-simultaneously）控制**多个终端设备**以
及**多个程序序列**的提案是 1954 年 Wesley Clark 的一份报告 [21, 30]【注 21】。
在 50 年代末期，这种思想还会以不同的形式被多次重提。

> 【注 21】Clark worked at Lincoln Lab on the Whirlwind and the Memory Test Computer at
> the time they were starting to prepare for project SAGE, he would later head the
> development of the TX-0 and TX-2. Remark that human or interactive intervention
> is not planned in Clark’s 1954 proposal, though such interaction would appear on
> the TX-0 and TX-2.

<a name="ch_2.6.2"></a>

### 2.6.2 软件分时思想

Bob Bemer 当时在 IBM， 工作是开发编程标准。他在 1957 年写到，一台计算机“服务多个用
户” [11, p. 69] 。大约在同一时间，Ramo-Woolridge 的项目经理 Walter Bauer 设想了
一台计算机：Ultradatic [9, p. 49]。

> 每个都市的大行政区都会有一台或几台这样的超级计算机，这些计算机能够并发地处理任
> 务。每个公司或组织在它们本地安装输入-输出设备，然后购买计算机的计算时间 —— 就
> 像普通家庭从城市服务公司购买电力和自来水一样。

Bemer 和 Bauer 都深度参与了早期编程系统和操作系统的开发，他们的后来被称为“
computer utility”【注 22】的思想，代表了他们在计算系统中看到的演进趋势。

* 计算机和外设越来越快，如果管理不当，会浪费大量的计算机时间
* 另外，**计算机的操作和编程已经变得越来越自动化 —— 这会产生一批新的计算机用户**

> 【注 22】The ‘computer utility’ has recently (and anachronically) been
> reclaimed as a pre-cursor to cloud computing.

**受命令与控制系统（command and control systems）中** —— 例如 50 年代末期的 SAGE
—— **控制台（console）和显示器（display）的启发，他们想象到了“外行用户”（没有操
作或编程经验）从一台中心计算机请求信息的场景**。这类系统，

* 可能是专家系统，例如 mathematical Culler-Fried Online System [25] (1963)，或者
  Hospital Research System at BBN (1965)
* 也可能是通用信息处理系统，例如设想中的 Ultradatic，或者后来的 MAC 和 Multics 项目

1959 年，C. Strachey 和 J. McCarthy 提出了一个与此稍有不同的哲学，它更关注的是程
序员而不是 utility 用户。通过硬件中断对多个程序进行自动控制（调度，这也产生了多
道编程），不仅部分地自动化了操作员的工作，还使得程序员与他的程序的执行隔离开来。
他们的提案思想与此相反：考虑到处理器的高速和 I/O 的低速，Strachey 对计算机被低效
地使用非常失望。因此，他提出了“**在操作员之间共享时间** [...] 允
许 [...] 在一个特殊的控制台手动查看程序状态”，以及支持维护。为了处理复杂的任务协
调，Strachey 描述了一些硬件设备，例如 interlock 和中断，以及一个 “Director [...]
master programme designed to cater automatically for the conflicting demands of
a number of stations of different types within a predetermined basic plan”。[62]

<a name="ch_2.6.3"></a>

### 2.6.3 交互式系统和对话式编程语言（conversational languages）

**John McCarthy，受 MIT TX-0 计算机的交互式使用方式的启发 [45, p. 52]，在寻找一
个能容纳他的 LISP 编程语言的系统，这个系统需要支持交互式调试**：

> programmers are given the same near-simultaneous ability through time-shared
> computer use with routines designed to minimize programmer decision time. This
> involves connection of a number of typewriters to a computer, a language for
> communication, a program filing system which can allow fast access to one of
> many partially run programs in memory or on file, and a monitor program [and]
> could allow a ‘conversation’ between programmer and machine. [63, p. 12]

相比于 computer utility 的**“消极”用户**（‘passive’ user）哲学，Strachey 和 Mc-Carty
看到的是**“积极”用户**（‘active’ user）的可能性，这些用户通过机器进行通信。

**多功能打字机（flexowriters）、电传打字电报机（teletypes）、显示器和控制台（consoles ）
的发展使得这些交互成为可能**。由此带来的影响是，很多冠以 **“conversational languages”**
（对话式语言）之称的新编程语言被开发出来

* 其中一些是对已有语言（IPL-V、MAD、APL、ALGOL）进行改造
* 另一些则是完全新开发的（LISP、BASIC、Jovial）

<a name="ch_2.6.4"></a>

### 2.6.4 分时系统的出现和发展

分时系统的第一次实验是 1961 年，使用的是 MIT 的一台 IBM 709 计算机，这套系统
**基于 FMS**，并添加了修改过的多功能打字机（flexowriters）。

由于 J.C.R. Licklider 对分时系统的热情和推广，这次展示使得 ARPA 后来赞助了许多美
国的分时项目【注 23】。从那时起，全美范围内安装了很大实验性的分时系统（见表 2）
，其中一些厂商是建立在SAGE 项目获得的经验上（MIT, DEC【注 24】, SDC, IBM），另外
一些是建立在批处理和过程控制的基础上（GE, CDC【注 25】, SDS【注 26】）。

> 【注 23】For a history of ARPA sponsored research in timesharing and its
> eventual influence, see [66, Chapter 5 & 6].
>
> 【注 24】 DEC’s PDP-1 owed much to the design of the TX-0, also its engineers closely
> communicated with MIT and BBN for the development of their time-sharing sys-
> tems. DECUS, the PDP user’s group, would play an important role in spreading
> the implementation of time-sharing on the PDP-machines.
>
> 【注 25】 An important part of CDC’s personnel, in particular William Norris and
> Seymour Cray, came from ERA where they had worked on the ERA 1103 and the Naval
> Tactical Defence System (NTDS), a computerized information processing system.
>
> 【注 26】 Max Palevsky and Robert Beck, who founded SDS in 1961, came from Bendix and
> Packard-Bell where they had been involved in developing computers such as the
> Bendix G-15 and the PB-250 that were often used as process control computers.

<p align="center">表 2：第一批分时操作系统（1960-1965）</p>
<p align="center"><img src="/assets/img/what-is-an-os-zh/table-2.png" width="80%" height="80%"></p>

**第一个稳定版分时系统出现在 1963 年底和 1964 年** [59, pp. 90-91]。至于商业版，
DEC、GE、IBM、SDS 和 CDC 直到 1965 年底才开始提供交互式的分时系统 [24]。这也标志
着之间的**分时系统的铁杆粉丝**和**批处理系统的坚定捍卫者**之间的争论正式开始【注
27】，或者换句话说，从经济的角度看，它**开启了“一个争夺价值数十亿美金的软件行业
的巨大竞争”** [58, p. 8]【注 28】。

>【注 27】The 1965 issues of both trade magazines Computers and Automation and Data-
> mation amply illustrate the early discussions.
>
>【注 28】This aspect of the time-sharing industry is closely connected to the turn
> towards viewing programs as a commodity and the emergence of the software
> industry in the 1960s, see [18], [34] and, for time-sharing in particular, [19].

<a name="ch_3"></a>

# 3 IBM 发明了“操作系统”

<a name="ch_3.1"></a>

## 3.1 “操作系统”从众多竞争术语中脱颖而出

虽然“操作系统”这个术语今天早已确立了它的正统地位【注 29】，但在当时，还有其他一
些术语也指类似的东西。Orchard-Hays 在他 1961 年的综述中评论到，人们使用多个术语
来指代操作系统的主例程（‘master’ routine）：

> 操作系统中的某一部分有多个不同术语来指代。术语 “supervisory program” 前面已经
> 提到过。supervisor 是任何时候都负责维护机器最终控制的程序，当一个例程（routine
> ）执行结束后，控制会回到 supervisor；当发生未预期的 stop 或 trap 时，控制也会
> 回到 supervisor。与 supervisor 基本同义的词有 “executive routine”、“monitor”、“
> master control routine”。[55, p. 290]

> 【注 29】It should be remarked that in other languages (and thus countries), sometimes
> different terms have prevailed. In many languages, such as Spanish, Italian,
> Swedish or Russian, a variant of ‘operating system’ is used, but in Germany, ‘
> Betriebssystem’ is the usual word, in France, ‘syst`eme d’exploitation’, in the
> Netherlands ‘bestur- ingssysteem’.

这些术语有一些变种，例如 “control sequence routine”（控制序列例程）、“executive
control”（执行控制）等等。**人们经常用这些例程（routine）的名字指代整个系统**【注 30】。
他们常说的词是 “executive system”、“monitor system”、“supervisory system”、
“control system”、“program sequencing system” 等等，而不是 “operating system”。

> 【注 30】This transfer of meaning, from a part of a system to the whole system, is
> quite a nat- ural linguistic process called ‘pars pro toto’ (the parts for the
> whole) or ‘metonymy’. Some everyday exemples of this process are: ‘I read the
> latest Stephen King’ (the author stands for the book), ‘Berlin expressed its
> support with the French people’ (Berlin, as a capital, standing for Germany or
> its gouvernment).

<a name="ch_3.2"></a>

## 3.2 “操作系统”术语历史溯源

“operating system” 这个术语是如何从众多的竞争者之中脱颖而出的呢？

这个术语本身似
乎是在 SHARE 社区提出的，第一次使用时指代的是一个具体的系统：由 SHARE 社区开发的
SHARE Operating System (SOS)。在 《Communications of the ACM》的 SHARE 系统专题
中，使用的并不是这个术语，而是 SHARE 709 System。如 D.L Shell 所说，“委员会面
临的第一个问题是定义出**一个系统意味着什么**（what was meant by a system）”，它
应该“能取得这台机器所有用户的广泛接受” [60, p. 124 and p. 126]。 对于系统的控制
部分（controlling part of the system），即 “supervisory control program”，它“在
处理一组独立作业的过程中，负责协调 SHARE 709 System 的各个部分，并维持计算机在持
续操作状态”。[14, p. 152] 它“提供了一个与机器操作相关的作业标准形式（standard
formulation of a job）”，消除了“作业之间浪费的时间（between-job time）”。这与批
处理系统的优点完全吻合，虽然其名字中并没有出现 “batch” 或 “operating system”【注 31】。

> 【注 31】 As a matter of fact, in the ACM-publications on the SHARE 709 system, the
> term ‘operating program’ is used to denote the program running on the machine.
> This use of ‘operating’ makes the use of ‘operating system’ if not impossible,
> at the very least confusing.

在 SHARE 社区的手册中，SHARE 709 System 自始至终都被称为SOS (SHARE Operating
System)。前言中写到：

> the SHARE operating system, familiarly known as SOS, is a highly flexible
> complex of languages, procedures and machine codes. The threefold purpose of the
> System is to provide assistance to the 709 programmer in the coding and
> check-out phase of program preparation, to assume from the 709 machine operator
> those burdens that may be sensibly automated and to provide the computer
> installation with an efficient operation and complete and accurate records on
> machine usage [36, sec. 01.01.01]

他们评论到，“SOS 实际上是一个集成系统（integrated system），出于方便与易于索引的
原因，这个系统分为了下面几个子系统”。这些子系统分别是：

* SHARE-Compiler-Assembler-Translator (SCAT)
* Debugging System
* Input/Output System
* Monitor

ACM 官方出版物以及实践中的这种命名法（SOS）似乎并没有完全与 IBM 709 System 相吻合。
但是，从两个信息来源都可以看出，此时 “operating system” 这个术语还并没有作为一个
正式术语，而且它的定义还局限在“编程系统”甚至“集成系统”的领域。

<a name="ch_3.3"></a>

## 3.3 “操作系统”与“编程系统”的彻底区分

IBM 709 System（SOS）之后出现的系统改变了这一现实。

在 1959 年 North American Aviation 开发的 Fortran Monitor System (FMS) 中已经可
以找到证据。**作为一个操作系统，FMS 是 SOS 在 SHARE 社区的主要竞争者，并且后来证
明比其比 SOS 更成功**【注 32】。FMS 的手册中写到，“这个 Monitor 是一个适用
709/7090 FORTRAN、FAP 以及对象程序（object programs）的监督程序（supervisor），
它会按需调用其他的系统程序（System programs）。”[38, p. 61] SOS 被定位为一种编程
语言，而 **FMS 从一开始就定位为一个 FORTRAN 程序的加载器和链接器（loader and
linker）**。这能够非常有助于**区分“操作系统”和“编程系统”**。

> 【注 32】In 1961, 76 % of IBM 709 and 7090 installations used FMS [44, p. 819]. One of
> the main reasons of SOS’s lack of succes was its failure to accommodate for
> FORTRAN usage, another one the complexity of its command language, cfr. [4, pp.
> 731-733].

也许正是 FORTRAN 以及它“编程语言”定位的成功，使得它有可能将编程系统与操作系统区
分开来【注 33】。虽然从历史的角度看，这种划分有一些人工的痕迹，并且也有一些问题
，但是下面的事实还是**强有力地增加了“操作系统”的区分性**：

* 有一个清楚可辨的“包”（package），即编程语言 FORTRAN （及其包括汇编器、编译器和
  函数库在内的系统）
* 有另一个使得访问和使用 FORTRAN 更方便的“包”（package），以及与此相关的硬件组件
  和其他编程系统

> 【注 33】The idea of programming language seems to have first developed in the user’s
> communities, notably USE (1955), and later proliferated. The emphasis on ‘
> language’ probably helped to stress that it was a coding technique that was
> universal and portable, cf. [54]. If one looks at FORTRAN in particular, a
> distinction is made within the FORTRAN system between the language, in which
> programs are written, and the translator.

这种**思想上的进步（evolution in thinking）**由 George H. Mealy 明确地表达出来，
他是 RAND 公司程序员小组的成员，致力于改进 SOS 使其支持 FORTRAN，其结果就是
RAND-SHARE 操作系统。在他的关于“操作系统”的报告中，他写到，

> 拥有一台机器的目的是运行作业，而不是编程系统。要调用用户和机器上的“编程系统”之间
> 的系统，就是在 mechanical coding aids 上投入了不必要的精力，而在操作的其他方面投
> 入了过少的精力。提到“操作系统”时，我们指的是程序员需要处理的整个编程、调试和操作
> 方面的帮助。[46, p. 4]

在这种方式下，**“操作系统”开始包含与控制（contain and control）计算机中越来越多的
编程系统**。同样地，当时在IBM 工作的 Bob Bemer，看到了操作系统作为编程系统发展过程
的第三阶段，**操作系统实际上囊括和控制了各种编程系统** [12]。

RAND-SHARE 操作系统手册的前言中，也描述了类似的编程系统与操作系统的区分：

> 操作系统是一个由很多计算机例程（computer routines）组成的综合体，用于获取程序、
> 将数据输入机器、将数据从机器输出、对数据进行转换（包括程序汇编和编译）、监督作业
> 、对任务进行排列、方便程序员和操作系统组件之间的通信 [17, p. iii]。

<a name="ch_3.4"></a>

## 3.4 人类操作员消失，操作系统成为用户和计算机之间的主要接口

对于 SOS，RAND-SHARE 系统的目的有三个：“节省机器时间；提升操作效率；节省程序员时间。”
[17, p. 5] 但现在，**人类操作员开始已经从描述中淡出，操作系统开始掌管所有编程系
统**。这使得**操作系统称为程序员和计算机之间的主要接口，操作员从中消失**（至少在
理论上，但实际上不可能完全消失！）。

IBM 即将开发的操作系统 —— IBSYS —— 加强了这种趋势。

> 7090/8094 ibsys 操作系统由一组完整的（a set of integrative）系统程序组成，在
> System Monitor 的执行控制和协调（
> executive control and coordination）下执行操作。System Monitor 通过协调各子系统
> 的操作，使得无需或只需很少人类操作员干预，就能够处理一系列不相关的任务。通过减少
> 数据处理过程中人的参与程度，7090/7094 ibsys 操作系统保证了作业能处理的更快、更高
> 效，并且人为失误的可能性更小。最终的结果是，turn-around time （例如，从程序从提
> 交一个作业到他拿到结果）显著降低。[39, p. 5]

上面的描述里几乎将操作员从方程中移除，并且假定了操作系统将成为程序员工作的催化
剂。“完整”（integrative）这个词表达了这些程序是如何放到单个配置以方便访问和使
用的，但这里并没有暗示说它们都位于相同的层次。实际上，**操作系统，尤其是它的
monitor 程序，在计算机和用户的配置中位于层级中的最上层**。它控制着如何
处理（程序）数据，减少因为人干预而导致的问题。它还控制着其他的编程系统，按照操作
系统的定义 —— “在一个 monitor 程序控制下的一组编程系统” [28, p. 631] —— 这使得它
成为一个“操作系统”。

<a name="ch_3.5"></a>

## 3.5 操作系统的组织方式：层级结构

IBSYS 系统的框图（第二章图 1）中提现出的也是类似的结构。IBM 在 60 年代早期的解决方案
最清楚地说明了如何在机器和它的系统之间添加缓冲层（how to install
buffering layers around the machine and its systems），这使得操作系统成为人类用
户和计算机之间的主要接口，访问计算机及其设施更方便。其中有 IOCS (Input Output
Control System) 处理 I/O 通信和缓冲信息，在此之上是操作系统IBSYS。
IBSYS 控制编程系统（例如 FORTRAN 和 COBOL）、子例程库和 I/O 例程，以及更早一些的
批处理系统（例如 FMS）。**每一个能完全自动化的任务都对应一个系统，而操作系统负责监
督所有这些系统，代替人类操作员（至少在理论上）的工作**。新的设施，例如远程处理（
teleprocessing），有 supervising monitor （监督监视器）提供。

“第二代”系统支持多道编程（multiprogramming），IBM 后来的系统 system OS/360 (1966)
的设计中也融合了相同的哲学。机器与用户之间有很多层，包括人类操作员和操作系统，这
些层使得用户和机器之间保持了一定距离，而监督程序处理用户侧和机器侧（各种程序和编
程系统）的所有通信。

60 年代中期的某些操作系统并没有遵循这种层级结构（hierarchical structure），大部
分是因为它们主打多道编程和/或（实时）交互，例如，Univac’s EXEC I (1962) 的核心组件是一个通
信处理器（communication processor）和一个调度例程（scheduling routine）；
CDC’s SIPROS system (1965) 有一组外设处理器（a pool of peripheral processors），
这些处理器在 monitor 的管理下执行 I/O 或顺序化（sequencing）工作。

<p align="center"><img src="/assets/img/what-is-an-os-zh/4a.png" width="100%" height="100%"></p>
<p align="center">（a）OS/360（1966）的组织结构。
左图可以清楚看出 IBM 将用户对机器的访问进行分层的哲学；右图是 OS/360 内部的层级
结构，可以看到 supervisor 位于最中间的位置。</p>

<p align="center"><img src="/assets/img/what-is-an-os-zh/4b.png" width="100%" height="100%"></p>
<p align="center">（b）Univac’s EXEC (1962) 的组织结构</p>

<p align="center"><img src="/assets/img/what-is-an-os-zh/4c.png" width="70%" height="70%"></p>
<p align="center">（c）CDC’s SIPROS (1965) 的组织结构</p>

<p align="center">图 4. 60 年代中期一些操作系统的组织结构。可以看到，与 OS/360
（图 a）相比，EXEC 和 SIPROS （图 b 和 c）的结构更加复杂。</p>

Burroughs 的 MCP (1965) 的核心是一个调度器，用来组织和操作包含计算机和编程系统基
本参数（essential  parameters of the computer and programming systems）的表（
tables），见图 4。实际上，**1964 年之后，操作系统的结构将会更加复杂，但“操作系统
”这个名字将会确定并沿用下来，即使那些新系统所做的事情远远不止是自动化人类操作员
的工作**。

<a name="ch_4"></a>

# 4 总结

如果搜索名字带 “operating system” 的书，搜索结果中第一本是 60 年代晚期出的，而且
你还可以清楚地看到 70 年代有一个高峰。这毫无疑问地说明，**在分时（time-sharing）
和软件工程（software engineering）出现之后，操作系统这个领域有多重要**。确实，
**大型程序催生了更加系统化的软件方法（more systematic approach to software）的需
求**，这种方法后来被称为 —— 虽然并不是所有人都同意 —— **软件工程（software
engineering）**，操作系统正是最具代表性的例子之一。

**OS/360 和 Multics 是操作系统的两个重要示范和标杆。如何在一台计算机上处理实时操作
、程序并发和多用户是 60 年代晚期操作系统的核心问题**。由此出现了一些基本概念和设计
方法，例如：

* 分段（segmentation）
* 文件系统（file systems）
* 虚拟内存（virtual memory）
* 调度算法（scheduling algorithms）

同样出现的还有一些通用操作系统哲学，例如，

* 虚拟机（virtual machines）
* 层级系统（hierarchical system）
* 基于内核的系统（kernel-based system）

这一时期操作系统领域的术语已经确立，人们开始撰写 “operating system” 的论文。

**但在 60 年代晚期之前，不管是在工业界还是学术界，（操作系统相关的）术语和技术都
没有达成广泛共识**。即使是 “operating system” 自身这个术语都没有确立，虽然大部分
计算机制造商看到了在其“计算机软件包”（computer package）中包含一些类似操作系统的
东西的必要性。确实，当时非常需要一些类似操作系统的东西来协助人类用户，以避免拖慢
计算机的自动化操作，并且充分利用到最新的技术进步。

**1964-1964 年间，硬件和软件的协同进步使得计算机系统和编程系统变得更加强大，用途
更加广泛**，但同时，也变得更加复杂，调研更加困难。一方面，随着更大、更快的内存设
备的出现，例如磁带和随机访问磁带驱动器（加上中断和 I/O 缓冲），另一方面，随着高
级编程系统在同一时期的发展，使得人类元素（human element），不管是程序员还是操作
员，都被计算机处理信息的速度和数量完全超越。

**伴随着自动化编程系统（automatic programming systems）的这一历史趋势的发展，第
一批操作系统出现了**。虽然在 50 年代，很难清楚地区分编程系统（programming
systems）、实用程序（utility programs）和控制程序（control programs），但**批处
理系统（batch-processing systems）的发明和多道编程（multi-programming）的到来还
是使得操作系统的概念慢慢变得独立**。虽然术语“操作系统”本身来自 IBM 及其用户，并
且这个术语还与 monitor 监控下的批处理系统紧密相连（见第三章），但同时期其他并行
在发展的系统也使得“操作系统”这个概念愈加可辨识。

**将许多通常不相关的程序组合到一起、形成一个更加结构化的配置（a more structured
configuration）的集成方式（integrative approach），在定义了操作系统轮廓的过程中
发挥了重要作用**。IBM 甚至在其 IBSYS 系统中使用了术语 “integrated” 以在事后强调
：所有程序 —— 不管新旧 —— 是如何被组织到一起、在 monitor 的监督下协同工作的 [52]。

在操作系统出现的过程中，**最后一个重要但经常被忽视的因素是特殊目的系统**（
special-purpose systems），最著名的是**命令与控制系统**（command and control
systems）和**过程控制系统**（process control systems）。这些系统中发展出的**实时操
作（real-time operation）和多道编程（multiprogramming）**经验非常重要，尤其是用
于处理异步通信的特殊目的控制程序的开发。这些通信可能发生在外设和中央处理器之间，
也可能是控制台上的人类输入或交互式设备与处理器之间。在这一领域获得的经验，为后
面分时系统的开发奠定了重要基础。

从更宽泛的层次来说，**第一批操作系统的出现是系统软件（systems software）和软件行
业（software industry）开始崛起的一部分**。**从用户驱动的软件到制造商驱动的软件**
是其中一方面，关于用户位置的争论以及 **“批处理 vs. 分时”** 的讨论是另一方面。
另外，操作系统的开发将为后来软件危机的爆发提供了一个示范。但早至 60 年代初期，
这样一种观念就开始展现：**操作系统已经成为至关重要的东西**。正如 Burroughs’s
AOSP 的开发者说的，操作系统“已经成为计算机系统的一部分，与计算机硬件本身一样重要
”。更甚者，根据他们的说法，当时“观念都有了很大变化”，即“计算机不运行程序，[...]
程序控制计算机” [5, p. 95]

<a name="ch_ack"></a>

## 致谢

I would like to thank Baptiste M ́eles for inviting me to talk
about Multics in his seminar Codes Sources and I. Astic, F. Anceau and P.
Mounier- Kuhn for giving me the opportunity to expand on operating systems
before 1964 at the CNAM seminar on the history of computing. Doing some research
for these talks and for my course Introduction to the History of Computing at
Paris 8 was the start for this study of early operating systems. Finally, I
would like to thank the organizers of the third HAPOP colloquium in Paris
where this paper was first presented as well as Liesbeth De Mol for
discussing the paper with me during the writing process. Finally, my thanks
go to two anonymous reviewers whose comments helped to improve the paper.

<a name="ch_ref"></a>

## References


1. Adams, C.W.; Gill, S. and others (eds.): Digital Computers: Business Applica- tions. Summer program 1954,
2. Adams, C.W.: Developments in programming research. AIEE-IRE ’55 (Eastern) Papers and discussions presented at the the November 7-9, 1955, eastern joint AIEE-IRE computer conference, pp. 75-79 (1955).
3. Adams, C.W.: A batch-processing operating system for the Whirlwind I com- puter. AFIPS Conference Proceedings, vol. 56, pp. 785-789 (1987).
4. Akera, A.: Voluntarism and the fruits of collaboration: The IBM user group Share. Technology and Culture, 42(4), pp.710-736 (2001).
5. Anderson, J.P.; Hoffman, S.H.; Shiman, J and Williams, R.J: The D-825, a multiple-computer system for command & control, 1962 Fall Joint Computer Conference (AFIPS), pp. 86-96 (1962).
6. Bauer, W.F.: An Integrated Computation System for the ERA-1103. Communi- cations of the ACM, 3 (3) pp. 181-185 (1956).
7. Bauer, W.F. and West, G.P.: A system for general-purpose digital-analog com- putation. Communications of the ACM, 4 (1) pp. 12-17 (1957).
8. Bauer, W.F., Use of Automatic Programming. Computers and Automation, 5 (11), pp. 6-11 (1956).
9. Bauer, W.F.: Computer Design from the Programmer’s Viewpoint. Proceedings Eastern Joint Computer Conference, December 1958, pp. 46-51.
10. Bauer, W.F. and Rosenberg, A.M.: SoftwareHistorical perspectives and current trends. Fall Joint Computer Conference, 1972, pp. 993-1007 (1972).
11. Bemer, R.: What the Engineer should know about Programming: How to con- sider a computer. Data Control Section, Automatic Control Magazine, 1957 March, pp. 66-69 (1957).
12. Bemer, R.: The Present Status, Achievement and Trends of Programming for Commercial Data Processing. In: Digitale Informationswandler, ed. Hoffmann, Wiesbaden, pp.312-349 (1962).
13. Bennington, H.D. and Gaudette, C.H.: Lincoln Laboratory Utility Program Sys- tem. AIEE-IRE ’56 (Western) Papers presented at the February 7-9, 1956, joint ACM-AIEE-IRE western computer conference p.21 (1956).
14. Bratman, H and Boldt, I.V.: The SHARE 709 System: Supervisory Control. Communications of the ACM, 6 (2), pp. 152-155 (1959).
15. Breheim, D.J.: ‘Open Shop's Programming at Rocketdyne Speeds Research and Production. Computers and Automation, 10 (7), pp. 8-9 (1961).
16. Brinch Hansen, P.: Classic Operating Systems: From Batch Processing to Dis- tributed Systems. Springer, Berlin (2001).
17. Bryan, G.E.: The RAND Share operating system manual for the IBM 7090. Memorandum RM–3327-PR, Santa Monica, CA (1962).
18. Campbell-Kelly,M.:FromAirlineReservationstoSonictheHedgehog:AHistory of the Software Industry. MIT Press: Cambridge, MA. (2003)
19. Campbell-Kelly, M. and Garcia-Swartz, D.D.: Economic Perspectives on the His- tory of the Computer Time-Sharing Industry, 1965-1985, IEEE Annals of the History of Computing, 30 (1), pp. 16-36 (2008).
20. Ceruzzi,P.:Ahistoryofmoderncomputing.2ndedition.MITPress,Cambridge, Massachussets (2003).
21. Clark, W.: The multi-sequence program concept. Lincoln Lab Memorandum 6M- 3144, 1954.
22. Cohn, J.: Transitions from Analog to Digital Computing in Electric Power Sys- tems. IEEE Annals of the History of Computing, 37 (3), pp. 32-43 (2015).
23. Corbat ́o, F. J.; Dagget, M. M.; and Daley, R.C.: An experimental time-sharing system. Proceedings AFIPS 1962 SJCC, ol. 21, pp. 335-344.
24. Computer Research Corporation: Time-Sharing System Scorecard. No. 1 Spring 1964, No. 2 Fall 1965.
25. Culler, G.J. and Fried, B.D.: An Online Computing Center for Scientific Prob- lems. M19-3U3, TRW report (1963).
26. Dines R.S.: Telecommunications and supervisory control programs. Computers and Automation 15 (5), pp. 22-24. (1966)
27. Drummond, R.E.: BESYS revisited. AFIPS Conference Proceedings, vol. 56, pp. 805-814 (1987).
28. Fisher, F.P. and Swindle G.F.: Computer programming systems. Holt, Rinehart and Winston: New York (1964).
29. Frank, W.L.: Organization of a Program Library for a Digital Computer Center. Computers and Automation, 5 (3), pp. 6-8 (1956).
30. Fredkin, E.: The time-sharing of computers. Computers and Automation, 12 (11), pp. 12-20 (1963).
31. Gilmore, J.T. Jr.: TX-0 Direct Input Utility System. Memorandum 6M-5097-1 Lincoln Lab.
32. Grabbe,E.N.(ed.):AutomationinBusinessandIndustry.London:Wiley(1957).
33. Grabbe, E.N. and Ramo, S. and Woolridge D.E.: Handbook of Automation, Computation and Control, Volume 2. Wiley, New York (1959).
34. Haigh, T. Software in the 1960s as concept, service, and product, IEEE Annals of the History of Computing 24 (1), pp. 5-13 (2002).
35. Hassitt, A.: Programming and Computer systems. Academic Press: New York and London (1967).
36. Homan, C.E. and Swindle, G.F.: Programmer’s Manual for the SHARE Oper- ating system. IBM (1959).
37. Hopper, G.: ACM Glossary. (1954)
38. Reference Guide to the 709/7090 FORTRAN Programming System. IBM: Poughkeepsie (1961). (includes material from IBM 709/7090 FORTRAN Moni- tor, form C28-6065)
39. IBM 7090/7094 IBSYS system operator’s guide. IBM: Poughkeepsie (1964).
40. IBM Field Engineering Education Student Self-Study Course: Introduction to Teleprocessing. IBM: Poughkeepsie (1965).
41. Knuth, D.E. and Pardo, L.: The early development of programming languages.  Belzer, J; Holzman, A.G.; Kent, A. (eds). Encyclopedia of Computer Science and Technology. Marcel Dekker: New York, pp. 419-496. (1979)
42. Krakowiak, S. : Les d ́ebuts d’une approche scientifique des systemes d’exploitation, Interstices, February 2014
43. Krakowiak S. and Mossiere J.: La naissance des systemes d’exploitation. Inter- stices, April 2013.
44. Larner, R.A.: FMS: The IBM FORTRAN Monitor System. AFIPS Conference Proceedings, vol. 56, pp. 815-820 (1987).
45. McCarthy, J.; Boilen S.; Fredkin E.; Licklider J. C. R.: A time-sharing debugging system for a small computer. Proc. AFIPS 1963 SJCC, vol. 23, pp. 51-57 (1963).
46. Mealy, G.H.: Operating Systems. RAND Report P-2584 (1962). Partially reprinted in [57].
47. Clark, W.A.; Mealy, G.H. and Witt, B.I: The functional structure of OS/360.  IBM Systems Journal, 5 (1), pp. 3-51 (1966).
48. Mealy,G.H.:Somethreadsinthedevelopmentofearlyoperatingsystems.AFIPS Conference Proceedings, vol. 56, pp. 779-784 (1987).
49. Mock, O.R.: The North American 701 Monitor. AFIPS Conference Proceedings, vol. 56, pp. 791-795 (1987).
50. Moncreiff, B.: An automatic supervisor for the IBM 702. AIEE-IRE ’56 (West- ern) Papers presented at the February 7-9, 1956, joint ACM-AIEE-IRE western computer conference , pp. 21-25 (1956).
51. Nelson, E.: Computer Installation at TRW systems – Some Experiences and Lessons. Computers and Automation 18 (8), pp. 21-22 (1969).
52. Noble, A.S. and Talmadge, R.B.: Design of an Integrated Programming and Operating System, I & II, IBM System Journal, vol. 2, pp. 152-181 (1963)
53. Noble, D.F.: Forces of Production: A Social History of Industrial Automation.  New York: Knopf (1984).
54. Nofre, D; Priestley, M. and Alberts, G.: When Technology Became Language: The Origins of the Linguistic Conception of Computer Programming, 1950-1960.  Technology and Culture, 55 (1), pp. 40-75. (2014)
55. Orchard-Hays, W.: The Evolution of Programming Systems. Proceedings of the IRE, 49 (1), pp. 283-295 (1961).
56. Patrick, R.L.: General Motors/North American Monitor for the IBM 704 com- puter. AFIPS Conference Proceedings, vol. 56, pp. 796-803 (1987).
57. Rosen, S.: Programming Systems and Languages. New York: McGraw-Hill (1967).
58. Sackman, H.: Man-Computer Problem solving. Princeton etc.: Auerbach (1970).
59. Schwartz, J.: Interactive systems: promises, present and future, Proceeding AFIPS '68 fall joint computer conference, part I, pp. 89-98 (1968).
60. Shell, D.L.: SHARE 709 system: a cooperative effort. Communications of the ACM, 6 (2), pp. 123-127 (1959).
61. SHARE Operating System Manual, Distribution 1 to 5. Poughkeepsie: IBM (1960).
62. Strachey, C.: Time-sharing in large fast computers. Proc. International Confer- ence on Information Processing, UNESCO (June 1959), Paris, paper B.2.19, pp. 336-341.
63. Teager, H. and McCarthy, J.: Time-shared program testing. Preprints of Papers ACM 14th National Meeting (Sept. 1959), 12-1 to 12-2.
64. Tanenbaum, A.: Modern operating systems. 2nd edition. Upper Saddle River, NJ: Prentice Hall (2001).
65. Walden D. and Van Vleck, T.: Compatible Time-Sharing System (1961-1973). Fiftieth Anniversary Commemorative Overview. IEEE Computer Society (2011).
66. Waldrop, M: The Dream Machine: J.C.R. Licklider and the Revolution That Made Computing Personal (2002).
