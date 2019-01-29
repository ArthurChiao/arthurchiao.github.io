---
layout: post
title:  "[译] Linux网络栈监控和调优：接收数据 3"
date:   2018-12-05
author: ArthurChiao
categories: network-stack monitoring tuning
---

## 9 从`netif_receive_skb`进入协议栈

重新捡起我们前面已经几次提到过的`netif_receive_skb`，这个函数在好几个地方被调用
。两个最重要的地方（前面都看到过了）：

* `napi_skb_finish`：当packet不需要被合并到已经存在的某个GRO flow的时候
* `napi_gro_complete`：协议层提示需要flush当前的flow的时候

提示：`netif_receive_skb`和它调用的函数都运行在软中断处理循环（softirq
processing loop）的上下文中，因此这里的时间会记录到`top`命令看到的`si`或者
`sitime`字段。

`netif_receive_skb`首先会检查用户有没有设置一个接收时间戳选项（sysctl），这个选
项决定在packet在到达backlog queue之前还是之后打时间戳。如果启用，那立即打时间戳
，在RPS之前（CPU和backlog queue绑定）；如果没有启用，那只有在它进入到backlog
queue之后才会打时间戳。如果RPS开启了，那这个选项可以将打时间戳的任务分散个其他
CPU，但会带来一些延迟。

### 9.1 调优: 收包打时间戳（RX packet timestamping）

你可以调整包被收到后，何时给它打时间戳。

关闭收包打时间戳：

```shell
$ sudo sysctl -w net.core.netdev_tstamp_prequeue=0
```

默认是1。

## 10 `netif_receive_skb`

处理完时间戳后，`netif_receive_skb`会根据RPS是否启用来做不同的事情。我们先来看简
单情况，RPS未启用。

### 10.1 不使用RPS（默认）

如果RPS没启用，会调用`__netif_receive_skb`，它做一些bookkeeping工作，进而调用
`__netif_receive_skb_core`，将数据移动到离协议栈更近一步。

`__netif_receive_skb_core`工作的具体细节我们稍后再看，先看一下RPS启用的情况下的
代码调用关系，它也会调到这个函数的。

### 10.2 使用RPS

如果RPS启用了，它会做一些计算，判断使用哪个CPU的backlog queue，这个过程由
`get_rps_cpu`函数完成。
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3199-L3200):

```c
cpu = get_rps_cpu(skb->dev, skb, &rflow);

if (cpu >= 0) {
  ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);
  rcu_read_unlock();
  return ret;
}
```

`get_rps_cpu`会考虑前面提到的RFS和aRFS设置，以此选出一个合适的CPU，通过调用
`enqueue_to_backlog`将数据放到它的backlog queue。

### 10.3 `enqueue_to_backlog`

首先从远端CPU的`struct softnet_data`实例获取backlog queue长度。如果backlog大于
`netdev_max_backlog`，或者超过了flow limit，直接drop，并更新`softnet_data`的drop
统计。注意这是远端CPU的统计。

[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3199-L3200):

```c
    qlen = skb_queue_len(&sd->input_pkt_queue);
    if (qlen <= netdev_max_backlog && !skb_flow_limit(skb, qlen)) {
        if (skb_queue_len(&sd->input_pkt_queue)) {
enqueue:
            __skb_queue_tail(&sd->input_pkt_queue, skb);
            input_queue_tail_incr_save(sd, qtail);
            return NET_RX_SUCCESS;
        }

        /* Schedule NAPI for backlog device */
        if (!__test_and_set_bit(NAPI_STATE_SCHED, &sd->backlog.state)) {
            if (!rps_ipi_queued(sd))
                ____napi_schedule(sd, &sd->backlog);
        }
        goto enqueue;
    }
    sd->dropped++;

    kfree_skb(skb);
    return NET_RX_DROP;
```

`enqueue_to_backlog`被调用的地方很少。在基于RPS处理包的地方，以及`netif_rx`，会
调用到它。大部分驱动都不应该使用`netif_rx`，而应该是用`netif_receive_skb`。如果
你没用到RPS，你的驱动也没有使用`netif_rx`，那增大`backlog`并不会带来益处，因为它
根本没被用到。

注意：检查你的驱动，如果它调用了`netif_receive_skb`，而且你没用RPS，那增大
`netdev_max_backlog`并不会带来任何性能提升，因为没有数据包会被送到
`input_pkt_queue`。

如果`input_pkt_queue`足够小，而flow limit（后面会介绍）也还没达到（或者被禁掉了
），那数据包将会被放到队列。这里的逻辑有点funny，但大致可以归为为：

* 如果backlog是空的：如果远端CPU NAPI实例没有运行，并且IPI没有被加到队列，那就
  触发一个IPI加到队列，然后调用`____napi_schedule`进一步处理
* 如果backlog非空，或者远端CPU NAPI实例正在运行，那就enqueue包

这里使用了`goto`，所以代码看起来有点tricky。

[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3201-L3218):

```c
  if (skb_queue_len(&sd->input_pkt_queue)) {
enqueue:
         __skb_queue_tail(&sd->input_pkt_queue, skb);
         input_queue_tail_incr_save(sd, qtail);
         rps_unlock(sd);
         local_irq_restore(flags);
         return NET_RX_SUCCESS;
 }

 /* Schedule NAPI for backlog device
  * We can use non atomic operation since we own the queue lock
  */
 if (!__test_and_set_bit(NAPI_STATE_SCHED, &sd->backlog.state)) {
         if (!rps_ipi_queued(sd))
                 ____napi_schedule(sd, &sd->backlog);
 }
 goto enqueue;
```

#### Flow limits

RPS在不同CPU之间分发packet，但是，如果一个flow特别大，会出现单个CPU被打爆，而
其他CPU无事可做（饥饿）的状态。因此引入了flow limit特性，放到一个backlog队列的属
于同一个flow的包的数量不能超过一个阈值。这可以保证即使有一个很大的flow在大量收包
，小flow也能得到及时的处理。

检查flow limit的代码，[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3199-L3200)：

```c
if (qlen <= netdev_max_backlog && !skb_flow_limit(skb, qlen)) {
```

默认，flow limit功能是关掉的。要打开flow limit，你需要指定一个bitmap（类似于RPS
的bitmap）。

#### 监控：由于`input_pkt_queue`打满或flow limit导致的丢包

在`/proc/net/softnet_stat`里面的dropped列计数，包含本节提到的原因导致的drop。

#### 调优

##### Tuning: Adjusting netdev_max_backlog to prevent drops

在调整这个值之前，请先阅读前面的“注意”。

如果你使用了RPS，或者你的驱动调用了`netif_rx`，那增加`netdev_max_backlog`可以改
善在`enqueue_to_backlog`里的丢包：

例如：increase backlog to 3000 with sysctl.

```shell
$ sudo sysctl -w net.core.netdev_max_backlog=3000
```

默认值是1000。

##### Tuning: Adjust the NAPI weight of the backlog poll loop

`net.core.dev_weight`决定了backlog poll loop可以消耗的整体budget（参考前面更改
`net.core.netdev_budget`的章节）：

```shell
$ sudo sysctl -w net.core.dev_weight=600
```

默认值是64。

记住，backlog处理逻辑和设备驱动的`poll`函数类似，都是在软中断（softirq）的上下文
中执行，因此受整体budget和处理时间的限制，前面已经分析过了。

##### Tuning: Enabling flow limits and tuning flow limit hash table size

```shell
$ sudo sysctl -w net.core.flow_limit_table_len=8192
```

默认值是4096.

这只会影响新分配的flow hash table。所以，如果你想增加table size的话，应该在打开
flow limit功能之前设置这个值。

打开flow limit功能的方式是，在`/proc/sys/net/core/flow_limit_cpu_bitmap`中指定一
个bitmask，和通过bitmask打开RPS的操作类似。

### 10.3 处理backlog队列：NAPI poller

每个CPU都有一个backlog queue，其加入到NAPI实例的方式和驱动差不多，都是注册一个
`poll`方法，在软中断的上下文中处理包。此外，还提供了一个`weight`，这也和驱动类似
。

注册发生在网络系统初始化的时候。

[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L6952-L6955)的`net_dev_init`函数：

```
sd->backlog.poll = process_backlog;
sd->backlog.weight = weight_p;
sd->backlog.gro_list = NULL;
sd->backlog.gro_count = 0;
```

backlog NAPI实例和设备驱动NAPI实例的不同之处在于，它的weight是可以调节的，而设备
驱动是hardcode 64。在下面的调优部分，我们会看到如何用sysctl调整这个设置。

### 10.4 `process_backlog`

`process_backlog`是一个循环，它会一直运行直至`weight`（前面介绍了）用完，或者
backlog里没有数据了。

backlog queue里的数据取出来，传递给`__netif_receive_skb`。这个函数做的事情和RPS
关闭的情况下做的事情一样。即，`__netif_receive_skb`做一些bookkeeping工作，然后调
用`__netif_receive_skb_core`将数据发送给更上面的协议层。

`process_backlog`和NAPI之间遵循的合约，和驱动和NAPI之间的合约相同：NAPI is
disabled if the total weight will not be used. The poller is restarted with the
call to `____napi_schedule` from `enqueue_to_backlog` as described above.

函数返回接收完成的数据帧数量（在代码中是变量`work`），`net_rx_action`（前面介绍了
）将会从budget（通过`net.core.netdev_budget`可以调整，前面介绍了）里减去这个值。

### 10.5 `__netif_receive_skb_core`：将数据送到抓包点（tap）或协议层

`__netif_receive_skb_core`完成将数据送到协议栈的繁重工作（the heavy lifting of
delivering the data)。在它做这件事之前，会先检查是否有packet tap（探测点）插入，
这些tap用于抓包。例如，`AF_PACKET`地址族可以做这种事情，一般通过`libpcap`这个库
。

如果存在抓包点（tap），数据就先发送到那里，然后才送到协议层。

### 10.6 送到抓包点（tap）

如果有packet tap（通常通过`libpcap`），packet会送到那里。
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3548-L3554):

```c
list_for_each_entry_rcu(ptype, &ptype_all, list) {
  if (!ptype->dev || ptype->dev == skb->dev) {
    if (pt_prev)
      ret = deliver_skb(skb, pt_prev, orig_dev);
    pt_prev = ptype;
  }
}
```

如何你对packet如何经过pcap有兴趣，可以阅读[net/packet/af_packet.c](https://github.com/torvalds/linux/blob/v3.13/net/packet/af_packet.c)。

### 10.7 送到协议层

处理完taps之后，`__netif_receive_skb_core`将数据发送到协议层。它会从数据包中取出
协议信息，然后遍历注册在这个协议上的回调函数列表。

可以看`__netif_receive_skb_core`函数，[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3548-L3554):

```c
type = skb->protocol;
list_for_each_entry_rcu(ptype,
                &ptype_base[ntohs(type) & PTYPE_HASH_MASK], list) {
        if (ptype->type == type &&
            (ptype->dev == null_or_dev || ptype->dev == skb->dev ||
             ptype->dev == orig_dev)) {
                if (pt_prev)
                        ret = deliver_skb(skb, pt_prev, orig_dev);
                pt_prev = ptype;
        }
}
```

上面的`ptype_base`是一个hash table，定义在[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L146)中:

```c
struct list_head ptype_base[PTYPE_HASH_SIZE] __read_mostly;
```

每种协议在上面的hash table的一个slot里，添加一个过滤器到列表里。这个列表的头用如
下函数获取：

```c
static inline struct list_head *ptype_head(const struct packet_type *pt)
{
        if (pt->type == htons(ETH_P_ALL))
                return &ptype_all;
        else
                return &ptype_base[ntohs(pt->type) & PTYPE_HASH_MASK];
}
```

添加的时候用`dev_add_pack`这个函数。这就是协议层如何注册自身，用于处理相应协议的
网络数据的。

现在，你已经知道了数据是如何从卡进入到协议层的了。
