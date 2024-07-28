---
layout    : post
title     : "Linux 时钟源之 TSC：软硬件原理、使用场景、已知问题（2024）"
date      : 2024-07-28
lastupdate: 2024-07-28
categories: linux kernel hardware
---

本文整理了一些 Linux 时钟源 `tsc` 相关的软硬件知识，在一些故障排查场景可能会用到。

<p align="center"><img src="/assets/img/linux-clock-source/freq-scale-up.gif" width="70%"/></p>
<p align="center">Fig. Scaling up crystal frequency for different components of a computer.
Image source <a href="https://www.youtube.com/watch?v=B7djs4zSbuU&t=150s">Youtube</a></p>

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----

# 1 计算机组件的运行频率

## 1.1 时钟源：**<mark><code>~20MHz</code></mark>** 的石英晶体谐振器（quartz crystal resonator）

石英晶体谐振器是利用**<mark>石英晶体（又称水晶）</mark>**的**<mark>压电效应</mark>**
来产生高精度**<mark>振荡频率</mark>**的一种电子器件。

> * 1880 年由雅克·居里与皮埃尔·居里发现压电效应。
> * 一战期间 保罗·朗之万首先探讨了石英谐振器在声纳上的应用。
> * 1917 第一个由晶体控制的电子式振荡器。
> * 1918 年贝尔实验室的 Alexander M. Nicholson 取得专利，虽然与同时申请专利的 Walter Guyton Cady 曾有争议。 
> * 1921 年 Cady 制作了第一个石英晶体振荡器。
>
> Wikipedia [石英晶体谐振器](https://zh.wikipedia.org/zh-cn/%E7%9F%B3%E8%8B%B1%E6%99%B6%E4%BD%93%E8%B0%90%E6%8C%AF%E5%99%A8)

现在一般长这样，焊在计算机**<mark>主板</mark>**上，

<p align="center"><img src="/assets/img/linux-clock-source/Crystal_oscillator_4MHz.jpg" width="40%"/></p>
<p align="center">Fig. A miniature 16 MHz quartz crystal enclosed in a hermetically sealed HC-49/S package, used as the resonator in a crystal oscillator.
Image source <a href="https://en.wikipedia.org/wiki/Crystal_oscillator">wikipedia</a></p>

受物理特性的限制，只有**<mark>几十 MHz</mark>**。

## 1.2 Clock generator：针对不同部分（内存、PCIe、CPU 等）倍频

计算机的内存、PCIe 设备、CPU 等等组件需要的工作频率不一样（主要原因之一是其他组件跟不上 CPU 的频率），
而且都**<mark>远大于几十 MHz</mark>**，因此需要对频率做提升。工作原理：

1. [What is a CPU clock physically?](https://cs.stackexchange.com/questions/153752/what-is-a-cpu-clock-physically)
2. [Wikipedia: Phase-locked_loop (PLL)](https://en.wikipedia.org/wiki/Phase-locked_loop)

有个视频解释地很形象，

<p align="center"><img src="/assets/img/linux-clock-source/freq-scale-up.gif" width="70%"/></p>
<p align="center">Fig. Scaling up crystal frequency for different components of a computer.
Image source <a href="https://www.youtube.com/watch?v=B7djs4zSbuU&t=150s">Youtube</a></p>

图中的 clock generator 是个专用芯片，也是**<mark>焊在主板上</mark>**，一般跟晶振挨着。

## 1.3 CPU 频率是如何从 `~20MHz` 提升到 **<mark><code>~3GHz</code></mark>** 的

本节稍微再开展一下，看看 CPU 频率是如何提升到我们常见的 ~3GHz 这么高的。

### 1.3.1 传递路径：最终连接到 CPU `CLK` 引脚

结合上面的图，时钟信号的**<mark>传递/提升路径</mark>**：

1. 晶振（**<mark><code>~20MHz</code></mark>**）
2. **<mark>主板上的 clock generator 芯片</mark>**
3. 北桥芯片
4. CPU

时钟信号连接到 CPU 的一个名为 **<mark><code>CLK</code></mark>** 的引脚。
两个具体的 CLK 引脚实物图：

* Intel 486 处理器（**<mark><code>1989</code></mark>**） 

    <p align="center"><img src="/assets/img/linux-clock-source/intel-486-pin-map.png" width="50%"/></p>
    <p align="center">Fig. Intel 486 pin map<a href="http://ps-2.kev009.com/eprmhtml/eprmx/12203.htm">Image Source</a></p>

    这种 CPU 引脚今天看来还是很简单的，CLK 在第三行倒数第三列。

* AMD SP3 CPU Socket (**<mark><code>2017</code></mark>**)

    EPYC 7001/7002/7003 系列用的这种。图太大了就不放了，见
    [SP3 Pin Map](https://en.wikichip.org/wiki/amd/packages/socket_sp3#Pin_Map)。

### 1.3.2 CPU 内部：还有一个 clock generator

现代 CPU 内部一般还有一个 **<mark><code>clock generator</code></mark>**，可以继续提升频率，
最终达到厂商宣传里的基频（base frequency）或标称频率（nominal frequency），例如 EPYC 6543 的 2795MHz。
这跟原始晶振频率比，已经提升了上百倍。

# 2 x86 架构的寄存器

介绍点必要的背景知识，有基础的可跳过。

## 2.1 通用目的寄存器

<p align="center"><img src="/assets/img/x86-asm-guide/x86-registers.png" width="50%" height="50%"></p>
<p align="center">Fig. 32-bit x86 general purpose registers [1]</p>

计算机执行的所有代码，几乎都是经由通用寄存器完成的。
进一步了解：[简明 x86 汇编指南（2017）]({% link _posts/2017-08-14-x86-asm-guide-zh.md %})。

## 2.2 特殊目的寄存器

如名字所示，用于特殊目的，一般也需要配套的特殊指令读写。大致分为几类：

* control registers
* debug registers
* **<mark><code>mode-specific registers (MSR)</code></mark>**

接下来我们主要看下 MSR 类型。

### 2.2.1 model-specific register (`MSR`)

[MSR](https://en.wikipedia.org/wiki/Model-specific_register) 是 x86 架构中的一组**<mark>控制寄存器</mark>**（control registers），
设计用于 debugging/tracing/monitoring 等等目的，以下是 **<mark><code>AMD</code></mark>** 的一些系统寄存器，
其中就包括了 MSR 寄存器们，来自 [AMD64 Architecture Programmer's Manual, Volume 3 (PDF)](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24594.pdf)，

<p align="center"><img src="/assets/img/linux-clock-source/amd-system-registers.png" width="60%"/></p>
<p align="center">Fig. AMD system registers, which include some MSR registers</p>

几个相关的指令：

* **<mark><code>RDMSR/WRMSR</code></mark>** 指令：读写 MSR registers；
* **<mark><code>CPUID</code></mark>** 指令：检查 CPU 是否支持某些特性。

> RDMSR/WRMSR 指令使用方式：
> 
> * 需要 priviledged 权限。
> * Linux `msr` 内核模块创建了一个伪文件 **<mark><code>/dev/cpu/{id}/msr</code></mark>**，用户可以读写这个文件。还有一个 `msr-tools` 工具包。

### 2.2.2 `MSR` 之一：`TSC`

今天我们要讨论的是 MSR 中与时间有关的一个寄存器，叫 TSC (Time Stamp Counter)。

# 3 TSC（时间戳计数器）

## 3.1 本质：X86 处理器中的一个 **<mark>特殊寄存器</mark>**

**<mark><code>Time Stamp Counter</code></mark>** (TSC) 是 X86 处理器
（Intel/AMD/...）中的一个 64-bit 特殊目的 **<mark>寄存器</mark>**，属于 MRS 的一种。
还是 AMD 编程手册中的图，可以看到 MSR 和 TSC 的关系：

<p align="center"><img src="/assets/img/linux-clock-source/amd-system-registers.png" width="60%"/></p>
<p align="center">Fig. AMD system registers, which include some MSR registers</p>

注意：在多核情况下（如今几乎都是多核了），每个物理核（processor）都有一个 TSC register，
或者说这是一个 **<mark><code>per-processor register</code></mark>**。

## 3.2 作用：记录 cpu 启动以来累计的 **<mark><code>cycles</code></mark>** 数量

前面已经介绍过，时钟信号经过层层提升之后，最终达到 CPU 期望的高运行频率，然后就会在这个频率上工作。

这里有个 **<mark><code>CPU cycles</code></mark>**（指令周期）的概念：
频率没经过一个周期（1Hz），CPU cycles 就增加 1 —— TSC 记录的就是**<mark>从 CPU 启动（或重置）以来的累计 cycles</mark>**。
这也呼应了它的名字：**<mark>时间戳计数器</mark>**。

## 3.3 实际：经常被当做（高精度）时钟用

根据以上原理，**<mark>如果</mark>** CPU 频率恒定且不存在 CPU 重置的话，

* TSC 记录的就是**<mark>系统启动以来的 cycles 数量</mark>**；
* cycles 可以**<mark>精确换算成时间</mark>**；
* 这个时间的**<mark>精度还非常高！</mark>**；
* 使用开销还很低（这涉及到操作系统和内核实现了）。

所以无怪乎 TSC 被大量**<mark>用户空间程序</mark>**当做**<mark>开销地高精度的时钟</mark>**。

### 3.3.1 使用代码

本质上用户空间程序只需要一条指令（`RDTSC`），就能读取这个值。非常简单的几行代码：

```c
unsigned long long rdtsc() {
    unsigned int lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((unsigned long long)hi << 32) | lo;
}
```

就能拿到当前时刻的 cpu cycles。所以统计耗时就很直接：

```c
    start = rdtsc();

    // business logic here

    end = rdtsc();
    elapsed_seconds = (end-start) / cycles_per_sec;
```

### 3.3.1 潜在问题

以上的假设是 TSC 恒定，随着 wall time 均匀增加。

如果 CPU 频率恒定的话（也就是没有超频、节能之类的特殊配置），cycles 就是以恒定速率增加的，
这时 TSC 确实能跟时钟保持同步，所以可以作为一种获取时间或计时的方式。
但接下来会看到，cycles 恒定这个前提条件如今已经很难满足了，内核也不推荐用 tsc 作为时间度量。

> 乱序执行会导致 RDTSC 的执行顺序与期望的顺序发生偏差，导致计时不准，两种解决方式：
>
> * 插入一个同步指令（a serializing instruction），例如 **<mark><code>CPUID</code></mark>**，强制前面的指令必现执行完，才能才执行 RDTSC；
> * 使用一个变种指令 RDTSCP，但这个指令只是对指令流做了部分顺序化（partial serialization of the instruction stream），并不完全可靠。

## 3.4 挑战：TSC 的准确性越来越难以保证

如果一台机器只有一个处理器，并且工作频率也一直是稳定的，那拿 TSC 作为计时方式倒也没什么问题。
但随着下面这些技术的引入，TSC 作为时钟就不准了：

* 多核处理器：意味着每个核上都有一个 TSC，如何保持这些 TSC 寄存器值的严格同步；
* 不同处理器的[温度差异也会导致 TSC 偏差](https://lwn.net/Articles/388188/)；
* 超线程：一个处理器上两个硬件线程（Linux 中看就是两个 CPU）；
* 超频、降频等等**<mark>功耗管理</mark>**功能：导致时钟不再是稳定的；
* CPU 指令乱序执行功能：获取 TSC 的指令的执行顺序和预期的可能不一致，导致计时不准；
* 休眠状态：恢复到运行状态时重置 TSC；

还有其他一些方面的挑战，都会导致**<mark>无法保证一台机器多个 CPU 的 TSC 严格同步</mark>**。

## 3.5 改进：引入 constant/invariant TSC

解决方式之一，是一种称为**<mark>恒定速率</mark>**（constant rate） TSC 的技术，

* 在 Linux 中，可以通过 `cat /proc/cpuinfo | grep constant_tsc` 来判断；
* 有这个 flag 的 CPU，TSC 以 CPU 的标称频率（nominal frequency）累积；超频或功耗控制等等导致的实际 CPU 时钟频率变化，不会影响到 TSC。

较新的 Intel、AMD 处理器都支持这个特性。

但是，constant_tsc 只是表明 **<mark>CPU 有提供恒定 TSC 的能力</mark>**，
并不表示实际工作 TSC 就是恒定的。后面会详细介绍。

## 3.5 小结：**<mark>计数器</mark>**（counter），而非时钟（clock）

从上面的内容已经可以看出，
TSC 如其名字“时间戳计数器”所说，确实本质上只是一个**<mark>计数器</mark>**，
记录的是 CPU 启动以来的 **<mark>cpu cycles 次数</mark>**。

虽然在很多情况下把它当时钟用，结果也是正确的，但这个是没有保证的，因为影响它稳定性的因素太多了 —— 不稳拿它计时也就不准了。

另外，它是一个 x86 架构的特殊寄存器，换了其他 cpu 架构可能就不支持，所以依赖 TSC 的代码**<mark>可移植性</mark>**会变差。

# 4 查看和监控 TSC 相关信息

以上几节介绍的基本都是硬件问题，很好理解。接下来设计到软件部分就复杂了，一部分原因是命名导致的。

## 4.1 Linux 系统时钟源（`clocksource`）配置

我们前面提到不要把 tsc 作为时钟来看待，它只是一个计数器。但另一方面，内核确实需要一个时钟，

* 内核自己的定时器、调度、网络收发包等等需要时钟；
* 用户程序也需要时间功能，例如 **<mark><code>gettimeofday() / clock_gettime()</code></mark>**。

在底层，内核肯定是要基于启动以来的计数器，这时 tsc 就成为它的备选之一（而且优先级很高）。

```shell
$ cat /sys/devices/system/clocksource/clocksource0/available_clocksource
tsc hpet acpi_pm

$ cat /sys/devices/system/clocksource/clocksource0/current_clocksource
tsc
```

### 4.1.1 `tsc`：优先

* **<mark>高精度</mark>**：基于 cycles，所以精度是几个 GHz，对应 `ns` 级别；
* **<mark>低开销</mark>**：跟内核实现有关。

### 4.1.2 `hpet`：性能开销太大

原理暂不展开，只说结论：相比 tsc，hpet 在很多场景会明显导致系统**<mark>负载升高</mark>**。所以能用 tsc 就不要用 hpet。

## 4.2 `turbostat` 查看实际 TSC 计数

前面提到用户空间程序写几行代码就能方便地获取 TSC 计数。所以对监控采集来说，还是很方便的。
我们甚至不需要自己写代码获取 TSC，一些内核的内置工具已经实现了这个功能，简单地执行一条 shell 命令就行了。

`turbostat` 是 Linux 内核自带的一个工具，可以查看包括 TSC 在内的很多信息。

> turbostat 源码在内核**<mark>源码</mark>**树中：[tools/power/x86/turbostat/turbostat.c](https://github.com/torvalds/linux/blob/v5.15/tools/power/x86/turbostat/turbostat.c)。

不加任何参数时，turbostat 会 **<mark><code>5s</code></mark>** 打印一次统计信息，内容非常丰富。
我们这里用精简模式，只打印每个 CPU 在过去 1s 的 TSC 频率和所有 CPU 的平均 TSC：

```shell
# sample 1s and only one time, print only per-CPU & average TSCs
$ turbostat --quiet --show CPU,TSC_MHz --interval 1 --num_iterations 1
CPU     TSC_MHz
-       2441
0       2445
64      2445
1       2445
```

## 4.3 监控

用合适的采集工具把以上数据送到监控平台（例如 Prometheus/VictoriaMetrics），就能很直观地看到 TSC 的状态。
例如下面是 1 分钟采集一次，每次采集过去 1s 内的平均 TSC，得到的结果：

<p align="center"><img src="/assets/img/linux-clock-source/monitoring-node-tsc.png" width="100%"/></p>
<p align="center">Fig. TSC runnning average of an AMD EPYC 7543 node</p>

# 5 TSC 若干坑

## 5.1 `constant_tsc`: a feature, not a runtime guarantee

AMD EPYC 7543 CPU 信息：

```shell
$ cat /proc/cpuinfo
...
processor       : 127
vendor_id       : AuthenticAMD
model name      : AMD EPYC 7543 32-Core Processor
cpu MHz         : 3717.449
flags           : fpu ... tsc msr rdtscp constant_tsc nonstop_tsc cpuid tsc_scale ...
```

flags 里面显式支持 `constant_tsc` 和 `nonstop_tsc`，所以按照文档的描述 TSC 应该是恒定的。

但是，看一下下面的监控，都是这款 CPU，机器来自两个不同的服务器厂商，

<p align="center"><img src="/assets/img/linux-clock-source/tsc-fluctuations.png" width="100%"/></p>
<p align="center">Fig. TSC fluctuations (delta of running average) of AMD EPYC 7543 nodes, from two server vendors</p>

可以看到，

* 联想和浪潮的 TSC 都有波动，
* 联想的偶尔波动非常剧烈（相对 base 2795MHz 偏离 16% 甚至更高）；
* 浪潮的相对较小（base 2445 MHz）。

这个波动可能有几方面原因，比如各厂商的 BIOS 逻辑，或者 SMI 中断风暴。

## 5.2 BIOS 设置致使 TSC 不恒定

### 5.2.1 TSC 寄存器是**<mark>可写</mark>**的！

TSC 可写，所以某些 BIOS 固件代码会修改 TSC 值，导致操作系统时序不同步（或者说不符合预期）。

### 5.2.2 BIOS SMI handler 通过修改 TSC 隐藏它们的执行

例如，2010 年内核社区的一个讨论 [x86: Export tsc related information in sysfs](https://lwn.net/Articles/388286/)
就提到，某些 BIOS SMI handler 会通过**<mark>修改 TSC value</mark>** 的方式来**<mark>隐藏它们的执行</mark>**。

> 为什么要隐藏？

### 5.2.3 服务器厂商出于功耗控制等原因在 BIOS 修改 TSC 同步逻辑

前面提到，恒定 TSC 特性只是说处理器提供了恒定的能力，但用不用这个能力，服务器厂商有非常大的决定权。

某些厂商的固件代码会在 TSC sync 逻辑中中修改 TSC 的值。
这种修改在固件这边没什么问题，但会破坏内核层面的时序视角，例如内核调度器工作会出问题。
因此，内核最后引入了一个 [patch](https://github.com/torvalds/linux/commit/cd7240c0b900eb6d690ccee088a6c9b46dae815a)
来处理 ACPI suspend/resume，以保证 TSC sync 机制在操作系统层面还是正常的，

```
x86, tsc, sched: Recompute cyc2ns_offset's during resume from sleep states

TSC's get reset after suspend/resume (even on cpu's with invariant TSC
which runs at a constant rate across ACPI P-, C- and T-states). And in
some systems BIOS seem to reinit TSC to arbitrary large value (still
sync'd across cpu's) during resume.

This leads to a scenario of scheduler rq->clock (sched_clock_cpu()) less
than rq->age_stamp (introduced in 2.6.32). This leads to a big value
returned by scale_rt_power() and the resulting big group power set by the
update_group_power() is causing improper load balancing between busy and
idle cpu's after suspend/resume.

This resulted in multi-threaded workloads (like kernel-compilation) go
slower after suspend/resume cycle on core i5 laptops.

Fix this by recomputing cyc2ns_offset's during resume, so that
sched_clock() continues from the point where it was left off during
suspend.
```

## 5.3 SMI 中断风暴导致 TSC 不稳

上一节提到，BIOS SMI handler 通过修改 TSC 隐藏它们的执行。如果有大量这种中断（可能是有 bug），
就会导致大量时间花在中断处理时，但又不会计入 TSC，最终导致系统出现卡顿等问题。

AMD 的机器比较尴尬，看不到 SMI 统计（试了几台 Intel 机器是能看到的），

```shell
$ turbostat --quiet --show CPU,TSC_MHz,SMI --interval 1 --num_iterations 1
CPU     TSC_MHz
-       2441
0       2445
64      2445
1       2445
...
```

## 5.4 VM TSC 不稳

例如

1. https://www.phoronix.com/news/AMD-Secure-TSC-Linux-Patches
1. http://oliveryang.net/2015/09/pitfalls-of-TSC-usage/

# 6 总结

本文整理了一些 TSC 相关的软硬件知识，在一些故障排查场景可能会用到。

# 参考资料

1. [简明 x86 汇编指南（2017）]({% link _posts/2017-08-14-x86-asm-guide-zh.md %})
2. [AMD64 Architecture Programmer's Manual, Volume 3 (PDF)](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24594.pdf)
3. [Linux 服务器功耗与性能管理（一）：CPU 硬件基础（2024）]({% link _posts/2024-02-15-linux-cpu-1-zh.md %})
4. [Pitfalls of TSC usage](http://oliveryang.net/2015/09/pitfalls-of-TSC-usage/), 2015
5. Wikipedia [MSR](https://en.wikipedia.org/wiki/Model-specific_register)
6. Wikipedia [TSC](https://en.wikipedia.org/wiki/Time_Stamp_Counter)
7. Wikipedia [Clock Generator](https://en.wikipedia.org/wiki/Clock_generator)

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
