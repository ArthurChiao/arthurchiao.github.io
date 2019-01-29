---
layout: post
title:  "[译] 网络包内核路径跟踪：1 开篇"
date:   2018-11-30
author: ArthurChiao
categories: eBPF perf tracepoint
---

译者按：本文翻译自一篇英文博客[Tracing a packet's journey using Linux tracepoints, perf and eBPF](https://blog.yadutaf.fr/2017/07/28/tracing-a-packet-journey-using-linux-tracepoints-perf-ebpf/)。由于原文篇幅较长，我将其分成了三篇，并添加了适当的标题。
本文不会100%逐词逐句翻译，那样的翻译太过生硬，看看《TCP/IP详解》中文版就知道了。
例如，有多少人会在讨论网络问题的时候说**"插口"**而不是**"socket"**？在技术领域，过
度翻译反而会带来交流障碍。**如果能看懂英文，我建议你阅读原文，或者和本文对照看。**

----

一段时间以来，我一直在寻找Linux上的底层网络调试（debug）工具。Linux允许
在宿主机上使用虚拟网卡（virtual interface）和网络命名空间（network namespace
）构建复杂的网络。但出现故障时，排障（troubleshooting）相当痛苦。如果是3层路由问
题，`mtr`可以排上用场。但如果是更底层的问题，我通常只能手动检查每个网
卡/网桥/网络命名空间/iptables规则，用tcpdump抓一些包，以确定正在发生什么。如果
不了解故障之前的网络设置，那感觉就像在走迷宫。

## 1 逃离迷宫：上帝视角

逃离迷宫的一种方式是在**迷宫内**不断左右尝试（exploring），试图找到通往出口的路
。当你在玩迷宫游戏（置身迷宫内）时，你只能如此。不过，如果不是在游戏内，那还有另
一种方式：**转换视角，从上面俯视**。

使用Linux术语，就是转换到**内核视角**（the
kernel point of view），在这种视角下，网络命名空间不再是容器（"containers"），而只
是一些标签（labels）。内核、数据包、网卡等此时都是“肉眼可见”的对象（objects）。

**原文注**：上面的"containers"我加了引号，因为从技术上说，网络命名空间是构成Linux容器的核心部件之一。

## 2 网络跟踪：渴求利器

所以我想要的是这样一个工具，它可以直接告诉我 “嗨，我看到你的包了：它从**属于这个网络命
名空间**的**这个网卡**上发出来，然后**依次经过这些函数**”。

本质上，我想要的是一个2层的`mtr`。

这样的工具存在吗？我们来造一个！

在本文结束的时候，我们将拥有一个简单、易于使用的底层**网络包跟踪器**（packet tracker
）。如果你ping本机一个Docker容器，它会显示类似如下信息：

```shell
# ping -4 172.17.0.2
[  4026531957]          docker0 request #17146.001 172.17.0.1 -> 172.17.0.2
[  4026531957]      vetha373ab6 request #17146.001 172.17.0.1 -> 172.17.0.2
[  4026532258]             eth0 request #17146.001 172.17.0.1 -> 172.17.0.2
[  4026532258]             eth0   reply #17146.001 172.17.0.2 -> 172.17.0.1
[  4026531957]      vetha373ab6   reply #17146.001 172.17.0.2 -> 172.17.0.1
[  4026531957]          docker0   reply #17146.001 172.17.0.2 -> 172.17.0.1
```

## 3 巨人肩膀：perf/eBPF

在本文中，我将聚焦两个跟踪工具：`perf` 和 `eBPF`。

`perf`是Linux上的最重要的性能分析工具之一。它和内核出自同一个源码树（source tree）
，但编译需要针对指定的内核版本。perf可以跟踪内核，也可以跟踪用户程序，
还可用于采样或者设置跟踪点。可以把它想象成开销更低，但功能更强大的`strace`。本
文只会使用非常简单的perf命令。如果想了解更多，强烈建议访问[Brendan
Gregg](http://www.brendangregg.com/perf.html)的博客。

`eBPF`是Linux内核新近加入的，其中e是`extended`的缩写。从名字可以看出，它是BPF（
Berkeley Packet Filter）字节码过滤器的增强版，后者是BSD家族的网络包过滤工具。在
Linux上，eBPF可以用来在正在运行的内核（live kernel）中安全地执行任何平台无关（
platform
independent）代码，只要这些代码满足一些安全前提。例如，在程序执行之前必须验证内
存访问合法性，而且要能证明程序会在有限时间内退出。如果内核证明不了这些条件，那即
使eBPF代码是安全的并且确定会退出，它仍然会被拒绝。

此类程序可用于QOS网络分类器（network classifier）、XDP（eXpress Data Plane）很底
层的网络功能和过滤功能组件、跟踪代理（tracing agent），以及其他很多方面。

任何在`/proc/kallsyms`导出的符号（译者注，内核函数）或者跟踪点（tracepoints），
都可以插入eBPF探测点（tracing probes）。在本文中，我将主要关注attach到
tracepoints的跟踪代理（tracing agents attached to tracepoints）。

如果想看一些在内核函数埋点进行跟踪的例子，或者需要入门级介绍，建议阅读我之前的eBPF文章
[英文](https://blog.yadutaf.fr/2016/03/30/turn-any-syscall-into-event-introducing-ebpf-kernel-probes/)，
[中文翻译](/blog/ebpf-turn-syscall-to-event-zh)。
