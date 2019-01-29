---
layout: post
title:  "[译] 网络包内核路径跟踪：3 eBPF"
date:   2018-11-30
author: ArthurChiao
categories: eBPF perf tracepoint
---

译者按：本文翻译自一篇英文博客[Tracing a packet's journey using Linux tracepoints, perf and eBPF](https://blog.yadutaf.fr/2017/07/28/tracing-a-packet-journey-using-linux-tracepoints-perf-ebpf/)。由于原文篇幅较长，我将其分成了三篇，并添加了适当的标题。
本文不会100%逐词逐句翻译，那样的翻译太过生硬，看看《TCP/IP详解》中文版就知道了。
例如，有多少人会在讨论网络问题的时候说**"插口"**而不是**"socket"**？在技术领域，过
度翻译反而会带来交流障碍。**如果能看懂英文，我建议你阅读原文，或者和本文对照看。**

----

前面介绍的内容已经可以满足大部分tracing场景的需求了。如果你只是想学习如何在Linux上
跟踪一个packet的传输路径，那到此已经足够了。但如果你想跟进一步，学习如何写一个自定
义的过滤器，跟踪网络命名空间、源IP、目的IP等信息，请继续往下读。

## 1 eBPF和kprobes

从Linux 内核4.7开始，eBPF程序可以attach到内核跟踪点(kernel tracepoints)。在此之
前，要完成类似的工作，只能用kprobes之类的工具attach到**导出的内核函数**
(exported kernel sysbols)。后者虽然可以完成工作，但存在很多不足：

1. 内核的内部（internal）API不稳定
2. 出于性能考虑，大部分网络相关的内层函数(inner functions)都是内联或者静态的（inlined or static），两者都不可探测
3. 找出调用某个函数的所有地方是相当乏味的，有时所需的字段数据不全具备

这篇博客的早期版本使用了kprobes，但结果并不是太好。

现在，诚实地说，通过内核tracepoints访问数据比通过kprobe要更加乏味。我尽量保持本文简
洁，如果你想了解本文稍老的版本，可以访问这里[英文](https://blog.yadutaf.fr/2016/03/30/turn-any-syscall-into-event-introducing-ebpf-kernel-probes/)
，[中文翻译](/blog/ebpf-turn-syscall-to-event-zh)。

## 2 安装

我不是徒手写汇编（handwritten assembly）的粉丝，因此将使用 `bcc`。`bcc`是一个灵
活强大的工具，允许用受限的C语法（restricted C）写内核探测代码，然后用Python在用
户态做控制。这种方式对于生产环境算是重量级，但对开发来说非常完美。

**注意：eBPF需要Linux Kernel 4.7+。**

Ubuntu 17.04 [安装 (GitHub)](https://github.com/iovisor/bcc/blob/master/INSTALL.md) `bcc`:

```shell
# Install dependencies
$ sudo apt install bison build-essential cmake flex git libedit-dev python zlib1g-dev libelf-dev libllvm4.0 llvm-dev libclang-dev luajit luajit-5.1-dev

# Grab the sources
$ git clone https://github.com/iovisor/bcc.git
$ mkdir bcc/build
$ cd bcc/build
$ cmake .. -DCMAKE_INSTALL_PREFIX=/usr
$ make
$ sudo make install
```

## 3 自定义跟踪器：Hello World

接下来我们从一个简单的hello world例子开始，展示如何在底层打点。我们还是用上一篇
文章里选择的四个点：

* `net_dev_queue`
* `netif_receive_skb_entry`
* `netif_rx`
* `napi_gro_receive_entry`

每当网络包经过这些点，就会触发我们的处理逻辑。为保持简单，我们的处理逻辑只是将程
序的`comm`字段（16字节字符串）发送出来（到用户空间程序），这个字段里存的是发送相应
的网络包的程序的名字。

```c
#include <bcc/proto.h>
#include <linux/sched.h>

// Event structure
struct route_evt_t {
        char comm[TASK_COMM_LEN];
};
BPF_PERF_OUTPUT(route_evt);

static inline int do_trace(void* ctx, struct sk_buff* skb)
{
    // Built event for userland
    struct route_evt_t evt = {};
    bpf_get_current_comm(evt.comm, TASK_COMM_LEN);

    // Send event to userland
    route_evt.perf_submit(ctx, &evt, sizeof(evt));

    return 0;
}

/**
  * Attach to Kernel Tracepoints
  */
TRACEPOINT_PROBE(net, netif_rx) {
    return do_trace(args, (struct sk_buff*)args->skbaddr);
}

TRACEPOINT_PROBE(net, net_dev_queue) {
    return do_trace(args, (struct sk_buff*)args->skbaddr);
}

TRACEPOINT_PROBE(net, napi_gro_receive_entry) {
    return do_trace(args, (struct sk_buff*)args->skbaddr);
}

TRACEPOINT_PROBE(net, netif_receive_skb_entry) {
    return do_trace(args, (struct sk_buff*)args->skbaddr);
}
```

可以看到，我们的程序attach到4个tracepoint，并会访问`skbaddr`字段，将其传给处理
逻辑函数，这个函数现在只是将程序名字发送出来。你可能会有疑问，`args->skbaddr`是哪
里来的？答案是，每次用`TRACEPONT_PROBE`定义一个tracepoint，`bcc`就会为其自动生成`args`参数，由
于它是动态生成的，因此要查看它的定义不太容易。

不过，有另外一种简单的方式可以查看。在Linux上面，每个tracepoint都对应一个
`/sys/kernel/debug/tracing/events`条目。例如，查看`net:netif_rx`：

```shell
$ cat /sys/kernel/debug/tracing/events/net/netif_rx/format
name: netif_rx
ID: 1183
format:
	field:unsigned short common_type;         offset:0; size:2; signed:0;
	field:unsigned char common_flags;         offset:2; size:1; signed:0;
	field:unsigned char common_preempt_count; offset:3; size:1; signed:0;
	field:int common_pid;                     offset:4; size:4; signed:1;

	field:void * skbaddr;         offset:8;  size:8; signed:0;
	field:unsigned int len;       offset:16; size:4; signed:0;
	field:__data_loc char[] name; offset:20; size:4; signed:1;

print fmt: "dev=%s skbaddr=%p len=%u", __get_str(name), REC->skbaddr, REC->len
```

注意最后一行`print fmt`，这正是`perf trace`打印相应消息的格式。

在底层插入这样的探测点之后，我们再写个Python脚本，接收内核发出来的消息，每个eBP
发出的数据都打印一行：

```python
#!/usr/bin/env python
# coding: utf-8

from socket import inet_ntop
from bcc import BPF
import ctypes as ct

bpf_text = '''<SEE CODE SNIPPET ABOVE>'''

TASK_COMM_LEN = 16 # linux/sched.h

class RouteEvt(ct.Structure):
    _fields_ = [
        ("comm",    ct.c_char * TASK_COMM_LEN),
    ]

def event_printer(cpu, data, size):
    # Decode event
    event = ct.cast(data, ct.POINTER(RouteEvt)).contents

    # Print event
    print "Just got a packet from %s" % (event.comm)

if __name__ == "__main__":
    b = BPF(text=bpf_text)
    b["route_evt"].open_perf_buffer(event_printer)

    while True:
        b.kprobe_poll()
```

现在可以测试了，注意需要root权限。

**注意：现在的代码没有对包做任何过滤，因此即便你的机器网络流量很小，输出也很可能刷屏。**

```shell
$> sudo python ./tracepkt.py
...
Just got a packet from ping6
Just got a packet from ping6
Just got a packet from ping
Just got a packet from irq/46-iwlwifi
...
```

上面的输出显示，我正在使用ping和ping6，另外WiFi驱动也收到了一些包。

## 4 自定义跟踪器：改进

接下来让我们添加一些有用的数据/过滤条件。

### 4.1 添加网卡信息

首先，可以安全地删除前面代码中的comm字段，它在这里没什么用处。然后，include
`net/inet_sock.h`头文件，这里有我们所需要的函数声明。最后给event结构体添加`char
ifname[IFNAMSIZ]`字段。

现在我们可以从device结构体中访问device name字段。这里开始展示出**代码的强大之处
：我们可以访问任何受控范围内的字段。**

```c
// Get device pointer, we'll need it to get the name and network namespace
struct net_device *dev;
bpf_probe_read(&dev, sizeof(skb->dev), ((char*)skb) + offsetof(typeof(*skb), dev));

// Load interface name
bpf_probe_read(&evt.ifname, IFNAMSIZ, dev->name);
```

现在你可以测试一下，这样是能工作的。注意相应地修改一下Python部分。那么，它是怎么
工作的呢？

我们引入了`net_device`结构体来访问**网卡名字**字段。第一个`bpf_probe_read`从内核
的网络包中将网卡名字拷贝到`dev`，第二个将其接力复制到`evt.ifname`。

不要忘了，eBPF的目标是允许安全地编写在内核运行的脚本。这意味着，随机内存访问是绝
对不允许的。所有的内存访问都要经过验证。除非你要访问的内存在协议栈，否则你需要通
过`bpf_probe_read`读取数据。这会使得代码看起来很繁琐，但非常安全。`bpf_probe_read`
像是`memcpy`的一个更安全的版本，它定义在内核源文件
[bpf_trace.c](http://elixir.free-electrons.com/linux/v4.10.17/source/kernel/trace/bpf_trace.c#L64)
中:

1. 它和memcpy类似，因此注意内存拷贝的代价
2. 如果遇到错误，它会返回一个错误和一个初始化为0的缓冲区，而不会造成程序崩溃或停
   止运行

接下来为使代码看起来更加简洁，我将使用如下宏：

```c
#define member_read(destination, source_struct, source_member)                 \
  do{                                                                          \
    bpf_probe_read(                                                            \
      destination,                                                             \
      sizeof(source_struct->source_member),                                    \
      ((char*)source_struct) + offsetof(typeof(*source_struct), source_member) \
    );                                                                         \
  } while(0)
```

这样上面的例子就可以写成：

```c
member_read(&dev, skb, dev);
```

### 4.2 添加网络命名空间ID

采集网络命名空间信息非常有用，但是实现起来要复杂一些。原理上可以从两个地方访问：

1. socket结构体`sk`
1. device结构体`dev`

当我在写
[`solisten.py`](https://github.com/iovisor/bcc/blob/master/tools/solisten.py)时
，我使用的时socket结构体。不幸的是，不知道为什么，网络命名空间ID在跨命名空间的地
方消失了。这个字段全是0，很明显是有非法内存访问时的返回值（回忆前面介绍的
`bpf_probe_read`如何处理错误）。

幸好，device结构体工作正常。想象一下，我们可以问一个`packet`它在哪个`网卡`，进而
问这个网卡它在哪个`网络命名空间`。

```c
struct net* net;

// Get netns id. Equivalent to: evt.netns = dev->nd_net.net->ns.inum
possible_net_t *skc_net = &dev->nd_net;
member_read(&net, skc_net, net);
struct ns_common* ns = member_address(net, ns);
member_read(&evt.netns, ns, inum);
```

其中的宏定义如下：

```c
#define member_address(source_struct, source_member) \
({                                                   \
  void* __ret;                                       \
  __ret = (void*) (((char*)source_struct) + offsetof(typeof(*source_struct), source_member)); \
  __ret;                                             \
})
```

这个宏还可以用于简化`member_read`，这个就留给读者作为练习了。

好了，有了以上实现，我们再运行的效果就是：

```shell
$> sudo python ./tracepkt.py
[  4026531957]          docker0
[  4026531957]      vetha373ab6
[  4026532258]             eth0
[  4026532258]             eth0
[  4026531957]      vetha373ab6
[  4026531957]          docker0
```

如果ping一个容器，你看到的就是类似上面的输出。packet首先经过本地的docker0网桥，
然后经veth pair跨过网络命名空间，最后到达容器的eth0网卡。应答包沿着相反的路径回
到宿主机。

至此，功能是实现了，不过还太粗糙，继续改进。

### 4.3 只跟踪ICMP echo request/reply包

这次我们将读取包的IP信息，这里我只展示IPv4的例子，IPv6的与此类似。

不过，事情也并没有那么简单。我们是在和kernel的网络部分打交道。一些包可能还没被打
开，这意味着，变量的很多字段是没有初始化的。我们只能从MAC头开始，用offset的方式
计算IP头和ICMP头的位置。

首先从MAC头地址推导IP头地址。这里我们不(从`skb`的相应字段)加载MAC头长度信息，就认为
它是固定的14字节。


```c
// Compute MAC header address
char* head;
u16 mac_header;

member_read(&head,       skb, head);
member_read(&mac_header, skb, mac_header);

// Compute IP Header address
#define MAC_HEADER_SIZE 14;
char* ip_header_address = head + mac_header + MAC_HEADER_SIZE;
```

这表示我们假设IP头开始的地方在：`skb->head + skb->mac_header + MAC_HEADER_SIZE`
。
现在，我们可以解析IP头第一个字节的前4个bit：

```c
// Load IP protocol version
u8 ip_version;
bpf_probe_read(&ip_version, sizeof(u8), ip_header_address);
ip_version = ip_version >> 4 & 0xf;

// Filter IPv4 packets
if (ip_version != 4) {
    return 0;
}
```

然后加载整个IP头，获取IP地址，以使得Python程序的输出看起来更有意义。另外注意，IP
包内的下一个头就是ICMP头。

```c
// Load IP Header
struct iphdr iphdr;
bpf_probe_read(&iphdr, sizeof(iphdr), ip_header_address);

// Load protocol and address
u8 icmp_offset_from_ip_header = iphdr.ihl * 4;
evt.saddr[0] = iphdr.saddr;
evt.daddr[0] = iphdr.daddr;

// Filter ICMP packets
if (iphdr.protocol != IPPROTO_ICMP) {
    return 0;
}
```

最后，我们加载ICMP头，如果是ICMP echo request或reply，就读取序列号：

```c
// Compute ICMP header address and load ICMP header
char* icmp_header_address = ip_header_address + icmp_offset_from_ip_header;
struct icmphdr icmphdr;
bpf_probe_read(&icmphdr, sizeof(icmphdr), icmp_header_address);

// Filter ICMP echo request and echo reply
if (icmphdr.type != ICMP_ECHO && icmphdr.type != ICMP_ECHOREPLY) {
    return 0;
}

// Get ICMP info
evt.icmptype = icmphdr.type;
evt.icmpid   = icmphdr.un.echo.id;
evt.icmpseq  = icmphdr.un.echo.sequence;

// Fix endian
evt.icmpid  = be16_to_cpu(evt.icmpid);
evt.icmpseq = be16_to_cpu(evt.icmpseq);
```

这就是全部工作了。

如果你想过滤特定的ping进程的包，你可以认为`evt.icmpid`就是相应ping进程的进程号，
至少Linux上如此。

## 5 最终效果

再写一些比较简单的Python程序配合，我们就可以测试我们的跟踪器在多种场景下的用途。
以root权限启动这个程序，在不同终端发起几个ping进程，就会看到：

```shell
# ping -4 localhost
[  4026531957]               lo request #20212.001 127.0.0.1 -> 127.0.0.1
[  4026531957]               lo request #20212.001 127.0.0.1 -> 127.0.0.1
[  4026531957]               lo   reply #20212.001 127.0.0.1 -> 127.0.0.1
[  4026531957]               lo   reply #20212.001 127.0.0.1 -> 127.0.0.1
```

这个ICMP请求是进程20212（Linux ping的ICMP ID）在loopback网卡发出的，最后的
reply原路回到了这个loopback。这个环回接口既是发送网卡又是接收网卡。

如果是我的WiFi网关会是什么样子内？

```shell
# ping -4 192.168.43.1
[  4026531957]           wlp2s0 request #20710.001 192.168.43.191 -> 192.168.43.1
[  4026531957]           wlp2s0   reply #20710.001 192.168.43.1 -> 192.168.43.191
```

可以看到，这种情况下走的是WiFi网卡，也没问题。

另外，让我们的话题稍微偏一下，还记得刚开始我们只打印程序名字的版本吗？在
上面这种情况下，ICMP请求的程序名字会是ping，而应答包的程序的名字会是WiFi驱动，因
为是驱动发的应答包，至少Linux上是如此。

最后还是拿我最喜欢的例子：ping容器。之所以最喜欢并不是因为Docker，而是它展示了
eBPF的强大，**就像给ping过程做了一次X射线检查**。

```shell
# ping -4 172.17.0.2
[  4026531957]          docker0 request #17146.001 172.17.0.1 -> 172.17.0.2
[  4026531957]      vetha373ab6 request #17146.001 172.17.0.1 -> 172.17.0.2
[  4026532258]             eth0 request #17146.001 172.17.0.1 -> 172.17.0.2
[  4026532258]             eth0   reply #17146.001 172.17.0.2 -> 172.17.0.1
[  4026531957]      vetha373ab6   reply #17146.001 172.17.0.2 -> 172.17.0.1
[  4026531957]          docker0   reply #17146.001 172.17.0.2 -> 172.17.0.1
```

来点 ASCII 艺术，就变成：

```shell
       Host netns           | Container netns
+---------------------------+-----------------+
| docker0 ---> veth0e65931 ---> eth0          |
+---------------------------+-----------------+
```

## 6 结束语

在eBPF/bcc出现之前，要深入的排查和追踪很多网络问题，只能靠给内核打补丁。现在，我
们可以比较方便地用eBPF/bcc编写一些工具来完成这些事情。跟踪点(tracepoint)也很方便
，它们提示了我们可以在哪些地方进行探测，避免了去看繁杂的内核代码。kprobe无法探测
的一些地方，例如一些内联函数和静态函数，eBPF/bcc也可以探测。

本文的例子要添加对IPv6的支持也非常简单，我就留给读者作为练习。

如果要使本文更加完善的话，我需要对我们的程序做性能测试。但考虑到文章本身已经非常
长，这里就不做了。

对我们的代码进行改进，用在跟踪路由和iptables判决，或是ARP包，也是很有意思的。
这将会把它变成一个完美的X射线跟踪器，对像我这样需要经常处理复杂网络问题的
人来说将非常有用。

完整的（包含IPv6支持）代码可以访问：
[https://github.com/yadutaf/tracepkt](https://github.com/yadutaf/tracepkt)。

最后，我要感谢 [@fcabestre](https://twitter.com/fcabestre)帮我将这篇文章的草稿从
一个异常的硬盘上恢复出来，感谢[@bluxte](https://twitter.com/bluxte)的耐心审读，
以及技术上使得本文成为可能的[bcc](https://github.com/iovisor/bcc)团队。
