---
layout    : post
title     : "GPU 进阶笔记（四）：NVIDIA GH200 芯片、服务器及集群组网（2024）"
date      : 2024-08-19
lastupdate: 2024-08-19
categories: ai gpu
---

记录一些平时接触到的 GPU 知识。由于是笔记而非教程，因此内容不求连贯，有基础的同学可作查漏补缺之用。

* [GPU 进阶笔记（一）：高性能 GPU 服务器硬件拓扑与集群组网（2023）]({% link _posts/2023-09-16-gpu-advanced-notes-1-zh.md %})
* [GPU 进阶笔记（二）：华为昇腾 910B GPU 相关（2023）]({% link _posts/2023-09-16-gpu-advanced-notes-2-zh.md %})
* [GPU 进阶笔记（三）：华为 NPU (GPU) 演进（2024）]({% link _posts/2023-09-16-gpu-advanced-notes-3-zh.md %})
* [GPU 进阶笔记（四）：NVIDIA GH200 芯片、服务器及集群组网（2024）]({% link _posts/2023-09-16-gpu-advanced-notes-4-zh.md %})

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----

# 1 传统原厂 GPU 服务器：`Intel/AMD x86 CPU` + `NVIDIA GPU`

2024 之前，不管是 NVIDIA 原厂还是第三方服务器厂商的 NVIDIA GPU 机器，都是以 x86 CPU 机器为底座，
GPU 以 PCIe 板卡或 8 卡模组的方式连接到主板上，我们在第一篇中有过详细介绍，

<p align="center"><img src="/assets/img/gpu-notes/8x-a100-node-hw-topo.png" width="90%" height="90%"></p>
<p align="center">典型 8 卡 A100 主机硬件拓扑</p>

这时 CPU 和 GPU 是独立的，服务器厂商只要买 GPU 模组（例如 8*A100），都可以自己组装服务器。
至于 Intel/AMD CPU 用哪家，就看性能、成本或性价比考虑了。

# 2 新一代原厂 GPU 服务器：`NVIDIA CPU` + `NVIDIA GPU`

随着 2024 年 NVIDIA GH200 芯片的问世，NVIDIA 的 **<mark>GPU 开始自带 CPU</mark>** 了。

* **<mark>桌面计算机时代</mark>**：CPU 为主，GPU（显卡）为辅，CPU 芯片中可以集成一块 GPU 芯片，
  叫**<mark>集成显卡</mark>**；
* AI 数据中心时代：GPU 反客为主，CPU 退居次席，GPU 芯片/板卡中集成 CPU。

所以 NVIDIA 集成度越来越高，开始提供整机或整机柜。

## 2.1 CPU 芯片：Grace (`ARM`)

基于 **<mark><code>ARMv9</code></mark>** 设计。

## 2.2 GPU 芯片：Hopper/Blackwell/...

比如 Hopper 系列，先出的 H100-80GB，后面继续迭代：

1. **<mark><code>H800</code></mark>**：H100 的阉割版，
1. H200：H100 的升级版，
1. **<mark><code>H20</code></mark>**：H200 的阉割版，比 H800 还差，差多了。

算力对比：[GPU Performance (Data Sheets) Quick Reference (2023)]({% link _posts/2023-10-25-gpu-data-sheets.md %})

## 2.3 芯片产品（命名）举例

### 2.3.1 Grace CPU + Hopper 200 (H200) GPU：`GH200`

一张板子：

<p align="center"><img src="/assets/img/gpu-notes/gh200-chip-render.png" width="60%" height="60%"></p>
<p align="center">NVIDIA GH200 芯片（板卡）渲染图。左：Grace CPU 芯片；右：Hopper GPU 芯片 [2]</p>

### 2.3.2 Grace CPU + Blackwell 200 (B200) GPU：`GB200`

一个板子（模块），功耗太大，自带液冷：

<p align="center"><img src="/assets/img/gpu-notes/gb200-compute-tray.png" width="50%" height="50%"></p>
<p align="center">NVIDIA GB200 渲染图，一个模块包括 <mark>2 Grace CPU + 4 B200 GPU</mark>，另外<mark>自带了液冷模块</mark>。 [3]</p>

72 张 B200 组成一个原厂机柜 NVL72：

<p align="center"><img src="/assets/img/gpu-notes/gb200-nvl72-rack.png" width="50%" height="50%"></p>
<p align="center">NVIDIA GB200 NVL72 机柜。 [3]</p>

# 3 GH200 服务器内部设计

## 3.1 GH200 芯片逻辑图：`CPU+GPU+RAM+VRAM` 集成到单颗芯片

<p align="center"><img src="/assets/img/gpu-notes/gh200-chip-logical.png" width="80%" height="80%"></p>
<p align="center">NVIDIA GH200 芯片（单颗）逻辑图。[2] </p>

### 3.1.1 核心硬件

如上图所示，一颗 GH200 超级芯片集成了下面这些核心部件：

1. 一颗 NVIDIA Grace CPU；
2. 一颗 NVIDIA H200 GPU；
3. 最多 480GB CPU 内存；
3. 96GB 或 144GB GPU 显存。

### 3.1.2 芯片硬件互连

1. CPU 通过 4 个 PCIe Gen5 x16 连接到主板，

    * 单个 PCIe Gen5 x16 的速度是双向 128GB/s，
    * 所以 4 个的总速度是 512GB/s；

2. CPU 和 GPU 之间，通过 NVLink® Chip-2-Chip (**<mark><code>NVLink-C2C</code></mark>**) 技术互连，

    * 900 GB/s，比 PCIe Gen5 x16 的速度快 7 倍；

3. GPU 互连（同主机扩跨主机）：18x NVLINK4

    * 900 GB/s

NVLink-C2C 提供了一种 NVIDIA 所谓的“memory coherency”：内存/显存一致性。好处：

* 内存+显存高达 624GB，对用户来说是统一的，可以不区分的使用；提升开发者效率；
* CPU 和 GPU 可以同时（concurrently and transparently）访问 CPU 和 GPU 内存。
* GPU 显存可以超分（oversubscribe），不够了就用 CPU 的内存，互连带宽够大，延迟很低。

下面再展开看看 CPU、内存、GPU 等等硬件。

## 3.2 CPU 和内存

### 3.2.1 `72-core ARMv9` CPU

* **<mark><code>72-core</code></mark>** Grace CPU (**<mark><code>Neoverse V2 Armv9 core</code></mark>**)

### 3.2.2 `480GB LPDDR5X` (Low-Power DDR) 内存

* 最大支持 480GB LPDDR5X 内存；
* 500GB/s per-CPU memory bandwidth。

参考下这个速度在存储领域的位置：

<p align="center"><img src="/assets/img/practical-storage-hierarchy/storage-bandwidth-3.png" width="90%"/></p>
<p align="center">Fig. Peak bandwidth of storage media, networking, and distributed storage solutions. [1]</p>

### 3.2.3 三种内存对比：`DDR vs. LPDDR vs. HBM`

* **<mark>普通服务器</mark>**（绝大部分服务器）用的是 **<mark><code>DDR</code></mark>** 内存，通过主板上的 DIMM 插槽连接到 CPU，[1] 中有详细介绍；
* 1-4 代的 LPDDR 是对应的 1-4 代 DDR 的低功耗版，常用于手机等设备。
    * LPDDR5 是独立于 DDR5 设计的，甚至比 DDR5 投产还早；
    * 直接和 CPU 焊到一起的，不可插拔，不可扩展，成本更高，但好处是**<mark>速度更快</mark>**；
    * 还有个类似的是 GDDR，例如 RTX 4090 用的 GDDR。
* HBM 在第一篇中已经介绍过了；

下面列个表格对比三种内存的优缺点，注意其中的高/中/低都是三者相对而言的：

|     | DDR | LPDDR | HBM |
|:----|:----|:----|:----|
| 容量 | **<mark>大</mark>** | 中 | 小 |
| 速度 | **<mark>慢</mark>** | 中 | 快 |
| 带宽 | **<mark>低</mark>** | 中 | 高 |
| 可扩展性 | **<mark>好</mark>** | 差 | 差 |
| 可插拔 | 可 | 不可 | 不可 |
| 成本 | **<mark>低</mark>** | 中 | 高 |
| 功耗 | **<mark>高</mark>** | 中 | 低 |

更多细节，见 [1]。

例如，与 **<mark><code>8-channel DDR5</code></mark>**（目前高端 x86 服务器的配置）相比，
GH200 的 LPDDR5X 内存带宽高 **<mark><code>53%</code></mark>**，功耗还低 **<mark><code>1/8</code></mark>**。

## 3.3 GPU 和显存

### 3.3.1 H200 GPU

算力见下面。

### 3.3.2 显存选配

支持两种显存，二选一：

* 96GB HBM3
* 144GB HBM3e，4.9TB/s，比 H100 SXM 的带宽高 50%；

## 3.4 变种：`GH200 NVL2`，用 NVLINK 全连接两颗 `GH200`

在一张板子内放两颗 GH200 芯片，CPU/GPU/RAM/VRAM 等等都翻倍，而且两颗芯片之间是全连接。

例如，对于一台能插 8 张板卡的服务器，

* 用 GH200 芯片：CPU 和 GPU 数量 **<mark><code>8 * {72 Grace CPU, 1 H200 GPU}</code></mark>**
* 用 GH200 NVL2 变种：CPU 和 GPU 数量 **<mark><code>8 * {144 Grace CPU, 2 H200 GPU}</code></mark>**

## 3.5 `GH200` & `GH200 NVL2` 产品参数（算力）

<p align="center"><img src="/assets/img/gpu-notes/gh200-product-spec.png" width="80%" height="80%"></p>
<p align="center">NVIDIA GH200 产品参数。上半部分是 CPU、内存等参数，从 <mark>"FP64"</mark> 往下是 GPU 参数。[2] </p>

# 4 GH200 服务器及组网

两种服务器规格，分别对应 PCIe 板卡和 NVLINK 板卡。

## 4.1 NVIDIA MGX with GH200：原厂主机及组网

下图是单卡 node 的一种组网方式：

<p align="center"><img src="/assets/img/gpu-notes/gh200-mgx-cluster-topo.png" width="100%" height="100%"></p>
<p align="center">NVIDIA GH200 MGX 服务器组网。<mark>每台 node 只有一片 GH200 芯片，作为 PCIe 板卡，没有 NVLINK</mark>。[2] </p>

1. 每台 node 只有一片 GH200 芯片（所以只有一个 GPU），作为 PCIe 板卡，没有 NVLINK；
1. 每台 node 的网卡或加速卡 BlueField-3 (BF3) DPUs 连接到交换机；
1. 跨 node 的 GPU 之间没有直连，而是通过主机网络（走 `GPU->CPU-->NIC` 出去）的方式实现通信；
1. 适合 HPC workload、中小规模的 AI workload。

## 4.2 NVIDIA GH200 NVL32：原厂 32 卡机柜

通过 NVLINk 将 **<mark>32 个 GH200 芯片</mark>**全连接为一个逻辑 GPU 模块，所以叫 **<mark><code>NVL32</code></mark>**，

<p align="center"><img src="/assets/img/gpu-notes/gh200-nvl32-cluster-topo.png" width="100%" height="100%"></p>
<p align="center">NVIDIA GH200 NVL32 组网。[2] </p>

1. NVL32 模块实际形态是一个**<mark>机柜</mark>**；
    * 一个机柜能提供 **<mark><code>19.5TB</code></mark>** 内存+显存；
    * NVLink TLB 能让任意一个 GPU 访问这个机柜内的任意内存/显存；

      <p align="center"><img src="/assets/img/gpu-notes/gh200-nvl32-mem-access.png" width="80%" height="80%"></p>
      <p align="center">NVIDIA GH200 NVL32 中 3 种内存/显存访问方式。[2] </p>

    * Extended GPU Memory (EGM)
2. 多个机柜再通过网络互连，形成集群，适合超大规模 AI workload。

# 5 总结

本文粗浅地整理了一些 NVIDIA GH200 相关技术知识。

其他：

* [Grace CPU 的测评](https://chipsandcheese.com/2024/07/31/grace-hopper-nvidias-halfway-apu/)

# 参考资料

1. [Practical Storage Hierarchy and Performance: From HDDs to On-chip Caches（2024）]({% link _posts/2024-05-26-practical-storage-hierarchy.md %})
2. [NVIDIA GH200 Grace Hopper Superchip & Architecture](https://www.nvidia.com/en-us/data-center/grace-hopper-superchip/), datasheet, 2024
3. [NVIDIA GB200 NVL72 Delivers Trillion-Parameter LLM Training and Real-Time Inference](https://developer.nvidia.com/blog/nvidia-gb200-nvl72-delivers-trillion-parameter-llm-training-and-real-time-inference/), 2024

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
