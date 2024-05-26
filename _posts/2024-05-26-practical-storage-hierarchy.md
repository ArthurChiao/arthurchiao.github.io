---
layout    : post
title     : "Practical Storage Hierarchy and Performance: From HDDs to On-chip Caches（2024）"
date      : 2024-05-26
lastupdate: 2024-05-28
categories: storage gpu
---

This post summarizes bandwidths for local storage media, networking
infra, as well as remote storage systems. Readers may find this helpful when
**<mark><code>identifying bottlenecks in IO-intensive applications</code></mark>**
(e.g. AI training and LLM inference).

<p align="center"><img src="/assets/img/practical-storage-hierarchy/storage-bandwidth-3.png" width="100%"/></p>
<p align="center">Fig. Peak bandwidth of storage media, networking, and distributed storage solutions.</p>

Note: this post may contain inaccurate and/or stale information.

----

* TOC
{:toc}

----

# 1 Fundamentals

Before delving into the specifics of storage, let's first go through some
fundamentals about data transfer protocols.

## 1.1 SATA

From wikepedia [SATA](https://en.wikipedia.org/wiki/SATA):

> SATA (**<mark><code>Serial AT Attachment</code></mark>**) is a
> computer **<mark><code>bus interface</code></mark>** that connects host bus
> adapters to mass storage devices such as hard disk drives, optical drives,
> and solid-state drives.

### 1.1.2 Real world pictures

<p align="center"><img src="/assets/img/practical-storage-hierarchy/SATA2-pic.jpg" width="50%"/></p>
<p align="center">Fig. <mark>SATA interfaces and cables</mark> on a computer motherboard. Image source <a href="https://en.wikipedia.org/wiki/SATA">wikipedia</a></p>

### 1.1.1 Revisions and data rates

The SATA standard has evolved through multiple revisions.
The current prevalent revision is 3.0, offering a maximum IO bandwidth of **<mark><code>600MB/s</code></mark>**:

<p align="center">Table: SATA revisions. Data source: <a href="https://en.wikipedia.org/wiki/SATA">wikipedia</a></p>

| Spec              |  Raw data rate | Data rate | Max cable length |
|:------------------|:---------------|:----------|:-----------------|
| SATA Express      | 16 Gbit/s  |  1.97 GB/s | 1m |
| SATA revision 3.0 | 6 Gbit/s   | **<mark><code>600 MB/s</code></mark>** | 1m |
| SATA revision 2.0 | 3 Gbit/s   | 300 MB/s | 1m |
| SATA revision 1.0 | 1.5 Gbit/s | 150 MB/s | 1m |

## 1.2 PCIe

From wikipedia [PCIe (PCI Express)](https://en.wikipedia.org/wiki/PCI_Express):

> PCI Express is high-speed serial computer expansion bus standard.

PCIe (**<mark><code>Peripheral Component Interconnect Express</code></mark>**)
is another kind of system bus, designed to connect a variety of peripheral
devices, including **<mark><code>GPUs, NICs, sound cards</code></mark>**,
and certain storage devices.

### 1.1.2 Real world pictures

<p align="center"><img src="/assets/img/practical-storage-hierarchy/pcie-pic.jpg" width="50%"/></p>
<p align="center">Fig.
<mark>Various slots on a computer motherboard</mark>, from top to bottom:<br />
PCIe x4 (e.g. for <mark>NVME SSD</mark>)<br />
PCIe x16 (e.g. for <mark>GPU card</mark>)<br />
PCIe x1<br />
PCIe x16<br />
Conventional PCI (32-bit, 5 V)<br />
Image source <a href="https://en.wikipedia.org/wiki/PCI_Express">wikipedia</a></p>

As shown in the above picture,
PCIe electrical interface is measured by the number of **<mark><code>lanes</code></mark>**. 
A lane is a **<mark><code>single data send+receive line</code></mark>**,
functioning similarly to a "one-lane road" with traffic in **<mark><code>both directions</code></mark>**.

### 1.2.2 Generations and data rates

Each new PCIe generation doubles the bandwidth of a lane than the previous generation:

<p align="center">Table: PCIe <mark>Unidirectional Bandwidth</mark>.
Data source: <a href="https://www.trentonsystems.com/en-us/resource-hub/blog/pcie-gen4-vs-gen3-slots-speeds">trentonsystems.com</a>
</p>

| Generation   | Year of Release| Data Transfer Rate | Bandwidth x1 | Bandwidth x16|
|:-------------|:---------------|:-------------------|:-------------|:-------------|
| PCIe 1.0     | 2003           | 2.5 GT/s           | 250 MB/s     | 4.0 GB/s     |
| PCIe 2.0     | 2007           | 5.0 GT/s           | 500 MB/s     | 8.0 GB/s     |
| PCIe 3.0     | 2010           | 8.0 GT/s           | 1 GB/s       | 16 GB/s      |
| PCIe 4.0     | 2017           | 16 GT/s            | 2 GB/s       | **<mark><code>32 GB/s</code></mark>** |
| PCIe 5.0     | 2019           | 32 GT/s            | 4 GB/s       | **<mark><code>64 GB/s</code></mark>** |
| PCIe 6.0     | 2021           | 64 GT/s            | 8 GB/s       | 128 GB/s     |

Currently, the most widely used generations are **<mark><code>Gen4 and Gen5</code></mark>**.

Note: Depending on the document you're referencing, PCIe bandwidth may be presented
as **<mark><code>either unidirectional or bidirectional</code></mark>**,
with the latter indicating a bandwidth that is twice that of the former.

## 1.3 Summary

With the above knowledge, we can now proceed to discuss the
performance characteristics of various storage devices.

# 2 Disk

## 2.1 HDD: **<mark><code>~200 MB/s</code></mark>**

From wikipedia [HDD](https://en.wikipedia.org/wiki/Hard_disk_drive):

> A hard disk drive (HDD) is an **<mark><code>electro-mechanical</code></mark>**
> data storage device that stores and retrieves digital data using
> **<mark><code>magnetic storage</code></mark>** with one or more rigid
> **<mark><code>rapidly rotating platters</code></mark>** coated with magnetic material.

### 2.1.1 Real world pictures

A real-world picture is shown below:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/hdd-internal.jpg" width="60%"/></p>
<p align="center">Fig. Internals of a real world HDD. Image source <a href="https://hardwaresecrets.com/anatomy-of-a-hard-disk-drive/">hardwaresecrets.com</a></p>

### 2.1.2 Supported interfaces (bus types)

HDDs connect to a motherboard over one of several bus types, such as,

* **<mark><code>SATA</code></mark>**
* SCSI
* Serial Attached SCSI (SAS)

Below is a SATA HDD:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/sata-hdd.jpg" width="50%"/></p>
<p align="center">Fig. A real world SATA HDD. Image source <a href="https://hardwaresecrets.com/anatomy-of-a-hard-disk-drive/">hardwaresecrets.com</a></p>

and how an HDD connects to a computer motherboard via SATA cables:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/sata-connector.jpg" width="35%"/></p>
<p align="center">Fig. An HDD with SATA cables. Data source <a href="https://www.datalab247.com/articles/desktop-connectors.html">datalab247.com</a></p>

### 2.1.3 Bandwidth: constrained by **<mark><code>machanical factors</code></mark>**

HDDs are machanical devices, and their peak IO performance is inherently
limited by various mechanical factors, including the speed at which the
actuator arm can function. The current upper limit of HDDs is **<mark><code>~200MB/s</code></mark>**,
which is **<mark><code>significantly below the saturation point of a SATA 3.0</code></mark>** interface (600MB/s).

### 2.1.4 Typical latencies

<p align="center">Table. Latency characteristics typical of HDDs. Data source: <a href="https://en.wikipedia.org/wiki/Hard_disk_drive">wikipedia</a></p>

| Rotational speed (rpm) | Average rotational latency (ms) |
|:----------------------- |:-------------------------------|
| 15,000                  | 2                              |
| 10,000                  | 3                              |
| 7,200                   | **<mark><code>4.16</code></mark>**   |
| 5,400                   | 5.55                           |
| 4,800                   | 6.25                           |

## 2.2 SATA SSD: **<mark><code>~600MB/s</code></mark>**

What's a SSD? From wikipedia [SSD](https://en.wikipedia.org/wiki/Solid-state_drive):

> A solid-state drive (SSD) is a solid-state storage device. It provides
> persistent data storage using **<mark><code>no moving parts</code></mark>**.

Like HDDs, SSDs support several kind of bus types:

* SATA
* PCIe (NVME)
* ...

Let's see the first one: SATA-interfaced SSD, or SATA SSD for short.

### 2.2.1 Real world pictures

SSDs are usually smaller than HDDs,

<p align="center"><img src="/assets/img/practical-storage-hierarchy/hdd-ssd-nvme.png" width="60%"/></p>
<p align="center">Fig. Size of different drives, left to right: HDD, <mark>SATA SSD</mark>, NVME SSD.
Image source <a href="https://www.avg.com/en/signal/ssd-hdd-which-is-best">avg.com</a></p>

### 2.2.2 Bandwidth: constrained by **<mark><code>SATA bus</code></mark>**

The absence of mechanical components (such as rotational arms) allows SATA SSDs
to fully utilize the capabilities of the SATA bus.
This results in an upper limit of **<mark><code>600MB/s</code></mark>** IO bandwidth,
which is **<mark><code>3x faster than that of SATA HDDs</code></mark>**.

## 2.3 NVME SSD: **<mark><code>~7GB/s, ~13GB/s</code></mark>**

Let's now explore another type of SSD: the **<mark><code>PCIe-based</code></mark>** NVME SSD.

### 2.3.1 Real world pictures

NVME SSDs are even smaller than SATA SSDs,
and they **<mark><code>connect directly to the PCIe bus with 4x lanes</code></mark>** instead of SATA cables,

<p align="center"><img src="/assets/img/practical-storage-hierarchy/hdd-ssd-nvme.png" width="60%"/></p>
<p align="center">Fig. Size of different drives, left to right: HDD, SATA SSD, <mark>NVME SSD</mark>.
Image source <a href="https://www.avg.com/en/signal/ssd-hdd-which-is-best">avg.com</a></p>

### 2.3.2 Bandwidth: contrained by **<mark><code>PCIe bus</code></mark>**

NVME SSDs has a peak bandwidth of 7.5GB/s over PCIe Gen4, and ~13GB/s over PCIe Gen5.

## 2.4 Summary

We illustrate the peak bandwidths of afore-mentioned three kinds of local storage media in a graph:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/storage-bandwidth.png" width="90%"/></p>
<p align="center">Fig. Peak bandwidths of different storage media.</p>

These (HDDs, SSDs) are commonly called **<mark><code>non-volatile or persistent storage</code></mark>** media.
And as the picture hints, in next chapters we'll delve into some other kinds of storage devices.

# 3 DDR SDRAM (CPU Memory): **<mark><code>~400GB/s</code></mark>**

DDR SDRAM nowadays serves mainly as the **<mark><code>main memory</code></mark>** in computers.

## 3.1 Real world pictures

<p align="center"><img src="/assets/img/practical-storage-hierarchy/ddr-pic.jpg" width="70%"/></p>
<p align="center">Fig. Front and back of a DDR RAM module for desktop PCs (DIMM). Image source <a href="https://en.wikipedia.org/wiki/DDR_SDRAM">wikipedia</a></p>

<p align="center"><img src="/assets/img/practical-storage-hierarchy/Corsair_CMX512-3200C2PT_20080602.jpg" width="60%"/></p>
<p align="center">Fig. Corsair DDR-400 memory with heat spreaders. Image source <a href="https://en.wikipedia.org/wiki/DDR_SDRAM">wikipedia</a></p>

DDR memory connects to the motherboard via DIMM slots:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/sdram-dimm-slots.png" width="80%"/></p>
<p align="center">Fig. Three SDRAM DIMM slots on a ABIT BP6 computer motherboard. Image source <a href="https://en.wikipedia.org/wiki/DIMM">wikipedia</a></p>

## 3.2 Bandwidth: contrained by **<mark><code>memory clock, bus width, channel</code></mark>**, etc

Single channel bandwidth:

|       | Transfer rate | Bandwidth |
|:------|:------|:-----|
| [DDR4](https://en.wikipedia.org/wiki/DDR4_SDRAM)  | 3.2GT/s | 25.6 GB/s |
| [DDR5](https://en.wikipedia.org/wiki/DDR5_SDRAM)  | 4–8GT/s | **<mark><code>32–64 GB/s</code></mark>** |

if [Multi-channel memory architecture](https://en.wikipedia.org/wiki/Multi-channel_memory_architecture) is enabled,
the peak (aggreated) bandwidth will be increased by multiple times:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/Dual_channel_slots.jpg" width="40%"/></p>
<p align="center">Fig. Dual-channel memory slots, color-coded orange and yellow for this particular motherboard.
Image source <a href="https://en.wikipedia.org/wiki/Multi-channel_memory_architecture">wikipedia</a></p>

Such as [4], 

* Intel Xeon Gen5: up to 8 memory-channels running at up to 5600MT/s (**<mark><code>358GB/s</code></mark>**)
* Intel Xeon Gen4: up to 8 memory-channels running at up to 4800MT/s (**<mark><code>307GB/s</code></mark>**)

## 3.3 Summary

DDR5 bandwidth in the hierarchy:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/storage-bandwidth.png" width="90%"/></p>
<p align="center">Fig. Peak bandwidths of different storage media.</p>

# 4 GDDR SDRAM (GPU Memory): **<mark><code>~1000GB/s</code></mark>**

Now let's see another variant of DDR, commonly used in **<mark><code>graphics cards (GPUs)</code></mark>**.

## 4.1 GDDR vs. DDR

From wikipedia [GDDR SDRAM](https://en.wikipedia.org/wiki/GDDR_SDRAM):

> Graphics DDR SDRAM (GDDR SDRAM) is a type of synchronous dynamic random-access
> memory (SDRAM) specifically designed for applications requiring high bandwidth,
> e.g. graphics processing units (**<mark><code>GPUs</code></mark>**).
>
> GDDR SDRAM is **<mark><code>distinct from the more widely known types of DDR SDRAM</code></mark>**,
> such as DDR4 and DDR5, although they share some of the same features—including double
> data rate (DDR) data transfers.

## 4.2 Real world pictures

<p align="center"><img src="/assets/img/practical-storage-hierarchy/GDDR-Hynix_HY5DU561622CTP-5-5390.jpg" width="40%"/></p>
<p align="center">Fig. Hynix GDDR SDRAM. Image Source: <a href="https://en.wikipedia.org/wiki/GDDR_SDRAM">wikipedia</a></p>

## 4.3 Bandwidth: contrained by **<mark><code>lanes & clock rates</code></mark>**

Unlike DDR, GDDR is **<mark><code>directly integrated with GPU</code></mark>** devices,
bypassing the need for pluggable PCIe slots. This integration
**<mark><code>liberates GDDR from the bandwidth limitations imposed by the PCIe bus</code></mark>**.
Such as,

* GDDR6: 1008GB/s. Peak per-pin data rate 16Gb/s, max memory bus width 384-bits.
* GDDR6x: 1008GB/s, used by **<mark><code>NVIDIA RTX 4090</code></mark>**

## 4.4 Summary

With GDDR included:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/storage-bandwidth.png" width="90%"/></p>
<p align="center">Fig. Peak bandwidths of different storage media.</p>

# 5 HBM: **<mark><code>1~5 TB/s</code></mark>**

If you'd like to achieve even more higher bandwidth than GDDR, then there is an option: HBM (High Bandwidth Memory).

> A great innovation but a terrible name.

## 5.1 What's new

HBM is designed to provide a **<mark><code>larger memory bus width</code></mark>** than GDDR, resulting in larger data transfer rates. 

<p align="center"><img src="/assets/img/practical-storage-hierarchy/HBM_schematic.png" width="60%"/></p>
<p align="center">Fig. Cut through a graphics card that uses HBM. 
Image Source: <a href="https://en.wikipedia.org/wiki/High_Bandwidth_Memory">wikipedia</a></p>

HBM sits **<mark><code>inside the GPU die</code></mark>** and is stacked – for example NVIDIA A800
GPU has 5 stacks of 8 HBM DRAM dies (8-Hi) each with two 512-bit
channels per die, resulting in a total width of 5120-bits (**<mark><code>5 active stacks * 2 channels * 512 bits</code></mark>**) [3].

As another example, HBM3 (used in NVIDIA H100) also has a **<mark><code>5120-bit bus</code></mark>**,
and 3.35TB/s memory bandwidth,

<p align="center"><img src="/assets/img/gpu-notes/nvidia-gpus-hbm.png" width="50%" height="50%"></p>
<p align="center">Fig. Bandwidth of several HBM-powered GPUs from NVIDIA. Image source: 
<a href="https://developer.nvidia.com/blog/nvidia-hopper-architecture-in-depth/">nvidia.com</a></p>

## 5.2 Real world pictures

The 4 squares in left and right are just HBM chips:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/AMD_Fiji_GPU_package_with_HBM_and_interposer.jpg" width="50%"/></p>
<p align="center">Fig. AMD Fiji, the <mark>first GPU to use HBM</mark>.
Image Source: <a href="https://en.wikipedia.org/wiki/High_Bandwidth_Memory">wikipedia</a></p>

## 5.3 Bandwidth: contrained by **<mark><code>lanes & clock rates</code></mark>**

From wikipedia [HBM](https://en.wikipedia.org/wiki/High_Bandwidth_Memory)，

|       | Bandwidth           | Year   | GPU |
|:------|:--------------------|:-------|:-----|
| HBM   | 128GB/s/package     |        |      |
| HBM2  | 256GB/s/package     | 2016   | V100 |
| HBM2e | ~450GB/s            | 2018   | `A100, ~2TB/s`; Huawei `Ascend 910B` |
| HBM3  | 600GB/s/site        | 2020   | H100, 3.35TB/s |
| HBM3e | ~1TB/s              | 2023   | `H200`, [4.8TB/s](https://www.nvidia.com/en-us/data-center/h200/) |

## 5.4 HBM-powered CPUs

HBM is **<mark><code>not exclusive to GPU memory</code></mark>**; it is also integrated into some CPU models, such as the
[Intel Xeon CPU Max Series](https://www.intel.com/content/www/us/en/products/details/processors/xeon/max-series.html).

## 5.5 Summary

This chapter concludes our exploration of dynamic RAM technologies, which includes

* DDR DRAM
* GDDR DRAM
* HBM DRAM

<p align="center"><img src="/assets/img/practical-storage-hierarchy/storage-bandwidth.png" width="90%"/></p>
<p align="center">Fig. Peak bandwidths of different storage media.</p>

In the next, let's see some on-chip static RAMs.

# 6 SRAM (on-chip): **<mark><code>20+ TB/s</code></mark>**

The term "on-chip" in this post refers to **<mark><code>memory storage that's integrated within the same silicon as the processor unit</code></mark>**.

## 6.1 SRAM vs. DRAM

From wikipedia [SRAM](https://en.wikipedia.org/wiki/Static_random-access_memory):

> Static random-access memory (static RAM or SRAM) is a type of random-access
> memory that uses latching circuitry (flip-flop) to store each bit. SRAM
> is volatile memory; data is lost when power is removed.

The term **<mark><code>static</code></mark>** differentiates SRAM from DRAM:

|       | SRAM           | DRAM   |
|:------|:--------------------|:-------|
| data freshness | stable in the presence of power | decays in seconds, must be periodically refreshed|
| speed (relative) | fast (10x) | slow |
| cost (relative)  | high | low |
| mainly used for | **<mark><code>cache</code></mark>** | main memory |

> SRAM requires **<mark><code>more transistors per bit to implement</code></mark>**, so it is less dense and
> more expensive than DRAM and also has a **<mark><code>higher power consumption</code></mark>** during read
> or write access. The power consumption of SRAM varies widely depending on how
> frequently it is accessed.

## 6.2 Cache hierarchy (L1/L2/L3/...)

In the architecture of multi-processor (CPU/GPU/...) systems, a multi-tiered static cache structure is usually used:

* L1 cache: typically exclusive to each individual processor;
* L2 cache: commonly accessible by a group of processors.

<p align="center"><img src="/assets/img/gpu-notes/h100-chip-layout.jpg" width="80%" height="80%"></p>
<p align="center">NVIDIA H100 chip layout (<mark>L2 cache in the middle</mark>, shared by many SM processors). Image source:
<a href="https://developer.nvidia.com/blog/nvidia-hopper-architecture-in-depth/">nvidia.com</a></p>

## 6.3 **<mark><code>Groq LPU</code></mark>**: eliminating memory bottleneck by using SRAM as main memory

From the [official website](https://wow.groq.com/why-groq/):
Groq is the AI infra company that builds the world’s **<mark><code>fastest AI inference technology</code></mark>** with both software and hardware. 
Groq LPU is designed to **<mark><code>overcome two LLM bottlenecks</code></mark>**: compute density and **<mark><code>memory bandwidth</code></mark>**.

* An LPU has greater compute capacity than a GPU and CPU in regards to LLMs.
  This reduces the amount of time per word calculated, allowing sequences of
  text to be generated much faster. 
* **<mark><code>Eliminating external memory bottlenecks</code></mark>** (using on-chip SRAM instead) enables the LPU Inference Engine to
  deliver **<mark><code>orders of magnitude better performance on LLMs compared to GPUs</code></mark>**.

Regarding to the chip:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/groq-asic-die.png" width="50%" height="50%"></p>
<p align="center">Fig. Die photo of 14nm ASIC implementation of the Groq TSP. Image source: groq paper [2]</p>

The East and West hemisphere of **<mark><code>on-chip memory</code></mark>** module (MEM)

* Composed of 44 parallel slices of **<mark><code>SRAM</code></mark>** and provides the memory concurrency necessary to fully utilize the 32 streams in each direction.
* Each slice provides 13-bits of physical addressing of 16-byte memory words, each byte maps to a lane, for a total of **<mark><code>220 MiBytes of on-chip SRAM</code></mark>**.

## 6.4 Bandwidth: contrained by **<mark><code>clock rates</code></mark>**, etc

## 6.5 Summary

This chapter ends our journey to various physical storage media, from machanical devices like HDDs all the way to on-chip cache.
We illustrate their peak bandwidth in a picture, note that the Y-axis is **<mark><code>log<sub>10</sub></code></mark>** scaled:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/storage-bandwidth.png" width="80%"/></p>
<p align="center">Fig. Speeds of different storage media.</p>

These are the maximum IO bandwidths when performing read/write operations on a local node.

Conversely, when considering remote I/O operations, such as those involved in
distributed storage systems like Ceph, AWS S3, or NAS, a new bottleneck emerges:
networking bandwidth.

# 7 Networking bandwidth: **<mark><code>400GB/s</code></mark>**

## 7.1 Traditional data center: `2*{25,100,200}Gbps`

For traditional data center workloads, the following per-server networking configurations are typically sufficient:

* 2 NICs * 25Gbps/NIC, providing up to **<mark><code>6.25GB/s</code></mark>** unidirectional bandwidth when operating in active-active mode;
* 2 NICs * 100Gbps/NIC, delivering up to **<mark><code>25GB/s</code></mark>** unidirectional bandwidth when operating in active-active mode;
* 2 NICs * 200Gbps/NIC, achieving up to **<mark><code>50GB/s</code></mark>** unidirectional bandwidth when operating in active-active mode.

## 7.2 AI data center: GPU-interconnect: `8*{100,400}Gbps`

This type of networking facilitates inter-GPU communication and is not intended
for general data I/O. The data transfer pathway is as follows:

```
            HBM <---> NIC <---> IB/RoCE <---> NIC <--> HBM
                Node1                            Node2
```

## 7.3 Networking bandwidths

Now we add networking bandwidths into our storage performance picture:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/storage-bandwidth-2.png" width="90%"/></p>
<p align="center">Fig. Speeds of different storage media, with networking bandwidth added.</p>

## 7.4 Summary

If remote storage solutions (such as distributed file systems) is involved, and networking is fast enough,
IO bottleneck would shift down to the remote storage solutions,
that's why there are some extremely high performance storage solutions dedicated for today's AI trainings.

# 8 Distributed storage: aggregated **<mark><code>2+ TB/s</code></mark>**

## 8.1 AlibabaCloud CPFS

AlibabaCloud's Cloud Parallel File Storage (CPFS) is an exemplar of such
high-performance storage solutions. It [claims](https://www.alibabacloud.com/help/en/cpfs/product-overview/product-specifications?spm=a2c63.p38356.0.0.2f6a6379tfKf2x)
to offer up to **<mark><code>2TB/s of aggregated bandwidth</code></mark>**.

But, note that the mentioned bandwidth is an aggregate across multiple nodes,
**<mark><code>no single node can achieve this</code></mark>** level of IO speed.
You can do some calcuatations to understand why, with PCIe bandwidth, networking bandwidth, etc;

## 8.2 NVME SSD powered Ceph clusters

An open-source counterpart is Ceph, which also delivers impressive results.
For instance, with a cluster configuration of `68 nodes * 2 * 100Gbps/node`,
a user achieved **<mark><code>aggregated throughput of 1TB/s</code></mark>**,
as [documented](https://ceph.io/en/news/blog/2024/ceph-a-journey-to-1tibps/).

## 8.3 Summary

Now adding distributed storage aggregated bandwidth into our graph:

<p align="center"><img src="/assets/img/practical-storage-hierarchy/storage-bandwidth-3.png" width="100%"/></p>
<p align="center">Fig. Peak bandwidth of storage media, networking, and distributed storage solutions.</p>

# 9 Conclusion

This post compiles bandwidth data for local storage media, networking infrastructure, and remote storage systems.
With this information as reference, readers can evaluate the potential IO
bottlenecks of their systems more effectively, such as GPU server IO bottleneck analysis [1]:

<p align="center"><img src="/assets/img/gpu-notes/8x-a100-bw-limits.png" width="100%" height="100%"></p>
<p align="center">Fig. Bandwidths inside a 8xA100 GPU node</p>

# References

1. [Notes on High-end GPU Servers (in Chinese)]({% link _posts/2023-09-16-gpu-advanced-notes-1-zh.md %}), 2023
2. [Think Fast: A Tensor Streaming Processor (TSP) for Accelerating Deep Learning Workloads](https://wow.groq.com/groq-isca-paper-2020/), ISCA paper, 2020
3. [GDDR6 vs HBM - Defining GPU Memory Types](https://www.exxactcorp.com/blog/hpc/gddr6-vs-hbm-gpu-memory), 2024
4. [5th Generation Intel® Xeon® Scalable Processors](https://edc.intel.com/content/www/us/en/products/performance/benchmarks/5th-generation-intel-xeon-scalable-processors/), intel.com

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
