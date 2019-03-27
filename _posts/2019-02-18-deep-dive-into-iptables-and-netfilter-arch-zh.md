---
layout: post
title:  "[译] 深入理解 iptables 和 netfilter 架构"
date:   2019-02-18
categories: iptables netfilter
---

### 译者序

本文翻译自2015年的一篇英文博客 [A Deep Dive into Iptables and Netfilter
Architecture](https://www.digitalocean.com/community/tutorials/a-deep-dive-into-iptables-and-netfilter-architecture)
。

这篇对 iptables 和 netfilter 的设计和原理介绍比较全面，美中不足的是没有那张
内核协议栈各 hook 点位置和 iptables 规则优先级的经典配图，这里补充如下（来自
[Wikipedia](https://upload.wikimedia.org/wikipedia/commons/3/37/Netfilter-packet-flow.svg)
）：

<p align="center"><img src="/assets/img/deep-dive-into-iptables-netfilter/Netfilter-packet-flow.svg" width="100%" height="100%"></p>

另外，本文只讲理论，而下面这篇则侧重实战（基于 iptables 做 NAT）：

1. [(译) NAT - 网络地址转换]({% link _posts/2019-02-17-nat-zh.md %})

可作为本文的补充阅读。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

### 前言

防火墙是保护服务器和基础设施安全的重要工具。在 Linux 生态系统中，`iptables` 是使
用很广泛的防火墙工具之一，它基于内核的包过滤框架（packet filtering framework）
`netfilter`。如果管理员或用户不了解这些系统的架构，那可能就无法创建出可靠的防火
墙策略，一方面是因为 iptables 的语法颇有挑战性，另外一方面是 netfilter 框架内部
相互交织而变得错综复杂。

本文将带领读者深入理解 `iptables` 框架，让那些需要创建防火墙策略的用户对它有一个
更全面的认识。我们会讨论 iptables 是如何与 netfilter 交互的，几个组件是如何组织
成**一个全面的过滤和矫正系统**（a comprehensive filtering and mangling system）的。

## 1 IPTables 和 Netfilter 是什么？

Linux 上最常用的防火墙工具是 iptables。iptables 与协议栈内有包过滤功能的hook 交
互来完成工作。这些内核 hook 构成了 netfilter 框架。

每个进入网络系统的包（接收或发送）在经过协议栈时都会触发这些 hook，程序
可以通过**注册 hook 函数**的方式在一些关键路径上处理网络流量。iptables 相关的内核模
块在这些 hook 点注册了处理函数，因此可以通过配置 iptables 规则来使得网络流量符合
防火墙规则。

## 2. Netfilter Hooks

netfilter 提供了 5 个 hook 点。包经过协议栈时会触发**内核模块注册在这里的处理函数**
。触发哪个 hook 取决于包的方向（是发送还是接收）、包的目的地址、以及包在上一个
hook 点是被丢弃还是拒绝等等。

下面几个 hook 是内核协议栈中已经定义好的：

* `NF_IP_PRE_ROUTING`: 接收到的包进入协议栈后立即触发此 hook，在进行任何路由判断
  （将包发往哪里）之前
* `NF_IP_LOCAL_IN`: 接收到的包经过路由判断，如果目的是本机，将触发此 hook
* `NF_IP_FORWARD`: 接收到的包经过路由判断，如果目的是其他机器，将触发此 hook
* `NF_IP_LOCAL_OUT`: 本机产生的准备发送的包，在进入协议栈后立即触发此 hook
* `NF_IP_POST_ROUTING`: 本机产生的准备发送的包或者转发的包，在经过路由判断之后，
  将触发此 hook

**注册处理函数时必须提供优先级**，以便 hook 触发时能按照
优先级高低调用处理函数。这使得**多个模块（或者同一内核模块的多个实例）可以在同一
hook 点注册，并且有确定的处理顺序**。内核模块会依次被调用，每次返回一个结果给
netfilter 框架，提示该对这个包做什么操作。

## 3 IPTables 表和链（Tables and Chains）

iptables 使用 table 来组织规则，根据**用来做什么类型的判断**（the type of
decisions they are used to make）标准，将规则分为不同table。例如，如果规则是处理
网络地址转换的，那会放到 `nat` table；如果是判断是否允许包继续向前，那可能会放到
`filter` table。

在每个 table 内部，规则被进一步组织成 chain，**内置的 chain 是由内置的 hook 触发
的**。chain 基本上能决定（basically determin）规则**何时**被匹配。

下面可以看出，内置的 chain 名字和 netfilter hook 名字是一一对应的：

* `PREROUTING`: 由 `NF_IP_PRE_ROUTING` hook 触发
* `INPUT`: 由 `NF_IP_LOCAL_IN` hook 触发
* `FORWARD`: 由 `NF_IP_FORWARD` hook 触发
* `OUTPUT`: 由 `NF_IP_LOCAL_OUT` hook 触发
* `POSTROUTING`: 由 `NF_IP_POST_ROUTING` hook 触发

chain 使管理员可以控制在**包的传输路径上哪个点**（where in a packet's delivery
path）应用策略。因为每个 table 有多个 chain，因此一个 table 可以在处理过程中的多
个地方施加影响。**特定类型的规则只在协议栈的特定点有意义，因此并不是每个table 都
会在内核的每个 hook 注册 chain**。

内核一共只有 5 个 netfilter hook，因此不同 table 的 chain 最终都是注册到这几个点
。例如，有三个 table 有 `PRETOUTING` chain。当这些 chain 注册到对应的
`NF_IP_PRE_ROUTING` hook 点时，它们需要指定优先级，应该依次调用哪个 table 的
`PRETOUTING` chain，优先级从高到低。我们一会就会看到 chain 的优先级问题。

## 4. table 种类

先来看看 iptables 提供的 table 类型。这些 table 是按规则类型区分的。

### 4.1 Filter Table

`filter` table 是最常用的 table 之一，用于**判断是否允许一个包通过**。

在防火墙领域，这通常称作“过滤”包（"filtering" packets）。这个 table 提供了防火墙
的一些常见功能。

### 4.2 NAT Table

`nat` table 用于实现网络地址转换规则。

当包进入协议栈的时候，这些规则决定是否以及如何修改包的源/目的地址，以改变包被
路由时的行为。`nat` table 通常用于将包路由到无法直接访问的网络。

### 4.3 Mangle Table

`mangle` （修正）table 用于**修改包的 IP 头**。

例如，可以修改包的 TTL，增加或减少包可以经过的跳数。

这个 table 还可以对包打**只在内核内有效的**“标记”（internal kernel "mark"），后
续的 table 或工具处理的时候可以用到这些标记。标记不会修改包本身，只是在包的内核
表示上做标记。

### 4.4 Raw Table

**iptables 防火墙是有状态的**：对每个包进行判断的时候是**依赖已经判断过的包**。

建立在 netfilter 之上的连接跟踪（connection tracking）特性**使得 iptables 将包
看作已有的连接或会话的一部分**，而不是一个由独立、不相关的包组成的流。连接跟踪逻
辑在包到达网络接口之后很快就应用了。

`raw` table 定义的功能非常有限，其**唯一目的就是提供一个让包绕过连接跟踪的框架**。

### 4.5 Security Table

`security` table 的作用是给包打上 SELinux 标记，以此影响 SELinux 或其他可以解读
SELinux 安全上下文的系统处理包的行为。这些标记可以基于单个包，也可以基于连接。

## 5 每种 table 实现的 chain

前面已经分别讨论了 table 和 chain，接下来看每个 table 里各有哪些 chain。另外，我
们还将讨论注册到同一 hook 的不同 chain 的优先级问题。例如，如果三个 table 都有
`PRETOUTING` chain，那应该按照什么顺序调用它们呢？

下面的表格展示了 table 和 chain 的关系。横向是 table， 纵向是 chain，Y 表示 这个
table 里面有这个 chain。例如，第二行表示 `raw` table 有`PRETOUTING` 和 `OUTPUT` 两
个 chain。具体到每列，从上倒下的顺序就是netfilter hook 触发的时候，（对应
table 的）chain 被调用的顺序。

有几点需要说明一下。在下面的图中，`nat` table 被细分成了 `DNAT` （修改目的地址）
和 `SNAT`（修改源地址），以更方便地展示他们的优先级。另外，我们添加了路由决策点
和连接跟踪点，以使得整个过程更完整全面：

| Tables/Chains | PREROUTING | INPUT | FORWARD | OUTPUT | POSTROUTING |
|:--|:--|:--|:--|:--|:--|
| (路由判断)    |            |       |         |   Y    |             |
| **raw**       |     Y      |       |         |   Y    |             |
| (连接跟踪）   |     Y      |       |         |   Y    |             |
| **mangle**    |     Y      |   Y   |    Y    |   Y    |      Y      |
| **nat (DNAT)**|     Y      |       |         |   Y    |             |
| (路由判断)    |     Y      |       |         |   Y    |             |
| **filter**    |            |   Y   |    Y    |   Y    |             |
| **security**  |            |   Y   |    Y    |   Y    |             |
| **nat (SNAT)**|            |   Y   |         |   Y    |      Y      |

**当一个包触发 netfilter hook 时，处理过程将沿着列从上向下执行。** 触发哪个 hook
（列）和包的方向（ingress/egress）、路由判断、过滤条件等相关。

特定事件会导致 table 的 chain 被跳过。例如，只有每个连接的第一个包会去匹配 NAT
规则，对这个包的动作会应用于此连接后面的所有包。到这个连接的应答包会被自动应用反
方向的 NAT 规则。

### Chain 遍历优先级

假设服务器知道如何路由数据包，而且防火墙允许数据包传输，下面就是不同场景下包的游
走流程：

* 收到的、目的是本机的包：`PRETOUTING` -> `INPUT`
* 收到的、目的是其他主机的包：`PRETOUTING` -> `FORWARD` -> `POSTROUTING`
* 本地产生的包：`OUTPUT` -> `POSTROUTING`

**综合前面讨论的 table 顺序问题，我们可以看到对于一个收到的、目的是本机的包**：
首先依次经过 `PRETOUTING` chain 上面的 `raw`、`mangle`、`nat` table；然后依次经
过`INPUT` chain 的 `mangle`、`filter`、`security`、`nat` table，然后才会到达本机
的某个 socket。

## 6 IPTables 规则

规则放置在特定 table 的特定 chain 里面。当 chain 被调用的时候，包会依次匹配chain
里面的规则。每条规则都有一个匹配部分和一个动作部分。

### 6.1 匹配

规则的匹配部分指定了一些条件，包必须满足这些条件才会和相应的将要执行的动作（“
target”）进行关联。

匹配系统非常灵活，还可以通过 iptables extension大大扩展其功能。规则可以匹配**协
议类型、目的或源地址、目的或源端口、目的或源网段、接收或发送的接口（网卡）、协议
头、连接状态**等等条件。这些综合起来，能够组合成非常复杂的规则来区分不同的网络流
量。

### 6.2 目标

包符合某种规则的条件而触发的动作（action）叫做目标（target）。目标分为两种类型：

* **终止目标**（terminating targets）：这种 target 会终止 chain 的匹配，将控制权
  转移回 netfilter hook。根据返回值的不同，hook 或者将包丢弃，或者允许包进行下一
  阶段的处理
* **非终止目标**（non-terminating targets）：非终止目标执行动作，然后继续 chain
  的执行。虽然每个 chain 最终都会回到一个终止目标，但是在这之前，可以执行任意多
  个非终止目标

每个规则可以跳转到哪个 target 依上下文而定，例如，table 和 chain 可能会设置
target 可用或不可用。规则里激活的 extensions 和匹配条件也影响 target 的可用性。

## 7 跳转到用户自定义 chain

这里要介绍一种特殊的非终止目标：跳转目标（jump target）。jump target 是跳转到其
他 chain 继续处理的动作。我们已经讨论了很多内置的 chain，它们和调用它们的
netfilter hook 紧密联系在一起。然而，iptables 也支持管理员创建他们自己的用于管理
目的的 chain。

向用户自定义 chain 添加规则和向内置的 chain 添加规则的方式是相同的。**不同的地方
在于，用户定义的 chain 只能通过从另一个规则跳转（jump）到它，因为它们没有注册到
netfilter hook**。

用户定义的 chain 可以看作是对调用它的 chain 的扩展。例如，用户定义的 chain 在结
束的时候，可以返回 netfilter hook，也可以继续跳转到其他自定义 chain。

这种设计使框架具有强大的分支功能，使得管理员可以组织更大更复杂的网络规则。

## 8 IPTables 和连接跟踪

在讨论 `raw` table 和 匹配连接状态的时候，我们介绍了构建在 netfilter 之上的连
接跟踪系统。连接跟踪系统使得 iptables 基于连接上下文而不是单个包来做出规则判
断，给 iptables 提供了有状态操作的功能。

连接跟踪在包进入协议栈之后很快（very soon）就开始工作了。在给包分配连接之前所做
的工作非常少，只有检查 `raw` table 和一些基本的完整性检查。

跟踪系统将包和已有的连接进行比较，如果包所属的连接已经存在就更新连接状态，否则就
创建一个新连接。如果 `raw` table 的某个 chain 对包标记为目标是 `NOTRACK`，那这
个包会跳过连接跟踪系统。

### 连接的状态

连接跟踪系统中的连接状态有：

* `NEW`：如果到达的包关连不到任何已有的连接，但包是合法的，就为这个包创建一个新连接。对
  面向连接的（connection-aware）的协议例如 TCP 以及非面向连接的（connectionless
  ）的协议例如 UDP 都适用

* `ESTABLISHED`：当一个连接收到应答方向的合法包时，状态从 `NEW` 变成
  `ESTABLISHED`。对 TCP 这个合法包其实就是 `SYN/ACK` 包；对 UDP 和 ICMP 是源和目
  的 IP 与原包相反的包

* `RELATED`：包不属于已有的连接，但是和已有的连接有一定关系。这可能是辅助连接（
  helper connection），例如 FTP 数据传输连接，或者是其他协议试图建立连接时的
  ICMP 应答包

* `INVALID`：包不属于已有连接，并且因为某些原因不能用来创建一个新连接，例如无法
  识别、无法路由等等

* `UNTRACKED`：如果在 `raw` table 中标记为目标是 `UNTRACKED`，这个包将不会进入连
  接跟踪系统

* `SNAT`：包的源地址被 NAT 修改之后会进入的虚拟状态。连接跟踪系统据此在收到
  反向包时对地址做反向转换

* `DNAT`：包的目的地址被 NAT 修改之后会进入的虚拟状态。连接跟踪系统据此在收到
  反向包时对地址做反向转换

这些状态可以定位到连接生命周期内部，管理员可以编写出更加细粒度、适用范围更大、更
安全的规则。

## 9 总结

netfilter 包过滤框架和 iptables 防火墙是 Linux 服务器上大部分防火墙解决方案的基
础。netfilter 的内核 hook 和协议栈足够紧密，提供了包经过系统时的强大控制功能。
iptables 防火墙基于这些功能提供了一个灵活的、可扩展的、将策略需求转化到内核的方
法。理解了这些不同部分是如何联系到一起的，就可以使用它们控制和保护你的的服务器环
境。

想了解更多 iptables 使用方式，参考这个[教程
](https://www.digitalocean.com/community/tutorials/how-to-choose-an-effective-firewall-policy-to-secure-your-servers)
。
