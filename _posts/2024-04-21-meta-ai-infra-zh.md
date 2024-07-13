---
layout    : post
title     : "[译] Meta/Facebook 超大规模 AI/GPU 基础设施设计（2024）"
date      : 2024-04-21
lastupdate: 2024-04-21
categories: facebook gpu ai
---

本文翻译自 2024 年 Meta/Facebook 的一篇文章：
[Building Meta’s GenAI Infrastructure](https://engineering.fb.com/2024/03/12/data-center-engineering/building-metas-genai-infrastructure/)。

1. 两个 GPU 集群，每个集群 **<mark><code>2.4w H100</code></mark>**，分别用 **<mark><code>RoCE/InfiniBand</code></mark>** 网络；
1. **<mark>LLaMA3 就是在这两个集群上训练出来的</mark>**；
1. 预计到 2024 年底，Meta AI 基础设施建设将拥有 **<mark><code>35w 张 H100</code></mark>** GPU，总算力相当于约 **<mark><code>60w 张 H100</code></mark>**。

<p align="center"><img src="/assets/img/meta-ai-infra/Meta-24K-GenAi-Clusters-hero.webp" width="100%" height="100%"></p>

水平及维护精力所限，译文不免存在错误或过时之处，如有疑问，请查阅原文。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

以下是译文。

----

* TOC
{:toc}

----

作为对未来人工智能的重要投资，Meta 打造了两个大规模 AI 集群，每个集群由 **<mark><code>2.4w 张 GPU</code></mark>** 组成，
本文分享其计算、网络、存储等设计细节。

# 1 第一代 GPU 集群：`1.6w A100` (RSC)

Meta 很早就开始构建 AI 基础设施，但第一次对外分享是在 2022 年，介绍了我们的
**<mark><code>Research SuperCluster</code></mark>**
（[RSC](https://ai.meta.com/blog/ai-rsc/)），它由 **<mark><code>1.6w 个 A100</code></mark>** GPU 组成。

RSC 支撑了 Meta 第一代先进 AI 模型的开发，在训练 **<mark><code>Llama/llama2</code></mark>**、
计算机视觉、NLP、语音识别、图像生成甚至编码等 AI 工作中发挥了重要作用。

# 2 第二代 GPU 集群：`2.4w H100`

> 精确数字是每个集群 **<mark><code>24,576</code></mark>** 张 H100 GPU。

我们的新一代 AI 集群充分吸收了 RSC 的成功和经验教训，这包括，

* 新集群致力于构建**<mark>端到端的 AI 系统</mark>**，特别强调**<mark>研究人员和开发人员的用户体验和工作效率</mark>**；
* 新集群能支持更大、更复杂的模型，为 GenAI 产品开发和 AI 研究的进步铺平了道路。

Meta 每天需要执行数以万亿计的 AI 任务，这就需要一个高度先进和灵活的基础设施。
我们**<mark>自研了大部分硬件、软件和网络 fabric</mark>**，使我们能进行端到端优化，确保数据中心的高效运行。

<p align="center"><img src="/assets/img/meta-ai-infra/Meta-24K-GenAi-Clusters-hero.webp" width="100%" height="100%"></p>
<p align="center">
左侧：<mark>计算机柜</mark>，包括 GPU 服务器机框，置顶交换机，fabric 交换机等等；右侧：<mark>存储机柜</mark>。
</p>

## 2.1 计算：`Grand Teton` GPU 主机

两个新集群都使用了 [Grand Teton](https://engineering.fb.com/2022/10/18/open-source/ocp-summit-2022-grand-teton/)，
这是 Meta 开发的**<mark>开放 GPU 硬件平台</mark>**，我们已经将其贡献给了开放计算项目（OCP）。

> 从 2015 年的 [Big Sur](https://engineering.fb.com/2015/12/10/ml-applications/facebook-to-open-source-ai-hardware-design/) 平台开始，
> 我们就一直在开放设计我们的 GPU 硬件平台。

Grand Teton 实物图如下，

<p align="center"><img src="/assets/img/meta-ai-infra/Meta-Grand-Teton.webp" width="90%" height="90%"></p>
<p align="center"><a href="https://engineering.fb.com/2022/10/18/open-source/ocp-summit-2022-grand-teton/">Image Source</a></p>

* 将 CPU 机头、GPU、交换机同步系统、电源等等集成到一个机框中，以获得更好的整体性能；
* 提供了快速可扩展性和灵活性，设计简化，可以快速部署到数据中心，并易于维护和扩展。

结合 [Open Rack](https://engineering.fb.com/2022/10/18/open-source/ocp-summit-2022-grand-teton/) 电源和机架架构
等其他内部创新，我们能为 Meta 当前和未来应用程序快速量身定制新集群。

## 2.2 网络

两个集群使用了不同的网络方案，但都是 **<mark><code>400Gbps</code></mark>** 接入。

### 2.2.1 集群一：400Gbps RoCE + 自研交换机

基于 RoCE 网络，使用的交换机包括

* 自研置顶交换机（**<mark><code>TOR</code></mark>**）[Wedge400](https://engineering.fb.com/2021/11/09/data-center-engineering/ocp-summit-2021/)
  / [Arista 7800](https://www.arista.com/assets/data/pdf/Datasheets/7800R3-Data-Sheet.pdf) ，
* 自研**<mark>模块化交换机</mark>** [Minipack2](https://engineering.fb.com/2021/11/09/data-center-engineering/ocp-summit-2021/)。

    * Minipack/Minipack2 在组网中能承担多种角色，例如作为 Spine 交换机，
    * 第一代 Minipack：[(译) 重新设计 Facebook 的数据中心网络（2019）]({% link _posts/2020-06-20-facebook-f16-minipack-zh.md %})。
    * 更早一点的数据中心网络：[(译) 数据中心 Fabric：Facebook 的下一代数据中心网络（2014）]({% link _posts/2020-06-14-facebook-f4-data-center-fabric-zh.md %})。

### 2.2.2 集群二：400Gbps InfiniBand

使用 NVIDIA Quantum2 InfiniBand fabric。

### 2.2.3 小结

两个方案作对比，使我们能够评估 RoCE/IB 在大规模训练中的适用性和可扩展性，
为设计和构建更大规模的集群提供了宝贵经验。
目前这两个不同组网类型的集群都能够运行大型生成式 AI 任务
（例如在 **<mark><code>RoCE</code></mark>** 集群上训练 **<mark><code>Llama 3</code></mark>**），
而没有遇到网络瓶颈。

## 2.3 存储

存储在 AI 训练中扮演着重要角色，然而相关的讨论确非常少。

最近的发展趋势可以看出，GenAI 任务越来越多模态，需要处理大量图像、视频和文本，因此对高性能存储的需求越来越强烈。
理想的存储方案**<mark>除了提供良好的性能，还要做到低能耗</mark>**。

### 2.3.1 数据和 checkpoints 存储：FUSE + Tectonic

我们 AI 集群的数据和 checkpoint 的存储方案：

* 上层是一个自研的 Linux 用户空间文件系统（FUSE）
* 底层是 Meta 的名为 [Tectonic 的分布式存储解决方案](https://www.usenix.org/conference/fast21/presentation/pan)，它针对闪存（Flash media）进行了优化。

这个解决方案使得

* 数千个 GPU 能同步保存和加载 checkpoints（对任何存储解决方案来说都是一个[挑战](https://en.wikipedia.org/wiki/Thundering_herd_problem#:~:text=In%20computer%20science%2C%20the%20thundering,able%20to%20handle%20the%20event.)），
* 同时还提供了 **<mark><code>EB</code></mark>** 级存储系统所需的灵活性和高吞吐。

### 2.3.2 交互式调试：Parallel NFS

我们还与 [Hammerspace](https://hammerspace.com/software/) 合作开发了一个并行网络文件系统（NFS），
它使工程师能够使用**<mark>数千个 GPU 进行交互式调试</mark>**，
因为代码改动能立即同步到环境中的所有节点。

Tectonic 分布式存储加上 Hammerspace，既能满足快速迭代，又不会限制规模。

### 2.3.3 大容量 SSD + 定制每个机柜的服务器数量

无论是 Tectonic 还是 Hammerspace 方案，都基于 
[YV3 Sierra Point server platform](https://www.opencompute.org/documents/e1s-expansion-2ou-1s-server-design-specification-pdf)，
使用了我们在市场上能够买到的最新高容量 **<mark><code>E1.S SSD</code></mark>**。

除此之外，**<mark>每个机架塞的服务器数量</mark>**也进行了定制，以在服务器吞吐量、机架数量和能效之间取得一个平衡。

OCP 服务器就像乐高积木，使我们的存储层能够灵活扩展到未来更大 AI 集群的需求，而且不影响日常基础设施的使用和维护操作。

# 3 性能

## 3.1 原则：性能和易用性缺一不可

我们构建大规模 AI 集群的一个原则是，同时最大化性能和易用性，而不是为了一个而牺牲另一个。
这是训练最佳 AI 模型的重要基础。

测试**<mark>系统设计的扩展性</mark>**的最佳方法就是先构建出一个系统，然后不断优化它，并进行实际测试（模拟器有帮助，但作用有限）。
通过这个过程，我们比较了小集群和大集群的性能，定位瓶颈在哪里。
下图显示了当大量 GPU 相互通信时（at message sizes where roofline performance is expected）的 AllGather 性能（带宽归一化到 0-100），

<p align="center"><img src="/assets/img/meta-ai-infra/Meta-24K-GenAi-clusters-performance.webp" width="70%" height="70%"></p>
<p align="center">
small cluster performance (overall communication bandwidth and
utilization) reaches 90%+ out of the box, but an unoptimized large cluster
performance has very poor utilization, ranging from 10% to 90%. After we
optimize the full system (software, network, etc.), we see large cluster
performance return to the ideal 90%+ range.
</p>

## 3.2 大集群优化

与优化过的小型集群性能相比，我们的大集群一开始性能是比较差的。
为了解决这个问题，我们做了如下优化：

1. 改进 **<mark><code>job scheduler</code></mark>**，使其具备**<mark>网络拓扑感知能力</mark>**，这带来的好处：

    1. 延迟降低
    2. 转发到更上层网络（交换机）的流量减少。

2. 结合 NVIDIA **<mark><code>NCCL</code></mark>**，优化了**<mark>网络路由策略</mark>**，以实现最优的网络利用率。

以上两项优化使大集群的性能已经接近小集群。

除此之外，我们还

1. 与**<mark>训练框架和模型团队</mark>**密切合作，不断改进基础设施。例如，

    1. 支持 NVIDIA H100 GPU 的**<mark>新数据类型</mark>** FP8，这对训练性能大有帮助，
    2. 并行技术优化，
    3. 存储优化，

4. 意识到**<mark>可调试性</mark>**（debuggability）是大规模训练的主要挑战之一。
  在大规模情况下，定位到哪个 GPU 卡顿导致的整个训练作业变慢是非常困难的。
  为此，我们正在构建 desync debug 或分布式 flight recorder 之类的工具，**<mark>跟踪分布式训练的过程</mark>**，以更快识别问题。

5. 继续开发基础 AI 框架 **<mark><code>PyTorch</code></mark>**，使其能支持数万甚至数十万 GPU 进行训练。
  例如，我们已经定位到进程组初始化方面的几个瓶颈，将启动时间从有时的几小时减少到几分钟。

# 4 对 open AI innovation 的承诺

Meta 保持对 AI 软件和硬件开放创新的承诺，我们始终相信**<mark>开源硬件和软件</mark>**是帮助行业解决大规模问题的有用工具。
我们将

* 继续作为 OCP 的创始成员支持**<mark>开放硬件创新</mark>**，例如已经将 Grand Teton 和 Open Rack 等设计贡献给 OCP 社区。
* 作为 **<mark><code>PyTorch</code></mark>** 的最大和主要贡献者，继续推动这一 AI 软件框架的开发和普及。
* 继续致力于 AI 研究社区的开放创新。

    * 我们发起了开放[创新 AI 研究社区](https://ai.meta.com/llama/open-innovation-ai-research-community)，
      旨在深化我们对如何负责任地开发和共享 AI 技术（尤其是大模型）的理解。
    * 我们还推出了 [AI Alliance](https://ai.meta.com/blog/ai-alliance/)，这是一个由 AI 行业领先组织组成的小组，专注于在开放社区内加速负责任的 AI 创新。

我们的 AI 工作建立在开放科学和协力合作的哲学之上。

# 5 未来展望

本文介绍的两个 AI 训练集群是我们未来 AI 路线图的一部分。
预计到 2024 年底，Meta AI 基础设施建设将拥有 **<mark><code>35w 张 H100</code></mark>** GPU，总算力相当于约 **<mark><code>60w 张 H100</code></mark>**。

当前有效的方法可能不足以满足明天的需求，这也是为什么我们一直在各个方面不断评估和改进我们的基础设施，
包括物理硬件层、虚拟层、软件层以及更上面的业务层等等。
我们的目标是创建灵活可靠的系统，以支持日新月异的新模型和研究。

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
