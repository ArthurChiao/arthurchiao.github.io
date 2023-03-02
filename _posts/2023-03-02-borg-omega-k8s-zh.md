---
layout    : post
title     : "[译] Borg、Omega、K8s：Google 十年三代容器管理系统的设计与思考（ACM, 2016）"
date      : 2023-03-02
lastupdate: 2023-03-02
categories: k8s
---

### 译者序

本文翻译自 [Borg, Omega, and Kubernetes](https://queue.acm.org/detail.cfm?id=2898444)，
acmqueue Volume 14，issue 1（2016），
原文副标题为 ***Lessons learned from three container-management systems over a decade***。
作者 Brendan Burns, Brian Grant, David Oppenheimer, Eric Brewer, and John Wilkes，
均来自 Google。

文章介绍了 Google 在过去十多年设计和使用前后三代容器管理（编排）系统的所得所思，
虽然是 7 年前的文章，但内容并不过时，尤其能让读者能更清楚地明白 k8s
里的很多架构、功能和设计是怎么来的。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 1 十年三代容器管理系统

业界这几年对容器的兴趣越来越大，但其实在 Google，我们十几年前就已经开始大规模容器实践了，
这个过程中也**<mark>先后设计了三套不同的容器管理系统</mark>**。
这三代系统虽然出于不同目的设计，但每一代都受前一代的强烈影响。
本文介绍我们开发和运维这些系统所学习到的经验与教训。

## 1.1 Borg

Google 第一代统一容器管理系统，我们内部称为 Borg <sup>7</sup>。

### 1.1.1 在线应用和批处理任务混布

Borg 既可以管理 **<mark>long-running service</mark>** 也可以管理 **<mark>batch job</mark>**；
在此之前，这两种类型的任务是由两个系统分别管理的，

* Babysitter
* Global Work Queue

**<mark>Global Work Queue</mark>** 主要面向 batch job，但它**<mark>强烈影响了 Borg 的架构设计</mark>**；

### 1.1.2 早于 Linux cgroups 的出现

需要说明的是，不论是我们设计和使用 Global Work Queue 还是后来的 Borg 时，
**<mark>Linux cgroup 都还没有出现</mark>**。

### 1.1.3 好处：计算资源共享

Borg 实现了 long-running service 和 batch job 这两种类型的任务共享计算资源，
**<mark>提升了资源利用率，降低了成本</mark>**。

在底层支撑这种共享的是**<mark>Linux 内核中新出现的容器技术</mark>**（Google 给
Linux 容器技术贡献了大量代码），它能实现**<mark>延迟敏感型应用</mark>**和
**<mark>CPU 密集型批处理任务</mark>**之间的更好隔离。

### 1.1.4 自发的 Borg 生态

随着越来越多的应用部署到 Borg 上，我们的应用与基础设施团队开发了大量围绕 Borg
的管理工具和服务，功能包括：

* 配置或更新 job；
* 资源需求量预测；
* 动态下发配置到线上 jobs；
* 服务发现和负载均衡；
* 自动扩缩容；
* Node 生命周期管理；
* Quota 管理
* ...

也就是产生了一个围绕 Borg 软件生态，但驱动这一生态发展的是 Google 内部的不同团队，
因此从结果来看，这个生态是一堆**<mark>异构、自发的工具和系统</mark>**（而非一个有设计的体系），
用户必须通过几种不同的配置语言和配置方式来和 Borg 交互。

虽然有这些问题，但由于其巨大的规模、出色的功能和极其的健壮性，Borg 当前仍然是
Google 内部主要的容器管理系统。

## 1.2 Omega

为了使 Borg 的生态系统更加符合软件工程规范，我们又开发了 Omega<sup>6</sup>，

### 1.2.1 架构更加整洁一致

Omega 继承了许多已经在 Borg 中经过验证的成功设计，但又是**<mark>完全从头开始开发</mark>**，
以便**<mark>架构更加整洁一致</mark>**。

* Omega 将**<mark>集群状态</mark>**存储在一个基于 Paxos 的中心式面向事务 store（数据存储）内；
* **<mark>控制平面组件</mark>**（例如调度器）都可以**<mark>直接访问这个 store</mark>**；
* 用**<mark>乐观并发控制</mark>**来处理偶发的访问冲突。

### 1.2.2 相比 Borg 的改进

这种解耦使得 **<mark>Borgmaster 的功能拆分为了几个彼此交互的组件</mark>**，
而不再是一个单体的、中心式的 master，修改和迭代更加方便。
Omega 的一些创新（包括多调度器）后来也反向引入到了 Borg。

## 1.3 Kubernetes

Google 开发的第三套容器管理系统叫 Kubernetes<sup>4</sup>。

### 1.3.1 为什么要设计 K8s

开发这套系统的背景：

* 全球越来越多的开发者也开始对 Linux 容器感兴趣，
* Google 已经把公有云基础设施作为一门业务在卖，且在持续增长；

因此与 Borg 和 Omega 不同的是：Kubernetes 是开源的，不是 Google 内部系统。

### 1.3.2 相比 Omega 的改进

* 与 Omega 类似，k8s 的核心也是一个**<mark>共享持久数据仓库</mark>**（store），
  几个组件会监听这个 store 里的 object 变化；
* Omega 将自己的 store 直接暴露给了受信任的控制平面组件，但 k8s 中的状态
  **<mark>只能通过一个 domain-specific REST API 访问</mark>**，这个 API 会执行
  higher-level versioning, validation, semantics, policy 等操作，支持多种不同类型的客户端；
* 更重要的是，k8s 在设计时就**<mark>非常注重应用开发者的体验</mark>**：
  首要设计目标就是在享受容器带来的资源利用率提升的同时，**<mark>让部署和管理复杂分布式系统更简单</mark>**。

## 1.4 小结

接下来的内容将介绍我们在设计和使用以上三代容器管理系统时学到的经验和教训。

# 2 底层 Linux 内核容器技术

容器管理系统属于上层管理和调度，在底层支撑整个系统的，是 Linux 内核的容器技术。

## 2.1 发展历史：从 `chroot` 到 cgroups

* 历史上，最初的容器只是提供了 **<mark>root file system 的隔离能力</mark>**
  （通过 **<mark><code>chroot</code></mark>**）；
* 后来 FreeBSD jails 将这个理念扩展到了对其他 namespaces（例如 PID）的隔离；
* Solaris 随后又做了一些前沿和拓展性的工作；
* 最后，Linux control groups (**<mark><code>cgroups</code></mark>**) 吸收了这些理念，成为集大成者。
  内核 cgroups 子系统今天仍然处于活跃开发中。

## 2.2 资源隔离

容器技术提供的资源隔离（resource isolation）能力，使 Google 的资源利用率远高于行业标准。
例如，Borg 能利用容器实现**<mark>延迟敏感型应用</mark>**和**<mark>CPU 密集型批处理任务</mark>**的混布（co-locate），
从而提升资源利用率，

* 业务用户为了应对**<mark>突发业务高峰</mark>**和做好 failover，
  通常申请的资源量要大于他们实际需要的资源量，这意味着大部分情况下都存在着资源浪费；
* 通过混布就能把这些资源充分利用起来，给批处理任务使用。

容器提供的**<mark>资源管理工具</mark>**使以上需求成为可能，再加上强大的内核**<mark>资源隔离技术</mark>**，
就能避免这两种类型任务的互相干扰。我们是**<mark>开发 Borg 的过程中，同步给 Linux 容器做这些技术增强</mark>**的。

但这种隔离并未达到完美的程度：容器无法避免那些不受内核管理的资源的干扰，例如三级缓存（L3 cache）、
内存带宽；此外，还需要对容器加一个安全层（例如虚拟机）才能避免公有云上各种各样的恶意攻击。

## 2.3 容器镜像

现代容器已经不仅仅是一种隔离机制了：还包括镜像 —— 将应用运行所需的所有文件打包成一个镜像。

在 Google，我们用 MPM (Midas Package Manager) 来构建和部署容器镜像。
隔离机制和 MPM packages 的关系，就像是 Docker daemon 和
Docker image registry 的关系。在本文接下来的内容中，我们所说的“容器”将包括这两方面，
即**<mark>运行时隔离和镜像</mark>**。

# 3 面向应用的基础设施

随着时间推移，我们意识到容器化的好处不只局限于提升资源利用率。

## 3.1 从“面向机器”到“面向应用”的转变

容器化使数据中心的观念从原来的**<mark>面向机器</mark>**（machine oriented）
转向了**<mark>面向应用</mark>**（application oriented），

* **<mark>容器封装了应用环境</mark>**（application environment），
  向应用开发者和部署基础设施**<mark>屏蔽了大量的操作系统和机器细节</mark>**，
* 每个设计良好的**<mark>容器和容器镜像都对应的是单个应用</mark>**，因此
  **<mark>管理容器其实就是在管理应用，而不再是管理机器</mark>**。

Management API 的这种从面向机器到面向应用的转变，显著提升了应用的部署效率和问题排查能力。

## 3.2 应用环境（application environment）

### 3.2.1 资源隔离 + 容器镜像：解耦应用和运行环境

资源隔离能力与容器镜像相结合，创造了一个全新的抽象：

* 内核 cgroup、chroot、namespace 等基础设施的最初目的是**<mark>保护应用免受 noisy、nosey、messy neighbors 的干扰</mark>**。
* 而这些技术**<mark>与容器镜像相结合</mark>**，创建了一个**<mark>新的抽象</mark>**，
  **<mark>将应用与它所运行的（异构）操作系统隔离开来</mark>**。

这种镜像和操作系统的解耦，使我们能在开发和生产环境提供相同的部署环境；
这种环境的一致性提升了部署可靠性，加速了部署。
这层抽象能**<mark>成功的关键</mark>**，是有一个<i>hermetic</i>（封闭的，不受外界影响的）容器镜像，

* 这个镜像能封装一个应用的几乎所有依赖（文件、函数库等等）；
* 那**<mark>唯一剩下的外部依赖就是 Linux 系统调用接口了</mark>** —— 虽然这组有限的接口极大提升了镜像的可移植性，
  但它并非完美：应用仍然可能通过 socket option、`/proc`、`ioctl` 参数等等产生很大的暴露面。
  我们希望 [Open Container Initiative](https://www.opencontainers.org)
  等工作可以进一步明确容器抽象的 surface area。

虽然存在不完美之处，但容器提供的资源隔离和依赖最小化特性，仍然使得它在 Google 内部非常成功，
因此容器成为了 Google 基础设施唯一支持的可运行实体。这带来的一个后果就是，
Google 内部只有很少几个版本的操作系统，也只需要很少的人来维护这些版本，
以及维护和升级服务器。

### 3.2.2 容器镜像实现方式

实现 hermetic image 有多种方式，

* 在 Borg 中，程序可执行文件在编译时会静态链接到公司托管的特定版本的库<sup>5</sup>；

   但实际上 Borg container image 并没有做到完全独立：所有应用共享一个所谓的
  <i>base image</i>，这个基础镜像是安装在每个 node 上的，而非打到每个镜像里去；
   由于这个基础镜像里包含了 `tar` `libc` 等基础工具和函数库，
   因此升级基础镜像时会影响已经在运行的容器（应用），偶尔会导致故障。

* Docker 和 ACI 这样的现代容器镜像在这方面做的更好一些，它们地消除了隐藏的 host OS 依赖，
  明确要求用户在容器间共享镜像时，必须显式指定这种依赖关系，这更接近我们理想中的 hermetic 镜像。

## 3.3 容器作为基本管理单元

围绕容器而非机器构建 management API，将数据中心的核心从机器转移到了应用，这带了了几方面好处：

1. 应用开发者和应用运维团队无需再关心机器和操作系统等底层细节；
2. 基础设施团队**<mark>引入新硬件和升级操作系统更加灵活</mark>**，
  可以最大限度减少对线上应用和应用开发者的影响；
3. 将收集到的 telemetry 数据（例如 CPU、memory usage 等 metrics）关联到应用而非机器，
  **<mark>显著提升了应用监控和可观测性</mark>**，尤其是在垂直扩容、
  机器故障或主动运维等需要迁移应用的场景。

### 3.3.1 通用 API 和自愈能力

容器能提供一些通用的 API 注册机制，使管理系统和应用之间无需知道彼此的实现细节就能交换有用信息。

* 在 Borg 中，这个 API 是一系列 attach 到容器的 HTTP endpoints。
  例如，`/healthz` endpoint 向 orchestrator 汇报应用状态，当检测到一个不健康的应用时，
  就会自动终止或重启对应的容器。这种**<mark>自愈能力</mark>**（self-healing）是构建可靠分布式系统的最重要基石之一。
* K8s 也提供了类似机制，health check 由用户指定，可以是 HTTP endpoint **<mark>也可以一条 shell 命令</mark>**（到容器内执行）。

### 3.3.2 用 annotation 描述应用结构信息

容器还能提供或展示其他一些信息。例如，

* Borg 应用可以提供一个字符串类型的状态消息，这个字段可以动态更新；
* K8s 提供了 key-value **<mark><code>annotation</code></mark>**，
  存储在每个 object metadata 中，可以用来传递**<mark>应用结构（application structure）信息</mark>**。
  这些 annotations 可以由容器自己设置，也可以由管理系统中的其他组件设置（例如发布系统在更新完容器之后更新版本号）。

容器管理系统还可以将 resource limits、container metadata 等信息传给容器，
使容器能按特定格式输出日志和监控数据（例如用户名、job name、identity），
或在 node 维护之前打印一条优雅终止的 warning 日志。

### 3.3.3 应用维度 metrics 聚合：监控和 auto-scaler 的基础

容器还能用其他方式提供面向应用的监控：例如，
cgroups 提供了应用的 resource-utilization 数据；前面已经介绍过了，
还可以通过 export HTTP API 添加一些自定义 metrics 对这些进行扩展。

**<mark>基于这些监控数据就能开发一些通用工具</mark>**，例如 **<mark>auto-scaler 和 cAdvisor</mark>**<sup>3</sup>，
它们记录和使用这些 metrics，但无需理解每个应用的细节。
由于应用收敛到了容器内，因此就无需在宿主机上分发信号到不同应用了；这更简单、更健壮，
也更容易实现细粒度的 metrics/logs 控制，不用再
`ssh` 登录到机器执行 `top` 排障了 —— 虽然开发者仍然能通过 `ssh` 登录到他们的
容器，但实际中很少有人这样做。

监控只是一个例子。面向应用的转变在管理基础设施（management infrastructure）中产生涟漪效应：

* 我们的 load balancer 不再针对 machine 转发流量，而是针对 application instance 转发流量；
* Log 自带应用信息，因此很容易收集和按应用维度（而不是机器维度）聚合；
  从而更容易看出应用层面的故障，而不再是通过宿主机层的一些监控指标来判断问题；
* 从根本上来说，实例在编排系统中的 identity 和用户期望的应用维度 identity 能够对应起来，
  因此更容易构建、管理和调试应用。

### 3.3.4 单实例多容器（pod vs. container）

到目前为止我们关注的都是 `application:container = 1:1` 的情况，
但实际使用中不一定是这个比例。我们使用嵌套容器，对于一个应用：

* **<mark>外层容器提供一个资源池</mark>**；在 Borg 中成为 `alloc`，在 K8s 中成为 `pod`；
* 内层容器们部署和隔离具体服务。

实际上 Borg 还允许不使用 allocs，直接创建应用 container；但这导致了一些不必要的麻烦，
因此 K8s 就统一规定应用容器必须运行在 pod 内，即使一个 pod 内只有一个容器。
常见方式是一个 pod hold 一个复杂应用的实例。

* 应用的主体作为一个 pod 内的容器。
* 其他辅助功能（例如 log rotate、click-log offloading）作为独立容器。

相比于把所有功能打到一个二进制文件，这种方式能让不同团队开发和管理不同功能，好处：

* 健壮：例如，应用即使出了问题，log offloading 功能还能继续工作；
* 可组合性：添加新的辅助服务很容易，因为操作都是在它自己的 container 内完成的；
* 细粒度资源隔离：每个容器都有自己的资源限额，比如 logging 服务不会占用主应用的资源。

## 3.4 编排是开始，不是结束

### 3.4.1 自发和野蛮生长的 Borg 软件生态

Borg 使得我们能在共享的机器上运行不同类型的 workload 来提升资源利用率。
但围绕 Borg 衍生出的生态系统让我们意识到，Borg 本身只是**<mark>开发和管理可靠分布式系统的开始</mark>**，
各团队根据自身需求开发出的围绕 Borg 的不同系统与 Borg 本身一样重要。下面列举其中一些，
可以一窥其广和杂：

* 服务命名（naming）和**<mark>服务发现</mark>**（Borg Name Service, BNS）；
* **<mark>应用选主</mark>**：基于 Chubby<sup>2</sup>；
* 应用感知的**<mark>负载均衡</mark>**（application-aware load balancing）；
* **<mark>自动扩缩容</mark>**：包括水平（实例数量）和垂直（实例配置/flavor）自动扩缩容；
* **<mark>发布工具</mark>**：管理部署和配置数据；
* **<mark>Workflow 工具</mark>**：例如允许多个 job 按 pipeline 指定的依赖顺序运行；
* **<mark>监控工具</mark>**：收集容器信息，聚合、看板展示、触发告警。

### 3.4.2 避免野蛮生长：K8s 统一 API（Object Metadata、Spec、Status）

开发以上提到的那些服务都是为了解决应用团队面临的真实问题，

* 其中成功的一些后来得到了大范围的采用，使很多其他开发团队的工作更加轻松有效；
* 但另一方面，这些工具经常使用非标准 API、非标准约定（例如文件位置）以及深度利用了 Borg 内部信息，
  副作用是增加了在 Borg 中部署应用的复杂度。

K8s 尝试通过引入一致 API 的方式来降低这里的复杂度。例如，**<mark>每个 K8s 对象都有三个基本字段</mark>**：

1. **<mark><code>Object Metadata</code></mark>**：所有 object 的 `Object Metadata` 字段都是一样的，包括
    * object name
    * UID (unique ID)
    * object version number（用于乐观并发控制）
    * labels
2. **<mark><code>Spec</code></mark>**：用于描述这个 object 的**<mark>期望状态</mark>**；
    `Spec` and `Status` 的内容随 object 类型而不同。
3. **<mark><code>Status</code></mark>**：用于描述这个 object 的**<mark>当前状态</mark>**；

这种统一 API 提供了几方面好处：

* 学习更加简单，因为所有 object 都遵循同一套规范和模板；
* 编写适用于所有 object 的通用工具也更简单；
* 用户体验更加一致。

### 3.4.3 K8s API 扩展性和一致性

基于前辈 Borg 和 Omega 的经验，K8s 构建在一些可组合的基本构建模块之上，用户可以方便地进行扩展，
通用 API 和 object-metadata 设计使得这种扩展更加方便。
例如，pod API 可以被开发者、K8s 内部组件和外部自动化工具使用。

为了进一步增强这种一致性，K8s 还进行了扩展，**<mark>支持用户动态注册他们自己的 API</mark>**，
这些 API 和它内置的核心 API 使用相同的方式工作。
另外，我们还通过解耦 K8s API 实现了一致性（consistency）。
API 组件的解耦考虑意味着上层服务可以共享相同的基础构建模块。一个很好的例子：
**<mark>replica controller 和 horizontal auto-scaling (HPA) 的解耦</mark>**。

* Replication controller **<mark>确保</mark>**给定角色（例如，"front end"）的 **<mark>pod 副本数量符合预期</mark>**；
* Autoscaler 这利用这种能力，简单地**<mark>调整期望的 pod 数量</mark>**，而无需关心这些 pod 是如何创建或删除的。
  autoscaler 的实现可以将关注点放在需求和使用量预测上，而无需关心这些决策在底层的实现细节。

解耦确保了多个相关但不同的组件看起来和用起来是类似的体验，例如，k8s 有三种不同类似的
replicated pods:

* `ReplicationController`: run-forever replicated containers (e.g., web servers).
* `DaemonSet`: ensure a single instance on each node in the cluster (e.g., logging agents).
* `Job`: a run-to-completion controller that knows how to run a (possibly parallelized) batch job from start to finish.

这三种 pod 的策略不同，但这三种 controller 都依赖相同的 pod object 来指定它们希望运行的容器。

### 3.4.4 Reconcile 机制

我们还通过让不同 k8s 组件使用同一套设计模式来实现一致性。Borg、Omega 和 k8s
都用到了 reconciliation controller loop 的概念，提高系统的容错性。

首先对观测到的当前状态（“当前能找到的这种 pod 的数量”）和期望状态（“label-selector
应该选中的 pod 数量”）进行比较；如果当前状态和期望状态不一致，则执行相应的行动
（例如扩容 2 个新实例）来使当前状态与期望相符，这个过程称为 **<mark><code>reconcile</code></mark>**（调谐）。

由于所有操作都是**<mark>基于观察</mark>**（observation）**<mark>而非状态机</mark>**，
因此 reconcile 机制**<mark>非常健壮</mark>**：每次一个 controller 挂掉之后再起来时，
能够接着之前的状态继续工作。

### 3.4.5 舞蹈编排（choreography）vs. 管弦乐编排（orchestration）

K8s 的设计综合了 microservice 和 small control loop 的理念，这是
**<mark>choreography（舞蹈编排）</mark>**的一个例子 ——
通过多个独立和自治的实体之间的协作（collaborate）实现最终希望达到的状态。

> 舞蹈编排：场上**<mark>没有</mark>**指挥老师，每个跳舞的人都是独立个体，大家共同协作完成一次表演。
> 代表**<mark>分布式</mark>**、非命令式。

我们特意这么设计，以区别于管弦乐编排**<mark>中心式编排系统</mark>**（centralized
orchestration system），后者在初期很容易设计和开发，
但随着时间推移会变得脆弱和死板，尤其在有状态变化或发生预期外的错误时。

> 管弦乐编排：场上有一个指挥家，每个演奏乐器的人都是根据指挥家的命令完成演奏。
> 代表**<mark>集中式</mark>**、命令式。

# 4 避坑指南

这里列举一些经验教训，希望大家要犯错也是去犯新错，而不是重复踩我们已经踩过的坑。

## 4.1 创建 Pod 时应该分配唯一 IP，而不是唯一端口（port）

在 Borg 中，容器没有独立 IP，**<mark>所有容器共享 node 的 IP</mark>**。因此，
Borg 只能在调度时，**<mark>给每个容器分配唯一的 port</mark>**。
当一个容器漂移到另一台 node 时，会获得一个新的 port（容器原地重启也可能会分到新 port）。
这意味着，

* 类似 **<mark>DNS</mark>**（运行在 `53` 端口）这样的传统服务，只能用一些**<mark>内部魔改的版本</mark>**；
* 客户端无法提前知道一个 service 的端口，只有在 service 创建好之后再告诉它们；
* URL 中不能包含 port（容器重启 port 可能就变了，导致 URL 无效），必须引入一些 name-based redirection 机制；
* 依赖 IP 地址的工具都必须做一些修改，以便能处理 `IP:port`。

因此在设计 k8s 时，我们决定**<mark>给每个 pod 分配一个 IP</mark>**，

* 这样就实现了**<mark>网络身份</mark>**（IP）与**<mark>应用身份</mark>**（实例）的一一对应；
* 避免了前面提到的魔改 DNS 等服务的问题，应用可以随意使用 well-known ports（例如，HTTP 80）；
* 现有的网络工具（例如网络隔离、带宽控制）也无需做修改，直接可以用；

此外，所有公有云平台都提供 IP-per-pod 的底层能力；在 bare metal 环境中，可以使用
SDN overlay 或 L3 routing 来实现每个 node 上多个 IP 地址。

## 4.2 容器索引不要用数字 index，用 labels

用户一旦习惯了容器开发方式，马上就会创建一大堆容器出来，
因此接下来的一个需求就是如何对这些容器进行**<mark>分组和管理</mark>**。

### 4.2.1 Borg 基于 index 的容器索引设计

Borg 提供了 <i>jobs</i> 来对容器名字相同的 <i>tasks</i> 进行分组。

* 每个 job 由一个或多个完全相同的 task 组成，用向量（vector）方式组织，从 index=0 开始索引。
* 这种方式非常简单直接，很好用，但随着时间推移，我们越来越发觉它比较死板。

例如，

* 一个 task 挂掉后在另一台 node 上被创建出来时，task vector 中对应这个 task 的 slot 必须做双份的事情：
  识别出新的 task；在需要 debug 时也能指向老的 task；
* 当 task vector 中间某个 task 正常退出之后，vector 会留下空洞；
* vector 也很难支持跨 Borg cluster 的 job；
* 应用如何使用 task index（例如，在 tasks 之间做数据的 sharding/partitioning）
  和 Borg 的 job-update 语义（例如，默认是滚动升级时按顺序重启 task）之间，也存在一些不明朗之处：
  如果**<mark>用户基于 task index 来设计 sharding</mark>**，那 Borg 的重启策略就会导致数据不可用，因为它都是按 task 顺序重启的；
* Borg 还没有很好的方式**<mark>向 job 添加 application-relevant metadata</mark>**，
  例如 role (e.g. "frontend")、rollout status (e.g. "canary")，
  因此用户会将这些信息编码到 job name 中，然后自己再来解析 job name。

### 4.2.2 K8s 基于 label 的容器索引设计

作为对比，k8s 主要使用 <i>labels</i> 来识别一组容器（groups of containers）。

* Label 是 key/value pair，包含了可以用来鉴别这个 object 的信息；
  例如，一个 pod 可能有 `role=frontend` 和 `stage=production` 两个 label，表明这个容器运行的是生产环境的前端应用；
* Label 可以动态添加、删除和修改，可以通过工具或用户手动操作；
* **<mark>不同团队可以管理各自的 label</mark>**，基本可以做到不重叠；

可以通过 **<mark><code>label selector</code></mark>**
（例如，`stage==production && role==frontend`）来选中一组 objects；

* 组可以重合，也就是一个 object 可能出现在多个 label selector 筛选出的结果中，因此这种基于 label 的方式更加灵活；
* label selector 是动态查询语句，也就是说只要用户有需要，他们随时可以按自己的需求编写新的查询语句（selector）；
* Label selector 是 k8s 的 grouping mechanism，也定义了所有管理操作的范围（多个 objects）。

在某些场景下，能精确（静态）知道每个 task 的 identity 是很有用的（例如，静态分配 role 和 sharding/partitioning），
在 k8s 中，通过 label 方式也能实现这个效果，只要给每个 pod 打上唯一 label 就行了，
但打这种 label 就是用户（或 k8s 之上的某些管理系统）需要做的事情了。

Labels 和 label selectors 提供了一种通用机制，
**<mark>既保留了 Borg 基于 index 索引的能力，又获得了上面介绍的灵活性</mark>**。

## 4.3 Ownership 设计要格外小心

在 Borg 中，task 并不是独立于 job 的存在：

* 创建一个 job 会创建相应的 tasks，这些 tasks 永远与这个 job 绑定；
* 删除这个 jobs 时会删除它所有的 tasks。

这种方式很方便，但有一个严重不足：Borg 中只有一种 grouping 机制，也就是前面提到的 vector index
方式。例如，一个 job 需要存储某个配置参数，但这个参数只对 service 或 batch job 有用，并不是对两者都有用。
当这个 vector index 抽象无法满足某些场景（例如， DaemonSet 需要将一个 pod 在每个 node 上都起一个实例）时，
用户必须开发一些 workaround。

Kubernetes 中，那些 **<mark>pod-lifecycle 管理组件</mark>** —— 例如 replication controller ——
**<mark>通过 label selector 来判断哪些 pod 归自己管</mark>**；
但这里也有个问题：多个 controller 可能会选中同一个 pod，认为这个 pod 都应该归自己管，
这种冲突理应在配置层面解决。

label 的灵活性带来的好处：例如，
controller 和 pod 分离意味着**<mark>可以 "orphan" 和 "adopt" container</mark>**。
考虑一个 service，
如果其中一个 pod 有问题了，那只需要把相应的 label 从这个 pod 上去掉，
k8s service 就不会再将流量转发给这个 pod。这个 pod 不再接生产流量，但仍然活在线上，因此就可以对它进行 debug 之类的。
而与此同时，负责管理这些 pod 的 replication controller 就会立即再创建一个新的 pod 出来接流量。

## 4.4 不要暴露原始状态

Borg、Omega 和 k8s 的一个**<mark>核心区别</mark>**是它们的 **<mark>API 架构</mark>**。

**<mark>Borgmaster 是一个单体组件，理解每个 API 操作的语义</mark>**：

* 包含集群管理逻辑，例如 job/task/machine 的状态机；
* 运行基于 Paxos 的 replicated storage system，用来存储 master 的状态。

**<mark>Omega 除了 store 之外没有中心式组件</mark>**，

* **<mark>store 存储了 passive 状态信息，执行乐观并发控制</mark>**；
* **<mark>所有逻辑和语义都下放到了操作 store 的 client 上</mark>**，后者会直接读写 store 内容；
* 实际上，每个 Omega 组件都使用了同一套 client-side library 来与 store 交互，
 这个 liabrary 做的事情包括 packing/unpacking of data structures、retries、
 enforce semantic consistency。

**<mark>k8s 在 Omega 的分布式架构和 Borg 的中心式架构之间做了一个折中</mark>**，

* 在继承 Omega 分布式架构的灵活性和扩展性的同时，对系统级别的规范、策略、数据转换等方面还是集中式的；
  实现方式是在 **<mark>store 前面加了一层集中式的 API server</mark>**，屏蔽掉所有 store 实现细节，
  提供 object validation、defaulting 和 versioning 服务。
* 与 Omega 类似，客户端组件都彼此独立，可以独立开发、升级（在开源场景尤其重要），
  但各组件都要经过 apiserver 这个中心式服务的语义、规范和策略。

# 5 开放问题讨论

虽然我们已经了十几年的大规模容器管理经验，但仍然有些问题还没有很好的解决办法。
本节介绍几个供讨论，集思广益。

## 5.1 应用配置管理

在我们面临的所有问题中，耗费了最多脑力、头发和代码的是管理配置（configurations）相关的。
这里的配置指的是**<mark>应用配置</mark>**，即如何把应用的参数在创建容器时传给它，
而不是 hard-code。这个主题值得单独一整篇文章来讨论，这里仅 highlight 几方面。

首先，Borg 仍然缺失的那些功能，最后都能与应用配置（application configuration）扯上关系。
这些功能包括：

* Boilerplate reduction：例如，根据 workload 类型等信息为它设置默认的 restart policy；
* **<mark>调整和验证应用参数和命令行参数</mark>**；
* 实现一些 workaround，解决容器镜像管理 API 缺失的问题；
* 给应用用的函数库和配置魔板；
* **<mark>发布管理工具</mark>**；
* 容器镜像版本规范；

为了满足这些需求，配置管理系统倾向于发明一种 domain-specific 语言，
并希望最终成为一门图灵完备的配置语言：解析配置文件，提取某些数据，然后执行一些计算。
例如，根据一个 service 的副本数量，利用一个函数自动调整分给一个副本的内存。
用户的需求是减少代码中的 hardcode 配置，但最终的结果是一种难以理解和使用的“配置即代码”产品，用户避之不及。
它没有减少运维复杂度，也没有使配置的 debug 变得更简单；它只是将计算从一门真正的
编程语言转移到了一个 domain-specific 语言，而后者通常的配置开发工具更弱
（例如 debuggers, unit test frameworks 等）。

我们认为最有效的方式是接受这个需求，承认
**<mark><code>programmatic configuration</code></mark>** 的不可避免性，
在计算和数据之间维护一条清晰边界。
**<mark>表示数据的语言应该是简单、data-only 的格式</mark>**，例如 JSON or YAML，
而针对这些数据的计算和修改应该在一门真正的编程语言中完成，后者有
完善的语义和配套工具。

有趣的是，这种计算与数据分离的思想已经在其他领域开始应用，例如一些前端框架的开发，
比如 Angular 在 markup (data) 和 JavaScript (computation) 之间。

## 5.2 依赖管理

上线一个新服务通常也意味着需要上线一系列相关的服务（监控、存储、CI/CD 等等）。
如果一个应用依赖其他一些应用，那由集群管理系统来自动化初始化后者（以及它们的依赖）不是很好吗？

但事情并没有这么简单：**<mark>自动初始化依赖（dependencies）并不是仅仅启动一个新实例</mark>** ——
例如，可能还需要将其注册为一个已有服务的消费者，
(e.g., Bigtable as a service)，以及将认证、鉴权和账单信息传递给这些依赖系统。

但几乎没有哪个系统收集、维护或暴露这些依赖信息，因此即使是一些常见场景都很难在基础设施层实现自动化。
上线一个新应用对用户来说仍然是一件复杂的事情，导致开发者构建新服务更困难，
经常导致最新的最佳实践无法用上，从而影响新服务的可靠性。

一个常见问题是：如果依赖信息是手工提供的，那很难维持它的及时有效性，
与此同时，自动判断（例如，通过 tracing accesses）通常会失败，因为无法捕捉理解相应结果的语义信息。
（Did that access have to go to <i>that</i> instance, or would any instance have sufficed?）

一种可能的解决方式是：每个应用显式声明它依赖的服务，基础设施层禁止它访问除此之外的所有服务
（我们的构建系统中，编译器依赖就是这么做的<sup>1</sup>）。
好处就是基础设施能做一些有益的工作，例如自动化设置、认证和连接性。

不幸的是，**<mark>表达、分析和使用系统依赖会导致系统的复杂性升高</mark>**，
因此并没有任何一个主流的容器管理系统做了这个事情。我们仍然希望 k8s
能成为一个构建此类工具的平台，但这一工作目前仍困难重重。

# 6 总结

过去十多年开发容器管理系统的经历教会了我们很多东西，我们也将这些经验用到了 k8s —— Google 最新的容器管理系统 —— 的设计中，
它的目标是基于容器提供的各项能力，显著提升开发者效率，以及使系统管理（不管是手动还是自动）更加方便。
希望大家能与我们一道，继续完善和优化它。

# 参考资料

1. Bazel: {fast, correct}—choose two; <a href="http://bazel.io/">http://bazel.io</a>.
2. Burrows, M. 2006. The Chubby lock service for loosely coupled distributed
   systems. Symposium on Operating System Design and Implementation (OSDI), Seattle, WA.
3. cAdvisor; <a href="https://github.com/google/cadvisor">https://github.com/google/cadvisor</a>.
4. Kubernetes; <a href="https://kubernetes.io/">http://kubernetes.io/</a>.
5. Metz, C. 2015. Google is 2 billion lines of code—and it's all in one place.
   <i>Wired </i>(September); <a href="https://www.wired.com/2015/09/google-2-billion-lines-codeand-one-place/">http://www.wired.com/2015/09/google-2-billion-lines-codeand-one-place/</a>.
6. Schwarzkopf, M., Konwinski, A., Abd-el-Malek, M., Wilkes, J. 2013.
   **<mark>Omega: flexible, scalable schedulers for large compute clusters</mark>**.
   European Conference on Computer Systems (EuroSys), Prague, Czech Republic.
7. Verma, A., Pedrosa, L., Korupolu, M. R., Oppenheimer, D., Tune, E., Wilkes,
   J. 2015. **<mark>Large-scale cluster management at Google with Borg</mark>**.
   European Conference on Computer Systems (EuroSys), Bordeaux, France.
