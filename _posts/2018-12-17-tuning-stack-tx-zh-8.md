---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 8"
date:   2018-12-17
author: ArthurChiao
categories: network-stack monitoring tuning
---

## 11 Extras

### 11.1 减少ARP流量 (MSG_CONFIRM)

`send`, `sendto`和`sendmsg`系统调用都支持一个`flags`参数。如果你调用的时候传递了
`MSG_CONFIRM` flag，它会使内核里的`dst_neigh_output`函数更新邻居（ARP）缓存的时
间戳。所导致的结果是，相应的邻居缓存不会被垃圾回收。这会减少发出的ARP请求的数量
。

### 11.2 UDP Corking（软木塞）

在查看UDP协议栈的时候我们深入地研究过了UDP corking这个选项。如果你想在应用中使用
这个选项，可以在调用`setsockopt`设置IPPROTO_UDP类型socket的时候，将UDP_CORK标记
位置1。

### 11.3 打时间戳

本文已经看到，网络栈可以收集发送包的时间戳信息。我们在文章中已经看到了软
件部分哪里可以设置时间戳；而一些网卡甚至还支持硬件时间戳。

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

## 12 结论

Linux网络栈很复杂。

我们已经看到，即使是`NET_RX`这样看起来极其简单的（名字），也不是按照我们（字面上
）理解的方式在运行，虽然名字带RX，但其实发送数据也在`NET_RX`软中断处理函数中被处
理。

这揭示了我认为的问题的核心：**不深入阅读和理解网络栈，就不可能优化和监控它**。
**你监控不了你没有深入理解的代码**。

## 13 额外帮助

需要一些额外的关于网络栈的指导(navigating the network stack)？对本文有疑问，或有
相关内容本文没有提到？以上问题，都可以发邮件给[我们](support@packagecloud.io)，
以便我们知道如何提供帮助。
