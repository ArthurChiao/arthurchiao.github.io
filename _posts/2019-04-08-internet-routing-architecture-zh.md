---
layout: post
title:  "[笔记] Internet Routing Architecture (Cisco Press, 2000)"
date:   2019-04-08
categories: bgp datacenter
---

### 关于本文

本文是我在读 [Internet Routing Architecture, 2nd Edition](https://www.amazon.com/Internet-Routing-Architectures-2nd-Halabi/dp/157870233X) （
Cisco Press, 2000）时的读书笔记。

注意这本书是 2000 年写的，因此有些内容可能已经过时，比如说到“当前大型网络都是使
用 xxx 协议”的时候，说的是距今 20 年前的情况，现在则并不一定。

本书致力于解决实际问题，书中包含大量的架构图、拓扑图和真实场景示例，内容全面而且
易于上手，是不可多得的良心之作。本书目的是使读者成为**将自有网络集成到全球互联网**
（integrating your network into the global Internet）领域的专家。

以下是笔记内容。

----

1. [互联网的演进](#chap_1)
    * 1.1 [互联网：起源及近史](#chap_1.1)
    * 1.1 [Network Access Points](#chap_1.2)
    * 1.1 [Routing Arbiter Project](#chap_1.3)
    * 1.1 [The Very High-Speed Backbone Network Service](#chap_1.4)
    * 1.1 [Transitioning the Regional Networks from the NSFNET](#chap_1.5)
    * 1.1 [NSF Solicits NIS Managers](#chap_1.6)
    * 1.1 [Other Internet Registries](#chap_1.7)
    * 1.1 [Internet Routing Registries](#chap_1.8)
    * 1.1 [The Once and Future Internet](#chap_1.9)
2. [ISP Services and Characteristics](#chap_2)
    * 2.1 ISP Services
    * 2.2 ISP Service Pricing, Service-Level Agreements, and Technical Characteristics
3. [IP 寻址和分配](#chap_3)
    * 3.1 History of Internet Addressing
    * 3.2 IP Address Space Depletion
    * 3.3 Looking Ahead
    * 3.4 Frequently Asked Questions
    * 3.5 References
4. [域间路由基础](#chap_4)
    * 4.1 Overview of Routers and Routing
    * 4.2 Routing Protocol Concepts
    * 4.3 Segregating the World into Autonomous Systems
5. [边界网关协议第 4 版（BGP-4）](#chap_5)
    * 5.1 How BGP Works
    * 5.2 BGP Capabilities Negotiation
    * 5.3 Multiprotocol Extensions for BGP
    * 5.4 TCP MD5 Signature Option
6. [Tuning BGP Capabilities](#chap_6)
    * 6.1 Building Peer Sessions
    * 6.2 Sources of Routing Updates
    * 6.3 Overlapping Protocols: Backdoors
    * 6.4 The Routing Process Simplified
    * 6.5 Controlling BGP Routes
    * 6.6 Route Filtering and Attribute Manipulation
    * 6.7 BGP-4 Aggregation
7. [冗余、对称和负载均衡](#chap_7)
    * 7.1 Redundancy
    * 7.2 Symmetry
    * 7.3 Load Balancing
    * 7.4 Specific Scenarios: Designing Redundancy, Symmetry, and Load Balancing
8. [AS 内部路由控制](#chap_8)
    * 8.1 [Interaction of Non-BGP Routers with BGP Routers](#chap_8.1)
    * 8.2 BGP Policies Conflicting with Internal Defaults
    * 8.3 Policy Routing
9. [大型 AS 控制管理](#chap_9)
    * 9.1 Route Reflectors
    * 9.2 Confederations
    * 9.3 Controlling IGP Expansion
10. [设计稳定的因特网](#chap_10)
    * 10.1 因特网路由的不稳定性
    * 10.2 BGP Stability Features


## Introduction

互联网（Internet）起源于 1960s 的一个学术实验。

一些人惊讶于网络会发生故障，另外一些人则惊讶于网络竟然会运行良好。

### 目标

本书致力于使读者成为**将自有网络集成到全球互联网**（integrating your network
into the global Internet）领域的专家。

本书致力于解决实际问题，书中包含大量的架构图、拓扑图和真实场景示例，内容全面而且
易于上手（comprehensive and accessible）。

### 目标读者

需要将自有网络接入互联网的公司的网络管理员、集成者、架构师。

不需要太多 TCP/IP 基础。

# I: The Contemporary Internet

路由问题和方案（routing problems and solutions）的复杂性和当代互联网的增长与演进密切相关。

因此，在深入到路由协议细节之前，先了解互联网的发展历史，会对理解问题非常有帮助。

<a name="chap_1"></a>

## 1 互联网的演进

介绍互联网的发展历史，主要组件，理解互联网现在面临的挑战，以及如何构建可扩展互连
网络（internetworks）。

<a name="chap_1.1"></a>

### 1.1 互联网：起源及近史

#### ARPANET

1969 年 12 月，四个节点、通过 56kbps 电路连接的试验网络 ARPANET。

这项技术大获成功，随后几千个大型和政府机构将他们的私有网络连接到了 ARPANET。

<p align="center"><img src="/assets/img/internet-routing-arch/1-1.PNG" width="60%" height="60%"></p>
<p align="center">图 1-1 ARPANET Architecture, December 1969 </p>

<p align="center"><img src="/assets/img/internet-routing-arch/1-2.PNG" width="60%" height="60%"></p>
<p align="center">图 1-2 ARPANET Architecture, July 1976 </p>

这就是互联网（Internet）的前身。

Internet 被禁止用于商业目的，不过大量的接入还是导致了扩展性和链路拥塞问题，因此
NSF开始研究 NSFNET。

#### NSFNET

NSFNET 是为了解决 ARPANET 的拥塞问题。设计：

1. 多个区域网络（regional networks）和对等网络（peer networks），
1. 骨干网（backbone）：NSFNET 的核心
1. regional networks 和 peer networks 都接入骨干网
1. 带宽升级到 T1（1.544 Mbps，1988），后来又到 T3（45 Mbps，1991）

<p align="center"><img src="/assets/img/internet-routing-arch/1-3.PNG" width="60%" height="60%"></p>
<p align="center">图 1-3 The NSFNET-Based Internet Environment </p>

1990 年左右，NSFNET 仍然是用于科研和学术目的。之后，开始出现 ISP 产业。

1990 年之后，这张网络开始连接到欧洲和亚洲。

1995 年，这张网络完成了自己的历史使命。

#### The Internet Today

今天的互联网是从一个核心网络（core network，也就是 NSFNET）转变成了一个由商业提
供商运营的分布式网络，这些供应商网络通过主要的网络交换节点或直连而连接到一起。

<p align="center"><img src="/assets/img/internet-routing-arch/1-4.PNG" width="60%" height="60%"></p>
<p align="center">图 1-4 The General Structure of Today's Internet</p>

ISP 在多个 region 都提供连接接入点，称为 POP（Points of Presence）。

<a name="chap_2"></a>

## 2 ISP 服务和特点

<a name="chap_2.1"></a>

### 2.1 ISP Services

For more details about switches, VLANs, and broadcast
domains, read Interconnections: Bridges, Routers, Switches, and Internetworking
Protocols,
Second Edition (Addison-Wesley, 1999) by Radia Perlman, or Cisco LAN Switching
(Cisco
Press, 1999) by Kennedy Clark and Kevin Hamilton.

<a name="chap_3"></a>

## 3 IP 寻址和分配

### 3.1 History of Internet Addressing

### 3.2 IP Address Space Depletion

CIDR: Classless Inter-domain Routing

路由条目越多，所需的处理能力和内存空间就更多。

路由表规模在 1991~1995 年每 10 个月就翻一番：

<p align="center"><img src="/assets/img/internet-routing-arch/3-9.PNG" width="60%" height="60%"></p>
<p align="center">图 3-9 The Growth of Internet Routing Tables </p>

CIDR 相比于之前的有类别 IP 地址（classful IP addresses），是革命性的一步。通过
prefix 做路由聚合，大大减小路由表的规模。

<p align="center"><img src="/assets/img/internet-routing-arch/3-11.PNG" width="60%" height="60%"></p>
<p align="center">图 3-11 Classful Addressing Versus CIDR-Based Addressing</p>

路由安装最长前缀匹配算法（LPM）选择路由。

<p align="center"><img src="/assets/img/internet-routing-arch/3-12.PNG" width="60%" height="60%"></p>
<p align="center">图 3-12 Longest Match</p>

如图 3-12，如果因为一些原因 path 1 路由失效率，那会用到下一个最长匹配，在图中就
是 path 2。

#### 将自己聚合的路由指向黑洞

每个路由器会对外通告自己聚合的路由，表面自己到这些路由是可达的。

但是，为了避免出现路由环路，每个路由器在自己内部，要将自己聚合的路由指向黑洞，即
，丢弃所有到这条路由的包。来看个具体的例子。

<p align="center"><img src="/assets/img/internet-routing-arch/3-13.PNG" width="60%" height="60%"></p>
<p align="center">图 3-13 Following Less-Specific Routes of a Network's Own Aggregate Causes Loops</p>

ISP1 的配置：

1. 默认路由指向 ISP2
1. ISP1 到 Foonet 网络 198.32.1.0/24 可达
1. ISP1 经过路由聚合，对外通告自己到 198.32.0.0/13 可达

则，当 ISP1 和 Foonet 的网络发生故障之后，目的是 198.32.1.1 的流量从 ISP2 到达
ISP1 时，会匹配到默认路由，流量会绕回 ISP2，形成环路。

解决办法是：在 ISP1 的路由表内添加一条到 198.32.0.0/13 的 null 路由，将所有流量
丢弃。这样网络正常时，流量会匹配 198.32.1.0/24 这条路由；网络异常导致这条路由失
效后，流量匹配到 198.32.0.0/13，丢弃所有流量。

# II: Routing Protocol Basics

本书主要介绍外部网关协议（exterior gateway protocols），即不同自治系统（AS）之间
的路由。但先了解一下内部网关协议（internal gateway protocols）会非常有帮助。

<a name="chap_4"></a>

## 4 域间路由基础

互联网是由自治系统（AS）组成的，这些 AS 由不同组织管理，拥有不同的路由策略。

<a name="chap_4.1"></a>

### 4.1 路由器和路由（Routers and Routing）

内部网关协议（IGP）是为**企业网**（enterprise）设计的，**不适用于大型网络**，
例如上千个节点的、有上万条路由的网络。因此引入了外部网关协议（EGP），例如**边界
网关协议**（BGP）。

本章介绍 IGP 基础。

### 4.2 路由协议

大部分路由协议都可以归为两类分布式路由算法：

1. 链路-状态（link-state）
1. 距离矢量（distance vector）

#### 距离矢量算法

为每条路由维护一个**距离矢量**（vector of distances），其中“距离”用跳数（hops）或类
似指标衡量。

每个节点独立计算最短路径，因此是分布式算法。

每个节点向邻居通告自己已知的最短路径，邻居根据收到的消息判断是否有更短路径，如果
有就更新自己的路由信息，然后再次对外通告最短路径。如此反复，直到整个网络收敛到一
致状态。

**早期 IGP 代表**：RIP（Routing Information Protocol）

早期 IGP 缺点：

1. 早期协议（RIP-1）只计算跳数（相当于每跳权重一样），没有优先级和权重，而跳数最
   少的路径不一定最优
1. 早期协议（RIP-1）**规定了最大跳数**（一般是 15），**因此限制了网络的规模**（
   但解决了 count to infinity 问题）
1. 早期协议（RIP-1）靠**定时器触发路由通告**（没有事件触发机制），因此路由发生变
   动时，**收敛比较慢**
1. 第一代协议不支持 CIDR

新 IGP 解决了以上问题，协议代表：

1. RIP-2
1. EIGRP

**距离矢量协议的优点**：

1. 简单
1. 成熟

BGP 也是距离矢量协议，但它是通过引入路径矢量（path vector）解决 count to
infinity 问题。path vector 包含了路径上的 ASN，相同 ASN 的路径只会接受一条，因此
消除了路由环路。BGP 还支持基于域的策略（domain-based policies）。后面会详细介绍
BGP。

#### 链路状态算法

* 距离矢量算法：交换路由表信息
* 链路状态算法：交换邻居的链路状态信息，比距离矢量算法复杂

分布式数据库（replicated distributed database），存储链路状态（link state）。

代表：

1. OSFP
1. IS-IS

**路由可扩展性和收敛速度都有改善，可以支持更大的网络，但仍然只适用于域内路由**（
interior routing）。

大部分大型服务供应商在域内（intra-domain）都使用 link-state 协议，主要是看中它的
**快速收敛**特性。

### 4.3 将互联网分割为自治系统（AS）

**外部路由协议（Exterior routing protocol）的提出是为了解决两个问题**：

1. **控制路由表的膨胀**
1. 提供结构化的互联网视图

将路由域划分为独立的管理单元，称为自治系统（autonomous systems，AS）。
每个 AS 有自己**独立的路由策略**和 **IGP**。

当前域间路由的事实标准：BGP-4。

> intra-domain 和 inter-domain routing 的主要区别
>
> intra-domain 主要解决技术需求，而 inter-domain 主要反映网络和公司的政治与商
> 业关系。

##### Autonomous Systems

一个 AS 是拥有如下特点的一组路由器：

1. 共享相同的路由策略
1. 被作为一个整体进行管理
1. 通常路由器之间运行同一种 IGP 协议

每个 AS 有一个编号，称为 ASN。AS 之间通过 BGP 交换路由。

<p align="center"><img src="/assets/img/internet-routing-arch/4-2.PNG" width="60%" height="60%"></p>
<p align="center">图 4-2 AS 之间的路由交换</p>

#### 三种 AS 类型

1. stub AS：末梢 AS，只有一条默认出口，因此不需要同步路由信息
1. non-transient AS：只通告自己的路由，不传播学习到的路由
1. transit AS：既通告自己的路由，又传播学习到的路由

<p align="center"><img src="/assets/img/internet-routing-arch/4-3.PNG" width="60%" height="60%"></p>
<p align="center">图 4-3 Single-Homed (Stub) AS</p>

<p align="center"><img src="/assets/img/internet-routing-arch/4-5.PNG" width="60%" height="60%"></p>
<p align="center">图 4-5 Multihomed Nontransit AS Example</p>

<p align="center"><img src="/assets/img/internet-routing-arch/4-6.PNG" width="60%" height="60%"></p>
<p align="center">图 4-6 Multihomed Transit AS Using BGP Internally and Externally</p>

### 4.5 Frequently Asked Questions

#### Domain 和 AS 有什么区别？

两者都是指满足某些条件的一组路由器。

Domain 一般指**运行相同路由协议**的一组路由器，例如一个 RIP domain 或一个 OSFP
domain。

AS 是**管理概念**，**作为整体统一管理的、有相同路由策略**的一组路由器是一个 AS。
一个 AS 可能包含一个或多个 domain。

#### BGP 是用于 AS 之间的。那用于 AS 内的 BGP 又是什么？

AS 内的 BGP 是 iBGP。

如果 AS 是 transit AS，那 iBGP 可以保护这个 AS 内的 nontransit routers，不会被大
量的 AS 外路由撑爆路由表。另外，即使不是 transit AS，iBGP 也可以提供更强的控制能
力，例如本书后面会看到的选择 exit and entrance points。

<a name="chap_5"></a>

## 5 边界网关协议第 4 版（BGP-4）

BGP-4 1993 年开始部署，是第一个支持路由聚合的 BGP 版本。

### 5.1 BGP 工作原理

BGP 是一种**路径矢量协议（path vector protocol）**。

***Path vector*** 是一条路由（network prefix）经过的所有 AS 组成的路径。目的是防
止出现**路由环路**。

* BGP 使用 TCP 协议，运行在 179 端口。
* peer 之间建立连接之后交换全部路由，之后只交换更新的路由（增量更新）
* 交换路由是 UPDATE 消息
* 维护**路由表**的**版本号**，每次路由表有更新，版本号都会递增
* 通告 UPDATE 消息通告和撤回路由

#### BGP 消息头格式

<p align="center"><img src="/assets/img/internet-routing-arch/5-6.PNG" width="60%" height="60%"></p>
<p align="center">图 5-6 BGP Message Header Format</p>

字段：

1. Marker：16 字节，用于 BGP 消息认证及检测 peer 是否同步
1. Length: 2 字节，BGP 消息总长度，包括 header。总长度在 19~4096 字节之间。
1. Type: 2 字节，四种类型：
    * `OPEN`
    * `UPDATE`
    * `NOTIFICATION`
    * `KEEPALIVE`

### 5.2 BGP 功能协商

检测到错误时会发送 NOTIFICATION 消息，然后关闭 peer 连接。

#### UPDATE Message and Routing Information

UPDATE 消息:

* Network Layer Reachability Information (NLRI)
* Path Attributes
* Unfeasible Routes

<p align="center"><img src="/assets/img/internet-routing-arch/5-10.PNG" width="60%" height="60%"></p>
<p align="center">图 5-10 BGP UPDATE Message</p>

<p align="center"><img src="/assets/img/internet-routing-arch/5-11.PNG" width="60%" height="60%"></p>
<p align="center">图 5-11 BGP Routing Update Example</p>

### 5.3 Multiprotocol Extensions for BGP

对 BGP-4 的兼容性扩展，支持除了 IPv4 之外的其他协议（所以叫多协议），例如 IPv6
等等。

### 5.4 TCP MD5 Signature Option


### 5.5 Looking Ahead


### 5.6 Frequently Asked Questions

#### BGP 是否像 RIP 一样定期发布路由更新消息？

不是。只有路由有变动时，才会通告，而且只通告变动的路由。

#### ASN 在 BGP 消息中的什么地方？

UPDATE 消息的 AS_PATH 属性中。

# III: Effective Internet Routing Designs

接下来用前面学到的知识解决实际问题。

<a name="chap_6"></a>

## 6 BGP Capabilities 调优

从本章开始，内容从理论转向 BGP 实现。

### 6.1 Building Peer Sessions

虽然 BGP 大部分情况都是用于 AS 之间，但是，它也可以用在 AS 内部，为 AS 内部的路
由器提供外部路由可达信息（external destination reachability information）。

AS 内部的 BGP 称为 iBGP；AS 之间的 BGP 称为 eBGP。

<p align="center"><img src="/assets/img/internet-routing-arch/6-1.PNG" width="60%" height="60%"></p>
<p align="center">图 6-1 iBGP 和 eBGP</p>

邻居之间建立连接，然后通过 OPEN 消息进行协商，在这个过程中，peer routers 之间会
比较 ASN 来判断他们是否属于同一个 AS。

iBGP 和 eBGP 的区别：

1. 对收到的 UPDATE 消息的处理不同
1. 消息携带的属性不同

#### 物理和逻辑连接

eBGP 要求邻居之间必须是物理直连的，但是有些情况下两个 AS 之间的 BGP peer 无法满
足直连的要求，例如经过了一些非 GBP 路由器。这种情况下，需要对 BGP 做特殊配置。

<p align="center"><img src="/assets/img/internet-routing-arch/6-2.PNG" width="60%" height="60%"></p>
<p align="center">图 6-2 External BGP Multihop Environment</p>

iBGP 对于 peer 之间是否直连没有要求，只要 peer 之间 IP 通即可。

#### Synchronization Within an AS

BGP 的默认行为是，只有 iBGP 收敛之后，才将 AS 内部的路由通告给其他 AS。

否则，会出现问题。来看个例子。

<p align="center"><img src="/assets/img/internet-routing-arch/6-4.PNG" width="60%" height="60%"></p>
<p align="center">图 6-4 BGP Route Synchronization</p>

ISP3 里面只有 RTA 和 RTC 运行 BGP 协议。当 ISP1 将 192.213.1.0/24 通告给 ISP3 的
RTA 之后，RTA 进一步将消息通告给 RTC。RTC 再通告给 ISP2。当 ISP2 向这个路由发送
流量时，RTC 会将流量转发给 RTB，而 RTB 没有这个路由信息，会将流量丢弃。

因此，BGP 规定，从 iBGP 邻居学习到的路由不应该通告给其他 AS，除非这条路由通告IGP
也能访问到（The BGP rule states that a BGP router should not advertise to
external neighbors destinations learned from IBGP neighbors unless those
destinations are also known via an IGP.）。这就是所谓的同步。如果 IGP 可达，那说
明这条路由在 AS 内部是可达的。

**将 BGP 路由注入 IGP 路由是有代价的。**

首先，这会**给 IGP 节点带来额外的计算开销**。前面已经提到，IGP 并不是为处理大规
模路由设计的（IGPs are not designed to handle that many routes）。

其次，**没有必要将所有外部路由都同步到所有内部节点**。更简单的方式通常是，AS 内
分成non-BGP 路由器和 BGP 路由器，non-BGP 路由器的默认路由指向 BGP 路由器。这样可
能会导致路径并不是最优的，但是跟在 AS 内维护上千条外部路由相比，代价要小的多。

除了 BGP+IGP 方式之外，解决这个问题的另一个办法是，AS 内部的非边界路由器之间做
iBGP full-mesh，这样路由可以**通过 iBGP 保证同步**。向 IGP 内部插入成千上万条路
由太恐怖了。

因此，一些 BGP 的实现里允许关掉同步，例如 Cisco 的 `no synchronization` 命令，这
是当前的常见配置（disable BGP synchronization and rely on a full mesh of IBGP
routers）。

### 6.2 路由更新方式

对于像互联网这样复杂的网络来说，**路由稳定性**（route stability）是一个很大的问题。
这和链路的稳定性，以及路由的注入方式（动态/静态）有关系。

#### BGP 动态注入

可以进一步分为：

* 纯动态注入：所有从 IGP 学习到的路由都注入到 BGP（通过 `redistribute` 命令）
* 半动态注入：部分从 IGP 学习到的路由注入到 BGP（通过 `network` 命令）

动态注入：

* 优点
    * 配置简单，IGP 路由自动注入 BGP，不管是具体哪种 IGP 类型（RIP、OSPF、IS-IS等等）
* 缺点
    * 可能会泄露内网路由到公网，造成安全问题
    * IGP 路由抖动会影响到 BGP，想象一下几百个 AS 同时有 IGP 路由抖动给 BGP 造成
      的影响

为了防止因特网的路由抖动，提出了一些技术，第十章会介绍到，一个叫 route dampening
的进程会对抖动的路由进行惩罚，抑制它进入 BGP 的时间。

保证路由稳定性是一项很难的工作，因为很多因素都是不受控的，例如硬件故障。减少路由
不稳定的一种方式是路由聚合，可以在 AS 边界做，也可以在因特网边界做。

最后，另一种解决路由不稳定的方式是静态注入路由。

#### BGP 静态注入

静态注入的路由会一直存在于路由表，一直会被通告。

可以解决路由不稳定的问题，但是会导致失效的路由无法自动从路由表删除，而且静态配置
相当繁琐，配置不当还容易产生环路。因此只在特定的场景下使用。

#### 静态路由和动态路由例子：移动网络

移动网络中分配 IP 地址的问题。

移动设备希望在从一个 AS 移动到另一个 AS 的过程中，需要切换 IP 地址。因此，静态路
由的方式不合适，只能通过动态注入 BGP 的方式。具体到实现，一种方式就是将 IGP 注入
BGP。这会带来一些问题，前面已经分析过，例如需要对路由做过滤。

另一种实现方式是通过 `network` 命令，在所有位置的边界路由器定义这些网络。

### 6.3 重叠的协议：后门（Overlapping Protocols: Backdoors）

路由可以通过多种协议学习，选择不同的协议会影响流量的路径。例如，如果选择一条 RIP
路由，可能会走某链路；而选择一条 eBGP 路由，则可能会走另一条链路。

后门链路（backdoor link）提供了一种 IGP 路径的备选方式，可以用来替代 eBGP 路径。
可以通过后门链路到达的 IGP 路由称作后门路由。

有了这种后门路由，就需要一种机制，能够使得一种协议的优先级比另一种更高。例如，
Cisco 提供的 ***administrative distance*** 就是这个功能。

通过设置不同协议的路由的优先级，使得后门路由被选中作为最优路由。
或者，前面介绍过，通过 `distance` BGP 命令也可以设置优先级。

### 6.4 BGP 路由过程

简要查看完整的 BGP 路由处理过程。

BGP 是一种相当简单的协议，这也是它灵活的原因。BGP peer 之间通过 UPDATE 消息交换
路由。BGP 路由器收到 UPDATE 消息后，运行一些策略或者对消息进行过滤，然后将路由转
发给其他 BGP peers。

BGP 实现需要维护一张 BGP 路由表，这张表是和 IP 路由表独立的。如果到同一目的地有
多条路由，BGP 并不会将所有这些路由都转发给 peer；而是选出最优路由，然后将最优路
由转发给 peer。除了传递从 peer 来的 eBGP 路由，或从路由反射器客户端（RR client）
来的 iBGP 路由之外，BGP 路由器还可以主动发起路由更新，通告它所在 AS 内的内部网络。

来源是本 AS 的合法的本地路由，以及从 BGP peer 学习到的最优路由，会被添加到 IP 路
由表。IP 路由表是最终的路由决策表，用于操控转发表。

<p align="center"><img src="/assets/img/internet-routing-arch/6-8.PNG" width="60%" height="60%"></p>
<p align="center">图 6-8 Routing Process Overview</p>

#### BGP 路由：通告和存储

根据 RFC 1771，BGP 协议中路由（route）的定义是：一条路由是**一个目标及其到达这个目
标的一条路径的属性**组成的信息单元（a route is defined as a unit of information that pairs a destination with the attributes of a path to that destination）。

路由在 BGP peer 之间通过 UPDATE 消息进行通告：目标是 NLRI 字段，路径是 path 属性
字段。

路由存储在 RIB（Routing Information Bases）。

BGP speaker 选择通告一条路由的时候，可能会修改路由的 path 属性。

<p align="center"><img src="/assets/img/internet-routing-arch/6-9.PNG" width="60%" height="60%"></p>
<p align="center">图 6-9 BGP 路由表的逻辑表示</p>

* 一个 Adj-RIB-In 逻辑上对应一个 peer，存储从 peer 学习到的路由
* Loc-RIB 存储最优路由
* 一个 Adj-RIB-Out 逻辑上对应一个 peer，存储准备从这个路由器发送给对应 peer 的路由

这里的逻辑图是将过程分成了三部分，每部分都有自己的存储，但实现不一定这样，事实上
大部分实现都是共享一份路由表，以节省内存。

<p align="center"><img src="/assets/img/internet-routing-arch/6-10.PNG" width="60%" height="60%"></p>
<p align="center">图 6-10 Sample Routing Environment</p>

#### BGP 决策过程总结

1. 如果下一跳不可达，则忽略此路由（这就是为什么有一条 IGP 路由作为下一跳如此重要
   的原因）
1. 选择权重最大的一条路径
1. 如果权重相同，选择本地偏向（local preference）最大的一条路由
1. 如果没有源自本地的路由（locally originated routes），并且 local preference 相
   同，则选择 AS_PATH 最短的路由
1. 如果 AS_PATH 相同，选择 origin type 最低（`IGP < EGP < INCOMPLETE`）的路由
1. 如果 origin type 相同，选择 MED 最低的，如果这些路由都是从同一个 AS 收到的
1. 如果 MED 相同，优先选择 eBGP（相比于 iBGP）
1. 如果前面所有条件都相同，选择经过最近的 IGP 邻居的路由——也就是选择 AS 内部最短
   的到达目的的路径
1. 如果内部路径也相同，那就依靠 BGP ROUTE_ID 来选择了。选择从 RID 最小的 BGP 路
   由器来的路由。对 Cisco 路由器来说，RID 就是路由器的 loopback 地址。

### 6.5 Controlling BGP Routes

介绍路由的每个属性。

#### ORIGIN（type code 1)

* 0: `IGP`, NLRI that is inteior to the originating AS
* 1: `EGP`, NLRI learned via EGP
* 2: `INCOMPLETE`, NLRI learned by some means

#### AS_PATH

BGP 依靠这个字段实现路由无环路。里面存储了路径上的 ASN。

<p align="center"><img src="/assets/img/internet-routing-arch/6-11.PNG" width="60%" height="60%"></p>
<p align="center">图 6-11 Sample Loop Condition Addressed by the AS_PATH Attribute</p>

#### NEXT_HOP

<p align="center"><img src="/assets/img/internet-routing-arch/6-12.PNG" width="60%" height="60%"></p>
<p align="center">图 6-12 BGP NEXT_HOP Example</p>

### 6.6 Route Filtering and Attribute Manipulation

### 6.7 BGP-4 Aggregation

### 6.8 Looking Ahead

### 6.9 Frequently Asked Questions

#### 是否应该将 BGP 路由注入 IGP？

不。不推荐将 BGP 路由注入 IGP。应该关闭 BGP synchronization。

### 6.1 References

<a name="chap_7"></a>

## 7 冗余、对称和负载均衡

* 冗余：发生链路故障时，有备用路由
* 对称：流量在相同的点进出 AS（enters and exits an AS at the same point）
* 负载均衡：在多条链路之间均衡地分发流量

### 7.1 冗余

冗余和对称这两个目标是有冲突的：**一个网络提供的冗余越多，它的对称性越难保证**。

**冗余最终会以路由的形式落到路由表**。为了避免路由表过于复杂，通常的冗余实现方式
就是默认路由（default routing）。

#### 设置默认路由

默认路由是**优先级最低的路由**，因此是最后的选择（gateway of the last resort）。分为两种：

* 动态学习
* 静态配置

##### 动态学习默认路由

0.0.0.0/0.0.0.0 是全网约定的默认路由，并且可以动态通告给其他路由器。通告此路由的
系统表示它可以**作为其他系统最后尝试的网关**（represents itself as a gateway of
last resort for other systems）。

动态默认路由可以通过 BGP 或 IGP 学习。出于冗余目的，应该设置允许从多个源学习默认
路由。在 BGP 中，可以通过设置 `local reference` 给默认路由设置优先级。如果高优先
级的默认路由发生故障，低优先级的可以补上。

##### 静态配置默认路由

动态学习到的默认路由可能不是我们想要的，因此一些管理员会选择静态配置默认路由。

静态默认路由也可以设置多条，用优先级区分。

### 7.2 对称

流量从 AS 的哪个点出去的，也通过哪个点进来。

大部分情况下都应该是对称的，但是特定的一些场景下也会有非对称的情况，与设计有关。

> 实际上非对称路由在现实中并不少见（more often than not），而且也没有造成太大问
> 题。

### 7.3 非对称路由

流量要根据 inbound 和 outbound 分开考虑。
例如，如果网络和 ISP1 之间的带宽被打爆了，那你肯定是先问：是 inbound 还是
outbound 被打爆了？

路由行为影响因素：

* `inbound traffic` 受**本 AS 通告出去的路由**的影响
* `outbound traffic` 受**本 AS 从其他 AS 学习到的路由**的影响

因此，要调整 inbound 流量，就需要调整从本 AS 通告出去的路由；而要调整 outbound，
就需要控制本 AS 如何学习邻居通告的路由。

### 7.4 不同场景下对三者的权衡

可以看出，冗余、对称和负载均衡之间是有联系的，并且存在一些冲突。

第六章介绍的路由属性（routing attributes）是实现这三个目标的工具。

### 7.5 Looking Ahead

### 7.6 Frequently Asked Questions

**BGP 本身不考虑链路速度和流量特性**，因此需要管理员通过策略配置达到所期望的目的。


<a name="chap_8"></a>

## 8 AS 内部路由控制

### 8.1 非 BGP 路由器和 BGP 路由器的交互

非 BGP 路由器如何连接到外部网络：

1. 将 BGP 注入到 IGP（即，将外部路由注入到 AS 内部）
1. 静态配置 AS 内的默认路由到外网

#### 8.1.1 BGP 注入 IGP

**不推荐将全部 BGP 路由注入到 IGP**，这会给 IGP 路由增加很大的负担。IGP 路由是针
对AS 内路由和很小的网络设计的，不适用于大规模网络。但可以将部分 BGP 路由注入 IGP。

需要考虑的因素：

1. 计算路径和处理路由更新所需的内存、CPU
1. link utilization from routing control traffic
1. 对收敛的影响
1. IGP 的限制
1. 网络拓扑
1. 其他

缺点：

1. 如果 IGP 非常老，例如 RIP-1，不支持 CIDR，那 BGP 过来的 CIDR 路由都会丢失
1. BGP 路由的抖动会引起 IGP 的抖动，很多 IGP 挂掉都是这个原因

#### 8.1.2 静态配置默认路由

在每个 AS 的边界路由器上添加一条默认路由。

### 8.2 BGP Policies Conflicting with Internal Defaults

**BGP 路由策略和 IGP 的默认行为有冲突会导致出现路由环路**，来看 图 8-2 这个例子
。

#### 8.2.1 例子：主备 BGP 策略和 IGP 默认行为冲突导致环路

考虑图 8-2。RTC 和 RTD 和外面的 AS 运行 eBGP；在 AS 内部，它们两个之间运行 iBGP
。但是，他们不是直连的，要经过 RTA 和 RTB 两个非 BGP 路由器。RTA 和 RTB 会和 AS
内的所有路由器运行 IGP 协议，因此它们看不到所有的外部路由（BGP 路由）。

如果 BGP 策略是 RTD 做主，RTC 做备，那 RTC 收到流量时，会转发给 RTD，但因为 RTC
和 RTD 不是直连的，因此它会先转发给 RTA。RTA 根据 IGP 学习到的默认路由是 RTC，因
此它又会将流量转发回 RTC，形成了路由环路。

RTC 和 RTD 之间出现环路：

<p align="center"><img src="/assets/img/internet-routing-arch/8-2.PNG" width="60%" height="60%"></p>
<p align="center">图 8-2 Following Defaults: Loop Situation</p>

解决这个问题的办法有如下几种。

##### 方案 1: 修改 IGP Metric

**将 RTA 的默认路由从指向 RTC 改为指向 RTD**。

具体地，将 RTC 的默认路由 0/0 的 metric 设置的非常大。这样 RTD 的路径相比之下很
短，RTA就会将 RTA-RTB-RTD 作为最优路径。

##### 方案 2: 直连 RTC 和 RTD

**直连 RTC 和 RTD，使得二者之间的最优路径不需要经过 RTA。**

RTC-RTD 是 iBGP 路径，RTC-RTA-RTB-RTD 是 IGP 路径。

##### 方案 3: Transit Routers 都跑 BGP

Transit routers 都跑 BGP，在图 8-2 中就是 RTA 和 RTB。

##### 方案 4: 控制默认路由自动注入

RTD 和 RTC 只有一个注入默认路由，另一个不注入。

缺点：在对 primary/backup 模式有用，而且 primary 挂掉之后，backup 用不了，因为它
没有注入默认路由。

#### 8.2.2 Defaults Inside the AS: Other BGP Policies

IGP 默认配置和 BGP policy 冲突产生的环路。

### 8.3 策略路由（Policy Routing）

通常所说的路由，都是根据**目的地址**做转发。

而策略路由是根据**源地址**，或**源地址+目的地址**做转发。可以做更高级的路由控制。

### 8.4 Looking Ahead

### 8.5 Frequently Asked Questions

<a name="chap_9"></a>

## 9 大型 AS 控制管理

网络节点超过几百个之后，会带来很大的管理问题。

## 9.1 路由反射器（Route Reflectors）

BGP 之间通过 full-mesh 做 peering，当节点多了之后，BGP mesh 非常复杂。

引入路由反射器（Route Reflector，RR）。RR 带来的好处：

1. 向多个 peer 发送 UPDATE 时效率更高
1. 路由器只需要和 local RR 做 peer，大大减少 BGP session 的数量

只有 BGP mesh 比较大之后才推荐 RR。因为 **RR 也是有代价的**：

1. 增加额外计算开销
1. 如果配置不正常会引起路由环路和路由不稳定

#### 9.1.1 没有 RR 的拓扑：full-mesh

<p align="center"><img src="/assets/img/internet-routing-arch/9-1.PNG" width="60%" height="60%"></p>
<p align="center">图 9-1 Internal Peers in a Normal Full-Mesh Environment</p>

没有 RR 的情况下，同一 AS 内的 BGP speaker 之间形成一个 **logical** full-mesh。
如图9-1 所示，虽然 RTA-RTC 之间没有物理链路，但仍然有一条逻辑 peer 链路。

**RTB 从 RTA 收到的 UPDATE 消息并不会发送给 RTC**，因为：

1. RTC 是内部节点（同一个 AS）
1. 这条 UPDATE 消息也是从内部节点发来的（RTA）

因此，如果 RTA-RTC 之间没有做 peer，RTC 就收不到 RTA 的消息；所以没有 RR 的情况
下必须得用 full-mesh。

#### 9.1.2 有 RR 的拓扑

再来看有 RR 的情况，如图 9-2 所示。

<p align="center"><img src="/assets/img/internet-routing-arch/9-2.PNG" width="60%" height="60%"></p>
<p align="center">图 9-2 Internal Peers Using a Route Reflector</p>

引入 RR 之后，其他路由器称为客户端。客户端和 RR 之间做 peer，RR 再将消息转发给其
他 IBGP 或 eBGP peers。

RR 大大减少了 BGP session 数量，使得网络更具扩展性。

#### 9.1.3 路由反射原则

所有设备分为三类：

1. 路由反射器
1. 路由反射器的客户端，简称客户端
1. 非路由反射器的客户端，简称非客户端

<p align="center"><img src="/assets/img/internet-routing-arch/9-3.PNG" width="60%" height="60%"></p>
<p align="center">图 9-3 Route Reflection Process Components</p>

**路由反射原则**：

1. 从 nonclient peer 来的路由，只反射给 clients（无须反射给 nonclients 是因为
   nonclients 之间有 full-mesh）
1. 从 client peer 来的路由，反射给 clients 及 nonclients
1. 从 eBGP peer 来的路由，反射给 clients 及 nonclients

RR 只用于 AS 内部，因此 AS 边界的外部路由节点（eBGP）也当作 nonclients 对待。

#### 9.1.4 RR 高可用

RR 是集中式节点，因此非常重要，需要做冗余。

但是，如果本身物理拓扑就没有冗余，那 RR 做冗余也是无用的，如下图。

<p align="center"><img src="/assets/img/internet-routing-arch/9-4.PNG" width="60%" height="60%"></p>
<p align="center">图 9-4 Comparison of Logical and Physical Redundancy Solutions</p>

#### 9.1.5 RR 拓扑

RR 拓扑主要取决于物理网络拓扑，事实上每个路由器都可以配置成 RR。

<p align="center"><img src="/assets/img/internet-routing-arch/9-5.PNG" width="60%" height="60%"></p>
<p align="center">图 9-5 Complex Multiple Route Reflector Environment</p>

**RR 不会修改路由消息的属性**（UPDATE attributes，例如 NEXT_HOP），但是一些实现
会允许 RR 做一些过滤工作。

<p align="center"><img src="/assets/img/internet-routing-arch/9-7.PNG" width="60%" height="60%"></p>
<p align="center">图 9-7 Typical BGP Route Reflection Topology</p>

<p align="center"><img src="/assets/img/internet-routing-arch/9-8.PNG" width="60%" height="60%"></p>
<p align="center">图 9-8 Full-Mesh BGP Topology</p>

<a name="chap_10"></a>

## 10 设计稳定的因特网

### 10.1 因特网路由的不稳定性

最常见的现象：路由抖动（flapping），BGP 频繁 UPDATE 和 WITHDRAWN 路由。

一些影响因特网路由稳定性的因素：

1. IGP 不稳定
1. 硬件错误
1. 软件问题
1. CPU、内存等资源不足
1. 网络升级和例行维护
1. 人为错误
1. 链路拥塞

### 10.2 BGP Stability Features

# IV: Internet Routing Device Configuration
