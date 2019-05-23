---
layout    : post
title     : "[译] Segment Routing Overview"
date      : 2019-05-23
lastupdate: 2019-05-23
categories: segment-routing
---

### 译者序

Segment Routing (SR) 是近年来网络领域的一项新技术，“segment” 在这里
指代网络隔离技术，例如 MPLS。如果快速回顾网络设计在过去几十年的
发展，我们会发现 SR 也许是正在形成的第三代网络设计思想。

第一代是以**互联网**为代表的**无中心式**设计，所有网络节点通过**分布式路由协议**
同步路由信息，这些路由协议包括 IGP（RIP、OSPF、IS-IS）和 EGP（BGP）。

第二代是近些年以 **SDN** 为代表的**集中式**设计，**全局的控制器**
了解整张网络的拓扑和状态，可以精确控制网络中每个节点的每条转发规则。比较有代表性
的是 Google 基于 OpenFlow 实现 B4 Network [1]。

以上两代网络的设计思想截然相反，因此必然各有优缺点。SR 此时横空出世，某种程度上
可以看作两者的折中（或优点结合）：给定一个源节点和目的节点，**集中式控制器（如果
有）只负责选取若干中间节点，形成一条转发路径**；而这些**中间节点之间**还有很多其
他结点，它们之间如何转发及同步路由，**都交给分布式算法**。这种设计同时兼顾了集中
式控制（若干节点形成的转发路径）和分布式智能（路由同步、链路负载均衡等），网络的
控制粒度从最粗（第一代）到最细（第二代），再到 SR 的粗细适中（第三代）。

SR 实现以上目标的最重要技术之一是**源路由**（source routing），每个包在离开源节
点时就已经确定了（核心）转发路径，并将路径信息编码到了每个包里。

[***Segment Routing***](https://www.amazon.com/Segment-Routing-Part-Clarence-Filsfils-ebook/dp/B01I58LSUO)
一书的作者举了一个形象的例子（这也是他设计 SR 的直接灵感来源）：从 Rome 开始到
Brussel [2]，翻译成中文就是**从上海开车去杭州。你不会规划出整条路径上的每一个转弯**
，那太细了。你真正做的是，选出途经的几个重要地方，例如虹桥-松江-嘉兴-余杭-
西湖，只要确保沿着这几个地方向开，就一定能到达。至于两个地方之间，比如虹桥到松江
，到底是走大路还是小路，要视当时的路况。假如一条路堵了，你可能会当即切换到另一条
路，但松江这个目标不变。（“规划出每一个转弯”的比喻，听上去是在揶揄 OpenFlow SDN
的那帮人。）

和术语 SDN 一样，“SR” 本身只是一个概念，并不是实现。目前 SR 的实现有两种：分别基
于 MPLS 和 IPv6，其中 MPLS SR 与现有的 MPLS 网络兼容，但大大简化了控制平面；而基
于 IPv6 的版本（称为 SRv6）看起来前景更广阔。另外，Linux 4.10 已经初步支持了 SRv6
，但性能还比较差 [3]。

以上可视为对 SR 的入门导读。

本文接下来的内容来自 Cisco ASR 920 路由器文档 [Segment Routing for Cisco ASR 920
Series Aggregation Services
Routers](https://www.cisco.com/c/en/us/td/docs/routers/asr920/configuration/guide/segment-routing/segment-routing-book/overview.pdf)
第一章 **Segment Routing Overview**。翻译仅供学习交流，如有侵权立即删除。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

Segment routing (SR) 是一种基于**源路由**（source routing）的网络设计。

**包从源节点（source node）发出之前转发路径就已经确定了**，转发指令（forwarding
instructions）以 segment list 的形式编码到每个包中。List 中的每一个 segment 在路
由信息库（Router Information Base，RIB）中都有记录。每到达一跳（hop）后，list 最
外层的 segment 用于确定下一跳。Segments 以栈的形式（stacked）存储在包头中。如果
栈顶 segment 指向的是另一个节点，当前节点就通过 ECMP 将包发送到下一跳。如果栈顶
segment 指向的是本节点，就 pop out segment，执行下一个 segment 规定的任务。

SR 基于已有的一些内部网关协议（IGP），例如 IS-IS、OSFP 和 MPLS，来实现高效和灵活
的转发。

## SR 是如何工作的？

在 SR 网络中，路由器有能力选择任意的转发路径，不管是显式（explicit）指定的路
径，还是 IGP 自动计算出的最短路径。

**一个 segment 代表一段子路径（subpath）**，路由器将可以多段子路径结合起来，形成
一条到达目的节点的最终路径。**每个segment 都有一个唯一的标识符（segment identifier
，SID），通过 IGP 的扩展协议在网络中分发**。IGP 扩展协议对 IPv4 和 IPv6 都适用。
和传统的 MPLS 网络不同，SR 网络中的路由器不需要 LDP 和 RSVP-TE 协议 来分配和同步
SID，以及对转发信息进行编程。

每个路由器（节点，node）和每个链路（邻接，adjacency）都有相应的 SID。

**Node segment ID** 是全局唯一的，表示 **IGP 确定的到一个路由器的最短路径**。网
络管理员从保留的一段范围内为每台路由器分配一个 node ID。

**Adjacency segment ID** **只在局部有效**（locally significant），表示**到一个
邻居路由器的具体邻接**，例如一个出向接口（egress interface）。Adjacency segment
ID 是由路由器自动生成的，范围不会和 node SID 重合。

在 MPLS 网络中，一个 SID 会编码成 MPLS label stack 中的一条纪录项（entry）
。SID 指示应该沿着一条特定的路径转发包。SID 分为两类：

* **Prefix SID**：带 IP 地址前缀的 SID，其中的 IP 地址前缀是由 IGP 计算出来的。
  Prefix SID 全局（globally）唯一。**Node SID 是 Prefix SID的一种特殊情况，其
  prefix IP 是 node 自身的 loopbakck 地址**。It is advertised
  as an index into the node specific SR Global Block or SRGB.
* **Adjacency SID**：一个 Adjacency SID 就是**两个路由器之间的一条链路**。Adjacency
  SID 是和它所属的路由器相关的，因此它只是局部唯一的

## SR 举例

图 1 是一张由 5 个路由器组成的 MPLS SR 网络，控制平面基于 IS-IS。Node ID 的范围
是 100-199，Adjacency ID 的范围是 200 及以上。IS-IS 会将 segment ID（这里是 MPLS
label）连同 IP Prefix 可达性信息在网络内做通告。

<p align="center"><img src="/assets/img/segment-routing-overview/1.png" width="50%" height="50%"></p>
<p align="center">图 1 五个路由器组成的一张 MPLS SR 网络</p>

在这个网络中，任何路由器想向路由器 E 发送流量，必须先将 103（路由器 E 的 node
SID）push 到 segment list，以便利用 IS-IS 最短路径转发流量。中间结点的MPLS 标签
交换（label-swapping）过程会保留 103 标签，直到包到达节点 E，如图 2 所示。

<p align="center"><img src="/assets/img/segment-routing-overview/2.png" width="50%" height="50%"></p>
<p align="center">图 2 MPLS 标签交换操作</p>

以上是通过 Node SID 实现的转发路径。**Adjacency segments 的行为与此不同**。例如
，如果一个包到达路由器 D，栈顶 MPLS label 是 203（D 到 E 的 adjacency SID），D
会先 pop label，然后将包转发给 E。

SIDs 可以组合成有序列表（ordered list）来实现**流量工程**（traffic engineering，
TE）。根据需求的不同，一个 segment list 可以包含：

1. 多个 adjacency segments
1. 多个 node segments
1. 多个 adjacency segments 和 node segments 的组合

上面例子还可以用 **node segments 和 adjacency segment 的组合**来实现，如图 3 所示：

1. 首先，路由器 A push label stack（104，203）到每个包
1. 然后，路由器 A 利用到最短路径和 ECMP 特性将包转发到路由器 D
1. 最后在路由器 D 经过一个显式的接口（203）到达目的地 E

整个过程中，路由器 A **无需向网络节点声明任何路径信息**（保存在每个包中）。**网
络的（配置）状态不受这条路径的影响，还是保持原来的配置**。也就是说，在保持网络状
态（配置）不变的情况下，A 设置的新路径生效（enforce）了。（作为对比，如果要在 SDN
中网络添加一条新的转发路径，那必然要对整个链路上的所有节点添加配置。）

<p align="center"><img src="/assets/img/segment-routing-overview/3.png" width="50%" height="50%"></p>
<p align="center">图 3 组合 Node segments 和 Adjacency segment 到达 E 的路径</p>

## SR 的好处

### Ready for SDN

**SR 被认为是 SDN 的首选架构之一**，而且它还是**应用工程化路由**（Application
Engineered Routing，AER）的基础。它在**基于网络的分布式智能**（例如链路和节点自
动保护）和**基于控制器的集中式智能**（例如流量优化）之间取得了很好的平衡。

SR 能够提供严格的网络**性能保证**、网络**资源的高效利用**、基于应用的交易（
application-based transactions）的高**可扩展性**。SR 使得网络**使用最少的状态信
息**（minimal state information）来满足这些需求。

SR 可以很容易地集成到基于控制器的 SDN 架构，下图是一个示例，其中的控制器负责集中
式优化，包括带宽控制。

<p align="center"><img src="/assets/img/segment-routing-overview/4.png" width="50%" height="50%"></p>
<p align="center">图 4 SDN 控制器</p>

在这个方案中，SDN 控制器了解整张网络的拓扑和flow。路由器申请到目的地的一条路
径时，声明它期望的特性，例如延迟、带宽、链路多样性。控制器据此计算出一条最优路径，
返回 segment list（例如一个 MPLS label stack）。然后路由器将这个 segment
list 编码到包头中，而控制器不需要对网络做任何额外的配置（signaling）。

### 网络无需维护任何应用状态

无需向网络添加任何应用状态（application state），segment list 就可以实现完全的网
络虚拟化。状态信息以 segment list 的形式编码在每个包中。因为**网络只需维护
segment 状态**（node/adjacency segment ID，数量非常少而且变更不频繁），因此**可
以支持非常大 —— 而且非常高频—— 的 transaction-based 的应用请求**，而不会给网络造
成任何负担。

### 简化/简单

* 当用于 MPLS 数据平面时，SR 可以通过隧道的方式将 MPLS 服务（VPN、VPLS、VPWS）
  从一个 ingress provider edge（供应商边缘路由器）送到一个 egress provider
  edge，只需要 IGP（IS-IS 或 OSPF），而不需要其他协议
* 不需要额外的协议（例如 LDP 或 RSVP）来分发标签
* 可以复用已有网络基础设施，支持 ECMP（使用 node segment ID）

### 支持快速重路由（FRR）

对任何拓扑都支持快速重路由（Fast ReRoute）。在链路或节点挂掉的情况下，MPLS 依靠
FRR 实现收敛。有了 SR 之后，**收敛时间**可以做到 `50ms` 以下。

### 适用于大规模数据中心

* 用 BGP 分发 node SID，类似于 IGP 分发 node SID
* Any node within the topology allocates the same BGP segment for the same
  switch
* 支持 ECMP 和 FRR（BGP PIC：Prefix Independent Convergence）
* 流量工程的基石之一，SRTE

### 可扩展

* 避免了 LDP database 中的成千上万的标签
* 避免了网络中成千上万的 MPLS TE LSP
* 避免了成千上万的隧道配置

### 双平面网络（Dual-plane networks）

* 支持 Dual-plane（MPLS 和 SRv6？），支持跨 plane 的转发策略（disjointness
  enforcement）
* 任播（anycast）SID 支持宏策略（macro policy），类似于：“从 node A 注入的、到达
  node Z 的 flow 1，必须经过 plane 1 到达”，“从 node A 注入的、目的是 node Z 的
  flow 2，必须经过 plane 2 转发”

### 集中式流量工程

* 控制器和编排平台可以和 SR 流量工程联动，实现集中式优化，例如 WAN 优化
* 网络变动，例如拥塞，可以触发应用重新计算 SR TE tunnel 的 placement 方式
* SR tunnel 可以由编排器通过南向接口（例如 PCE）动态编程
* 敏捷网络编程，不需要对中间结点和尾节点做任何配置，也不需要对每条 flow 做配置（
  signaling）

### Egress Peering 流量工程（EPE）

* SR 支持集中式 EPE
* 控制器指导流量从 ingress provider edge（边界路由器）和内容源（包从边界路由器开
  始转发）**依照指定的路径和接口**到达 egress provider edge
* 用 **BGP “peering” SID** 表达**源路由域内路径**（source-routed inter-domain path）
* 控制器通过 BGP Link Status（BGP-LS） EPE 路由学习 BGP peering SID 和 egress 边界路由器外部的拓扑
* 控制器编程控制 ingress 点的期望路径

### 即插即用（Plug-and-Play）部署

和当前的 MPLS 网络兼容。

## SR 的限制

1. Segment Routing must be globally enabled on the chassis before enabling it on the IGPs, like ISIS or OSPF.
1. Segment routing must be configured on the ISIS instance before configuring a prefix SID value.
1. The prefix SID value must be removed from all the interfaces under the same ISIS instance before disabling segment routing.

## References

1. Jain, Sushant, et al. ["B4: Experience with a globally-deployed software
   defined WAN."](https://dl.acm.org/citation.cfm?id=2486019) ACM SIGCOMM
   Computer Communication Review. Vol. 43. No. 4. ACM, 2013.
2. Clarence Filsfils, Kris Michielsen, Ketan Talaulikar, [Segment Routing Part
   I](https://www.amazon.com/Segment-Routing-Part-Clarence-Filsfils-ebook/dp/B01I58LSUO), 2016
3. Lebrun, David, and Olivier Bonaventure. ["Implementing IPv6 segment routing in
   the linux kernel."](https://inl.info.ucl.ac.be/system/files/paper_10.pdf)
   Proceedings of the Applied Networking Research Workshop.  ACM, 2017.
