---
layout    : post
title     : "[笔记] Cloud Native Data Center Networking (O'Reilly 2019)"
date      : 2021-01-10
lastupdate: 2021-01-10
categories: bgp datacenter cloud-native
---

### 关于本文

本文是读 [Cloud Native Data Center Networking](https://www.oreilly.com/library/view/cloud-native-data/9781492045595/) （
O'Reilly, 2019）时的所做的一些笔记。这本书理论和实践兼备，是现代数据中心网络、云
原生数据中心网络设计和 BGP 的很好入门参考。

作者 Dinesh G. Dutt 是一家网络公司的首席科学家，在网络行业有 20 多年工作经验，曾
是 Cisco Fellow，是 TRILL、VxLAN 等协议的合作者（co-author）之一。

----

* TOC
{:toc}

---

# 1 传统网络架构面临瓶颈

**分布式应用**（distributed application）正在与**网络**（network）共舞，而且
前者是主角。

**分布式应用突然变换了舞步，现代数据中心的故事也由此开始。**
<mark>理解这种转变对网络从业人员至关重要，否则只会不断被一些新名词带着走</mark>。

本章回答以下问题：

1. 新一代应用（new applications）有哪些特征？
2. 什么是接入-汇聚-核心（access-aggregation-core）网络？
3. 接入-汇聚-核心网络架构在哪些方面已经无法满足新一代应用的需求？

## 1.1 “应用-网络”架构演进

<p align="center"><img src="/assets/img/cloud-native-dc-networking/1-1.png" width="60%" height="60%"></p>
<p align="center">Fig 1-1. The evolution of application architecture</p>

应用架构演进：

1. 单体应用

    * 通常部署在 mainframe 上。
    * 特定的厂商提供网络方案，**协议是私有的**（不是 TCP/IP 协议）。
    * 以今天的眼光看，应用所需的带宽极小。

2. 客户端-服务器应用

    * 工作站和 PC 时代。
    * LAN 开始兴起。**充斥着各种 L2、L3 和更上层协议**。
        * 以太网、Token Ring、FDDI 等是比较流行的互连方式。带宽上限 100Mbps。
        * **TCP/IP 体系开始发展**，但还没用成为主流。

3. Web 应用

    * <mark>以太网和 TCP/IP 一统互联网</mark>，其他绝大部分协议成为历史。
    * 计算和网络虚拟化：**虚拟机时代**。

4. 微服务（分布式应用）

    * 大规模的数据处理（例如 MapReduce），使数据中心网络的带宽瓶颈**从南北向变成东西向**，这是一个历史性的转变。
    * **容器时代**。

## 1.2 21 世纪以来的网络设计

<p align="center"><img src="/assets/img/cloud-native-dc-networking/1-2.png" width="60%" height="60%"></p>
<p align="center">Fig 1-2. Access-aggregation-core network architecture</p>

图 1-2 是在上世纪末开始占据统治地位的网络架构：接入-汇聚-核心三级网络架构。

图中**没有画出汇聚和核心之间的连接**，是因为这些连接**因方案和厂商而异**，而且
不影响接下来的讨论。

### 桥接（Bridging）的魅力

这种网络架构**重度依赖交换**（或称桥接，bridging），而在同时期，互联网（the
internet）真正快速发展成型。

<mark>既然支撑互联网的是 IP 路由（IP routing）技术，为什么数据中心网络没有选择路
由（routing），而是选择的交换（bridging）呢？</mark>三方面原因：

1. **交换芯片**的出现（silicon switched packet forwarding）

    * 做数据转发的芯片原先主要用在**网卡**，现在用于功能更强大的**交换设备**。
    * 这种设备显然要求芯片具备**更高密度的接口**，而这样的芯片在当时**只支持交换**（bridging），不支持路由（routing）。

2. **厂商特定的软件栈**（proprietary network software stacks）在企业中占据主导地位

    * “客户端-服务器”模型所处的时代，TCP/IP 只是众多协议种的一种，并没有今天所处的统治地位。
    * 但各家的协议有一个共同点：**二层协议是一样的，都是走交换**（bridging）。因
      此**汇聚层以下走交换**就是顺理成章也是唯一的选择。
    * 接入-汇聚-核心成为了一种**通用的网络架构**：
        * 汇聚以下走交换（bridging），不区分厂商
        * 汇聚以上走各家的三层协议
        * 这样就避免了为每家厂商的设备单独搭建一张网络。

3. **交换网络**所宣称的**零配置**（zero configuration of bridging）

    * 路由网络很难配置，甚至对某些厂商的设备来说，直到今天仍然如此。需要很多显式
      配置。
    * 相比交换，路由的延迟更大，更消耗 CPU 资源。
    * 交换网络是自学习的（self-learning），也是所谓的“零配置”（zero
      configurations）。

### 构建和扩展的桥接网络

厂商设备无关、高性能芯片加上零配置，使得桥接网络在那个年代取得很大成功。但这种网
络也存在一些**限制**：

1. 广播风暴和 STP：这是自学习的机制决定的，

    * MAC 头中没有 TTL 字段，因此一旦形成环路就无法停下来。
    * STP（生成树协议）：避免交换网络出现环路，非常复杂，因此很难做到没有 bug。

2. 泛洪成本

    * 大的交换网络的泛洪
    * 缓解：划分 VLAN，使得泛洪域限制到 VLAN 内。

3. 网关高可用

    * **网关**配置在**汇聚交换机**。
    * 为保证高可用，一组汇聚配置同一个网关；一台挂掉后自动切换到另一台。需要协议
      支持，这种协议称为**第一跳路由协议**（First Hop Routing Protocol, **FHRP**）。

    **FHRP 原理**：几台路由器之间互相检测状态，确保永远有且只有一台在应答对网关的 ARP 请求。

    FHRP 协议举例：

    * HSRP（Hot Standby Routing Protocol）：思科的私有协议。
    * **VRRP**（Virtual Router Rundundency Protocol）：目前用的最多的协议。

## 1.3 接入-汇聚-核心网络架构存在的问题

* **广播风暴**是所有在那个年代运维过这种网络的网工们的噩梦 —— 即便已经开启了 STP。
* **应用（applications）变了** —— 服务器之间的东西向流量开始成为瓶颈，而这种网络架构主要面向的是“客
  户端-服务器”模式的南北向流量时代。
* **应用的规模**显著变大，在故障、复杂性和敏捷度方面对网络提出了完全不同于以往的新需求。

现有网络架构无法解决以上问题。

### 不可扩展性（Unscalability）

* 泛洪

    自学习机制是 “flood-and-learn”，因此泛洪是不可避免的。当网络规模非常大时
  （例如大规模虚拟机场景），定期地会有上百万的泛洪包，终端计算节点不堪重负。

* VLAN 限制

    VLAn 总共 4096 个，无法满足云计算时代的多租户需求。

* 汇聚应答 ARP 的负担

    汇聚负责应答 ARP。ARP 数量可能非常多，导致汇聚交换机 CPU 飙升。

* 交换机水平扩展性和 STP 限制

    理论上，增加汇聚交换机数量似乎就能增加东西向带宽。但是，**STP 不支持两个以上
    的交换机**场景，否则后果无法预测。因此汇聚交换机就固定在了两个，无法扩展。

### 复杂性（Complexity）

交换网络需要运行大量不同类型的协议，例如：

1. STP
1. FHRP
1. 链路检测协议
1. 厂商特定的协议，例如 VLAN Trunking Protocol（VTP）

显著增加了网络的复杂性。

STP 使得网络只能用到一半的链路带宽。

### 故障域（Failure Domain）

* 一条链路挂掉，可用带宽减半。
* 一台汇聚挂掉，整个网络的带宽减半；而且此时所有流量都会打到同组的另一台汇聚，很
  容易导致这一台也扛不住，即发生级联故障。
* 级联故障的另一种场景：广播风暴，整个系统全挂。

### 不可预测性（Unpredictability）

STP 的行为无法预测。一些常规故障或设备维护都可能导致 STP 故障。

### 欠灵活性（Inflexibility）

<mark>VLAN 在汇聚交换机终结</mark>，即在交换（bridging）和路由（routing）的边界终结。

网工无法灵活地将任意可用接口分配给用
户的VLAN（需要端到端的链路都有可用接口才行）。

### 欠敏捷性（Lack of Agility）

云计算场景下，需要非常快速的网络资源交付。

VLAN 需要整条链路端到端的配置和感知，而且配置会引起控制平面的 STP 等协议震荡（收
敛），容易引起网络故障。因此添加或删除 VLAN 都需要天级别的时间。

## 1.4 The Stories Not Told

一些对这种网络方案的改进尝试：TRILL 和 MLAG。

经过时间沉淀，**其他各种上层协议（L3+）已经退潮，IP 协议成为唯一主流**。
<mark>是时候从网络需求出发，设计一种新的架构了</mark>。

## 1.5 小结

本章我们看到了**应用架构**（application architecture）是如何驱动**网络架构**演进的。

# 2 CLOS：新一代网络架构

> Form is destiny — in networking even more than in life. The structure of the
> network lays the foundation for everything that follows.

形式决定结果 —— 在网络世界尤其如此。**网络结构**为后面的所有东西
奠定了基础，正如树根与树干、树冠的关系一样。**新一代的结构就是**：CLOS 拓扑。

云原生数据中心基础设施的先行者们希望打造一种<mark>具备大规模扩展性的东西</mark>。
CLOS 拓扑就像红杉树，用一种类似**分形**的模型（fractal model）实现了**水平扩展**（scale out）。

**本书主要内容就是 CLOS 拓扑中的网络设计**。<mark>CLOS 拓扑及其特性，
是每一位网络工程师和网络架构师的必修课</mark>。

## 2.1 Introducing the Clos Topology

<p align="center"><img src="/assets/img/cloud-native-dc-networking/2-1.png" width="60%" height="60%"></p>
<p align="center">Fig 2-1. Illustration of a common Clos topology</p>

**Spine 和 Leaf 可以是同一种交换机**，这虽然不是强制要求，但**同构设备**的使用会
给这种架构带来明显收益。

Spine-Leaf 架构的容量（capacity）很高，因为

1. 任何两台服务器之间都有多条可达路径。
2. 添加 Spine 节点可以直接扩展 Leaf 之间的可用带宽。

<mark>Spine 交换机只有一个目的</mark>：连接所有的 Leaf 节点。

* 服务器不会直连到 Spine。
* Spine 也不承担其他的功能。因此 Spine 与三级架构中的汇聚交换机的角色是不一样的。

总结起来就是，Spine-Leaf 架构中，<mark>所有功能都下放到了边缘节点</mark>（Leaf
和服务器），<mark>中心节点（Spine）只提供连接功能</mark>。

水平扩展（scale-out）和垂直扩展（scale-in）：

1. Spine-Leaf 架构可以实现良好的水平扩展（scale-out)：

    * 添加 Leaf 和服务器节点：扩展系统容量。
    * 增加 Spine 节点：扩展互连带宽。

2. Access-Aggregation-Core 架构扩展容量的方式：只能替换成性能更强的设备，称为垂直扩展（scale-in）。

## 2.2 深入理解 Clos 架构

### 使用同构设备

CLOS 架构的一个好处是：**只用一种设备就能构建出超大型网络**（build very large
packet-switched networks using simple fixed-form-factor switches）。

从根本上改变了我们思考网络故障、管理网络设备，以及设计和管理网络的方式。

### 使用路由作为基本互连模型

接入-汇聚-核心三级网络架构的一个固有限制是：<mark>只能支持两台汇聚交换机</mark>。
那 CLOS 架构是如何支持多台 Spine 的呢？答案是：CLOS 中不再使用 STP，交换机之间的
互连不再走桥接（bridging），而是走路由（routing）。

但并不是说 CLOS 中不再有桥接，只不过转发已经限制到了边缘节点，即
Leaf 交换机和服务器之间。

* 同一个机柜内，同网段之间：走桥接（bridging）。
* 跨机柜实现 bridging：可以借助 VxLAN。

前面提到，用交换（bridging）做交换机互连的原因之一是：**各厂商有不同的三层协议，
只有二层是相同的，都是以太网**。发展到后来，各种三层协议逐渐淘汰，IP 协议作为唯一
的三层协议一统江湖，因此**用 bridging 方式做交换机互连**已经不是必须的了。

那<mark>路由方式（routing）到底是如何支持多台 Spine 的呢</mark>？答案：ECMP。

从本质上来说，CLOS 拓扑是用路由（routing）替代了原来的交换（switching），作为最
主要的数据包转发方式（primary packet forwarding model）。

### 收敛比（Oversubscription）

<mark>收敛比：下行带宽 / 上行带宽。</mark>

收敛比是 `1:1` 的网络称为**无阻塞网络**（nonblocking network）。

如果 Spine 和 Leaf 都是 `n` 端口交换机，那 CLOS 拓扑

1. <mark>支持的最大服务器数量</mark>：`n*n/2`

    * n=64 时，支持 2048 台服务器
    * n=128 时，支持 8192 台服务器

1. <mark>所需的交换机数量</mark>（假设无阻塞网络）：`n + n/2`

    * n 台 leaf，n/2 台 spine
    * n=64 时，需要 96 台交换机

### 互连带宽

ISL: inter-switch link。

更大的 Spine-Leaf 互连带宽的好处：

1. 减少布线成本（cost of cabling）。
2. 减少 Spine 交换机数量。
3. 减小运维负担。
4. 任何一条互连链路被单个大象流打爆的概率更低。

### 实际中的一些限制

考虑到制冷、机柜尺寸、服务器封装、交换机芯片等方面的原因，以上的理论并不能原封不
动落实到实际的数据中心中。

受电源功率限制，单个机柜一般不超过 20 台服务器。考虑到散热能力，这个限制可能会更
小。

实际中很少需要无阻塞网络，Spine 和 Leaf 也使用不同类似的设备。商业芯片厂商一般会
**提供配套的 Spine 和 Leaf 交换芯片**，例如 Broadcom 的 `Trident` 和 `Tomahawk`
系列。

### 更细粒度的故障域

有了多台 Spine 之后，挂掉一台就不会产生灾难性的影响。例如，假如有 16 台 Spine，
那平均来说，挂掉一台只会影响 1/16 的流量。而在传统网络中，挂掉一台汇聚会影响 1/2
的流量。

另外，挂掉一条链路（link）时，只影响一台 leaf 到一台 spine 之间的流量，这台 leaf
到其他 spine 的流量是不受影响的。

> there are no systemic failures of the style found in access-agg networks due
> to the use of routing, not bridging, for packet switching.

用路由代替交换，使得 CLOS 架构消除了**系统性故障**（systemic failures）的风险，
而接入-汇聚-核心三级网络架构是无法消除这种风险的（例如，全网广播风暴）。

## 2.3 扩展 Close 拓扑（Scaling the Clos Topology）

前面看到，128 端口的两级 CLOS 架构最多支持 8192 台服务器。如何设计出能支撑更多服
务器的 CLOS 架构呢？答案是三级（甚至更多级）CLOS 架构。

<p align="center"><img src="/assets/img/cloud-native-dc-networking/2-5.png" width="60%" height="60%"></p>
<p align="center">Fig 2-5. Three-tier Clos topology with four-port switches</p>

上图中有两种三级 CLOS 的设计：

* 图 b：SuperSpine 和 Spine 是同一台设备，它们之间的互连是在交换芯片完成的。

  * **Facebook 带火了这种设计**。

* 图 c：三层交换机都是独立设备，Spine 和 Leaf 组成 POD，再和上面的 SuperSpine 互连。

  * **Microsoft, AWS** 使用这种设计。

三级 CLOS 服务器和交换机数量：

1. 支撑的最大服务器数量：`n*n*n/4`

    * n=64 时，支撑 65536 台服务器。
    * n=128 时，支撑 524288 台服务器。

2. 所需交换机数量：`n + n*n`

## 2.4 对比两种 Three-Tier Models

## 2.5 Clos 拓扑带来的其他变化

### 重新思考 Failures and Troubleshooting

* 设备类型更单一，故障类型更明确。
* 从少数大型设备变成数量较多的小型设备：出故障时直接拉出和替换（swap-out failing
  switches），而不是现场排障（troubleshooting a failure in a live network）。
* 以前更看重设备的新功能提供能量，现在更看重遇到故障时网络的弹性（resilience）。

### 布线

CLOS 架构是富连接（richly connected）或称全连接（full mesh）网络，因此所需的布线
工作量大大增加。

### Inventory Management 更简单

设备都是一致的，没有特殊性，管理起来更简单。管理交换机就像管理 Linux 服务器一样。

### 网络自动化

便于自动化。

## 2.6 Some Best Practices for a Clos Network

### Use of Multiple Links Between Switches

### Use of Spines as Only a Connector

### Use of Chassis as a Spine Switch

## 2.7 服务器接入模型（Host Attach Models）

* 单接入（single-attach）
* 双接入（dual-attach）

<p align="center"><img src="/assets/img/cloud-native-dc-networking/2-7.png" width="60%" height="60%"></p>
<p align="center">Fig 2-7. Dual-attached host models</p>

## 2.8 Summary

a primary implication of Clos topology: the rise of network disaggregation.

# 3 Network Disaggregation（网络分解）

## 3.1 什么是 Network Disaggregation?

交换机不再是厂商提供的一体机，而是分解为各个软件和硬件部分，每个部分可以独立设计
、采购和升级。

<p align="center"><img src="/assets/img/cloud-native-dc-networking/3-1.png" width="40%" height="40%"></p>
<p align="center">Fig 3-1. High-level components of a network switch</p>

组装一台交换机就像组装一台 PC 机。

## 3.2 为什么 Network Disaggregation 很重要？

交换机分解**更多的是出于商业或业务考虑，而非技术**（a business model, not a
technical issue）。

* 控制成本。

    后期的运维成本比前期的采购成本高的多。

* 避免厂商锁定。
* 功能的标准化。

## 3.3 哪些方面使得 Network Disaggregation 如今成为可能？

1. 传统数据中心已经开始拖慢业务发展速度。
2. CLOS 架构减少了对特定功能的厂商设备的依赖，更多地依赖标准化的设备。
3. 专门设计和生产交换芯片（称为商用芯片）的出现，这些厂商只设计和生产芯片，不做成品交换机。

## 3.4 Difference in Network Operations with Disaggregation

## 3.5 Open Network Installer Environment

Facebook 发起了 Open Compute Project (OCP)。

在裸交换机上安装网络操作系统（NOS）：Open Network Installer Environment (ONIE)，类似于服务
器领域的 PXE。

为什么没有直接用 PXE？PXE 只支持 x86。ARM 和 PowerPC 用的 u-boot。

## 3.6 The Players in Network Disaggregation: Hardware

### Packet-Switching Silicon

VxLAN 的硬件支持最早出现在商用芯片（merchant silicon），而不是传统交换机厂商的芯片上。

芯片厂商：

1. Broadcom 是商用芯片领域的拓荒者和领导者，

    * Trident 系列：主要用在 Leaf
    * Tomahawk 系列：主要用在 Spine
    * Jericho 系统：主要用在 edge

2. Mellanox 是另一个有力竞争者，

    * Spectrum 系列：Leaf 和 Spine 都可以用。

3. Barefoot 是这个领域的新秀，Cisco 和 Arista 都有基于 barefoot 芯片的交换机。

4. 其他：Innovium, Marvell

芯片配置：

* 主流：单芯片 64x100G
* 顶配：单芯片 128x100G，例如 Tomahawk 3

### ODM 厂商

Edgecore, Quanta, Agema, and Celestica, Dell.

# 4 Network Operating System Choices

# 5 Routing Protocol Choices
