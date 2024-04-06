---
layout    : post
title     : "GPU 进阶笔记（三）：华为 NPU/GPU 演进（2024）"
date      : 2024-01-01
lastupdate: 2024-01-01
categories: gpu ai
---

记录一些平时接触到的 GPU 知识。由于是笔记而非教程，因此内容不求连贯，有基础的同学可作查漏补缺之用。

水平有限，文中不免有错误或过时之处，请酌情参考。

* [GPU 进阶笔记（一）：高性能 GPU 服务器硬件拓扑与集群组网（2023）]({% link _posts/2023-09-16-gpu-advanced-notes-1-zh.md %})
* [GPU 进阶笔记（二）：华为昇腾 910B GPU 相关（2023）]({% link _posts/2023-09-16-gpu-advanced-notes-2-zh.md %})
* [GPU 进阶笔记（三）：华为 NPU (GPU) 演进（2024）]({% link _posts/2023-09-16-gpu-advanced-notes-3-zh.md %})

----

* TOC
{:toc}

----

本文内容都来自公开资料，仅供个人了解参考。
AI 相关的东西现在迭代非常快，所以部分内容可能已经过时，请注意甄别。

# 1 术语

CPU/GPU/NPU 等等都是硬件芯片，简单来说，晶体管既可以用来实现**<mark>逻辑控制</mark>**单元，
也可以用来实现**<mark>运算</mark>**单元（算力）。
在芯片总面积一定的情况下，就看控制和算力怎么分。

* CPU：通用目的处理器，重逻辑控制；
* GPU：通用目的**<mark>并行处理器</mark>**（GPGPU），图形处理器；
* NPU：**<mark>专用处理器</mark>**，相比 CPU/GPU，擅长执行更具体的计算任务。

## 1.1 CPU

大部分芯片面积都用在了逻辑单元，因此逻辑控制能力强，算力弱（相对）。

## 1.2 GPU

大部分芯片面积用在了计算单元，因此并行计算能力强，但逻辑控制弱。
适合图像渲染、矩阵计算之类的并行计算场景。作为**<mark>协处理器</mark>**，
需要在 CPU 的指挥下工作，

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/gpu-workflow.webp" width="45%" height="45%"></p>
<p align="center">Image Source [8]</p>

## 1.3 NPU / TPU

也是协处理器。在 wikipedia 中没有专门的 NPU (Neural Processing Unit) 页面，而是归到
[<mark>AI Processors</mark>](https://en.wikipedia.org/wiki/AI_accelerator) 大类里面，
指的是一类**<mark>特殊目的硬件加速器</mark>**，更接近 ASIC，硬件实现神经网络运算，
比如张量运算、卷积、点积、激活函数、多维矩阵运算等等[7]。

> 如果还不清楚什么是神经网络，可以看看
> [<mark>以图像识别为例，关于卷积神经网络（CNN）的直观解释</mark>（2016）]({% link _posts/2023-06-11-cnn-intuitive-explanation-zh.md %})。

在这些特殊任务上，比 CPU/GPU 这种通用处理器**<mark>效率更高，功耗更小，响应更快</mark>**
（比如一个时钟周期内可以完成几十万个乘法运算），
因此适合用在手机、边缘计算、物联网等等场景。

TPU：这里特制 Google 的 Tensor Processing Unit，目的跟 NPU 差不多。
[11] 对 TPU 和 GPU 的使用场景区别有一个**<mark>非常形象的比喻</mark>**：

> 如果外面下雨了，你其实并不需要知道**<mark>每秒到底有多少滴雨</mark>**，
> 而只要知道**<mark>雨是大还是小</mark>**。
> 与此类似，神经网络通常不需要 16/32bit 浮点数做精确计算，可能 8bit 整型预测的精度就足以满足需求了。

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/floor-plan-of-tpu-die.png" width="50%" height="50%"></p>
<p align="center">Floor Plan of Google TPU die(yellow = compute, blue = data, green = I/O, red = control) [11]</p>

## 1.4 小结

GPU 已经从最初的图像渲染和通用并行计算，逐步引入越来越多的神经网络功能
（比如 Tensor Cores、Transformer）；
另一方面，NPU 也在神经网络的基础上，开始引入越来越强大的通用计算功能，
所以这俩有双向奔赴的趋势。

# 2 华为 DaVinci 架构：一种方案覆盖所有算力场景

## 2.1 场景、算力需求和解决方案

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/ai-chip-ranges.png" width="70%" height="70%"></p>
<p align="center">不同<mark>算力场景</mark>下，算力需求（TFLOPS）和内存大小（GB）的对应关系 [1]</p>

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/huawei-chip-ranges.png" width="70%" height="70%"></p>
<p align="center">华为的解决方案：<mark>一种架构（DaVinci），覆盖所有场景</mark> [1]</p>

用在几个不同产品方向上，

1. 手机处理器，自动驾驶芯片等等
2. 专门的 AI 处理器，使用场景类似于 GPU

## 2.2 Ascend NPU 设计

2017 年发布了自己的 NPU 架构，[2] 详细介绍了 DaVinci 架构的设计。
除了支持传统标量运算、矢量运行，还引入了 3D Cube 来加速矩阵运算，

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/different-dimension-computation.png" width="60%" height="60%"></p>
<p align="center">Image Source [2]</p>

单位芯片面积或者单位功耗下，性能比 CPU/GPU 大幅提升：

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/paper-table3.png" width="50%" height="50%"></p>
<p align="center">Image Source [2]</p>

下面看看实际使用场景和产品系列。

# 3 路线一：NPU 用在手机芯片（Mobile AP SoC）

现代手机芯片不再是单功能处理器，而是集成了多种芯片的一个
片上系统（**<mark><code>SoC</code></mark>**），
华为 NPU 芯片就**<mark>集成到麒麟手机芯片内部</mark>**，随着华为 Mate 系列高端手机迭代。

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/soc.jpg" width="30%" height="30%"></p>
<p align="center">Image Source [7]</p>

比如，一些典型的功能划分 [7]：

* CPU 主处理器，运行 app；
* GPU 渲染、游戏等；
* NPU 图像识别、AI 应用加速。

Mate 系列手机基本上是跟 Kirin 系列芯片一起成长的，早期的手机不是叫 “Mate XX”，
而是 “Ascend Mate XX”，从中也可以看出跟昇腾（Ascend）的渊源。

## 3.1 `Kirin 970`，<mark>2017</mark>, Mate 10 系列手机

据称是第一个手机内置的 AI 处理器（NPU）[3]。
在 AI 任务上（比如手机上输入文字搜图片，涉及大批量图片识别）比 CPU 快 25~50 倍。

* **<mark><code>10nm</code></mark>**，台积电代工
* CPU 8-core with a clockspeed of uP to 2.4GHz i.e. 4 x Cortex A73 at 2.4GHz + 4 x Cortex 53 at 1.8GHz
* **<mark><code>GPU</code></mark>** 12-core Mali G72MP12 ARM GPU
* **<mark><code>NPU</code></mark>** **<mark><code>1.92 TFLOPs FP16</code></mark>**

## 3.2 `Kirin 990 5G`，<mark>2019</mark>, Mate 30 系列手机

Kirin 990 包含了 D-lite 版本的 NPU [1]：

* World's st 5G SoC Poweed by 7nm+ EUV
* World's 1st 5G NSA & SA Flagship SoC
* Wolrd's 1st 16-Core Mali-G76 **<mark><code>GPU</code></mark>**
* World's 1st Big-Tiny Core Architechture **<mark><code>NPU</code></mark>**

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/kirin-990-5g-chip-logical-layout.png" width="80%" height="80%"></p>
<p align="center">麒麟 990 5G 芯片逻辑拓扑 [1]</p>

一些硬件参数 [1,4]：

* 台积电 **<mark><code>7nm+</code></mark>** 工艺
* CPU 8-Core
* **<mark><code>NPU</code></mark>** 2+1 Core
* **<mark><code>GPU</code></mark>** 16-core Mali-G76（ARM GPU）

* **<mark><code>GPU</code></mark>** 16-core Mali-G76 (ARM GPU)
* **<mark><code>NPU</code></mark>**
    * HUAWEI Da Vinci Architecture,
    * 2x Ascend Lite + 1x Ascend Tiny
* 2G/3G/4G/5G Modem
* LPDDR 4X
* 4K HDR Video

## 3.3 `Kirin 9000 5G`，<mark>2020</mark>，Mate 40 系列手机

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/Kirin-9000-and-9000E.png" width="80%" height="80%"></p>
<p align="center"><a href="https://www.phonearena.com/news/huawei-kirin-9000-5nm-5g-soc_id128007">Image Source</a></p>

* 台积电 **<mark><code>5nm</code></mark>** 工艺
* GPU 24-core Mali-G78, Kirin Gaming+ 3.0
* **<mark><code>NPU</code></mark>**
    * HUAWEI **<mark>Da Vinci Architecture 2.0</mark>** 第二代架构
    * 2x Ascend Lite + 1x Ascend Tiny

这个是台积电 **<mark><code>5nm</code></mark>** 工艺 [5]，然后就被美国卡脖子了。
所以 Mate 50 系列用的高通处理器，Mate 60 系列重新回归麒麟处理器。

## 3.4 `Kirin 9000s`，<mark>2023</mark>，Mate 60 系列手机

王者低调回归，官网没有资料。

据各路媒体分析，是中芯国际 **<mark><code>7+nm</code></mark>** 工艺，比上一代 9000 落后一些，
毕竟制程有差距，看看国外媒体的副标题 [6]：

> **<mark><code>It's tough to beat a 5nm processor with a 7nm chip</code></mark>**.

Wikipedia 提供的参数 [10]：

* SMIC 7nm FinFET
* CPU HiSilicon Taishan microarchitecture Cortex-A510
* GPU Maleoon 910 MP4
* NPU 有，但是没提

## 3.5 小结

手机芯片系列先到这里，接下来看看作为独立卡使用的 NPU 系列。

# 4 路线二：NPU 用作推理/训练芯片（Ascend AI Processor）

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/ascend-310-910.png" width="65%" height="65%"></p>
<p align="center">两个产品：301 低功耗；910 高算力。</p>

设计见 paper [2]。

## 4.1 产品：加速卡 Atlas 系列

型号 Atlas 200/300/500/...，包括了 NPU 在内的 SoC，用于 AI 推理和训练。

## 4.2 `Ascend 310`，<mark>2019</mark>，推理

### Spec

用的是 D-mini version：

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/ascend-310-spec.png" width="90%" height="90%"></p>

纸面算力基本对标 NVIDIA **<mark><code>T4</code></mark>** [9]。

## 4.3 `Ascend 910`, <mark>2019</mark>，训练

### 4.3.1 Spec & Performance, vs. Google TPU

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/ascend-910-logical-topo.png" width="100%" height="100%"></p>
<p align="center">Image Source [1]</p>

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/ascend-910-spec-performance.png" width="80%" height="80%"></p>
<p align="center">Image Source [1]</p>

### 4.3.2 计算集群

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/ascend-910-server.png" width="50%" height="50%"></p>
<p align="center">Image Source [1]</p>

<p align="center"><img src="/assets/img/huawei-npu-to-gpu/ascend-910-cluster.png" width="80%" height="80%"></p>
<p align="center">Image Source [1]</p>

## 4.4 `Ascend 910B`, <mark>2023</mark>

[GPU 进阶笔记（二）：华为 Ascend 910B GPU 相关（2023）]({% link _posts/2023-09-16-gpu-advanced-notes-2-zh.md %})。

# 参考资料

1. [DaVinci: A Scalable Architecture for Neural Network Computing](https://www.cmc.ca/wp-content/uploads/2020/03/Zhan-Xu-Huawei.pdf)，huawei, 2020
2. [Ascend: a Scalable and Unified Architecture for Ubiquitous Deep Neural Network Computing](https://ieeexplore.ieee.org/abstract/document/9407221), HPCA, 2021
3. [Huawei Unveils The Kirin 970, The World’s First Processor With A Dedicated NPU](https://www.gizmochina.com/2017/09/02/huawei-unveils-kirin-970-worlds-first-processor-dedicated-npu/), 2017
4. [Kirin 990 5G Data Sheet](https://www.hisilicon.com/en/products/Kirin/Kirin-flagship-chips/Kirin-990-5G), hisilicon.com
5. [Kirin 9000 Data Sheet](https://www.hisilicon.com/en/products/Kirin/Kirin-flagship-chips/Kirin-9000), hisilicon.com
6. [Huawei's sanctions-evading Kirin 9000S processor tested: significantly behind its Kirin 9000 predecessor that used TSMC tech](https://www.tomshardware.com/pc-components/chipsets/huaweis-sanctions-evading-kirin-9000s-tested-significantly-behind-kirin-9000-with-tsmc-tech)，2023
7. [Neural Processing Unit (NPU) Explained](https://www.utmel.com/blog/categories/integrated%20circuit/neural-processing-unit-npu-explained), 2021
8. [AI 101: GPU vs. TPU vs. NPU](https://www.backblaze.com/blog/ai-101-gpu-vs-tpu-vs-npu/), 2023
9. [GPU Performance (Data Sheets) Quick Reference (2023)]({% link _posts/2023-10-25-gpu-data-sheets.md %})
10. [Wikipedia: HiSilicon](https://en.wikipedia.org/wiki/HiSilicon), wikipedia.com
11. [An in-depth look at Google’s first Tensor Processing Unit (TPU)](https://cloud.google.com/blog/products/ai-machine-learning/an-in-depth-look-at-googles-first-tensor-processing-unit-tpu), Google Cloud Blog, 2017

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
