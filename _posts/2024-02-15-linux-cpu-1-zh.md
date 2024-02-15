---
layout    : post
title     : "Linux 服务器功耗与性能管理（一）：CPU 硬件基础（2024）"
date      : 2024-02-15
lastupdate: 2024-02-15
categories: linux kernel
---

整理一些 Linux 服务器性能相关的 CPU 硬件基础及内核子系统知识。

水平有限，文中不免有错误或过时之处，请酌情参考。

* [Linux 服务器功耗与性能管理（一）：CPU 硬件基础（2024）]({% link _posts/2024-02-15-linux-cpu-1-zh.md %})
* [Linux 服务器功耗与性能管理（二）：几个内核子系统的设计（2024）]({% link _posts/2024-02-15-linux-cpu-2-zh.md %})
* [Linux 服务器功耗与性能管理（三）：cpuidle 子系统的实现（2024）]({% link _posts/2024-02-15-linux-cpu-3-zh.md %})
* [Linux 服务器功耗与性能管理（四）：监控、配置、调优（2024）]({% link _posts/2024-02-15-linux-cpu-4-zh.md %})
* [Linux 服务器功耗与性能管理（五）：问题讨论（2024）]({% link _posts/2024-02-15-linux-cpu-5-zh.md %})

----

* TOC
{:toc}

----

对于 Linux 机器，可以用 `lscpu`、`cat /proc/info` 等命令查看它的 CPU 信息，
比如下面这台机器，

```shell
$ lscpu
Architecture:          x86_64
CPU(s):                48
On-line CPU(s) list:   0-47
Thread(s) per core:    2
Core(s) per socket:    12
Socket(s):             2
NUMA node(s):          2
Model:                 63
Model name:            Intel(R) Xeon(R) CPU E5-2680 v3 @ 2.50GHz
NUMA node0 CPU(s):     0-11,24-35
NUMA node1 CPU(s):     12-23,36-47
...
```

看到有 48 个 CPU。
要理解这些 CPU **<mark>在物理上是怎么分布的</mark>**（layout），需要先熟悉几个概念。

# 1 拓扑

## 1.1 `Package`

如下图，package（直译为“封装”）是我们能直接在**<mark>主板上</mark>**看到的一个东西，

<p align="center"><img src="/assets/img/linux-cpu/cpu-package.jpg" width="45%" height="45%"></p>
<p align="center">Fig. CPU package
<a href="https://superuser.com/questions/324284/what-is-meant-by-the-terms-cpu-core-die-and-package"> Image source </a>
</p>

里面封装一个或多个处理器核心（称为 core 或 processor）。

## 1.2 `Core` (processor)

本文的 “core/processor” 都是指**<mark>硬件核心/硬件处理器</mark>**。一个 package 里面可能会包含多个处理器，如下图所示，

<p align="center"> <img src="/assets/img/linux-cpu/pkg-core.jpg" width="40%" height="40%"> </p>
<p align="center">Fig. Cores/processors in a package
<a href="https://superuser.com/questions/324284/what-is-meant-by-the-terms-cpu-core-die-and-package"> Image source </a>
</p>

或者从芯片视图看：

<p align="center"> <img src="/assets/img/linux-cpu/pkg-die.jpg" width="50%" height="50%"> </p>
<p align="center">Fig. Cores/processors in a package
<a href="https://superuser.com/questions/324284/what-is-meant-by-the-terms-cpu-core-die-and-package"> Image source </a>
</p>

## 1.3 超线程（Hyper-threading）/硬件线程（hardware thread）

<p align="center"> <img src="/assets/img/linux-cpu/hyper-threading.jpeg" width="60%" height="60%"> </p>
<p align="center">Fig. Hyper-threading
<a href="https://hackernoon.com/what-is-hyperthreading-and-how-do-you-enable-it-pa2k3784">Image source </a>
</p>

大部分 X86 处理器都支持超线程，也叫**<mark>硬件线程</mark>**。
如果一个 CORE 支持 2 个硬件线程， 那么启用超线程后，
这个 CORE 上面就有 **<mark>2 个在大部分情况下都能独立执行的指令流</mark>**（这 2 个硬件线程共享 L1 cache 等），
**<mark>操作系统能看到的 CPU 数量会翻倍</mark>**（相比 CORE 的数量），
每个 CPU 对应的不是一个 CORE，而是一个硬件线程/超线程（hyper-thread）。

## 1.4 (Logical) `CPU`

以上提到的 package、core/processor、hyper-threading/hardware-thread，都是**<mark>硬件概念</mark>**。

在任务调度的语境中，我们所说的 “CPU” 其实是一个**<mark>逻辑概念</mark>**。
例如，内核的任务调度是**<mark>基于逻辑 CPU</mark>** 来的，

* 为每个逻辑 CPU 分配一个任务队列（run queue），独立调度；
* 为每个逻辑 CPU 能独立加载指令并执行。

逻辑 CPU 的数量和分布跟 package/core/hyper-threading 有直接关系，
**<mark>一个逻辑 CPU 不一定对应一个独立的硬件处理器</mark>**。

下面通过一个具体例子来看下四者之间的关系。

## 1.5 Linux node 实探：`cpupower/hwloc/lstopo` 查看三者的关系

还是本文最开始那台 Intel CPU 机器，**<mark><code>Thread(s) per core:    2</code></mark>**
说明它启用了超线程/硬件线程。另外，我们通过工具 **<mark><code>cpupower</code></mark>** 来看下它的 CPU 分布，

```shell
$ cpupower monitor
              | Mperf              
 PKG|CORE| CPU| C0   | Cx   | Freq 
   0|   0|   0|  2.66| 97.34|  2494
   0|   0|  24|  1.89| 98.11|  2493
   0|   1|   1|  2.09| 97.91|  2494
   0|   1|  25|  1.77| 98.23|  2494
   ...
   0|  13|  11|  1.95| 98.05|  2493
   0|  13|  35|  2.30| 97.70|  2492
   1|   0|  12|  1.65| 98.35|  2493
   1|   0|  36|  1.58| 98.42|  2494
   ...
   1|  13|  23|  1.78| 98.22|  2494
   1|  13|  47|  5.07| 94.93|  2493
```

前三列：

1. `PKG`：package，

    **<mark>2 个独立的 CPU package</mark>**（`0~1`），对应上面的 NUMA；

2. `CORE`：**<mark>物理核心</mark>**/物理处理器

    每个 package 里 **<mark>14 个 CORE</mark>**（`0~13`）；

3. `CPU`：用户看到的 CPU，即我们上面所说的**<mark>逻辑 CPU</mark>**

    这台机器启用了超线程（hyperthreading），每个 CORE 对应两个 **<mark><code>hardware thread</code></mark>**，
    每个 hardware thread 最终呈现为一个**<mark>用户看到的 CPU</mark>**，因此最终是 48 个 CPU（`0~47`）。

也可以通过 `hw-loc` 查看**<mark>硬件拓扑</mark>**，里面能详细到不同 CPU 的 **<mark><code>L1/L2 cache</code></mark>** 关系：

```shell
$ hwloc-ls
Machine (251GB total)
  NUMANode L#0 (P#0 125GB)
    Package L#0 + L3 L#0 (30MB)                                    # <-- PKG 0
      L2 L#0 (256KB) + L1d L#0 (32KB) + L1i L#0 (32KB) + Core L#0  #   <-- CORE 0
        PU L#0 (P#0)                                               #     <-- Logical CPU 0  对应到这里
        PU L#1 (P#24)                                              #     <-- Logical CPU 24 对应到这里
      L2 L#1 (256KB) + L1d L#1 (32KB) + L1i L#1 (32KB) + Core L#1  #   <-- CORE 1
        PU L#2 (P#1)                                               #     <-- Logical CPU 1  对应到这里
        PU L#3 (P#25)                                              #     <-- Logical CPU 25 对应到这里
  ...
  NUMANode L#1 (P#1 126GB) + Package L#1 + L3 L#1 (30MB)
    L2 L#12 (256KB) + L1d L#12 (32KB) + L1i L#12 (32KB) + Core L#12
      PU L#24 (P#12)
      PU L#25 (P#36)
    ...
    L2 L#23 (256KB) + L1d L#23 (32KB) + L1i L#23 (32KB) + Core L#23
      PU L#46 (P#23)
      PU L#47 (P#47)
```

如无特殊说明，本文接下来的 “CPU” 都是指**<mark>逻辑 CPU</mark>**，
也就是 Linux 内核看到的 CPU。

# 2 频率

## 2.1 **<mark><code>P-State</code></mark>** (processor performance state)：处理器支持的 **<mark><code>voltage-freq</code></mark>** 列表

处理器可以工作在不同的频率，对应不同的电压（最终体现为功耗）。这些
voltage-frequency 组合就称为 P-State（**<mark>处理器性能状态</mark>**）。
比如下面这个
[P-State Table](https://en.wikichip.org/wiki/intel/frequency_behavior)

<table >
  <tbody>
    <tr>
    <th> Voltage </th>
    <th> Frequency </th></tr>
    <tr>
    <td> 1.21 V </td>
    <td> 2.8 GHz (HFM)
    </td></tr>
    <tr>
    <td> 1.18 V </td>
    <td> 2.4 GHz
    </td></tr>
    <tr>
    <td> 1.05 V </td>
    <td> 2.0 GHz
    </td></tr>
    <tr>
    <td> 0.96 V </td>
    <td> 1.6 GHz
    </td></tr>
    <tr>
    <td> 0.93 V </td>
    <td> 1.3 GHz
    </td></tr>
    <tr>
    <td> 0.86 V </td>
    <td> 600 MHz (LFM)
    </td></tr>
  </tbody>
</table>


这个 table 会保存在一个名为 **<mark><code>MSR</code></mark>** (model specific register)
的 read-only **<mark>寄存器中</mark>**。

## 2.2 LFM/HFM (low/high freq mode)：`p-state` 中的**<mark>最低和最高</mark>**频率

p-state table 中，

* 最低频率模式称为 Low Frequency Mode (LFM)，工作频率和电压不能比这个更低了。
* 最高频率模式称为 High Frequency mode (HFM)，工作频率和电压不能比这个更高了。

## 2.3 基频（**<mark><code>base frequency</code></mark>**）：市场宣传术，其实就是 p-state 中的**<mark>最高频率</mark>**

上面介绍了根据 p-state 的定义，处理器的最低（LF）和最高（HF）频率，这些都是很好理解的技术术语。

但在市场宣传中，厂商将 HF —— p-state 中的**<mark>上限频率</mark>** ——
称为基础频率或**<mark>基频</mark>**（Base Frequency），给技术人造成了极大的困惑。

## 2.4 超频（**<mark><code>overclocking</code></mark>**）：运行在比**<mark>基频更高</mark>**的频率

既然敢将 HF 称为基频，那处理器（至少在某些场景下）肯定能工作在更高的频率。
根据 [wikipedia](https://en.wikipedia.org/wiki/Overclocking)，

* 处理器厂商出于功耗、散热、稳定性等方面的原因，会给出一个**<mark>官方认证的最高稳定频率</mark>**
  （clock rate certified by the manufacturer）但这个频率可能不是处理器的物理极限（最高）频率。
  厂商承诺在这个最高稳定频率及以下可以长时间稳定运行，但超出这个频率，
  有的厂商提供有限保证，有的厂商完全不保证。
* 工作在比**<mark>处理器厂商认证的频率</mark>**更高的频率上，就称为超频（overclocking）；

结合我们前面的术语，这里说的“官方认证的最高稳定频率”就是基频（HF），
**<mark>工作在基频以上</mark>**的场景，就称为超频。比如基频是 2.8GHz，超频到 3.0GHz。

## 2.5 **<mark><code>Intel Turbo</code></mark>**（睿频） 或 **<mark><code>AMD PowerTune</code></mark>**：动态超频

Turbo 是 Intel 的技术方案，其他家也都有类似方案，基本原理都一样：根据负载动态调整频率 ——
但这句话其实只说对了一半 —— 这项技术的场景也非常明确，但宣传中经常被有意或无意忽略：
**<mark>在部分处理器空闲的情况下，另外那部分处理器才可能动态超频</mark>**。

所在官方文档说，我们会看到它一般都是写“能支持的最大单核频率”（**<mark><code>maximum single-core frequency</code></mark>**）
叫 Max Turbo Frequency，因为它们在设计上就**<mark>不支持所有核同时运行在这个最大频率</mark>**。

原因其实也很简单：
频率越高，功耗越高，散热越大。整个系统软硬件主要是围绕基频（HF）设计和调优的，
出厂给出的也是**<mark>和基频对应的功耗</mark>**（TDP，后面会介绍）。
另外，TDP 也是数据中心设计时的主要参考指标之一，所以大规模长时间持续运行在 TDP 之上，
考验的不止是处理器、主板、散热片这些局部的东西，数据中心全局基础设施都得跟上。

下面看个具体处理器 turbo 的例子。

### 2.5.1 Turbo 频率越高，能同时工作在这个频率的 CORE 数量越少

下面是一个 Intel 处理器官方参数，

<p align="center"><img src="/assets/img/linux-cpu/i9-9900k-turbo-freq.png" width="90%" height="90%"></p>
<p align="center">Turbo Freq and corresponding Active Cores
<a href="https://boxx.com/blog/hardware/intel%E2%80%99s-frequency-boosting-technologies-explained">Image source</a>
</p>

解释一下，

* 基频是 **<mark><code>3.6GHz</code></mark>**，
* 超频到 5GHz 时，最多只有 2 个核能工作在这个频率；
* 超频到 4.8GHz 时，最多只有 4 个核能工作在这个频率；
* 超频到 4.7GHz 时，最多只有 8 个核能工作在这个频率；

### 2.5.1 Turbo 高低跟 workload 使用的指令集（`SSE/AVX/...`）也有关系

能超到多少，跟跑的业务类型（或者说使用的指令集）也有关系，使用的指令集不同，能达到的最高频率也不同。
[比如](https://en.wikichip.org/wiki/intel/frequency_behavior)，

<table class="wikitable">
<tbody><tr>
<th> Mode </th>
<th> Example Workload </th>
<th> Absolute Guaranteed<br/>Lowest Frequency </th>
<th> Absolute<br/>Highest Frequency
</th></tr>
<tr>
<td> Non-AVX </td>
<td> SSE, light AVX2 Integer Vector (non-MUL), All regular instruction </td>
<td> Base Frequency </td>
<td> Turbo Frequency
</td></tr>
<tr>
<td> <a href="/w/index.php?title=x86/avx2&amp;action=edit&amp;redlink=1" class="new" title="x86/avx2 (page does not exist)">AVX2</a> Heavy </td>
<td> All AVX2 operations, light AVX-512 (non-FP, Int Vect non-MUL) </td>
<td> AVX2 Base </td>
<td> AVX2 Turbo
</td></tr>
<tr>
<td> <a href="/wiki/x86/avx-512" title="x86/avx-512">AVX-512</a> Heavy </td>
<td>  All heavy AVX-512 operations </td>
<td> AVX-512 Base </td>
<td> AVX-512 Turbo
</td></tr></tbody>
</table>

另外，在一些 CPU data sheet 中，还有一个所谓的 **<mark><code>all-core turbo</code></mark>**：
这是所有 core 同时超到同一个频率时，所能达到的最高频率。这个频率可能比 base 高一些，
但肯定比 max turbo frequency 低。例如，`Xeon Gold 6150`

* base	2.7 GHz
* all-core turbo 3.4 GHz
* turbo max 3.7GHz

### 2.5.3 p-state vs. freq 直观展示

<p align="center"><img src="/assets/img/linux-cpu/P-state-vs-speed-shift.jpg" width="85%" height="85%"></p>
<p align="center"><a href="https://www.thomas-krenn.com/en/wiki/Processor_P-states_and_C-states">Image Source</a>
</p>


## 2.6 Linux node `lscpu/procinfo` 实际查看各种频率

老版本的 `lscpu` 能看到三个频率指标：

```shell
$ lscpu
...
Model name:            Intel(R) Xeon(R) CPU E5-2680 v3 @ 2.50GHz
CPU MHz:               2494.374   # 实际运行频率，但不准，因为每个 CORE 可能都运行不同频率
CPU max MHz:           2500.0000  # max turbo freq
CPU min MHz:           1200.0000  # p-state low-freq
```

新版本的 `lscpu` 去掉了 `CPU MHz` 这个字段，每个 CORE 都可能工作在不同频率，
这种情况下这个字段没什么意义。要看每个 CORE/CPU 的实时工作频率，

```shell
# CPU info: Intel(R) Xeon(R) Gold 5318Y CPU @ 2.10GHz
$ cat /proc/cpuinfo | egrep '(processor|cpu MHz)'
processor       : 0
cpu MHz         : 2100.000
processor       : 1
cpu MHz         : 2100.000
processor       : 2
cpu MHz         : 2100.000
processor       : 3
cpu MHz         : 2600.000
...
processor       : 51
cpu MHz         : 2100.000
...
```

* 一共 96 个 CPU，回忆前面，这里的 CPU 本质上都是**<mark>硬件线程</mark>**（超线程），并不是独立 CORE；
* 从这台机器的输出看，同属一个 CORE 的**<mark>两个硬件线程</mark>**（CPU 3 & CPU 51）**<mark>可以工作在不同频率</mark>**。

# 3 功耗

## 3.1 **<mark><code>TDP</code></mark>** (Thermal Design Power)：Base Freq 下的额定功耗

TDP 表示的处理器运行在基频时的平均功耗（average power）。

这就是说，超频或 turbo 之后，处理器的功耗会比 TDP 更大。
具体到实际，需要关注功耗、电压、电流、出风口温度等等指标。
这些内容后面再专门讨论。

## 3.2 Turbo 和功耗控制架构

以 AMD 的 turbo 技术为例：

<p align="center"><img src="/assets/img/linux-cpu/AMD_PowerTune_Bonaire.png" width="70%" height="70%"></p>
<p align="center">Fig. Architecture of the PowerTune version
<a href="https://en.wikipedia.org/wiki/AMD_PowerTune"> Image source </a>
</p>

# 4 BIOS

服务器启动过程中的硬件初始化，可以配置一些硬件特性。运行在内核启动之前。
