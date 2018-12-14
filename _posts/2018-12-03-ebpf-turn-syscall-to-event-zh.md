---
layout: post
title:  "[译] eBPF内核探测：如何将任意系统调用转换成事件"
date:   2018-12-03
author: ArthurChiao
categories: eBPF tracing bcc
---

译者按：本文翻译自2016年的一篇英文博客 [How to turn any syscall into an event: Introducing eBPF Kernel probes](https://blog.yadutaf.fr/2016/03/30/turn-any-syscall-into-event-introducing-ebpf-kernel-probes/)。**如果能看懂英文，我建议你阅读原文，或者和本文对照看。**

----

**写给不想读长文的人(TL; DR)**：Linux `4.4+`支持`eBPF`，使用它可以将任何的**内核
函数调用**转换成**可带任何数据**的**用户空间事件**，而`bcc`使这个过程更加方便。
内核探测代码用C写，数据处理代码用Python。

如果你对eBPF或Linux tracing不是太熟悉，建议你阅读全文。本文循序渐进，并介绍了我在
上手bcc/eBPF时遇到的一些坑，这会节省你大量的时间。

## 1 消息系统：Push还是Pull

刚接触容器时，我曾思考如何根据系统的真实状态动态地更新负载均衡器的配置。一
个通常可用的方案是，每当容器编排服务（orchestrator）启动一个容器，就由它去负责轮
询这个容器，然后根据健康检查的结果触发一次负载均衡器的配置更新。这看上去是个简单
的"SYN"测试（探测新启动的服务是否正常）。

这种方式可以工作，但也有缺点：负载均衡器需要（分心）等待其他系统的结果，而它实际
上只应该负责负载均衡。

我们能做的更好吗？

当你希望一个程序能对系统变化做出反应时，通常有2种可能的方式。一种是程序主动去轮
询，检查系统变化；另一种，如果系统支持事件通知的话，让它主动通知程序。**使用push还
是pull取决于具体的问题**。通常的经验是，如果事件频率相对于事件处理时间来说比较低，
那push模型比较合适；如果事件频率很高，就采用pull模型，否则系统变得不稳定。例如，
通常的网络驱动会等待网卡事件，而dpdk这样的框架会主动poll网卡，以取得最高的吞吐性
能和最低的延迟。

在一个理想的世界中，我们有如下事件机制：

* **操作系统**："嗨，容器管理服务，我刚给一个容器创建了一个socket，你需要更新你的状态吗？"
* **容器管理服务**："喔谢谢你的通知，我需要更新。"

虽然Linux有大量的函数接口用于事件处理，其中包括3个用于文件事件的，但没有专门的用
于socket事件的。你可以获取路由表事件、邻居表（2层转发表，译者注）事件，conntrack
事件，接口（网络设备，译者注）变动事件，但就是没有socket事件。或者非要说有，也可
以说有，但是深深地隐藏在一个Netlink接口中。

理想情况下，我们需要一个**通用的方式**处理事件。怎么做呢？

## 2 内核跟踪和eBPF简史

直到最近，唯一的通用方式是**给内核打补丁，或者使用
[SystemTap](https://en.wikipedia.org/wiki/SystemTap)**。SystemTap是一个tracing系
统，简单来说，它提供了一种领域特定语言（DSL），代码编译成内核模块，然后热加载到
运行中的内核。但出于安全考虑，一些生产系统禁止动态模块加载，例如我研究eBPF时所用
的系统。另一种方式是给内核打补丁来触发事件，可能会基于Netlink。这种方式不太方便
，内核hacking有副作用，例如新引入的特性也许有毒，而且会增加维护成本。

从Linux 3.15开始，将任何可跟踪的内核函数**安全地**转换成事件，很可能将成为现实。
在计算机科学的表述中，**“安全地”**经常是指通过“一类虚拟机”执行代码，这里也不例外
。事实上，Linux内部的这个“虚拟机”已经存在了几年了，从1997年的2.1.75版本就开始了
。它称作伯克利包过滤器（Berkeley Packet Filter），缩写BPF。从名字就可以看出，它
最开始是为BSD防火墙开发的。它只有两个寄存器，只允许前向跳转，这意味着你不能用它
实现循环（如果非要说行也可以：如果你知道最大的循环次数，那可以手动做循环展开）。
这样设计是为了保证程序会结束，不会让操作系统卡住。你可能在考虑，我已经有iptables
做防火墙了，要这个有什么用？（作为一个例子，）它是CloudFlare的防DDOS攻击工具
[AntiDDos](https://blog.cloudflare.com/bpf-the-forgotten-bytecode/)的基础。

从Linux 3.15开始，BPF被扩展成了eBPF，extended BPF的缩写。它从2个32bit寄存器扩展
到了10个64bit寄存器，并增加了后向跳转。Linux 3.18中又对它进行了进一步扩展，从网
络子系统中移出来，并添加了maps等工具。为了保证安全性，它引入了一个检测器，用于验
证内存访问的合法性和可能的代码路径。如果检测器不能推断出程序会在有限的步骤内结束
，它会拒绝程序的注入（内核）。

更多关于eBPF的历史，可以参考Oracle的一篇精彩[分享
](http://events.linuxfoundation.org/sites/events/files/slides/tracing-linux-ezannoni-linuxcon-ja-2015_0.pdf)
。

下面让我们正式开始。

## 3 你好，世界

即使对大神级程序员来说，写汇编代码也并不太方便，因此我们这里使用`bcc`。`bcc`是基于
LLVM的工具集，用Python封装了底层机器相关的细节。探测代码用C写，数据用Python分
析，可以比较容易地开发一些实用工具。

我们从安装bcc开始。本文的一些例子需要4.4以上内核。如果你要尝试运行这些例子，我强
烈建议你启动一个**虚拟机**。注意是虚拟机，而**不是docker容器**。容器使用的是宿主
机内核，因此你不能单独更改容器内核。安装参考
[GitHub](https://github.com/iovisor/bcc/blob/master/INSTALL.md)。

我们的目标是，每当有程序监听TCP socket，就得到一个事件通知。当在`AF_INET +
SOCK_STREAM`类型socket上调用系统调用`listen()`时，底层的负责处理的内核函数就是
`inet_listen()`。我们从用`kprobe`在它的入口做hook，打印一个"Hello, World"开始。

```python
from bcc import BPF

# Hello BPF Program
bpf_text = """
#include <net/inet_sock.h>
#include <bcc/proto.h>

// 1. Attach kprobe to "inet_listen"
int kprobe__inet_listen(struct pt_regs *ctx, struct socket *sock, int backlog)
{
    bpf_trace_printk("Hello World!\\n");
    return 0;
};
"""

# 2. Build and Inject program
b = BPF(text=bpf_text)

# 3. Print debug output
while True:
    print b.trace_readline()
```

这个程序做了3件事情：

1. 依据特定的命名规则，将探测点attach到`inet_listen`函数。举个例子，按照这种规则，如果`my_probe`被调用，它
   将会被显式地attach到`b.attach_kprobe("inet_listen", "my_probe")`。
2. 使用LLM eBPF后端编译，将生成的字节码用`bpf()`系统调用注入（inject）内核，并自动根据命名规则attach到probe点。
3. 从内核管道读取原始格式的输出

`bpf_trace_printk()`是内核函数`printk()`的简单版，用于debug。它可以将tracing信息
打印到`/sys/kernel/debug/tracing/trace_pipe`下面的一个特殊管道，从名字就可以看出
这是一个管道。如果有多个程序读，只有一个会读到，因此对生产环境并不适用。

幸运的是，Linux 3.19为消息传递引入了maps，4.4引入了任意perf事件的支持。本文后面
会展示perf事件的例子。

```shell
# From a first console
ubuntu@bcc:~/dev/listen-evts$ sudo /python tcv4listen.py
nc-4940  [000] d... 22666.991714: : Hello World!

# From a second console
ubuntu@bcc:~$ nc -l 0 4242
^C
```

成功！

## 4 改进

接下来让我们的事件发送一些有用的信息出来。

### 4.1 抓取backlog信息

"backlog"是TCP socket允许建立的最大连接的数量(，等待被`accept()`)。

只需对`bpf_trace_printk`稍作调整：

```python
bpf_trace_printk("Listening with with up to %d pending connections!\\n", backlog);
```

重新运行：

```shell
(bcc)ubuntu@bcc:~/dev/listen-evts$ sudo python tcv4listen.py
nc-5020  [000] d... 25497.154070: : Listening with with up to 1 pending connections!
```

`nc`是个**单连接**的小工具，因此backlog是1。如果Nginx或Redis，这里将会是128，
后面会看到。

是不是很简单？接下来再获取端口和IP信息。

### 4.2 抓取Port和IP信息

浏览内核`inet_listen`代码发现，我们需要从`socket`对象中拿到`inet_sock`字段。从内
核直接拷贝这两行代码，放到我们tracing程序的开始处：

```c
// cast types. Intermediate cast not needed, kept for readability
struct sock *sk = sock->sk;
struct inet_sock *inet = inet_sk(sk);
```

现在Port可以从`inet->inet_sport`中获得，注意是网络序（大端）。

如此简单！再更新下打印：

```c
bpf_trace_printk("Listening on port %d!\\n", inet->inet_sport);
```

运行：

```shell
ubuntu@bcc:~/dev/listen-evts$ sudo /python tcv4listen.py
...
R1 invalid mem access 'inv'
...
Exception: Failed to load BPF program kprobe__inet_listen
```

从出错信息看，内核检测器无法证明这个程序的内存访问是合法的。解决办法是让内存访问
变得更加显式：使用受信任的`bpf_probe_read`函数，只要有必要的安全检测，可以用它读
取任何内存地址。

```c
// Explicit initialization. The "=0" part is needed to "give life" to the variable on the stack
u16 lport = 0;

// Explicit arbitrary memory access. Read it:
// Read into 'lport', 'sizeof(lport)' bytes from 'inet->inet_sport' memory location
bpf_probe_read(&lport, sizeof(lport), &(inet->inet_sport));
```

获取IP与此类似，从`inet->inet_rcv_saddr`读取。综上，现在我们可以读取backlog，
port和绑定的IP：

```python
from bcc import BPF

# BPF Program
bpf_text = """
#include <net/sock.h>
#include <net/inet_sock.h>
#include <bcc/proto.h>

// Send an event for each IPv4 listen with PID, bound address and port
int kprobe__inet_listen(struct pt_regs *ctx, struct socket *sock, int backlog)
{
    // Cast types. Intermediate cast not needed, kept for readability
    struct sock *sk = sock->sk;
    struct inet_sock *inet = inet_sk(sk);

    // Working values. You *need* to initialize them to give them "life" on the stack and use them afterward
    u32 laddr = 0;
    u16 lport = 0;

    // Pull in details. As 'inet_sk' is internally a type cast, we need to use 'bpf_probe_read'
    // read: load into 'laddr' 'sizeof(laddr)' bytes from address 'inet->inet_rcv_saddr'
    bpf_probe_read(&laddr, sizeof(laddr), &(inet->inet_rcv_saddr));
    bpf_probe_read(&lport, sizeof(lport), &(inet->inet_sport));

    // Push event
    bpf_trace_printk("Listening on %x %d with %d pending connections\\n", ntohl(laddr), ntohs(lport), backlog);
    return 0;
};
"""

# Build and Inject BPF
b = BPF(text=bpf_text)

# Print debug output
while True:
  print b.trace_readline()
```

输出信息：

```shell
(bcc)ubuntu@bcc:~/dev/listen-evts$ sudo python tcv4listen.py
nc-5024  [000] d... 25821.166286: : Listening on 7f000001 4242 with 1 pending connections
```

这里IP是用16进制打印的，没有转换成人类可读的格式。

注：你可能会有疑问，为什么`ntohs`和`ntohl`并不是受信任的，却可以在BPF里被调用。
这是因为他们是定义在`.h`文件中的内联函数，在写作本文期间，修了一个与此相关的小
[bug](https://github.com/iovisor/bcc/pull/453)。

接下来，我们想获取相关的容器（container）。对于网络，这意味着我们要获得网络命名
空间。网络命名空间是容器的基石之一，使得（docker等）容器拥有隔离的网络。

### 4.3 抓取网络命名空间信息

在用户空间，可以在`/proc/PID/ns/net`下面查看网络命名空间。格式类似于
`net:[4026531957]`。中括号中的数字是网络命名空间的inode值。这意味着，想获取命名
空间，我们直接去读`/proc`就行了。但是，这种方式太粗暴，只适用于运行时间比较短的
进程；而且还存在竞争。我们接下来从kernel直接读取inode值，幸运的是，这很容易：

```c
// Create an populate the variable
u32 netns = 0;

// Read the netns inode number, like /proc does
netns = sk->__sk_common.skc_net.net->ns.inum;
```

更新打印格式：

```c
bpf_trace_printk("Listening on %x %d with %d pending connections in container %d\\n", ntohl(laddr), ntohs(lport), backlog, netns);
```

执行的时候，遇到如下错误：

```shell
(bcc)ubuntu@bcc:~/dev/listen-evts$ sudo python tcv4listen.py
error: in function kprobe__inet_listen i32 (%struct.pt_regs*, %struct.socket*, i32)
too many args to 0x1ba9108: i64 = Constant<6>
```

clang想告诉你的是：`bpf_trace_printk`只能带4个参数，而你传了5个给它。这里我不展
开，只告诉你结论：这是BPF的限制。如果你想深入了解，[这里
](http://lxr.free-electrons.com/source/kernel/trace/bpf_trace.c#L86)是一个不错的
入门点。

唯一解决这个问题的办法就是。。把eBPF做到生产ready（写作本文时还没，因此eBPF的探
索就都这里了，译者注）。所以接下来我们换到perf，它支持传递任意大小的结构体到用户
空间。注意需要Linux 4.4以上内核。

要使用perf，我们需要：

1. 定义一个结构体
1. 声明一个事件
1. 推送（push）事件
1. 在Python端再定义一遍这个事件（将来这一步就不需要了）
1. 消费并格式化输出事件

看起来要做的事情很多，其实不是。

C端：

```c
// At the begining of the C program, declare our event
struct listen_evt_t {
    u64 laddr;
    u64 lport;
    u64 netns;
    u64 backlog;
};
BPF_PERF_OUTPUT(listen_evt);

// In kprobe__inet_listen, replace the printk with
struct listen_evt_t evt = {
    .laddr = ntohl(laddr),
    .lport = ntohs(lport),
    .netns = netns,
    .backlog = backlog,
};
listen_evt.perf_submit(ctx, &evt, sizeof(evt));
```

Python端事情稍微多一点：

```python
# We need ctypes to parse the event structure
import ctypes

# Declare data format
class ListenEvt(ctypes.Structure):
    _fields_ = [
        ("laddr",   ctypes.c_ulonglong),
        ("lport",   ctypes.c_ulonglong),
        ("netns",   ctypes.c_ulonglong),
        ("backlog", ctypes.c_ulonglong),
    ]

# Declare event printer
def print_event(cpu, data, size):
    event = ctypes.cast(data, ctypes.POINTER(ListenEvt)).contents
    print("Listening on %x %d with %d pending connections in container %d" % (
        event.laddr,
        event.lport,
        event.backlog,
        event.netns,
    ))

# Replace the event loop
b["listen_evt"].open_perf_buffer(print_event)
while True:
    b.kprobe_poll()
```

测试一下，这里我用一个跑在容器里的redis，在宿主机上用`nc`命令：

```shell
(bcc)ubuntu@bcc:~/dev/listen-evts$ sudo python tcv4listen.py
Listening on 0 6379 with 128 pending connections in container 4026532165
Listening on 0 6379 with 128 pending connections in container 4026532165
Listening on 7f000001 6588 with 1 pending connections in container 4026531957
```

## 5 结束语

使用eBPF，任何内核的函数调用都可以转换成事件触发的方式。
本文也展示了笔者过程中遇到的一些常见的坑。完整代码（包括IPv6支持）码见
[https://github.com/iovisor/bcc/blob/master/tools/solisten.py](https://github.com/iovisor/bcc/blob/master/tools/solisten.py)
，感谢bcc team的支持，现在它已经是一个正式工具。

如果想更深入的了解这个topic，建议阅读Brendan Gregg的博客，尤其是关于eBPF的maps和
statistics的[这一篇](http://www.brendangregg.com/blog/2015-05-15/ebpf-one-small-step.html)。
Brendan Gregg是这个项目的主要贡献者之一。
