---
layout: post
title:  "[译] Linux网络栈监控和调优：接收数据 5"
date:   2018-12-05
author: ArthurChiao
categories: network-stack monitoring tuning
---

## 12 其他

还有一些值得讨论的地方，放在前面哪里都不太合适，故统一放到这里。

### 12.1 打时间戳 (timestamping)

前面提到，网络栈可以收集包的时间戳信息。如果使用了RPS功能，有相应的`sysctl`参数
可以控制何时以及如何收集时间戳；更多关于RPS、时间戳，以及网络栈在哪里完成这些工
作的内容，请查看前面的章节。一些网卡甚至支持在硬件上打时间戳。

如果你想看内核网络栈给收包增加了多少延迟，那这个特性非常有用。

内核[关于时间戳的文档](https://github.com/torvalds/linux/blob/v3.13/Documentation/networking/timestamping.txt)
非常优秀，甚至还包括一个[示例程序和相应的Makefile](https://github.com/torvalds/linux/tree/v3.13/Documentation/networking/timestamping)，有兴趣的话可以上手试试。

使用`ethtool -T`可以查看网卡和驱动支持哪种打时间戳方式：

```shell
$ sudo ethtool -T eth0
Time stamping parameters for eth0:
Capabilities:
  software-transmit     (SOF_TIMESTAMPING_TX_SOFTWARE)
  software-receive      (SOF_TIMESTAMPING_RX_SOFTWARE)
  software-system-clock (SOF_TIMESTAMPING_SOFTWARE)
PTP Hardware Clock: none
Hardware Transmit Timestamp Modes: none
Hardware Receive Filter Modes: none
```

从上面这个信息看，该网卡不支持硬件打时间戳。但这个系统上的软件打时间戳，仍然可以
帮助我判断内核在接收路径上到底带来多少延迟。

### 12.2 socket低延迟选项：busy polling

socket有个`SO_BUSY_POLL`选项，可以让内核在**阻塞式接收**（blocking receive）
的时候做busy poll。这个选项会减少延迟，但会增加CPU使用量和耗电量。

**重要提示**：要使用此功能，首先要检查你的设备驱动是否支持。Linux内核3.13.0的
`igb`驱动不支持，但`ixgbe`驱动支持。如果你的驱动实现(并注册)了`struct
net_device_ops`(前面介绍过了)的`ndo_busy_poll`方法，那它就是支持`SO_BUSY_POLL`。

Intel有一篇非常好的[文章](http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/open-source-kernel-enhancements-paper.pdf)介绍其原理。

对单个socket设置此选项，需要传一个以微秒（microsecond）为单位的时间，内核会
在这个时间内对设备驱动的接收队列做busy poll。当在这个socket上触发一个阻塞式读请
求时，内核会busy poll来收数据。

全局设置此选项，可以修改`net.core.busy_poll`配置（毫秒，microsecond），当`poll`或`select`方
法以阻塞方式调用时，busy poll的时长就是这个值。

### 12.3 Netpoll：特殊网络场景支持

Linux内核提供了一种方式，在内核挂掉（crash）的时候，设备驱动仍然可以接收和发送数
据，相应的API被称作`Netpoll`。这个功能在一些特殊的网络场景有用途，比如最著名的两个例子：
[`kgdb`](http://sysprogs.com/VisualKernel/kgdboe/launch/)和
[`netconsole`](https://github.com/torvalds/linux/blob/v3.13/Documentation/networking/netconsole.txt)。

大部分驱动都支持`Netpoll`功能。支持此功能的驱动需要实现`struct net_device_ops`的
`ndo_poll_controller`方法（回调函数，探测驱动模块的时候注册的，前面介绍过）。

当网络设备子系统收包或发包的时候，会首先检查这个包的目的端是不是`netpoll`。

例如，我们来看下`__netif_receive_skb_core`，[net/dev/core.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3511-L3514):

```c
static int __netif_receive_skb_core(struct sk_buff *skb, bool pfmemalloc)
{

  /* ... */

  /* if we've gotten here through NAPI, check netpoll */
  if (netpoll_receive_skb(skb))
    goto out;

  /* ... */
}
```

设备驱动收发包相关代码里，关于`netpoll`的判断逻辑在很前面。

Netpoll API的消费者可以通过`netpoll_setup`函数注册`struct netpoll`实例，后者有收
包和发包的hook方法（函数指针）。

如果你对使用Netpoll API感兴趣，可以看看
[netconsole](https://github.com/torvalds/linux/blob/v3.13/drivers/net/netconsole.c)
的[驱动](https://github.com/torvalds/linux/blob/v3.13/drivers/net/netconsole.c)
，Netpoll API的头文件
[`include/linux/netpoll.h`](https://github.com/torvalds/linux/blob/v3.13/include/linux/netpoll.h)
，以及[这个](http://people.redhat.com/~jmoyer/netpoll-linux_kongress-2005.pdf)精
彩的分享。

### 12.4 `SO_INCOMING_CPU`

`SO_INCOMING_CPU`直到Linux 3.19才添加, 但它非常有用，所以这里讨论一下。

使用`getsockopt`带`SO_INCOMING_CPU`选项，可以判断当前哪个CPU在处理这个socket的网
络包。你的应用程序可以据此将socket交给在期望的CPU上运行的线程，增加数据本地性（
data locality）和CPU缓存命中率。

在提出`SO_INCOMING_CPU`的[邮件列表](https://patchwork.ozlabs.org/patch/408257/)
里有一个简单示例框架，展示在什么场景下使用这个功能。

### 12.5 DMA引擎

DMA engine (直接内存访问引擎)是一个硬件，允许CPU将**很大的复制操作**（large copy
operations）offload（下放）给它。这样CPU就从数据拷贝中解放出来，去做其他工作，而
拷贝就交由硬件完成。合理的使用DMA引擎（代码要利用到DMA特性）可以减少CPU的使用量
。

Linux内核有一个通用的DMA引擎接口，DMA engine驱动实现这个接口即可。更多关于这个接
口的信息可以查看内核源码的[文档
](https://github.com/torvalds/linux/blob/v3.13/Documentation/dmaengine.txt)。

内核支持的DMA引擎很多，这里我们拿Intel的[IOAT DMA
engine](https://en.wikipedia.org/wiki/I/O_Acceleration_Technology)为例来看一下。

#### Intel’s I/O Acceleration Technology (IOAT)

很多服务器都安装了[Intel I/O AT
bundle](http://www.intel.com/content/www/us/en/wireless-network/accel-technology.html)
，其中包含了一系列性能优化相关的东西，包括一个硬件DMA引擎。可以查看`dmesg`里面有
没有`ioatdma`，判断这个模块是否被加载，以及它是否找到了支持的硬件。

DMA引擎在很多地方有用到，例如TCP协议栈。

Intel IOAT DMA engine最早出现在Linux 2.6.18，但随后3.13.11.10就禁用掉了，因为有一些bug，会导致数据损坏。

`3.13.11.10`版本之前的内核默认是开启的，将来这些版本的内核如果有更新，可能也会禁用掉。

##### 直接缓存访问 (DCA, Direct cache access)

[Intel I/O AT bundle](http://www.intel.com/content/www/us/en/wireless-network/accel-technology.html)
中的另一个有趣特性是直接缓存访问（DCA）。

该特性允许网络设备（通过各自的驱动）直接将网络数据放到CPU缓存上。至于是如何实现
的，随各家驱动而异。对于`igb`的驱动，你可以查看`igb_update_dca`和
`igb_update_rx_dca`这两个函数的实现。`igb`驱动使用DCA，直接写硬件网卡的一个
寄存器。

要使用DCA功能，首先检查你的BIOS里是否打开了此功能，然后确保`dca`模块加载了，
还要确保你的网卡和驱动支持DCA。

##### Monitoring IOAT DMA engine

如上所说，如果你不怕数据损坏的风险，那你可以使用`ioatdma`模块。监控上，可以看几
个sysfs参数。

例如，监控一个DMA通道（channel）总共offload的`memcpy`操作次数：

```shell
$ cat /sys/class/dma/dma0chan0/memcpy_count
123205655
```

类似的，一个DMA通道总共offload的字节数：

```shell
$ cat /sys/class/dma/dma0chan0/bytes_transferred
131791916307
```

##### Tuning IOAT DMA engine

IOAT DMA engine只有在包大小超过一定的阈值之后才会使用，这个阈值叫`copybreak`。
之所以要设置阈值是因为，对于小包，设置和使用DMA的开销要大于其收益。

调整DMA engine `copybreak`参数：

```shell
$ sudo sysctl -w net.ipv4.tcp_dma_copybreak=2048
```

默认值是4096。

## 13 总结

Linux网络栈很复杂。

对于这样复杂的系统（以及类似的其他系统），
如果不能在更深的层次理解它正在做什么，就不可能做监控和调优。
当遇到网络问题时，你可能会在网上搜到一些`sysctl.conf`最优实践一类的东西，然后应
用在自己的系统上，但这并不是网络栈调优的最佳方式。

监控网络栈需要从驱动开始，逐步往上，仔细地在每一层统计网络数据。
这样你才能清楚地看出哪里有丢包（drop），哪里有收包错误（errors），然后根据导致错
误的原因做相应的配置调整。

**不幸的是，这项工作并没有捷径。**

## 14 额外讨论和帮助

需要一些额外的关于网络栈的指导(navigating the network stack)？对本文有疑问，或有
相关内容本文没有提到？以上问题，都可以发邮件给[我们](support@packagecloud.io)，
以便我们知道如何提供帮助。

## 15 相关文章

如果你喜欢本文，你可能对下面这些底层技术文章也感兴趣：

* [Monitoring and Tuning the Linux Networking Stack: Sending Data](https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data/)
* [The Definitive Guide to Linux System Calls](https://blog.packagecloud.io/eng/2016/04/05/the-definitive-guide-to-linux-system-calls/)
* [How does strace work?](https://blog.packagecloud.io/eng/2016/02/29/how-does-strace-work/)
* [How does ltrace work?](https://blog.packagecloud.io/eng/2016/03/14/how-does-ltrace-work/)
* [APT Hash sum mismatch](https://blog.packagecloud.io/eng/2016/03/21/apt-hash-sum-mismatch/)
* [HOWTO: GPG sign and verify deb packages and APT repositories](https://blog.packagecloud.io/eng/2014/10/28/howto-gpg-sign-verify-deb-packages-apt-repositories/)
* [HOWTO: GPG sign and verify RPM packages and yum repositories](https://blog.packagecloud.io/eng/2014/11/24/howto-gpg-sign-verify-rpm-packages-yum-repositories/)
