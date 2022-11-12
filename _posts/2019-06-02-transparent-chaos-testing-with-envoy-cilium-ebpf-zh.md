---
layout: post
title:  "[译] 基于 Envoy、Cilium 和 eBPF 实现透明的混沌测试（KubeCon, 2019）"
date:   2019-06-02
lastupdate:   2019-06-04
categories: envoy cilium bpf chaos
---

### 译者序

本文内容来自 2019 年的一个技术分享 [Transparent Chaos Testing with Envoy, Cilium
and eBPF](https://docs.google.com/presentation/d/1gMlmXqH6ufnb8eNO10WqVjqrPRGAO5-1S1zjcGo1Zr4)
，演讲嘉宾是 Cilium 项目的创始人和核心开发者，演讲为英文。
本文翻译了其中的技术性内容，少量非技术内容（例如开场白）已略过。如有疑
问，请观看 [原视频](https://www.youtube.com/watch?v=i6d_R7akyU4) 或
[PPT](https://docs.google.com/presentation/d/1gMlmXqH6ufnb8eNO10WqVjqrPRGAO5-1S1zjcGo1Zr4)。

以下是译文。

----

<p align="center"><img src="/assets/img/chaos-with-cilium/1.PNG" width="70%" height="70%"></p>

在座有些人可能会觉得奇怪，以前我的分享都是关于网络、BPF、安全等主题，为什么今
天变成了 **混沌测试** （chaos testing）？直接原因是：我们当前确实在做这件事情。
如果我们自己开发的工具能用来做混沌测试，那为什么不试试看呢？所以今天给大家分享
的就是我们如何利用三个工具：**Envoy、Cilium 和 eBPF** 来做混沌测试的。

那么这三者是如何组成一个系统的？如何基于这个系统来做混沌测试？到底什么是**透
明的混沌测试**（transparent chaos testing）？这些都是我们今天要深入探讨的主题。

<p align="center"><img src="/assets/img/chaos-with-cilium/2.PNG" width="70%" height="70%"></p>

我们的透明混沌测试基于如下技术栈：

* Envoy Go Extensions
* Envoy
* Cilium
* eBPF

首先，我们会用到 Envoy 的 **Go Extension**（Go 语言扩展）。如果你还没听说过
Envoy，那应该尽快去了解一下。简单来说，Go Extension 提供了一种让 **Go 编写的程
序和 Envoy 本身一起运行**的能力。这使得开发者可以**扩展和定制化 Envoy**，而且使
用的是 Go 而非 C++ （Envoy 本身用 C++编写）。Envoy 还提供了其他语言扩展，不限
于 Go。

其次，我们会用到 Envoy （本身）。稍后我会对 Envoy 做一个快速介绍。

其次，用到 Cilium。运行在 Envoy 的下面，作为 CNI plugin 和 Load Balancing plugin。

最后，eBPF，这是一项内核里的强大技术，允许我们透明、高效地做类似这样的事情。

接下来会对这几项内容深入展开，介绍我们混沌测试的技术栈和其中的各个组件。

<p align="center"><img src="/assets/img/chaos-with-cilium/3.PNG" width="70%" height="70%"></p>

什么是混沌测试（chaos testing）？

如果搜索 *Chao Testing*，你可能首先会搜到 Chaos Engineering （混沌工程 ）的定义：

> Chaos engineering is the discipline of experimenting on a software system in
> production in order to build confidence in the system’s capability to
> withstand turbulent and unexpected conditions. [1]
>
> （混沌工程是一门在生产环境对系统进行实验、测试系统在混乱或非预期情
> 况下的容错能力、以构建对系统容错能力信心的学科。）

Chaos Testing 就是从 Chaos Engineering 发展出来的一个分支，而 Chaos Engineering
是从 Netflix 的一个叫 Chaos Monkey 的项目发展而来的。**从定义来说，混沌工程是要在
生产环境进行的。我不知道多少人理解这句话的分量**，也不清楚有多少人真正在生产环境
做过这种测试。但总体来说，它意味着主动向基础设施引入混沌（chaos），以更好地了解
故障模式（failure modes）。

<p align="center"><img src="/assets/img/chaos-with-cilium/4.PNG" width="70%" height="70%"></p>

今天我们主要关注的是 **故障注入**（fault injection）。故障注入是混沌测试的一个子
集，其基本原理就是向正常运行的系统主动注入故障，以模拟服务中断（outage）或服务故
障（service failure）等等。

故障注入非常有用，因为它可以测试系统在特定情况下是如何运行的，以及发生故障、尤其
是多个组件同时发生故障时的系统行为。

有了这些基础，我们来看一个具体的例子。

<p align="center"><img src="/assets/img/chaos-with-cilium/5.PNG" width="70%" height="70%"></p>

两个服务 A 和 B，A 向 B 的 `/awesome-api/func1/` 发送 `PUT` 请求，正常的话 B 返
回 200。

有了故障注入，我们可以修改 B 的响应，变成我们期望的行为。例如返回 503，
提示系统遇到内部错误；或者给响应加一些延迟，模拟服务端响应比较慢的场景；甚至
还可以模拟 payload 数据损坏等等。

我们来看一个最简单场景的场景：模拟服务端遇到内部错误，例如服务程序 Go panic，导
致返回 503。

<p align="center"><img src="/assets/img/chaos-with-cilium/6.PNG" width="70%" height="70%"></p>

要模拟以上场景，我们需要几方面准备：

首先，需要一个**代理**（Proxy），在两个服务之间转发和修改信息，因为我们不想修改
应用代码来返回错误。

其次，**模拟失败的能力**。即使服务端返回 200，我们也能够将其改为 4xx、5xx 或者期
望的任何值，以模拟服务端（应用）错误。另外，我们要有模拟延迟的能力。

其次，能**指定错误率**，我们不希望响应是 100% 失败的；而是希望例如以 10% 的概
率发生错误； 必现的错误（100%）很好查；但如果错误率只有 1%，且同时还有 `500ms`
的延迟，那查起来就会困难很多。

另外，还需要**透明**。我们希望整个过程**应用是无感知的，无需做任何修改**。
我们的目的就是在**运行中的生产环境或 staging 环境**跑这种测试，看看基
础设施会如何表现、如何恢复、自动扩缩容是否工作等等。

最后，**可见性**（visibility）。如果设置了 50% 的故障注入率，**如何判断注入是
否成功**？因为服务也有可能真的发生了故障。如果得到了一个 3/5 的故障率，如何判
断这是混沌测试设置导致的预期结果，还是服务真的发生了故障导致的错误率？或者如果响
应延迟很大，那到底是混沌测试导致的预期结果，还是服务真的比较慢？

<p align="center"><img src="/assets/img/chaos-with-cilium/7.PNG" width="70%" height="70%"></p>

接下来看如何满足以上需求。我们将深入认识这些组件，看它们分别用来做什么。

<p align="center"><img src="/assets/img/chaos-with-cilium/8.PNG" width="70%" height="70%"></p>

首先是 Envoy。Envoy 是一个服务，也是一个边缘代理（edge proxy）。它能感知 7 层协
议，也能运行在 TCP （或称 4 层）模式。但 Envoy 的最主要使用场景是**作为代理，理
解应用层协议**（application protocols），在其中承担多种功能，例如高级负载均衡、
基于路径的路由（path based routing）或者 7 层路由、金丝雀发布（canary release）、
自动重试、熔断、限流等等。

安全方面，Envoy 提供鉴权、mTLS 等等。

Envoy 具有可观测性（observability ），这也是我觉得 Service Mesh 中非常重要、
非常有前途的一项功能，因为它提供了所有服务间通信涉及的所有 API 调用的可见性（
visibility）。

另外，Envoy 还是可扩展的。我们这里使用的是 Go Extension，但其实还有其他语言的扩
展。例如 WASM（Web Assembly）、Lua 等等。

<p align="center"><img src="/assets/img/chaos-with-cilium/9.PNG" width="70%" height="70%"></p>

接下来看 Go Extension。

这里使用 Go Extension 是因为我们不想改 Envoy 的 C++ 代码。另外，Envoy 自身已经集
成了故障注入功能，但我们这里需要定制化。我们想对它的故障注入进行扩展，这样不仅可
以测试通用的故障错误（generic service failure），而且可以测试**特定应用相关的**（
application-specific）服务错误。

例如，如果是一个正在执行计费事务（billing transaction）的服务，你可能想模拟事务
失败的场景。注意，这里期望的不单单是返回一般的 543 错误码，而且是特定应用相关的
错误。那就需要一个代理，它解析请求，能理解应用的 payload，比如解析 REST API 调用
。这种场景就很适合 Go 扩展，用 Go `net/http` 库写起来非常方便。最后的形式就是 Go
实现代理的处理逻辑，然后和 Envoy 一起运行，作为 Envoy 的一部分。

下面是个具体例子：金丝雀发布。

<p align="center"><img src="/assets/img/chaos-with-cilium/10.PNG" width="70%" height="70%"></p>

两个服务 A 和 B。服务 B 当前是 `1.0` 版本，A 和 B 之间是 Envoy，通过负载均衡功能
将 50% 的流量分别打到 B 的两台机器。

接下来你想将 B 升级到 2.0 版本。

一种方式是 **滚动升级**（rolling update），将 B 机器逐台升级到新版本。但这种方式
非常激进（radical），因为每台升级后，从负载均衡过来的全部流量（总流量的 50%）会
立刻打到 v2.0 API。

另一种方式就是**金丝雀发布**（canary release），过程大致如下：再加入一台 v2.0
的新机器，然后先切 1% 的流量到这台机器（v2.0），然后逐步增大 v2.0 机器的流量百分
比。这是 Envoy 的一个典型使用场景，也是我们混沌测试要用到的模式。

我们用到的下一个组件是 Cilium。

<p align="center"><img src="/assets/img/chaos-with-cilium/11.PNG" width="70%" height="70%"></p>

使用 Cilium 主要为了实现测试的**透明性**。这里先介绍一下 Cilium 项目。

Cilium 最大的特点是基于 eBPF 技术，稍后我也会对 eBPF 做一个介绍，这是内核里的一
项非常强大的技术。

Cilium 可以做很多事情，首先是网络功能，它有自己的 Cilium-CNI 插件，但也可以
运行在其他 CNI 插件之上，例如 Flannel、Calico、AWS CNI、Lyft VPC CNI 等等。

其次，Cilium 实现了 K8S 的 Service 功能，并且可扩展性非常好，能够支持 10K+
Services。

其次，Cilium 实现了 K8S 网络安全策略（Network Policy），并且进行了扩展，支持额外
的功能，例如可感知 DNS 的安全策略（DNS-aware policies）。举个例子，安全策略可以
配置成：*“clusterB 接受从 `a.cluster-a.com` 来的流量。”*，或者*“接受从
`b.cluster-b.com` 过来的建立 TCP 连接的请求。”*因此，你可以指定基于 DNS 的安全策略
。另外，Cilium 还支持基于 7 层的安全策略、基于 Service name 的安全策略等等。扩展
的 K8S 的安全策略通过 CRD（Custom Resource Definition）的方式实现，我们正在努力
将这些功能变成社区标准，这样其他 CNI 插件也可以实现这些功能。

Cilium 支持 identity-based security enforcement（安全生效/落实方式）。限于时间关
系我无法详细展开，但总体来说，这是 IP-based security enforcement 的升级版。

Cilium 支持多集群，支持加密。

原生支持与 Envoy 集成，稍后会看到。今天我们主要关注**透明的 Envoy 注入**（
transparent Envoy injection）：在两个服务之间运行 Envoy 作为代理，或者在一个服务
前面运行 Envoy 作为代理，服务完全感知不到 Envoy 的存在，因此对它来说是完全透明的
。

另外一点，Cilium 可以加速 Service Mesh 中的服务测量（service measure），还即将支
持透明 SSL。

<p align="center"><img src="/assets/img/chaos-with-cilium/12.PNG" width="70%" height="70%"></p>

最后是 eBPF，这是所需的最后一个重要组件。eBPF 是一项令人振奋的新技术，今年以来我
们已经看到了大量基于 eBPF 的新项目。

eBPF 的前身是 BPF。BPF 已经很老了，但最近我们意识到业内对**可编程内核**有很强的
需求，而将 BPF 扩展成一个虚拟机嵌入到内核显然可以满足这个需求，因此我们投入了大
量的精力扩展（extend）BPF（因此称为 eBPF），以允许程序**对内核本身进行编程**（即
通过程序动态修改内核的行为。传统方式要么是**给内核打补丁**，要么是**修改内核源码
重新编译**，译者注）。

一句话来概括：**编写代码监听内核事件，当事件发生时，BPF 代码就会在内核执行**。

如图中的几个例子：

* 在网络设备每次收或发包时，执行一段 BPF 程序，这就是 Cilium 在网络侧做的事情
* 在发生特定的系统调用（例如 read 或 connect）时执行一段 BPF 程序，这就是
  seccomp 之类的程序如何工作的，也是基于 BPF 的容器运行时保护机制如何工作的
* 在发生 block IO 的时候执行一段 BPF 程序，这就是一些跟踪和监控采集系统如何工作的
* 为称为 tracepoints 的东西执行一段 BPF 程序，例如每次内核发生 TCP 连接断开或 TCP 重传

这**使得 Linux 内核真正变成了可编程的**，而无需对内核源码做任何修改。另
外，这一方式非常安全和高效，加载的 BPF 程序能以（内核）原生的速度执行，而且很安
全，简直完美。
我们可以基于 BPF 扩展和定制化内核的功能，给它增加新的特性，而完全不需要修
改内核源码。这就是为什么很多人对 eBPF 技术如此兴奋的原因。

那么，到底什么是**透明的混沌测试**呢？

<p align="center"><img src="/assets/img/chaos-with-cilium/13.PNG" width="70%" height="70%"></p>

我们通过一个例子来更清楚的解释，这个例子将会把前面介绍的几个组件串联成一个完成的
技术栈。

<p align="center"><img src="/assets/img/chaos-with-cilium/14.PNG" width="70%" height="70%"></p>

很简单的例子。两个服务 A 和 B，假设运行在不同 node 上（也可以运行在相同 node 上
）。

两个服务之间要通信，首先需要一个 CNI plugin 打通网络：

<p align="center"><img src="/assets/img/chaos-with-cilium/15.PNG" width="70%" height="70%"></p>

接下来引入 Envoy，运行在两个服务直接，来做故障注入。

<p align="center"><img src="/assets/img/chaos-with-cilium/16.PNG" width="70%" height="70%"></p>

这里我画的 Envoy 架构已经极大简化过了，只需要知道：

1. listener 用于监听和建立连接
1. filter chain 用于处理过滤规则
1. proxy 将请求发送给真正的服务

接下来，在 Envoy 之上运行 Go Extension：

<p align="center"><img src="/assets/img/chaos-with-cilium/17.PNG" width="70%" height="70%"></p>

可以看到，Go Extension 实现了 filter 功能。Envoy 可以动态加载 Go 代码。从 A 过
来的请求会经过我们实现的 Go 过滤器，故障注入就是在这里实现的。

**之所以称为透明是因为 A 和 B 都感知不到 Envoy 的存在**。Cilium 会用 eBPF 这项黑
科技，魔法般地将请求重定向到 Envoy。透明意味着我们无需 sidecar 注入，或者运行其
他服务来拦截流量或请求，只需配置 Cilium。

下图展示了 Cilium 的配置长什么样：

<p align="center"><img src="/assets/img/chaos-with-cilium/18.PNG" width="70%" height="70%"></p>

这是一个简单配置，它是一个 Cilium network policy 的 CRD 配置。其中：

1. `endpointSelector`：带 `myService` label 的 Pod 都会被选中，应用此规则
1. `ingress`：只有**进入** Pod 的流量需要应用此规则
1. `port: 8080`：只有 8080 口的流量需要应用此规则
1. `protocol: TCP`：只有 TCP 流量需要应用此规则
1. `l7proto: chaos`：运行名为 `chaos` 的 Go Extension
1. `probability: 0.5`：以 50% 概率注入故障
1. `rewrite-status: 504 Application Error`：将响应重写为这里的指定值

总结一下以上过程：

<p align="center"><img src="/assets/img/chaos-with-cilium/19.PNG" width="70%" height="70%"></p>

我们使用 Envoy 作为中间代理；并通过名为 `chaos` 的 Go Extension 完成了故障注入和
错误率设置；Cilium 和 eBPF 在其中承担了关键角色，实现了对应用的透明；最后，整个
过程是易于观测的，例如我们可以看到 HTTP 头、Envoy 监控指标等等。

接下来看 Demo（讲解略）。

<p align="center"><img src="/assets/img/chaos-with-cilium/20.PNG" width="70%" height="70%"></p>

步骤：

<p align="center"><img src="/assets/img/chaos-with-cilium/21.PNG" width="70%" height="70%"></p>

更多配置参考：

<p align="center"><img src="/assets/img/chaos-with-cilium/22.PNG" width="70%" height="70%"></p>

<p align="center"><img src="/assets/img/chaos-with-cilium/23.PNG" width="70%" height="70%"></p>

<p align="center"><img src="/assets/img/chaos-with-cilium/24.PNG" width="70%" height="70%"></p>

其他相关信息：

<p align="center"><img src="/assets/img/chaos-with-cilium/25.PNG" width="70%" height="70%"></p>

## Q & A

#### 1. 模拟响应延迟的时候，每个包的 delay 是在哪里实现的，内核？服务端？还是哪里？

在 Envoy 里。响应其实已经被解析，但没有立即发出，被 Envoy hold 在那里。Envoy 维
护了连接状态，delay 的原理和 Envoy 限流的原理其实是一样的。另外注意它已经解析了
response，**delay 的单位是整个 response，而不是单个包**。

#### 2. Demo 展示的是 HTTP，请问 gRPC 也可以实现类似的测试吗？这个混沌测试是后端协议无关的吗？

可以。我这里展示的是 Go Extension，因此使用了 Go 里面的 `net.http` 库，但这
个过程其实跟协议无关。CNI Plugin 是在**数据层**捕获数据的，你可以用 `io.Read`
之类的函数拿到原始数据，接下来解析成什么协议完全看你自己。

#### 3. 这几个组件分别需要什么时候启动？

Envoy 是 network policy 生效之后才开始工作的。匹配到 label 的 Pod
会被应用这个策略，然后将流量送到 Envoy。当删除这个策略时，所以的流量又重新回
到原来的路径，不会再经过 Envoy。

另外，Go Extension 相比于原生的 Envoy，可能会有 10% 的性能开销，主要是上下文切换
等原因造成的。

#### 4. 如果已经有 K8S 原生的 network policy，那添加新的 Cilium network policy 之后会怎样？

两种策略都有效（respect both）。K8S network policy 都是白名单，没有 deny、reject
等指令，因此这两种策略不会冲突，最终都会有效。

Cilium 推出自己的 network policy 的原因是易于配置和解析，配置格式都是标准的
Golang map，可以配置任何的 key-value，添加新功能时非常方便。

#### 5. Cilium 可以和 Istio 集成，那 Go extension 和 Istio 会不会有冲突？

你可以将这里介绍的混沌测试和 Istio 一起运行，不过这里的 Go Extension 是 Envoy 的
filter。Envoy 和 Istio 有各自的 filter，你需要将我们这里的 filter 实现为 Istio
的 filter，而 Istio 目前是不支持 Go Extension 的，你可能需要将这段逻辑放到 Istio
的 sidecar 里。

#### 6. 刚才提到 Go Extension 有 10% 的性能下降，如果用 C++ Extension，是不是就没有这么差？

是的。本质原因是 Go 和 C++ 的内存模型不同，内存上下文切换（并不是真正的核心态/用
户态上下文切换）代价比较大。但注意 Go 也并非每个包都会产生一次上下文切换，通常是
每个请求一次（usually one call per request），除非 HTTP 请求特别特别大。10% 是我
们观察到的最差情况，用 C++ 实现确实会好很多。使用 Go 主要是开发方便。

#### 7. 能再解释一下是如何启动 Envoy 和 `l7proto` 的吗？

可以。首先得有我们这份配置文件，在配置文件中，`l7proto` 之前的部分都是不涉及
Envoy 的。

对于 `l7proto`，我们支持 Cassandra、Kafka、Memcached 等等，你要选择其中一种，并且
这种协议还要 Envoy 支持，之后的事情就是自动的了。
