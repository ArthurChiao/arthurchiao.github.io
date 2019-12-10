---
layout    : post
title     : "[译] TTY 的前世今生（2008）"
date      : 2019-12-08
lastupdate: 2019-12-08
categories: tty
---

### 译者序

本文翻译自 2008 年的一篇帖子 [The TTY demystified](http://www.linusakesson.net/programming/tty/index.php).

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

## 目录

1. [历史](#ch_1)
2. [使用场景](#ch_2)
3. [进程](#ch_3)
4. [作业（Jobs）和会话](#ch_4)
5. [简单粗暴的信号机制](#ch_5)
6. [一个例子](#ch_6)
7. [流控和阻塞式 I/O](#ch_7)
8. [配置 TTY 设备](#ch_8)
9. [结束语](#ch_9)

TTY 子系统是 Linux 乃至 Unix 家族中最核心的设计之一。

但不幸的是，TTY 的重要性经常被低估，而且网上也很难找到不错的介绍性文章。而我
认为，对 Linux 中的 TTY 有一些基本了解对于**开发者和高级用户**来说是非常有帮助的。

但要注意：**接下来你将看到的东西并不是非常优雅**。事实上，TTY 子系统 —— 虽然从用
户的角度来非常好用 —— 是很多特殊场景杂糅在一起的结果。而要理解为什么会变成这样，
我们需要从历史说起。

<a name="ch_1"></a>

# 1. 历史

1869，人类发明了**股票自动报价机**（stock ticker）。它是一个用于**跨长距离实时传
递股票价格**的电子-机械设备，由一个打字机（typewriter）、一对很长的电缆（a long
pair of wires）和一个报价用的磁带打印机（tape printer）组成。后来，这个概念逐渐
进化成速度更快的、基于 ASCII 码的**电传打印机**（teletype）。电传打印机曾通过一个称
为 Telex 的网络实现全球互联，用于传递商业电报，但它们并没有连接到任何计算机（
computers）。

<p align="center"><img src="/assets/img/tty-demystified/oldschool.jpg" width="40%" height="40%"></p>
<p align="center">20 世纪 40 年代的真实电传打印机（teletypes）</p>

在同一时期，计算机 —— 虽然仍是很大很原始的机器，但能处理多任务（multitask）—— 开
始 变得越来越强大，足以实现与用户的实时交互。**当命令行（command line）最终取代
了老式的批处理模型（batch processing model）后，人们直接将电传打印机用作了计算
机的输入和输出设备（input and output devices），因为这些设备在市场上很容易买到。**

但此时面临的一个问题是：**市场上有大量的电传打印机模型**，所有模型之间都有一些细
微差别，因此就需要**某种层面的软件中间层来屏蔽这些差异**。Unix 世界中的方式是**
让操作系统内核来处理所有的低层（low-level）细节**，例如 word 长度、波特率（baud
rate）、流控（flow control）、奇偶校验（parity）、基本的行编辑（line editing）功
能所用的控制码等等。而 20 世纪 70 年代随着例如 VT-100 这样的固态视频终端（solid
state video terminals）的出现而变成为现实的**光标炫酷移动、彩色输入和其他高级特
性，则交给应用（application）来控制**。

如今在我们的世界中，物理电传打印机和视频终端事实上已经绝迹了。除非你去参观某个博
物馆或者硬件爱好者的私藏，否则你能看到的所有 TTY 很可能都是**仿真（模拟）的视频
终端**（emulated video terminals）——用软件去模拟真实硬件。但我们将会看到，这些传
统的钢铁浇筑的怪兽仍然潜伏在表面的平静之下。

<a name="ch_2"></a>

# 2. 使用场景

<p align="center"><img src="/assets/img/tty-demystified/case1.png" width="70%" height="70%"></p>

用户（通过一个物理电传打印机）在一个终端上输入（打字）。这个终端通过一对电缆连接
到计算机上的一个 **UART**（Universal Asynchronous Receiver and Transmitter，通用
异步收发器）。操作系统中安装了 UART 驱动，能够**处理字节的物理传输**，包括奇偶校
验和流控。在一个简陋的系统中，UART 驱动会**将收到的字节直接发送给某个应用进程**
。但是，以上方式缺少下列必备特性：

## 2.1 行编辑（Line editing）

大部分用户都难免在打字时犯错，因此**退格键**（backspace key）是很有必要的。
这个功能当然可以由应用自己实现，但按照 Unix 的设计哲学，应用应该越简单越好。因此
，为了方便，**操作系统**提供了一个**编辑缓冲区（editing buffer）**以及**一些基本
的编辑命令**（退格、擦除单词、清除行、重新打印），这些功能在 line discipline（行
规程）中是默认开启的。

### Line discipline

**高级应用可以选择关闭这些特性**，只要将 line discipline 从默认（或 canonical）
模式改为 raw 模式就行了。**大部分交互式应用（编辑器、邮件用户 agent、shell，以及
所有依赖 `curses` 或 `readline` 的程序）都运行在 raw 模式，自己来处理所有的行编辑
命令**。line discipline 还包含了**字符回显（character echoing）**和**回车/换行（
carriage returns and linefeeds）自动转换**的功能。如果你愿意，可以将其想象成内核
中的 `sed(1)`。

出于某些偶然的原因，**内核提供了多种 line discipline**。但在任何时刻，对于某个给
定的串行设备，内核只会 attach 其中的一种到这个设备。**默认的 discipline** 叫
`N_TTY`（位于 `drivers/char/n_tty.c`，如果你喜欢刨根究底）。其他几种 disciplines
用于不同目的，例如管理包交换数据（packet switched data，例如 ppp, IrDA, serial
mice 等等），但这些超出了本文的范围。

## 2.2 会话管理（Session management）

用户可能希望同时运行多个程序，在不同时刻
和不同的程序交互。如果一个程序进入无限循环，用户可能会杀掉或挂起这个程序。
**后台（background）启动的程序如果执行到需要向终端写数据的地方，需要被挂起**。与
此类似，**用户输入只应当被重定向到前台程序**（foreground program）。操作系统在
TTY 驱动（`drivers/char/tty_io.c`）中实现了这些特性。

我们说一个操作系统进程“活着”（alive）时（有执行上下文），意味着这个进程能够执行
动作（perform actions）。TTY 驱动并没有活着；用面向对象的术语来说，TTY 驱动是一
个被动对象（passive object）。它有一些数据字段和方法，但只有当
它在某个进程或某个内核中断处理函数的上下文中被调用时，它才能够执行。同样的，line
discipline 也是一个被动实体（passive entity）。

**UART 驱动、line discipline 实例和 TTY 驱动**三者组成一个 **TTY 设备**，
有时简称为 TTY。用户进程能够通过操作 `/dev` 目录下的相应设备文件来改变 TTY
设备的行为。进程需要对设备文件有写权限，因此当一个**用户登陆到某个特定的 TTY 时
，该用户必须成为相应设备文件的 owner**。传统上这是**通过 `login(1)` 程序实现**的
，该程序需要以 root 特权执行。

前面图中的物理线路当然也可以是一个长距离电话线路：

<p align="center"><img src="/assets/img/tty-demystified/case2.png" width="70%" height="70%"></p>

在这张图中，除了系统此时也需要处理调制解调器（modem）的 hangup 情况之外，其他方
面跟前一张没有太大区别。

接下来我们来看一个典型的桌面系统。下图展示的是 Linux console 是如何工作的：

<p align="center"><img src="/assets/img/tty-demystified/case3.png" width="70%" height="70%"></p>

TTY 驱动和 line discipline 的行为和前面例子中的一样，但其中不再涉及 UART 或物理
终端。与前面不同的地方在于，现在多了一个软件仿真的视频终端（一个复杂的状态机，包
括一个字符帧缓冲区和一些图形字符属性），渲染到一个 VGA 显示器。

控制台（console）子系统某种程度上比较刻板。如果我们**将终端仿真放到用户空间（
userland），事情就会变得更加灵活（和抽象）**。下面是 `xterm(1)` 及其衍生版本如何
工作的：

<p align="center"><img src="/assets/img/tty-demystified/case4.png" width="50%" height="50%"></p>

**为了方便将终端模拟移到用户空间且同时保持 TTY 子系统（会话管理和 line
discipline）的完整性**，人们引入了**伪终端（pseudo terminal）或称 pty**。
你也许已经猜到了，当在伪终端内运行伪终端时（running pseudo terminals inside
pseudo terminals），事情会变得更加复杂，例如 `screen(1)` 或 `ssh(1)`。

现在让我们退后一步，来看一看这些东西是如何适配到进程模型的。

<a name="ch_3"></a>

# 3. 进程

一个 Linux 进程可以处于以下几种状态之一：

<p align="center"><img src="/assets/img/tty-demystified/linuxprocess.png" width="50%" height="50%"></p>

* `R`: 运行中或可运行（Running or runnable (on run queue)）
* `D`: 不可中断睡眠（Uninterruptible sleep (waiting for some event)）
* `S`: 可中断睡眠（Interruptible sleep (waiting for some event or signal)）
* `T`: 停止（Stopped, either by a job control signal or because it is being traced by a debugger.）
* `Z`: 僵尸进程（Zombie process, terminated but not yet reaped by its parent.）

运行 `ps l` 可以看到各进程的状态。例如，如果是 sleeping 状态，`WCHAN` 列（"wait
channel"，等待队列的名字）会显示**这个进程正在等待的内核事件**（kernel event）：

```shell
$ ps l
F   UID   PID  PPID PRI  NI    VSZ   RSS WCHAN  STAT TTY        TIME COMMAND
0   500  5942  5928  15   0  12916  1460 wait   Ss   pts/14     0:00 -/bin/bash
0   500 12235  5942  15   0  21004  3572 wait   S+   pts/14     0:01 vim index.php
0   500 12580 12235  15   0   8080  1440 wait   S+   pts/14     0:00 /bin/bash -c (ps l) >/tmp/v727757/1 2>&1
0   500 12581 12580  15   0   4412   824 -      R+   pts/14     0:00 ps l
```

"wait" 等待队列（wait queue）和 `wait(2)` 系统调用相关，因此当这些进程的
任何一个子进程有任何状态变化时，这些进程就会被移动到 running 状态。

sleeping 状态有两种：可中断 sleep 和不可中断 sleep。可中断 sleep 最常见，它表示
虽然该进程当前在 wait 队列中，但只要它收到信号，就可以被移动到 running 状态。如
果查看内核源码，你会发现任何正在等待事件的内核代码都必须在 `schedule()` 返回
之后检查是否有信号 pending，如果有就 abort。

在上面 `ps` 命令的输出结果中，`STAT` 列显式了每个进程的当前状态。除此之外，这一
列还可能包含额外的属性或标记：

* `s`：表示这个进程是 session leader
* `+`：表示这个进程是一个前台进程组的一部分（part of a foreground process group）

这些属性用于作业控制（job control）。

<a name="ch_4"></a>

# 4. 作业（Jobs）和会话

当你**按下 `^Z` 键**，或**使用 `&` 在后台启动一个程序**时，就是在进行**作业控制**。

**作业和进程组的概念是一样的**（A job is the same as a process group）。shell 内
置的命令，例如 `jobs`、`fg`、`bg` 等等可以用于**管理一个会话内已有的作业**。**每
个 session 都是由一个 session leader 管理的，这个 session leader 就是 shell** ——
通过一个复杂的信号协议和系统调用来和内核紧密协作。

下面的例子展示了进程、作业和会话之间的关系：

<p align="center"><img src="/assets/img/tty-demystified/exampleterm.png" width="50%" height="50%"></p>

上图中的 shell 交互对应下面的这些进程：

<p align="center"><img src="/assets/img/tty-demystified/examplediagram.png" width="70%" height="70%"></p>

以及下面这些内核结构：

* TTY Driver (`/dev/pts/0`)

    ```
    Size: 45x13                                           # 尺寸：45x13
    Controlling process group: (101)                      # 控制进程组：101
    Foreground process group: (103)                       # 前台进程组：103
    UART configuration (ignored, since this is an xterm): # UART 配置（忽略，因为这是虚拟终端 xterm）
      Baud rate, parity, word length and much more.
    Line discipline configuration:                        # Line discipline 配置：
      cooked/raw mode, linefeed correction,               #   cooked/raw 模式
      meaning of interrupt characters etc.
    Line discipline state:                                # Line discipline 状态：
      edit buffer (currently empty),                      #   编辑缓冲区（当前为空）
      cursor position within buffer etc.
    ```

* `pipe0`

    ```
    Readable end (connected to PID 104 as file descriptor 0) # 可读端（作为文件描述符 0 连接到 PID 104）
    Writable end (connected to PID 103 as file descriptor 1) # 可写端（作为文件描述符 1 连接到 PID 103）
    Buffer                                                   # 缓冲区
    ```

这里的基本思想是：**every pipeline is a job（每条流水线都是一个作业）**，因为每个
pipeline 内的进程都需要被同时操控（stopped, resumed, killed）。这也是为什么能够
用 `kill(2)` 向一整个进程组发送信号的原因。默认情况下，`fork(2)` 会将新创建出来
的子进程放到与其父进程相同的进程组，因此，例如一个 `^C` 键就会同时影响到父子进程
。但 shell 有些不同，作为其 session leader 职责的一部分，它每次创建一个 pipeline
的时候都会创建一个新的进程组。

TTY 驱动跟踪记录前台进程组 ID（foreground process group id），但只会以**被动的方
式跟踪**。当有必要时，**session leader 必须显式更新这项信息**。类似地，TTY 驱动
也会以被动的方式跟踪所连接的终端的尺寸大小（size），但这个信息必须由终端模拟器甚
至用户来显式更新。

前面的图中可以看到，几个不同进程都将 `/dev/pts/0` attach 到了它们的标准输入。但
只有前台任务（`ls | sort` pipeline）会从 TTY 接收输入。类似地，只有前台作业是允
许写到 TTY 设备的（在默认配置下）。如果图中的 `cat` 进程试图写到该 TTY，内核会通
过一个信号挂起它。

<a name="ch_5"></a>

# 5. 简单粗暴的信号机制

现在让我们来更加近距离地看看内核中的 TTY 驱动、line discipline 和 UART 驱动
是如何与用户空间进程通信的。

UNIX 文件，包括 TTY 设备文件，都可以被读取或写入，以及通过神奇的 `ioctl(2)`（
UNIX 中的瑞士军刀）系统调用进一步操作，内核中已经为 TTY 设备实现了很多相关的
`ioctl` 操作。但是，**`ioctl` 请求必须从进程（向内核）发起，因此当内核（主动）希
望异步地与应用进行通信时，`ioctl` 就不适用了**。

在《银河系漫游指南》中， Douglas Adams 描述了一个极其迟钝的星球，上面居住了一群
意志消沉的人以及一种带有锋利牙齿的动物，后者与前者交谈的方式就是用力撕咬他们的大
腿。**这与 UNIX 非常相似**，因为内核与进程通信的方式就是向进程发送能使之瘫痪或致
命的信号。进程可能会捕获其中某些信号，然后尝试解决遇到的问题，但大部分信号都是没
有被捕获的。

因此，**信号是一种粗暴的内核与应用进程异步通信的机制**。UNIX 中信号的设计并不整
洁或通用；每个信号都是唯一的，因此必须逐个研究。

`kill -l` 命令可以查看当前系统已经实现了哪些信号。这个命令的输出可能与下面的类似
：

```
$ kill -l
 1) SIGHUP       2) SIGINT       3) SIGQUIT      4) SIGILL
 5) SIGTRAP      6) SIGABRT      7) SIGBUS       8) SIGFPE
 9) SIGKILL      10) SIGUSR1     11) SIGSEGV     12) SIGUSR2
13) SIGPIPE      14) SIGALRM     15) SIGTERM     16) SIGSTKFLT
17) SIGCHLD      18) SIGCONT     19) SIGSTOP     20) SIGTSTP
21) SIGTTIN      22) SIGTTOU     23) SIGURG      24) SIGXCPU
25) SIGXFSZ      26) SIGVTALRM   27) SIGPROF     28) SIGWINCH
29) SIGIO        30) SIGPWR      31) SIGSYS      34) SIGRTMIN
35) SIGRTMIN+1   36) SIGRTMIN+2  37) SIGRTMIN+3  38) SIGRTMIN+4
39) SIGRTMIN+5   40) SIGRTMIN+6  41) SIGRTMIN+7  42) SIGRTMIN+8
43) SIGRTMIN+9   44) SIGRTMIN+10 45) SIGRTMIN+11 46) SIGRTMIN+12
47) SIGRTMIN+13  48) SIGRTMIN+14 49) SIGRTMIN+15 50) SIGRTMAX-14
51) SIGRTMAX-13  52) SIGRTMAX-12 53) SIGRTMAX-11 54) SIGRTMAX-10
55) SIGRTMAX-9   56) SIGRTMAX-8  57) SIGRTMAX-7  58) SIGRTMAX-6
59) SIGRTMAX-5   60) SIGRTMAX-4  61) SIGRTMAX-3  62) SIGRTMAX-2
63) SIGRTMAX-1   64) SIGRTMAX    
```

如上所示，信号是从 1 开始编码的。但如果是掩码（bitmask）形式表示（例如 `ps s` 的
输出中），最不重要比特（least significant bit）表示的是 1。

本文将关下面几信号：`SIHUP`、`SIGIT`、`SIGQUI`、`SIGPIPE`、` SIGCHLD`、`SIGSTOP`
、` SIGCONT`、` SIGTSTP`、` SIGTTIN`、`SIGTTOU` 和 `SIGWINCH`。

* `SIGHUP`

    * 默认动作：Terminate
    * 可能动作：Terminate, Ignore, Function call

    当**检测到 hangup** 时，**UART 驱动**会向**整个 session** 发送 SIGHUP 信号。
    正常情况下，这会 kill 掉所有进程。某些程序，例如
    **`nohup(1)` 和 `screen(1)`，会从他们的 session（和 TTY）中  detach 出来**，
    因此这些程序的子进程无法关注到 hangup 事件。

* `SIGINT`

    * 默认动作：Terminate
    * 可能动作：Terminate, Ignore, Function call

    当输入流中出现**interactive attention character**（交互式注意字符，通常是
    `^C`，ASCII 码是 3）时，**TTY 驱动**会向**当前的前台**作业发送 `SIGINT` 信号
    ，除非这个特性被关闭了。任何对 TTY 设备有权限的人都可以修改 the interactive
    attention character 或打开/关闭这个特性；另外，**会话管理器（session manager）
    跟踪记录每个作业的 TTY 配置，当发生作业切换时会更新 TTY**。

* `SIGQUIT`

    * 默认动作：Core dump
    * 可能动作：Core dump, Ignore, Function call

    `SIGQUIT` 和 `SIGINT` 类似，但 quit 字符通常是 ^\，而且默认动作不同。

* `SIGPIPE`

    * 默认动作：Terminate
    * 可能动作：Terminate, Ignore, Function call

    对于每个尝试**向没有 reader 的 piepe 写数据的进程**，内核会向其发送
    `SIGPIPE` 信号。这很有用，因为如果没有这个信号，某些作业就无法终止。

* `SIGCHLD`
    * 默认动作：Ignore
    * 可能动作：Ignore, Function call

    当一个进程死掉或状态发生改变时（stop/continue），内核会向其父进程发送此信号
    。该信号还附带了其他信息，即该进程的进程 ID、用户 ID、退出状态码（或终止信号）
    以及其他一些执行时统计信息（execution time statistics）。session leader 使用
    这个信号跟踪它的作业。

* `SIGSTOP`

    * 默认动作：Suspend
    * 可能动作：Suspend

    该信号会无条件地挂起信号接受者，例如，该信号的动作是不能被重新配置的（
    reconfigure）。但要注意，该信号并不是在作业控制（job control）期间被内核发送
    的。`^Z` 通常情况下触发的是 `SIGTSTP` 信号，这个信号是可以被应用捕获的。例如
    ，应用可以将光标移动到屏幕底部，或者将终端置于某个已知状态，随后通过
    `SIGSTOP` 将自己置于 sleep 状态。

* `SIGCONT`

    * 默认动作：Wake up
    * 可能动作：Wake up, Wake up + Function call

    该信号会唤醒（un-suspend）一个已经 stop 的进程。**用户执行 `fg` 命令时，
    shell 会显式地发送这个信号**。由于应用无法捕获该信号，因此如果出现未预期的
    `SIGCONT` 信号，可能就表示某些进程在一段时间之前被挂起了，现在挂起被解除了。

* `SIGTSTP`

    * 默认动作：Suspend
    * 可能动作：Suspend, Ignore, Function call

    该信号与 `SIGINT` 和 `SIGQUIT` 类似，但对应的**魔法字符通常是
    `^Z`，默认动作是挂起进程**。

* `SIGTTIN`

    * 默认动作：Suspend
    * 可能动作：Suspend, Ignore, Function call

    如果一个后台作业中的进程尝试读取一个 TTY 设备，TTY 会发送该信号给整个作业。
    正常情况下这会挂起作业。

* `SIGTTOU`

    * 默认动作：Suspend
    * 可能动作：Suspend, Ignore, Function call

    如果一个后台作业中的进程尝试写一个 TTY 设备，TTY 会发送该信号给整个作业。
    正常情况下这会挂起作业。可以在 per-TTY 级别打开或关闭这个特性。

* `SIGWINCH`

    * 默认动作：Ignore
    * 可能动作：Ignore, Function call

    前面提到，TTY 设备会跟踪记录终端的尺寸（size），但这个信息需要手动更新。
    **当终端尺寸发送变化时，TTY 设备会向前台作业发送该信号**。行为良好的交互式应用，
    例如编辑器，会对此作出响应：从 TTY 设备获取新的终端尺寸，然后根据该信息重绘自己。

<a name="ch_6"></a>

# 6. 一个例子

设想你在用自己的（基于终端的）编辑器编辑某个文件。光标当前位于屏幕中央，编辑器正
忙于执行某些 CPU 密集型任务，例如在一个大文件中执行搜索或替换操作。现在假设你按
下了`^Z` 键。因为 line discipline 已经配置了捕获此字符（`^Z` 是单个字节，ASCII 码
为 26），因此你无需等待编辑器完成它正在执行的任务然后开始从 TTY 设备读取数据。

此时的情况是，**line discipline 子系统会立即向前台进程组发送 `SIGTSTP` 信号**。
这个进程组中包括编辑器进程，以及它创建出来的任何子进程。

编辑器为 `SIGTSTP` 进程注册了信号处理函数，因此内核此时开始执行该**信号处理函数**
的代码。该代码通过**向 TTY 设备写入相应的控制序列**（control sequences），**将
光标移动到屏幕最后一行**。由于编辑器仍然在前台，这个控制序列能够正常发送出去（给
TTY）。但之后，编辑器会**给自己所在的进程组发送一个 `SIGSTOP` 信号**。

编辑器此时就被挂起（stop）了。这个事件会通过一个 `SIGCHLD` 信号发送给 session leader，
其中包括了被挂起进程的进程 ID。当前台作业中的所有进程都被挂起后，session leader
从 TTY 设备中读取当前配置，保存以备后面恢复时用。session leader 使用 `ioctl` 系
统调用，继续将自己注册（install itself）为该 TTY 的当前前台进程组。然后，它打印
出类似 `"[1]+ Stopped"` 之类的信息，告知用户有一个作业刚被挂起了。

此时，`ps(1)` 会告诉你编辑器进程当前处于 stopped state ("T")。如果我们试图唤醒它
，不管是通过 shell 内置的 `bg` 命令，还是使用 `kill(1)` 发送 `SIGCONT` 信号给进程
，都会触发编辑器执行它的 `SIGCONT` 信号处理函数。该信号处理函数可能会尝试通过写
TTY 设备来重绘编辑器 GUI。但由于此时编辑器是后台作业，TTY 设备是不允许其写入的。
这种情况下 TTY 会给编辑器发送 `SIGTTOU` 信号，再次将其 stop。这个事件会通过
`SIGCHLD` 信号通知到 session leader，然后 shell 会再次将 `"[1]+ Stopped"` 之类的
消息写到终端。
 
但当我们输入 `fg` 命令时，shell 首先会恢复此前保存的 line discipline 配置。
然后，它通知 TTY 驱动从现在开始编辑器作业应当被作为前台作业对待了。最后，它发送
一个 `SIGCONT` 信号给进程组。编辑器进程尝试重绘 GUI，而这一次它不会被 `SIGTTOU`
中断了，因为它现在是前台作业的一部分了。

> （译者）总结：
> 
> 1. 使用编辑器编辑文件。
> 2. 按 `^Z` 键 -> 唤醒 line discipline。
> 3. line discipline -> 前台进程组：`SIGTSTP`。
> 4. 编辑器 `SIGTSTP` 信号处理函数 -> TTY：写入控制序列，将光标移动到最后屏幕一行
> 5. 编辑器 `SIGTSTP` 信号处理函数 -> 自己所在的进程组：`SIGSTOP`。
> 6. 编辑器被挂起（stop）。这个事件会通过一个 `SIGCHLD` 信号发送给 session
>    leader，其中包括了被挂起进程的进程 ID。
> 7. 前台进程组中的所有进程都被挂起，session leader 从 TTY 中读取当前配置并保存
> 8. session leader 使用 `ioctl` 系统调用，继续将自己注册（install itself）为该
>    TTY 的当前前台进程组。然后，它打印出类似 `"[1]+ Stopped"` 之类的信息，告知
>    用户有一个作业刚被挂起了。
> 9. `bg` 或 `kill -SIGCONT` 给编辑器发信号：编辑器会尝试写 TTY 来重绘窗口，但此
>    时编辑器进程是后台进程，不允许写 TTY，因此 TTY 会给其发送 `SIGTTOU` 信号，
>    再次将其 stop；这个事件会通过 `SIGCHLD` 信号告知 session leader，后者再次将
>    `[1]+ Stopped` 信息写到终端。
> 10. 但当我们输入 `fg` 命令时，shell 会恢复此前保存的 line discipline 配置。然
>     后通知 TTY 驱动编辑器进程现在是前台进程了。最后，它发送一个 `SIGCONT` 信号
>     给进程组恢复编辑器的执行。

<a name="ch_7"></a>

# 7. 流控和阻塞式 I/O

<p align="center"><img src="/assets/img/tty-demystified/dsc00043.jpg" width="40%" height="40%"></p>

在 xterm 中执行 `yes` 命令，你会看到大量的 `"yes"` 一行一行地快速闪过。正常情况
下条，`yes` 进程产生 `"yes"` 输出的速度要远快于 xterm 应用解析这些行、更新帧缓冲
区、与 X server 通信来滚动窗口等等的速度。那么，**这些进程之间是如何协作的呢**？

**答案就是 blocking I/O**（阻塞式输入/输出）。伪终端只能在其内核缓冲区中保存一定量
的数据，当缓冲区已经填满而 `yes` 程序仍然调用 `write(2)` 写入时，`write(2)` 会阻
塞，`yes` 进程会被移入可中断 sleep 状态，直到 xterm 进程读走了一部分缓存的数据。

当 TTY 连接到的是串口（serial port）时，过程与此类似。`yes` 能够以很快的速度
发送数据，例如 9600 波特，但如果串口速度比这个低，内核缓冲区很快就会塞满，随后的
任何 `write(2)` 调用都会阻塞写进程（或者返回 `EAGAIN` 错误码 —— 如果进程请求的是非
阻塞 I/O）。

如果我告诉你，我们能够**显式地将 TTY 置于阻塞状态**，即使内核缓冲区中仍然有可用
空间呢？这样设置之后，每个进程调用 `write(2)` 进行写入时，TTY 都会自动阻塞。但
**什么情况下回用到这个特性呢**？

设想我们正在以 9600 波特和某个陈旧的 VT-100 硬件通信。我们刚发送了一个复杂的控制
序列要求终端滚动显示页面。此时，终端忙于执行滚动操作，无法以全速 9600 波特接收新
的数据。这种情况下，在物理上，终端 UART 仍然运行在 9600 波特，但缓冲区中没有足够
的空间来给终端存储接收到的数据。这就是一个将 TTY 置于阻塞状态的好时机。那么**要实
现这个效果，我们该怎么做呢**？

前面已经看到，可以**配置 TTY 设备对某些特定的数据给予特殊对待**。例如，在默认配
置中，TTY 收到的 `^C` 字符并不会通过 `read(2)` 直接交给应用，而是会触发发送一个
`SIGINT` 信号给前台作业。类似地，可以配置 TTY 对 **stop flow byte**（停止流字节）
和**start flow byte**（开始流字节）做出响应。通常情况下，这**分别是`^S` (ASCII
code 19) 和 `^Q` (ASCII code 17)**。老式硬件终端能自动发送这些字节，然后期待操作
系统能够按照约定对它的数据流进行管控。这个过程称为**流控**（flow control），这也是
**为什么有时你误按了 `^S` 时，你的 xterm 会锁定的原因**。

这里要区分两种情况：

* **向一个由于流控或内核缓冲空间不足而 stop 的 TTY 写入**：写入进程会被阻塞（block）
* **从后台作业向一个 TTY 写入**：会导致 TTY 发送一个 `SIGTTOU` 给整个进程组将其挂起（suspend）

我不清楚 UNIX 的设计者为何发明 `SIGTTOU` 和 `SIGTTIN` 而不是依靠 blocking I/O，
我尽己所能猜到的原因是：负责着作业控制（job control）的 TTY 驱动，设计用于监控和
操作全部作业，而不是作业内的单个进程。

<a name="ch_8"></a>

# 8. 配置 TTY 设备

<p align="center"><img src="/assets/img/tty-demystified/cockpit.jpg" width="40%" height="40%"></p>

要确定当前 shell 的 TTY，可以通过我们前面介绍的 `ps l` 命令，或者直接运行
`tty(1)` 命令。

一个进程可能会**通过 `ioctl(2)` 读取或修改一个已经打开的 TTY 设备**。相应的 API
在 `tty_ioctl(4)` 中作了描述。由于这是 Linux 应用和内核之间的二进制接口的一部分，
因此它在不同的 Linux 版本之间是保持稳定的。但是，**这个接口是不可移植的**，若想编
写可移植的程序，应用应当使用 **`termios(3)` man page 中提供的 POSIX wrapper**。

这里我不会深入介绍 `termios(3)` 接口，但如果你正在编写 C 程序，涉及到捕获 `^C`、
关闭行编辑或字符回显、修改串口的波特率、关闭流控等等工作，那你就需要去阅读前面提
到的 man page。

另外还有一个**命令行工具 `stty(1)`，用于操纵 TTY 设备**。它使用了 `termios(3)` API。

我们来试试！

## TTY 配置选项

`stty -a` 打印所有配置项。默认打印的是当前 shell 所 attach 的 TTY 设备配置项，但
可以通过 `-F` 指定其他设备。

```shell
$ stty -a
speed 38400 baud; rows 73; columns 238; line = 0;
intr = ^C; quit = ^\; erase = ^?; kill = ^U; eof = ^D; eol = <undef>; eol2 = <undef>; swtch = <undef>; start = ^Q; stop = ^S; susp = ^Z; rprnt = ^R; werase = ^W; lnext = ^V; flush = ^O; min = 1; time = 0;
-parenb -parodd cs8 -hupcl -cstopb cread -clocal -crtscts
-ignbrk brkint ignpar -parmrk -inpck -istrip -inlcr -igncr icrnl ixon -ixoff -iuclc -ixany imaxbel -iutf8
opost -olcuc -ocrnl onlcr -onocr -onlret -ofill -ofdel nl0 cr0 tab0 bs0 vt0 ff0
isig icanon iexten echo echoe echok -echonl -noflsh -xcase -tostop -echoprt echoctl echoke
```

以上选项中，某些是 UART 参数；某些影响 line discipline，某些用于作业控制。我们先
来看第一行：

* `speed`
    * UART 参数
    * 波特率。伪终端忽略此选项。
* `rows` 和 `columns`
    * TTY 驱动参数
    * attach 到这个 TTY 设备的终端大小（size），单位是字符数。本质上这只是内核空
      间中的一对变量，可以随意修改和读取。修改这两个参数会触发 TTY 驱动发送
      `SIGWINCH` 信号给前台作业。
* `line`
    * Line discipline 参数
    * 表示 attach 到这个 TTY 的 line discipline。0 代表 `N_TTY`。所有的合法值列在
      `/proc/tty/ldiscs` 下面。未列出的值似乎是 `N_TTY` 的 alias，但不依赖前者。

## 修改窗口尺寸

尝试下面的例子：开启一个 xterm。记录下它的 TTY 设备（执行 `tty` 命令查看）以及尺
寸（执行 `stty -a` 命令查看）。在 xterm 中启动 vim（或其他全屏终端应用）。编辑器会询问 TTY 设
备当前的终端尺寸，以填充整个窗口。

现在，在另一个 shell 窗口中执行：

```shell
$ stty -F X rows Y
```

其中 X 是 TTY 设备，Y 是终端高度的一半。这条命令会更新内核内存中的 TTY 配置数据
，并触发向编辑器发送一个 `SIGWINCH` 信号；vim 收到信号会立即重绘自身，结果是编辑
器的高度减半。

## 修改 SIGINT 对应的控制字符

`stty -a` 命令的输出中，第二行列出了所有的特殊字符。

打开一个新 xterm 然后尝试：

```shell
$ stty intr o
```

现在输入字符 `o` —— 而不是原来默认的 `^C` —— 会触发发送 `SIGINT` 信号给前台作业。

你可以运行着某个命令，例如 `cat`，然后验证此时 `^C` 是不能终止其执行的。然后，再
试试输入 `hello` 给 `cat`。

## 退格键无法使用

某些场合下，你可能会在某个 UNIX 系统上遇到退格键无法使用的情况。

发生这种情况是因为**终端模拟器发送的退格码**（不管是 ASCII 8 还是 127）**与 TTY
设备中的擦除设置（erase setting）不匹配**。要解决这个问题，通常需要**输入 `stty`
擦除 `^H`（ASCII 8）或 stty erase `^?`（ASCII 127）**。但请记住，某些终端应用使
用 `readline`，它们会将 line discipline 置于 raw 模式，这些应用不会受此影响。

## TTY 开关项

最后，`stty -a` 列出了一系列的开关。这些开关并没有先后顺序。某些与 UART 相关，某
些影响 line discipline 行为，某些用于流控，某些用于作业控制。有减号（`-`）表示该
开关当前是关闭的；否则就是打开的。所有开关都在 `stty(1)` man page 中有解释，因此
这里只是简要介绍几个：

`icanon` 打开/关闭 canonical (line-based) 模式。尝试在一个新 xterm 内运行：

```shell
$ stty -icanon; cat 
```

执行这条命令后，所有的行编辑字符，例如退格和 `^U` 将无法使用。你会注意到 `cat` 此
时开始按字符接收（以及打印）内容，而不是像之前一样按行。

`echo` 打开字符回显（character echoing），这个选项默认是打开的。重新启用
canonical mode（`stty icanon`），然后执行：

```shell
$ stty -echo; cat
```

输入命令时，终端模拟器会将命令信息发送给内核。通常情况下，内核会将相同的信息回显给
终端模拟器，这样我们就可以看到自己输入的内容了。没有字符回显的话，我们无法看到自己输
入的内容，但由于我们在 cooked 模式，因此行编辑设施还是仍然工作的。当按下回车键
时，line discipline 会将编辑缓冲区发送给 `cat`，后者就会显示输入的内容。

`tostop` 控制是否允许后台作业写终端。首先尝试：

```shell
$ stty tostop; (sleep 5; echo hello, world) &
```

`&` 使得前面的进程以后台作业的方式执行。5 秒之后，该作业会尝试写 TTY。TTY 驱动会
使用 SIGTTOU 来挂起该进程，shell 可能会报告这个结果，可能是立即，也可能是某个时
候弹出一个提醒框。现在 `kill` 掉后台作业，执行：

```shell
stty -tostop; (sleep 5; echo hello, world) &
```

以上命令会重新打开输入回显功能；5 秒之后，后台作业发送 `hello, world` 给终端，此
时不管你正在输入什么，这句话都会打印出来。

最后，`stty sane` 会将 TTY 设备恢复到某个合理的配置。

<a name="ch_9"></a>

# 9. 结束语

本文提供了 TTY 驱动和 line discipline 相关的知识，以及它们和终端、行编辑及作业控
制的联系，希望这些内容足够读者对它们有一个了解。更多信息请参考前面提到的几个 man
page，以及 glibc 手册（`info libc`，"Job Control"）。

最后，感谢阅读！
