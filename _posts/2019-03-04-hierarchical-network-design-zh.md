---
layout: post
title:  "[译] 数据中心网络：分层网络设计综述"
date:   2019-03-04
categories: network architecture design
---

### 译者序

本文内容翻译自 Cisco 的一门叫 [***Connecting Networks***](http://www.ciscopress.com/store/connecting-networks-companion-guide-9781587133329) 的教材（2014），
英文版可以在官网[在线阅读](http://www.ciscopress.com/store/connecting-networks-companion-guide-9781587133329)
，也可以[在这里下载 PDF](http://ptgmedia.pearsoncmg.com/images/9781587133329/samplepages/1587133326.pdf)（仅前三章）。

搜索网络架构的资料时偶然看到这本小册子，其中关于基础网络和数据中心网络架构设计的
内容非常不错，故通过翻译的方式（不知道有没有中文版）做个笔记顺便加深理解。**本文
翻译仅供个人学习交流，无商业目的，如有侵权将及时删除**。

本篇翻译自原书第一章第一节，介绍经典的数据中心三层网络架构：接入层-汇聚层-核
心层。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。** 
原书英文比较简单，也推荐有需要的读者阅读英文全书。

以下是译文。

----

## 1.0 引言

网络不仅要满足企业的当前需求，还要能演进以支持新的技术。网络设计原则和模型能帮助
网络工程师设计和建造灵活、有弹性和方便管理的网络。

本章将介绍网络设计的概念、原则、模型和架构，以及遵循系统的设计方法（systematic
design approach）所带来的收益。

## 1.1 分层网络设计综述

思科的**分层（三层）互连网络模型**（hierarchical internetworking model）是工业界
设计可靠、可扩展、高性价比的互连网络时广泛采用的模型。在本章中，你将学习到接入层
、分发层（思科这里称作 distribution layer，但更通用的称呼是 aggregation layer，
即汇聚层。译者注）、核心层，以及它们在分层网络模型中承担的角色。

### 1.1.1 企业网络设计

**对网络规模的了解**，以及**良好的结构化工程原则**（an understanding of
network scale and knowledge of good structured engineering principles），对讨论
网络设计非常有帮助。

#### 1.1.1.1 网络需求

讨论网络设计时，通常根据设备数量将网络分成几类：

* 小型网络：支持最多 200 个设备
* 中型网络：支持 200 ~ 1000 个设备
* 大型网络：支持 1000+ 设备

网络设计随规模和公司需求的不同而不同。例如，小公司的设备数量比较少，因此网络基础
设施就无需像大公司设计的那么复杂。

网络设计需要考虑很多变量。例如，图 1-1 是一个由主 site 构成的大型企业网络连接
小型、中型、大型 site 的高层拓扑示例。

<p align="center"><img src="/assets/img/enterprise-network-design/1-1.PNG" width="60%" height="60%"></p>
<p align="center">图 1-1 大型企业网络设计</p>

网络设计领域还在快速发展，需要用到大量的知识和经验。本节的目的是介绍一些网络设计
领域已经广泛接受的概念。

#### 1.1.1.2 结构化工程原则

不管网络规模和企业需求如何，结构化工程原则都是设计一个成功的网络架构的关键因
素。这些原则包括：

* **层级化**（hierarchy）：分层网络模型（hierarchical network model）是设计可靠
  的网络基础设施时非常有用的高层工具，它将复杂的网络设计问题拆分为更小的易掌控的
  若干个领域。
* **模块化**（modularity）：将功能划分为几个网络模块会使设计更容易。
  Cisco 定义的模块包括企业网（enterprise campus）、服务模块（service block）、数
  据中心（data center）、以及互联网边缘（internet edge）。
* **弹性**（resiliency）：保证网络在正常和异常的情况下都可用（
  available for use）。正常的情况包括正常或预期的网络流量和模式（traffic flow
  and traffic patterns），计划事件，例如维护窗口（maintenance window）。不正
  常的情况包括硬件和软件故障、极端网络流量、异常流量（unusual traffic
  patterns）、DoS 事件、计划外事件等等。
* **灵活性**（flexibility）：在不对网络做根本性改造（forklift upgrade）的前提
  下（例如更换主要的硬件设备），有修改部分网络、添加新服务或扩容的能力

要满足这些基础的设计目标，网络必须构建在分层架构之上，这样的架构足够灵活，并且支
持网络规模的增长。

### 1.1.2 分层网络设计

本节讨论分层网络模型的三个层：接入层、分发层、核心层。

#### 1.1.2.1 网络层级（Network Hierarchy）

早期的网络都是扁平拓扑（flat topology），如图 1-2 所示。

<p align="center"><img src="/assets/img/enterprise-network-design/1-2.PNG" width="50%" height="50%"></p>
<p align="center">图 1-2 扁平网络</p>

当更多的设备需要接入网络时，就添加集线器（hub）和交换机（switch）。扁平网络设计
的缺点是**很难控制广播流量，或者对特定流量进行过滤**。当网络中的设备越来越多时，
响应时间也会越来越慢，最终使得网络不可用。

因此，我们需要一个更好的方案。现在，大部分公司都使用图 1-3 所示的分层网络方案。

<p align="center"><img src="/assets/img/enterprise-network-design/1-3.PNG" width="50%" height="50%"></p>
<p align="center">图 1-3 分层网络</p>

分层网络设计将网络划分为多个独立的层级（discrete layers），每一层（
layer，or tier）提供特定的功能，这些功能也定义了它们在整个网络架构中的角色。这
种设计使得网络设计师和架构师可以针对每层的角色来优化和选择合适的网络硬件
、软件和功能特性。分层网络模型对局域网（LAN）和广域网（WAN）设计均使用。

将扁平网络划分成更小、更易管理的组成部分的好处是可以**将本地流量限制在本地**（
local traffic remains local）。只有目的是其他网络的流量才会被传送到更高的层。例
如图 1-3 所示的扁平网络已经被划分为三个独立的广播域（broadcast domains）。

一个典型的企业分层局域网（hierarchical LAN）包括三层：

* **接入层**：提供工作组/用户接入网络的功能
* **分发层**：提供基于策略的连接功能，控制接入层和核心层的边界
* **核心层**：提供分发层交换机之间的高速传输

另一个三层分层网络设计如图 1-4 所示，注意其中的每个 building 都是分层网络模型
，包括了接入层、分发层和核心层。

<p align="center"><img src="/assets/img/enterprise-network-design/1-4.PNG" width="50%" height="50%"></p>
<p align="center">图 1-4 多建筑区（Multi Building）企业网络设计</p>

> 注意：设计网络的物理拓扑并没有绝对的规则。虽然很多网络都是基于三层的物理
> 设备搭建的，但这并不是强制条件。在小一些的网络中，核心层和分发层可能会合并为
> 一层，由同一个物理交换机充当，这样网络就变成了两层。这被称为 collapsed core
> design（塌缩的核心层设计）。

#### 1.1.2.1 接入层（Access Layer）

在局域网中，接入层提供终端设备接入网络的功能；在广域网中，它可能还提供远程办公（
teleworker）或远程 site 通过 WAN 访问公司网络的功能。

如图 1-5 所示，小的商业网络的接入层通常由 L2 交换机和接入点构成，提供工作站和服
务器之间的互联。

<p align="center"><img src="/assets/img/enterprise-network-design/1-5.PNG" width="50%" height="50%"></p>
<p align="center">图 1-5 接入层</p>

接入层的功能包括：

1. 二层转发
1. 高可用
1. 端口安全（port security）
1. QoS 分类和标记，信任边界（trust boundaries）
1. ARP inspection
1. 虚拟访问控制列表（Virtual ACL, VACL）
1. 生成树（spanning tree）
1. Power over Ethernet (PoE) and auxiliary VLANs for VoIP

#### 1.1.2.3 分发层（Distribution Layer）

分发层对接入层的包进行聚合（aggregate），然后送到核心层进行路由。如图 1-6
所示，**分发层是 L2 网络（交换）和 L3 网络（路由）的边界**。

<p align="center"><img src="/assets/img/enterprise-network-design/1-6.PNG" width="50%" height="50%"></p>
<p align="center">图 1-6 分发层</p>

分发层设备是布线室（wiring closet）的核心（focal point）。通常使用路由器或者多层
交换机（multilayer switch）划分工作组和隔离网络问题。

一个分发层交换机可能会为许多接入层交换机提供上游服务（upstream services）。

分发层提供的功能：

1. 聚合 LAN 或 WAN 链路
1. 基于策略的安全，通过 ACL 和 filtering 形式
1. LAN 和 VLAN 之间，以及路由域（例如 EIGRP 到 OSPF）之间的路由服务
1. 冗余和负载均衡
1. 路由聚合和摘要的边界，配置在与核心层连接的端口上
1. 广播域控制（路由器和多层交换机不转发广播包），分发层设备充当了广播域之间的边界（demarcation）

#### 1.1.2.4 核心层（Core Layer）

核心层也称作网络骨干（network backbone），由高速网络设备组成，例如 Cisco
Catalyst 6500 和 6800。核心层设计用来**尽可能快地转发包，以及互联多个网络模块**
，例如分发模块、服务模块、数据中心，以及 WAN 边缘。

从图 1-7 可以看出，核心层对分发层设备的互联非常关键（例如，将分发层连接到 WAN 和
因特网边缘）。

<p align="center"><img src="/assets/img/enterprise-network-design/1-7.PNG" width="50%" height="50%"></p>
<p align="center">图 1-7 核心层</p>

核心层应当是高可用和有冗余的。核心层对分发层设备的所有流量进行聚合，因此转
发数据的性能必须很高。

核心层需要考虑：

1. 高速交换（例如，fast transport）
1. 可靠性和容错
1. **通过更快的而不是更多的设备进行扩展**（scaling by using faster, not more, equipment）
1. 避免对包进行 CPU 密集的操作，这些操作可能来自安全、检测、QoS 分类或其他过程

#### 1.1.2.5 Two-Tier Collapsed Core Design（两层塌缩核心层设计）

三层分层设计最大化了性能、网络可用性、以及网络设计的扩展性。

然而，很多小型企业网络并不会随着时间变的很大。因此，一种将核心层和分发层合并
的两层设计（two-tier hierarchical design）就更加实际。“collapsed core”（塌缩核心
层）就是核心层和分发层合并，由同一个设备实现其功能。这种设计的主要目的就是**在保
留三层模型的大部分好处的同时，降低网络成本**。

在图 1-8 中，核心层和分发层设备合并成一层设备，由多层交换机来充当。

<p align="center"><img src="/assets/img/enterprise-network-design/1-8.PNG" width="50%" height="50%"></p>
<p align="center">图 1-8 两级分层设计（Two-Tier Hierarchical Design）</p>

分层网络模型提供了一个模块化的框架，使得网络设计更加灵活，而且实现和排障（
trouble shooting）也更容易。
