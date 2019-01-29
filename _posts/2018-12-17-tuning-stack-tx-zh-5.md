---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 5"
date:   2018-12-17
author: ArthurChiao
categories: network-stack monitoring tuning
---

## 7 Linux netdevice 子系统

在继续跟进`dev_queue_xmit`发送数据包之前，让我们花点时间介绍几个将在下一部分中出
现的重要概念。

### 7.1 Linux traffic control（流量控制）

Linux支持称为流量控制（[traffic
control](http://tldp.org/HOWTO/Traffic-Control-HOWTO/intro.html)）的功能。此功能
允许系统管理员控制数据包如何从机器发送出去。本文不会深入探讨Linux流量控制
的各个方面的细节。[这篇文档](http://tldp.org/HOWTO/Traffic-Control-HOWTO/)对流量
控制系统、它如何控制流量，及其其特性进行了深入的介绍。

这里介绍一些值得一提的概念，使后面的代码更容易理解。

流量控制系统包含几组不同的queue system，每种有不同的排队特征。各个排队系统通常称
为qdisc，也称为排队规则。你可以将qdisc视为**调度程序**; qdisc决定数据包的发送时
间和方式。

在Linux上，每个device都有一个与之关联的默认qdisc。对于仅支持单发送队列的网卡，使
用默认的qdisc `pfifo_fast`。支持多个发送队列的网卡使用mq的默认qdisc。可以运行`tc
qdisc`来查看系统qdisc信息。

某些设备支持硬件流量控制，这允许管理员将流量控制offload到网络硬件，节省系统的
CPU资源。

现在已经介绍了这些概念，让我们从
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L2890-L2894)
继续`dev_queue_xmit`。

### 7.2 `dev_queue_xmit` and `__dev_queue_xmit`

`dev_queue_xmit`简单封装了`__dev_queue_xmit`:

```c
int dev_queue_xmit(struct sk_buff *skb)
{
        return __dev_queue_xmit(skb, NULL);
}
EXPORT_SYMBOL(dev_queue_xmit);
```

`__dev_queue_xmit`才是干脏活累活的地方。我们[一段段
](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L2808-L2825)来看：

```c
static int __dev_queue_xmit(struct sk_buff *skb, void *accel_priv)
{
        struct net_device *dev = skb->dev;
        struct netdev_queue *txq;
        struct Qdisc *q;
        int rc = -ENOMEM;

        skb_reset_mac_header(skb);

        /* Disable soft irqs for various locks below. Also
         * stops preemption for RCU.
         */
        rcu_read_lock_bh();

        skb_update_prio(skb);
```

开始的逻辑：

1. 声明变量
1. 调用`skb_reset_mac_header`，准备发送skb。这会重置skb内部的指针，使得ether头可
   以被访问
1. 调用`rcu_read_lock_bh`，为接下来的读操作加锁。更多关于使用RCU安全访问数据的信
   息，可以参考[这里](https://www.kernel.org/doc/Documentation/RCU/checklist.txt)
1. 调用`skb_update_prio`，如果启用了[网络优先级cgroup](https://github.com/torvalds/linux/blob/v3.13/Documentation/cgroups/net_prio.txt)，这会设置skb的优先级

现在，我们来看更复杂的部分：

```c
        txq = netdev_pick_tx(dev, skb, accel_priv);
```

这会选择发送队列。本文后面会看到，一些网卡支持多发送队列。我们来看这是如何工作的。

#### 7.2.1 `netdev_pick_tx`

`netdev_pick_tx`定义在[net/core/flow_dissector.c](https://github.com/torvalds/linux/blob/v3.13/net/core/flow_dissector.c#L397-L417):

```c
struct netdev_queue *netdev_pick_tx(struct net_device *dev,
                                    struct sk_buff *skb,
                                    void *accel_priv)
{
        int queue_index = 0;

        if (dev->real_num_tx_queues != 1) {
                const struct net_device_ops *ops = dev->netdev_ops;
                if (ops->ndo_select_queue)
                        queue_index = ops->ndo_select_queue(dev, skb,
                                                            accel_priv);
                else
                        queue_index = __netdev_pick_tx(dev, skb);

                if (!accel_priv)
                        queue_index = dev_cap_txqueue(dev, queue_index);
        }

        skb_set_queue_mapping(skb, queue_index);
        return netdev_get_tx_queue(dev, queue_index);
}
```

如上所示，如果网络设备仅支持单个TX队列，则会跳过复杂的代码，直接返回单个TX队列。
大多高端服务器上使用的设备都有多个TX队列。具有多个TX队列的设备有两种情况：

1. 驱动程序实现`ndo_select_queue`，以硬件或feature-specific的方式更智能地选择TX队列
1. 驱动程序没有实现`ndo_select_queue`，这种情况需要内核自己选择设备

从3.13内核开始，没有多少驱动程序实现`ndo_select_queue`。bnx2x和ixgbe驱动程序实
现了此功能，但仅用于以太网光纤通道（[FCoE](https://en.wikipedia.org/wiki/Fibre_Channel_over_Ethernet)）。鉴于此，我们假设网络设备没有实现
`ndo_select_queue`和/或没有使用FCoE。在这种情况下，内核将使用`__netdev_pick_tx`
选择tx队列。

一旦`__netdev_pick_tx`确定了队列号，`skb_set_queue_mapping`将缓存该值（稍后将在
流量控制代码中使用），`netdev_get_tx_queue`将查找并返回指向该队列的指针。让我们
看一下`__netdev_pick_tx`在返回`__dev_queue_xmit`之前的工作原理。

#### 7.2.2 `__netdev_pick_tx`

我们来看内核如何选择TX队列。
[net/core/flow_dissector.c](https://github.com/torvalds/linux/blob/v3.13/net/core/flow_dissector.c#L375-L395):

```c
u16 __netdev_pick_tx(struct net_device *dev, struct sk_buff *skb)
{
        struct sock *sk = skb->sk;
        int queue_index = sk_tx_queue_get(sk);

        if (queue_index < 0 || skb->ooo_okay ||
            queue_index >= dev->real_num_tx_queues) {
                int new_index = get_xps_queue(dev, skb);
                if (new_index < 0)
                        new_index = skb_tx_hash(dev, skb);

                if (queue_index != new_index && sk &&
                    rcu_access_pointer(sk->sk_dst_cache))
                        sk_tx_queue_set(sk, new_index);

                queue_index = new_index;
        }

        return queue_index;
}
```

代码首先调用`sk_tx_queue_get`检查发送队列是否已经缓存在socket上，如果尚未缓存，
则返回-1。

下一个if语句检查是否满足以下任一条件：

1. `queue_index < 0`：表示尚未设置TX queue的情况
1. `ooo_okay`标志是否非零：如果不为0，则表示现在允许无序（out of order）数据包。
   协议层必须正确地地设置此标志。当flow的所有outstanding（需要确认的？）数据包都
   已确认时，TCP协议层将设置此标志。当发生这种情况时，内核可以为此数据包选择不同
   的TX队列。UDP协议层不设置此标志 - 因此UDP数据包永远不会将`ooo_okay`设置为非零
   值。
1. TX queue index大于TX queue数量：如果用户最近通过ethtool更改了设备上的队列数，
   则会发生这种情况。稍后会详细介绍。

以上任何一种情况，都表示没有找到合适的TX queue，因此接下来代码会进入慢路径以继续
寻找合适的发送队列。首先调用`get_xps_queue`，它会使用一个由用户配置的TX queue到
CPU的映射，这称为XPS（Transmit Packet Steering ，发送数据包控制），我们将更详细
地了解XPS是什么以及它如何工作。

如果内核不支持XPS，或者系统管理员未配置XPS，或者配置的映射引用了无效队列，
`get_xps_queue`返回-1，则代码将继续调用`skb_tx_hash`。

一旦XPS或内核使用`skb_tx_hash`自动选择了发送队列，`sk_tx_queue_set`会将队列缓存
在socket对象上，然后返回。让我们看看XPS，以及`skb_tx_hash`在继续调用
`dev_queue_xmit`之前是如何工作的。

##### Transmit Packet Steering (XPS)

发送数据包控制（XPS）是一项功能，允许系统管理员配置哪些CPU可以处理网卡的哪些发送
队列。XPS的主要目的是**避免处理发送请求时的锁竞争**。使用XPS还可以减少缓存驱逐，
避免[NUMA](https://en.wikipedia.org/wiki/Non-uniform_memory_access)机器上的远程
内存访问等。

可以查看内核有关XPS的[文档
](https://github.com/torvalds/linux/blob/v3.13/Documentation/networking/scaling.txt#L364-L422)
了解其如何工作的更多信息。我们后面会介绍如何调整系统的XPS，现在，你只需要知道
配置XPS，系统管理员需要定义TX queue到CPU的映射（bitmap形式）。

上面代码中，`get_xps_queue`将查询这个用户指定的映射，以确定应使用哪个发送
队列。如果`get_xps_queue`返回-1，则将改为使用`skb_tx_hash`。

##### `skb_tx_hash`

如果XPS未包含在内核中，或XPS未配置，或配置的队列不可用（可能因为用户调整了队列数
），`skb_tx_hash`将接管以确定应在哪个队列上发送数据。准确理解`skb_tx_hash`的工作
原理非常重要，具体取决于你的发送负载。请注意，这段代码已经随时间做过一些更新，因
此如果你使用的内核版本与本文不同，则应直接查阅相应版本的j内核源代码。

让我们看看它是如何工作的，来自
[include/linux/netdevice.h](https://github.com/torvalds/linux/blob/v3.13/include/linux/netdevice.h#L2331-L2340)
：

```c
/*
 * Returns a Tx hash for the given packet when dev->real_num_tx_queues is used
 * as a distribution range limit for the returned value.
 */
static inline u16 skb_tx_hash(const struct net_device *dev,
                              const struct sk_buff *skb)
{
        return __skb_tx_hash(dev, skb, dev->real_num_tx_queues);
}
```

直接调用了` __skb_tx_hash`, [net/core/flow_dissector.c](https://github.com/torvalds/linux/blob/v3.13/net/core/flow_dissector.c#L239-L271)：

```c
/*
 * Returns a Tx hash based on the given packet descriptor a Tx queues' number
 * to be used as a distribution range.
 */
u16 __skb_tx_hash(const struct net_device *dev, const struct sk_buff *skb,
                  unsigned int num_tx_queues)
{
        u32 hash;
        u16 qoffset = 0;
        u16 qcount = num_tx_queues;

        if (skb_rx_queue_recorded(skb)) {
                hash = skb_get_rx_queue(skb);
                while (unlikely(hash >= num_tx_queues))
                        hash -= num_tx_queues;
                return hash;
        }
```

这个函数中的第一个if是一个有趣的短路。函数名`skb_rx_queue_recorded`有点误导。skb
有一个`queue_mapping`字段，rx和tx都会用到这个字段。无论如何，如果系统正在接收数
据包并将其转发到其他地方，则此if语句都为`true`。否则，代码将继续向下：

```c
        if (dev->num_tc) {
                u8 tc = netdev_get_prio_tc_map(dev, skb->priority);
                qoffset = dev->tc_to_txq[tc].offset;
                qcount = dev->tc_to_txq[tc].count;
        }
```

要理解这段代码，首先要知道，程序可以设置socket上发送的数据的优先级。这可以通过
`setsockopt`带`SOL_SOCKET`和`SO_PRIORITY`选项来完成。有关`SO_PRIORITY`的更多信息
，请参见[socket (7) man
page](http://man7.org/linux/man-pages/man7/socket.7.html)。

请注意，如果使用`setsockopt`带`IP_TOS`选项来设置在socket上发送的IP包的TOS标志（
或者作为辅助消息传递给`sendmsg`，在数据包级别设置），内核会将其转换为
`skb->priority`。

如前所述，一些网络设备支持基于硬件的流量控制系统。**如果num_tc不为零，则表示此设
备支持基于硬件的流量控制**。这种情况下，将查询一个**packet priority到该硬件支持
的流量控制**的映射，根据此映射选择适当的流量类型（traffic class）。

接下来，将计算出该traffic class的TX queue的范围，它将用于确定发送队列。

如果`num_tc`为零（网络设备不支持硬件流量控制），则`qcount`和`qoffset`变量分
别设置为发送队列数和0。

使用`qcount`和`qoffset`，将计算发送队列的index：

```c
        if (skb->sk && skb->sk->sk_hash)
                hash = skb->sk->sk_hash;
        else
                hash = (__force u16) skb->protocol;
        hash = __flow_hash_1word(hash);

        return (u16) (((u64) hash * qcount) >> 32) + qoffset;
}
EXPORT_SYMBOL(__skb_tx_hash);
```

最后，通过`__netdev_pick_tx`返回选出的TX queue index。

### 7.3 继续`__dev_queue_xmit`

至此已经选到了合适的发送队列。

继续`__dev_queue_xmit can continue`:

```c
        q = rcu_dereference_bh(txq->qdisc);

#ifdef CONFIG_NET_CLS_ACT
        skb->tc_verd = SET_TC_AT(skb->tc_verd, AT_EGRESS);
#endif
        trace_net_dev_queue(skb);
        if (q->enqueue) {
                rc = __dev_xmit_skb(skb, q, dev, txq);
                goto out;
        }
```

首先获取与此队列关联的qdisc。回想一下，之前我们看到单发送队列设备的默认类型是
`pfifo_fast` qdisc，而对于多队列设备，默认类型是`mq` qdisc。

接下来，如果内核中已启用数据包分类API，则代码会为packet分配traffic class。 接下
来，检查disc是否有合适的队列来存放packet。像`noqueue`这样的qdisc没有队列。 如果
有队列，则代码调用`__dev_xmit_skb`继续处理数据，然后跳转到此函数的末尾。我们很快
就会看到`__dev_xmit_skb`。现在，让我们看看如果没有队列会发生什么，从一个非常有用
的注释开始：

```c
        /* The device has no queue. Common case for software devices:
           loopback, all the sorts of tunnels...

           Really, it is unlikely that netif_tx_lock protection is necessary
           here.  (f.e. loopback and IP tunnels are clean ignoring statistics
           counters.)
           However, it is possible, that they rely on protection
           made by us here.

           Check this and shot the lock. It is not prone from deadlocks.
           Either shot noqueue qdisc, it is even simpler 8)
         */
        if (dev->flags & IFF_UP) {
                int cpu = smp_processor_id(); /* ok because BHs are off */
```

正如注释所示，**唯一可以拥有"没有队列的qdisc"的设备是环回设备和隧道设备**。如果
设备当前处于运行状态，则获取当前CPU，然后判断此设备队列上的发送锁是否由此CPU拥有
：

```c
                if (txq->xmit_lock_owner != cpu) {

                        if (__this_cpu_read(xmit_recursion) > RECURSION_LIMIT)
                                goto recursion_alert;
```

如果发送锁不由此CPU拥有，则在此处检查per-CPU计数器变量`xmit_recursion`，判断其是
否超过`RECURSION_LIMIT`。 一个程序可能会在这段代码这里持续发送数据，然后被抢占，
调度程序选择另一个程序来运行。第二个程序也可能驻留在此持续发送数据。因此，
`xmit_recursion`计数器用于确保在此处竞争发送数据的程序不超过`RECURSION_LIMIT`个
。

我们继续：

```c
                        HARD_TX_LOCK(dev, txq, cpu);

                        if (!netif_xmit_stopped(txq)) {
                                __this_cpu_inc(xmit_recursion);
                                rc = dev_hard_start_xmit(skb, dev, txq);
                                __this_cpu_dec(xmit_recursion);
                                if (dev_xmit_complete(rc)) {
                                        HARD_TX_UNLOCK(dev, txq);
                                        goto out;
                                }
                        }
                        HARD_TX_UNLOCK(dev, txq);
                        net_crit_ratelimited("Virtual device %s asks to queue packet!\n",
                                             dev->name);
                } else {
                        /* Recursion is detected! It is possible,
                         * unfortunately
                         */
recursion_alert:
                        net_crit_ratelimited("Dead loop on virtual device %s, fix it urgently!\n",
                                             dev->name);
                }
        }
```

接下来的代码首先尝试获取发送锁，然后检查要使用的设备的发送队列是否被停用。如果没
有停用，则更新`xmit_recursion`计数，然后将数据向下传递到更靠近发送的设备。我们稍
后会更详细地看到`dev_hard_start_xmit`。

或者，如果当前CPU是发送锁定的拥有者，或者如果`RECURSION_LIMIT`被命中，则不进行发
送，而会打印告警日志。

函数剩余部分的代码设置错误码并返回。

由于我们对真正的以太网设备感兴趣，让我们来看一下之前就需要跟进去的
`__dev_xmit_skb`函数，这是发送主线上的函数。

### 7.4 `__dev_xmit_skb`

现在我们带着排队规则`qdisc`、网络设备`dev`和发送队列`txq`三个变量来到
`__dev_xmit_skb`，
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L2684-L2745)
：

```c
static inline int __dev_xmit_skb(struct sk_buff *skb, struct Qdisc *q,
                                 struct net_device *dev,
                                 struct netdev_queue *txq)
{
        spinlock_t *root_lock = qdisc_lock(q);
        bool contended;
        int rc;

        qdisc_pkt_len_init(skb);
        qdisc_calculate_pkt_len(skb, q);
        /*
         * Heuristic to force contended enqueues to serialize on a
         * separate lock before trying to get qdisc main lock.
         * This permits __QDISC_STATE_RUNNING owner to get the lock more often
         * and dequeue packets faster.
         */
        contended = qdisc_is_running(q);
        if (unlikely(contended))
                spin_lock(&q->busylock);
```

代码首先使用`qdisc_pkt_len_init`和`qdisc_calculate_pkt_len`来计算数据的准确长度
，稍后qdisc会用到该值。 对于硬件offload（例如UFO）这是必需的，因为添加的额外的头
信息，硬件offload的时候回用到。

接下来，使用另一个锁来帮助减少qdisc主锁上的竞争（我们稍后会看到这第二个锁）。 如
果qdisc当前正在运行，那么试图发送的其他程序将在qdisc的`busylock`上竞争。 这允许
运行qdisc的程序在处理数据包的同时，与较少量的程序竞争第二个主锁。随着竞争者数量
的减少，这种技巧增加了吞吐量。[原始commit描述
](https://github.com/torvalds/linux/commit/79640a4ca6955e3ebdb7038508fa7a0cd7fa5527)
。 接下来是主锁：

```c
        spin_lock(root_lock);
```

接下来处理3种可能情况：

1. 如果qdisc已停用
1. 如果qdisc允许数据包bypass排队系统，并且没有其他包要发送，并且qdisc当前没有运
   行。允许包bypass所谓的**“work-conserving qdisc” - 那些用于流量整形（traffic
   reshaping）目的并且不会引起发送延迟的qdisc**
1. 所有其他情况

让我们来看看每种情况下发生什么，从qdisc停用开始：

```c
        if (unlikely(test_bit(__QDISC_STATE_DEACTIVATED, &q->state))) {
                kfree_skb(skb);
                rc = NET_XMIT_DROP;
```

这很简单。 如果qdisc停用，则释放数据并将返回代码设置为`NET_XMIT_DROP`。

接下来，如果qdisc允许数据包bypass，并且没有其他包要发送，并且qdisc当前没有运行：

```c
        } else if ((q->flags & TCQ_F_CAN_BYPASS) && !qdisc_qlen(q) &&
                   qdisc_run_begin(q)) {
                /*
                 * This is a work-conserving queue; there are no old skbs
                 * waiting to be sent out; and the qdisc is not running -
                 * xmit the skb directly.
                 */
                if (!(dev->priv_flags & IFF_XMIT_DST_RELEASE))
                        skb_dst_force(skb);

                qdisc_bstats_update(q, skb);

                if (sch_direct_xmit(skb, q, dev, txq, root_lock)) {
                        if (unlikely(contended)) {
                                spin_unlock(&q->busylock);
                                contended = false;
                        }
                        __qdisc_run(q);
                } else
                        qdisc_run_end(q);

                rc = NET_XMIT_SUCCESS;
```

这个if语句有点复杂，如果满足以下所有条件，则整个语句的计算结果为true：

1. `q-> flags＆TCQ_F_CAN_BYPASS`：qdisc允许数据包绕过排队系统。对于所谓的“
   work-conserving” qdiscs这会是`true`；即，允许packet bypass流量整形qdisc。
   `pfifo_fast` qdisc允许数据包bypass
1. `!qdisc_qlen(q)`：qdisc的队列中没有待发送的数据
1. `qdisc_run_begin(p)`：如果qdisc未运行，此函数将设置qdisc的状态为“running”并返
   回`true`，如果qdisc已在运行，则返回`false`

如果以上三个条件都为`true`，那么：

* 检查`IFF_XMIT_DST_RELEASE`标志，此标志允许内核释放skb的目标缓存。如果标志已禁用，将强制对skb进行引用计数
* 调用`qdisc_bstats_update`更新qdisc发送的字节数和包数统计
* 调用`sch_direct_xmit`用于发送数据包。我们将很快深入研究`sch_direct_xmit`，因为慢路径也会调用到它

`sch_direct_xmit`的返回值有两种情况：

1. 队列不为空（返回> 0）。在这种情况下，`busylock`将被释放，然后调用`__qdisc_run`重新启动qdisc处理
1. 队列为空（返回0）。在这种情况下，`qdisc_run_end`用于关闭qdisc处理

在任何一种情况下，都会返回`NET_XMIT_SUCCESS`，这不是太糟糕。

让我们检查最后一种情况：

```c
        } else {
                skb_dst_force(skb);
                rc = q->enqueue(skb, q) & NET_XMIT_MASK;
                if (qdisc_run_begin(q)) {
                        if (unlikely(contended)) {
                                spin_unlock(&q->busylock);
                                contended = false;
                        }
                        __qdisc_run(q);
                }
        }
```

在所有其他情况下：

1. 调用`skb_dst_force`强制对skb的目标缓存进行引用计数
1. 调用qdisc的`enqueue`方法将数据入队，保存函数返回值
1. 调用`qdisc_run_begin(p)`将qdisc标记为正在运行。如果它尚未运行（`contended ==
   false`），则释放`busylock`，然后调用`__qdisc_run(p)`启动qdisc处理

函数最后释放相应的锁，并返回状态码：

```c
        spin_unlock(root_lock);
        if (unlikely(contended))
                spin_unlock(&q->busylock);
        return rc;
```

### 7.5 调优: Transmit Packet Steering (XPS)

使用XPS需要在内核配置中启用它（Ubuntu上内核3.13.0有XPS），并提供一个位掩码，用于
描述**CPU和TX queue的对应关系**。

这些位掩码类似于
[RPS](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#receive-packet-steering-rps)
位掩码，你可以在内核[文档
](https://github.com/torvalds/linux/blob/v3.13/Documentation/networking/scaling.txt#L147-L150)
中找到有关这些位掩码的一些资料。

简而言之，要修改的位掩码位于以下位置：

```shell
/sys/class/net/DEVICE_NAME/queues/QUEUE/xps_cpus
```

因此，对于eth0和TX queue 0，你需要使用十六进制数修改文件：
`/sys/class/net/eth0/queues/tx-0/xps_cpus`，制定哪些CPU应处理来自eth0的发送队列0
的发送过程。另外，[文档
](https://github.com/torvalds/linux/blob/v3.13/Documentation/networking/scaling.txt#L412-L422)
指出，在某些配置中可能不需要XPS。
