---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 6"
date:   2018-12-17
author: ArthurChiao
categories: network-stack monitoring tuning
---

## 8 Queuing Disciplines（排队规则）

至此，我们需要先查看一些qdisc代码。本文不打算涵盖发送队列的每个选项的具体细节。
如果你对此感兴趣，可以查看[这篇](http://lartc.org/howto/index.html)很棒的指南。

我们接下来将查看**通用的数据包调度程序**（generic packet scheduler）是如何工作的
。特别地，我们将研究`qdisc_run_begin`、`qdisc_run_end`、`__ qdisc_run`和
`sch_direct_xmit`是如何将数据移动到更靠近驱动程序的地方，以进行发送的。

让我们从`qdisc_run_begin`的工作原理开始。

### 8.1 `qdisc_run_begin` and `qdisc_run_end`

定义在[include/net/sch_generic.h](https://github.com/torvalds/linux/blob/v3.13/include/net/sch_generic.h#L101-L107):

```c
static inline bool qdisc_run_begin(struct Qdisc *qdisc)
{
        if (qdisc_is_running(qdisc))
                return false;
        qdisc->__state |= __QDISC___STATE_RUNNING;
        return true;
}
```

这个函数很简单：检查qdisc的`__state`字段是否设置了`__QDISC___STATE_RUNNING`标记
位。如果设置了，直接返回`false`；否则，标记位置1，然后返回`true`。

类似地， `qdisc_run_end`执行相反的操作，将此标记位置0：

```c
static inline void qdisc_run_end(struct Qdisc *qdisc)
{
        qdisc->__state &= ~__QDISC___STATE_RUNNING;
}
```

需要注意的是，这两个函数都只是设置标记位，并没有真正处理数据。真正的处理过程是从
`__qdisc_run`开始的。

### 8.2 `__qdisc_run`

这个函数乍看非常简单，甚至让人产生错觉：

```c
void __qdisc_run(struct Qdisc *q)
{
        int quota = weight_p;

        while (qdisc_restart(q)) {
                /*
                 * Ordered by possible occurrence: Postpone processing if
                 * 1. we've exceeded packet quota
                 * 2. another process needs the CPU;
                 */
                if (--quota <= 0 || need_resched()) {
                        __netif_schedule(q);
                        break;
                }
        }

        qdisc_run_end(q);
}
```

函数首先获取`weight_p`，这个变量通常是通过sysctl设置的，收包路径也会用到。我们稍
后会看到如何调整此值。这个循环做两件事：

1. 在`while`循环中调用`qdisc_restart`，直到它返回`false`（或触发下面的中断）
1. 判断quota是否小于等于零，或`need_resched()`是否返回`true`。其中任何一个为真，
   将调用`__netif_schedule`然后跳出循环

注意：用户程序调用`sendmsg`系统调用之后，**内核便接管了执行过程，一路执行到这里;
用户程序一直在累积系统时间（system time）**。如果用户程序在内核中用完其时间quota
，`need_resched`将返回`true`。 如果仍有可用quota，且用户程序的时间片尚未使用，则
将再次调用`qdisc_restart`。

让我们先来看看`qdisc_restart(q)`是如何工作的，然后将深入研究`__netif_schedule(q)`。

### 8.3 `qdisc_restart`

[qdisc_restart](https://github.com/torvalds/linux/blob/v3.13/net/sched/sch_generic.c#L156-L192):

```c
/*
 * NOTE: Called under qdisc_lock(q) with locally disabled BH.
 *
 * __QDISC_STATE_RUNNING guarantees only one CPU can process
 * this qdisc at a time. qdisc_lock(q) serializes queue accesses for
 * this queue.
 *
 *  netif_tx_lock serializes accesses to device driver.
 *
 *  qdisc_lock(q) and netif_tx_lock are mutually exclusive,
 *  if one is grabbed, another must be free.
 *
 * Note, that this procedure can be called by a watchdog timer
 *
 * Returns to the caller:
 *                                0  - queue is empty or throttled.
 *                                >0 - queue is not empty.
 *
 */
static inline int qdisc_restart(struct Qdisc *q)
{
        struct netdev_queue *txq;
        struct net_device *dev;
        spinlock_t *root_lock;
        struct sk_buff *skb;

        /* Dequeue packet */
        skb = dequeue_skb(q);
        if (unlikely(!skb))
                return 0;
        WARN_ON_ONCE(skb_dst_is_noref(skb));
        root_lock = qdisc_lock(q);
        dev = qdisc_dev(q);
        txq = netdev_get_tx_queue(dev, skb_get_queue_mapping(skb));

        return sch_direct_xmit(skb, q, dev, txq, root_lock);
}
```

`qdisc_restart`函数开头的注释非常有用，描述了此函数用到的三个锁：

1. `__QDISC_STATE_RUNNING`保证了同一时间只有一个CPU可以处理这个qdisc
1. `qdisc_lock(q)`将访问此qdisc的操作顺序化
1. `netif_tx_lock`将访问设备驱动的操作顺序化

函数首先通过`dequeue_skb`从qdisc中取出下一个要发送的skb。如果队列为空，
`qdisc_restart`将返回`false`（导致上面`__qdisc_run`中的循环退出）。

如果skb不为空，代码将获取qdisc队列锁，然后拿到相关的发送设备`dev`和发送队列`txq`
，最后带着这些参数调用`sch_direct_xmit`。

我们来先看`dequeue_skb`，然后再回到`sch_direct_xmit`。

#### 8.3.1 `dequeue_skb`

我们来看看定义在 [net/sched/sch_generic.c](https://github.com/torvalds/linux/blob/v3.13/net/sched/sch_generic.c#L59-L78)中的`dequeue_skb`。

函数首先声明一个`struct sk_buff * `类型的局部变量`skb`，用这个变量表示接下来要处
理的数据。这个变量后面会依的不同情况而被赋不同的值，最后作为函数的返回值被返回给
调用方。

变量`skb`被初始化为qdisc的`gso_skb`成员变量（`q->gso_skb`），后者指向之前由于无
法发送而重新入队的数据。

接下来分为两种情况，根据`q->gso_skb`是否为空：

1. 如果不为空，会将之前重新入队的skb出队，作为待处理数据返回
1. 如果为空，则从要处理的qdisc中取出一个新skb，作为待处理数据返回

先看第一种情况，如果`q->gso_skb`不为空：

```c
static inline struct sk_buff *dequeue_skb(struct Qdisc *q)
{
        struct sk_buff *skb = q->gso_skb;
        const struct netdev_queue *txq = q->dev_queue;

        if (unlikely(skb)) {
                /* check the reason of requeuing without tx lock first */
                txq = netdev_get_tx_queue(txq->dev, skb_get_queue_mapping(skb));
                if (!netif_xmit_frozen_or_stopped(txq)) {
                        q->gso_skb = NULL;
                        q->q.qlen--;
                } else
                        skb = NULL;
```

代码将检查数据的发送队列是否已停止。如果队列未停止工作，则清除`gso_skb`字段，并
将队列长度减1。如果队列停止工作，则数据扔留在gso_skb，但将局部变量`skb`置空。

第二种情况，如果`q->gso_skb`为空，即之前没有数据被重新入队：

```c
        } else {
                if (!(q->flags & TCQ_F_ONETXQUEUE) || !netif_xmit_frozen_or_stopped(txq))
                        skb = q->dequeue(q);
        }

        return skb;
}
```

进入另一个tricky的if语句，如果：

1. qdisc不是单发送队列，或
1. 发送队列未停止工作

则将调用qdisc的`dequeue`函数以获取新数据，赋值给局部变量`skb`。dequeue的内部实现
依qdisc的实现和功能而有所不同。

该函数最后返回局部变量`skb`，这是接下来要处理的数据包。

#### 8.3.2 `sch_direct_xmit`

现在来到`sch_direct_xmit`（定义在
[net/sched/sch_generic.c](https://github.com/torvalds/linux/blob/v3.13/net/sched/sch_generic.c#L109-L154)
），这是将数据向下发送到网络设备的重要参与者。

我们一段一段地看：

```c
/*
 * Transmit one skb, and handle the return status as required. Holding the
 * __QDISC_STATE_RUNNING bit guarantees that only one CPU can execute this
 * function.
 *
 * Returns to the caller:
 *                                0  - queue is empty or throttled.
 *                                >0 - queue is not empty.
 */
int sch_direct_xmit(struct sk_buff *skb, struct Qdisc *q,
                    struct net_device *dev, struct netdev_queue *txq,
                    spinlock_t *root_lock)
{
        int ret = NETDEV_TX_BUSY;

        /* And release qdisc */
        spin_unlock(root_lock);

        HARD_TX_LOCK(dev, txq, smp_processor_id());
        if (!netif_xmit_frozen_or_stopped(txq))
                ret = dev_hard_start_xmit(skb, dev, txq);

        HARD_TX_UNLOCK(dev, txq);
```

这段代码首先释放qdisc（发送队列）锁，然后获取（设备驱动的）发送锁。注意
`HARD_TX_LOCK`这个宏：

```c
#define HARD_TX_LOCK(dev, txq, cpu) {                   \
        if ((dev->features & NETIF_F_LLTX) == 0) {      \
                __netif_tx_lock(txq, cpu);              \
        }                                               \
}
```

这个宏检查设备是否设置了`NETIF_F_LLTX` flag，这个flag已经弃用，不推荐使用，新设
备驱动程序不应使用此标志。内核中的大多数驱动程序也不使用此标志，因此这个if语句
将为`true`，接下来获取此数据的发送队列的锁。

接下来，如果发送队列没有停止，就会调用`dev_hard_start_xmit`，并保存其返回值，以
确定发送是否成功。正如我们稍后将看到的，`dev_hard_start_xmit`会将数据从Linux内核
的网络设备子系统发送到设备驱动程序。

`dev_hard_start_xmit`执行之后，（或因发送队列停止而跳过执行），队列的发送锁定就会被释放。

让我们继续：

```c
        spin_lock(root_lock);

        if (dev_xmit_complete(ret)) {
                /* Driver sent out skb successfully or skb was consumed */
                ret = qdisc_qlen(q);
        } else if (ret == NETDEV_TX_LOCKED) {
                /* Driver try lock failed */
                ret = handle_dev_cpu_collision(skb, txq, q);
```

接下来，再次获取此qdisc的锁，然后通过调用`dev_xmit_complete`检查`dev_hard_start_xmit`的返回值。


1. 如果`dev_xmit_complete`返回`true`，数据已成功发送，则将qdisc队列长度设置为返回值，否则
1. 如果`dev_hard_start_xmit`返回的是`NETDEV_TX_LOCKED`，调用`handle_dev_cpu_collision`来处理锁竞争

当驱动程序尝试锁定发送队列并失败时，支持`NETIF_F_LLTX`功能的设备可以返回`NETDEV_TX_LOCKED`。 
我们稍后会仔细研究`handle_dev_cpu_collision`。

现在，让我们继续关注`sch_direct_xmit`并查看，以上两种情况都不满足时的情况：

```c
        } else {
                /* Driver returned NETDEV_TX_BUSY - requeue skb */
                if (unlikely(ret != NETDEV_TX_BUSY))
                        net_warn_ratelimited("BUG %s code %d qlen %d\n",
                                             dev->name, ret, q->q.qlen);

                ret = dev_requeue_skb(skb, q);
        }
```

如果发送失败，而且不是以上两种情况，那还有第三种可能：由于`NETDEV_TX_BUSY`。驱动
程序可以返回`NETDEV_TX_BUSY`表示设备或驱动程序“忙”并且数据现在无法发送。在这种情
况下，通过调用`dev_requeue_skb`将数据重新入队，等下次继续发送。

最后，函数（可能）调整返回值，然后返回：

```c
        if (ret && netif_xmit_frozen_or_stopped(txq))
                ret = 0;

        return ret;
```

我们来深入地看一下`handle_dev_cpu_collision`和`dev_requeue_skb`。

#### 8.3.3 `handle_dev_cpu_collision`

定义在 [net/sched/sch_generic.c](https://github.com/torvalds/linux/blob/v3.13/net/sched/sch_generic.c#L80-L107)，处理两种情况：

1. 发送锁由当前CPU保持
1. 发送锁由其他CPU保存

第一种情况认为是配置问题，打印一条警告。

第二种情况，更新统计计数器`cpu_collision`，通过`dev_requeue_skb`将数据重新入队
以便稍后发送。回想一下，我们在`dequeue_skb`中看到了专门处理重新入队的skb的代码。

代码很简短，可以快速阅读：

```c
static inline int handle_dev_cpu_collision(struct sk_buff *skb,
                                           struct netdev_queue *dev_queue,
                                           struct Qdisc *q)
{
        int ret;

        if (unlikely(dev_queue->xmit_lock_owner == smp_processor_id())) {
                /*
                 * Same CPU holding the lock. It may be a transient
                 * configuration error, when hard_start_xmit() recurses. We
                 * detect it by checking xmit owner and drop the packet when
                 * deadloop is detected. Return OK to try the next skb.
                 */
                kfree_skb(skb);
                net_warn_ratelimited("Dead loop on netdevice %s, fix it urgently!\n",
                                     dev_queue->dev->name);
                ret = qdisc_qlen(q);
        } else {
                /*
                 * Another cpu is holding lock, requeue & delay xmits for
                 * some time.
                 */
                __this_cpu_inc(softnet_data.cpu_collision);
                ret = dev_requeue_skb(skb, q);
        }

        return ret;
}
```

接下来看看`dev_requeue_skb`做了什么，后面会看到，`sch_direct_xmit`会调用它.

#### 8.3.4 `dev_requeue_skb`

这个函数很简短，[net/sched/sch_generic.c](https://github.com/torvalds/linux/blob/v3.13/net/sched/sch_generic.c#L39-L57):

```c
/* Modifications to data participating in scheduling must be protected with
 * qdisc_lock(qdisc) spinlock.
 *
 * The idea is the following:
 * - enqueue, dequeue are serialized via qdisc root lock
 * - ingress filtering is also serialized via qdisc root lock
 * - updates to tree and tree walking are only done under the rtnl mutex.
 */

static inline int dev_requeue_skb(struct sk_buff *skb, struct Qdisc *q)
{
        skb_dst_force(skb);
        q->gso_skb = skb;
        q->qstats.requeues++;
        q->q.qlen++;        /* it's still part of the queue */
        __netif_schedule(q);

        return 0;
}
```

这个函数做了一些事情：

1. 在skb上强制增加一次引用计数
1. 将skb添加到qdisc的`gso_skb`字段。回想一下，之前我们看到在从qdisc的队列中取出数据之前，在dequeue_skb中检查了该字段
1. 更新`requque`统计计数
1. 更新队列长度
1. 调用`__netif_schedule`

简单明了。

接下来我们再回忆一遍我们一步步到达这里的过程，然后检查`__netif_schedule`。

### 8.4 Reminder, while loop in `__qdisc_run`

回想一下，我们是在查看`__qdisc_run`的时候到达这里的：

```c
void __qdisc_run(struct Qdisc *q)
{
        int quota = weight_p;

        while (qdisc_restart(q)) {
                /*
                 * Ordered by possible occurrence: Postpone processing if
                 * 1. we've exceeded packet quota
                 * 2. another process needs the CPU;
                 */
                if (--quota <= 0 || need_resched()) {
                        __netif_schedule(q);
                        break;
                }
        }

        qdisc_run_end(q);
}
```

代码在`while`循环中调用`qdisc_restart`，循环内部使skb出队，尝试通过
`sch_direct_xmit`来发送它们，`sch_direct_xmit`调用`dev_hard_start_xmit`来向驱动
程序进行实际发送。任何无法发送的skb都将被重新入队，以便在`NET_TX` softirq中进行
发送。

发送过程的下一步是查看`dev_hard_start_xmit`，了解如何调用驱动程序来发送数据。但
在此之前，我们应该先查看`__netif_schedule`以完全理解`__qdisc_run`和
`dev_requeue_skb`的工作方式。

#### 8.4.1 `__netif_schedule`

现在我们来看`__netif_schedule`，
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L2127-L2146):

```c
void __netif_schedule(struct Qdisc *q)
{
        if (!test_and_set_bit(__QDISC_STATE_SCHED, &q->state))
                __netif_reschedule(q);
}
EXPORT_SYMBOL(__netif_schedule);
```

此代码检查qdisc状态中的`__QDISC_STATE_SCHED`位，如果为该位为0，会将其置1。如果置
位成功（意味着之前不在`__QDISC_STATE_SCHED`状态），代码将调用
`__netif_reschedule`，这个函数不长，但做的事情非常重要。让我们来看看：

```c
static inline void __netif_reschedule(struct Qdisc *q)
{
        struct softnet_data *sd;
        unsigned long flags;

        local_irq_save(flags);
        sd = &__get_cpu_var(softnet_data);
        q->next_sched = NULL;
        *sd->output_queue_tailp = q;
        sd->output_queue_tailp = &q->next_sched;
        raise_softirq_irqoff(NET_TX_SOFTIRQ);
        local_irq_restore(flags);
}
```

这个函数做了几件事：

1. 保存当前的硬中断（IRQ）状态，并通过调用`local_irq_save`禁用IRQ
1. 获取当前CPU的`struct softnet_data`实例
1. 将qdisc添加到`struct softnet_data`实例的output队列中
1. 触发`NET_TX_SOFTIRQ`类型软中断（softirq）
1. 恢复IRQ状态并重新启用硬中断

你可以阅读我们之前关于网络栈接收数据的[文章](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#linux-network-device-subsystem)，了解更多有关`softnet_data`初始化的信息。

这个函数中的重要代码是：`raise_softirq_irqoff`，它触发`NET_TX_SOFTIRQ`类型
softirq。 softirqs及其注册也包含在我们之前的[文章
](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#softirqs)
中。简单来说，你可以认为softirqs是以非常高的优先级在执行的内核线程，并代表内核处
理数据，它们用于网络数据的收发处理（incoming和outgoing）。

正如在[上一篇]()文章中看到的，`NET_TX_SOFTIRQ` softirq有一个注册的回调函数
`net_tx_action`，这意味着有一个内核线程将会执行`net_tx_action`。该线程偶尔会被暂
停（pause），`raise_softirq_irqoff`会恢复（resume）其执行。让我们看一下
`net_tx_action`的作用，以便了解内核如何处理发送数据请求。

#### 8.4.2 `net_tx_action`

定义在
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3297-L3353)
，由两个if组成，分别处理executing CPU的`softnet_data`实例的两个queue：

1. completion queue
1. output queue

让我们分别来看这两种情况，注意，**这段代码在softirq上下文中作为一个独立的内核线
程执行**。网络栈发送侧的热路径中不适合执行的代码，将被延后（defer），然
后由执行`net_tx_action`的线程处理。

##### 8.4.3 net_tx_action completion queue

`softnet_data`的completion queue存放等待释放的skb。函数`dev_kfree_skb_irq`可以将
skbs添加到队列中以便稍后释放。 设备驱动程序通常使用它来推迟释放消耗的skbs。 驱动
程序想要推迟释放skb而不是直接释放的原因是，释放内存可能需要时间，而且有些代码（如hardirq处理程序）
需要尽可能快的执行并返回。

看一下`net_tx_action`第一段代码，该代码处理completion queue中等待释放的skb：

```c
        if (sd->completion_queue) {
                struct sk_buff *clist;

                local_irq_disable();
                clist = sd->completion_queue;
                sd->completion_queue = NULL;
                local_irq_enable();

                while (clist) {
                        struct sk_buff *skb = clist;
                        clist = clist->next;

                        WARN_ON(atomic_read(&skb->users));
                        trace_kfree_skb(skb, net_tx_action);
                        __kfree_skb(skb);
                }
        }
```

如果completion queue非空，`while`循环将遍历这个列表并`__kfree_skb`释放每个skb占
用的内存。**牢记，此代码在一个名为softirq的独立“线程”中运行 - 它并没有占用用
户程序的系统时间（system time）**。

##### 8.4.4 net_tx_action output queue

output queue存储待发送skb。如前所述，`__netif_reschedule`将数据添加到output
queue中，通常从`__netif_schedule`调用过来。目前，`__netif_schedule`函数在我们
看到的两个地方被调用：

1. `dev_requeue_skb`：正如我们所看到的，如果驱动程序返回`NETDEV_TX_BUSY`或者存在
   CPU冲突，则可以调用此函数
1. `__qdisc_run`：我们之前也看过这个函数。 一旦超出quota或者需要reschedule，它也
   会调用`__netif_schedule`

在任何一种情况下，都会调用`__netif_schedule`函数，它会将qdisc添加到softnet_data
的output queue进行处理。 这里将输出队列处理代码拆分为三个块。我们来看看第一块：

```c
        if (sd->output_queue) {
                struct Qdisc *head;

                local_irq_disable();
                head = sd->output_queue;
                sd->output_queue = NULL;
                sd->output_queue_tailp = &sd->output_queue;
                local_irq_enable();
```

这一段代码仅确保output queue上有qdisc，如果有，则将`head`变量指向第一个qdisc，并
更新队尾指针。

接下来，一个`while`循环开始遍历qdsics列表：

```c
                while (head) {
                        struct Qdisc *q = head;
                        spinlock_t *root_lock;

                        head = head->next_sched;

                        root_lock = qdisc_lock(q);
                        if (spin_trylock(root_lock)) {
                                smp_mb__before_clear_bit();
                                clear_bit(__QDISC_STATE_SCHED,
                                          &q->state);
                                qdisc_run(q);
                                spin_unlock(root_lock);
```

上面的代码段拿到qdisc锁`root_lock`。`spin_trylock`尝试锁定;请注意，这里是有意使
用的spin lock（自旋锁），因为它不会阻塞。如果spin lock已经被别人获得，则
`spin_trylock`将立即返回，而不是等待获取。

`spin_trylock`锁定成功将返回非零值。在这种情况下，qdisc的状态的
`__QDISC_STATE_SCHED`位被置0，然后调用qdisc_run，它会再将
`__QDISC___STATE_RUNNING`位置1，并开始执行`__qdisc_run`。

这里很重要。这里发生的是，我们之前跟下来的处理循环是代表用户进行的，从发送系统调
用开始；现在它再次运行，但是，在softirq上下文中，因为这个qdisc的skb之前没有被发
送出去发。这种区别非常重要，因为它会影响你监控发送大量数据的应用程序的CPU使用情
况。让我以另一种方式陈述：

1. 无论发送完成还是驱动程序返回错误，程序的系统时间都将包括调用驱动程序尝试发送
   数据所花费的时间
1. 如果发送在驱动层不成功（例如因为设备正在忙于发送其他内容），则qdisc将被添加到
   output quue并稍后由softirq线程处理。在这种情况下，将会额外花费一些softirq（
   `si`）时间在发送数据上

因此，发送数据所花费的总时间是发送相关（send-related）的**系统调用的系统时间**和
**`NET_TX`类型softirq时间**的组合。

代码最后释放qdisc锁。

如果上面的`spin_trylock`调用失败，则执行以下代码：

```c
                        } else {
                                if (!test_bit(__QDISC_STATE_DEACTIVATED,
                                              &q->state)) {
                                        __netif_reschedule(q);
                                } else {
                                        smp_mb__before_clear_bit();
                                        clear_bit(__QDISC_STATE_SCHED,
                                                  &q->state);
                                }
                        }
                }
        }
```

通过检查qdisc状态的`__QDISC_STATE_DEACTIVATED`标记位，处理两种情况：

1. 如果qdisc未停用，调用`__netif_reschedule`，这会将qdisc放回到原queue中，稍后会再次尝试获取qdisc锁
1. 如果qdisc已停用，则确保`__QDISC_STATE_SCHED`状态也被清除

### 8.5 最终来到`dev_hard_start_xmit`

至此，我们已经穿过了整个网络栈，最终来到`dev_hard_start_xmit`。也许你是从
`sendmsg`系统调用直接到达这里的，或者你是通过qdisc上的softirq线程处理网络数据来
到这里的。`dev_hard_start_xmit`将调用设备驱动程序来实际执行发送操作。

这个函数处理两种主要情况：

1. 已经准备好要发送的数据，或
1. 需要segmentation offloading的数据

先看第一种情况，要发送的数据已经准备好的情况。
[net/code/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L2541-L2652)
：

```c
int dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev,
                        struct netdev_queue *txq)
{
        const struct net_device_ops *ops = dev->netdev_ops;
        int rc = NETDEV_TX_OK;
        unsigned int skb_len;

        if (likely(!skb->next)) {
                netdev_features_t features;

                /*
                 * If device doesn't need skb->dst, release it right now while
                 * its hot in this cpu cache
                 */
                if (dev->priv_flags & IFF_XMIT_DST_RELEASE)
                        skb_dst_drop(skb);

                features = netif_skb_features(skb);
```

代码首先获取设备的回调函数集合`ops`，后面让驱动程序做一些发送数据的工作时会用到
。检查`skb->next`以确定此数据不是已分片数据的一部分，然后继续执行以下两项操作：

首先，检查设备是否设置了`IFF_XMIT_DST_RELEASE`标志。这个版本的内核中的任何“真实”
以太网设备都不使用此标志，但环回设备和其他一些软件设备使用。如果启用此特性，则可
以减少目标高速缓存条目上的引用计数，因为驱动程序不需要它。

接下来，`netif_skb_features`获取设备支持的功能列表，并根据数据的协议类型（
`dev->protocol`）对特性列表进行一些修改。例如，如果设备支持此协议的校验和计算，
则将对skb进行相应的标记。 VLAN tag（如果已设置）也会导致功能标记被修改。

接下来，将检查vlan标记，如果设备无法offload VLAN tag，将通过`__vlan_put_tag`在软
件中执行此操作：

```c
                if (vlan_tx_tag_present(skb) &&
                    !vlan_hw_offload_capable(features, skb->vlan_proto)) {
                        skb = __vlan_put_tag(skb, skb->vlan_proto,
                                             vlan_tx_tag_get(skb));
                        if (unlikely(!skb))
                                goto out;

                        skb->vlan_tci = 0;
                }
```

然后，检查数据以确定这是不是encapsulation （隧道封装）offload请求，例如，
[GRE](https://en.wikipedia.org/wiki/Generic_Routing_Encapsulation)。 在这种情况
下，feature flags将被更新，以添加任何特定于设备的硬件封装功能：

```c
                /* If encapsulation offload request, verify we are testing
                 * hardware encapsulation features instead of standard
                 * features for the netdev
                 */
                if (skb->encapsulation)
                        features &= dev->hw_enc_features;
```

接下来，`netif_needs_gso`用于确定skb是否需要分片。 如果需要，但设备不支持，则
`netif_needs_gso`将返回`true`，表示分片应在软件中进行。 在这种情况下，调用
`dev_gso_segment`进行分片，代码将跳转到gso以发送数据包。我们稍后会看到GSO路径。

```c
                if (netif_needs_gso(skb, features)) {
                        if (unlikely(dev_gso_segment(skb, features)))
                                goto out_kfree_skb;
                        if (skb->next)
                                goto gso;
                }
```

如果数据不需要分片，则处理一些其他情况。 首先，数据是否需要顺序化？ 也就是说，如
果数据分布在多个缓冲区中，设备是否支持发送网络数据，还是首先需要将它们组合成单个
有序缓冲区？ 绝大多数网卡不需要在发送之前将数据顺序化，因此在几乎所有情况下，
`skb_needs_linearize`将为`false`然后被跳过。

```c
                                    else {
                        if (skb_needs_linearize(skb, features) &&
                            __skb_linearize(skb))
                                goto out_kfree_skb;
```

从接下来的一段注释我们可以了解到，下面的代码判断数据包是否仍然需要计算校验和。 如果设备不支持计算校验和，则在这里通过软件计算：

```c
                        /* If packet is not checksummed and device does not
                         * support checksumming for this protocol, complete
                         * checksumming here.
                         */
                        if (skb->ip_summed == CHECKSUM_PARTIAL) {
                                if (skb->encapsulation)
                                        skb_set_inner_transport_header(skb,
                                                skb_checksum_start_offset(skb));
                                else
                                        skb_set_transport_header(skb,
                                                skb_checksum_start_offset(skb));
                                if (!(features & NETIF_F_ALL_CSUM) &&
                                     skb_checksum_help(skb))
                                        goto out_kfree_skb;
                        }
                }
```

再往前，我们来到了packet taps（tap是包过滤器的安插点，例如抓包执行的地方）。回想
一下在[接收数据的文章
](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#netifreceiveskbcore-special-box-delivers-data-to-packet-taps-and-protocol-layers)
中，我们看到了数据包是如何传递给tap（如
[PCAP](http://www.tcpdump.org/manpages/pcap.3pcap.html)）的。 该函数中的下一个代
码块将要发送的数据包传递给tap（如果有的话）：

```c
                if (!list_empty(&ptype_all))
                        dev_queue_xmit_nit(skb, dev);
```

最终，调用驱动的`ops`里面的发送回调函数`ndo_start_xmit`将数据包传给网卡设备：

```c
                skb_len = skb->len;
                rc = ops->ndo_start_xmit(skb, dev);

                trace_net_dev_xmit(skb, rc, dev, skb_len);
                if (rc == NETDEV_TX_OK)
                        txq_trans_update(txq);
                return rc;
        }
```

`ndo_start_xmit`的返回值表示发送成功与否，并作为这个函数的返回值被返回给更上层。
我们看到了这个返回值将如何影响上层：数据可能会被此时的qdisc重新入队，因此
稍后尝试再次发送。

我们来看看GSO的case。如果此函数的前面部分完成了分片，或者之前已经完成了分片但是
上次发送失败，则会进入下面的代码：

```c
gso:
        do {
                struct sk_buff *nskb = skb->next;

                skb->next = nskb->next;
                nskb->next = NULL;

                if (!list_empty(&ptype_all))
                        dev_queue_xmit_nit(nskb, dev);

                skb_len = nskb->len;
                rc = ops->ndo_start_xmit(nskb, dev);
                trace_net_dev_xmit(nskb, rc, dev, skb_len);
                if (unlikely(rc != NETDEV_TX_OK)) {
                        if (rc & ~NETDEV_TX_MASK)
                                goto out_kfree_gso_skb;
                        nskb->next = skb->next;
                        skb->next = nskb;
                        return rc;
                }
                txq_trans_update(txq);
                if (unlikely(netif_xmit_stopped(txq) && skb->next))
                        return NETDEV_TX_BUSY;
        } while (skb->next);
```

你可能已经猜到，此`while`循环会遍历分片生成的skb列表。

每个数据包将被：

* 传给包过滤器（tap，如果有的话）
* 通过`ndo_start_xmit`传递给驱动程序进行发送

设备驱动`ndo_start_xmit()`返回错误时，会进行一些错误处理，并将错误返回给更上层。
未发送的skbs可能会被重新入队以便稍后再次发送。

该函数的最后一部分做一些清理工作，在上面发生错误时释放一些资源：

```c
out_kfree_gso_skb:
        if (likely(skb->next == NULL)) {
                skb->destructor = DEV_GSO_CB(skb)->destructor;
                consume_skb(skb);
                return rc;
        }
out_kfree_skb:
        kfree_skb(skb);
out:
        return rc;
}
EXPORT_SYMBOL_GPL(dev_hard_start_xmit);
```

在继续进入到设备驱动程序之前，先来看一些和前面分析过的代码有关的监控和调优的内容。

### 8.6 Monitoring qdiscs

#### Using the tc command line tool

使用`tc`工具监控qdisc统计：

```shell
$ tc -s qdisc show dev eth1
qdisc mq 0: root
 Sent 31973946891907 bytes 2298757402 pkt (dropped 0, overlimits 0 requeues 1776429)
 backlog 0b 0p requeues 1776429
```

网络设备的qdisc统计对于监控系统发送数据包的运行状况至关重要。你可以通过运行命令
行工具tc来查看状态。 上面的示例显示了如何检查eth1的统计信息。

* `bytes`: The number of bytes that were pushed down to the driver for transmit.
* `pkt`: The number of packets that were pushed down to the driver for transmit.
* `dropped`: The number of packets that were dropped by the qdisc. This can
  happen if transmit queue length is not large enough to fit the data being
  queued to it.
* `overlimits`: Depends on the queuing discipline, but can be either the number
  of packets that could not be enqueued due to a limit being hit, and/or the
  number of packets which triggered a throttling event when dequeued.
* `requeues`: Number of times dev_requeue_skb has been called to requeue an skb.
  Note that an skb which is requeued multiple times will bump this counter each
  time it is requeued.
* `backlog`: Number of bytes currently on the qdisc’s queue. This number is
  usually bumped each time a packet is enqueued.

一些qdisc还会导出额外的统计信息。每个qdisc都不同，对同一个counter可能会累积不同
的次数。你需要查看相应qdisc的源代码，弄清楚每个counter是在哪里、什么条件下被更新
的，如果这些数据对你非常重要，那你必须这么谨慎。

### 8.7 Tuning qdiscs

#### 调整`__qdisc_run`处理权重

你可以调整前面看到的`__qdisc_run`循环的权重（上面看到的`quota`变量），这将导致
`__netif_schedule`更多的被调用执行。 结果将是当前qdisc将被更多的添加到当前CPU的
output_queue，最终会使发包所占的时间变多。

例如：调整所有qdisc的`__qdisc_run`权重：

```shell
$ sudo sysctl -w net.core.dev_weight=600
```

#### 增加发送队列长度

每个网络设备都有一个可以修改的txqueuelen。 大多数qdisc在将数据插入到其发送队列之
前，会检查txqueuelen是否足够（表示的是字节数？）。 你可以调整这个参数以增加qdisc
队列的字节数。

Example: increase the `txqueuelen` of `eth0` to `10000`.

```shell
$ sudo ifconfig eth0 txqueuelen 10000
```

默认值是1000，你可以通过ifconfig命令的输出，查看每个网络设备的txqueuelen。
