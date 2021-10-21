---
layout    : post
title     : "[译] NAT 穿透是如何工作的：技术原理及企业级实践（Tailscale, 2020）"
date      : 2021-10-21
lastupdate: 2021-10-21
categories: nat
---

### 译者序

本文翻译自 2020 年的一篇英文博客：
[How NAT traversal works](https://tailscale.com/blog/how-nat-traversal-works/)。

设想这样一个问题：在北京和上海各有一台**<mark>局域网的机器</mark>**（例如一台是家里的台式机，一
台是连接到星巴克 WiFi 的笔记本），二者都是私网 IP 地址，但可以访问公网，
**<mark>如何让这两台机器通信呢？</mark>**

既然二者都能访问公网，那最简单的方式当然是在公网上架设一个中继服务器：
两台机器分别连接到中继服务，后者完成双向转发。这种方式显然有很大的性能开销，而
且中继服务器很容易成为瓶颈。

有没有办法不用中继，让**<mark>两台机器直接通信</mark>**呢？

如果有一定的网络和协议基础，就会明白这事儿是可能的。Tailscale
的这篇**<mark>史诗级长文</mark>**由浅入深地展示了这种“可能”，如果完全实现本文所
介绍的技术，你将得到一个企业级的 NAT/防火墙穿透工具。
此外，如作者所说，**<mark>去中心化软件</mark>**领域中的许多有趣想法，简化之后其实都变成了
**<mark>跨过公网（互联网）实现端到端直连</mark>** 这一问题，因此本文的意义并不仅限于 NAT 穿透本身。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

在前一篇文章 [How Tailscale Works](https://tailscale.com/blog/how-tailscale-works/) 中，
我们已经用较长篇幅介绍了 Tailscale 是如何工作的。但其中并没有详细描述我们是
**<mark>如何穿透 NAT 设备，从而实现终端设备直连的</mark>** —— 不管这些终端之间
有什么设备（防火墙、NAT 等），以及有多少设备。本文试图补足这一内容。

# 1 引言

## 1.1 背景：IPv4 地址短缺，引入 NAT

全球 IPv4 地址早已不够用，因此人们发明了 NAT（网络地址转换）来缓解这个问题。

简单来说，大部分机器都使用**<mark>私有 IP 地址</mark>**，如果它们需要访问公网服务，那么，

* 出向流量：需要经过一台 NAT 设备，它会对流量进行 SNAT，将私有 srcIP+Port 转
  换成 NAT 设备的公网 IP+Port（这样应答包才能回来），然后再将包发出去；
* 应答流量（入向）：到达 NAT 设备后进行相反的转换，然后再转发给客户端。

整个过程对双方透明。

> 更多关于 NAT 的内容，可参考 [(译) NAT - 网络地址转换（2016）]({% link _posts/2019-02-17-nat-zh.md %})。
> 译注。

以上是本文所讨论问题的**<mark>基本背景</mark>**。

## 1.2 需求：两台经过 NAT 的机器建立点对点连接

在以上所描述的 NAT 背景下，我们从最简单的问题开始：如何在两台经过 NAT 的机器之间建立
**<mark>点对点连接</mark>**（直连）。如下图所示：

<p align="center"><img src="/assets/img/nat-traversal/nat-intro.png" width="70%" height="70%"></p>

直接用机器的 IP 互连显然是不行的，因为它们都是私有 IP（例如 `192.168.1.x`）。
在 Tailscale 中，我们会建立一个 **<mark>WireGuard® 隧道</mark>** 来解决这个问题 ——
但这并不是太重要，因为我们将**<mark>过去几代人努力</mark>**都整合到了一个工具集，
**<mark>这些技术广泛适用于各种场景</mark>**。例如，

1. <a href="https://webrtc.org/"><mark>WebRTC</mark></a> 使用这些技术在浏览器之间完成 peer-to-peer 语音、视频和数据传输，
2. **<mark>VoIP 电话和一些视频游戏</mark>**也使用类似机制，虽然不是所有情况下都很成功。

接下来，本文将**<mark>在一般意义上讨论这些技术</mark>**，并在合适的地方拿
Tailscale 和其他一些东西作为例子。

## 1.3 方案：NAT 穿透

### 1.3.1 两个必备前提：UDP + 能直接控制 socket

如果想**<mark>设计自己的协议来实现 NAT 穿透</mark>**，那必须满足以下两个条件：

1. **<mark>协议应该基于 UDP</mark>**。

    理论上用 TCP 也能实现，但它会给本已相当复杂的问题再增加一层复杂性，
    甚至还需要定制化内核 —— 取决于你想实现到什么程度。本文接下来都将关注在 UDP 上。

    如果考虑 TCP 是想在 NAT 穿透时获得**<mark>面向流的连接</mark>**（
    stream-oriented connection），可以考虑用 **<mark>QUIC</mark>** 来替代，它构
    建在 UDP 之上，因此我们能将关注点放在 UDP NAT 穿透，而仍然能获得一个
    很好的流协议（stream protocol）。

2. 对收发包的 **<mark>socket 有直接控制权</mark>**。

    例如，从经验上来说，无法基于某个现有的网络库实现 NAT 穿透，因为我们
    **<mark>必须在使用的“主要”协议之外，发送和接收额外的数据包</mark>**。

    某些协议（例如  WebRTC）将 NAT 穿透与其他部分紧密集成。但如果你在构建自己的协议，
    **<mark>建议将 NAT 穿透作为一个独立实体，与主协议并行运行</mark>**，二者仅
    仅是共享 socket 的关系，如下图所示，这将带来很大帮助：

    <p align="center"><img src="/assets/img/nat-traversal/nat-deep-integration.png" width="70%" height="70%"></p>

### 1.3.2 保底方式：中继

在某些场景中，直接访问 socket 这一条件可能很难满足。

退而求其次的一个方式是设置一个 local proxy（本地代理），主协议与这个 proxy 通信
，后者来完成 NAT 穿透，将包中继（relay）给对端。这种方式增加了一个额外的间接层
，但好处是：

1. 仍然能获得 NAT 穿透，
2. **<mark>不需要对已有的应用程序做任何改动</mark>**。

## 1.4 挑战：有状态防火墙和 NAT 设备

有了以上铺垫，下面就从最基本的原则开始，一步步看如何实现一个企业级的 NAT 穿透方案。

我们的**<mark>目标</mark>**是：**<mark>在两个设备之间通过 UDP 实现双向通信</mark>**，
有了这个基础，上层的其他协议（WireGuard, QUIC, WebRTC 等）就能做一些更酷的事情。

但即便这个看似最基本的功能，在实现上也要解决**<mark>两个障碍</mark>**：

1. 有状态防火墙
2. NAT 设备

# 2 穿透防火墙

有状态防火墙是以上两个问题中相对比较容易解决的。实际上，**<mark>大部分 NAT 设备都自带了一个有状态防火墙</mark>**，
因此要解决第二个问题，必须先解决有第一个问题。

有状态防火墙具体有很多种类型，有些你可能见过：

* Windows Defender firewall
* Ubuntu's ufw (using iptables/nftables)
* BSD/macOS `pf`
* AWS Security Groups（**<mark>安全组</mark>**）

## 2.1 有状态防火墙

### 2.1.1 默认行为（策略）

以上防火墙的配置都是很灵活的，但大部分配置默认都是如下行为：

1. **<mark>允许所有出向连接</mark>**（allows all "outbound" connections）
2. **<mark>禁止所有入向连接</mark>**（blocks all "inbound" connections）

可能有少量例外规则，例如 allowing inbound SSH。

### 2.1.2 如何区分入向和出向包

连接（connection）和方向（direction）都是协议设计者头脑中的概念，到了
**<mark>物理传输层，每个连接都是双向的</mark>**；允许所有的包双向传输。
那**<mark>防火墙是如何区分哪些是入向包、哪些是出向包的呢</mark>**？
这就要回到**<mark>“有状态”（stateful）</mark>**这三个字了：有状态防火墙会记录它
看到的每个包，当收到下一个包时，会利用这些信息（状态）来判断应该做什么。

对 UDP 来说，规则很简单：如果防火墙之前看到过一个出向包（outbound），就会允许
相应的入向包（inbound）通过，以下图为例：

<p align="center"><img src="/assets/img/nat-traversal/nat-firewalls-1a.png" width="70%" height="70%"></p>

笔记本电脑中自带了一个防火墙，当该防火墙看到从这台机器出去的
`2.2.2.2:1234 -> 5.5.5.5:5678` 包时，就会记录一下：`5.5.5.5:5678 -> 2.2.2.2:1234` 入向包应该放行。
**<mark>这里的逻辑</mark>**是：我们信任的世界（即笔记本）想主动与 `5.5.5.5:5678` 通信，因此应该放行（allow）其回包路径。

> 某些**<mark>非常</mark>**宽松的防火墙只要看到有从 `2.2.2.2:1234` 出去的包，就
> 会允许所有从外部进入 `2.2.2.2:1234` 的流量。这种防火墙对我们的 NAT 穿透来说非
> 常友好，但已经越来越少见了。

## 2.2 防火墙朝向（face-off）与穿透方案

### 2.2.1 防火墙朝向相同

#### 场景特点：服务端 IP 可直接访问

在 NAT 穿透场景中，以上默认规则对 UDP 流量的影响不大 —— 只要**<mark>路径上所有防火墙的“朝向”是一样的</mark>**。
一般来说，从内网访问公网上的某个服务器都属于这种情况。

我们唯一的要求是：**<mark>连接必须是由防火墙后面的机器发起的</mark>**。这是因为
在它主动和别人通信之前，没人能主动和它通信，如下图所示：

#### 穿透方案：客户端直连服务端，或 hub-and-spoke 拓扑

<p align="center"><img src="/assets/img/nat-traversal/nat-firewalls-2.png" width="80%" height="80%"></p>

但上图是**<mark>假设了</mark>**通信双方中，其中一端**<mark>（服务端）是能直接访问到的</mark>**。
在 VPN 场景中，这就形成了所谓的 **<mark>hub-and-spoke 拓扑</mark>**：中心的 hub 没有任何防火墙策略，谁都能访问到；
防火墙后面的 spokes 连接到 hub。如下图所示：

<p align="center"><img src="/assets/img/nat-traversal/nat-firewalls-3.png" width="70%" height="70%"></p>

### 2.2.2 防火墙朝向不同

#### 场景特点：服务端 IP 不可直接访问

但如果两个“客户端”想直连，以上方式就不行了，此时两边的防火墙相向而立，如下图所示：

<p align="center"><img src="/assets/img/nat-traversal/nat-firewalls-4.png" width="70%" height="70%"></p>

根据前面的讨论，这种情况意味着：**<mark>两边要同时发起连接请求</mark>**，但也意味着
两边都无法发起有效请求，因为对方先发起请求才能在它的防火墙上打开一条缝让我们进去！
如何破解这个问题呢？一种方式是**<mark>让用户重新配置一边或两边的防火墙，打开一个端口</mark>**，
允许对方的流量进来。

1. 这显然对用户不友好，在像 Tailscale 这样的 mesh 网络中的扩展性也不好，在 mesh
   网络中，我们假设对端会以一定的粒度在公网上移动。
2. 此外，在很多情况下用户也没有防火墙的控制权限：例如在咖啡馆或机场中，连接的路
   由器是不受你控制的（否则你可能就有麻烦了）。

因此，我们需要寻找一种不用重新配置防火墙的方式。

#### 穿透方案：两边同时主动建连，在本地防火墙为对方打开一个洞

解决的思路还是先重新审视前面提到的有状态防火墙规则：

* 对于 UDP，其规则（逻辑）是：**<mark>包必须先出去才能进来</mark>**（packets must flow out before packets can flow back in）。
* 注意，这里除了要满足包的 IP 和端口要匹配这一条件之外，**<mark>并没有要求包必须是相关的</mark>**（related）。
  换句话说，只要某些包带着正确的源和目的地址出去了，**<mark>任何看起来像是响应的包都会被防火墙放进来</mark>** ——
  即使对端根本没收到你发出去的包。

因此，要穿透这些有状态防火墙，我们只需要**<mark>共享一些信息：让两端提前知道对方使用的 ip:port</mark>**：

* 手动静态配置是一种方式，但显然扩展性不好；
* 我们开发了一个 <a href="https://tailscale.com/blog/how-tailscale-works/#the-control-plane-key-exchange-and-coordination">coordination server</a>，
  以灵活、安全的方式来同步 `ip:port` 信息。

有了对方的 `ip:port` 信息之后，两端开始给对方发送 UDP 包。在这个过程中，我们预
料到某些包将会被丢弃。因此，双方**<mark>必须要接受某些包会丢失的事实</mark>**，
因此如果是重要信息，你必须自己准备好重传。对 UDP 来说丢包是可接受的，但这里尤其需要接受。

来看一下具体建连（穿透）过程：

1. 如图所示，笔记本出去的第一包，`2.2.2.2:1234 -> 7.7.7.7:5678`，穿过 Windows
   Defender 防火墙进入到公网。

    <p align="center"><img src="/assets/img/nat-traversal/nat-firewalls-5a.png" width="70%" height="70%"></p>

    对方的防火墙会将这个包拦截掉，因为它没有 `7.7.7.7:5678 -> 2.2.2.2:1234` 的流量记录。
    但另一方面，Windows Defender 此时已经记录了出向连接，因此会允许 `7.7.7.7:5678 -> 2.2.2.2:1234` 的应答包进来。

2. 接着，第一个 `7.7.7.7:5678 -> 2.2.2.2:1234` 穿过它自己的防火墙到达公网。

    <p align="center"><img src="/assets/img/nat-traversal/nat-firewalls-5b.png" width="70%" height="70%"></p>

    到达客户端侧时，Windows Defender **<mark>认为这是刚才出向包的应答包，因此就放行它进入了！</mark>**
    此外，右侧的防火墙此时也记录了：`2.2.2.2:1234 -> 7.7.7.7:5678` 的包应该放行。

3. 笔记本收到服务器发来的包之后，发送一个包作为应答。这个包穿过 Windows Defender 防火墙
  和服务端防火墙（因为这是对服务端发送的包的应答包），达到服务端。

    <p align="center"><img src="/assets/img/nat-traversal/nat-firewalls-5c.png" width="70%" height="70%"></p>

成功！这样我们就建立了一个**<mark>穿透两个相向防火墙</mark>**的双向通信连接。
而初看之下，这项任务似乎是不可能完成的。

## 2.3 关于穿透防火墙的一些思考

穿透防火墙并非永远这么轻松，有时会受一些第三方系统的间接影响，需要仔细处理。
那穿透防火墙需要注意什么呢？重要的一点是：**<mark>通信双方必须几乎同时发起通信</mark>**，
这样才能在路径上的防火墙打开一条缝，而且两端还都是活着的。

### 2.3.1 双向主动建连：旁路信道

如何实现“同时”呢？一种方式是两端不断重试，但显然这种方式很浪费资源。假如双方都
知道何时开始建连就好了。

* 这听上去是**<mark>鸡生蛋蛋生鸡的问题</mark>**了：**<mark>双方想要通信，必须先提前通个信</mark>**。
* 但实际上，我们可以通过**<mark>旁路信道</mark>**（side channel）来达到这个目的
  ，并且这个旁路信道并不需要很 fancy：它可以有几秒钟的延迟、只需要传送几 KB 的
  信息，因此即使是一个配置非常低的虚拟机，也能为几千台机器提供这样的旁路通信服务。

    * 在遥远的过去，我曾用  XMPP 聊天消息作为旁路，效果非常不错。
    * 另一个例子是 WebRTC，它需要你提供一个自己的“信令信道”（signalling channel，
      这个词也暗示了 WebRTC 的 IP telephony ancestry），并将其配置到 WebRTC API。
    * 在 Tailscale，我们的协调服务器（coordination server）和 DERP (Detour
      Encrypted Routing Protocol) 服务器集群是我们的旁路信道。

### 2.3.2 非活跃连接被防火墙清理

有状态防火墙内存通常比较有限，因此会定期清理不活跃的连接（UDP 常见的是 30s），
因此要保持连接 alive 的话需要定期通信，否则就会被防火墙关闭，为避免这个问题，
我们，

1. 要么定期向对方发包来 keepalive，
2. 要么有某种带外方式来按需重建连接。

### 2.3.3 问题都解决了？不，挑战刚刚开始

对于防火墙穿透来说，
我们**<mark>并不需要关心路径上有几堵墙</mark>** —— 只要它们是有状态防火墙且允许出
向连接，这种同时发包（simultaneous transmission）机制就能穿透任意多层防火墙。
这一点对我们来说非常友好，因为只需要实现一个逻辑，然后能适用于任何地方了。

...对吗？

其实，**<mark>不完全对</mark>**。这个机制有效的前提是：我们能**<mark>提前知道对方的 ip:port</mark>**。
而这就涉及到了我们今天的主题：NAT，它会使前面我们刚获得的一点满足感顿时消失。

下面，**<mark>进入本文正题</mark>**。

# 3 NAT 的本质

## 3.1 NAT 设备与有状态防火墙

可以认为 NAT 设备是一个**<mark>增强版的有状态防火墙</mark>**，虽然它的增强功能
对于本文场景来说并不受欢迎：除了前面提到的有状态拦截/放行功能之外，它们还会在数据包经过时修改这些包。

## 3.2 NAT 穿透与 SNAT/DNAT

具体来说，NAT 设备能完成某种类型的网络地址转换，例如，替换源或目的 IP 地址或端口。

* **<mark>讨论连接问题和 NAT 穿透问题时</mark>**，我们**<mark>只会受 source NAT —— SNAT 的影响</mark>**。
* DNAT 不会影响 NAT 穿透。

## 3.3 SNAT 的意义：解决 IPv4 地址短缺问题

SNAT 最常见的使用场景是**<mark>将很多设备连接到公网，而只使用少数几个公网 IP</mark>**。
例如对于消费级路由器，会将所有设备的（私有） IP 地址映射为**<mark>单个</mark>**连接到公网的 IP 地址。

这种方式存在的意义是：我们有远多于可用公网 IP 数量的设备需要连接到公网，（至少
对 IPv4 来说如此，IPv6 的情况后面会讨论）。NAT 使多个设备能共享同一 IP 地址，因
此即使面临 IPv4 地址短缺的问题，我们仍然能不断扩张互联网的规模。

## 3.4 SNAT 过程：以家用路由器为例

假设你的笔记本连接到家里的 WiFi，下面看一下它连接到公网某个服务器时的情形：

1. 笔记本发送 UDP packet `192.168.0.20:1234 -> 7.7.7.7:5678`。

    <p align="center"><img src="/assets/img/nat-traversal/nat-overview-1.png" width="70%" height="70%"></p>

    这一步就好像笔记本有一个公网 IP 一样，但源地址 `192.168.0.20` 是私有地址，
    只能出现在私有网络，公网不认，收到这样的包时它不知道如何应答。

2. 家用路由器出场，执行 SNAT。

    包经过路由器时，路由器发现这是一个它没有见过的新会话（session）。
    它知道 `192.168.0.20` 是私有 IP，公网无法给这样的地址回包，但它有办法解决：

    1. 在它**<mark>自己的公网 IP 上挑一个可用的 UDP 端口</mark>**，例如 `2.2.2.2:4242`，
    2. 然后创建一个 *NAT mapping*：`192.168.0.20:1234` `<-->` `2.2.2.2:4242`，
    3. 然后将包发到公网，此时源地址变成了 `2.2.2.2:4242` 而不是原来的 `192.168.0.20:1234`。因此服务端看到的是转换之后地址，
    4. 接下来，每个能匹配到这条映射规则的包，都会被路由器改写 IP 和 端口。

    <p align="center"><img src="/assets/img/nat-traversal/nat-overview-2.png" width="70%" height="70%"></p>

3. 反向路径是类似的，路由器会执行相反的地址转换，将 `2.2.2.2:4242` 变回
  `192.168.0.20:1234`。对于笔记本来说，它根本感知不知道这正反两次变换过程。

这里是拿家用路由器作为例子，但**<mark>办公网的原理是一样的</mark>**。不同之处在
于，办公网的 NAT 可能有多台设备组成（高可用、容量等目的），而且它们有不止一个公
网 IP 地址可用，因此在选择可用的公网 `ip:port` 来做映射时，选择空间更大，能支持
更多客户端。

<p align="center"><img src="/assets/img/nat-traversal/nat-overview-3.png" width="70%" height="70%"></p>

## 3.5 SNAT 给穿透带来的挑战

现在我们遇到了与前面有状态防火墙类似的情况，但这次是 NAT 设备：**<mark>通信双方
不知道对方的 ip:port 是什么</mark>**，因此**<mark>无法主动建连</mark>**，如下图所示：

<p align="center"><img src="/assets/img/nat-traversal/nat-stun-1.png" width="70%" height="70%"></p>

但这次比有状态防火墙更糟糕，严格来说，**<mark>在双方发包之前，根本无法确定（自己及对方的）ip:port 信息</mark>**，因为
**<mark>只有出向包经过路由器之后才会产生 NAT mapping</mark>**（即，可以被对方连接的 `ip:port` 信息）。

因此我们又回到了与防火墙遇到的问题，并且情况更糟糕：**<mark>双方都需要主动和对
方建连，但又不知道对方的公网地址是多少</mark>**，只有当对方先说话之后，我们才能拿到它的地址信息。

如何破解以上死锁呢？这就轮到 [STUN](https://en.wikipedia.org/wiki/STUN) 登场了。

# 4 穿透 “NAT+防火墙”：STUN (Session Traversal Utilities for NAT) 协议

[STUN](https://en.wikipedia.org/wiki/STUN) 
既是一些对 NAT 设备行为的详细研究，也是一种协助 NAT 穿透的协议。本文主要关注 STUN 协议。

## 4.1 STUN 原理

**<mark>STUN 基于一个简单的观察</mark>**：从一个会被 NAT 的客户端访问公网服务器时，
服务器看到的是 **<mark>NAT 设备的公网 ip:port 地址</mark>**，而非该
**<mark>客户端的局域网 ip:port 地址</mark>**。

也就是说，服务器能告诉客户端**<mark>它看到的客户端的 ip:port 是什么</mark>**。
因此，只要将这个信息以某种方式告诉通信对端（peer），后者就知道该和哪个地址建连了！
这样就又**<mark>简化为前面的防火墙穿透问题了</mark>**。

本质上这就是 **<mark>STUN 协议的工作原理</mark>**，如下图所示：

* 笔记本向 STUN 服务器发送一个请求：“从你的角度看，我的地址什么？”
* STUN 服务器返回一个响应：“我看到你的 UDP 包是从这个地址来的：`ip:port`”。

<p align="center"><img src="/assets/img/nat-traversal/nat-stun-2.png" width="70%" height="70%"></p>

> The STUN protocol has a bunch more stuff in it — there's a way of
> obfuscating the `ip:port` in the response to stop really broken NATs
> from mangling the packet's payload, and a whole authentication
> mechanism that only really gets used by TURN and ICE, sibling
> protocols to STUN that we'll talk about in a bit. We can ignore all of
> that stuff for address discovery.

## 4.2 为什么 NAT 穿透逻辑和主协议要共享同一个 socket

理解了 STUN 原理，也就能理解为什么我们在文章开头说，如果
**<mark>要实现自己的 NAT 穿透逻辑和主协议，就必须让二者共享同一个 socket</mark>**：

1. 每个 socket 在 NAT 设备上都对应一个映射关系（私网地址 -> 公网地址），
1. STUN 服务器只是**<mark>辅助</mark>**穿透的基础设施，
1. 与 STUN 服务器通信之后，在 NAT 及防火墙设备上打开了一个连接，允许入向包进来（回忆前面内容，
  **<mark>只要目的地址对，UDP 包就能进来</mark>**，不管这些包是不是从 STUN 服务器来的），
1. 因此，接下来只要将这个地址告诉我们的通信对端（peer），让它往这个地址发包，就能实现穿透了。

## 4.3 STUN 的问题：不能穿透所有 NAT 设备（例如企业级 NAT 网关）

有了 STUN，我们的**<mark>穿透目的似乎已经实现了</mark>**：每台机器都通过 STUN
来获取自己的私网 socket 对应的公网 `ip:port`，然后把这个信息告诉对端，然后两端
同时发起穿透防火墙的尝试，后面的过程就和上一节介绍的防火墙穿透一样了，**<mark>对吗</mark>**？

答案是：**<mark>看情况</mark>**。某些情况下确实如此，但有些情况下却不行。通常来说，

* 对于大部分**<mark>家用路由器场景</mark>**，这种方式是没问题的；
* 但对于一些**<mark>企业级 NAT 网关</mark>**来说，这种方式无法奏效。

NAT 设备的说明书上越强调它的安全性，STUN 方式失败的可能性就越高。（但注意，从实际意义上来说，
**<mark>NAT 设备在任何方面都并不会增强网络的安全性</mark>**，但这不是本文重点，因此不展开。）

## 4.4 重新审视 STUN 的前提

再次审视前面**<mark>关于 STUN 的假设</mark>**：当 STUN 服务器告诉客户端在公网看来它的地址是
`2.2.2.2:4242` 时，那所有目的地址是 `2.2.2.2:4242` 的包就都能穿透防火墙到达该客户端。

这也正是问题所在：**<mark>这一点并不总是成立</mark>**。

* 某些 NAT 设备的行为与我们假设的一致，它们的有状态防火墙组件只要看到有客户端自己
  发起的出向包，就会允许相应的入向包进入；因此只要利用 STUN 功能，再加上两端同时
  发起防火墙穿透，就能把连接打通；

    > in theory, there are also NAT devices that are super relaxed, and
    > don't ship with stateful firewall stuff at all. In those, you don't
    > even need simultaneous transmission, the STUN request gives you an
    > internet `ip:port` that anyone can connect to with no further
    > ceremony. If such devices do still exist, they're increasingly rare.

* 另外一些 NAT 设备就要困难很多了，它会**<mark>针对每个目的地址来生成一条相应的映射关系</mark>**。
  在这样的设备上，如果我们用相同的 socket 来分别发送数据包到
  `5.5.5.5:1234` and `7.7.7.7:2345`，我们就会得到 `2.2.2.2` 上的两个不同的端口，每个目的地址对应一个。
  如果反向包的端口用的不对，包就无法通过防火墙。如下图所示：

    <p align="center"><img src="/assets/img/nat-traversal/nat-stun-3.png" width="70%" height="70%"></p>

# 5 中场补课：NAT 正式术语

知道 NAT 设备的行为并不是完全一样之后，我们来引入一些正式术语。

## 5.1 早期术语

如果之前接触过 NAT 穿透，可能会听说过下面这些名词：

* "Full Cone"
* "Restricted Cone"
* "Port-Restricted Cone"
* "Symmetric" NATs

这些都是 NAT 穿透领域的早期术语。

但其实这些术语**<mark>相当让人困惑</mark>**。我每次都要
查一下 Restricted Cone NAT 是什么意思。从实际经验来看，我并不是唯一对此感到困惑的人。
例如，如今互联网上将 "easy" NAT 归类为 Full Cone，而实际上它们更应该归类为
Port-Restricted Cone。

## 5.2 近期研究与新术语

最近的一些研究和 RFC 已经提出了一些更准确的术语。

* 首先，它们明确了如下事实：**<mark>NAT 设备的行为差异表现在多个维度</mark>**，
  而并非只有早期研究中所说的 “cone” 这一个维度，因此**<mark>基于 “cone” 来划分类别并不是很有帮助</mark>**。
* 其次，新研究和新术语能**<mark>更准确地描述 NAT 在做什么</mark>**。

前面提到的所谓 **<mark>"easy" 和 "hard" NAT，只在一个维度有不同</mark>**：NAT 映射是否考虑到目的地址信息。
<a href="https://tools.ietf.org/html/rfc4787">RFC 4787</a> 中，

* 将 **<mark>easy NAT 及其变种</mark>**称为 “Endpoint-Independent Mapping” (**<mark>EIM，终点无关的映射</mark>**)

    但是，从**<mark>“命名很难”</mark>**这一程序员界的伟大传统来说，EIM 这个词其实
    也并不是 100% 准确，因为这种 NAT 仍然依赖 endpoint，只不过依赖的是源 endpoint：每个 source
    `ip:port` 对应一个映射 —— 否则你的包就会和别人的包混在一起，导致混乱。

    严格来说，EIM 应该称为 "Destination Endpoint Independent Mapping" (DEIM?)，
    但这个名字太拗口了，而且按照惯例，Endpoint 永远指的是 Destination Endpoint。

* 将 **<mark>hard NAT 以及变种</mark>**称为 “Endpoint-Dependent Mapping”（**<mark>EDM，终点相关的映射</mark>**） 。

  EDM 中还有一个子类型，依据是只根据 dst_ip 做映射，还是根据 dst_ip + dst_port 做映射。
  对于 NAT 穿透来说，这种区分对来说是一样的：它们**<mark>都会导致 STUN 方式不可用</mark>**。

## 5.3 老的 cone 类型划分

你可能会有疑问：根据是否依赖 endpoint 这一条件，只能组合出两种可能，那为什么传
统分类中会有四种 cone 类型呢？答案是 **<mark>cone 包含了两个正交维度的 NAT 行为</mark>**：

* **<mark>NAT 映射行为</mark>**：前面已经介绍过了，
* **<mark>有状态防火墙行为</mark>**：与前者类似，也是分为与 endpoint 相关还是无关两种类型。

因此最终组合如下：

<p align="center">NAT Cone Types</p>

<table>
  <thead>
    <tr>
      <th></th>
      <th><strong>Endpoint 无关 NAT mapping</strong></th>
      <th><strong>Endpoint 相关 NAT mapping (all types)</strong></th>
    </tr>
  </thead>

  <tbody>
    <tr>
      <td><strong>Endpoint 无关防火墙</strong></td>
      <td>Full Cone NAT</td>
      <td>N/A*</td>
    </tr>
    <tr>
      <td><strong>Endpoint 相关防火墙 (dst. IP only)</strong></td>
      <td>Restricted Cone NAT</td>
      <td>N/A*</td>
    </tr>
    <tr>
      <td><strong>Endpoint 相关防火墙 (dst. IP+port)</strong></td>
      <td>Port-Restricted Cone NAT</td>
      <td>Symmetric NAT</td>
    </tr>
  </tbody>
</table>

分解到这种程度之后就可以看出，**<mark>cone 类型对 NAT 穿透场景来说并没有什么意义</mark>**。
我们关心的只有一点：是否是 Symmetric —— 换句话说，一个 NAT 设备是 EIM 还是 EDM 类型的。

## 5.4 针对 NAT 穿透场景：简化 NAT 分类

以上讨论可知，虽然理解防火墙的具体行为很重要，但对于编写 NAT 穿透代码来说，这一点并不重要。
我们的**<mark>两端同时发包</mark>**方式（simultaneous transmission trick）能
**<mark>有效穿透以上三种类型的防火墙</mark>**。在真实场景中，
我们主要在处理的是 IP-and-port endpoint-dependent 防火墙。

因此，对于实际 NAT 穿透实现，我们可以将以上分类简化成：

<table>
  <thead>
    <tr>
      <th></th>
      <th>Endpoint-Independent NAT mapping</th>
      <th>Endpoint-Dependent NAT mapping (dst. IP only)</th>
    </tr>
  </thead>

  <tbody>
    <tr>
      <td><strong>Firewall is yes</strong></td>
      <td>Easy NAT</td>
      <td>Hard NAT</td>
    </tr>
  </tbody>
</table>

## 5.5 更多 NAT 规范（RFC）

想了解更多新的 NAT 术语，可参考

* RFC <a href="https://tools.ietf.org/html/rfc4787">4787</a> (NAT Behavioral Requirements for UDP)
* RFC <a href="https://tools.ietf.org/html/rfc5382">5382</a> (for TCP)
* RFC <a href="https://tools.ietf.org/html/rfc5508">5508</a> (for ICMP)

如果自己实现 NAT，那应该（should）遵循这些 RFC 的规范，这样才能使你的 NAT
行为符合业界惯例，与其他厂商的设备或软件良好兼容。

# 6 穿透 NAT+防火墙：STUN 不可用时，fallback 到中继模式

## 6.1 问题回顾与保底方式（中继）

补完基础知识（尤其是定义了什么是 hard NAT）之后，回到我们的 NAT 穿透主题。

* 第 1~4 节已经解决了 STUN 和防火墙穿透的问题，
* 但 **<mark>hard NAT 对我们来说是个大问题</mark>**，只要路径上出现一个这种设备，前面的方案就行不通了。

准备放弃了吗？
这才**<mark>进入 NAT 真正有挑战的部分</mark>**：如果已经试过了前面介绍的所有方式
仍然不能穿透，我们该怎么办呢？

* 实际上，确实有很多 NAT 实现在这种情况下都会选择放弃，向用户报一个**<mark>“无法连接”</mark>**之类的错误。
* 但对我们来说，这么快就放弃显然是不可接受的 —— 解决不了连通性问题，Tailscale 就没有存在的意义。

我们的保底解决方式是：创建一个**<mark>中继连接</mark>**（relay）实现双方的无障碍地通信。
但是，中继方式性能不是很差吗？这要看具体情况：

* 如果能直连，那显然没必要用中继方式；
* 但如果无法直连，而中继路径又非常接近双方直连的真实路径，并且带宽足够大，那中
  继方式并不会明显降低通信质量。延迟肯定会增加一点，带宽会占用一些，但
  **<mark>相比完全连接不上，还是更能让用户接受的</mark>**。

不过要注意：我们只有在无法直连时才会选择中继方式。实际场景中，

1. 对于大部分网络，我们都能通过前面介绍的方式实现直连，
2. 剩下的长尾用中继方式来解决，并不算一个很糟的方式。

此外，某些网络会阻止 NAT 穿透，其影响比这种 hard NAT 大多了。例如，我们观察到
UC Berkeley guest WiFi 禁止除 DNS 流量之外的所有 outbound UDP 流量。
不管用什么 NAT 黑科技，都无法绕过这个拦截。因此我们终归还是需要一些可靠的 fallback 机制。

## 6.2 中继协议：TURN、DERP

有多种中继实现方式。

1. **<mark>TURN</mark>** (Traversal Using Relays around NAT)：经典方式，核心理念是

    1. **<mark>用户</mark>**（人）先去公网上的 TURN 服务器认证，成功后后者会告诉你：“我已经为你分配了 ip:port，接下来将为你中继流量”，
    2. 然后将这个 ip:port 地址告诉对方，让它去连接这个地址，接下去就是非常简单的客户端/服务器通信模型了。

    Tailscale 并不使用 TURN。这种协议**<mark>用起来并不是很好</mark>**，而且与 STUN 不同，
    它没有真正的交互性，因为互联网上并没有公开的 TURN 服务器。

2. DERP (Detoured Encrypted Routing Protocol)

    这是我们创建的一个协议，<a href="https://tailscale.com/blog/how-tailscale-works/#encrypted-tcp-relays-derp">DERP</a>，

    1. 它是一个**<mark>通用目的包中继协议，运行在 HTTP 之上</mark>**，而大部分网络都是允许 HTTP 通信的。
    2. 它根据目的公钥（destination's public key）来中继加密的流量（encrypted payloads）。

    前面也简单提到过，DERP 既是我们在 NAT 穿透失败时的保底通信方式（此时的角色
    与 TURN 类似），也是在其他一些场景下帮助我们完成 NAT 穿透的旁路信道。
    换句话说，它既是我们的保底方式，也是有更好的穿透链路时，帮助我们进行连接升
    级（upgrade to a peer-to-peer connection）的基础设施。

## 6.3 小结

有了“中继”这种保底方式之后，我们穿透的成功率大大增加了。
如果此时不再阅读本文接下来的内容，而是把上面介绍的穿透方式都实现了，我预计：

* 90% 的情况下，你都能实现直连穿透；
* 剩下的 10% 里，用中继方式能穿透**<mark>一些</mark>**（some）；

这已经算是一个“足够好”的穿透实现了。

# 7 穿透 NAT+防火墙：企业级改进

如果你并不满足于“足够好”，那我们可以做的事情还有很多！

本节将介绍一些五花八门的 tricks，在某些特殊场景下会帮到我们。单独使用这项技术都
无法解决 NAT 穿透问题，但将它们巧妙地组合起来，我们能更加接近 100% 的穿透成功率。

## 7.1 穿透 hard NAT：暴力端口扫描

回忆 hard NAT 中遇到的问题，如下图所示，关键问题是：easy NAT 不知道该往 hard NAT 方的哪个
`ip:port` 发包。

<p align="center"><img src="/assets/img/nat-traversal/nat-birthday-attack-1.png" width="70%" height="70%"></p>

但**<mark>必须</mark>**要往正确的 `ip:port` 发包，才能穿透防火墙，实现双向互通。
怎么办呢？

1. 首先，我们能知道 hard NAT 的**<mark>一些</mark>** `ip:port`，因为我们有 STUN 服务器。

    这里先假设我们获得的这些 IP 地址都是正确的（这一点并不总是成立，但这里先这么假
    设。而实际上，大部分情况下这一点都是成立的，如果对此有兴趣，可以参考
    REQ-2 in <a href="https://tools.ietf.org/html/rfc4787">RFC 4787</a>）。

2. IP 地址确定了，剩下的就是端口了。总共有 65535 中可能，我们能**<mark>遍历这个端口范围</mark>**吗？

    如果发包速度是 100 packets/s，那最坏情况下，需要 **<mark>10 分钟</mark>**来找到正确的端口。
    还是那句话，这虽然不是最优的，但总比连不上好。

    这很像是端口扫描（事实上，确实是），实际中可能会触发对方的网络入侵检测软件。

## 7.2 基于生日悖论改进暴力扫描：hard side 多开端口 + easy side 随机探测

利用 <a href="https://en.wikipedia.org/wiki/Birthday_problem">birthday paradox</a> 算法，
我们能对端口扫描进行改进。

* 上一节的基本前提是：hard side 只打开一个端口，然后 easy side 暴力扫描 65535 个端口来寻找这个端口；
* 这里的改进是：在 hard size 开多个端口，例如 256 个（即同时打开 256 个 socket，目的地址都是 easy side 的 `ip:port`），
  然后 easy side 随机探测这边的端口。

这里省去算法的数学模型，如果你对实现干兴趣，可以看看我写的 
<a href="https://github.com/danderson/nat-birthday-paradox">python calculator</a>。
计算过程是“经典”生日悖论的一个小变种。
下面是随着 easy side random probe 次数（假设 hard size 256 个端口）的变化，两边打开的端口有重合（即通信成功）的概率：

<table>
  <thead>
    <tr>
    <th>随机探测次数</th>
    <th>成功概率</th>
    </tr>
  </thead>

  <tbody>
    <tr>
    <td>174</td>
    <td>50%</td>
    </tr>
    <tr>
    <td>256</td>
    <td>64%</td>
    </tr>
    <tr>
    <td>1024</td>
    <td>98%</td>
    </tr>
    <tr>
    <td>2048</td>
    <td>99.9%</td>
    </tr>
  </tbody>
</table>

根据以上结果，如果还是假设 100 ports/s 这样相当温和的探测速率，那 **<mark>2 秒钟就有约 50% 的成功概率</mark>**。
即使非常不走运，我们仍然能在 **<mark>20s 时几乎 100% 穿透成功</mark>**，而此时**<mark>只探测了总端口空间的 4%</mark>**。

非常好！虽然这种 hard NAT 给我们带来了严重的穿透延迟，但最终结果仍然是成功的。
那么，如果是两个 hard NAT，我们还能处理吗？

## 7.3 双 hard NAT 场景

<p align="center"><img src="/assets/img/nat-traversal/nat-birthday-attack-2.png" width="70%" height="70%"></p>

这种情况下仍然可以用前面的 **<mark>多端口+随机探测</mark>** 方式，但成功概率要低很多了：

* 每次通过一台 hard NAT 去探测对方的端口（目的端口）时，我们**<mark>自己同时也生成了一个随机源端口</mark>**，
* 这意味着我们的搜索空间变成了二维 `{src port, dst port}` 对，而不再是之前的一维 dst port 空间。

这里我们也不就具体计算展开，只告诉结果：仍然**<mark>假设目的端打开 256 个端口，从源端发起 2048 次（20 秒）</mark>**，
成功的概率是：**<mark>0.01%</mark>**。

如果你之前学过生日悖论，就并不会对这个结果感到惊讶。理论上来说，

* 要达到 **<mark>99.9% 的成功率</mark>**，我们需要两边各进行**<mark>170,000 次</mark>**探测 ——
  如果还是以 100 packets/sec 的速度，就需要 **<mark>28 分钟</mark>**。
* 要达到 **<mark>50% 的成功率</mark>**，“只”需要 54,000 packets，也就是 **<mark>9 分钟</mark>**。
* 如果不使用生日悖论方式，而且**<mark>暴力穷举，需要 1.2 年时间</mark>**！

**<mark>对于某些应用来说，28 分钟可能仍然是一个可接受的时间</mark>**。用半个小时暴力穿透 NAT 之后，
这个连接就可以一直用着 —— 除非 NAT 设备重启，那样就需要再次花半个小时穿透建个新连接。但对于
交互式应用来说，这样显然是不可接受的。

更糟糕的是，如果去看常见的办公网路由器，你会震惊于它的 active session low limit 有多么低。
例如，一台 Juniper SRX 300 **<mark>最多支持 64,000 active sessions</mark>**。
也就是说，

* 如果我们想创建**<mark>一个</mark>**成功的穿透连接，**<mark>就会把它的整张 session 表打爆</mark>**
 （因为我们要暴力探测 65535 个端口，每次探测都是一条新连接记录）！
  这显然要求这台路由器能**<mark>从容优雅地处理过载的情况</mark>**。
* 这只是创建一条连接带来的影响！如果 20 台机器同时对这台路由器发起穿透呢？**<mark>绝对的灾难！</mark>**

至此，我们通过这种方式穿透了比之前更难一些的网络拓扑。这是一个很大的成就，因为
**<mark>家用路由器一般都是 easy NAT，hard NAT 一般都是办公网路由器或云 NAT 网关</mark>**。
这意味着这种方式能帮我们解决

* home-to-office（家->办公室）
* home-to-cloud （家->云）

的场景，以及一部分

* office-to-cloud （办公室->云）
* cloud-to-cloud （云->办公室）

场景。

## 7.4 控制端口映射（port mapping）过程：UPnP/NAT-PMP/PCP 协议

如果我们能**<mark>让 NAT 设备的行为简单点</mark>**，不要把事情搞这么复杂，那建
立连接（穿透）就会简单很多。真有这样的好事吗？还真有，有专门的一种协议叫
**<mark>端口映射协议</mark>**（port mapping protocols）。通过这种协议禁用掉前面
遇到的那些乱七八糟的东西之后，我们将得到一个非常简单的“请求-响应”。

下面是三个具体的端口映射协议：

1. <a href="https://openconnectivity.org/developer/specifications/upnp-resources/upnp/internet-gateway-device-igd-v-2-0/">UPnP IGD</a> (Universal Plug'n'Play Internet Gateway Device)

    最老的端口控制协议， 诞生于 1990s 晚期，因此使用了很多上世纪 90 年代的技术
    （XML、SOAP、**<mark>multicast HTTP over UDP —— 对，HTTP over UDP</mark>**
    ），而且很难准确和安全地实现这个协议。但以前很多路由器都内置了 UPnP 协议，
    现在仍然很多。

    请求和响应：

    * “你好，请将我的 `lan-ip:port` 转发到公网（WAN）”，
    * “好的，我已经为你分配了一个公网映射 `wan-ip:port` ”。

2. NAT-PMP

    UPnP IGD 出来几年之后，Apple 推出了一个功能类似的协议，名为
    <a href="https://tools.ietf.org/html/rfc6886">NAT-PMP</a> (NAT Port Mapping Protocol)。

    但与 UPnP 不同，这个协议**<mark>只</mark>**做端口转发，不管是在客户端还是服务端，实现起来都非常简单。

3. PCP

    稍后一点，又出现了 NAT-PMP v2 版，并起了个新名字<a href="https://tools.ietf.org/html/rfc6887">PCP</a> (Port Control Protocol)。

因此要更好地实现穿透，可以

1. **<mark>先判断本地的默认网关上是否启用了 UPnP IGD, NAT-PMP and PCP</mark>**，
2. 如果探测发现其中任何一种协议有响应，我们就**<mark>申请一个公网端口映射</mark>**，

    可以将这理解为一个**<mark>加强版 STUN</mark>**：我们不仅能发现自己的公网
    `ip:port`，而且能指示我们的 NAT 设备对我们的通信对端友好一些 —— 但并不是为这个端口修改或添加防火墙规则。

3. 接下来，任何到达我们 NAT 设备的、地址是我们申请的端口的包，都会被设备转发到我们。

但我们**<mark>不能假设这个协议一定可用</mark>**：

1. 本地 NAT 设备可能不支持这个协议；
2. 设备支持但默认禁用了，或者没人知道还有这么个功能，因此从来没开过；
3. 安全策略要求关闭这个特性。

    这一点非常常见，因为 UPnP 协议曾曝出一些高危漏洞（后面都修复了，因此如果是较新的设备，可以安全地使用 UPnP —— 如果实现没问题）。
    不幸的是，某些设备的配置中，UPnP, NAT-PMP，PCP 是放在一个开关里的（可能
    统称为 “UPnP” 功能），一开全开，一关全关。因此如果有人担心 UPnP 的安全性，他连另
    外两个也用不了。

最后，终归来说，**<mark>只要这种协议可用，就能有效地减少一次 NAT</mark>**，大大方便建连过程。
但接下来看一些不常见的场景。

## 7.5 多 NAT 协商（Negotiating numerous NATs）

目前为止，我们看到的客户端和服务端都各只有一个 NAT 设备。如果有多个 NAT 设备会
怎么样？例如下面这种拓扑：

<p align="center"><img src="/assets/img/nat-traversal/nat-multiple-layers.png" width="70%" height="70%"></p>

这个例子比较简单，不会给穿透带来太大问题。包从客户端 A **<mark>经过多次 NAT</mark>**
到达公网的过程，与前面分析的**<mark>穿过多层有状态防火墙</mark>**是一样的：

* 额外的这层（NAT 设备）**<mark>对客户端和服务端来说都不可见</mark>**，我们的穿
  透技术也不关心中间到底经过了多少层设备。
* **<mark>真正有影响的其实只是最后一层设备</mark>**，因为对端需要在这一层设备上
  找到入口让包进来。

具体来说，真正有影响的是端口转发协议。

1. 客户端使用这种协议分配端口时，为我们分配端口的是最靠近客户端的这层 NAT 设备；
2. 而我们期望的是让最离客户端最远的那层 NAT 来分配，否则我们得到的就是一个网络中间层分配的 `ip:port`，对端是用不了的；
3. 不幸的是，**<mark>这几种协议都不能递归地</mark>**告诉我们下一层 NAT 设备是多少 ——
  虽然可以用 traceroute 之类的工具来探测网络路径，再加上
  猜路上的设备是不是 NAT 设备（尝试发送 NAT 请求） —— 但这个就看运气了。

这就是为什么互联网上充斥着大量的文章说 **<mark>double-NAT 有多糟糕</mark>**，以
及警告用户为保持后向兼容不要使用 double-NAT。但实际上，double-NAT **<mark>对于绝大部分
互联网应用来说都是不可见的（透明的）</mark>**，因为大部分应用并不需要主动地做这种 NAT 穿
透。

但我也绝不是在建议你在自己的网络中设置 double-NAT。

1. 破坏了端口映射协议之后，某些视频游戏的多人（multiplayer）模式就会无法使用，
2. 也可能会使你的 IPv6 网络无法派上用场，后者是不用 NAT 就能双向直连的一个好方案。

但如果 double-NAT 并不是你能控制的，那除了不能用到这种端口映射协议之外，其他大部分东西都是不受影响的。

double-NAT 的故事到这里就结束了吗？—— 并没有，而且更大型的 double-NAT 场景将展现在我们面前。

## 7.6 运营商级 NAT 带来的问题

即使用 NAT 来解决 IPv4 地址不够的问题，地址仍然是不够用的，ISP（互联网服务提供商） 显然
无法为每个家庭都分配一个公网 IP 地址。那怎么解决这个问题呢？ISP 的做法是**<mark>不够了就再嵌套一层 NAT</mark>**：

1. 家用路由器将你的客户端 SNAT 到一个 "intermediate" IP 然后发送到运营商网络，
2. ISP's network 中的 NAT 设备再将这些 intermediate IPs 映射到少量的公网 IP。

后面这种 NAT 就称为“运营商级 NAT”（**<mark>carrier-grade NAT</mark>**，或称电信级 NAT），缩写 CGNAT。如下图所示：

<p align="center"><img src="/assets/img/nat-traversal/nat-cgnat-1.png" width="70%" height="70%"></p>

CGNAT 对 NAT 穿透来说是一个大麻烦。

* 在此之前，办公网用户要快速实现 NAT 穿透，只需在他们的路由器上手动设置端口映射就行了。
* 但有了 CGNAT 之后就不管用了，因为你无法控制运营商的 CGNAT！

好消息是：这其实是 double-NAT 的一个小变种，因此前面介绍的解决方式大部分还仍然是适用的。
某些东西可能会无法按预期工作，但只要肯给 ISP 交钱，这些也都能解决。
除了 port mapping protocols，其他我们已经介绍的所有东西在 CGNAT 里都是适用的。

### 新挑战：同一 CGNAT 侧直连，STUN 不可用

但我们确实遇到了一个新挑战：如何直连两个在同一 CGNAT 但不同家用路由器中的对端呢？如下图所示：

<p align="center"><img src="/assets/img/nat-traversal/nat-cgnat-2.png" width="70%" height="70%"></p>

**<mark>在这种情况下，STUN 就无法正常工作了</mark>**：STUN 看到的是客户端在公网（CGNAT 后面）看到的地址，
而我们想获得的是在 "middle network" 中的 `ip:port`，这才是对端真正需要的地址，

### 解决方案：如果端口映射协议能用：一端做端口映射

怎么办呢？

如果你想到了端口映射协议，那恭喜，答对了！**<mark>如果 peer 中任何一个 NAT 支持端口映射协议</mark>**，
对我们就能实现穿透，因为它分配的 `ip:port` 正是对端所需要的信息。

这里讽刺的是：double-NAT（指 CGNAT）破坏了端口映射协议，但在这里又救了我们！
当然，我们假设这些协议一定可用，因为 CGNAT ISP 倾向于在它们的家用路由器侧关闭
这些功能，已避免软件得到“错误的”结果，产生混淆。

### 解决方案：如果端口映射协议不能用：NAT hairpin 模式

如果不走运，NAT 上没有端口映射功能怎么办？

让我们回到基于 STUN 的技术，看会发生什么。两端在 CGNAT 的同一侧，假设 STUN 告诉我们 A 的地址是
`2.2.2.2:1234`，B 的地址是 `2.2.2.2:5678`。

那么接下来的问题是：如果 A 向 `2.2.2.2:5678` 发包会怎么样？期望的 CGNAT 行为是：

1. 执行 A 的 NAT 映射规则，即对 `2.2.2.2:1234 -> 2.2.2.2:5678` 进行 SNAT。
2. 注意到目的地址 `2.2.2.2:5678` 匹配到的是 B 的入向 NAT 映射，因此接着对这个包执行 DNAT，将目的 IP 改成 B 的私有地址。
3. 通过  CGNAT 的 internal 接口（而不是 public 接口，对应公网）将包发给 B。

这种 NAT 行为有个专门的术语，叫 **<mark>hairpinning</mark>**（直译为发卡，意思
是像发卡一样，沿着一边上去，然后从另一边绕回来），

<p align="center"><img src="/assets/img/nat-traversal/hairpin-icon.png" width="20%" height="20%"></p>

大家应该猜到的一个事实是：**<mark>不是所以 NAT 都支持 hairpin 模式</mark>**。
实际上，大量 well-behaved NAT 设备都不支持 hairpin 模式，

* 因为它们都有 **<mark>“只有 src_ip 是私有地址且 dst_ip 是公网地址的包才会经过我”</mark>** 之类的假设。
* 因此对于这种目的地址不是公网、需要让路由器把包再转回内网的包，它们会**<mark>直接丢弃</mark>**。
* 这些逻辑甚至是直接实现在路由芯片中的，因此除非升级硬件，否则单靠软件编程无法改变这种行为。

Hairpin 是所有 NAT 设备的特性（支持或不支持），并不是 CGNAT 独有的。

1. 在大部分情况下，这个特性对我们的 NAT 穿透目的来说都是无所谓的，因为我们期望中
  **<mark>两个 LAN NAT 设备会直接通信，不会再向上绕到它们的默认网关 CGNAT 来解决这个问题</mark>**。

     Hairpin 特性可有可无这件事有点遗憾，这可能也是为什么 hairpin 功能经常 broken 的原因。

2. 一旦必须涉及到 CGNAT，那 hairpinning 对连接性来说就至关重要了。

    Hairpinning 使内网连接的行为与公网连接的行为完成一致，因此我们无需关心目的
    地址类型，也不用知晓自己是否在一台 CGNAT 后面。

**<mark>如果 hairpinning 和 port mapping protocols 都不可用，那只能降级到中继模式了</mark>**。

## 7.7 全 IPv6 网络：理想之地，但并非问题全无

行文至此，一些读者可能已经对着屏幕咆哮：**<mark>不要再用 IPv4 了！</mark>**
花这么多时间精力解决这些没意义的东西，还不如直接换成 IPv6！

* 的确，之所以有这些乱七八糟的东西，就是因为 IPv4 地址不够了，我们**<mark>一直在用越来越复杂的 NAT 来给 IPv4 续命</mark>**。
* 如果 IP 地址够用，无需 NAT 就能让世界上的每个设备都有一个自己的公网 IP 地址，这些问题不就解决了吗？

简单来说，是的，这也正是 IPv6 能做的事情。但是，也只说对了一半：在理想的全 IPv6
世界中，所有这些东西会变得更加简单，但我们面临的**<mark>问题并不会完全消失</mark>** ——
因为**<mark>有状态防火墙仍然还是存在的</mark>**。

* 办公室中的电脑可能有一个公网 IPv6 地址，但你们公司肯定会架设一个防火墙，只允许
  你的电脑主动访问公网，而不允许反向主动建连。
* 其他设备上的防火墙也仍然存在，应用类似的规则。

因此，我们仍然会用到

1. 本文最开始介绍的防火墙穿透技术，以及
2. 帮助我们获取自己的公网 `ip:port` 信息的旁路信道
3. 仍然需要在某些场景下 fallback 到中继模式，例如 fallback 到最通用的 HTTP 中继
   协议，以绕过某些网络禁止 outbound UDP 的问题。

但我们现在可以抛弃 **<mark>STUN、生日悖论、端口映射协议、hairpin</mark>** 等等东西了。
这是一个好消息！

### 全球 IPv4/IPv6 部署现状

另一个更加严峻的现实问题是：当前并不是一个全 IPv6 世界。目前世界上

* 大部分还是 IPv4，
* <a href="https://www.google.com/intl/en/ipv6/statistics.html">大约 33% 是 IPv6</a>，而且分布极度不均匀，因此某些
  通信对所在的可能是 100% IPv6，也可能是 0%，或二者之间。

不幸的是，这意味着，IPv6 **<mark>还**<mark>无法作为我们的解决方案。
就目前来说，它只是我们的工具箱中的一个备选。对于某些 peer 来说，它简直是完美工
具，但对其他 peer 来说，它是用不了的。如果目标是“任何情况下都能穿透（连接）
成功”，那我们就仍然需要 IPv4+NAT 那些东西。

### 新场景：NAT64/DNS64

IPv4/IPv6 共存也引出了一个新的场景：NAT64 设备。

<p align="center"><img src="/assets/img/nat-traversal/nat-ipv6.png" width="70%" height="70%"></p>

前面介绍的都是 NAT44 设备：它们将一个 IPv4 地址转换成另一 IPv4 地址。
NAT64 从名字可以看出，是将一个内侧 IPv6 地址转换成一个外侧 IPv4 地址。
利用 DNS64 设备，我们能将 IPv4 DNS 应答给 IPv6 网络，这样对终端来说，它看到的就是一个
全 IPv6 网络，而仍然能访问 IPv4 公网。

> Incidentally, you can extend this naming scheme indefinitely. There
> have been some experiments with NAT46; you could deploy NAT66 if you
> enjoy chaos; and some RFCs use NAT444 for carrier-grade NAT.

如果需要处理 DNS 问题，那这种方式工作良好。例如，如果连接到
google.com，将这个域名解析成 IP 地址的过程会涉及到 DNS64 设备，它又会进一步 involve
NAT64 设备，但后一步对用户来说是无感知的。

但**<mark>对于 NAT 和防火墙穿透来说，我们会关心每个具体的 IP 地址和端口</mark>**。

### 解决方案：CLAT (Customer-side transLATor)

如果设备支持 CLAT (Customer-side translator — from Customer XLAT)，那我们就很幸运：

* **<mark>CLAT 假装操作系统有直接 IPv4 连接，而背后使用的是 NAT64</mark>**，以对应用程序无感知。
  在有 CLAT 的设备上，我们无需做任何特殊的事情。
* CLAT **<mark>在移动设备上非常常见</mark>**，但在桌面电脑、笔记本和服务器上非常少见，
  因此在后者上，必须自己做 CLAT 做的事情：检测 NAT64+DNS64 的存在，然后正确地使用它们。

### 解决方案：CLAT 不存在时，手动穿透 NAT64 设备

1. 首先检测是否存在 NAT64+DNS64。

    方法很简单：向 `ipv4only.arpa.` 发送一个 DNS 请求。这个域名会解析
    到一个已知的、固定的 IPv4 地址，而且是**<mark>纯 IPv4 地址</mark>**。如果得到的
    是一个 IPv6 地址，就可以判断有 DNS64 服务器做了转换，而它必然会用到 NAT64。这样
    就能判断出 NAT64 的前缀是多少。

1. 此后，要向 IPv4 地址发包时，发送格式为`{NAT64 prefix + IPv4 address}` 的 IPv6 包。
   类似地，收到来源格式为 `{NAT64 prefix + IPv4 address}` 的包时，就是 IPv4 流量。

1. 接下来，通过 NAT64 网络与 STUN 通信来获取自己在 NAT64 上的公网 `ip:port`，接
   下来就回到经典的 NAT 穿透问题了 —— 除了需要多做一点点事情。

幸运的是，如今的大部分 v6-only 网络都是移动运营商网络，而几乎所有手机都支持 CLAT。
运营 v6-only 网络的 ISPs 会在他们给你的路由器上部署 CLAT，因此最后你其实不需要做什么事情。
但如果想实现 100% 穿透，就需要解决这种边边角角的问题，即必须显式支持从 v6-only 网络连接 v4-only 对端。

## 7.8 将所有解决方式集成到 ICE 协议

### 针对具体场景，该选择哪种穿透方式？

至此，我们的 NAT 穿透之旅终于快结束了。我们已经覆盖了有状态防火墙、简单和高级
NAT、IPv4 和 IPv6。只要将以上解决方式都实现了，NAT 穿透的目的就达到了！

但是，

* 对于给定的 peer，如何判断改用哪种方式呢？
* 如何判断这是一个简单有状态防火墙的场景，还是该用到生日悖论算法，还是需要手动处理 NAT64 呢？
* 还是通信双方在一个 WiFi 网络下，连防火墙都没有，因此不需要任何操作呢？

**<mark>早期 NAT 穿透</mark>**比较简单，能让我们**<mark>精确判断出 peer 之间的路径特点</mark>**，然后针对性地采用相应的解决方式。
但后面，网络工程师和 NAT 设备开发工程师引入了一些新理念，给路径判断造成很大困难。因此
我们需要简化客户端侧的思考（判断逻辑）。

这就要提到 Interactive Connectivity Establishment (ICE，交换式连接建立) 协议了。
与 STUN/TURN 类似，ICE 来自**<mark>电信领域</mark>**，因此其 RFC 充满了 SIP、SDP、信令会话、拨号等等电话术语。
但如果忽略这些领域术语，我们会看到它**<mark>描述了一个极其优雅的判断最佳连接路径的算法</mark>**。

真的？这个算法是：**<mark>每种方法都试一遍，然后选择最佳的那个方法</mark>**。就是这个算法，惊喜吗？

来更深入地看一下这个算法。

### ICE (Interactive Connectivity Establishment) 算法

这里的讨论不会严格遵循 ICE spec，因此如果是在自己实现一个可互操作的
ICE 客户端，应该通读<a href="https://tools.ietf.org/html/rfc8445">RFC 8445</a>,
根据它的描述来实现。这里忽略所有电信术语，只关注核心的算法逻辑，
并提供几个在 ICE 规范允许范围的灵活建议。

1. 为实现和某个 peer 的通信，首先需要确定我们自己用的（客户端侧）这个 socket 的地址，
  这是一个列表，至少应该包括：

    1. 我们自己的 IPv6 `ip:ports`
    1. 我们自己的 IPv4 LAN `ip:ports`（局域网地址）
    1. 通过 STUN 服务器获取到的我们自己的 IPv4 WAN `ip:ports`（**<mark>公网地址</mark>**，可能会经过 NAT64 转换）
    1. 通过端口映射协议获取到的我们自己的 IPv4 WAN `ip:port`（NAT 设备的**<mark>端口映射协议分配的公网地址</mark>**）
    1. 运营商提供给我们的 endpoints（例如，**<mark>静态配置的端口转发</mark>**）

2. 通过旁路信道与 peer 互换这个列表。两边都拿到对方的列表后，就开始互相探测对方提供的地址。
  **<mark>列表中地址没有优先级</mark>**，也就是说，如果对方给的了 15 个地址，那我们应该把这 15 个地址都探测一遍。

    这些**<mark>探测包有两个目的</mark>**：

    1. **<mark>打开防火墙，穿透 NAT</mark>**，也就是本文一直在介绍的内容；
    2. **<mark>健康检测</mark>**。我们在不断交换（最好是已认证的）“ping/pong” 包，来检测某个特定的路径是不是端到端通的。

3. 最后，一小会儿之后，从可用的备选地址中（根据某些条件）选择“最佳”的那个，任务完成！

这个算法的优美之处在于：只要选择最佳线路（地址）的算法是正确的，那就总能获得最佳路径。

* ICE 会预先对这些备选地址进行排序（通常：LAN > WAN > WAN+NAT），但用户也可以自己指定这个排序行为。
* 从 v0.100.0 开始，Tailscale 从原来的 hardcode 优先级切换成了根据 round-trip
  latency 的方式，它大部分情况下排序的结果和 `LAN > WAN > WAN+NAT` 是一致的。
  但相比于静态排序，我们是动态计算每条路径应该属于哪个类别。

ICE spec 将协议组织为两个阶段：

1. 探测阶段
2. 通信阶段

但不一定要严格遵循这两个步骤的顺序。在 Tailscale，

* 我们发现更优的路径之后就会自动切换过去，
* 所有的连接都是先选择 DERP 模式（中继模式）。这意味着连接立即就能建立（**<mark>优先级最低但 100% 能成功的模式</mark>**），用户不用任何等待，
* 然后并行进行路径发现。通常几秒钟之后，我们就能发现一条更优路径，然后将现有连接透明升级（upgrade）过去。

但有一点需要关心：非对称路径。ICE 花了一些精力来保证通信双方选择的是相同的网络
路径，这样才能保证这条路径上有双向流量，能保持防火墙和 NAT 设备的连接一直处于 open 状态。
自己实现的话，其实并不需要花同样大的精力来实现这个保证，但需要确保你所有使用的所有路径上，都有双向流量。
这个目标就很简单了，只需要定期在所有已使用的路径上发 ping/pong 就行了。

### 健壮性与降级

要实现健壮性，还需要检测当前已选择的路径是否已经失败了（例如，NAT 设备维护清掉了所有状态），
如果失败了就要**<mark>降级（downgrade）到其他路径</mark>**。这里有两种方式：

1. 持续探测所有路径，维护一个降级时会用的备用地址列表；
2. **<mark>直接降级到保底的中继模式</mark>**，然后再通过路径探测升级到更好的路径。

    考虑到发生降级的概率是非常小的，因此这种方式可能是**<mark>更经济</mark>**的。

## 7.9 安全

最后需要提到安全。

本文的所有内容都假设：我们使用的**<mark>上层协议已经有了自己的安全机制</mark>**（
例如 QUIC 协议有 TLS 证书，WireGuard 协议有自己的公钥）。
如果还没有安全机制，那显然是要立即补上的。一旦动态切换路径，**<mark>基于 IP 的安全机制就是无用的了</mark>**
（IP 协议最开始就没怎么考虑安全性），至少要有**<mark>端到端的认证</mark>**。

* 严格来说，如果上层协议有安全机制，那即使收到是欺骗性的 ping/pong 流量，问题都不大，
  最坏的情况也就是**<mark>攻击者诱导两端通过他们的系统来中继流量</mark>**。
  而有了端到端安全机制，这并不是一个大问题（取决于你的威胁模型）。
* 但出于谨慎考虑，最好还是对路径发现的包也做认证和加密。具体如何做可以咨询你们的应用安全工程师。

# 8 结束语

我们终于完成了 NAT 穿透的目标！

如果实现了以上提到的所有技术，你将得到一个业内领先的 NAT 穿透软件，能在绝大多数场景下实现端到端直连。
如果直连不了，还可以降级到保底的中继模式（对于长尾来说只能靠中继了）。

但这些工作相当复杂！其中一些问题研究起来很有意思，但很难做到完全正确，尤其是那些
非常边边角角的场景，真正出现的概率极小，但解决它们所需花费的经历又极大。
不过，这种工作只需要做一次，一旦解决了，你就具备了某种超级能力：
探索令人激动的、相对还比较崭新的**<mark>端到端应用</mark>**（peer-to-peer applications）世界。

## 8.1 跨公网 端到端直连

**<mark>去中心化软件</mark>**领域中的许多有趣想法，简化之后其实都变成了
**<mark>跨过公网（互联网）实现端到端直连</mark>** 这一问题，开始时可能觉得很简单，但真正做才
发现比想象中难多了。现在知道如何解决这个问题了，动手开做吧！

## 8.2 结束语之 TL; DR

实现健壮的 NAT 穿透需要下列基础：

1. 一种基于 UDP 的协议；
1. 能在程序内直接访问 socket；
1. 有一个与 peer 通信的旁路信道；
1. 若干 STUN 服务器；
1. 一个保底用的中继网络（可选，但强烈推荐）

然后需要：

1. 遍历所有的 `ip:port`；
1. 查询 STUN 服务器来获取自己的公网 `ip:port` 信息，以及判断自己这一侧的 NAT 的“难度”（difficulty）；
1. 使用 port mapping 协议来获取更多的公网 `ip:ports`；
1. 检查 NAT64，通过它获取自己的公网 `ip:port`；
1. 将自己的所有公网 `ip:ports` 信息通过旁路信道与 peer 交换，以及某些加密秘钥来保证通信安全；
1. 通过保底的中继方式与对方开始通信（可选，这样连接能快速建立）
1. 如果有必要/想这么做，探测对方的提供的所有 `ip:port`，以及执行生日攻击（birthday attacks）来穿透 harder NAT；
1. 发现更优路径之后，透明升级到该路径；
1. 如果当前路径断了，降级到其他可用的路径；
1. 确保所有东西都是加密的，并且有端到端认证。
