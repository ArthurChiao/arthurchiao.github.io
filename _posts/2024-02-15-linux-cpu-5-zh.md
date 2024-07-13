---
layout    : post
title     : "Linux 服务器功耗与性能管理（五）：问题讨论（2024）"
date      : 2024-02-15
lastupdate: 2024-02-15
categories: linux kernel
---

整理一些 Linux 服务器性能相关的 CPU 硬件基础及内核子系统知识。

* [Linux 服务器功耗与性能管理（一）：CPU 硬件基础（2024）]({% link _posts/2024-02-15-linux-cpu-1-zh.md %})
* [Linux 服务器功耗与性能管理（二）：几个内核子系统的设计（2024）]({% link _posts/2024-02-15-linux-cpu-2-zh.md %})
* [Linux 服务器功耗与性能管理（三）：cpuidle 子系统的实现（2024）]({% link _posts/2024-02-15-linux-cpu-3-zh.md %})
* [Linux 服务器功耗与性能管理（四）：监控、配置、调优（2024）]({% link _posts/2024-02-15-linux-cpu-4-zh.md %})
* [Linux 服务器功耗与性能管理（五）：问题讨论（2024）]({% link _posts/2024-02-15-linux-cpu-5-zh.md %})

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----


# 1 `idle=poll` 的潜在风险

前面已经介绍过，`idle=poll` 就是强制处理器工作在 C0，保持最高性能。
但内核文档中好几个地方提示这样设置是有风险的，这里整理一下。

## 1.1. `5.15` 内核文档 "CPU Idle Time Management"

[CPU Idle Time Management](https://github.com/torvalds/linux/blob/v5.15/Documentation/admin-guide/pm/cpuidle.rst)：

```
using ``idle=poll`` is somewhat drastic in many cases, as preventing idle
CPUs from saving almost any energy at all may not be the only effect of it.

For example, on Intel hardware it effectively prevents CPUs from using
P-states (see |cpufreq|) that require any number of CPUs in a package to be
idle, so it very well may hurt single-thread computations performance as well as
energy-efficiency.  Thus using it for performance reasons may not be a good idea
at all.]
```

这段写的比较晦涩，基于本系列前几篇的基础，尝试给大家翻译一下：

`idle=poll` 除了功耗高，还有其他后果；例如，

* 在 Intel 处理器上，这会使得 p-states 功能无法正常工作，
  因而**<mark>无法将同属一个 PKG 的那些空闲 CPU 的功耗降低</mark>**，
  这不是省不省电本身的问题：**<mark>空闲的 CPU 不降低功耗，其他的 CPU 可能就无法超频！</mark>**
* 所以对于单线程的任务来说（更宽泛的来说，是这样一类场景下：系统比较空闲，只有少量线程在执行），
  **<mark>性能反而无法达到最优</mark>**，因为总功耗限制下，少量在工作的 CPU 无法超频（turbo）。

另外，这个文档是 Intel 的人写的，但看过超频原理就应该明白，这个问题不仅限于 Intel CPU。

## 1.2  `5.15` 内核文档 "NO_HZ: Reducing Scheduling-Clock Ticks"

[NO_HZ: Reducing Scheduling-Clock Ticks](https://github.com/torvalds/linux/blob/v5.15/Documentation/timers/no_hz.rst)：

```
Known Issues
    d.    On x86 systems, use the "idle=poll" boot parameter.
        However, please note that use of this parameter can cause
        your CPU to overheat, which may cause thermal throttling
        to degrade your latencies -- and that this degradation can
        be even worse than that of dyntick-idle.  Furthermore,
        this parameter effectively disables Turbo Mode on Intel
        CPUs, which can significantly reduce maximum performance.
```

这是归类到了**<mark>已知问题</mark>**，写的比前一篇清楚多了：

1. 导致 CPU 过热，延迟可能上升，可能比 tickless 模式（dyntick）的延迟还大；
2. 更重要的，`idle=poll` effectively **<mark>禁用了 Intel Turbo Mode</mark>**，
  也就是无法超频到 base frequency 以上，因此**<mark>峰值性能显著变差</mark>**。

## 1.3 `5.15` 内核文档 "AMD64 Specific Boot Options"

[AMD64 Specific Boot Options](https://github.com/torvalds/linux/blob/v5.15/Documentation/x86/x86_64/boot-options.rst)：

这个是启动项说明，里面以 Intel CPU 为例但问题不仅限于 Intel，
AMD 的很多在用参数和功能这个文档里都没有，

```
Idle loop
=========

  idle=poll
    Don't do power saving in the idle loop using HLT, but poll for rescheduling
    event. This will make the CPUs eat a lot more power, but may be useful
    to get slightly better performance in multiprocessor benchmarks. It also
    makes some profiling using performance counters more accurate.
    Please note that on systems with MONITOR/MWAIT support (like Intel EM64T
    CPUs) this option has no performance advantage over the normal idle loop.
    It may also interact badly with hyperthreading.
```

* `idle=poll` 在某些场景下能提升 multiple benchmark 的性能，也能让某些 profiling 更准确一些；
* 在支持 `MONITOR/MWAIT` 的平台上，这个配置并不会带来性能提升；
* 最后一句：**<mark>与超线程的交互（兼容）可能很差</mark>**。为什么？没说，
  但是下面来自 Dell 的一篇白皮书做了一些进一步解释。

## 1.4 Dell Whitepaper: Controlling Processor C-State Usage in Linux, 2013

[Dell Whitepaper: Controlling Processor C-State Usage in Linux](https://wiki.bu.ost.ch/infoportal/_media/embedded_systems/ethercat/controlling_processor_c-state_usage_in_linux_v1.1_nov2013.pdf)，
服务器厂商 Dell 的技术白皮书，其中一段，

```
If a user wants the absolute minimum latency, kernel parameter “idle=poll” can be used to keep the
processors in C0 even when they are idle (the processors will run in a loop when idle, constantly
checking to see if they are needed). If this kernel parameter is used, it should not be necessary to
disable C-states in BIOS (or use the “idle=halt” kernel parameter).

Take care when keeping processors in C0, though--this will increase power usage considerably.
Also, hyperthreading should probably be
disabled, as keeping processors in C0 can interfere with proper operation of logical cores
(hyperthreading). (The hyperthreading hardware works best when it knows when the logical processors
are idle, and it doesn’t know that if processors are kept busy in a loop when they are not running
useful code.)
```

* 可能需要关闭 hyperthreading，因为**<mark>处理器保持在 C0 状态</mark>**会**<mark>干扰逻辑核（也就是超线程）的正常功能</mark>**。
* 超线程硬件的工作原理：

    * 根据逻辑核（硬件线程）的**<mark>空闲状态</mark>**，做出下一步判断和动作；
    * C0 会在没有任务时**<mark>执行无意义代码</mark>**（即前面说的“轻量级”指令流）来保持处理器处于繁忙状态，
      用这种方式**<mark>避免处理器进入节能状态</mark>**；
* C0 模式的这种行为使得**<mark>超线程硬件无法判断硬件处理器的真实状态</mark>**（区分不出在执行有意义代码还是无意义代码），
  因而无法有效工作。

## 1.5 I really don't think you should really ever use "idle=poll" on HT-enabled hardware, Linus, 2003

用户报告 `idle=poll + hyperthreading` 导致并发性能显著变差，
Linus 回复说，
[I really don't think you should really ever use "idle=poll" on HT-enabled hardware](https://linux-kernel.vger.kernel.narkive.com/gVqKQELn/ht-and-idle-poll)，
**<mark><code>HT</code></mark>** 是超线程的缩写。

## 1.6 小结

看起来 `idle=poll` 与 turbo-frequency/hyperthreading 存在工作机制的冲突。

需要一些场景和 testcase 来验证。有经验的专家大佬，欢迎交流。

# 2 内核大量 ACPI 日志

一台惠普机器：

```shell
$ dmesg -T
kernel: ACPI Error: SMBus/IPMI/GenericSerialBus write requires Buffer of length 66, found length 32 (20180810/exfield-393)
kernel: ACPI Error: Method parse/execution failed \_SB.PMI0._PMM, AE_AML_BUFFER_LIMIT (20180810/psparse-516)
kernel: ACPI Error: AE_AML_BUFFER_LIMIT, Evaluating _PMM (20180810/power_meter-338)
...
```

这是 HP 的 BIOS 实现没有遵守协议，实际上这个报错不会产生硬件性能影响之类的（但是打印的日志量可能很大，每分钟十几条，不间断）。

一台联想机器：

```shell
$ dmesg -T
kernel: power_meter ACPI000D:00: Found ACPI power meter.
kernel: power_meter ACPI000D:00: Found ACPI power meter.
...
```

如果是 k8s node 遇到以上问题，可能是部署了 [prometheus/node_exporter](https://github.com/prometheus/node_exporter) 导致的 [2]，
试试关闭其 hwmon collector。

# 参考资料

1. [Controlling Processor C-State Usage in Linux](https://wiki.bu.ost.ch/infoportal/_media/embedded_systems/ethercat/controlling_processor_c-state_usage_in_linux_v1.1_nov2013.pdf), A Dell technical white paper describing the use of C-states with Linux operating systems, 2013
2. [After PMM2 client installation kernel: ACPI Error: SMBus/IPMI/GenericSerialBus write requires Buffer of length 66](https://forums.percona.com/t/after-pmm2-client-installation-kernel-acpi-error-smbus-ipmi-genericserialbus-write-requires-buffer-of-length-66/20425/2), forums.percona.com, 2023

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
