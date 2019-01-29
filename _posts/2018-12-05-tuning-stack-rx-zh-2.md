---
layout: post
title:  "[译] Linux网络栈监控和调优：接收数据 2"
date:   2018-12-05
author: ArthurChiao
categories: network-stack monitoring tuning
---

## 4 软中断（SoftIRQ）

在查看网络栈之前，让我们先开个小差，看下内核里一个叫SoftIRQ的东西。

### 4.1 软中断是什么

内核的软中断系统是一种在硬中断处理上下文（驱动中）之外执行代码机制。硬件中断处理
函数执行的时候，会屏蔽部分或全部的（新的）硬中断。中断被屏蔽的时间越长，丢失事件
的可能性也就越大。所以，所有耗时的操作都应该从硬中断处理逻辑中剥离出来，硬中断因
此能尽可能快的执行，然后再重新打开硬中断。

内核中也有其他机制将耗时操作转移出去，不过对于网络栈，我们接下来只看软中断。

可以把软中断系统想象成一系列内核线程（每个CPU一个），这些线程执行针对不同事件注
册好的处理函数（handler）。如果你执行过top命令，可能会注意到ksoftirqd/0这个内核
线程，其表示这个软中断线程跑在CPU 0上面。

内核子系统（比如网络）能通过open_softirq函数注册软中断处理函数。接下来我们会看到
网络系统是如何注册它的处理函数的。现在，我们先来学习一下软中断是如何工作的。

### 4.2 `ksoftirqd`

软中断对分担硬中断的工作如此重要，因此软中断线程在内核启动的很早阶段就spawn出来了。

[`kernel/softirq.c`](https://github.com/torvalds/linux/blob/v3.13/kernel/softirq.c#L743-L758) 展示了ksoftirqd系统是如何初始化的：

```c
static struct smp_hotplug_thread softirq_threads = {
      .store              = &ksoftirqd,
      .thread_should_run  = ksoftirqd_should_run,
      .thread_fn          = run_ksoftirqd,
      .thread_comm        = "ksoftirqd/%u",
};

static __init int spawn_ksoftirqd(void)
{
      register_cpu_notifier(&cpu_nfb);

      BUG_ON(smpboot_register_percpu_thread(&softirq_threads));

      return 0;
}
early_initcall(spawn_ksoftirqd);
```

看到注册了两个回调函数: `ksoftirqd_should_run`和`run_ksoftirqd`。这两个函数都会从
[`kernel/smpboot.c`](https://github.com/torvalds/linux/blob/v3.13/kernel/smpboot.c#L94-L163)
里调用，作为事件处理循环的一部分。

`kernel/smpboot.c`里面的代码首先调用`ksoftirqd_should_run`判断是否有pending的软
中断，如果有，就执行`run_ksoftirqd`，后者做一些bookeeping工作，然后调用
`__do_softirq`。

### 4.3 `__do_softirq`

`__do_softirq`做的几件事情：

* 判断哪个softirq被pending
* 计算softirq时间，用于统计
* 更新softirq执行相关的统计数据
* 执行pending softirq的处理函数

查看CPU利用率时，`si`字段对应的就是softirq，度量（从硬中断转移过来的）软中断的CPU使用量。

### 4.4 监控

软中断的信息可以从 `/proc/softirqs`读取：

```shell
$ cat /proc/softirqs
                    CPU0       CPU1       CPU2       CPU3
          HI:          0          0          0          0
       TIMER: 2831512516 1337085411 1103326083 1423923272
      NET_TX:   15774435     779806     733217     749512
      NET_RX: 1671622615 1257853535 2088429526 2674732223
       BLOCK: 1800253852    1466177    1791366     634534
BLOCK_IOPOLL:          0          0          0          0
     TASKLET:         25          0          0          0
       SCHED: 2642378225 1711756029  629040543  682215771
     HRTIMER:    2547911    2046898    1558136    1521176
         RCU: 2056528783 4231862865 3545088730  844379888
```

监控这些数据可以得到软中断的执行频率信息。

例如，`NET_RX`一行显示的是软中断在CPU间的分布。如果分布非常不均匀，那某一列的值
就会远大于其他列，这预示着下面要介绍的Receive Packet Steering / Receive Flow
Steering可能会派上用场。在监控这个数据的时候也要注意：不要太相信这个数值，NET_RX
太高并不一定都是网卡触发的，下面会介绍到，其他地方也有可能触发之。

当调整其他网络配置时，也留意下这个指标的变动。

现在，让我们进入网络栈部分，跟踪一个包是如何被接收的。

## 5 Linux网络设备子系统

我们已经知道了网络驱动和软中断是如何工作的，接下来看Linux网络设备子系统是如何初始化的。
然后我们就可以从一个包到达网卡开始跟踪它的整个路径。

### 5.1 网络设备子系统的初始化

网络设备(netdev)的初始化在`net_dev_init`，里面有些东西很有意思。

#### `struct softnet_data`实例初始化

`net_dev_init`为每个CPU创建一个`struct softnet_data`实例。这些实例包含一些重要指
针，指向处理网络数据的相关一些信息：

* 需要注册到这个CPU的NAPI实例列表
* 数据处理backlog
* 处理权重
* receive offload实例列表
* receive packet steering设置

接下来随着逐步进入网络栈，我们会一一查看这些功能。

#### SoftIRQ Handler初始化

`net_dev_init`分别为接收和发送数据注册了一个软中断处理函数。

```c
static int __init net_dev_init(void)
{
  /* ... */

  open_softirq(NET_TX_SOFTIRQ, net_tx_action);
  open_softirq(NET_RX_SOFTIRQ, net_rx_action);

 /* ... */
}
```

后面会看到驱动的中断处理函数是如何触发`net_rx_action`这个为`NET_RX_SOFTIRQ`软中断注册的处理函数的。

### 5.2 数据来了

终于，网络数据来了！

如果RX队列有足够的描述符（descriptors），包会通过DMA写到RAM。设备然后发起对应于
它的中断（或者在MSI-X的场景，中断和包达到的RX队列绑定）。

#### 5.2.1 中断处理函数

一般来说，中断处理函数应该将尽可能多的处理逻辑移出（到软中断），这至关重要，因为
发起一个中断后，其他的中断就会被屏蔽。

我们来看一下MSI-X中断处理函数的代码，它展示了中断处理函数是如何尽量简单的。

[igb_main.c](https://github.com/torvalds/linux/blob/v3.13/drivers/net/ethernet/intel/igb/igb_main.c#L2038-L2059)：

```c
static irqreturn_t igb_msix_ring(int irq, void *data)
{
  struct igb_q_vector *q_vector = data;

  /* Write the ITR value calculated from the previous interrupt. */
  igb_write_itr(q_vector);

  napi_schedule(&q_vector->napi);

  return IRQ_HANDLED;
}
```

这个中断处理函数非常简短，只做了2个很快的操作，然后就返回了。

首先，它调用`igb_write_itr` 更新一个硬件寄存器。对这个例子，这个寄存器是记录硬件
中断频率的。

这个寄存器和一个叫"Interrupt Throttling"（也叫"Interrupt Coalescing"）的硬件特性
相关，这个特性可以平滑传送到CPU的中断数量。我们接下来会看到，ethtool是怎么样提供
了一个机制用于调整IRQ触发频率的。

第二，napi_schedule 触发，如果NAPI的处理循环还没开始的话，这会唤醒它。注意，这个
处理循环是在软中断中执行的，而不是硬中断。

这段代码展示了硬中断尽量简短为何如此重要；为我们接下来理解多核CPU的接收逻辑很有
帮助。

#### 5.2.2 NAPI 和 `napi_schedule`

接下来看从硬件中断中调用的`napi_schedule`是如何工作的。

注意，NAPI存在的意义是无需硬件中断通知可以收包了，就可以接收网络数据。前面提到，
NAPI的轮询循环（poll loop）是受硬件中断触发而跑起来的。换句话说，NAPI功能启用了
，但是默认是没有工作的，直到第一个包到达的时候，网卡触发的一个硬件将它唤醒。后面
会看到，也还有其他的情况，NAPI功能也会被关闭，直到下一个硬中断再次将它唤起。

`napi_schedule`只是一个简单的封装，内层调用`__napi_schedule`。
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L4154-L4168):

```c
/**
 * __napi_schedule - schedule for receive
 * @n: entry to schedule
 *
 * The entry's receive function will be scheduled to run
 */
void __napi_schedule(struct napi_struct *n)
{
  unsigned long flags;

  local_irq_save(flags);
  ____napi_schedule(&__get_cpu_var(softnet_data), n);
  local_irq_restore(flags);
}
EXPORT_SYMBOL(__napi_schedule);
```

 `__get_cpu_var`用于获取属于这个CPU的`structure softnet_data`实例。

`____napi_schedule`, [net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L4154-L4168):

```c
/* Called with irq disabled */
static inline void ____napi_schedule(struct softnet_data *sd,
                                     struct napi_struct *napi)
{
  list_add_tail(&napi->poll_list, &sd->poll_list);
  __raise_softirq_irqoff(NET_RX_SOFTIRQ);
}
```

这段代码了做了两个重要的事情：

1. 将（从驱动的中断函数中传来的）`napi_struct`实例，添加到poll list，后者attach到这个CPU上的`softnet_data`
1. `__raise_softirq_irqoff`触发一个`NET_RX_SOFTIRQ`类型软中断。这会触发执行
   `net_rx_action`（如果没有正在执行），后者是网络设备初始化的时候注册的

接下来会看到，软中断处理函数`net_rx_action`会调用NAPI的poll函数来收包。

#### 5.2.3 关于CPU和网络数据处理的一点笔记

注意到目前为止，我们从硬中断处理函数中转移到软中断处理函数的逻辑，都是使用的本
CPU实例。

驱动的硬中断处理函数做的事情很少，但软中断将会在和硬中断相同的CPU上执行。这就是
为什么给每个CPU一个特定的硬中断非常重要：这个CPU不仅处理这个硬中断，而且通过NAPI
处理接下来的软中断来收包。

后面我们会看到，Receive Packet Steering可以将软中断分给其他CPU。

#### 5.2.4 监控网络数据到达

##### 硬中断请求

注意：监控硬件中断拿不到关于网络包处理的健康状况的全景图，一些驱动在NAPI运行的
时候会关闭硬中断。这只是你整个监控方案的一个重要部分。

读取硬中断统计：

```shell
$ cat /proc/interrupts
            CPU0       CPU1       CPU2       CPU3
   0:         46          0          0          0 IR-IO-APIC-edge      timer
   1:          3          0          0          0 IR-IO-APIC-edge      i8042
  30: 3361234770          0          0          0 IR-IO-APIC-fasteoi   aacraid
  64:          0          0          0          0 DMAR_MSI-edge      dmar0
  65:          1          0          0          0 IR-PCI-MSI-edge      eth0
  66:  863649703          0          0          0 IR-PCI-MSI-edge      eth0-TxRx-0
  67:  986285573          0          0          0 IR-PCI-MSI-edge      eth0-TxRx-1
  68:         45          0          0          0 IR-PCI-MSI-edge      eth0-TxRx-2
  69:        394          0          0          0 IR-PCI-MSI-edge      eth0-TxRx-3
 NMI:    9729927    4008190    3068645    3375402  Non-maskable interrupts
 LOC: 2913290785 1585321306 1495872829 1803524526  Local timer interrupts
```

可以看到有多少包进来、硬件中断频率，RX队列被哪个CPU处理等信息。这里只能看到硬中
断数量，不能看出实际多少数据被接收或处理，因为一些驱动在NAPI收包时会关闭硬中断。
进一步，使用Interrupt Coalescing时也会影响这个统计。监控这个指标能帮你判断出你设
置的Interrupt Coalescing是不是在工作。

为了使监控更加完整，需要同时监控`/proc/softirqs` (前面提到)和`/proc`。

#### 5.2.5 数据接收调优

##### 中断合并（Interrupt coalescing）

中断合并会将多个中断事件放到一起，到达一定的阈值之后才向CPU发起中断请求。

这可以防止中断风暴，提升吞吐。减少中断数量能使吞吐更高，但延迟也变大，CPU使用量
下降；中断数量过多则相反。

历史上，早期的igb、e1000版本，以及其他的都包含一个叫InterruptThrottleRate参数，
最近的版本已经被ethtool可配置的参数取代。

```shell
$ sudo ethtool -c eth0
Coalesce parameters for eth0:
Adaptive RX: off  TX: off
stats-block-usecs: 0
sample-interval: 0
pkt-rate-low: 0
pkt-rate-high: 0
...
```

ethtool提供了用于中断合并相关的通用的接口。但切记，不是所有的设备都支持完整的配
置。你需要查看你的驱动文档或代码来确定哪些支持，哪些不支持。ethtool的文档说的：“
驱动没有实现的接口将会被静默忽略”。

某些驱动支持一个有趣的特性“自适应 RX/TX 硬中断合并”。这个特性一般是在硬件实现的
。驱动通常需要做一些额外的工作来告诉网卡需要打开这个特性（前面的igb驱动代码里有
涉及）。

自适应RX/TX硬中断合并带来的效果是：带宽比较低时降低延迟，带宽比较高时提升吞吐。

用`ethtool -C`打开自适应RX IRQ合并：

```shell
$ sudo ethtool -C eth0 adaptive-rx on
```

还可以用`ethtool -C`更改其他配置。常用的包括：

* `rx-usecs`: How many usecs to delay an RX interrupt after a packet arrives.
* `rx-frames`: Maximum number of data frames to receive before an RX interrupt.
* `rx-usecs-irq`: How many usecs to delay an RX interrupt while an interrupt is being serviced by the host.
* `rx-frames-irq`: Maximum number of data frames to receive before an RX interrupt is generated while the system is servicing an interrupt.

请注意你的硬件可能只支持以上列表的一个子集，具体请参考相应的驱动说明或源码。

不幸的是，通常并没有一个很好的文档来说明这些选项，最全的文档很可能是头文件。查看
[include/uapi/linux/ethtool.h](https://github.com/torvalds/linux/blob/v3.13/include/uapi/linux/ethtool.h#L184-L255) ethtool每个每个选项的解释。

注意：虽然硬中断合并看起来是个不错的优化项，但要你的网络栈的其他一些相应
部分也要针对性的调整。只合并硬中断很可能并不会带来多少收益。

##### 调整硬中断亲和性（IRQ affinities）

If your NIC supports RSS / multiqueue or if you are attempting to optimize for data locality, you may wish to use a specific set of CPUs for handling interrupts generated by your NIC.

Setting specific CPUs allows you to segment which CPUs will be used for processing which IRQs. These changes may affect how upper layers operate, as we’ve seen for the networking stack.

If you do decide to adjust your IRQ affinities, you should first check if you running the irqbalance daemon. This daemon tries to automatically balance IRQs to CPUs and it may overwrite your settings. If you are running irqbalance, you should either disable irqbalance or use the --banirq in conjunction with IRQBALANCE_BANNED_CPUS to let irqbalance know that it shouldn’t touch a set of IRQs and CPUs that you want to assign yourself.

Next, you should check the file /proc/interrupts for a list of the IRQ numbers for each network RX queue for your NIC.

Finally, you can adjust the which CPUs each of those IRQs will be handled by modifying /proc/irq/IRQ_NUMBER/smp_affinity for each IRQ number.

You simply write a hexadecimal bitmask to this file to instruct the kernel which CPUs it should use for handling the IRQ.

Example: Set the IRQ affinity for IRQ 8 to CPU 0

```shell
$ sudo bash -c 'echo 1 > /proc/irq/8/smp_affinity'
```

### 5.3 网络数据处理：开始

一旦软中断代码判断出有softirq处于pending状态，就会开始处理，执行`net_rx_action`，网络数据处理就此开始。

我们来看一下`net_rx_action` 的循环部分，理解它是如何工作的。哪个部分可以调优，哪个可以被监控。

#### 5.3.1 `net_rx_action`处理循环

`net_rx_action`从包所在的内存开始处理，包是被设备通过DMA直接送到内存的。

函数遍历本CPU队列的NAPI实例列表，依次出队，操作它。

处理逻辑考虑任务量（work）和执行时间两个因素：

1. 跟踪记录工作量预算（work budget），预算可以调整
2. 记录消耗的时间

[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L4380-L4383):

```c
while (!list_empty(&sd->poll_list)) {
    struct napi_struct *n;
    int work, weight;

    /* If softirq window is exhausted then punt.
     * Allow this to run for 2 jiffies since which will allow
     * an average latency of 1.5/HZ.
     */
    if (unlikely(budget <= 0 || time_after_eq(jiffies, time_limit)))
      goto softnet_break;
```

这里可以看到内核是如何防止处理数据包过程霸占整个CPU的，其中budget是该CPU的所有
NAPI实例的总预算。

这也是多队列网卡应该精心调整IRQ Affinity的原因。回忆前面讲的，处理硬中断的CPU接
下来会处理相应的软中断，进而执行上面包含budget的这段逻辑。

多网卡多队列可能会出现这样的情况：多个NAPI实例注册到同一个CPU上。每个CPU上的所有
NAPI实例共享一份budget。

如果你没有足够的CPU来分散网卡硬中断，可以考虑增加`net_rx_action`允许每个CPU处理
更多包。增加budget可以增加CPU使用量（`top`等命令看到的`sitime`或`si`部分），
但可以减少延迟，因为数据处理更加及时。

Note: the CPU will still be bounded by a time limit of 2 jiffies, regardless of the assigned budget.

#### 5.3.2 NAPI poll function and weight

回忆前面，网络设备驱动使用`netif_napi_add`注册poll方法，`igb`驱动有如下
代码片段：

```c
 /* initialize NAPI */
  netif_napi_add(adapter->netdev, &q_vector->napi, igb_poll, 64);
```

这注册了一个NAPI实例，hardcode 64的权重。我们来看在`net_rx_action`处理循环中这个
值是如何使用的。
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L4322-L4338):

```c
weight = n->weight;

work = 0;
if (test_bit(NAPI_STATE_SCHED, &n->state)) {
        work = n->poll(n, weight);
        trace_napi_poll(n);
}

WARN_ON_ONCE(work > weight);

budget -= work;
```

其中的`n`是`struct napi`的实例。其中的`poll`指向`igb_poll`。`poll()`返回处理的数
据帧数量，budget会减去这个值。

所以，假设你的驱动使用weight值64（Linux 3.13.0 的所有驱动都是hardcode这个值）
，设置budget默认值300，那你的系统将在如下条件之一停止数据处理：

1. `igb_poll`函数被调用了最多5次（如果没有数据需要处理，那次数就会很少）
2. 时间经过了至少2个jiffies

#### 5.3.3 NAPI和设备驱动的合约

NAPI子系统和设备驱动之间的合约，最重要的一点是关闭NAPI的条件。具体如下：

1. 如果驱动的`poll`方法用完了它的全部weight（默认hardcode 64），那它**不要更改**NAPI
   状态。接下来`net_rx_action` loop会做的
2. 如果驱动的`poll`方法没有用完全部weight，那它**必须关闭**NAPI。下次有硬件中断触
   发，驱动的硬件处理函数调用`napi_schedule`时，NAPI会被重新打开

接下来先看`net_rx_action`如何处理合约的第一部分，然后看`poll`方法如何处理第二部
分。

#### 5.3.4 Finishing the `net_rx_action` loop

`net_rx_action`循环的基础部分，处理NAPI合约的第一部分。
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L4342-L4363):

```c
/* Drivers must not modify the NAPI state if they
 * consume the entire weight.  In such cases this code
 * still "owns" the NAPI instance and therefore can
 * move the instance around on the list at-will.
 */
if (unlikely(work == weight)) {
  if (unlikely(napi_disable_pending(n))) {
    local_irq_enable();
    napi_complete(n);
    local_irq_disable();
  } else {
    if (n->gro_list) {
      /* flush too old packets
       * If HZ < 1000, flush all packets.
       */
      local_irq_enable();
      napi_gro_flush(n, HZ >= 1000);
      local_irq_disable();
    }
    list_move_tail(&n->poll_list, &sd->poll_list);
  }
}
```

如果全部`work`都用完了，`net_rx_action`会有两种情况需要处理：

1. 网络设备需要关闭（例如，用户敲了`ifconfig eth0 down`命令）
2. 如果设备不需要关闭，那检查是否有GRO（后面会介绍）列表。如果时钟tick rate `>=
   1000`，所有最近被更新的GRO network flow都会被flush。将这个NAPI实例移到list末
   尾，这个循环下次再进入时，处理的就是下一个NAPI实例

这就是包处理循环如何唤醒驱动注册的`poll`方法进行包处理的过程。接下来会看到，
`poll`方法会收割网络数据，发送到上层栈进行处理。

#### 5.3.5 到达limit时退出循环

`net_rx_action`下列条件之一退出循环：

1. 这个CPU上注册的poll列表已经没有NAPI实例需要处理(`!list_empty(&sd->poll_list)`)
2. 剩余的`budget <= 0`
3. 已经满足2个jiffies的时间限制

代码：

```c
/* If softirq window is exhausted then punt.
 * Allow this to run for 2 jiffies since which will allow
 * an average latency of 1.5/HZ.
 */
if (unlikely(budget <= 0 || time_after_eq(jiffies, time_limit)))
  goto softnet_break;
```

如果跟踪`softnet_break`，会发现很有意思的东西：

From net/core/dev.c:

```c
softnet_break:
  sd->time_squeeze++;
  __raise_softirq_irqoff(NET_RX_SOFTIRQ);
  goto out;
```

`softnet_data`实例更新统计信息，软中断的`NET_RX_SOFTIRQ`被关闭。

`time_squeeze`字段记录的是满足如下条件的次数：`net_rx_action`有很多`work`要做但
是bugdget用完了，或者work还没做完但时间限制到了。这对理解网络处理的瓶颈至关重要
。我们后面会看到如何监控这个值。关闭`NET_RX_SOFTIRQ`是为了释放CPU时间给其他任务
用。这行代码是有意义的，因为只有我们有更多工作要做（还没做完）的时候才会执行到这里，
我们主动让出CPU，不想独占太久。

然后执行到了`out`标签所在的代码。另外一种条件也会跳转到`out`标签：所有NAPI实例都
处理完了，换言之，budget数量大于网络包数量，所有驱动都已经关闭NAPI，没有什么事情
需要`net_rx_action`做了。

`out`代码段在从`net_rx_action`返回之前做了一件重要的事情：调用
`net_rps_action_and_irq_enable`。Receive Packet Steering功能打开的时候，这个函数
有重要作用：唤醒其他CPU处理网络包。

我们后面会看到RPS是如何工作的。现在，让我们看看怎样监控`net_rx_action`处理循环的
健康状态，以及进入NAPI `poll`的内部，这样才能更好的理解网络栈。

#### 3.5.6 NAPI `poll`

回忆前文，驱动程序会分配一段内存用于DMA，将数据包写到内存。就像这段内存是由驱动
程序分配的一样，驱动程序也负责解绑（unmap）这些内存，读取数据，将数据送到网络栈
。

我们看下`igb`驱动如何实现这一过程的。

##### `igb_poll`

可以看到`igb_poll`代码其实相当简单。
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

        /* ... */

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

几件有意思的事情：

* 如果内核[DCA](https://lwn.net/Articles/247493/)（Direct Cache Access）功能打开了，CPU缓存是热的，对RX ring的访问会
  命中CPU cache。更多DCA信息见本文“Extra”部分。
* 然后执行`igb_clean_rx_irq`，这里做的事情非常多，我们后面看
* 然后执行`clean_complete`，判断是否仍然有work可以做。如果有，就返回budget（回忆
  ，这里是hardcode 64）。在之前我们已经看到，`net_rx_action`会将这个NAPI实例移动
  到poll列表的末尾
* 如果所有`work`都已经完成，驱动通过调用`napi_complete`关闭NAPI，并通过调用
  `igb_ring_irq_enable`重新进入可中断状态。下次中断到来的时候回重新打开NAPI

我们来看`igb_clean_rx_irq`如何将网络数据送到网络栈。

##### `igb_clean_rx_irq`

`igb_clean_rx_irq`方法是一个循环，每次处理一个包，直到budget用完，或者没有数据需要处理了。

做的几件重要事情：

1. 分配额外的buffer用于接收数据，因为已经用过的buffer被clean out了。一次分配`IGB_RX_BUFFER_WRITE (16)`个。
2. 从RX队列取一个buffer，保存到一个`skb`类型的实例中
3. 判断这个buffer是不是一个包的最后一个buffer。如果是，继续处理；如果不是，继续
   从buffer列表中拿出下一个buffer，加到skb。当数据帧的大小比一个buffer大的时候，
   会出现这种情况
4. 验证数据的layout和头信息是正确的
5. 更新`skb->len`，表示这个包已经处理的字节数
6. 设置`skb`的hash, checksum, timestamp, VLAN id, protocol字段。hash，
   checksum，timestamp，VLAN ID信息是硬件提供的，如果硬件报告checksum error，
   `csum_error`统计就会增加。如果checksum通过了，数据是UDP或者TCP数据，`skb`就会
   被标记成`CHECKSUM_UNNECESSARY`
7. 构建的skb经`napi_gro_receive()`进入协议栈
8. 更新处理过的包的统计信息
9. 循环直至处理的包数量达到budget

循环结束的时候，这个函数设置收包的数量和字节数统计信息。

接下来在进入协议栈之前，我们先开两个小差：首先是看一些如何监控和调优软中断，其次
是介绍GRO。有了这个两个背景，后面（通过`napi_gro_receive`进入）协议栈部分会更容易理解。

#### 5.3.6 监控网络数据处理

##### `/proc/net/softnet_stat`

前面看到，如果budget或者time limit到了而仍有包需要处理，那`net_rx_action`在退出
循环之前会更新统计信息。这个信息存储在该CPU的`struct softnet_data`实例中。

这些统计信息打到了`/proc/net/softnet_stat`，但不幸的是，关于这个的文档很少。每一
列代表什么并没有标题，而且列的内容会随着内核版本可能发生变化。

在内核 3.13.0中，你可以阅读内核源码，查看每一列分别对应什么。
[net/core/net-procfs.c](https://github.com/torvalds/linux/blob/v3.13/net/core/net-procfs.c#L161-L165):

```c
seq_printf(seq,
       "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
       sd->processed, sd->dropped, sd->time_squeeze, 0,
       0, 0, 0, 0, /* was fastroute */
       sd->cpu_collision, sd->received_rps, flow_limit_count);
```

其中一些的名字让人很困惑，而且在你意想不到的地方更新。在接下来的网络栈分析说，我
们会举例说明其中一些字段是何时、在哪里被更新的。前面我们已经看到了`squeeze_time`
是在`net_rx_action`在被更新的，到此时，如下数据你应该能看懂了：

```shell
$ cat /proc/net/softnet_stat
6dcad223 00000000 00000001 00000000 00000000 00000000 00000000 00000000 00000000 00000000
6f0e1565 00000000 00000002 00000000 00000000 00000000 00000000 00000000 00000000 00000000
660774ec 00000000 00000003 00000000 00000000 00000000 00000000 00000000 00000000 00000000
61c99331 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
6794b1b3 00000000 00000005 00000000 00000000 00000000 00000000 00000000 00000000 00000000
6488cb92 00000000 00000001 00000000 00000000 00000000 00000000 00000000 00000000 00000000
```

关于`/proc/net/softnet_stat`的重要细节:

1. 每一行代表一个`struct softnet_data`实例。因为每个CPU只有一个该实例，所以每行
   其实代表一个CPU
2. 每列用空格隔开，数值用16进制表示
3. 第一列 `sd->processed`，是处理的网络帧的数量。如果你使用了ethernet bonding，
   那这个值会大于总的网络帧的数量，因为ethernet bonding驱动有时会触发网络数据被
   重新处理（re-processed）
4. 第二列，`sd->dropped`，是因为处理不过来而drop的网络帧数量。后面会展开这一话题
5. 第三列，`sd->time_squeeze`，前面介绍过了，由于budget或time limit用完而退出
   `net_rx_action`循环的次数
6. 接下来的5列全是0
7. 第九列，`sd->cpu_collision`，是为了发送包而获取锁的时候有冲突的次数
8. 第十列，`sd->received_rps`，是这个CPU被其他CPU唤醒去收包的次数
9. 最后一列，`flow_limit_count`，是达到flow limit的次数。flow limit是RPS的特性，
   后面会稍微介绍一下

如果你要画图监控这些数据，确保你的列和相应的字段是对的上的，最保险的方式是阅读相
应版本的内核代码。

#### 5.3.7 网络数据处理调优

##### 调整`net_rx_action` budget

`net_rx_action` budget表示一个CPU单次轮询（`poll`）所允许的最大收包数量。单次
poll收包是，所有注册到这个CPU的NAPI实例收包数量之和不能大于这个阈值。 调整：

```shell
$ sudo sysctl -w net.core.netdev_budget=600
```

如果要保证重启仍然生效，需要将这个配置写到`/etc/sysctl.conf`。

Linux 3.13.0的默认配置是300。

### 5.4 GRO（Generic Receive Offloading）

Large Receive Offloading (LRO) 是一个硬件优化，GRO是LRO的一种软件实现。

两种方案的主要思想都是：通过合并“足够类似”的包来减少往网络栈传送的包的数量，这有
助于减少CPU的使用量。例如，考虑大文件传输的场景，包的数量非常多，大部分包都是一
段文件数据。相比于每次都将小包送到网络栈，可以将收到的小包合并成一个很大的包再送
到网络栈。这可以使得协议层只需要处理一个header，而将包含大量数据的整个大包送到用
户程序。

这类优化方式的缺点就是：信息丢失。如果一个包有一些重要的option或者flag，那将这个
包的数据合并到其他包时，这些信息就会丢失。这也是为什么大部分人不使用或不推荐使用
LRO的原因。

LRO的实现，一般来说，对合并包的规则非常宽松。GRO是LRO的软件实现，但是对于包合并
的规则更严苛。

顺便说一下，如果你曾经用过tcpdump抓包，并收到看起来不现实的非常大的包，那很可能
是你的系统开启了GRO。你接下来会看到，捕获包的tap在整个栈的更后面一下，在GRO之
后。

#### 使用ethtool修改GRO配置

`-k`查看GRO配置：

```shell
$ ethtool -k eth0 | grep generic-receive-offload
generic-receive-offload: on
```

`-K`修改GRO配置：

```shell
$ sudo ethtool -K eth0 gro on
```

注意：对于大部分驱动，修改GRO配置会涉及先down再up这个网卡，因此这个网卡上的连接
都会中断。

### 5.5 `napi_gro_receive`

如果开启了GRO，`napi_gro_receive`将负责处理网络数据，并将数据送到协议栈，大部分
相关的逻辑在函数`dev_gro_receive`里实现。

#### `dev_gro_receive`

这个函数首先检查GRO是否开启了，如果是，就准备做GRO。GRO首先遍历一个offload
filter列表，如果高层协议认为其中一些数据属于GRO处理的范围，就会允许其对数据进行
操作。

协议层以此方式让网络设备层知道，这个packet是不是当前正在处理的一个需要做GRO的
network flow的一部分，而且也可以通过这种方式传递一些协议相关的信息。例如，TCP协
议需要判断是否以及合适应该将一个ACK包合并到其他包里。

[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3844-L3856):

```c
list_for_each_entry_rcu(ptype, head, list) {
  if (ptype->type != type || !ptype->callbacks.gro_receive)
    continue;

  skb_set_network_header(skb, skb_gro_offset(skb));
  skb_reset_mac_len(skb);
  NAPI_GRO_CB(skb)->same_flow = 0;
  NAPI_GRO_CB(skb)->flush = 0;
  NAPI_GRO_CB(skb)->free = 0;

  pp = ptype->callbacks.gro_receive(&napi->gro_list, skb);
  break;
}
```

如果协议层提示是时候flush GRO packet了，那就到下一步处理了。这发生在
`napi_gro_complete`，会进一步调用相应协议的`gro_complete`回调方法，然后调用
`netif_receive_skb`将包送到协议栈。
这个过程见[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3862-L3872)：

```c
if (pp) {
  struct sk_buff *nskb = *pp;

  *pp = nskb->next;
  nskb->next = NULL;
  napi_gro_complete(nskb);
  napi->gro_count--;
}
```

接下来，如果协议层将这个包合并到一个已经存在的flow，`napi_gro_receive`就没什么事
情需要做，因此就返回了。如果packet没有被合并，而且GRO的数量小于 `MAX_GRO_SKBS`（
默认是8），就会创建一个新的entry加到本CPU的NAPI实例的`gro_list`。
[net/core/dev.c](https://github.com/torvalds/linux/blob/v3.13/net/core/dev.c#L3877-L3886)：

```c
if (NAPI_GRO_CB(skb)->flush || napi->gro_count >= MAX_GRO_SKBS)
  goto normal;

napi->gro_count++;
NAPI_GRO_CB(skb)->count = 1;
NAPI_GRO_CB(skb)->age = jiffies;
skb_shinfo(skb)->gso_size = skb_gro_len(skb);
skb->next = napi->gro_list;
napi->gro_list = skb;
ret = GRO_HELD;
```

这就是Linux网络栈中GRO的工作原理。

### 5.6 `napi_skb_finish`

一旦`dev_gro_receive`完成，`napi_skb_finish`就会被调用，其如果一个packet被合并了
，就释放不用的变量；或者调用`netif_receive_skb`将数据发送到网络协议栈（因为已经
有`MAX_GRO_SKBS`个flow了，够GRO了）。

接下来，是看`netif_receive_skb`如何将数据交给协议层。但在此之前，我们先看一下RPS。

## 6 RPS (Receive Packet Steering)

回忆前面我们讨论了网络设备驱动是如何注册NAPI `poll`方法的。每个NAPI实例都会运
行在相应CPU的软中断的上下文中。而且，触发硬中断的这个CPU接下来会负责执行相应的软
中断处理函数来收包。

换言之，同一个CPU既处理硬中断，又处理相应的软中断。

一些网卡（例如Intel I350）在硬件层支持多队列。这意味着收进来的包会被通过DMA放到
位于不同内存的队列上，而不同的队列有相应的NAPI实例管理软中断`poll()`过程。因此，
多个CPU同时处理从网卡来的中断，处理收包过程。

这个特性被称作RSS（Receive Side Scaling，接收端扩展）。

[RPS](https://github.com/torvalds/linux/blob/v3.13/Documentation/networking/scaling.txt#L99-L222)
（Receive Packet Steering，接收包控制，接收包引导）是RSS的一种软件实现。因为是软
件实现的，意味着任何网卡都可以使用这个功能，即便是那些只有一个接收队列的网卡。但
是，因为它是软件实现的，这意味着RPS只能在packet通过DMA进入内存后，RPS才能开始工
作。

这意味着，RPS并不会减少CPU处理硬件中断和NAPI `poll`（软中断最重要的一部分）的时
间，但是可以在packet到达内存后，将packet分到其他CPU，从其他CPU进入协议栈。

RPS的工作原理是对个packet做hash，以此决定分到哪个CPU处理。然后packet放到每个CPU
独占的接收后备队列（backlog）等待处理。这个CPU会触发一个进程间中断（
[IPI](https://en.wikipedia.org/wiki/Inter-processor_interrupt)，Inter-processor
Interrupt）向对端CPU。如果当时对端CPU没有在处理backlog队列收包，这个进程间中断会
触发它开始从backlog收包。`/proc/net/softnet_stat`其中有一列是记录`softnet_data`
实例（也即这个CPU）收到了多少IPI（`received_rps`列）。

因此，`netif_receive_skb`或者继续将包送到协议栈，或者交给RPS，后者会转交给其他CPU处理。

### RPS调优

使用RPS需要在内核做配置（Ubuntu + Kernel 3.13.0 支持），而且需要一个掩码（
bitmask）指定哪些CPU可以处理那些RX队列。相关的一些信息可以在[内核文档
](https://github.com/torvalds/linux/blob/v3.13/Documentation/networking/scaling.txt#L138-L164)
里找到。

bitmask配置位于：`/sys/class/net/DEVICE_NAME/queues/QUEUE/rps_cpus`。

例如，对于eth0的queue 0，你需要更改`/sys/class/net/eth0/queues/rx-0/rps_cpus`。[
内核文档
](https://github.com/torvalds/linux/blob/v3.13/Documentation/networking/scaling.txt#L160-L164)
里说，对一些特定的配置下，RPS没必要了。

注意：打开RPS之后，原来不需要处理软中断（softirq）的CPU这时也会参与处理。因此相
应CPU的`NET_RX`数量，以及`si`或`sitime`占比都会相应增加。你可以对比启用RPS前后的
数据，以此来确定你的配置是否生效，以及是否符合预期（哪个CPU处理哪个网卡的哪个中
断）。

## 7 RFS (Receive Flow Steering)

RFS（Receive flow steering）和RPS配合使用。RPS试图在CPU之间平衡收包，但是没考虑
数据的本地性问题，如何最大化CPU缓存的命中率。RFS将属于相同flow的包送到相同的CPU
进行处理，可以提高缓存命中率。


### 调优：打开RFS

RPS记录一个全局的hash table，包含所有flow的信息。这个hash table的大小可以在`net.core.rps_sock_flow_entries`：

```
$ sudo sysctl -w net.core.rps_sock_flow_entries=32768
```

其次，你可以设置每个RX queue的flow数量，对应着`rps_flow_cnt`：

例如，eth0的RX queue0的flow数量调整到2048：

```
$ sudo bash -c 'echo 2048 > /sys/class/net/eth0/queues/rx-0/rps_flow_cnt'
```

## 8 aRFS (Hardware accelerated RFS)

RFS可以用硬件加速，网卡和内核协同工作，判断哪个flow应该在哪个CPU上处理。这需要网
卡和网卡驱动的支持。

如果你的网卡驱动里对外提供一个`ndo_rx_flow_steer`函数，那就是支持RFS。

### 调优: 启用aRFS

假如你的网卡支持aRFS，你可以开启它并做如下配置：

* 打开并配置RPS
* 打开并配置RFS
* 内核中编译期间指定了`CONFIG_RFS_ACCEL`选项。Ubuntu kernel 3.13.0是有的
* 打开网卡的ntuple支持。可以用ethtool查看当前的ntuple设置
* 配置IRQ（硬中断）中每个RX和CPU的对应关系

以上配置完成后，aRFS就会自动将RX queue数据移动到指定CPU的内存，每个flow的包都会
到达同一个CPU，不需要你再通过ntuple手动指定每个flow的配置了。
