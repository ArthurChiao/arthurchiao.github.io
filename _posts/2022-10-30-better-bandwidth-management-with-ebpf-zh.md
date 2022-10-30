---
layout    : post
title     : "[译] Cilium：基于 BPF+EDT+FQ+BBR 更好地带宽网络管理（KubeCon+CloudNativeCon, 2022）"
date      : 2022-10-30
lastupdate: 2022-10-30
categories: bpf tc cilium bbr
---

### 译者序

本文翻译自 KubeCon+CloudNativeCon Europe 2022 的一篇分享：
[Better Bandwidth Management with eBPF](https://kccnceu2022.sched.com/event/ytsQ/better-bandwidth-management-with-ebpf-daniel-borkmann-christopher-m-luciano-isovalent)。

作者 Daniel Borkmann, Christopher, Nikolay 都来自 Isovalent（Cilium 母公司）。
翻译时补充了一些背景知识、代码片段和链接，以方便理解。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 1 问题描述

## 1.1 容器部署密度与（CPU、内存）资源管理

下面两张图来自 Sysdig 2022 的一份调研报告，

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/container-usage-trends.png" width="90%" height="90%"></p>
<p align="center">Source: Sysdig 2022 Cloud Native Security and Usage Report</p>

1. 左图是容器的**<mark>部署密度分布</mark>**，比如 33% 的 k8s 用户中，每个 node 上平均会部署 16~25 个 Pod；
2. 右图是**<mark>每台宿主机上的容器中位数</mark>**，可以看到过去几年明显在不断增长。

这两个图说明：容器的部署密度越来越高。这导致的 CPU、内存等**<mark>资源竞争将更加激烈</mark>**，
如何管理资源的分配或配额就越来越重要。具体到 CPU 和 memory 这两种资源，
K8s 提供了 **<mark>resource requests/limits</mark>** 机制，用户或管理员可以指定一个
Pod **<mark>需要用到的资源量（requests）</mark>**和**<mark>最大能用的资源量（limits）</mark>**，

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: frontend
spec:
  containers:
  - name: app
    image: nginx-slim:0.8
    resources:
      requests:         # 容器需要的资源量，kubelet 会将 pod 调度到剩余资源大于这些声明的 node 上去 
        memory: "64Mi"
        cpu: "250m"
      limits:           # 容器能使用的硬性上限（hard limit），超过这个阈值容器就会被 OOM kill
        memory: "128Mi"
        cpu: "500m"
```

* `kube-scheduler` 会将 pod 调度到能满足 `resource.requests` 声明的资源需求的 node 上；
* 如果 pod 运行之后使用的内存超过了 memory limits，就会被操作系统以 OOM （Out Of Memory）为由干掉。

这种针对 CPU 和 memory 的资源管理机制还是不错的，
那么，**<mark>网络方面有没有类似的机制呢</mark>**？

## 1.2 网络资源管理：带宽控制模型

先回顾下基础的网络知识。
下图是往返时延（Round-Trip）与 TCP 拥塞控制效果之间的关系，

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/tcp-cc-states.png" width="60%" height="60%"></p>

结合
[<mark>流量控制（TC）五十年：从基于缓冲队列（Queue）到基于时间戳（EDT）的演进（Google, 2018）</mark>]({% link _posts/2022-10-07-traffic-control-from-queue-to-edt-zh.md %})，
这里只做几点说明：

1. TCP 的发送模型是**<mark>尽可能快</mark>**（As Fast As Possible, AFAP）
2. 网络流量主要是靠**<mark>网络设备上的出向队列</mark>**（device output queue）做**<mark>整形</mark>**（shaping）
3. **<mark>队列长度</mark>**（queue length）和**<mark>接收窗口</mark>**（receive window）决定了传输中的数据速率（in-flight rate）
4. “多快”（how fast）取决于**<mark>队列的 drain rate</mark>**

现在回到我们刚才提出的问题（k8s 网络资源管理），
在 K8s 中，有什么机制能限制 pod 的网络资源（带宽）使用量吗？

## 1.3 K8s 中的 pod 带宽管理

### 1.3.1 Bandwidth meta plugin

K8s 自带了一个限速（bandwidth enforcement）机制，但到目前为止还是 experimental 状态；
实现上是通过第三方的 bandwidth meta plugin，它会解析特定的 pod annotation，

* **<mark><code>kubernetes.io/ingress-bandwidth=XX</code></mark>**
* **<mark><code>kubernetes.io/egress-bandwidth=XX</code></mark>**

然后转化成对 pod 的具体限速规则，如下图所示，

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/k8s-bw-plugin.png" width="100%" height="100%"></p>
<p align="center">Fig. Bandwidth meta plugin 解析 pod annotation，并通过 TC TBF 实现限速</p>

bandwidth meta plugin 是一个 CNI plugin，底层利用 Linux TC 子系统中的 TBF，
所以最后转化成的是 **<mark>TC 限速规则，加在容器的 veth pair 上（宿主机端）</mark>**。

这种方式确实能实现 pod 的限速功能，但也存在很严重的问题，我们来分别看一下出向和入向的工作机制。

> 在进入下文之前，有两点重要说明：
> 
> 1. 限速只能在出向（egress）做。为什么？可参考 [<mark>《Linux 高级路由与流量控制手册（2012）》第九章：用 tc qdisc 管理 Linux 网络带宽</mark>]({% link _posts/2020-10-08-lartc-qdisc-zh.md %})；
> 2. veth pair 宿主机端的流量方向与 pod 的流量方向完全相反，也就是
> **<mark>pod 的 ingress 对应宿主机端 veth 的 egress</mark>**，反之亦然。
>
> 译注。

### 1.3.2 入向（ingress）限速存在的问题

**<mark>对于 pod ingress 限速，需要在宿主机端 veth 的 egress 路径上设置规则</mark>**。
例如，对于入向 `kubernetes.io/ingress-bandwidth="50M"` 的声明，会落到 veth 上的 TBF qdisc 上：

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/bw-plugin-ingress-1.png" width="80%" height="80%"></p>

TBF（Token Bucket Filter）是个令牌桶，所有连接/流量都要经过**<mark>单个队列</mark>**排队处理，如下图所示：

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/bw-plugin-ingress-2.png" width="80%" height="80%"></p>

在设计上存在的问题：

1. TBF qdisc **<mark>所有 CPU 共享一个锁</mark>**（著名的 qdisc root lock），因此存在锁竞争；流量越大锁开销越大；
2. **<mark>veth pair 是单队列</mark>**（single queue）虚拟网络设备，因此物理网卡的
  多队列（multi queue，不同 CPU 处理不同 queue，并发）优势到了这里就没用了，
  大家还是要走到同一个队列才能进到 pod；
3. 在入向排队是不合适的（no-go），会占用大量系统资源和缓冲区开销（bufferbloat）。

### 1.3.3 出向（egress）限速存在的问题

出向工作原理：

* Pod egress 对应 veth 主机端的 ingress，**<mark>ingress 是不能做整形的，因此加了一个 ifb 设备</mark>**；
* 所有从 veth 出来的流量会被重定向到 ifb 设备，通过 ifb TBF qdisc 设置容器限速。

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/bw-plugin-egress-1.png" width="80%" height="80%"></p>

存在的问题：

1. **<mark>原来只需要在物理网卡排队</mark>**（一般都会设置一个默认 qdisc，例如
  `pfifo_fast/fq_codel/noqueue`），现在又多了一层 ifb 设备排队，缓冲区膨胀（bufferbloat）；
2. 与 ingress 一样，存在 **<mark>root qdisc lock 竞争</mark>**，所有 CPU 共享；
3. **<mark>干扰 TCP Small Queues (TSQ) 正常工作</mark>**；TSQ 作用是**<mark>减少 bufferbloat</mark>**，
  工作机制是觉察到发出去的包还没有被有效处理之后就减少发包；ifb 使得包都缓存在 qdisc 中，
  使 TSQ 误以为这些包都已经发出去了，实际上还在主机内。
4. **<mark>延迟显著增加</mark>**：每个 pod 原来只需要 2 个网络设备，现在需要 3 个，增加了大量 queueing 逻辑。

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/lots-of-queues.png" width="50%" height="50%"></p>

### 1.3.4 Bandwidth meta plugin 问题总结

总结起来：

1. 扩展性差，性能无法随 CPU 线性扩展（root qdisc lock 被所有 CPU 共享导致）；
2. 导致额外延迟；
3. 占用额外资源，缓冲区膨胀。

因此**<mark>不适用于生产环境</mark>**；

# 2 解决思路

> 这一节是介绍 Google 的基础性工作，作者引用了 
> [Evolving from AFAP: Teaching NICs about time (Netdev, 2018)](https://www.youtube.com/watch?v=MAni0_lN7zE)
> 中的一些内容；之前我们已翻译，见
> [<mark>流量控制（TC）五十年：从基于缓冲队列（Queue）到基于时间戳（EDT）的演进（Google, 2018）</mark>]({% link _posts/2022-10-07-traffic-control-from-queue-to-edt-zh.md %})，
> 因此一些内容不再赘述，只列一下要点。
>
> 译注。

## 2.1 回归源头：TCP “尽可能快”发送模型存在的缺陷

<p align="center"><img src="/assets/img/traffic-control-from-queue-to-edt/queue-bottleneck.png" width="90%" height="90%"></p>
<p align="center">Fig. 根据排队论，实际带宽接近瓶颈带宽时，延迟将急剧上升</p>

## 2.2 思路转变：不再基于排队（queue），而是基于时间戳（EDT）

两点核心转变：

1. 每个包（skb）打上一个**<mark>最早离开时间</mark>**（Earliest Departure Time, EDT），也就是最早可以发送的时间戳；
2. 用**<mark>时间轮调度器</mark>**（timing-wheel scheduler）替换原来的**<mark>出向缓冲队列</mark>**（qdisc queue）

<p align="center"><img src="/assets/img/traffic-control-from-queue-to-edt/token-bucket-vs-edt.png" width="100%" height="100%"></p>
<p align="center">Fig. 传统基于 queue 的流量整形器 vs. 新的基于 EDT 的流量整形器</p>

## 2.3 3 EDT/timing-wheel 应用到 K8s

有了这些技术基础，我们接下来看如何应用到 K8s。

# 3 Cilium 原生 pod 限速方案

## 3.1 整体设计：基于 BPF+EDT 实现容器限速

Cilium 的 bandwidth manager，

* 基于 eBPF+EDT，实现了**<mark>无锁</mark>** 的 pod 限速功能；
* **<mark>在物理网卡（或 bond 设备）而不是 veth 上限速</mark>**，避免了 bufferbloat，也不会扰乱 TCP TSQ 功能。
* **<mark>不需要进入协议栈</mark>**，Cilium 的 BPF host routing 功能，使得 FIB
  lookup 等过程**<mark>完全在 TC eBPF 层完成</mark>**，并且能**<mark>直接转发到网络设备</mark>**。
* 在物理网卡（或 bond 设备）上添加 MQ/FQ，实现**<mark>时间轮调度</mark>**。

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/cilium-bw-rate-limit.png" width="80%" height="80%"></p>

## 3.2 工作流程

在之前的分享
[<mark>为 K8s workload 引入的一些 BPF datapath 扩展（LPC, 2021）</mark>]({% link _posts/2021-11-24-bpf-datapath-extensions-for-k8s-zh.md %})
中已经有比较详细的介绍，这里在重新整理一下。

Cilium attach 到宿主机的物理网卡（或 bond 设备），在 BPF 程序中为每个包设置 timestamp，
然后通过 earliest departure time 在 fq 中实现限速，下图：

> 注意：容器限速是在**<mark>物理网卡</mark>**上做的，而不是在每个 pod 的 veth 设备上。这跟之前基于 ifb 的限速方案有很大不同。

<p align="center"><img src="/assets/img/bpf-datapath-ext-for-k8s/pod-egress-rate-limit.png" width="60%" height="60%"></p>
<p align="center">Fig. Cilium 基于 BPF+EDT 的容器限速方案（逻辑架构）</p>

从上到下三个步骤：

1. **<mark>BPF 程序</mark>**：管理（计算和设置） skb 的 departure timestamp；
2. TC **<mark>qdisc (multi-queue) 发包调度</mark>**；
3. **<mark>物理网卡的队列</mark>**。

> 如果宿主机使用了 bond，那么**<mark>根据 bond 实现方式的不同，FQ 的数量会不一样</mark>**，
> 可通过 **<mark><code>tc -s -d qdisc show dev {bond}</code></mark>** 查看实际状态。具体来说，
>
> * Linux bond [默认支持多队列（multi-queue），会默认创建 16 个 queue](https://www.kernel.org/doc/Documentation/networking/bonding.txt)，
>   每个 queue 对应一个 FQ，挂在一个 MQ 下面，也就是上面图中画的；
> * OVS bond 不支持 MQ，因此只有一个 FQ（v2.3 等老版本行为，新版本不清楚）。
>
> bond 设备的 TXQ 数量，可以通过 **<mark><code>ls /sys/class/net/{dev}/queues/</code></mark>** 查看。
> 物理网卡的 TXQ 数量也可以通过以上命令看，但 **<mark><code>ethtool -l {dev}</code></mark>**
> 看到的信息更多，包括了最大支持的数量和实际启用的数量。
>
> 译注。

## 3.3 数据包处理过程

先复习下 Cilium datapath，细节见 2020 年的分享：

<p align="center"><img src="/assets/img/bpf-datapath-ext-for-k8s/datapath-forwarding.png" width="60%" height="60%"></p>

egress 限速工作流程：

<p align="center"><img src="/assets/img/bpf-datapath-ext-for-k8s/datapath-works-today.png" width="80%" height="80%"></p>

1. Pod egress 流量从容器进入宿主机，此时会发生 **<mark>netns 切换</mark>**，但 socket 信息 `skb->sk` 不会丢失；
2. Host veth 上的 BPF 标记（marking）包的 aggregate（queue_mapping），见 [Cilium 代码](https://github.com/cilium/cilium/blob/v1.10/bpf/lib/edt.h)；
3. 物理网卡上的 BPF 程序根据 aggregate 设置的限速参数，设置每个包的时间戳 `skb->tstamp`；
4. FQ+MQ 基本实现了一个 timing-wheel 调度器，根据 `skb->tstamp` 调度发包。

过程中用**<mark>到了 bpf map 存储 aggregate 信息</mark>**。

## 3.4 性能对比：Cilium vs. Bandwidth meta plugin

netperf 压测。

同样限速 100M，延迟下降：

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/edt-vs-tbf-perf-1.png" width="80%" height="80%"></p>

同样限速 100M，TPS：

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/edt-vs-tbf-perf-2.png" width="80%" height="80%"></p>

## 3.4 小结

主机内的问题解决了，那更大范围 —— 即公网带宽 —— 管理呢？

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/internet-traffic.png" width="70%" height="70%"></p>

别着急，**<mark>EDT 还能支持 BBR</mark>**。

# 4 公网传输：Cilium 基于 BBR 的带宽管理

## 4.1 BBR 基础

> 想完整了解 BBR 的设计，可参考
> [<mark>(论文) BBR：基于拥塞（而非丢包）的拥塞控制（ACM, 2017）</mark>]({ % link _posts/2022-01-02-bbr-paper-zh.md % })。
> 译注。

### 4.1.1 设计初衷

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/bbr-1.png" width="95%" height="95%"></p>

### 4.1.2 性能对比：bbr vs. cubic

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/bbr-vs-cubic-table.png" width="80%" height="80%"></p>

**<mark>CUBIC + fq_codel</mark>**：

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/bbr-vs-cubic-perf-1.png" width="80%" height="80%"></p>

**<mark>BBR + FQ (for EDT)</mark>**：

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/bbr-vs-cubic-perf-2.png" width="80%" height="80%"></p>

效果非常明显。

## 4.2 BBR + K8s/Cilium

### 4.2.1 存在的问题：跨 netns 时，`skb->tstamp` 要被重置

BBR 能不能用到 k8s 里面呢？

* BBR + FQ 机制上是能协同工作的；但是，
* 内核在 skb 离开 pod netns 时，将 skb 的时间戳清掉了，导致包进入 host netns 之后没有时间戳，FQ 无法工作

问题如下图所示，

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/pod-bbr-1.png" width="80%" height="80%"></p>

### 4.2.2 为什么会被重置

下面介绍一些背景，为什么这个 ts 会被重置。

> 几种时间规范：https://www.cl.cam.ac.uk/~mgk25/posix-clocks.html

对于包的时间戳 `skb->tstamp`，内核**<mark>根据包的方向（RX/TX）不同而使用的两种时钟源</mark>**：

* **<mark>Ingress 使用 CLOCK_TAI</mark>** (TAI: international atomic time)
* **<mark>Egress 使用 CLOCK_MONOTONIC</mark>**（也是 **<mark>FQ 使用的时钟类型</mark>**）

如果不重置，将包**<mark>从 RX 转发到 TX 会导致包在 FQ 中被丢弃</mark>**，因为
[超过 FQ 的 drop horizon](https://github.com/torvalds/linux/blob/v5.10/net/sched/sch_fq.c#L463)。
FQ `horizon` [默认是 10s](https://github.com/torvalds/linux/blob/v5.10/net/sched/sch_fq.c#L950)。

> `horizon` 是 FQ 的一个配置项，表示一个时间长度，
> 在 [net_sched: sch_fq: add horizon attribute](https://github.com/torvalds/linux/commit/39d010504e6b) 引入，
>
> ```
> QUIC servers would like to use SO_TXTIME, without having CAP_NET_ADMIN,
> to efficiently pace UDP packets.
> 
> As far as sch_fq is concerned, we need to add safety checks, so
> that a buggy application does not fill the qdisc with packets
> having delivery time far in the future.
> 
> This patch adds a configurable horizon (default: 10 seconds),
> and a configurable policy when a packet is beyond the horizon
> at enqueue() time:
> - either drop the packet (default policy)
> - or cap its delivery time to the horizon.
> ```
>
> 简单来说，如果一个**<mark>包的时间戳离现在太远，就直接将这个包
> 丢弃，或者将其改为一个上限值</mark>**（cap），以便节省队列空间；否则，这种
> 包太多的话，队列可能会被塞满，导致时间戳比较近的包都无法正常处理。
> [内核代码](https://github.com/torvalds/linux/blob/v5.10/net/sched/sch_fq.c#L436)如下：
>
> ```c
> static bool fq_packet_beyond_horizon(const struct sk_buff *skb, const struct fq_sched_data *q)
> {
>     return unlikely((s64)skb->tstamp > (s64)(q->ktime_cache + q->horizon));
> }
> ```
>
> 译注。

另外，现在给定一个包，我们**<mark>无法判断它用的是哪种 timestamp</mark>**，因此只能用这种 reset 方式。

### 4.2.3 能将 `skb->tstamp` 统一到同一种时钟吗？

其实最开始，TCP **<mark>EDT 用的也是 CLOCK_TAI 时钟</mark>**。
但有人在[邮件列表](https://lore.kernel.org/netdev/2185d09d-90e1-81ef-7c7f-346eeb951bf4@gmail.com/)
里反馈说，某些特殊的嵌入式设备上重启会导致时钟漂移 50 多年。所以后来
**<mark>EDT 又回到了 monotonic 时钟</mark>**，而我们必须跨 netns 时 reset。

我们做了个原型验证，新加一个 bit `skb->tstamp_base` 来解决这个问题，

* 0 表示使用的 TAI，
* 1 表示使用的 MONO，

然后，

* TX/RX 通过 `skb_set_tstamp_{mono,tai}(skb, ktime)` helper 来获取这个值，
* `fq_enqueue()` 先检查 timestamp 类型，如果不是 MONO，就 reset `skb->tstamp`

此外，

* 转发逻辑中所有 `skb->tstamp = 0` 都可以删掉了
* skb_mstamp_ns union 也可能删掉了
* 在 RX 方向，`net_timestamp_check()` 必须推迟到 tc ingress 之后执行

### 4.2.4 解决

我们和 Facebook 的朋友合作，已经解决了这个问题，在跨 netns 时保留时间戳，
patch 并合并到了 **<mark><code>kernel 5.18+</code></mark>**。
因此 BBR+EDT 可以工作了，

<p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/pod-bbr-2.png" width="80%" height="80%"></p>

## 4.3 Demo（略）

K8s/Cilium backed video streaming service: CUBIC vs. BBR

## 4.4 BBR 使用注意事项

1. 如果同一个环境（例如数据中心）同时启用了 BBR 和 CUBIC，那使用 **<mark>BBR 的机器会强占更多的带宽</mark>**，造成不公平（unfaireness）；

    <p align="center"><img src="/assets/img/better-bw-manage-with-ebpf/bbr-usage-considerations.png" width="95%" height="95%"></p>

2. BBR 会触发**<mark>更高的 TCP 重传速率</mark>**，这源自它**<mark>更加主动或激进的探测机制</mark>**
  （higher TCP retransmission rate due to more aggressive probing）；

**<mark>BBRv2 致力于解决以上问题</mark>**。

# 5 总结及致谢

## 5.1 问题回顾与总结

1. K8s 带宽限速功能可以做地更好；
2. Cilium 的原生带宽限速功能（v1.12 GA）

    1. 基于 BPF+EDT 的高效实现
    2. **<mark>第一个支持 Pod 使用 BBR (及 socket pacing）的 CNI 插件</mark>**
    3. 特别说明：**<mark>要实现这样的架构，只能用 eBPF</mark>**（realizing such architecture only possible with eBPF）

## 5.2 致谢

* Van Jacobson
* Eric Dumazet
* Vytautas Valancius
* Stanislav Fomichev
* Martin Lau
* John Fastabend
* Cilium, eBPF & netdev kernel community

# 6 Cilium 限速方案存在的问题（译注）

Cilium 的限速功能[我们]({% link _posts/2022-09-28-trip-large-scale-cloud-native-networking-and-security-with-cilium-ebpf.md %})
在 v1.10 就在用了，但是使用下来发现两个问题，到目前（2022.11）社区还没有解决，

1. 启用 bandwidth manager 之后，Cilium 会 [hardcode](https://github.com/cilium/cilium/blob/v1.12/pkg/bandwidth/bandwidth.go#L114)
   somaxconn、netdev_max_backlog 等内核参数，覆盖掉用户自己的内核调优；

   例如，如果 node `netdev_max_backlog=8192`，那 Cilium 启动之后，
   就会把它强制覆盖成 1000，导致在大流量场景因为宿主机这个配置太小而出现丢包。

2. **<mark>启用 bandwidth manager 再禁用之后，并不会恢复到原来的 qdisc 配置</mark>**，MQ/FQ 是残留的，导致大流量容器被限流（throttle）。

    例如，如果原来物理网卡使用的默认 `pfifo_fast` qdisc，或者 bond 设备默认使用
    的 `noqueue`，那启用再禁用之后，并不会恢复到原来的 qdisc 配置。残留 FQ 的一
    个副作用就是**<mark>大流量容器的偶发网络延迟</mark>**，因为 FQ 要保证 flow
    级别的公平（而实际上很多场景下并不需要这个公平，总带宽不超就行了）。

    查看曾经启用 bandwidth manager，但现在已经禁用它的 node，可以看到 MQ/FQ 还在，

    ```shell
    $ tc qdisc show dev bond0
    qdisc mq 8042: root
    qdisc fq 0: parent 8042:10 limit 10000p flow_limit 100p buckets 1024 quantum 3028 initial_quantum 15140
    qdisc fq 0: parent 8042:f limit 10000p flow_limit 100p buckets 1024 quantum 3028 initial_quantum 15140
    ...
    qdisc fq 0: parent 8042:b limit 10000p flow_limit 100p buckets 1024 quantum 3028 initial_quantum 15140
    ```

    是否发生过限流可以在 tc qdisc 统计中看到：

    ```shell 
    $ tc -s -d qdisc show dev bond0
    qdisc fq 800b: root refcnt 2 limit 10000p flow_limit 100p buckets 1024 orphan_mask 1023 quantum 3028 initial_quantum 15140 refill_delay 40.0ms
     Sent 1509456302851808 bytes 526229891 pkt (dropped 176, overlimits 0 requeues 0)
     backlog 3028b 2p requeues 0
      15485 flows (15483 inactive, 1 throttled), next packet delay 19092780 ns
      2920858688 gc, 0 highprio, 28601458986 throttled, 6397 ns latency, 176 flows_plimit
      6 too long pkts, 0 alloc errors
    ```

    要恢复原来的配置，目前我们只能手动删掉 MQ/FQ。根据内核代码分析及实际测试，删除 qdisc 的操作是无损的，

    ```shell
    $ tc qdisc del dev bond0 root
    $ tc qdisc show dev bond0
    qdisc noqueue 0: root refcnt 2
    qdisc clsact ffff: parent ffff:fff1
    ```
