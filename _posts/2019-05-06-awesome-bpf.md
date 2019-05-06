---
layout    : post
title     : "Awesome BPF Resources"
date      : 2019-05-06
lastupdate: 2019-05-06
categories: bpf ebpf
---

## 1 Introduction & Overview

1. **Elena Zannoni, [New (and Exciting!) Developments in Linux
   Tracing](https://events.static.linuxfound.org/sites/events/files/slides/tracing-linux-ezannoni-linuxcon-ja-2015_0.pdf),
   LinuxCon, 2015**

    Exciting!

1. **Matt Fleming, [A thorough introduction to
   eBPF](https://lwn.net/Articles/740157/), lwn.net, 2017**

     This article explains how eBPF evolved how it works, and how it is used in
     the kernel.

1. **Cilium Blog, [Why is the kernel community replacing iptables with BPF? —
   Cilium](https://cilium.io/blog/2018/04/17/why-is-the-kernel-community-replacing-iptables/),
   cilium.io, 2018**

Chinese articles:

1. **张亦鸣, [eBPF 简史](https://www.ibm.com/developerworks/cn/linux/l-lo-eBPF-history/index.html), IBM Developer, 2017**

## 2 Design & Implementation Details

1. **Jonathan Corbet, [A JIT for packet
   filters](https://lwn.net/Articles/437981/), lwn.net, 2011**

    A retrospection to classic BPF (since 1990s) in Linux kernel, and the first
    eBPF patch for modern kernels.

    Classic BPF is implemented in
    [`net/core/filter.c`](https://github.com/torvalds/linux/blob/master/net/core/filter.c)
    in Linux kernel tree, only thousands lines of code.

1. **Jonathan Corbet, [BPF tracing filters](https://lwn.net/Articles/575531/),
   lwn.net, 2013**

    BPF progress for packet filtering. Another interesting topic in this article
    is the licensing scope of future BPF code.

1. **Jonathan Corbet, [BPF: the universal in-kernel virtual
   machine](https://lwn.net/Articles/599755/), lwn.net, 2014**

    Road from a JIT compiler to the universal in-kernel virtual machine.

1. **PLUMgrid, [BPF – in-kernel virtual
   machine](https://www.slideshare.net/AlexeiStarovoitov/bpf-inkernel-virtual-machine),
   LinuxCon, 2015**

    Lots of design and implementation details, and byte code examples.

1. **Linux Programmer's Manual, [`bpf(2)` system
   call](http://man7.org/linux/man-pages/man2/bpf.2.html), man7.org, 2019+**

    Introduction to eBPF design/architecture and data structures, official
    definition of BPF terminologies, e.g. BPF maps. Also including code and
    examples.

1. **Linux source tree, BPF Source Code, 2019+**

    * Source code: [`kernel/bpf/`](https://github.com/torvalds/linux/tree/master/kernel/bpf)
    * Sample code: [`sample/bpf/`](https://github.com/torvalds/linux/tree/master/sample/bpf)

## 3 Tools

1. **Matt Fleming, [An introduction to the BPF Compiler
   Collection](https://lwn.net/Articles/742082/), lwn.net, 2017**

   One of eBPF's biggest challenges for newcomers is that writing programs
   requires compiling and linking to the eBPF library from the kernel source.
   Kernel developers might always have a copy of the kernel source within reach,
   but that's not so for engineers working on production or customer machines.
   Addressing this limitation is one of the reasons that the BPF Compiler
   Collection was created.

## 4 Use Cases

### Tracking & Monitoring

eBPF programs can access kernel data structures, developers can write and test
new debugging code without recompiling the kernel.

### Container Network Security (Cilium)

1. **Thomas Graf, [How to Make Linux Microservice-Aware with Cilium and
   eBPF](https://www.infoq.com/presentations/linux-cilium-ebpf), InfoQ, 2019**

    Chinese translated：[如何基于 Cilium 和 eBPF 打造可感知微服务的 Linux
    ]({% link _posts/2019-04-16-how-to-make-linux-microservice-aware-with-cilium-zh.md %})。

### Fast Datapath (XDP)

### Security Computation (seccomp)
