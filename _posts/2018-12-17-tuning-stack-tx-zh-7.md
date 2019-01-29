---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 7"
date:   2018-12-17
author: ArthurChiao
categories: network-stack monitoring tuning
---

## 9 网络设备驱动

我们即将结束我们的网络栈之旅。

要理解数据包的发送过程，有一个重要的概念。大多数设备和驱动程序通过两个阶段处理数
据包发送：

1. 合理地组织数据，然后触发设备通过DMA从RAM中读取数据并将其发送到网络中
1. 发送完成后，设备发出中断，驱动程序解除映射缓冲区、释放内存或清除其状态

第二阶段通常称为“发送完成”（transmit completion）阶段。我们将对以上两阶段进行研
究，先从第一个开始：发送阶段。

之前已经看到，`dev_hard_start_xmit`通过调用`ndo_start_xmit`（保持一个锁）来发送
数据，所以接下来先看驱动程序是如何注册`ndo_start_xmit`的，然后再深入理解该函数的
工作原理。

与上篇[Linux网络栈监控和调优：接收数据]({% link _posts/2018-12-05-tuning-stack-rx-zh-1.md %})
一样，我们将拿`igb`驱动作为例子。

### 9.1 驱动回调函数注册

驱动程序实现了一系列方法来支持设备操作，例如：

1. 发送数据（`ndo_start_xmit`）
1. 获取统计信息（`ndo_get_stats64`）
1. 处理设备`ioctl`s（`ndo_do_ioctl`）

这些方法通过一个`struct net_device_ops`实例导出。让我们来看看[igb驱动程序
](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L1905-L1928)
中这些操作：

```c
static const struct net_device_ops igb_netdev_ops = {
        .ndo_open               = igb_open,
        .ndo_stop               = igb_close,
        .ndo_start_xmit         = igb_xmit_frame,
        .ndo_get_stats64        = igb_get_stats64,

                /* ... more fields ... */
};
```

这个`igb_netdev_ops`变量在
[`igb_probe`](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L2090)
函数中注册给设备：

```c
static int igb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
                /* ... lots of other stuff ... */

        netdev->netdev_ops = &igb_netdev_ops;

                /* ... more code ... */
}
```

正如我们在上一节中看到的，更上层的代码将通过设备的`netdev_ops`字段
调用适当的回调函数。想了解更多关于PCI设备是如何启动的，以及何时/何处调用
`igb_probe`，请查看我们之前文章中的[驱动程序初始化
](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#initialization)
部分。

### 9.2 通过`ndo_start_xmit`发送数据

上层的网络栈通过`struct net_device_ops`实例里的回调函数，调用驱动程序来执行各种
操作。正如我们之前看到的，qdisc代码调用`ndo_start_xmit`将数据传递给驱动程序进行
发送。对于大多数硬件设备，都是在保持一个锁时调用`ndo_start_xmit`函数。

在igb设备驱动程序中，`ndo_start_xmit`字段初始化为`igb_xmit_frame`函数，所以
我们接下来从`igb_xmit_frame`开始，查看该驱动程序是如何发送数据的。跟随
[drivers/net/ethernet/intel/igb/igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L4664-L4741)
，并记得以下代码在整个执行过程中都hold着一个锁：

```c
netdev_tx_t igb_xmit_frame_ring(struct sk_buff *skb,
                                struct igb_ring *tx_ring)
{
        struct igb_tx_buffer *first;
        int tso;
        u32 tx_flags = 0;
        u16 count = TXD_USE_COUNT(skb_headlen(skb));
        __be16 protocol = vlan_get_protocol(skb);
        u8 hdr_len = 0;

        /* need: 1 descriptor per page * PAGE_SIZE/IGB_MAX_DATA_PER_TXD,
         *       + 1 desc for skb_headlen/IGB_MAX_DATA_PER_TXD,
         *       + 2 desc gap to keep tail from touching head,
         *       + 1 desc for context descriptor,
         * otherwise try next time
         */
        if (NETDEV_FRAG_PAGE_MAX_SIZE > IGB_MAX_DATA_PER_TXD) {
                unsigned short f;
                for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
                        count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size);
        } else {
                count += skb_shinfo(skb)->nr_frags;
        }
```

函数首先使用`TXD_USER_COUNT`宏来计算发送skb所需的描述符数量，用`count`
变量表示。然后根据分片情况，对`count`进行相应调整。

```c
        if (igb_maybe_stop_tx(tx_ring, count + 3)) {
                /* this is a hard error */
                return NETDEV_TX_BUSY;
        }
```

然后驱动程序调用内部函数`igb_maybe_stop_tx`，检查TX Queue以确保有足够可用的描
述符。如果没有，则返回`NETDEV_TX_BUSY`。正如我们之前在qdisc代码中看到的那样，这
将导致qdisc将skb重新入队以便稍后重试。

```c
        /* record the location of the first descriptor for this packet */
        first = &tx_ring->tx_buffer_info[tx_ring->next_to_use];
        first->skb = skb;
        first->bytecount = skb->len;
        first->gso_segs = 1;
```

然后，获取TX Queue中下一个可用缓冲区信息，用`struct igb_tx_buffer *first`表
示，这个信息稍后将用于设置缓冲区描述符。数据包`skb`指针及其大小`skb->len`
也存储到`first`。

```c
        skb_tx_timestamp(skb);
```

接下来代码调用`skb_tx_timestamp`，获取基于软件的发送时间戳。应用程序可以
使用发送时间戳来确定数据包通过网络栈的发送路径所花费的时间。

某些设备还支持硬件时间戳，这允许系统将打时间戳任务offload到设备。程序员因此可以
获得更准确的时间戳，因为它更接近于硬件实际发送的时间。

某些网络设备可以使用[Precision Time
Protocol](https://events.linuxfoundation.org/sites/events/files/slides/lcjp14_ichikawa_0.pdf)
（PTP，精确时间协议）在硬件中为数据包加时间戳。驱动程序处理用户的硬件时间戳请求。

我们现在看到这个代码：

```c
        if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
                struct igb_adapter *adapter = netdev_priv(tx_ring->netdev);

                if (!(adapter->ptp_tx_skb)) {
                        skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
                        tx_flags |= IGB_TX_FLAGS_TSTAMP;

                        adapter->ptp_tx_skb = skb_get(skb);
                        adapter->ptp_tx_start = jiffies;
                        if (adapter->hw.mac.type == e1000_82576)
                                schedule_work(&adapter->ptp_tx_work);
                }
        }
```

上面的if语句检查`SKBTX_HW_TSTAMP`标志，该标志表示用户请求了硬件时间戳。接下来检
查是否设置了`ptp_tx_skb`。一次只能给一个数据包加时间戳，因此给正在打时间戳的skb
上设置了`SKBTX_IN_PROGRESS`标志。然后更新`tx_flags`，将`IGB_TX_FLAGS_TSTAMP`标志
置位。`tx_flags`变量稍后将被复制到缓冲区信息结构中。

当前的`jiffies`值赋给`ptp_tx_start`。驱动程序中的其他代码将使用这个值，
以确保TX硬件打时间戳不会hang住。最后，如果这是一个82576以太网硬件网卡，将用
`schedule_work`函数启动[工作队列](http://www.makelinux.net/ldd3/chp-7-sect-6)。

```c
        if (vlan_tx_tag_present(skb)) {
                tx_flags |= IGB_TX_FLAGS_VLAN;
                tx_flags |= (vlan_tx_tag_get(skb) << IGB_TX_FLAGS_VLAN_SHIFT);
        }
```

上面的代码将检查skb的`vlan_tci`字段是否设置了，如果是，将设置`IGB_TX_FLAGS_VLAN`
标记，并保存VLAN ID。

```c
        /* record initial flags and protocol */
        first->tx_flags = tx_flags;
        first->protocol = protocol;
```

最后将`tx_flags`和`protocol`值都保存到`first`变量里面。

```c
        tso = igb_tso(tx_ring, first, &hdr_len);
        if (tso < 0)
                goto out_drop;
        else if (!tso)
                igb_tx_csum(tx_ring, first);
```

接下来，驱动程序调用其内部函数`igb_tso`，判断skb是否需要分片。如果需要
，缓冲区信息变量（`first`）将更新标志位，以提示硬件需要做TSO。

如果不需要TSO，则`igb_tso`返回0；否则返回1。 如果返回0，则将调用`igb_tx_csum`来
处理校验和offload信息（是否需要offload，是否支持此协议的offload）。
`igb_tx_csum`函数将检查skb的属性，修改`first`变量中的一些标志位，以表示需要校验
和offload。

```c
        igb_tx_map(tx_ring, first, hdr_len);
```

`igb_tx_map`函数准备给设备发送的数据。我们后面会仔细查看这个函数。

```c
        /* Make sure there is space in the ring for the next send. */
        igb_maybe_stop_tx(tx_ring, DESC_NEEDED);

        return NETDEV_TX_OK;
```

发送结束之后，驱动要检查确保有足够的描述符用于下一次发送。如果不够，TX Queue将被
关闭。最后返回`NETDEV_TX_OK`给上层（qdisc代码）。

```c
out_drop:
        igb_unmap_and_free_tx_resource(tx_ring, first);

        return NETDEV_TX_OK;
}
```

最后是一些错误处理代码，只有当`igb_tso`遇到某种错误时才会触发此代码。
`igb_unmap_and_free_tx_resource`用于清理数据。在这种情况下也返回`NETDEV_TX_OK`
。发送没有成功，但驱动程序释放了相关资源，没有什么需要做的了。请注意，在这种情
况下，此驱动程序不会增加drop计数，但或许它应该增加。

### 9.3 `igb_tx_map`

`igb_tx_map`函数处理将skb数据映射到RAM的DMA区域的细节。它还会更新设备TX Queue的
尾部指针，从而触发设备“被唤醒”，从RAM获取数据并开始发送。

让我们简单地看一下这个[函数
](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L4501-L4627)
的工作原理：

```c
static void igb_tx_map(struct igb_ring *tx_ring,
                       struct igb_tx_buffer *first,
                       const u8 hdr_len)
{
        struct sk_buff *skb = first->skb;

                /* ... other variables ... */

        u32 tx_flags = first->tx_flags;
        u32 cmd_type = igb_tx_cmd_type(skb, tx_flags);
        u16 i = tx_ring->next_to_use;

        tx_desc = IGB_TX_DESC(tx_ring, i);

        igb_tx_olinfo_status(tx_ring, tx_desc, tx_flags, skb->len - hdr_len);

        size = skb_headlen(skb);
        data_len = skb->data_len;

        dma = dma_map_single(tx_ring->dev, skb->data, size, DMA_TO_DEVICE);
```

上面的代码所做的一些事情：

1. 声明变量并初始化
1. 使用`IGB_TX_DESC`获取下一个可用描述符的指针
1. `igb_tx_olinfo_status`函数更新`tx_flags`，并将它们复制到描述符（`tx_desc`）中
1. 计算skb头长度和数据长度
1. 调用`dma_map_single`为`skb->data`构造内存映射，以允许设备通过DMA从RAM中读取数据

接下来是驱动程序中的一个**非常长的循环，用于为skb的每个分片生成有效映射**。具体如何
做的细节并不是特别重要，但如下步骤值得一提：

* 驱动程序遍历该数据包的所有分片
* 当前描述符有其数据的DMA地址信息
* 如果分片的大小大于单个IGB描述符可以发送的大小，则构造多个描述符指向可DMA区域的块，直到描述符指向整个分片
* 更新描述符迭代器
* 更新剩余长度
* 当没有剩余分片或者已经消耗了整个数据长度时，循环终止

下面提供循环的代码以供以上描述参考。这里的代码进一步向读者说明，**如果可能的话，避
免分片是一个好主意**。分片需要大量额外的代码来处理网络栈的每一层，包括驱动层。

```c
        tx_buffer = first;

        for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
                if (dma_mapping_error(tx_ring->dev, dma))
                        goto dma_error;

                /* record length, and DMA address */
                dma_unmap_len_set(tx_buffer, len, size);
                dma_unmap_addr_set(tx_buffer, dma, dma);

                tx_desc->read.buffer_addr = cpu_to_le64(dma);

                while (unlikely(size > IGB_MAX_DATA_PER_TXD)) {
                        tx_desc->read.cmd_type_len =
                                cpu_to_le32(cmd_type ^ IGB_MAX_DATA_PER_TXD);

                        i++;
                        tx_desc++;
                        if (i == tx_ring->count) {
                                tx_desc = IGB_TX_DESC(tx_ring, 0);
                                i = 0;
                        }
                        tx_desc->read.olinfo_status = 0;

                        dma += IGB_MAX_DATA_PER_TXD;
                        size -= IGB_MAX_DATA_PER_TXD;

                        tx_desc->read.buffer_addr = cpu_to_le64(dma);
                }

                if (likely(!data_len))
                        break;

                tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type ^ size);

                i++;
                tx_desc++;
                if (i == tx_ring->count) {
                        tx_desc = IGB_TX_DESC(tx_ring, 0);
                        i = 0;
                }
                tx_desc->read.olinfo_status = 0;

                size = skb_frag_size(frag);
                data_len -= size;

                dma = skb_frag_dma_map(tx_ring->dev, frag, 0,
                                       size, DMA_TO_DEVICE);

                tx_buffer = &tx_ring->tx_buffer_info[i];
        }
```

所有需要的描述符都已建好，且`skb`的所有数据都映射到DMA地址后，驱动就会
进入到它的最后一步，触发一次发送：

```c
        /* write last descriptor with RS and EOP bits */
        cmd_type |= size | IGB_TXD_DCMD;
        tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type);
```

对最后一个描述符设置`RS`和`EOP`位，以提示设备这是最后一个描述符了。

```c
        netdev_tx_sent_queue(txring_txq(tx_ring), first->bytecount);

        /* set the timestamp */
        first->time_stamp = jiffies;
```

调用`netdev_tx_sent_queue`函数，同时带着将发送的字节数作为参数。这个函数是byte
query limit（字节查询限制）功能的一部分，我们将在稍后详细介绍。当前的jiffies存
储到`first`的时间戳字段。

接下来，有点tricky：

```c
        /* Force memory writes to complete before letting h/w know there
         * are new descriptors to fetch.  (Only applicable for weak-ordered
         * memory model archs, such as IA-64).
         *
         * We also need this memory barrier to make certain all of the
         * status bits have been updated before next_to_watch is written.
         */
        wmb();

        /* set next_to_watch value indicating a packet is present */
        first->next_to_watch = tx_desc;

        i++;
        if (i == tx_ring->count)
                i = 0;

        tx_ring->next_to_use = i;

        writel(i, tx_ring->tail);

        /* we need this if more than one processor can write to our tail
         * at a time, it synchronizes IO on IA64/Altix systems
         */
        mmiowb();

        return;
```

上面的代码做了一些重要的事情：

1. 调用`wmb`函数强制完成内存写入。这通常称作**“写屏障”**（write barrier）
   ，是通过CPU平台相关的特殊指令完成的。这对某些CPU架构非常重要，因为如果触发
   设备启动DMA时不能确保所有内存写入已经完成，那设备可能从RAM中读取不一致
   状态的数据。[这篇文章](http://preshing.com/20120930/weak-vs-strong-memory-models/)和[这个课程](http://www.cs.utexas.edu/~pingali/CS378/2012fa/lectures/consistency.pdf)深
   入探讨了内存顺序的细节
1. 设置`next_to_watch`字段，它将在completion阶段后期使用
1. 更新计数，并且TX Queue的`next_to_use`字段设置为下一个可用的描述符。使用
   `writel`函数更新TX Queue的尾部。`writel`向[内存映射I/O](https://en.wikipedia.org/wiki/Memory-mapped_I/O)地址写入一个`long`型数据
   ，这里地址是`tx_ring->tail`（一个硬件地址），要写入的值是`i`。这次写操作会让
   设备知道其他数据已经准备好，可以通过DMA从RAM中读取并写入网络
1. 最后，调用`mmiowb`函数。它执行特定于CPU体系结构的指令，对内存映射的
   写操作进行排序。它也是一个写屏障，用于内存映射的I/O写

想了解更多关于`wmb`，`mmiowb`以及何时使用它们的信息，可以阅读Linux内核中一些包含
内存屏障的优秀[文档](https://github.com/torvalds/linux/blob/v3.13/Documentation/memory-barriers.txt)
。

最后，代码包含了一些错误处理。只有DMA API（将skb数据地址映射到DMA地址）返回错误
时，才会执行此代码。

```c
dma_error:
        dev_err(tx_ring->dev, "TX DMA map failed\n");

        /* clear dma mappings for failed tx_buffer_info map */
        for (;;) {
                tx_buffer = &tx_ring->tx_buffer_info[i];
                igb_unmap_and_free_tx_resource(tx_ring, tx_buffer);
                if (tx_buffer == first)
                        break;
                if (i == 0)
                        i = tx_ring->count;
                i--;
        }

        tx_ring->next_to_use = i;
```

在继续跟进“发送完成”（transmit completion）过程之前，让我们来看下之前跳过了的一
个东西：dynamic queue limits（动态队列限制）。

#### Dynamic Queue Limits (DQL)

正如在本文中看到的，**数据在逐步接近网络设备的过程中，花费了大量时间在
不同阶段的Queue里面**。队列越大，在队列中所花费的时间就越多。

解决这个问题的一种方式是**背压**（back pressure）。动态队列限制（DQL）系统是一种
机制，驱动程序可以使用该机制向网络系统（network system）施加反压，以避免设备
无法发送时有过多的数据积压在队列。

要使用DQL，驱动需要在其发送和完成例程（transmit and completion routines）中调用
几次简单的API。DQL内部算法判断何时数据已足够多，达到此阈值后，DQL将暂时禁用TX
Queue，从而对网络系统产生背压。当足够的数据已发送完后，DQL再自动重新启用
该队列。

[这里](https://www.linuxplumbersconf.org/2012/wp-content/uploads/2012/08/bql_slide.pdf)
给出了DQL的一些性能数据及DQL内部算法的说明。

我们刚刚看到的`netdev_tx_sent_queue`函数就是DQL API一部分。当数据排
队到设备进行发送时，将调用此函数。发送完成后，驱动程序调用
`netdev_tx_completed_queue`。在内部，这两个函数都将调用DQL库（在
[lib/dynamic_queue_limits.c](https://github.com/torvalds/linux/blob/v3.13/lib/dynamic_queue_limits.c)
和
[include/linux/dynamic_queue_limits.h](https://github.com/torvalds/linux/blob/v3.13/include/linux/dynamic_queue_limits.h)
），以判断是否禁用、重新启用DQL，或保持配置不动。

DQL在sysfs中导出了一些统计信息和调优参数。调整DQL不是必需的；算法自己会随着时间
变化调整其参数。尽管如此，为了完整性，我们稍后会看到如何监控和调整DQL。

### 9.4 发送完成（Transmit completions）

设备发送数据之后会产生一个中断，表示发送已完成。然后，设备驱动程序可以调度一些长
时间运行的工作，例如解除DMA映射、释放数据。这是如何工作的取决于不同设备。对于
`igb`驱动程序（及其关联设备），发送完成和数据包接收所触发的IRQ是相同的。这意味着
对于`igb`驱动程序，`NET_RX`既用于处理发送完成，又用于处理数据包接收。

让我重申一遍，以强调这一点的重要性：**你的设备可能会发出与“接收到数据包时触发的中
断”相同的中断来表示“数据包发送已完成”**。如果是这种情况，则`NET_RX` softirq会被用于
处理**数据包接收**和**发送完成**两种情况。

由于两个操作共享相同的IRQ，因此只能注册一个IRQ处理函数来处理这两种情况。
回忆以下收到网络数据时的流程：

1. 收到网络数据
1. 网络设备触发IRQ
1. 驱动的IRQ处理程序执行，清除IRQ并运行softIRQ（如果尚未运行）。这里触发的softIRQ是`NET_RX`类型
1. softIRQ本质上作为单独的内核线程，执行NAPI轮询循环
1. 只要有足够的预算，NAPI轮询循环就一直接收数据包
1. 每次处理数据包后，预算都会减少，直到没有更多数据包要处理、预算达到0或时间片已过期为止

在igb（和ixgbe）驱动中，上面的步骤5在处理接收数据之前会先处理发送完成（TX
completion）。请记住，**根据驱动程序的实现，处理发送完成和接收数据的函数可能共享一
份处理预算**。igb和ixgbe驱动程序分别跟踪发送完成和接收数据包的预算，因此处理发送完
成不一定会消耗完RX预算。

也就是说，整个NAPI轮询循环在hard code时间片内运行。这意味着如果要处理大量的TX完成
，TX完成可能会比处理接收数据时占用更多的时间片。对于在高负载环境中运行网络硬
件的人来说，这可能是一个重要的考虑因素。

让我们看看igb驱动程序在实际是如何实现的。

#### 9.4.1 Transmit completion IRQ

收包过程我们已经在[数据接收部分的博客]({% link _posts/2018-12-05-tuning-stack-rx-zh-1.md %})
中介绍过，这里不再赘述，只给出相应链接。

那么，让我们从头开始：

1. 网络设备[启用](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#bringing-a-network-device-up)（bring up）
1. IRQ处理函数完成[注册](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#register-an-interrupt-handler)
1. 用户程序将数据发送到socket。数据穿过网络栈，最后被网络设备从内存中取出并发送
1. 设备完成数据发送并触发IRQ表示发送完成
1. 驱动程序的IRQ处理函数开始[处理中断](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#interrupt-handler)
1. IRQ处理程序调用`napi_schedule`
1. [NAPI代码](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#napi-and-napischedule)触发`NET_RX`类型softirq
1. `NET_RX`类型sofitrq的中断处理函数`net_rx_action`[开始执行](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#network-data-processing-begins)
1. `net_rx_action`函数调用驱动程序注册的[NAPI轮询函数](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#napi-poll-function-and-weight)
1. NAPI轮询函数`igb_poll`[开始运行](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#igbpoll)

poll函数`igb_poll`同时处理接收数据包和发送完成（transmit completion）逻辑。让我
们深入研究这个函数的代码，看看发生了什么。

#### 9.4.2 `igb_poll`

[drivers/net/ethernet/intel/igb/igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L5987-L6018):

```c
/**
 *  igb_poll - NAPI Rx polling callback
 *  @napi: napi polling structure
 *  @budget: count of how many packets we should handle
 **/
static int igb_poll(struct napi_struct *napi, int budget)
{
        struct igb_q_vector *q_vector = container_of(napi,
                                                     struct igb_q_vector,
                                                     napi);
        bool clean_complete = true;

#ifdef CONFIG_IGB_DCA
        if (q_vector->adapter->flags & IGB_FLAG_DCA_ENABLED)
                igb_update_dca(q_vector);
#endif
        if (q_vector->tx.ring)
                clean_complete = igb_clean_tx_irq(q_vector);

        if (q_vector->rx.ring)
                clean_complete &= igb_clean_rx_irq(q_vector, budget);

        /* If all work not completed, return budget and keep polling */
        if (!clean_complete)
                return budget;

        /* If not enough Rx work done, exit the polling mode */
        napi_complete(napi);
        igb_ring_irq_enable(q_vector);

        return 0;
}
```

函数按顺序执行以下操作：

1. 如果在内核中启用了直接缓存访问（[DCA](https://lwn.net/Articles/247493/)）功能
   ，则更新CPU缓存（预热，warm up），后续对RX Ring Buffer的访问将命中CPU缓存。可以在接
   收数据博客的Extras部分中阅读[有关DCA的更多信息](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#direct-cache-access-dca)
1. 调用`igb_clean_tx_irq`执行发送完成操作
1. 调用`igb_clean_rx_irq`处理收到的数据包
1. 最后，检查`clean_complete`变量，判断是否还有更多工作可以完成。如果是，则返
   回预算。如果是这种情况，`net_rx_action`会将此NAPI实例移动到轮询列表的末尾，
   以便稍后再次处理

要了解`igb_clean_rx_irq`如何工作的，请阅读上一篇博客文章的[这一部分
](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#igbcleanrxirq)
。

本文主要关注发送方面，因此我们将继续研究上面的`igb_clean_tx_irq`如何工作。

#### 9.4.3 `igb_clean_tx_irq`

来看一下这个函数的实现，
[drivers/net/ethernet/intel/igb/igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L6020-L6189)。

这个函数有点长，分成几部分来看：

```c
static bool igb_clean_tx_irq(struct igb_q_vector *q_vector)
{
        struct igb_adapter *adapter = q_vector->adapter;
        struct igb_ring *tx_ring = q_vector->tx.ring;
        struct igb_tx_buffer *tx_buffer;
        union e1000_adv_tx_desc *tx_desc;
        unsigned int total_bytes = 0, total_packets = 0;
        unsigned int budget = q_vector->tx.work_limit;
        unsigned int i = tx_ring->next_to_clean;

        if (test_bit(__IGB_DOWN, &adapter->state))
                return true;
```

该函数首先初始化一些变量，其中比较重要的是预算（变量`budget`）
，初始化为此队列的`tx.work_limit`。在igb驱动程序中，`tx.work_limit`初始化为hard
code值`IGB_DEFAULT_TX_WORK`（128）。

值得注意的是，虽然我们现在看到的TX完成代码与RX处理在同一个`NET_RX` softirq中运行
，但igb驱动的TX和RX函数**不共享处理预算**。由于整个轮询函数在同一时间片内运行，因此
每次`igb_poll`运行不会出现RX或TX饥饿，只要调用`igb_poll`，两者都将被处理。

继续前进，代码检查网络设备是否已关闭。如果是，则返回`true`并退出`igb_clean_tx_irq`。

```c
        tx_buffer = &tx_ring->tx_buffer_info[i];
        tx_desc = IGB_TX_DESC(tx_ring, i);
        i -= tx_ring->count;
```

接下来：

1. `tx_buffer`变量初始化为`tx_ring->next_to_clean`（其本身被初始化为0）
1. `tx_desc`变量初始化为相关描述符的指针
1. 计数器`i`减去TX Queue的大小。可以调整此值（我们将在调优部分中看到），但初始化为`IGB_DEFAULT_TXD`（256）

接下来，循环开始。它包含一些有用的注释，用于解释每个步骤中发生的情况：

```c
        do {
                union e1000_adv_tx_desc *eop_desc = tx_buffer->next_to_watch;

                /* if next_to_watch is not set then there is no work pending */
                if (!eop_desc)
                        break;

                /* prevent any other reads prior to eop_desc */
                read_barrier_depends();

                /* if DD is not set pending work has not been completed */
                if (!(eop_desc->wb.status & cpu_to_le32(E1000_TXD_STAT_DD)))
                        break;

                /* clear next_to_watch to prevent false hangs */
                tx_buffer->next_to_watch = NULL;

                /* update the statistics for this packet */
                total_bytes += tx_buffer->bytecount;
                total_packets += tx_buffer->gso_segs;

                /* free the skb */
                dev_kfree_skb_any(tx_buffer->skb);

                /* unmap skb header data */
                dma_unmap_single(tx_ring->dev,
                                 dma_unmap_addr(tx_buffer, dma),
                                 dma_unmap_len(tx_buffer, len),
                                 DMA_TO_DEVICE);

                /* clear tx_buffer data */
                tx_buffer->skb = NULL;
                dma_unmap_len_set(tx_buffer, len, 0);
```

1. 首先将`eop_desc`（eop = end of packet）设置为发送缓冲区`tx_buffer`的`next_to_watch`，后者是在我们之前看到的发送代码中设置的
1. 如果`eop_desc`为`NULL`，则表示没有待处理的工作
1. 接下来调用`read_barrier_depends`函数，该函数执行此CPU体系结构相关的指令，通过屏障防止其他任何读操作
1. 接下来，检查描述符`eop_desc`的状态位。如果`E1000_TXD_STAT_DD`未设置，则表示发送尚未完成，因此跳出循环
1. 清除`tx_buffer->next_to_watch`。驱动中的watchdog定时器将监视此字段以判断发送是否hang住。清除此字段将不会触发watchdog
1. 统计发送的总字节数和包数，这些计数将被复制到驱动的相应计数中
1. 释放skb
1. 调用`dma_unmap_single`取消skb数据区映射
1. `tx_buffer->skb`设置为`NULL`，解除`tx_buffer`映射

接下来，在上面的循环内部开始了另一个循环：

```c
                /* clear last DMA location and unmap remaining buffers */
                while (tx_desc != eop_desc) {
                        tx_buffer++;
                        tx_desc++;
                        i++;
                        if (unlikely(!i)) {
                                i -= tx_ring->count;
                                tx_buffer = tx_ring->tx_buffer_info;
                                tx_desc = IGB_TX_DESC(tx_ring, 0);
                        }

                        /* unmap any remaining paged data */
                        if (dma_unmap_len(tx_buffer, len)) {
                                dma_unmap_page(tx_ring->dev,
                                               dma_unmap_addr(tx_buffer, dma),
                                               dma_unmap_len(tx_buffer, len),
                                               DMA_TO_DEVICE);
                                dma_unmap_len_set(tx_buffer, len, 0);
                        }
                }
```

这个内层循环会遍历每个发送描述符，直到`tx_desc`等于`eop_desc`，并会解除被其他描
述符引用的被DMA映射的数据。

外层循环继续：

```c
                /* move us one more past the eop_desc for start of next pkt */
                tx_buffer++;
                tx_desc++;
                i++;
                if (unlikely(!i)) {
                        i -= tx_ring->count;
                        tx_buffer = tx_ring->tx_buffer_info;
                        tx_desc = IGB_TX_DESC(tx_ring, 0);
                }

                /* issue prefetch for next Tx descriptor */
                prefetch(tx_desc);

                /* update budget accounting */
                budget--;
        } while (likely(budget));
```

外层循环递增迭代器，更新budget，然后检查是否要进入下一次循环。

```c
        netdev_tx_completed_queue(txring_txq(tx_ring),
                                  total_packets, total_bytes);
        i += tx_ring->count;
        tx_ring->next_to_clean = i;
        u64_stats_update_begin(&tx_ring->tx_syncp);
        tx_ring->tx_stats.bytes += total_bytes;
        tx_ring->tx_stats.packets += total_packets;
        u64_stats_update_end(&tx_ring->tx_syncp);
        q_vector->tx.total_bytes += total_bytes;
        q_vector->tx.total_packets += total_packets;
```

这段代码：

1. 调用`netdev_tx_completed_queue`，它是上面解释的DQL API的一部分。如果处理了足够的发送完成，这可能会重新启用TX Queue
1. 更新各处的统计信息，以便用户可以访问它们，我们稍后会看到

代码继续，首先检查是否设置了`IGB_RING_FLAG_TX_DETECT_HANG`标志。每次运行定时器
回调函数时，watchdog定时器都会设置此标志，以强制定期检查TX Queue。如果该标志被设
置了，则代码将检查TX Queue是否hang住：

```c
        if (test_bit(IGB_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags)) {
                struct e1000_hw *hw = &adapter->hw;

                /* Detect a transmit hang in hardware, this serializes the
                 * check with the clearing of time_stamp and movement of i
                 */
                clear_bit(IGB_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags);
                if (tx_buffer->next_to_watch &&
                    time_after(jiffies, tx_buffer->time_stamp +
                               (adapter->tx_timeout_factor * HZ)) &&
                    !(rd32(E1000_STATUS) & E1000_STATUS_TXOFF)) {

                        /* detected Tx unit hang */
                        dev_err(tx_ring->dev,
                                "Detected Tx Unit Hang\n"
                                "  Tx Queue             <%d>\n"
                                "  TDH                  <%x>\n"
                                "  TDT                  <%x>\n"
                                "  next_to_use          <%x>\n"
                                "  next_to_clean        <%x>\n"
                                "buffer_info[next_to_clean]\n"
                                "  time_stamp           <%lx>\n"
                                "  next_to_watch        <%p>\n"
                                "  jiffies              <%lx>\n"
                                "  desc.status          <%x>\n",
                                tx_ring->queue_index,
                                rd32(E1000_TDH(tx_ring->reg_idx)),
                                readl(tx_ring->tail),
                                tx_ring->next_to_use,
                                tx_ring->next_to_clean,
                                tx_buffer->time_stamp,
                                tx_buffer->next_to_watch,
                                jiffies,
                                tx_buffer->next_to_watch->wb.status);
                        netif_stop_subqueue(tx_ring->netdev,
                                            tx_ring->queue_index);

                        /* we are about to reset, no point in enabling stuff */
                        return true;
                }
```

上面的if语句检查：

1. `tx_buffer->next_to_watch`已设置，并且
1. 当前jiffies大于`tx_buffer`发送路径上记录的`time_stamp`加上超时因子，并且
1. 设备的发送状态寄存器未设置`E1000_STATUS_TXOFF`

如果这三个条件都为真，则会打印一个错误，表明已检测到挂起。`netif_stop_subqueue`
用于关闭队列，最后函数返回true。

让我们继续阅读代码，看看如果没有发送挂起检查会发生什么，或者如果有，但没有检测到
挂起：

```c
#define TX_WAKE_THRESHOLD (DESC_NEEDED * 2)
        if (unlikely(total_packets &&
            netif_carrier_ok(tx_ring->netdev) &&
            igb_desc_unused(tx_ring) >= TX_WAKE_THRESHOLD)) {
                /* Make sure that anybody stopping the queue after this
                 * sees the new next_to_clean.
                 */
                smp_mb();
                if (__netif_subqueue_stopped(tx_ring->netdev,
                                             tx_ring->queue_index) &&
                    !(test_bit(__IGB_DOWN, &adapter->state))) {
                        netif_wake_subqueue(tx_ring->netdev,
                                            tx_ring->queue_index);

                        u64_stats_update_begin(&tx_ring->tx_syncp);
                        tx_ring->tx_stats.restart_queue++;
                        u64_stats_update_end(&tx_ring->tx_syncp);
                }
        }

        return !!budget;
```

在上面的代码中，如果先前已禁用，则驱动程序将重新启动TX Queue。
它首先检查：

1. 是否有数据包处理完成（`total_packets`非零）
1. 调用`netif_carrier_ok`，确保设备没有被关闭
1. TX Queue中未使用的描述符数量大于等于`TX_WAKE_THRESHOLD`（我的x86_64系统上此阈值为42）

如果满足以上所有条件，则执行**写屏障**（`smp_mb`）。

接下来检查另一组条件。如果：

1. 队列停止了
1. 设备未关闭

则调用`netif_wake_subqueue`唤醒TX Queue，并向更高层发信号通知它们可能需要将数据
再次入队。`restart_queue`统计计数器递增。我们接下来会看到如何阅读这个值。

最后，返回一个布尔值。如果有任何剩余的未使用预算，则返回true，否则为false。在
`igb_poll`中检查此值以确定返回`net_rx_action`的内容。

#### 9.4.4 `igb_poll`返回值

`igb_poll`函数通过以下逻辑决定返回什么值给`net_rx_action`：

```c
        if (q_vector->tx.ring)
                clean_complete = igb_clean_tx_irq(q_vector);

        if (q_vector->rx.ring)
                clean_complete &= igb_clean_rx_irq(q_vector, budget);

        /* If all work not completed, return budget and keep polling */
        if (!clean_complete)
                return budget;
```

换句话说，如果：

1. `igb_clean_tx_irq`清除了所有**待发送**数据包，且未用完其TX预算（transmit
   completion budget），并且
1. `igb_clean_rx_irq`清除了所有**接收到的**数据包，且未用完其RX预算（packet
   processing budget）

那么，最后将返回整个预算值（包括igb在内的大多数驱动程序hard code为64）；否则，如果
RX或TX处理中的任何用完了其budget（因为还有更多工作要做），则调用`napi_complete`
禁用NAPI并返回0：

```c
        /* If not enough Rx work done, exit the polling mode */
        napi_complete(napi);
        igb_ring_irq_enable(q_vector);

        return 0;
}
```

### 9.5 监控网络设备

监控网络设备有多种方式，每种方式提供的监控粒度和复杂度各不相同。我们先从最粗
大粒度开始，然后逐步到最细的粒度。

#### 9.5.1 使用`ethtool -S`命令

Ubuntu安装ethtool：

```shell
$ sudo apt-get install ethtool.
```

`ethtool -S <NIC>`可以打印设备的收发统计信息（例如，发送错误）：

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

监控这个数据不是太容易，因为并无统一的标准规定`-S`应该打印出哪些字段。不同的设备
，甚至是相同设备的不同版本，都可能打印出名字不同但意思相同的字段。

你首先需要检查里面的“drop”、“buffer”、“miss”、“errors”等字段，然后查看驱动程序的
代码，以确定哪些计数是在软件里更新的（例如，内存不足时更新），哪些是直接来自硬件
寄存器更新的。如果是硬件寄存器值，那你需要查看网卡的data sheet，确定这个计数真正
表示什么，因为ethtool给出的很多字段都是有误导性的（misleading）。

#### 9.5.2 使用`sysfs`

sysfs也提供了很多统计值，但比网卡层的统计更上层一些。

例如，你可以通过`cat <file>`的方式查看eth0接收的丢包数。

示例：

```shell
$ cat /sys/class/net/eth0/statistics/tx_aborted_errors
2
```

每个counter对应一个文件，包括`tx_aborted_errors`, `tx_carrier_errors`, `tx_compressed`, `tx_dropped`,等等。

**不幸的是，每个值代表什么是由驱动决定的，因此，什么时候更新它们，在什么条件下更新
，都是驱动决定的。**例如，你可能已经注意到，对于同一种错误，有的驱动将其视为drop
，而有的驱动将其视为miss。

如果这些值对你非常重要，那你必须阅读驱动代码和网卡data sheet，以确定每个值真正代
表什么。

#### 9.5.3 使用`/proc/net/dev`

`/proc/net/dev`提供了更高一层的统计，它给系统中的每个网络设备一个统计摘要。

```shell
$ cat /proc/net/dev
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
  eth0: 110346752214 597737500    0    2    0     0          0  20963860 990024805984 6066582604    0    0    0     0       0          0
    lo: 428349463836 1579868535    0    0    0     0          0         0 428349463836 1579868535    0    0    0     0       0          0
```

这里打印出来的字段是上面sysfs里字段的一个子集，可以作为通用general reference。

上面的建议在这里同样适用，即：
如果这些值对你非常重要，那你必须阅读驱动代码和网卡data sheet，以确定每个值真正代
表什么。

### 9.6 监控 DQL

可以通过`/sys/class/net/<NIC>/queues/tx-<QUEUE_ID>/byte_queue_limits/`
监控网络设备的动态队列限制（DQL）信息。

```shell
$ cat /sys/class/net/eth0/queues/tx-0/byte_queue_limits/inflight
350
```

文件包括：

1. `hold_time`: Initialized to HZ (a single hertz). If the queue has been full for hold_time, then the maximum size is decreased.
1. `inflight`: This value is equal to (number of packets queued - number of packets completed). It is the current number of packets being transmit for which a completion has not been processed.
1. `limit_max`: A hardcoded value, set to DQL_MAX_LIMIT (1879048192 on my x86_64 system).
1. `limit_min`: A hardcoded value, set to 0.
1. `limit`: A value between limit_min and limit_max which represents the current maximum number of objects which can be queued.

在修改这些值之前，强烈建议先阅读[这些资料
](https://www.linuxplumbersconf.org/2012/wp-content/uploads/2012/08/bql_slide.pdf)，以更深入地了解其算法。

### 9.7 调优网络设备

#### 9.7.1 查询TX Queue数量

如果网络及其驱动支持多TX Queue，那可以用ethtool调整TX queue（也叫TX channel）的数量。

查看网卡TX Queue数量：

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

这里显示了（由驱动和硬件）预设的最大值，以及当前值。

注意：不是所有设备驱动都支持这个选项。

如果你的网卡不支持，会遇到以下错误：

```shell
$ sudo ethtool -l eth0
Channel parameters for eth0:
Cannot get device channel parameters
: Operation not supported
```

这表示设备驱动没有实现ethtool的`get_channels`方法，这可能是由于网卡不支持调整
queue数量，不支持多TX Queue，或者驱动版本太旧导致不支持此操作。

#### 9.7.2 调整TX queue数量

`ethtool -L`可以修改TX Queue数量。

注意：一些设备及其驱动只支持combined queue，这种情况下一个TX queue和和一个RX queue绑定到一起的。前面的例子中我们已经看到了。

例子：设置收发队列数量为8：

```shell
$ sudo ethtool -L eth0 combined 8
```

如果你的设备和驱动支持分别设置TX queue和RX queue的数量，那你可以分别设置。

```shell
$ sudo ethtool -L eth0 tx 8
```

注意：对于大部分驱动，调整以上设置会导致网卡先down再up，经过这个网卡的连接会断掉
。如果只是一次性改动，那这可能不是太大问题。

#### 9.7.3 调整TX queue大小

一些设备及其驱动支持修改TX queue大小，这是如何实现的取决于具体的硬件，但是，
ethtool提供了一个通用的接口可以调整这个大小。由于DQL在更高层面处理数据排队的问题
，因此调整队列大小可能不会产生明显的影响。然而，你可能还是想要将TX queue调到最大
，然后再把剩下的事情交给DQL：

`ethtool -g`查看队列当前的大小：

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

以上显示硬件支持最大4096个接收和发送描述符，但当前只使用了512个。

`-G`修改queue大小：

```shell
$ sudo ethtool -G eth0 tx 4096
```

注意：对于大部分驱动，调整以上设置会导致网卡先down再up，经过这个网卡的连接会断掉
。如果只是一次性改动，那这可能不是太大问题。

## 10 网络栈之旅：结束

至此，你已经知道关于Linux如何发送数据包的全部内容了：从用户程序直到驱动，以及反
方向。
