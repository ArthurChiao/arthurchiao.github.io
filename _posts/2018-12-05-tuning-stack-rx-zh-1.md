---
layout: post
title:  "[译] Linux网络栈监控和调优：接收数据 1"
date:   2018-12-05
author: ArthurChiao
categories: network-stack monitoring tuning
---

### 译者序

本文翻译自2016年的一篇英文博客 [Monitoring and Tuning the Linux Networking Stack: Receiving Data](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/)。**如果能看懂英文，我建议你阅读原文，或者和本文对照看。**

这篇文章写的是**“Linux networking stack"**，这里的”stack“并不仅仅是内核协议栈，而是包
括内核协议栈在内的、从数据包到达物理网卡到最终被用户态程序收起的整个路径。所以文
章有三方面，交织在一起，看起来非常累（但是很过瘾）：

1. 原理及代码实现：网络各层，包括驱动、硬中断、软中断、内核协议栈、socket等等
2. 监控：对代码中的重要计数进行监控，一般在`/proc`或`/sys`下面有对应输出
3. 调优：修改网络配置参数

本文的另一个特色是，几乎所有讨论的内核代码，都在相应的地方给出了github上的链接，
具体到行。

网络栈非常复杂，原文太长又没有任何章节号，所以看起来非常累。因此在翻译的时候，我
将其分为了若干篇，并添加了相应的章节号，以期按图索骥。

以下是翻译。


----

这篇文章的姊妹篇 [Monitoring and Tuning the Linux Networking Stack: Sending Data](https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data/)。

这篇文章的图文注释版 [the Illustrated Guide to Monitoring and Tuning the Linux Networking Stack: Receiving Data](https://blog.packagecloud.io/eng/2016/10/11/monitoring-tuning-linux-networking-stack-receiving-data-illustrated/)。

### 写给不想读长文的人（TL; DR）

本文章介绍了Linux内核是如何**收包**（receive packets）的，包是怎样从网络栈到
用户空间程序的，以及如何**监控**（monitoring）和**调优**（tuning）这一路径上的各个网络栈组件。

想对Linux网络栈进行监控或调优，必须对其正在发生什么有一个深入的理解，
而这离不开读内核源码。希望本文可以给那些正准备投身于此的人提供一份参考。

### 特别鸣谢

特别感谢[Private Internet Access](https://privateinternetaccess.com/)的各位同僚
。公司雇佣我们做包括本文主题在内的网络研究，并非常慷慨地允许我们将研究成果以文章
的形式发表。

这篇文章基于在[Private Internet Access](https://privateinternetaccess.com/)的研
究成果，最开始以[5篇连载
](https://www.privateinternetaccess.com/blog/2016/01/linux-networking-stack-from-the-ground-up-part-1/)
的形式出现。

## 1 监控和调优网络栈：常规建议

网络栈很复杂，没有一种方式适用于所有场景。如果性能和网络健康状态对你或你的业
务非常重要，那你没有别的选择，只能花大量的时间、精力和金钱去深入理解系统的各
个部分之间是如何交互的。

理想情况下，你应该考虑在网络栈的各层测量丢包状况，这样就可以缩小范围，确定那个组
件需要调优。

**然后，这也是一些网络管理员开始走偏的地方**：他们想当然的认为通过一翻`sysctl`配
置或查看`/proc`等方式可以解决问题，并适用于所有场景。在某些场景下，可能确实能解
决问题，但是，整个系统是如此细微地缠绕交织在一起，如果你想做有意义的监控和调优的
话，你必须得努力在更深层次搞清系统是如何工作的。否则，你虽然可以使用默认配置，并
在相当长的时间内表现良好，但终会到某个时间点，你不得不（投时间、精力和金钱研究这
些配置，然后）做优化。

本文中的一些示例配置仅为了方便理解（效果），并不作为任何特定配置或默认配置的建议
。在做任何配置改动之前，你应该有一个能够对系统进行监控的框架，以查看变更是否带来
预期的效果。

对远程连接上的机器进行网络变更是相当危险的，机器很可能失联。另外，不要在生产环境
直接调整这些配置；如果可能的话，在新机器上改配置，然后将机器灰度上线到生产。

## 2 收包过程俯瞰

本文将拿**Intel I350**网卡的`igb`驱动作为参考，网卡的data sheet这里可以下载[PDF](http://www.intel.com/content/dam/www/public/us/en/documents/datasheets/ethernet-controller-i350-datasheet.pdf)（警告：文件很大）。

从比较高的层次看，一个数据包从被网卡接收直到进入socket的接收队列的整个过程如下：

1. 加载网卡驱动，初始化
2. 包从外部网络进入网卡
3. 网卡（通过直接内存访问，DMA）将包copy到内核内存中的ring buffer
4. 产生硬件中断，通知系统收到了一个包
5. 驱动调用NAPI，如果轮询（poll）还没开始，就开始轮询
6. `ksoftirqd`进程调用NAPI的`poll`函数从ring buffer收包（`poll`函数是网卡驱动在初始化阶
   段注册的；每个CPU上都运行着一个`ksoftirqd`进程，在系统启动期间就注册了）
7. ring buffer里包对应的内存区域解除映射（unmapped）
8. 通过DMA进入内存的数据包以`skb`的形式被送至更上层处理
9. 如果packet steering功能打开，或者网卡有多队列，网卡收到的包会被分发到多个CPU
10. 包从队列进入协议层
11. 协议层处理包
12. 包从协议层进入相应socket的接收队列

接下来会详细介绍这个过程。

协议层分析我们将会关注IP和UDP层，其他协议层可参考这个过程。

## 3 网络设备驱动

本文基于Linux 3.13。

准确地理解Linux内核的收包过程非常有挑战性。我们需要仔细查看网卡驱动是如何工作的
，才能对网络栈的相应部分有更加清晰的理解。

本文将拿`ibg`驱动作为例子，它是常见的Intel I350网卡的驱动。我们先来理解网卡驱动是如何
工作的。


### 3.1 初始化

驱动会使用`module_init`向内核注册一个初始化函数，当驱动被加载时，内核会调用这个函数。

这个初始化函数(`igb_init_module`)的代码见 [`drivers/net/ethernet/intel/igb/igb_main.c`](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L676-L697).

过程非常简单直接：

```c
/**
 *  igb_init_module - Driver Registration Routine
 *
 *  igb_init_module is the first routine called when the driver is
 *  loaded. All it does is register with the PCI subsystem.
 **/
static int __init igb_init_module(void)
{
  int ret;
  pr_info("%s - version %s\n", igb_driver_string, igb_driver_version);
  pr_info("%s\n", igb_copyright);

  /* ... */

  ret = pci_register_driver(&igb_driver);
  return ret;
}

module_init(igb_init_module);
```

初始化的大部分工作在`pci_register_driver`里面完成，接下来细看。

#### PCI初始化

Intel I350 网卡是[PCI express](https://en.wikipedia.org/wiki/PCI_Express)设备。

PCI设备通过[PCI Configuration
Space](https://en.wikipedia.org/wiki/PCI_configuration_space#Standardized_registers)
里面的寄存器识别自己。

当设备驱动编译的时候，宏`MODULE_DEVICE_TABLE`(定义在
[`include/module.h`](https://github.com/torvalds/linux/blob/v3.13/include/linux/module.h#L145-L146))
会导出一个PCI设备ID表，驱动据此识别它可以控制的设备。内核也会依据这个设备表判断
对哪个设备加载哪个驱动。

`igb`驱动的设备表和PCI设备ID分别见：
[drivers/net/ethernet/intel/igb/igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L79-L117)
和[drivers/net/ethernet/intel/igb/e1000_hw.h](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/e1000_hw.h#L41-L75)。

```c
static DEFINE_PCI_DEVICE_TABLE(igb_pci_tbl) = {
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I354_BACKPLANE_1GBPS) },
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I354_SGMII) },
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I354_BACKPLANE_2_5GBPS) },
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I211_COPPER), board_82575 },
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_COPPER), board_82575 },
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_FIBER), board_82575 },
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_SERDES), board_82575 },
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_SGMII), board_82575 },
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_COPPER_FLASHLESS), board_82575 },
  { PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_SERDES_FLASHLESS), board_82575 },

  /* ... */
};
MODULE_DEVICE_TABLE(pci, igb_pci_tbl);
```

前面提到，驱动初始化的时候会调用`pci_register_driver`，这个函数会将该驱动的各
种回调方法注册到一个`struct pci_driver`实例，[drivers/net/ethernet/intel/igb/igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L238-L249)：

```c
static struct pci_driver igb_driver = {
  .name     = igb_driver_name,
  .id_table = igb_pci_tbl,
  .probe    = igb_probe,
  .remove   = igb_remove,

  /* ... */
};
```

### 3.2 网络设备初始化

通过PCI ID识别设备后，内核就会为它选择合适的驱动。每个PCI驱动注册了一个`probe()`
方法，内核会对每个设备依次调用驱动的`probe`方法，当找到一个合适的驱动
后，就不会再为这个设备尝试其他驱动。

很多驱动都需要大量代码来使得设备ready，具体做的事情各有差异。典型的过程：

1. 启用PCI设备
2. 请求（requesting）内存范围和IO端口
3. 设置DMA掩码
4. 注册设备驱动支持的ethtool方法（后面介绍）
5. 注册所需的watchdog（例如，e1000e有一个检测设备是否僵死的watchdog）
6. 其他和具体设备相关的事情，例如一些workaround，或者特定硬件的怪异处理
7. 创建、初始化和注册一个`struct net_device_ops`类型实例，包含了用于设备相关的回
   调函数，例如打开设备、发送数据到网络、设置MAC地址等
8. 创建、初始化和注册一个更高层的`struct net_device`类型实例，一个实例就代表了一
   个设备

我们来简单看下`igb`驱动的`igb_probe`包含哪些过程。下面的代码来自`igb_probe`，[drivers/net/ethernet/intel/igb/igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L2038-L2059)：

```c
err = pci_enable_device_mem(pdev);

/* ... */

err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

/* ... */

err = pci_request_selected_regions(pdev, pci_select_bars(pdev,
           IORESOURCE_MEM),
           igb_driver_name);

pci_enable_pcie_error_reporting(pdev);

pci_set_master(pdev);
pci_save_state(pdev);
```

#### 更多PCI驱动信息

详细的PCI驱动讨论不在本文范围，如果想进一步了解，如下材料很推荐：
[分享](http://free-electrons.com/doc/pci-drivers.pdf)，
[wiki](http://wiki.osdev.org/PCI)，
[Linux Kernel Documentation: PCI](https://github.com/torvalds/linux/blob/v3.13/Documentation/PCI/pci.txt)。

### 3.3 网络设备启动

`igb_probe`做了很多重要的设备初始化工作。除了PCI相关的，还有如下一些通用网络功能和网络设备相关的工作：

1. 注册`struct net_device_ops`实例
1. 注册ethtool相关的方法
1. 从网卡获取默认MAC地址
1. 设置`net_device`特性标记

我们逐一看一下这些过程，后面会用到。

#### 3.3.1 `struct net_device_ops`

网络设备相关的操作函数都注册到这个类型的实例中。[igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L2038-L2059)：

```c
static const struct net_device_ops igb_netdev_ops = {
  .ndo_open               = igb_open,
  .ndo_stop               = igb_close,
  .ndo_start_xmit         = igb_xmit_frame,
  .ndo_get_stats64        = igb_get_stats64,
  .ndo_set_rx_mode        = igb_set_rx_mode,
  .ndo_set_mac_address    = igb_set_mac,
  .ndo_change_mtu         = igb_change_mtu,
  .ndo_do_ioctl           = igb_ioctl,

  /* ... */
```

这个实例会在`igb_probe()`中赋给`struct net_device`中的`netdev_ops`字段：

```c
static int igb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
  ...

  netdev->netdev_ops = &igb_netdev_ops;
}
```

#### 3.3.2 ethtool函数注册

[`ethtool`](https://www.kernel.org/pub/software/network/ethtool/)是一个命令行工
具，可以查看和修改网络设备的一些配置，常用于收集网卡统计数据。在Ubuntu上，可以通
过`apt-get install ethtool`安装。

ethtool通过
[ioctl](http://man7.org/linux/man-pages/man2/ioctl.2.html)
和设备驱动通信。内核实现了一个通用ethtool接口，网卡驱动实现这些接口，就可以被
ethtool调用。当ethtool发起一个系统调用之后，内核会找到对应操作的回
调函数。回调实现了各种简单和复杂的函数，简单的如改变一个flag值，复杂的包括调整网
卡硬件如何运行。

相关实现见：[igb_ethtool.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_ethtool.c)。

#### 3.3.3 软中断（IRQ）

当一个数据帧通过DMA写到RAM（内存）后，网卡是如何通知其他系统这个包可以被处理了呢
？

传统的方式是，网卡会产生一个硬件中断（IRQ），通知数据包到了。有三种常见的硬中断类
型：

* MSI-X
* MSI
* legacy IRQ

稍后会介绍到。

如果有大量的数据包到达，会产生大量的硬件中断，CPU忙于处理硬件中断的时候，可用于
处理其他任务的时间就会减少。

NAPI（New API)是一种新的机制，可以减少产生的硬件中断的数量，但不能完全消除硬中断
。

#### 3.3.4 NAPI

##### NAPI

NAPI接收数据包的方式和传统方式不同，它允许设备驱动注册一个`poll`方法，然后调用这
个方法完成收包。

NAPI的使用方式：

1. 驱动打开了NAPI功能，但默认处于未工作状态（没有在收包）
2. 数据包到达，网卡通过DMA写到内存
3. 网卡触发一个硬中断，中断处理函数开始执行
4. 软中断（softirq，稍后介绍），唤醒NAPI子系统。这会触发在一个单独的线程里，调用
   驱动注册的`poll`方法收包
5. 驱动禁止网卡产生新的硬件中断。这样做是为了NAPI能够在收包的时候不会被新的中断
   打扰
6. 一旦没有包需要收了，NAPI关闭，网卡的硬中断重新开启
7. 转步骤2

和传统方式相比，NAPI一次中断会接收多个packet，因此可以减少硬件中断的数量。

`poll`方法是通过调用`netif_napi_add`注册到NAPI的，同时还可以指定权重`weight`，大
部分驱动都hardcode为64。后面会进一步解释这个weight以及hardcode 64。

通常来说，驱动在初始化的时候注册NAPI poll方法。

#### 3.3.5 `igb`驱动的NAPI初始化

`igb`驱动的初始化过程是一个很长的调用链：

1. `igb_probe` `->` `igb_sw_init`.
1. `igb_sw_init` `->` `igb_init_interrupt_scheme`.
1. `igb_init_interrupt_scheme` `->` `igb_alloc_q_vectors`.
1. `igb_alloc_q_vectors` `->` `igb_alloc_q_vector`.
1. `igb_alloc_q_vector` `->` `netif_napi_add`.

从比较高的层面来看，这个调用过程会做以下事情：

1. 如果支持`MSI-X`，调用`pci_enable_msix`打开它
1. 计算和初始化一些配置，包括网卡收发队列的数量
1. 调用`igb_alloc_q_vector`创建每个发送和接收队列
1. `igb_alloc_q_vector`会进一步调用`netif_napi_add`注册poll方法到NAPI实例

我们来看下`igb_alloc_q_vector`是如何注册poll方法和私有数据的：
[drivers/net/ethernet/intel/igb/igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L1145-L1271):

```c
static int igb_alloc_q_vector(struct igb_adapter *adapter,
                              int v_count, int v_idx,
                              int txr_count, int txr_idx,
                              int rxr_count, int rxr_idx)
{
  /* ... */

  /* allocate q_vector and rings */
  q_vector = kzalloc(size, GFP_KERNEL);
  if (!q_vector)
          return -ENOMEM;

  /* initialize NAPI */
  netif_napi_add(adapter->netdev, &q_vector->napi, igb_poll, 64);

  /* ... */
```

`q_vector`是新分配的队列，`igb_poll`是poll方法，当它收包的时候，会通过
这个接收队列找到关联的NAPI实例（`q_vector->napi`）。

这里很重要，后面我们介绍从驱动到网络协议栈的flow（根据IP头信息做哈希，哈希相同的
属于同一个flow）时会看到。

### 3.4 启用网卡 (Bring Up)

回忆前面我们提到的`structure net_device_ops`实例，它包含网卡启用、发包、设置mac
地址等回调函数（函数指针）。

当启用一个网卡时（例如，通过`ifconfig eth0 up`），`net_device_ops`的`ndo_open`方
法会被调用。它通常会做以下事情：

1. 分配RX、TX队列内存
1. 打开NAPI功能
1. 注册中断处理函数
1. 打开（允许）硬中断
1. 其他

`igb`驱动中，这个方法对应的是`igb_open`函数。

#### 3.4.1 准备从网络接收数据

今天的大部分网卡都使用DMA将数据直接写到内存，接下来操作系统可以直接从里面读取。
实现这一目的所使用的数据结构是buffer ring（环形缓冲区）。

要实现这一功能，设备驱动必须和操作系统合作，预留（reserve）出一段内存来给网卡使用。
预留成功后，网卡知道了这块内存的地址，接下来收到的包就会放到这里，他们接下来会被
操作系统取走。

由于这块内存区域是有限的，如果数据包的速率非常快，单个CPU来不及取走这些包，新来
的包就会被丢弃。这时候，Receive Side Scaling（RSS，接收端扩展）或者多队列（
multiqueue）一类的技术可能就会排上用场。

一些网卡有能力将接收到的包写到多个不同的内存区域，每个区域都是独立的接收队列。这
样操作系统就可以利用多个CPU（硬件层面）并行处理进来的包。只有部分网卡支持这个功
能。

Intel I350网卡支持多队列，我们可以在`igb`的驱动里看出来。`igb`驱动启用的时候，最
开始做的事情之一就是调用[`igb_setup_all_rx_resources`](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L2801-L2804)函数。这个函数会接着对每个RX
队列调用`igb_setup_rx_resources`, 里面会管理DMA的内存.

如果你对其原理感兴趣，可以进一步查看[Linux kernel’s DMA API
HOWTO](https://github.com/torvalds/linux/blob/v3.13/Documentation/DMA-API-HOWTO.txt)
。

RX队列的数量和大小可以通过ethtool进行配置，调整这两个参数会对收包或者丢包产生可见影响。

网卡通过对packet头（例如源地址、目的地址、端口等待）做哈希，以此决定将packet放到
哪个RX队列。只有很少的网卡支持调整哈希算法。如果支持的话，那你可以根据算法将特定
的flow发到特定的队列，甚至可以做到在硬件层面直接将某些包丢弃。

一些网卡支持调整RX队列的权重，你可以有意地将更多的流量发到指定的queue。

后面会介绍如何对这些参数进行调优。

#### 3.4.2 Enable NAPI

前面看到了驱动如何注册NAPI `poll`方法，但是，一般直到网卡被启用之后，NAPI才被启用。

启用NAPI很简单，调用`napi_enable`函数就行，这个函数会设置NAPI实例（`struct napi_struct`）的一个表示是否启用的标志位。
前面说到，NAPI启用后并不是立即开始工作（而是等硬中断触发）。

对于`igb`，驱动初始化或者通过ethtool修改queue数量或大小的时候，会启用每个`q_vector`的NAPI实例。
[drivers/net/ethernet/intel/igb/igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L2833-L2834):

```c
for (i = 0; i < adapter->num_q_vectors; i++)
  napi_enable(&(adapter->q_vector[i]->napi));
```

#### 3.4.3 注册中断处理函数

启用NAPI之后，下一步就是注册中断处理函数。设备有多种方式触发一个中断：

* MSI-X
* MSI
* legacy interrupts

设备驱动的实现也因此而异。

驱动必须判断出设备支持哪种中断方式，然后注册相应的中断处理函数，这些函数在中断发
生的时候会被执行。

一些驱动，例如`igb`，会试图为每种中断类型注册一个中断处理函数，如果注册失败，就尝
试下一种（没测试过的）类型。

MSI-X中断是比较推荐的方式，尤其是对于支持多队列的网卡。因为每个RX队列有独立的
MSI-X中断，因此可以被不同的CPU处理（通过`irqbalance`方式，或者修改
`/proc/irq/IRQ_NUMBER/smp_affinity`）。我们后面会看到，处理中断的CPU也是后来处理
这个包的CPU。这样的话，从网卡硬件中断的层面，就可以设置让收到的包被不同的CPU处理
。

如果MSI-X不支持，那MSI相比于传统中断方式仍然有一些优势，驱动仍然会优先考虑它。
这个[wiki](https://en.wikipedia.org/wiki/Message_Signaled_Interrupts)介绍了更多
关于MSI和MSI-X的信息。

在`igb`驱动中，函数`igb_msix_ring`, `igb_intr_msi`, `igb_intr`分别是MSI-X, MSI, 和传统中断方式的中断处理函数。

这段代码显式了驱动是如何尝试各种中断类型的，
[drivers/net/ethernet/intel/igb/igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L1360-L1413):

```c
static int igb_request_irq(struct igb_adapter *adapter)
{
  struct net_device *netdev = adapter->netdev;
  struct pci_dev *pdev = adapter->pdev;
  int err = 0;

  if (adapter->msix_entries) {
    err = igb_request_msix(adapter);
    if (!err)
      goto request_done;
    /* fall back to MSI */

    /* ... */
  }

  /* ... */

  if (adapter->flags & IGB_FLAG_HAS_MSI) {
    err = request_irq(pdev->irq, igb_intr_msi, 0,
          netdev->name, adapter);
    if (!err)
      goto request_done;

    /* fall back to legacy interrupts */

    /* ... */
  }

  err = request_irq(pdev->irq, igb_intr, IRQF_SHARED,
        netdev->name, adapter);

  if (err)
    dev_err(&pdev->dev, "Error %d getting interrupt\n", err);

request_done:
  return err;
}
```

这就是`igb`驱动注册中断处理函数的过程，这个函数在一个包到达网卡，触发了一个硬件
中断的时候，就会被执行。

#### 3.4.4 Enable Interrupts

到这里，几乎所有的准备工作都就绪了。唯一剩下的就是打开硬中断，等待数据包进来。
打开硬中断的方式因硬件而异，`igb`驱动是在`__igb_open`里调用辅助函数`igb_irq_enable`完成的。

中断通过写寄存器的方式打开：

```c
static void igb_irq_enable(struct igb_adapter *adapter)
{

  /* ... */

    wr32(E1000_IMS, IMS_ENABLE_MASK | E1000_IMS_DRSTA);
    wr32(E1000_IAM, IMS_ENABLE_MASK | E1000_IMS_DRSTA);

  /* ... */
}
```

现在，网卡已经启用了。驱动可能还会做一些额外的事情，例如启动定时器，工作队列（
work queue），或者其他硬件相关的设置。这些工作做完后，网卡就可以收包了。

我们接下来看一下监控和调优网卡。

### 3.5 网卡监控

监控网络设备有几种不同的方式，每种方式的监控粒度（granularity）和复杂度不同。我
们先从最粗的粒度开始，逐步细化。

#### 3.5.1 Using ethtool -S

`ethtool -S`可以查看网卡统计信息（例如丢弃的包数量）：

```shell
$ sudo ethtool -S eth0
NIC statistics:
     rx_packets: 597028087
     tx_packets: 5924278060
     rx_bytes: 112643393747
     tx_bytes: 990080156714
     rx_broadcast: 96
     tx_broadcast: 116
     rx_multicast: 20294528
     ....
```

监控这些数据比较困难。因为用命令行获取很容易，但是以上字段并没有一个统一的标准。
不同的驱动，甚至同一驱动的不同版本可能字段都会有差异。

你可以先粗略的查看“drop”, “buffer”, “miss”等字样。然后，在驱动的源码里找到对应的
更新这些字段的地方，这可能是在软件层面更新的，也有可能是在硬件层面通过寄存器更新
的。如果是通过硬件寄存器的方式，你就得查看网卡的data sheet（说明书），搞清楚这个
寄存器代表什么。ethtoool给出的这些字段名，有一些是有误导性的（misleading）。

#### 3.5.2 Using sysfs

sysfs也提供了统计信息，但相比于网卡层的统计，要更上层一些。

例如，获取eth0的接收端drop的数量：

```shell
$ cat /sys/class/net/eth0/statistics/rx_dropped
2
```

不同类型的统计分别位于`/sys/class/net/<NIC>/statistics/`下面的不同文件，包括
`collisions`, `rx_dropped`, `rx_errors`, `rx_missed_errors`等等。

不幸的是，每种类型代表什么意思，是有驱动来决定的，因此也是有驱动决定何时以及在哪
里更新这些计数的。你可能会发现一些驱动将一些特定类型的错误归类为dro，而另外
一些驱动可能将它们归类为miss。

这些值至关重要，因此你需要查看对应的网卡驱动，搞清楚它们真正代表什么。

#### 3.5.3 Using `/proc/net/dev`

`/proc/net/dev`提供了更高一层的网卡统计。

```shell
$ cat /proc/net/dev
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
  eth0: 110346752214 597737500    0    2    0     0          0  20963860 990024805984 6066582604    0    0    0     0       0          0
    lo: 428349463836 1579868535    0    0    0     0          0         0 428349463836 1579868535    0    0    0     0       0          0
```

这个文件里显式的统计只是sysfs里面的一个子集，但适合作为一个常规的统计参考。

前面的警告（caveat）也适用于此：如果这些数据对你非常重要，那你必须得查看内核源码
、驱动源码和驱动手册，搞清楚每个字段真正代表什么意思，计数是如何以及何时被更新的。

### 3.6 网卡调优

#### 3.6.1 查看RX队列数量

如果你的网卡及其驱动支持RSS/多队列，那你可能会调整RX queue（也叫RX channel）的数量。
这可以用ethtool完成。

查看RX queue数量：

```shell
$ sudo ethtool -l eth0
Channel parameters for eth0:
Pre-set maximums:
RX:   0
TX:   0
Other:    0
Combined: 8
Current hardware settings:
RX:   0
TX:   0
Other:    0
Combined: 4
```

这里可以看到允许的最大值（网卡及驱动限制），以及当前设置的值。

注意：不是所有网卡驱动都支持这个操作。如果你的网卡不支持，会看到如下类似的错误：

```shell
$ sudo ethtool -l eth0
Channel parameters for eth0:
Cannot get device channel parameters
: Operation not supported
```

这意味着你的驱动没有实现ethtool的`get_channels`方法。可能的原因包括：该网卡不支
持调整RX queue数量，不支持RSS/multiqueue，或者驱动没有更新来支持此功能。

#### 3.6.2 调整RX queues

`ethtool -L`可以修改RX queue数量。

注意：一些网卡和驱动只支持combined queue，这种模式下，RX queue和TX queue是一对一
绑定的，上面的例子我们看到的就是这种。

设置combined类型网卡的收发队列为8个：

```shell
$ sudo ethtool -L eth0 combined 8
```

如果你的网卡支持独立的RX和TX队列数量，那你可以只修改RX queue数量：

```shell
$ sudo ethtool -L eth0 rx 8
```

注意：对于大部分驱动，修改以上配置会使网卡先down再up，因此会造成丢包。请酌情使用
。

### 3.6.3 调整RX queue的大小

一些网卡和驱动也支持修改RX queue的大小。底层是如何工作的，随硬件而异，但幸运的是
，ethtool提供了一个通用的接口来做这件事情。增加RX queue的大小可以在流量很大的时
候缓解丢包问题，但是，只调整这个还不够，软件层面仍然可能会丢包，因此还需要其他的
一些调优才能彻底的缓解或解决丢包问题。

`ethtool -g`可以查看queue的大小。

```shell
$ sudo ethtool -g eth0
Ring parameters for eth0:
Pre-set maximums:
RX:   4096
RX Mini:  0
RX Jumbo: 0
TX:   4096
Current hardware settings:
RX:   512
RX Mini:  0
RX Jumbo: 0
TX:   512
```

以上显式网卡支持最多4096个接收和发送descriptor（描述符，简单理解，存放的是指向包
的指针），但是现在只用到了512个。

用`ethtool -G`修改queue大小：

```shell
$ sudo ethtool -G eth0 rx 4096
```

注意：对于大部分驱动，修改以上配置会使网卡先down再up，因此会造成丢包。请酌情使用
。

#### 3.6.4 调整RX queue的权重（weight）

一些网卡支持给不同的queue设置不同的权重，以此分发不同数量的网卡包到不同的队列。

如果你的网卡支持以下功能，那你可以使用：

1. 网卡支持flow indirection（flow重定向，flow是什么前面提到过）
1. 网卡驱动实现了`get_rxfh_indir_size`和`get_rxfh_indir`方法
1. 使用的ethtool版本足够新，支持`-x`和`-X`参数来设置indirection table

`ethtool -x`检查flow indirection设置：

```shell
$ sudo ethtool -x eth0
RX flow hash indirection table for eth3 with 2 RX ring(s):
0: 0 1 0 1 0 1 0 1
8: 0 1 0 1 0 1 0 1
16: 0 1 0 1 0 1 0 1
24: 0 1 0 1 0 1 0 1
```

第一列是哈希值索引，是该行的第一个哈希值；冒号后面的是每个哈希值对于的queue，例
如，第一行分别是哈希值0，1，2，3，4，5，6，7，对应的packet应该分别被放到RX queue
0，1，0，1，0，1，0，1。

例子：在前两个RX queue之间均匀的分发（接收到的包）：

```shell
$ sudo ethtool -X eth0 equal 2
```

例子：用`ethtool -X`设置自定义权重：

```shell
$ sudo ethtool -X eth0 weight 6 2
```

以上命令分别给rx queue 0和rx queue 1不同的权重：6和2，因此queue 0接收到的数量更
多。注意queue一般是和CPU绑定的，因此这也意味着相应的CPU也会花更多的时间片在收包
上。

一些网卡还支持修改计算hash时使用哪些字段。

#### 3.6.5 调整RXA hash fields for network flows

可以用ethtool调整RSS计算哈希时所使用的字段。

例子：查看UDP RX flow哈希所使用的字段：

```shell
$ sudo ethtool -n eth0 rx-flow-hash udp4
UDP over IPV4 flows use these fields for computing Hash flow key:
IP SA
IP DA
```

可以看到只用到了源IP（SA：Source Address）和目的IP。

我们接下来修改一下，加入源端口和目的端口：

```shell
$ sudo ethtool -N eth0 rx-flow-hash udp4 sdfn
```

`sdfn`的具体含义解释起来有点麻烦，请查看ethtool的帮助（man page）。

调整hash所用字段是有用的，而`ntuple`过滤对于更加细粒度的flow control更加有用。

#### 3.6.6 ntuple filtering for steering network flows

一些网卡支持“ntuple filtering”特性。该特性允许用户（通过ethtool）指定一些参数来
在硬件上过滤收到的包，然后将其直接放到特定的RX queue。例如，用户可以指定到特定目
端口的TCP包放到RX queue 1。

Intel的网卡上这个特性叫Intel Ethernet Flow Director，其他厂商可能也有他们的名字
，这些都是出于市场宣传原因，底层原理是类似的。

我们后面会看到，ntuple filtering是一个叫Accelerated Receive Flow Steering (aRFS)功能的
核心部分之一，后者使得ntuple filtering的使用更加方便。

这个特性适用的场景：最大化数据本地性（data locality），以增加CPU处理网络数据时的
缓存命中率。例如，考虑运行在80口的web服务器：

1. webserver进程运行在80口，并绑定到CPU 2
1. 和某个RX queue关联的硬中断绑定到CPU 2
1. 目的端口是80的TCP流量通过ntuple filtering绑定到CPU 2
1. 接下来所有到80口的流量，从数据包进来到数据到达用户程序的整个过程，都由CPU 2处
   理
1. 仔细监控系统的缓存命中率、网络栈的延迟等信息，以验证以上配置是否生效


检查ntuple filtering特性是否打开：

```shell
$ sudo ethtool -k eth0
Offload parameters for eth0:
...
ntuple-filters: off
receive-hashing: on
```

可以看到，上面的ntuple是关闭的。

打开：

```shell
$ sudo ethtool -K eth0 ntuple on
```

打开ntuple filtering功能，并确认打开之后，可以用`ethtool -u`查看当前的ntuple
rules：

```shell
$ sudo ethtool -u eth0
40 RX rings available
Total 0 rules
```

可以看到当前没有rules。

我们来加一条：目的端口是80的放到RX queue 2：

```shell
$ sudo ethtool -U eth0 flow-type tcp4 dst-port 80 action 2
```

你也可以用ntuple filtering在硬件层面直接drop某些flow的包。当特定IP过来的流量太大
时，这种功能可能会派上用场。更多关于ntuple的信息，参考ethtool man page。

`ethtool -S <DEVICE>`的输出统计里，Intel的网卡有`fdir_match`和`fdir_miss`两项，
是和ntuple filtering相关的。关于具体的、详细的统计计数，需要查看相应网卡的设备驱
动和data sheet。
