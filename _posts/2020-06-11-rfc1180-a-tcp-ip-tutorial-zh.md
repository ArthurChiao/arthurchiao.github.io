---
layout    : post
title     : "[译] RFC 1180：朴素 TCP/IP 教程（1991）"
date      : 2020-06-11
lastupdate: 2020-06-11
categories: tcp network
---

### 译者序

本文翻译自 1991 年的一份 RFC（1180）: [A TCP/IP Tutorial](https://tools.ietf.org/rfc/rfc1180.txt)。

本文虽距今将近 20 年，但内容并未过时，这不禁让人惊叹于 TCP/IP 协议栈生命力之强大
。要理解 1991 年在技术发展中处于什么样一个位置，下面的时间线可作参考：

* 1983：以太网协议第一版（IEEE 802.3）发布，速度 10 Mbps
* 1990：万维网（WWW）诞生
* 1991：**1 月，这份 RFC 发布**（本文）
* 1991：8 月，芬兰的一个大学生宣布自己在开发一个玩具性质的内核，后来这个内核正式命名为 Linux
* 1995：快速以太网（IEEE 802.3u）标准发布，速度 100 Mbps（前几年最普通、最常见的家用网线的带宽）
* 1998：澳大利亚的一个软件工程师开始开发一个叫 `iptables` 的网络工具
* 1999：千兆以太网（IEEE 802.3ab）标准发布，速度 1 Gbps（作为对比，现代数据中心
  的以太网都是`10/25/40` Gbps 接入，`100/400` Gbps 互联）

从今天的角度看，本文的一大特色是简洁易懂，深入问题本质，单凭这一点就已经远胜当前
网上的绝大部分 TCP/IP 入门教程。当然，简洁易懂的一个原因是，当时的网络就是这么简
单，没有今天各种精巧复杂的性能优化和眼花缭乱的虚拟化。但了解了网络的发展历史、理
解了物理网络的基本原理之后，再来看今天各种网络虚拟化的东西就会轻松很多。基于这个
原因，也推荐做底层的同学不要过于跟风新名词，有时间多读读（技术或更广义的）历史和
经典，技术考古很有趣。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

----

* TOC
{:toc}

以下是译文。

----

```
Network Working Group                                      T. Socolofsky
Request for Comments:  1180                                      C. Kale
                                                  Spider Systems Limited
                                                            January 1991
```

## 本备忘录状态（Status of this Memo）

本 RFC 是一份 TCP/IP 协议族教程，重点介绍**通过路由器将 IP 数据报从源主机转
发到目的主机的过程**。

本文并不是一份互连网标准（Internet standard）。

本文内容可以不受限制地传播。

# 1. 引言

TCP/IP 功能非常全面，但本文将只关注其核心技术（"bare bones" of TCP/IP technology
）之一。

本文不涉及 TCP/IP 的开发和资助历史、商业使用场景，以及与 ISO OSI 相比的发展前途
。此外，我们还省略了大量的技术细节。**除此之外的内容，则是与 TCP/IP 打交道的专业
人士至少要理解的**。这里所说的专业人士包括：

* 系统管理员（systems administrator）
* 系统程序员（systems programmer）
* 网络管理员（network manager）

本文的例子都源自 UNIX TCP/IP 环境，但其核心概念适用于所有版本的 TCP/IP 实现。

需要注意，**本备忘录的目的是解释（explanation），而非定义（definition）**。如果
对某个协议的规范（specification of a protocol）存疑，请参考定义相应的 RFC 标准（
actual standards defining RFC）。

# 2. TCP/IP 概览

“TCP/IP” 是一个非常宽泛的术语，通常指与 TCP/IP 协议相关的一切东西，其中包括，

1. 各种协议：例如 UDP、ARP 和 ICMP。
1. 各种应用：例如 TELNET, FTP, and rcp。
1. 甚至还包括网络媒介（network medium）。

描述这些内容的一个**更准确术语是 “internet technology”**（因特网技术）。**使用
internet technology 的网络称为 “internet”**（因特网）。

## 2.1 基本结构

要理解 TCP/IP 技术，必须先理解下面的逻辑结构：

```
                     ----------------------------
                     |    network applications  |
                     |                          |
                     |...  \ | /  ..  \ | /  ...|
                     |     -----      -----     |
                     |     |TCP|      |UDP|     |
                     |     -----      -----     |
                     |         \      /         |
                     |         --------         |
                     |         |  IP  |         |
                     |  -----  -*------         |
                     |  |ARP|   |               |
                     |  -----   |               |
                     |      \   |               |
                     |      ------              |
                     |      |ENET|              |
                     |      ---@--              |
                     ----------|-----------------
                               |
         ----------------------o---------
             Ethernet Cable

                  Figure 1.  Basic TCP/IP Network Node
```


这是 internet 上一台计算机内部**各协议层的逻辑图**（logical structure of the layered
protocols）。每一台能够通过 internet 技术进行通信的计算机都有这样的逻辑结构。**该逻
辑结构也决定了 internet 上的计算机的行为**。关于这张图有几点说明：

* 框（boxes）：表示数据经过计算机时，在这些地方处理数据
* 框之间的连线（lines）：表示数据的流经路径
* 最下面的横线：表示一根以太网网线（Ethernet cable）
* `o` 符号：收发器（transceiver）
* `*` 符号：IP 地址
* `@` 符号：以太网地址

**理解这张图是理解 internet 技术的基础**；本文将频繁引用此图。

## 2.2 术语

**流经 internet 的数据单元（a unit of data）该叫什么，和它处于协议栈的什么位置有直
接关系**。总的来说，如果：

* 位于以太网上：称为**以太帧**（Ethernet frame）
* 位于以太网驱动和 IP 模块之间：称为 **IP 包**
* 位于 IP 模块和 UDP 模块之间：称为 **UDP 数据报**（UDP datagram）
* 位于 IP 模块和 TCP 模块之间：称为 **TCP 段**（TCP segment），或者更宽泛地，称为一
  个传输层消息（transport message）
* 位于一个网络应用（network application）中：称为**应用消息**（application message）

这些定义并非完美。实际中不同文档的定义都有一些差异。更准确的定义参见
RFC 1122, section 1.3.3.

解释一下驱动和模块：

* **驱动**（driver）是一种直接和网络接口硬件（network interface hardware）通信的软件。
* **模块**（module）是和驱动、网络应用以及其他模块通信的软件。

本文接下来将用到以下术语：

* 驱动
* 模块
* 以太帧
* IP 包
* UDP 数据报
* TCP 消息
* 应用消息

## 2.3 数据流（Flow of Data）

来看图 1 中流经协议栈的数据流。

* 使用 TCP 的应用，数据在应用（application）和 TCP 模块之间传递。
* 使用 UDP 的应用，数据在应用（application）和 UDP 模块之间传递。
* FTP（File Transfer Protocol）是一个典型的 TCP 应用，其协议栈是 `FTP/TCP/IP/ENET`。
* SNMP（Simple Network Management Protocol）是一个使用 UDP 的应用，其协议栈是 `SNMP/UDP/IP/ENET`。

如图 2 所示，TCP 模块、UDP 模块和以太网驱动都是 **`N:1` 多路复用器**（n-to-1
multiplexers），即，它们将多路输入变成单路输出。另外，它们也是 **`1:N` 解复用器**
（1-to-n de-multiplexers），即，根据协议头中的类型（type）字段，将单路输入分解
为多路输出。

```
         1   2 3 ...   n                   1   2 3 ...   n
          \  |      /      |               \  | |      /       ^
           \ | |   /       |                \ | |     /        |
         -------------   flow              ----------------   flow
         |multiplexer|    of               |de-multiplexer|    of
         -------------   data              ----------------   data
              |            |                     |              |
              |            v                     |              |
              1                                  1

        Figure 2.  n-to-1 multiplexer and 1-to-n de-multiplexer
```

解复用（de-multiplexing）流程：

* **以太帧**从**网络**到达**以太网驱动**，然后将向上传递，要么给 ARP 模块，要么
  给 IP 模块。以太帧中的类型字段决定了该传给谁。
* 如果 IP 包到达 IP 模块，接下来将向上传递，要么给 TCP 模块，要么给 UDP 模块，IP
  头中的协议类型字段决定了该传给谁。
* 如果 UDP 数据报到达 UDP 模块，接下来将向上传递给网络应用（network application
  ），**UDP 头中的端口号**决定了该传给哪个应用。
* 同理，如果是 TCP 消息，将根据 TCP 头中的端口号决定传递给哪个网络应用。

多路复用（multiplexing）流程：

* 向下的多路复用（downwards multiplexing）流程比较简单，因为在每个位置只有一条向
  下的路；每个协议模块添加自己的头信息，这样包到达对端机器时就能被正确地解复用。
* 从应用发出的包，不管是 TCP 还是 UDP，在 IP 层都会统一为 IP 包，然后再传给下面
  的网络接口驱动发送出去。

虽然 internet 技术支持多种网络媒介，但以太网（Ethernet）是最常见的，因此本文所有例子
都将使用以太网。图 1 中的计算机只有一个以太网连接。每个以太网接口都有一个 **6 字
节的以太网地址**，位于以太网驱动的最低 6 字节，**该地址在以太网中是唯一的**。

此外，计算机还有一个 4 字节的 IP 地址，位于 IP 模块的最低 4 字节，该地址在 internet 
中唯一。

运行中的计算机永远知道自己的 IP 地址和以太网地址。

## 2.4 两个网络接口

图 3 中，一台计算机同时连接到 2 个独立的以太网。

```
                ----------------------------
                |    network applications  |
                |                          |
                |...  \ | /  ..  \ | /  ...|
                |     -----      -----     |
                |     |TCP|      |UDP|     |
                |     -----      -----     |
                |         \      /         |
                |         --------         |
                |         |  IP  |         |
                |  -----  -*----*-  -----  |
                |  |ARP|   |    |   |ARP|  |
                |  -----   |    |   -----  |
                |      \   |    |   /      |
                |      ------  ------      |
                |      |ENET|  |ENET|      |
                |      ---@--  ---@--      |
                ----------|-------|---------
                          |       |
                          |    ---o---------------------------
                          |             Ethernet Cable 2
           ---------------o----------
             Ethernet Cable 1

             Figure 3.  TCP/IP Network Node on 2 Ethernets
```

注意到，这台计算机有 2 个以太网地址和 2 个 IP 地址。

从这张图可以得出这样的结论：对于有多个物理网络接口的计算机，IP 模块是 `N:M` 多路
复用和 `M:N` 解复用。

```
         1   2 3 ...   n                   1   2 3 ...   n
          \  | |      /    |                \  | |      /       ^
           \ | |     /     |                 \ | |     /        |
         -------------   flow              ----------------   flow
         |multiplexer|    of               |de-multiplexer|    of
         -------------   data              ----------------   data
           / | |     \     |                 / | |     \        |
          /  | |      \    v                /  | |      \       |
         1   2 3 ...   m                   1   2 3 ...   m

        Figure 4.  n-to-m multiplexer and m-to-n de-multiplexer
```

对应多个网络接口（network interface）的 IP 模块，要比我们前面的例子更复杂，因
为它**能将数据从一个网络转发到另一个网络**（forward data onto the next network）。
从任何一个网络接口收到的数据可以在另一个接口发送出去，如图 5 所示。

```
                           TCP      UDP
                             \      /
                              \    /
                          --------------
                          |     IP     |
                          |            |
                          |    ---     |
                          |   /   \    |
                          |  /     v   |
                          --------------
                           /         \
                          /           \
                       data           data
                      comes in         goes out
                     here               here

            Figure 5.  Example of IP Forwarding a IP Packet
```

**将 IP 包从一个网络发送到另一个网络的过程，称为 “转发”**（forwarding）。

**主要任务是转发 IP 包的计算机**（computer），称为 **“IP 路由器”**（IP-router）。

从图 5 可以看出，转发 IP 包不会涉及 TCP 和 UDP 模块。因此某些 IP 路由器（
IP-router）的实现中都没有 TCP 和 UDP 模块。

## 2.5 多个物理网络的 IP 空间组成单个逻辑网络

IP 模块对 internet 的成功至关重要。

消息从协议栈经过时，每个模块或驱动都会加上相应层的头（header）。反之，当消息从低
层协议往上层应用走时，每个模块或驱动又会剥掉相应层的头。

IP 头包含 IP 地址，而**不同物理网络的所有 IP 地址**（组合起来），**能够形成单个逻
辑网络**（IP address which builds a single logical network from multiple
physical networks）。

**这种物理网络的互联正是 internet（互联网，音译 internet ）这个名字的由来**。一组互
联的物理网络限制了 IP 包的范围，这样的网络称为 “internet”（互联网， internet ）。

## 2.6 物理网络的独立性

IP 向网络应用隐藏了底层的物理硬件。如果你发明了一种新的物理网络，只需要开发一个
新驱动，将其通过 IP 连接到 internet 即可。

因此，网络应用无需做任何改动，不受底层硬件技术变化的影响。

## 2.7 互操作性

如果 **internet 上的两台计算机能通信**，我们就说它们能**互操作**（interoperate）
；如果某种 internet 技术实现地很好，我们就说它有**“互操作性”**（interoperability
）。

市面上的互操作性使得普通计算机用户从 internet 获益良多。一般来说，你**买了一台计算
机之后，这台计算机就将用于互操作**。如果计算机没有互操作性，并且也没有添加互操作
的能力，那它在市场上的竞争力就会非常小。

## 2.8 小结

有了以上背景，我们将回答以下问题：

1. 发送 IP 包时，如何**确定目的端的以太网地址**？
2. 如果有多个网络接口，发送 IP 包时如何知道**使用哪个接口**？
3. 一台计算机上的**客户端**（client）**如何访问到另一台计算机上的服务端**（server）？
4. 为什么会有 TCP 和 UDP 两种协议，只有 TCP 或 UDP 不行吗？
5. 有哪些网络应用（network applications）？

我们先回顾一下以太网相关的知识，再来回答这些问题。

# 3. 以太网（Ethernet）

本节简要回顾以太网技术。

一个以太网帧包含：

* 目的地址
* 源地址
* 类型
* 数据

以太网地址为 6 个字节。每个设备

* 都有自己的以太网地址
* 接收目的地址是自己的以太帧
* 所有设备还接收目的地址是 “FF-FF-FF-FF-FF-FF” 的以太帧，这个地址称为广播地址。

**以太网基于 CSMA/CD** (Carrier Sense and Multiple Access with Collision Detection)
技术。CSMA/CD 意味着

* 所有设备在同一介质上通信
* 在**任意时间只允许一个设备发送数据**，但它们**能同时接收数据**
* 如果有两台设备同时尝试发送，就会检测到发送冲突（transmit collision），这两个设
  备就必须等待一段随机（但很短）的时间，然后才能再次尝试发送。

## 3.1 类比：黑暗的屋子中多人交谈

以太网技术的一个很好的类比是：**一群人在一个很小的、全黑的房间里讨论问题**。在这
个类比中，物理介质是房间中**空气中的声波**（sound waves on air），对应以太网中的
同轴电缆中的**电信号**（electrical signals on a coaxial cable）。

* 有人说话时，**其他人都能听到**（Carrier Sense，**载波侦听**）。
* 房间中的**每个人都有同等说话的权利**（Multiple Access，**多路访问**）。
* 房间中的每个人都比较礼貌，**不会长时间地连续讲话**。如果谁不礼貌，就会
  被请出房间（例如，关闭他的网络）。
* **有人在说话时，其他人不能说话**。但如果两个人同时尝试说话，他们二人立即能分辨
  出来（Collision Detection，**冲突检测**）。
* 发现冲突后，他们会**等待一小会**，然后再次尝试发声。其他人如果想发言，会先倾听
  正在说话的人，等待他说完。
* 为避免混淆，**每个人都有唯一的名字**（唯一的以太网地址）。
* **每次说话时，人们会先说出对方的名字，然后是自己的名字，然后才是消息内容**（分
  别对应以太网目的和源地址），例如“你好 Jane，我是 Jack，... 巴拉巴拉 ...”。如果
  想和所有人说话，他可能会说 “所有人” （广播地址），例如 “你好所有人，我是
  Jack，... 巴拉巴拉 ...”。

# 4. ARP（地址解析协议）

发送 IP 包时，如何确定目的以太网地址呢？

ARP (Address Resolution Protocol，地址解析协议) 用于**将 IP 地址转换成以太网地址**
（translate IP addresses to Ethernet addresses）。

只会为出向（outgoing）IP 包执行这种转换，因为出向包才需要添加 IP 头和以太帧头。

## 4.1  ARP 表（ARP Table）

**转换是通过查表（table look-up）完成的**。

这个表称为 ARP 表，存储在内存中，每行表示一台计算机的信息，其中包括 IP 地址和以
太网地址。执行转换时，会去表中查询 IP 地址对应的以太网地址。下面是一张简化之后的
ARP 表：

```
                  ------------------------------------
                  |IP address       Ethernet address |
                  ------------------------------------
                  |223.1.2.1        08-00-39-00-2F-C3|
                  |223.1.2.3        08-00-5A-21-A7-22|
                  |223.1.2.4        08-00-10-99-AC-54|
                  ------------------------------------
                      TABLE 1.  Example ARP Table
```

人类可读格式的惯例是：IP 地址用十进制，并用小数点作为分隔符；以太网地址用十六进
制，并用短横岗或冒号作为分隔符。

**ARP 表是必需的**，因为 **IP 地址和以太网地址的分配是独立的，二者之间并没有算法
可以从一个计算出另一个**。

* IP 地址是网络管理员**根据计算机在 internet 上的位置选择**的。当计算机在
  internet 上**移动到另一个位置时，IP 地址必须要改**。
* 以太网地址是**制造商**根据其拿到的**以太网地址空间授权**，从这个空间中选择的。
  当以太网硬件接口板换掉时，以太网地址就会变化。

## 4.2 典型 ARP 转换场景

在网络应用（例如 TELNET）的正常运行过程中，应用将一段消息发给 TCP，TCP 会将其发
送给 IP 模块。应用、TCP 模块和 IP 模块都是知道目的 IP 地址的。

此时，**IP 包已经构建好**，可以送到以太网驱动，但在此之前，**必须先确定目的端的
以太网地址**。

因此，接下来会**根据目的 IP 查找 ARP 表**，获取目的以太网地址。

## 4.3  ARP 请求/响应

但 **ARP 表里的初始内容**是如何填充的呢？答案是：它们是 **ARP 按需自动填**的（
filled automatically by ARP on an "as-needed" basis）。

当 IP 不在 ARP 表中导致查询失败时，会做两件事情：

1. 发送一个 ARP 请求，目的以太网地址是**广播地址**，即，**向所有计算机发送这份请求**
2. **将待发送的 IP 包放入队列**

每台计算机的以太网接口都接收广播以太帧。以太网驱动检查帧中的**类型（Type）字段**
，发现属于 ARP 协议，因此**将帧交给 ARP 模块**。

ARP 请求带的消息是：**“如果你的 IP 地址就是我所请求的 IP 地址，请告诉我你的以太
网地址”**。一个 ARP 请求长这样：

```
                ---------------------------------------
                |Sender IP Address   223.1.2.1        |
                |Sender Enet Address 08-00-39-00-2F-C3|
                ---------------------------------------
                |Target IP Address   223.1.2.2        |
                |Target Enet Address <blank>          |
                ---------------------------------------
                     TABLE 2.  Example ARP Request
```

ARP 模块检查其中的 IP 地址字段，如果目标 IP 地址就是自己，它就会直接**向
源以太网地址发送一个 ARP 应答**。

ARP 应答所携带的消息是：**“目的 IP 地址就是我，下面是我的以太网地址”**。

ARP 应答的 sender/target 字段和 ARP 请求中的刚好相反，格式如下：

```
                ---------------------------------------
                |Sender IP Address   223.1.2.2        |
                |Sender Enet Address 08-00-28-00-38-A9|
                ---------------------------------------
                |Target IP Address   223.1.2.1        |
                |Target Enet Address 08-00-39-00-2F-C3|
                ---------------------------------------
                     TABLE 3.  Example ARP Response
```

源计算机收到这个应答之后，

1. 以太网驱动查看帧中的 Type 字段，判断是 ARP 协议
2. 将它交给 ARP 模块
3. ARP 模块检查包的内容，然后**将发送者的 IP 地址和以太网地址存到它的 ARP 表中**

更新之后的 ARP 表如下图所示：

```
                   ----------------------------------
                   |IP address     Ethernet address |
                   ----------------------------------
                   |223.1.2.1      08-00-39-00-2F-C3|
                   |223.1.2.2      08-00-28-00-38-A9|
                   |223.1.2.3      08-00-5A-21-A7-22|
                   |223.1.2.4      08-00-10-99-AC-54|
                   ----------------------------------
                   TABLE 4.  ARP Table after Response
```

## 4.4 ARP 场景续（Scenario Continued）

新规则此时已经自动加到 ARP 表，整个过程仅花费微秒级时间。

回忆前面过程，此时**待发送的 IP 包还缓存在队列里**。因此接下来会**再进行一次 ARP
查询**，得到目的 IP 地址对应的以太网地址，然后将以太帧在以太网上发送出去。

因此，对于发送计算机来说，**完整的步骤**是：

1. 发送一个 ARP 请求，目的以太网地址是广播地址，即，向所有计算机发送这份请求。
2. 将待发送的 IP 包（在等待相应的以太网地址）放入队列。
3. 收到 ARP 响应，其中包含了目的 IP 对应的以太网地址，将这条转换规则插入 ARP 表中。
4. 从队列中取出待发送的 IP 包，查询 ARP 表找到对应的以太网地址。
5. 将以太帧发送到以太网。

总结起来就是，**当 ARP 转换未命中**（missing）时，这个包就会被**放到队列**。然后
通过 ARP 请求和响应的方式**迅速地补充缺失的转换规则**，然后再将缓存的 IP 包发送出
去。

计算机中，**每个以太接口都有自己独立的 ARP 表**。如果目的计算机不存在，就收不到
ARP 应答，因此 ARP 表将为空。接下来 IP 模块就会丢弃到这个地址的 IP 包。
由以上原理可知，**上层协议无法区分下面两种情况**：

1. 以太网故障（a broken Ethernet）
2. 目的 IP 地址对应的机器不存在

某些 IP 和 ARP 模块的实现中，在等待 ARP 响应的期间，不会将待发送的 IP 包放到队
列。它们会直接丢弃这个 IP 包，然后由更上层的 TCP 模块或 UDP 网络应用来决定接下来
如何应对（recovery from the IP packet loss）。因对方式就是超时和重传。重传的消
息能成功地发送出去，因为前面被丢弃的那个包已经触发了 ARP 表的填充。

# 5.  互联网协议（Internet Protocol）

**IP 模块是 internet 技术的核心，而路由表（route table）是 IP 模块的核心**。
IP 模块利用这个内存中的表来判断每个 IP 包应该路由到哪里。路由表的内容由网络管理
员配置；如果配错会导致无法通信。

**理解路由表是如何工作的，就是在理解网络互连**（internetworking）。要管理和维护
IP 网络，必须得理解这个原理。

按下面的步骤来理解路由表比较容易：

1. 对路由有一个高层概览（overview of routing）
2. 学习 IP 网络和 IP 地址
3. 深入理解细节

## 5.1 直接路由（Direct Routing）

下图是一个由 3 台计算机 A、B、C 组成的微型 internet 。

* 每台计算机都有图 1 中所示的 TCP/IP 协议栈。
* 每台计算机的以太网接口都有自己的以太网地址。
* 网络管理员给每台计算机的 IP 接口都配置了 IP 地址
* 另外，管理员还为以太网分配了一个 IP 网络号（IP network number）

```
                          A      B      C
                          |      |      |
                        --o------o------o--
                        Ethernet 1
                        IP network "development"

                       Figure 6.  One IP Network
```

当 A 向 B 发送 IP 包时，IP 头中信息如下：

* 源 IP 地址：A 的 IP
* 源以太网地址：A 的以太网地址
* 目的 IP 地址：B 的 IP
* 目的以太网地址：B 的以太网地址

```
                ----------------------------------------
                |address            source  destination|
                ----------------------------------------
                |IP header          A       B          |
                |Ethernet header    A       B          |
                ----------------------------------------
       TABLE 5.  Addresses in an Ethernet frame for an IP packet
                              from A to B
```

对于这个简单的例子（将包从计算机 A 发送到计算机 B）来说，**以太层其实就足够了**
，IP 层并没有提供更多的服务，反而增加了开销：生成、发送和解析 IP 头都需要额外的
CPU 和带宽资源。

B 收到这个包后，检查目的 IP 地址是不是自己，然后根据头信息中的内容，将包交给合适
的上层协议。

**A 和 B 之间的这种通信，称为直接路由**（direct routing）。

## 5.2 间接路由（Indirect Routing）

相比图 6，下面的图 7 更接近真实 internet 。它

* 由 3 个以太网组成
* 以太网之上的 3 个 IP 网络（`development`、`accounting`、`factory`）由一个 IP
  路由器（计算机 D）连接到一起
* 每个 IP 网络中有 4 台计算机
* 每台计算机有自己的 IP 地址和以太网地址

```
          A      B      C      ----D----      E      F      G
          |      |      |      |   |   |      |      |      |
        --o------o------o------o-  |  -o------o------o------o--
        Ethernet 1                 |  Ethernet 2
        IP network "development"   |  IP network "accounting"
                                   |
                                   |
                                   |     H      I      J
                                   |     |      |      |
                                 --o-----o------o------o--
                                  Ethernet 3
                                  IP network "factory"

               Figure 7.  Three IP Networks; One internet
```

* 除了计算机 D 之外，其他计算机都有图 1 所示的 TCP/IP 协议栈。
* **计算机 D 是一台 IP 路由器**；它**连接到 3 个网络，因此有 3 个 IP 地址和 3 个以太网地址**。
* D 的 TCP/IP 协议栈和图 3 比较类似，但不同的是，D 有 3 个 ARP 模块和 3 个以太网驱动。
* 另外注意，D 只有一个 IP 模块。

网络管理员**为每个以太网分配一个唯一的编号，称为 IP 网络号**（IP network number
）。这张图中没有标出网络号，只标出了网络名。

当 A 向 B 发送 IP 包时，流程与前面的单网络例子是一样的。**同一 IP 网络内的任意计
算机之间通信，都遵循直接路由原则**。

**当 D 和 A 通信时，它们之间走直接路由。当 D 和 E 通信时，也是直接路由。D 和 H 通
信时，还是直接路由**。这是因为：**D 和这几台计算机（分别）都在同一个 IP 网络中**。

但是，当 A 与 IP 路由器连接的其他网络内的计算机通信时，它们之间不再是直接的（
direct）。必须要**由 D 将 IP 包转发给下一个 IP 网络**。这种通信称为**“间接的”**
（indirect）。

IP 包的路由过程是由 IP 模块处理的，对上层的 TCP、UDP 和网络应用是透明的。

**A 向 E 发送 IP 包时**，

* 源 IP 和源以太网地址是 A 的。
* **目的 IP 地址是 E 的 IP 地址**。
* **目的以太网地址是 D 的以太网地址**：因为 A 的 IP 模块需要将这个 IP 包发送给 D 做转发。

```
                ----------------------------------------
                |address            source  destination|
                ----------------------------------------
                |IP header          A       E          |
                |Ethernet header    A       D          |
                ----------------------------------------
       TABLE 6.  Addresses in an Ethernet frame for an IP packet
                         from A to E (before D)
```

D 的 IP 模块接收到这个 IP 后，**检查目的 IP 地址，发现这不是自己的 IP，然后会
直接将包发给 E**。

```
                ----------------------------------------
                |address            source  destination|
                ----------------------------------------
                |IP header          A       E          |
                |Ethernet header    D       E          |
                ----------------------------------------
       TABLE 7.  Addresses in an Ethernet frame for an IP packet
                         from A to E (after D)
```

因此，

* 直接路由：源 IP 和源以太网地址都是发送端的；目的 IP 地址和目的以太网地址都是接
  收端的。
* 间接路由：**目的 IP 地址和目的以太网地址不属于同一台机器**（do not pair up）。

这是一个非常简单的 internet 。实际的网络要比这个复杂地多，会涉及多个 IP 路由器和
不同类型的物理网络。如果网络管理员想将一个很大的以太网分割为几个较小的以太网，以
限制以太网广播流量，那可能会采用我们这种 internet 划分方式。

## 5.3 IP 模块的路由规则

至此，我们已经向大家介绍了路由过程发生了什么（what happens），但还没介绍它是如何
发生的（how it happens）。我们来看 **IP 模块所使用的规则，或者称为算法**：

1. 对于从上层进入 IP 层的出向（outgoing）包，IP 模块必须

    1. **判断是通过直接路由还是间接路由**发送
    2. **选择一个下层的网络接口**

   这些判断是**通过查询路由表**做出的。

2. 对于从下层的接口进入 IP 层的入向（incoming）包，

    1. IP 模块必须**判断是将包转发出去，还是送到上层**。
    1. 如果是转发，那接下来将这个包作为出向（outgoing）IP 包处理。

3. 当收到一个入向 IP 包时，**永远不会通过同一个接口再将它转发出去**。

这些判断都发生在**将包交给底层的网络接口之前**，以及**查询 ARP 表之前**。

## 5.4 IP 地址

网络管理员根据计算机所处的 IP 网络为它们分配 IP 地址。每个 IP 地址分为两部分：

* 网络号（IP network number）
* 计算机号（IP computer number）或主机号（IP network number）

表 1 中，对于 IP 地址 `223.1.2.1`，网络号为 `223.1.2`，主机号为 `1`。

一个 IP 地址的网络号和主机号分别是多少，是由 **IP 地址的前 4 比特决定的**。本文
所有例子中的 IP 地址都属于 C 类，这意味着，前 3 比特表示：

* 接下来的 21 比特表示网络号
* 剩下的 8 比特表示主机号

这意味着理论上有 2,097,152 个 C 类网络，每个网络中有 254 个主机。

IP 地址空间是由 NIC（Network Information Center，网络信息中心）管理的。所有连接
到**全球互联网**（the single world-wide Internet ）的 internet ，其网络号必须从
NIC 申请。即使你只是在搭建自己的 internet ，并且没有连接到全球互联网的打算，你还
是应该从 NIC 获取网络号。否则，假如某一天你的 internet 最终连接到了另一个
internet，将会产生混乱。

## 5.5 名字（主机名和网络名）

**人们习惯用名字（字符串）指代计算机，而不是用主机号（数字）**。例如，一台名为
`alpha` 的计算机可能有一个IP 地址 `223.1.2.1`。

* 对于较小的网络来说，这些 “名字 - IP 地址” 关联信息通常保存在每台**计算机的 `hosts` 文件**中。
* 对于较大的网络，这些数据保存在一个**服务器上**，在需要时跨网络去获取。

这些数据的格式如下：

```
   223.1.2.1     alpha
   223.1.2.2     beta
   223.1.2.3     gamma
   223.1.2.4     delta
   223.1.3.2     epsilon
   223.1.4.2     iota
```

第一列是 IP 地址，第二列是对应的主机名。

在大部分情况下，你可以为所有计算机维护一份完全一致的 `hosts` 文件。注意在上面的
例子中，主机 `delta` （即路由器 D）只有一个条目，但它实际上有 3 个 IP 地址。用 3
个中的任何一个 IP 地址都能访问到 `delta`。当 `delta` 收到一个 IP 包判断目标IP 地
址是不是自己时，它也会拿这个 IP 跟自己的 3 个 IP 地址分别进行比较。

IP 网络也有自己的名字。如果你有 3 个 IP 网络，你的 `networks` 文件可能长这样：

```
   223.1.2     development
   223.1.3     accounting
   223.1.4     factory
```

第一列是网络号，第二列是网络名。

从这个例子可以看出，

* `alpha` 是 `development` 网络中编号为 `1` 的计算机
* `beta` 是 `development` 网络中编号为 `2` 的计算机，以此类推。

你也可以说 `alpha` 是 `development.1`，`beta` 是 `development.2`，等等。

对于用户来说，以上 `hosts` 文件已经足够了；但网络管理员可能会将 `delta` 那一行
写成另一种格式：

```
   223.1.2.4     devnetrouter    delta
   223.1.3.1     facnetrouter
   223.1.4.1     accnetrouter
```

这三行分别给 `delta` 计算机的 3 个 IP 地址一个更有意义的名字。实际上，第一
行给了 `223.1.2.4` 这个 IP 地址两个名字；`delta` 和 `devnetrouter` 是 alias。
在实践中，`delta` 是计算机最常用的名字，其他 3 个名字都是在管理 IP 路由表的时候
才会用到。

这些文件都是网络管理命令和网络应用使用的，用于提供更有意义的名字。这些工作方式不
会影响 internet 运营，但会使后者更简单。

## 5.6  IP 路由表

当发送 IP 包时，IP 模块如何知道该使用下面的哪个网络接口？答案是：**从目标 IP 地
址中提取网络号，然后用网络号查询路由表**。

路由表中，每行表示一条路由。路由表的主要字段包括：

* IP 网络号
* 直接/间接标记
* 路由器 IP 地址
* 接口号（interface number）

对于每个出向 IP 包，IP 模块都会去查询路由表。

在大部分计算机上，都可以用 `route` 命令修改路由表。路由表的内容是由网络管理员修
改的，因为他们负责给计算机分配 IP 地址。

## 5.7 直接路由的若干细节

为解释路由表是如何工作的，我们需要针对前面的例子深入到更细节。

```
                        ---------         ---------
                        | alpha |         | beta  |
                        |    1  |         |  1    |
                        ---------         ---------
                             |               |
                     --------o---------------o-
                      Ethernet 1
                      IP network "development"

               Figure 8.  Close-up View of One IP Network
```

`alpha` 内的路由表长下面这样：

```
     --------------------------------------------------------------
     |network      direct/indirect flag  router   interface number|
     --------------------------------------------------------------
     |development  direct                <blank>  1               |
     --------------------------------------------------------------
                  TABLE 8.  Example Simple Route Table
```

在某些 UNIX 系统上，`netstat -r` 能够看到这个信息。在这个简单网络中，所有计算机
都有完全一样的路由表。

为使讨论方便，我们打印网络的数字表示（网络号），而不是网络名。如下图所示：

```
     --------------------------------------------------------------
     |network      direct/indirect flag  router   interface number|
     --------------------------------------------------------------
     |223.1.2      direct                <blank>  1               |
     --------------------------------------------------------------
           TABLE 9.  Example Simple Route Table with Numbers
```

## 5.8 直接路由的场景

假设 `alpha` 正在向 `beta` 发送 IP 包。IP 包位于 `alpha` 的 IP 模块中，目标 IP
地址是 `beta` 或 `223.1.2.2`。IP 模块从这个 IP 地址中提取出网络号部分，然后去路
由表的第一列中匹配。在这个例子中，会匹配到第一条规则。

这条路由规则中的信息显示：**`223.1.2` 网络中的计算机能够通过直接路由到达**（第二
列），只要将包**从接口 `1` 发送出去**（最后一列）就行了。因此，接下来会针对
`beta` 的 IP 地址发送一个 ARP 请求，获取其以太网地址，然后就通过接口 1 将 IP 包
发送给 `beta` 了。

如果应用试图向 `223.1.2` 之外的某个网络发包，IP 模块将无法在路由表中找到匹配的
路由规则。这些 IP 包将被丢弃。某些计算机会提示 “Network not reachable” 错误信息。

## 5.9 间接路由的若干细节

现在我们来看看间接路由的细节。

```
          ---------           ---------           ---------
          | alpha |           | delta |           |epsilon|
          |    1  |           |1  2  3|           |   1   |
          ---------           ---------           ---------
               |               |  |  |                |
       --------o---------------o- | -o----------------o--------
        Ethernet 1                |     Ethernet 2
        IP network "Development"  |     IP network "accounting"
                                  |
                                  |     --------
                                  |     | iota |
                                  |     |  1   |
                                  |     --------
                                  |        |
                                --o--------o--------
                                    Ethernet 3
                                    IP network "factory"

             Figure 9.  Close-up View of Three IP Networks
```

`alpha` 内的路由表如下：

```
 ---------------------------------------------------------------------
 |network      direct/indirect flag  router          interface number|
 ---------------------------------------------------------------------
 |development  direct                <blank>         1               |
 |accounting   indirect              devnetrouter    1               |
 |factory      indirect              devnetrouter    1               |
 ---------------------------------------------------------------------
                      TABLE 10.  Alpha Route Table
```

同样，为使讨论方便，我们打印网络号而不是网络名：

```
  --------------------------------------------------------------------
  |network      direct/indirect flag  router         interface number|
  --------------------------------------------------------------------
  |223.1.2      direct                <blank>        1               |
  |223.1.3      indirect              223.1.2.4      1               |
  |223.1.4      indirect              223.1.2.4      1               |
  --------------------------------------------------------------------
               TABLE 11.  Alpha Route Table with Numbers
```

在 `alpha` 的路由表中，路由器的 IP 地址是 **计算机 `delta` 连接 `development` 网
络的接口的 IP 地址**。

## 5.10 间接路由的场景

假设 `alpha` 正在向 `epsilon` 发送 IP 包。此时，IP 包位于 `alpha` 的 IP 模块中，目的
IP 地址是 `epsilon`（223.1.3.2）。

1. IP 模块从这个 IP 地址中提取出网络号部分（`223.1.3`），然后扫描路由表的第一列
   寻找匹配项。可以看出，匹配到了第二项。
2. 这条路由显示，位于 `223.1.3` 网络上的目标计算机能通过 IP 路由器 `devnetrouter` 到达。
3. `alpha` 的 IP 模块为 `devnetrouter` 的 IP 地址执行一次 ARP 查询，获取
   `devnetrouter` 对应的以太网地址。
4. 然后通过 `alpha` 的接口 1 将 IP 包直接发送出去。这个 IP 包仍然携带了
   `epsilon` 的 IP 地址（但携带了 IP 路由器的以太网地址）。
5. IP 包到达 `delta` 的 `development` 网络接口后，向上传递给 `delta` 的 IP 模块
   。`delta` 查看包的目的 IP 地址，发现不是自己的 IP，因此判断应该对这个 IP 包进
   行转发。
6. 接下来，`delta` 的 IP 模块从目的 IP 地址中提取出网络号（`223.1.3`），扫描路由
   表寻找匹配项。`delta` 的路由表如下：

    ```
     ----------------------------------------------------------------------
     |network      direct/indirect flag  router           interface number|
     ----------------------------------------------------------------------
     |development  direct                <blank>          1               |
     |factory      direct                <blank>          3               |
     |accounting   direct                <blank>          2               |
     ----------------------------------------------------------------------
                         TABLE 12.  Delta's Route Table
    ```

    显示网络号而不是网络名：
    
    ```
     ----------------------------------------------------------------------
     |network      direct/indirect flag  router           interface number|
     ----------------------------------------------------------------------
     |223.1.2      direct                <blank>          1               |
     |223.1.3      direct                <blank>          3               |
     |223.1.4      direct                <blank>          2               |
     ----------------------------------------------------------------------
                  TABLE 13.  Delta's Route Table with Numbers
    ```
    
    匹配到第二行。

7. `delta` 的 IP 模块接下来将包通过接口 3 直接发送给 `epsilon` 计算机。
  此时，IP 包的目的 IP 地址和目的以太网地址都是 `epsilon` 的。
8. IP 包到达 `epsilon` 后，传递给 `epsilon` 的 IP 模块。`epsilon` 检查包的目的 IP，
  发现是自己的，接下来将包传给更上层协议。

## 5.11 路由部分总结

当 IP 在很大的 internet 上传输时，可能要经过多个 IP 路由器才能到达最终目的地。这
条路径并不是由某个中心节点规定的，而是通过**查询路径上的每个路由器的路由表得到的**
（a result of consulting each of the routing tables used in the journey）。
**每台计算机只能决定下一跳（the next hop），并将包发往这下一跳**。

## 5.12 管理路由

在大型 internet 中，准确地维护所有计算机上的路由表是一项非常困难的工作；网络管理
员在不断针对各种需求变化修改网络的配置。路由表中出现错误会导致无法通信，并且排查
起来极其痛苦。

**保持网络配置的简单，是构建可靠 internet 的康庄大道**。例如，给以太网分配 IP 网
络的最直接方式就是：只为每个 internet 分配一个 IP 网络号。

某些协议和网络应用能协助我们排查问题。ICMP (Internet Control Message Protocol，
internet 控制消息协议) 能报告某些路由问题。对于小型网络，每台计算机上的路由表由
网络管理员配置。对于大型网络，网络管理员通过路由协议来在网络上分发路由，实现这些
配置步骤的自动化。

当将一台计算机从一个网络移动到另一个网络时，它的 IP 地址必须修改。当该计算机离开
原来的网络时，它的 IP 地址将变为无效的。这些变动都需要频繁的更新 `hosts` 文件。
即使对于中型网络，该文件也将变得难以维护。域名系统（Domain Name System，DNS）可
用于解决这个难题。

# 6. UDP（User Datagram Protocol）

UDP 是 IP 层之上的两种主要协议之一。它给用户的网络应用提供服务。使用 UDP 的网络
服务有：

* Network File System (NFS)
* Simple Network Management Protocol (SNMP)

这种服务并不仅仅是 IP 之上的一个接口那么简单。

UDP 是一个无连接的数据报传送服务（a connectionless datagram delivery service）。
本机 UDP 模块不与对端 UDP 模块维护端到端的连接；它仅仅是简单地将数据报发送到网络
上，以及从网络上接收进来的数据报。

UDP **在 IP 之上提供了两种价值**：

1. 基于端口号（port number），实现**应用之间的信息复用**（multiplexing of information）
2. 校验和，**校验数据的完整性**

## 6.1 端口（Ports）

一台计算机上的客户端（client）如何访问到另一台计算机上的服务端（server）？

**应用和 UDP 之间的通信路径是 UDP 端口**。这些端口是用数字表示的，从 0 开始。提
供服务的应用，会在特定于这个应用的端口上（specific port dedicated to that
service）等待接收消息进来。服务端会耐心地等待客户端的请求过来。

例如，名为 SNMP agent 的 SNMP 服务程序，永远在 161 端口等待。每台计算机上只能运
行一个 SNMP agent，因为每台计算机只有一个 UDP 161 端口。这个端口是 well known 的
；是由 internet 分配的数字；如果一个 SNMP 客户端想获取 SNMP 服务，它会向目标计算
机的 UDP 161 端口发送请求。

当应用通过 UDP 发送数据时，数据会作为一个整体（as a single unit）到达对端。例如
，如果一个应用在 UDP 端口上执行了 5 次写，对端应用会在 UDP 端口上执行 5 次读。
**每次写的大小和每次读的大小是匹配的**。

UDP **保留了应用定义的消息边界**（message boundary defined by the application）
。它不会将应用的两条消息合并，或者将一条消息做拆分。

## 6.2 校验和（Checksum）

计算机收到的 IP 包，如果 IP 头中的 `type` 字段是 `UDP` 类型，这个包会被 IP 模块
传递给 UDP 模块。

UDP 模块从 IP 模块收到 UDP 数据报之后，会检查 UDP 校验和（checksum）。如果
checksum 是 0，表示发送端没有计算校验和，可以忽略。因此，发送端可能生成也可能不
能生成校验和。如果通信双方的两个 UDP 模块之间，以太网是唯一的网络，那你可以选择
不需要校验和功能。但推荐打开校验和功能，因为在后面的路径中，路由表可能会指示将包
通过可靠性较差的媒介（less reliable media）发送。

如何校验和合法（或为 0），接下来会检查目的端口号；**如果有应用绑定到这个端口号，
就会为这个应用将这条消息缓存起来，等待应用的读取**。否则，该 UDP 数据报将被丢弃
。如果接收 UDP 数据报的速度比应用读取的速度还快，直到队列已经满了，那接下来的
UDP 数据将被丢弃。直到队列有空闲，否则接下来还会持续丢弃新收到的 UDP 报文。

# 7. TCP（Transmission Control Protocol）

TCP 提供了不同于 UDP 的另一种服务。TCP 提供了**面向连接的字节流**（
connection-oriented byte stream），而非无连接的数据报传送服务。TCP 保证传输，而
UDP 不保证。

对于需要保证传输可靠性、不想自己处理超时和重传的网络服务，使用 TCP 比较合适。两
个最典型的TCP 网络应用：

* File Transfer Protocol (FTP)
* TELNET

其他流行的 TCP 网络应用还包括：

* X-Window System
* `rcp` (remote copy)
* `r` 开头的一些命令

TCP 的强大能力也是有开销的：它需要更多的 CPU 和网络带宽。TCP 的内部机制要比 UDP
复杂的多。

与 UDP 类似，网络应用也是连接到 TCP 端口。一些 well-known 端口是为特殊的应用预留
的。例如，TELNET server 使用 23 端口。TELNET client 只需要连接指定计算机的 TCP
23 端口，就能找到上面的 TELENT 服务。

当应用开始使用 TCP 时，客户侧计算机上的 TCP 模块和服务侧计算机上的 TCP 模块开始互
信通信。这两个终端 TCP 模块（end-point TCP modules）包含了定义一条**虚拟电路**所
需的状态信息（state information that defines a virtual circuit）。这条虚拟电路会
消耗两边 TCP 端终的资源。虚拟电路是全双工的（full duplex）；数据可以同时双向传输
。应用将数据写到 TCP 端口，数据经过网络传输，最终被对端的应用读取。

**TCP 对字节流进行任意组包**（packetizes the byte stream at will）；它**不会保留
相邻写操作之间的边界**。例如，如果应用在 TCP 端口上写了 5 次，对端的应用可能会读
10 次才能将这份数据读完。或者，也可能 1 次就将数据读完了。**一侧的写大小和写次数，
和另一侧的读大小和读次数没有关系**。

TCP 是一个带超时和重传功能的滑动窗口协议（sliding window protocol）。发出去的包
必须被对端应答（be acknowledged）。应答可以附带在数据中（be piggybacked on data
）。两端的接收端都能能够对端的控制（flow control the far end），因此可以防止缓冲
区溢出（buffer overrun）。

就像所有的滑动窗口协议一样，TCP 协议有一个窗口大小（window size）。窗口大小决定
了在收到应答之前，最多可以发送的数据量（the amount of data that can be
transmitted before an acknowledgement is required）。对于 TCP 来说，这个窗口大小
的单位是字节，不是 TCP segments。

# 8. 网络应用

为什么会存在 TCP 和 UDP 两个协议，而不是只有一个？

它们用于不同的服务。大部分应用都只会用到其中之一。作为程序员，你应该根据你的需求
选择最合适的协议。

* 如果你需要一个可靠的流服务，TCP 可能最合适。
* 如果你需要一个数据报服务，UDP 可能最合适。
* 如果你需要跨长距离、高效的传输，TCP 可能最合适。
* 如果你需要跨快速网络、低延迟的传输，UDP 可能最合适。
* 如果你的需求无法落到以上类别，那“最合适”的选择还得再评估。

但是，协议的缺点可以在应用层做一些弥补。例如，

* 如果你选择的是 UDP 协议，并且你需要可靠性，那应用必须提供可靠性。
* 如果你选择的是 TCP 协议，并且你要求传输数据时有基本单位（record oriented
  service），那应用必须在字节流中插入标识符（markers）来识别记录。

现在有哪些网络应用呢？

太多了，无法一一列举。并且数量还在不断增加。其中一些应用在 internet 技术发展初期
就已经存在，例如 TELNET 和 FTP。其他一些相对比较新，例如 X-Windows 和 SNMMP。下
面分别介绍本文提到的几个网络应用。

## 8.1 TELNET

TELNET 在 TCP 之上提供了远程登录能力。其操作和外观（operation and appearance）与
通过电话交换机拨号连接看到的类似。在命令行上，用户输入 `telnet delta` 就会从计
算机 `delta` 收到一个登录提示符。

TELNET 工作良好；这是一个古老的应用，具有广泛的互操作性。在不同的操作系统上，通
常运行着不同版本的 TELNET 实现。例如，VAX/VMS 上可能是 TELNET client，UNIX
System V 上可能是 TELNET server。

## 8.2 FTP

File Transfer Protocol (FTP) 和 TELNET 一样古老，也基于 TCP，具有广泛的互操作性
。其操作和界面就好像 TELNET 到了一台远程计算机一样。但不同的是，不能敲任意命
令，而只能从一些预置的 FTP 命令中选择，例如列出文件目录命令。

FTP 命令使我们可以在不同计算机之间拷贝文件。

## 8.3 rsh

Remote shell (rsh or remsh) is one of an entire family of remote UNIX
style commands.  The UNIX copy command, cp, becomes rcp.  The UNIX
"who is logged in" command, who, becomes rwho.  The list continues
and is referred to collectively to as the "r" series commands or the
"r*" (r star) commands.

The r* commands mainly work between UNIX systems and are designed for
interaction between trusted hosts.  Little consideration is given to
security, but they provide a convenient user environment.

To execute the "cc file.c" command on a remote computer called delta,
type "rsh delta cc file.c".  To copy the "file.c" file to delta, type
"rcp file.c delta:".  To login to delta, type "rlogin delta", and if
you administered the computers in a certain way, you will not be
challenged with a password prompt.

## 8.4 NFS

Network File System，最早由 Sun Microsystems 公司开发，基于 UDP，是将 UNIX 文件
系统挂载到多个计算机上的优秀工具。无盘工作站（diskless workstation）能够访问它的
服务器的硬盘，就好像这个磁盘就插在工作站本机一样。大型机（mainframe）`alpha` 上
数据库的一个硬盘拷贝，也能够被大型机 `beta` 使用 —— 如果数据库的文件系统挂载到
`beta`。

NFS 给网络增加了显著负载，并且如果链路很慢，NFS 的性能将很差，但它的收益也是很大
的。NFS client 在内核实现，允许所有应用和命令使用以 NFS 方式挂载的磁盘，就好像是
本地磁盘一样。

## 8.5 SNMP

Simple Network Management Protocol (SNMP) 使用 UDP 协议，设计用于集中式网络管理
站（central network management stations）。

显然，能收集到的信息越多，网络管理员
就越能检测和诊断网络问题。集中式平台通过 SNMP 从网络中的计算机上收集这些数据。
SNMP 定义了数据的格式；而集中式平台或网络管理员负责解读（interpret）这些数据。

## 8.6 X-Window

The X Window System uses the X Window protocol on TCP to draw windows
on a workstation's bitmap display.  X Window is much more than a
utility for drawing windows; it is entire philosophy for designing a
user interface.

# 9. 其他信息

很多与 internet 相关的信息本文都没有涉及。如果读者在阅读本文之后，有兴趣进行更深
入的学习，推荐下面一些方向：

* 管理命令：`arp`, `route` 和 `netstat`
* ARP：添加永久（permanent）entry、发布新（publish）entry、time-out entry、ARP 欺骗（spoofing）
* IP 路由表：host entry, default gateway, subnets
* IP: time-to-live counter, fragmentation, ICMP
* RIP，路由环路
* 域名系统（DNS）

# 10. 参考文献

[1] Comer, D., "Internetworking with TCP/IP Principles, Protocols,
    and Architecture", Prentice Hall, Englewood Cliffs, New Jersey,
    U.S.A., 1988.

[2] Feinler, E., et al, DDN Protocol Handbook, Volume 2 and 3, DDN
    Network Information Center, SRI International, 333 Ravenswood
    Avenue, Room EJ291, Menlow Park, California, U.S.A., 1985.

[3] Spider Systems, Ltd., "Packets and Protocols", Spider Systems
    Ltd., Stanwell Street, Edinburgh, U.K. EH6 5NG, 1990.

# 11. 与其他 RFC 的关系

本文没有更新任何其他 RFC，也没有使任何其他 RFC 失效。

# 12. 安全方面的考虑

TCP/IP 协议族有一些安全方面需要考虑的问题。对一部分人来说，这非常重要；但对另一
部分人来说，可能无关紧要；要视具体的用户需求而定。

本文没有涉及这方面的内容，但如果你有兴趣学习，建议从 ARP-spoofing 开始学起，然后
按照 RFC 1122 中 “Security Considerations” 章节的指导获取更多信息。

# 13. 作者联系方式

```
   Theodore John Socolofsky
   Spider Systems Limited
   Spider Park
   Stanwell Street
   Edinburgh EH6 5NG
   United Kingdom

   Phone:
     from UK        031-554-9424
     from USA 011-44-31-554-9424
   Fax:
     from UK        031-554-0649
     from USA 011-44-31-554-0649

   EMail: TEDS@SPIDER.CO.UK


   Claudia Jeanne Kale
   12 Gosford Place
   Edinburgh EH6 4BJ
   United Kingdom

   Phone:
     from UK        031-554-7432
     from USA 011-44-31-554-7432

   EMail: CLAUDIAK@SPIDER.CO.UK
```
