---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 1"
date:   2018-12-17
author: ArthurChiao
categories: network-stack monitoring tuning
---

### 译者序

本文翻译自2017年的一篇英文博客 [Monitoring and Tuning the Linux Networking Stack: Sending Data](https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data)。**如果能看懂英文，我建议你阅读原文，或者和本文对照看。**

这篇文章写的是**“Linux networking stack"**，这里的”stack“并不仅仅是内核协议栈，
而是包括内核协议栈在内的，从应用程序通过系统调用**写数据到socket**，到数据被组织
成一个或多个数据包最终被物理网卡发出去的整个路径。所以文章有三方面，交织在一起，
看起来非常累（但是很过瘾）：

1. 原理及代码实现：网络各层，包括驱动、硬中断、软中断、内核协议栈、socket等等
2. 监控：对代码中的重要计数进行监控，一般在`/proc`或`/sys`下面有对应输出
3. 调优：修改网络配置参数

本文的另一个特色是，几乎所有讨论的内核代码，都在相应的地方给出了github上的链接，
具体到行。

网络栈非常复杂，原文太长又没有任何章节号，所以看起来非常累。因此在翻译的时候，我
将其分为了若干篇，并添加了相应的章节号，以期按图索骥。

以下是翻译。


----

### 写给不想读长文的人（TL; DR）

本文介绍了运行Linux内核的机器是如何**发包**（send packets）的，包是怎样从用户程
序一步步到达硬件网卡并被发出去的，以及如何**监控**（monitoring）和**调优**（
tuning）这一路径上的各个网络栈组件。

本文的姊妹篇是 [Linux网络栈监控和调优：接收数据]({% link _posts/2018-12-05-tuning-stack-rx-zh-1.md %})，
对应的原文是 [Monitoring and Tuning the Linux Networking Stack: Receiving
Data](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/)
。

想对Linux网络栈进行监控或调优，必须对其正在发生什么有一个深入的理解，
而这离不开读内核源码。希望本文可以给那些正准备投身于此的人提供一份参考。

## 1 监控和调优网络栈：常规建议

正如我们前一篇文章提到的，网络栈很复杂，没有一种方式适用于所有场景。如果性能和网络
健康状态对你或你的业务非常重要，那你没有别的选择，只能花大量的时间、精力和金钱去
深入理解系统的各个部分之间是如何交互的。

本文中的一些示例配置仅为了方便理解（效果），并不作为任何特定配置或默认配置的建议
。在做任何配置改动之前，你应该有一个能够对系统进行监控的框架，以查看变更是否带来
预期的效果。

对远程连接上的机器进行网络变更是相当危险的，机器很可能失联。另外，不要在生产环境
直接调整这些配置；如果可能的话，在新机器上改配置，然后将机器灰度上线到生产。

## 2 发包过程俯瞰

本文将拿**Intel I350**网卡的`igb`驱动作为参考，网卡的data sheet这里可以下载
[PDF](http://www.intel.com/content/dam/www/public/us/en/documents/datasheets/ethernet-controller-i350-datasheet.pdf)
（警告：文件很大）。

从比较高的层次看，一个数据包从用户程序到达硬件网卡的整个过程如下：

1. 使用**系统调用**（如`sendto`，`sendmsg`等）写数据
1. 数据穿过**socket子系统**，进入**socket协议族**（protocol family）系统（在我们的例子中为`AF_INET`）
1. 协议族处理：数据穿过**协议层**，这一过程（在许多情况下）会将**数据**（data）转换成**数据包**（packet）
1. 数据穿过**路由层**，这会涉及路由缓存和ARP缓存的更新；如果目的MAC不在ARP缓存表中，将触发一次ARP广播来查找MAC地址
1. 穿过协议层，packet到达**设备无关层**（device agnostic layer）
1. 使用XPS（如果启用）或散列函数**选择发送队列**
1. 调用网卡驱动的**发送函数**
1. 数据传送到网卡的`qdisc`（queue discipline，排队规则）
1. qdisc会直接**发送数据**（如果可以），或者将其放到队列，下次触发**`NET_TX`类型软中断**（softirq）的时候再发送
1. 数据从qdisc传送给驱动程序
1. 驱动程序创建所需的**DMA映射**，以便网卡从RAM读取数据
1. 驱动向网卡发送信号，通知**数据可以发送了**
1. **网卡从RAM中获取数据并发送**
1. 发送完成后，设备触发一个**硬中断**（IRQ），表示发送完成
1. **硬中断处理函数**被唤醒执行。对许多设备来说，这会**触发`NET_RX`类型的软中断**，然后NAPI poll循环开始收包
1. poll函数会调用驱动程序的相应函数，**解除DMA映射**，释放数据

接下来会详细介绍整个过程。
