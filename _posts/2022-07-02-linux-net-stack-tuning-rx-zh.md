---
layout    : post
title     : "Linux 网络栈接收数据（RX）：配置调优（2022）"
date      : 2022-07-02
lastupdate: 2024-01-13
categories: network kernel tuning
---

本文尝试从技术研发与工程实践（而非纯理论学习）角度，在原理与实现、监控告警、
配置调优三方面介绍内核 5.10 网络栈。由于内容非常多，因此分为了几篇系列文章。

**原理与实现**

1. [Linux 网络栈原理、监控与调优：前言]({% link _posts/2022-07-02-linux-net-stack-zh.md %})
1. [Linux 中断（IRQ/softirq）基础：原理及内核实现]({% link _posts/2022-07-02-linux-irq-softirq-zh.md %})
1. [Linux 网络栈接收数据（RX）：原理及内核实现]({% link _posts/2022-07-02-linux-net-stack-implementation-rx-zh.md %})

**监控**

1. [Monitoring Linux Network Stack]({% link _posts/2022-07-02-monitoring-network-stack.md %})

**调优**

1. [Linux 网络栈接收数据（RX）：配置调优]({% link _posts/2022-07-02-linux-net-stack-tuning-rx-zh.md %})

----

* TOC
{:toc}

----

网络栈非常复杂，没有一种放之四海而皆准的通用配置。如果网络性能和指标对你们团队和业务非常重要，
那别无选择，只能投入大量的时间、精力和资源去深入理解系统的各个部分是如何工作的。
理想情况下，应该监控**<mark>网络栈各个层级的丢包及其他健康状态</mark>**，
这样遇到问题时就能快速缩小范围，判断哪个组件或模块需要调优。

本文展示一些从下到上的配置调优示例，但注意这些配置并不作为任何特定配置或默认配置的建议。此外，

* 在任何配置变更之前，应该有一个能够对系统进行[<mark>监控</mark>]({% link _posts/2022-07-02-monitoring-network-stack.md %})的框架，以确认调整是否带来预期的效果；
* 对远程连接上的机器进行网络变更是相当危险的，机器很可能失联；
* 不要在生产环境直接调整配置；尽量先在线下或新机器上验证效果，然后灰度到生产。

本文章号与图中对应。 接下来就从最底层的网卡开始。

<p align="center"><img src="/assets/img/linux-net-stack/rx-overview.png" width="70%" height="70%"></p>
<p align="center">Fig. Steps of Linux kernel receiving data process and the corresponding chapters in this post</p>

# 1 网络设备驱动初始化

1. RX 队列的数量和大小可以通过 ethtool 进行配置，这两个参数会对收包或丢包产生显著影响。
1. 网卡通过对 packet 头（例如源地址、目的地址、端口等）做哈希来决定将 packet 放到
  哪个 RX 队列。对于支持自定义哈希的网卡，可以通过自定义算法将特定
  的 flow 发到特定的队列，甚至可以做到在硬件层面直接将某些包丢弃。
1. 一些网卡支持调整 RX 队列的权重，将流量按指定的比例发到指定的 queue。

## 1.1 调整 RX 队列数量（`ethtool -l/-L`）

如果网卡及其驱动支持 RSS/多队列，可以调整 RX queue（也叫 RX channel）的数量。

```shell
$ sudo ethtool -l eth0
Channel parameters for eth0:
Pre-set maximums:
RX:             0
TX:             0
Other:          0
Combined:       40
Current hardware settings:
RX:             0
TX:             0
Other:          0
Combined:       40
```

可以看到硬件最多支持 40 个，当前也用满了 40 个。

> 注意：**<mark>不是所有网卡驱动都支持这个操作</mark>**。不支持的网卡会报如下错误：
>
> ```shell
> $ sudo ethtool -l eth0
> Channel parameters for eth0:
> Cannot get device channel parameters
> : Operation not supported
> ```
>
> 这意味着驱动没有实现 ethtool 的 `get_channels()` 方法。可能原因：该网卡不支持
> 调整 RX queue 数量，不支持 RSS/multiqueue 等。

**<mark><code>ethtool -L</code></mark>** 可以修改 RX queue 数量（`ethtool` 参数
有个惯例，小写一般都是查询某个配置，对应的大写表示修改这个配置）。不过这里需要注意，

* 某些厂商的网卡中，RX 队列和 TX 队列是可以独立调整的，例如修改 RX queue 数量：

    ```shell
    $ sudo ethtool -L eth0 rx 8
    ```

* 另一些厂商网卡中，二者是一一绑定的，称为 **<mark>combined queue</mark>**，这种模式下，
  调整 RX 队列的数量也会同时调整 TX queue 的数量，例如

    ```shell
    $ sudo ethtool -L eth0 combined 8 # 将 RX 和 TX queue 数量都设为了 8
    ```

注意：**<mark>对于大部分驱动，修改以上配置会使网卡先 down 再 up，因此会造成丢包！</mark>**
请谨慎操作。

## 1.2 调整 RX 队列大小（`ethtool -g/-G`）

也就是调整每个 RX 队列中 descriptor 的数量，一个 descriptor 对应一个包。
这个能不能调整也要看具体的网卡和驱动。增大 ring buffer 可以在 PPS（packets per
second）很大时缓解丢包问题。

查看 queue 的大小：

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

以上输出显示网卡最多支持 4096 个 RX/TX descriptor，但是现在只用到了 512 个。
**<mark><code>ethtool -G</code></mark>** 修改 queue 大小：

```shell
$ sudo ethtool -G eth0 rx 4096
```

注意：对于大部分驱动，**<mark>修改以上配置会使网卡先 down 再 up，因此会造成丢包</mark>**。请谨慎操作。

## 1.4 调整 RX 队列权重（`ethtool -x/-X`）

一些网卡支持给不同的 queue 设置不同的权重（weight），权重越大，
每次网卡 `poll()` 能处理的包越多。如果网卡支持以下功能，就可以设置权重：

1. 支持 flow indirection；
1. 驱动实现了 `get_rxfh_indir_size()` 和 `get_rxfh_indir()` 方法；

检查 flow indirection 设置：

```shell
$ sudo ethtool -x eth0
RX flow hash indirection table for eth0 with 40 RX ring(s):
    0:      0     1     2     3     4     5     6     7
    8:      8     9    10    11    12    13    14    15
   16:     16    17    18    19    20    21    22    23
   24:     24    25    26    27    28    29    30    31
   32:     32    33    34    35    36    37    38    39
   40:      0     1     2     3     4     5     6     7
   48:      8     9    10    11    12    13    14    15
   ...
RSS hash key:
9a:b0:e3:53:ed:d4:14:7a:a0:...:e5:57:e8:6a:ec
RSS hash function:
    toeplitz: off
    xor: on
    crc32: off
```

第一列是改行的第一个哈希值，冒号后面的每个哈希值对应的 RX queue。例如，

* 第一行的哈希值是 0~7，分别对应 RX queue 0~7；
* 第六行的哈希值是 40~47，分别对应的也是 RX queue 0~7。

```shell
# 在前两个 RX queue 之间均匀的分发接收到的包
$ sudo ethtool -X eth0 equal 2

# 设置自定义权重：给 rx queue 0 和 1 不同的权重：6 和 2
$ sudo ethtool -X eth0 weight 6 2
```

注意 queue 一般是和 CPU 绑定的，因此这也意味着相应的 CPU 也会花更多的时间片在收包上。
一些网卡还支持修改计算 hash 时使用哪些字段。

## 1.5 调整 RSS RX 哈希字段（`ethtool -n/-N`）

可以用 ethtool 调整 RSS 计算哈希时所使用的字段。

例子：查看 UDP RX flow 哈希所使用的字段：

```shell
$ sudo ethtool -n eth0 rx-flow-hash udp4
UDP over IPV4 flows use these fields for computing Hash flow key:
IP SA
IP DA
```

可以看到只用到了源 IP（SA：Source Address）和目的 IP。

我们修改一下，加入源端口和目的端口：

```shell
$ sudo ethtool -N eth0 rx-flow-hash udp4 sdfn
```

`sdfn` 的具体含义解释起来有点麻烦，请查看 ethtool 的帮助（man page）。

调整 hash 所用字段是有用的，而 `ntuple` 过滤对于更加细粒度的 flow control 更加有用。

## 1.6 Flow 绑定到 CPU：ntuple filtering（`ethtool -k/-K, -u/-U`）

一些网卡支持 “ntuple filtering” 特性。该特性允许用户（通过 ethtool ）指定一些参数来
在硬件上过滤收到的包，然后将其直接放到特定的 RX queue。例如，用户可以指定到特定目
端口的 TCP 包放到 RX queue 1。

Intel 的网卡上这个特性叫 Intel Ethernet Flow Director，其他厂商可能也有他们的名字
，这些都是出于市场宣传原因，底层原理是类似的。

ntuple filtering 其实是 Accelerated Receive Flow Steering (aRFS) 功能的核心部分之一，
这个功能在原理篇中已经介绍过了。aRFS 使得 ntuple filtering 的使用更加方便。

适用场景：最大化数据局部性（data locality），提高 CPU 处理网络数据时的
缓存命中率。例如，考虑运行在 80 口的 web 服务器：

1. webserver 进程运行在 80 口，并绑定到 CPU 2
1. 和某个 RX queue 关联的硬中断绑定到 CPU 2
1. 目的端口是 80 的 TCP 流量通过 ntuple filtering 绑定到 CPU 2
1. 接下来所有到 80 口的流量，从数据包进来到数据到达用户程序的整个过程，都由 CPU 2 处理
1. 监控系统的缓存命中率、网络栈的延迟等信息，以验证以上配置是否生效

检查 ntuple filtering 特性是否打开：

```shell
$ sudo ethtool -k eth0
Offload parameters for eth0:
...
ntuple-filters: off
receive-hashing: on
```

可以看到，上面的 ntuple 是关闭的。

打开：

```shell
$ sudo ethtool -K eth0 ntuple on
```

打开 ntuple filtering 功能，并确认打开之后，可以用 `ethtool -u` 查看当前的 ntuple
rules：

```shell
$ sudo ethtool -u eth0
40 RX rings available
Total 0 rules
```

可以看到当前没有 rules。

我们来加一条：目的端口是 80 的放到 RX queue 2：

```shell
$ sudo ethtool -U eth0 flow-type tcp4 dst-port 80 action 2
```

也可以用 ntuple filtering **<mark>在硬件层面直接 drop 某些 flow 的包</mark>**。
当特定 IP 过来的流量太大时，这种功能可能会派上用场。更多关于 ntuple 的信息，参考 ethtool man page。

`ethtool -S <DEVICE>` 的输出统计里，Intel 的网卡有 `fdir_match` 和 `fdir_miss` 两项，
是和 ntuple filtering 相关的。关于具体、详细的统计计数，需要查看相应网卡的设备驱
动和 data sheet。

# 2 网卡收包

# 3 DMA 将包复制到 RX 队列

# 4 IRQ

## 4.1 中断合并（Interrupt coalescing，`ethtool -c/-C`）

中断合并会将多个中断事件放到一起，累积到一定阈值后才向 CPU 发起中断请求。

* 优点：防止**中断风暴**，提升吞吐，降低 CPU 使用量
* 缺点：延迟变大

查看：

```shell
$ ethtool -c eth0
Coalesce parameters for eth0:
Adaptive RX: on  TX: on        # 自适应中断合并
stats-block-usecs: 0
sample-interval: 0
pkt-rate-low: 0
pkt-rate-high: 0

rx-usecs: 8
rx-frames: 128
rx-usecs-irq: 0
rx-frames-irq: 0

tx-usecs: 8
tx-frames: 128
tx-usecs-irq: 0
tx-frames-irq: 0

rx-usecs-low: 0
rx-frame-low: 0
tx-usecs-low: 0
tx-frame-low: 0

rx-usecs-high: 0
rx-frame-high: 0
tx-usecs-high: 0
tx-frame-high: 0
```

不是所有网卡都支持这些配置。根据 ethtool 文档：**“驱动没有实现的接口将会被静默忽略”**。

某些驱动支持“自适应 RX/TX 硬中断合并”，效果是带宽比较低时降低延迟，带宽比较高时
提升吞吐。这个特性一般是在硬件实现的。

用 `ethtool -C` 打开自适应 RX IRQ 合并：

```shell
$ sudo ethtool -C eth0 adaptive-rx on
```

还可以用 `ethtool -C` 更改其他配置。常用的包括：

* `rx-usecs`: How many usecs to delay an RX interrupt after a packet arrives.
* `rx-frames`: Maximum number of data frames to receive before an RX interrupt.
* `rx-usecs-irq`: How many usecs to delay an RX interrupt while an interrupt is being serviced by the host.
* `rx-frames-irq`: Maximum number of data frames to receive before an RX interrupt is generated while the system is servicing an interrupt.

每个配置项的含义见
[include/uapi/linux/ethtool.h](https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/ethtool.h)。

注意：虽然硬中断合并看起来是个不错的优化项，但需要网络栈的其他一些
部分做针对性调整。只合并硬中断很可能并不会带来多少收益。

## 4.2 调整硬中断亲和性（IRQ affinities，`/proc/irq/<id>/smp_affinity`）

这种方式能手动配置哪个 CPU 负责处理哪个 IRQ。
但在配置之前，需要先确保关闭 `irqbalance` 进程（或者设置 `--banirq` 指定不要对那些 CPU 做 balance）
否则它会定期自动平衡 IRQ 和 CPU 映射关系，覆盖我们的手动配置。

然后，通过 `cat /proc/interrupts` 查看网卡的每个 RX 队列对应的 IRQ 编号。

最后，通过设置 `/proc/irq/<IRQ_NUMBER>/smp_affinity` 来指定哪个 CPU 来处理这个 IRQ。
注意这里的格式是 16 进制的 bitmask。

例子：指定 CPU 0 来处理 IRQ 8：

```shell
$ sudo bash -c 'echo 1 > /proc/irq/8/smp_affinity'
```

# 5 SoftIRQ

## 5.1 问题讨论

### 关于 NAPI pool 机制

* 这是 Linux 内核中的一种通用抽象，任何等待**不可抢占状态**发生（wait for a
  preemptible state to occur）的模块，都可以使用这种注册回调函数的方式。
* 驱动注册的这个 poll 是一个**主动式 poll**（active poll），一旦执行就会持续处理
  ，直到没有数据可供处理，然后进入 idle 状态。
* 在这里，执行 poll 方法的是运行在某个或者所有 CPU 上的**内核线程**（kernel thread）。
  虽然这个线程没有数据可处理时会进入 idle 状态，但如前面讨论的，在当前大部分分布
  式系统中，这个线程大部分时间内都是在运行的，不断从驱动的 DMA 区域内接收数据包。
* poll 会告诉网卡不要再触发硬件中断，使用**软件中断**（softirq）就行了。此后这些
  内核线程会轮询网卡的 DMA 区域来收包。之所以会有这种机制，是因为硬件中断代价太
  高了，因为它们比系统上几乎所有东西的优先级都要高。

我们接下来还将多次看到这个广义的 NAPI 抽象，因为它不仅仅处理驱动，还能处理许多
其他场景。内核用 NAPI 抽象来做驱动读取（driver reads）、epoll 等等。

NAPI 驱动的 poll 机制将数据从 DMA 区域读取出来，对数据做一些准备工作，然后交给比
它更上一层的内核协议栈。

软中断的信息可以从 `/proc/softirqs` 读取：

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

例如，`NET_RX` 一行显示的是软中断在 CPU 间的分布。如果分布非常不均匀，那某一列的
值就会远大于其他列，这预示着下面要介绍的 Receive Packet Steering / Receive Flow
Steering 可能会派上用场。但也要注意：不要太相信这个数值，`NET_RX` 太高并不一定都
是网卡触发的，其他地方也有可能触发。

调整其他网络配置时，可以留意下这个指标的变动。

### perf 跟踪 IRQ/Softirq 调用

```shell
$ sudo perf record -a \
    -e irq:irq_handler_entry,irq:irq_handler_exit \
    -e irq:softirq_entry --filter="vec == 3" \
    -e irq:softirq_exit --filter="vec == 3"  \
    -e napi:napi_poll \
    -C 1 \
    -- sleep 2

$ sudo perf script
```

```shell
$ perf stat -C 1 -e irq:softirq_entry,irq:softirq_exit,irq:softirq_raise -a sleep 10

 Performance counter stats for 'system wide':

             1,161      irq:softirq_entry
             1,161      irq:softirq_exit
             1,215      irq:softirq_raise

      10.001100401 seconds time elapsed
```

### `/proc/net/softnet_stat` 各字段说明

前面看到，如果 budget 或者 time limit 到了而仍有包需要处理，那 `net_rx_action` 在退出
循环之前会更新统计信息。这个信息存储在该 CPU 的 `struct softnet_data` 变量中。

这些统计信息打到了`/proc/net/softnet_stat`，但不幸的是，关于这个的文档很少。每一
列代表什么并没有标题，而且列的内容会随着内核版本可能发生变化，所以应该以内核源码为准，
下面是内核 5.10，可以看到每列分别对应什么：

```c
// https://github.com/torvalds/linux/blob/v5.10/net/core/net-procfs.c#L172

static int softnet_seq_show(struct seq_file *seq, void *v)
{
    ...
    seq_printf(seq,
           "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
           sd->processed, sd->dropped, sd->time_squeeze, 0,
           0, 0, 0, 0, /* was fastroute */
           0,    /* was cpu_collision */
           sd->received_rps, flow_limit_count,
           softnet_backlog_len(sd), (int)seq->index);
}
```

```shell
$ cat /proc/net/softnet_stat
6dcad223 00000000 00000001 00000000 00000000 00000000 00000000 00000000 00000000 00000000
6f0e1565 00000000 00000002 00000000 00000000 00000000 00000000 00000000 00000000 00000000
660774ec 00000000 00000003 00000000 00000000 00000000 00000000 00000000 00000000 00000000
61c99331 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
6794b1b3 00000000 00000005 00000000 00000000 00000000 00000000 00000000 00000000 00000000
6488cb92 00000000 00000001 00000000 00000000 00000000 00000000 00000000 00000000 00000000
```

每一行代表一个 `struct softnet_data` 变量。因为每个 CPU 只有一个该变量，所以每行其实代表一个 CPU；
数字都是 16 进制表示。字段说明：

* 第一列 `sd->processed`：处理的网络帧数量。**<mark>如果用了 ethernet bonding，那这个值会大于总帧数</mark>**，
  因为 bond 驱动有时会触发帧的重处理（re-processed）；
* 第二列 `sd->dropped`：因为处理不过来而 drop 的网络帧数量；具体见原理篇；
* 第三列 `sd->time_squeeze`：由于 budget 或 time limit 用完而退出 `net_rx_action()` 循环的次数；原理篇中有更多分析；
* 接下来的 5 列全是 0；
* 第九列 `sd->cpu_collision`：为发送包而获取锁时冲突的次数；
* 第十列 `sd->received_rps`：当前 CPU 被其他 CPU 唤醒去收包的次数；
* 最后一列，`flow_limit_count`：达到 flow limit 的次数；这是 RPS 特性。

## 5.2 调整 softirq 收包预算：`sysctl netdev_budget/netdev_budget_usecs`

权威解释见 [内核文档](https://github.com/torvalds/linux/blob/v5.10/Documentation/admin-guide/sysctl/net.rst#netdev_budget)。

* **<mark><code>netdev_budget</code></mark>**：一个 **<mark>CPU 单次轮询所允许的最大收包数量</mark>**。
  单次 poll 收包时，所有注册到这个 CPU 的 NAPI 变量收包数量之和不能大于这个阈值。
* **<mark><code>netdev_budget_usecs</code></mark>**：每次 NAPI poll cycle 的最长允许时间，单位是 `us`。

触发二者中任何一个条件后，都会导致一次轮询结束。

查看当前配置：

```shell
$ sudo sysctl -a | grep netdev_budget
net.core.netdev_budget = 300         # kernel 5.10 默认值
net.core.netdev_budget_usecs = 2000  # kernel 5.10 默认值
```

修改配置：

```shell
$ sudo sysctl -w net.core.netdev_budget=3000
$ sudo sysctl -w net.core.netdev_budget_usecs = 10000
```

要保证重启不丢失，需要将这个配置写到 `/etc/sysctl.conf`。

# 6 softirq：从 ring buffer 收包送到协议栈

## 6.1 修改 GRO 配置（`ethtool -k/-K`）

查看 GRO 配置：

```shell
$ ethtool -k eth0 | grep generic-receive-offload
generic-receive-offload: on
```

修改 GRO 配置：

```shell
$ sudo ethtool -K eth0 gro on
```

注意：对于大部分驱动，修改 GRO 配置会涉及先 down 再 up 这个网卡，因此这个网卡上的连接都会中断。

## 6.2 `sysctl gro_normal_batch`

```shell
$ sysctl net.core.gro_normal_batch
net.core.gro_normal_batch = 8
```

## 6.3 RPS 调优

使用 RPS 需要在内核做配置，而且需要一个掩码（bitmask）指定哪些 CPU 可以处理那些 RX 队列。相关信息见
[内核文档](https://github.com/torvalds/linux/blob/v5.10/Documentation/networking/scaling.rst)。

bitmask 配置位于：`/sys/class/net/DEVICE_NAME/queues/QUEUE/rps_cpus`。


例如，对于 eth0 的 queue 0，需要更改`/sys/class/net/eth0/queues/rx-0/rps_cpus`。

注意：打开 RPS 之后，原来不需要处理软中断（softirq）的 CPU 这时也会参与处理。因此相
应 CPU 的 `NET_RX` 数量，以及 `si` 或 `sitime` 占比都会相应增加。可以对比启用 RPS 前后的
数据，以此来确定配置是否生效以及是否符合预期（哪个 CPU 处理哪个网卡的哪个中断）。

## 6.4 调优：打开 RFS

RPS 记录一个全局的 hash table，包含所有 flow 的信息，这个 hash table 的大小是 `net.core.rps_sock_flow_entries`：

```shell
$ sysctl -a | grep rps_
net.core.rps_sock_flow_entries = 0 # kernel 5.10 默认值

# 如果要修改
$ sudo sysctl -w net.core.rps_sock_flow_entries=32768
```

其次，可以设置每个 RX queue 的 flow 数量，对应着 `rps_flow_cnt`：

例如，eth0 的 RX queue0 的 flow 数量调整到 2048：

```
$ sudo bash -c 'echo 2048 > /sys/class/net/eth0/queues/rx-0/rps_flow_cnt'
```

## 6.5 调优: 启用 aRFS

假如网卡支持 aRFS，可以开启它并做如下配置：

* 打开并配置 RPS
* 打开并配置 RFS
* 内核中编译期间指定了 `CONFIG_RFS_ACCEL` 选项
* 打开网卡的 ntuple 支持。可以用 ethtool 查看当前的 ntuple 设置
* 配置 IRQ（硬中断）中每个 RX 和 CPU 的对应关系

以上配置完成后，aRFS 就会自动将 RX queue 数据移动到指定 CPU 的内存，每个 flow 的包都会
到达同一个 CPU，不需要再通过 ntuple 手动指定每个 flow 的配置了。

# 7 协议栈：L2 处理

## 7.1 调优: 何时给包打时间戳（`sysctl net.core.netdev_tstamp_prequeue`）

决定包被收到后，何时给它打时间戳。

```shell
$ sysctl net.core.netdev_tstamp_prequeue
1 # 内核 5.10 默认值
```

## 7.2 调优（老驱动）

### `netdev_max_backlog`

如果启用了 RPS，或者你的网卡驱动调用了 `netif_rx()`（大部分网卡都不会再调用这个函数了），
那增加 `netdev_max_backlog` 可以改善在 `enqueue_to_backlog` 里的丢包：

```shell
$ sudo sysctl -w net.core.netdev_max_backlog=3000
```

默认值是 1000。

### NAPI weight of the backlog poll loop

`net.core.dev_weight` 决定了 backlog poll loop 可以消耗的整体 budget（参考前面更改
`net.core.netdev_budget` 的章节）：

```shell
$ sudo sysctl -w net.core.dev_weight=600
```

默认值是 64。

记住，backlog 处理逻辑和设备驱动的 `poll` 函数类似，都是在软中断（softirq）的上下文
中执行，因此受整体 budget 和处理时间的限制。

## 7.3 调优：`sysctl net.core.flow_limit_table_len`

```shell
$ sudo sysctl -w net.core.flow_limit_table_len=8192
```

默认值是 4096。

这只会影响新分配的 flow hash table。所以，如果想增加 table size 的话，应该在打开
flow limit 功能之前设置这个值。

打开 flow limit 功能的方式是，在`/proc/sys/net/core/flow_limit_cpu_bitmap` 中指定一
个 bitmask，和通过 bitmask 打开 RPS 的操作类似。

# 8 协议栈：L3 处理（IPv4）

## 8.1 调优: 打开或关闭 IP 协议的 early demux 选项

查看 `early_demux` 配置：

```shell
$ sudo sysctl net.ipv4.ip_early_demux
1 # 内核 5.10 默认值
```

默认是 1，即该功能默认是打开的。

添加这个 `sysctl` 开关的原因是，一些用户报告说，在某些场景下 `early_demux` 优化会导
致 ~5% 左右的吞吐量下降。

# 9 协议栈：L4 处理（UDP）

## 9.1 调优: socket receive buffer（`sysctl net.core.rmem_default/rmem_max`）

判断 socket 接收队列是否满了是和 `sk->sk_rcvbuf` 做比较。
这个值可以通过 sysctl 配置：

```shell
$ sysctl -a | grep rmem                # kernel 5.10 defaults
net.core.rmem_default = 212992                   # ~200KB
net.core.rmem_max = 212992                       # ~200KB
net.ipv4.tcp_rmem = 4096        131072  6291456
net.ipv4.udp_rmem_min = 4096
```

默认的 `200KB` 可能太小了，例如 QUIC 可能需要 [MB 级别的配置](https://github.com/lucas-clemente/quic-go/wiki/UDP-Receive-Buffer-Size)。

有两种修改方式：

1. 全局：`sysctl` 或 echo sysfs 方式
2. 应用程序级别：在应用程序里通过 `setsockopt` 带上 `SO_RCVBUF` flag 来修改这个值 (`sk->sk_rcvbuf`)，能设置的最大值不超过 `net.core.rmem_max`。
  如果有 `CAP_NET_ADMIN` 权限，也可以 `setsockopt` 带上 `SO_RCVBUFFORCE` 来覆盖 `net.core.rmem_max`。

实际中比较灵活的方式：

1. `rmem_default` 不动，这样 UDP 应用默认将仍然使用系统预设值；
2. `rmem_max` 调大（例如 `2.5MB`），有需要的应用可以自己通过 `setsockopt()` 来调大自己的 buffer。

# 10 全局调优：一些影响网络性能的非网络配置

## 10.1 CPU 动态跳频（节能模式）导致收发包不及时

业务现象：已经是独占 CPU（cpuset）类型的 pod，访问别人有偶发超时。

排查过程：

1. 查看容器和宿主机 CPU 利用率、load 之类的，都不算高，但 **<mark><code>time_squeeze</code></mark>** 计数一直断断续续有增加，说明收发包的 softirq 时间片不够；
2. 综合几个 case 看，发现出问题的都是 AMD 机型，怀疑跟 CPU 有关系；
3. **<mark><code>lscpu</code></mark>** 发现频率、缓存等等跟 Intel CPU 确实差异比较大；
4. 针对 AMD 机型配置看板，发现这些 node CPU 频率一直在变，

    <p align="center"><img src="/assets/img/linux-net-stack/amd-nodes-cpuhz.png" width="100%" height="100%"></p>
    <p align="center">Fig. CPU HZ of the AMD nodes</p>

    这里的 HZ 是通过如下命令采集的：

    ```shell
    $ lscpu | grep "CPU MHz"
    CPU MHz:                         2987.447
    ```

根据以上信息，判断这些 AMD 机器的 CPU 处在节能模式，根据负载高低自动调整频率。

解决方式：修改系统启动项，**<mark><code>/etc/default/grub</code></mark>** 添加如下内容：

```
AMD_pstat=disable idle=poll nohz=off iommu=pt
```

说明：

1. **<mark><code>nohz=off</code></mark>**：`"nohz=off" == "enable hz"`，
  也就是保留**<mark>系统每秒 HZ 次</mark>**（默认 1000）的 **<mark>tick 定时时钟中断，定期唤醒 CPU</mark>**，
  防止 CPU 在 IDLE 时进入较深的睡眠状态；
2. **<mark><code>idle=poll</code></mark>**（等价于 `cpupower idle-set -D 1`）：
  使 CPU **<mark>仅工作在 c0 （最高性能）模式</mark>**。
  本质上就是让 cpu 在 idle 时去执行一个空转函数，避免进入 c1/c2 等性能较低的工作模式；
3. **<mark><code>iommu=pt</code></mark>** (pass through)：这个是网络虚拟化 SR-IOV 用的，跟我们这次的 case 没有直接关系，
  但既然要改启动项，就一起加上了，否则下次要用 SR-IOV 的功能还得再改一次启动项。

    [更多信息](https://enterprise-support.nvidia.com/s/article/understanding-the-iommu-linux-grub-file-configuration)：
    When in pass-through mode, the adapter does not need to use DMA translation to the memory, and this improves the performance.

然后使配置生效：

```shell
$ grub2-mkconfig -o /boot/grub2/grub.cfg
$ reboot now
```

验证配置是否生效，几种方式：

1. **<mark>查看系统启动参数</mark>**：

    ```shell
    $ cat /proc/cmdline # 生效前
    BOOT_IMAGE=/vmlinuz-5.10.56-xxx ... biosdevname=0 clocksource=tsc tsc=reliable

    $ cat /proc/cmdline # 生效后
    BOOT_IMAGE=/vmlinuz-5.10.56-xxx ... biosdevname=0 clocksource=tsc tsc=reliable AMD_pstat=disable idle=poll nohz=off iommu=pt
    ```

2. 看监控，频率固定在 3.7GHz 了；而且即使负载比原来更高，也不会有 time_squeeze 了，

    <p align="center"><img src="/assets/img/linux-net-stack/cpuhz-vs-time_squeeze.png" width="85%" height="85%"></p>
    <p align="center">Fig. Changing CPU mode from to performance</p>

3. `cpupower` 查看 CPU 运行模式

    调整前：

    ```shell
    $ cpupower monitor
                  | Mperf              || Idle_Stats
     PKG|CORE| CPU| C0   | Cx   | Freq  || POLL | C1   | C2
       0|   0|   0| 19.87| 80.13|  2394||  0.00| 80.29|  0.00
       0|   0|  64|  6.32| 93.68|  2393||  0.00| 93.77|  0.00
       0|   1|   1| 27.10| 72.90|  2394||  0.00| 73.10|  0.00
       0|   1|  65| 34.41| 65.59|  2394||  0.00| 65.58|  0.00
       0|   2|   2| 33.27| 66.73|  2394||  0.01| 66.71|  0.00
       0|   2|  66| 36.77| 63.23|  2394||  0.00| 63.00|  0.00
    ...
    ```

    调整后：

    ```shell
    $ cpupower monitor
                  | Mperf
     PKG|CORE| CPU| C0   | Cx   | Freq
       0|   0|   0| 99.57|  0.43|  3701
       0|   0|  64| 99.57|  0.43|  3701
       0|   1|   1| 99.58|  0.42|  3701
       0|   1|  65| 99.57|  0.43|  3701
       0|   2|   2| 99.58|  0.42|  3701
       0|   2|  66| 99.57|  0.43|  3701
       0|   3|   3| 99.58|  0.42|  3701
       0|   3|  67| 99.58|  0.42|  3701
    ...
    ```

最后，更常见的启动项配置可能是这样：**<mark><code>AMD_idle.max_cstate=1 processor.max_cstate=1 ...</code></mark>**，
这两个参数表示：

* 限制 CPU 的 cstate 模式最大深度睡眠是 c1，c1 状态可以快速切换到 c0 状态，带来的业务延迟相对较小；
* 效果是在节能（省电）和高性能之间折中。

# 参考资料

1. [Linux 中断（IRQ/softirq）基础：原理及内核实现]({% link _posts/2022-07-02-linux-irq-softirq-zh.md %})
1. [Linux 网络栈接收数据（RX）：原理及内核实现]({% link _posts/2022-07-02-linux-net-stack-implementation-rx-zh.md %})
1. [Monitoring Linux Network Stack]({% link _posts/2022-07-02-monitoring-network-stack.md %})
