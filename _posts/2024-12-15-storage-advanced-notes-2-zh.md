---
layout    : post
title     : "存储进阶笔记（二）：Linux 存储栈：从 Device Mapper、LVM 到文件系统（2024）"
date      : 2024-12-15
lastupdate: 2024-12-15
categories: storage hardware storage-note-series
---

记录一些平时接触到的存储知识。由于是笔记而非教程，因此内容不求连贯，有基础的同学可作查漏补缺之用。

<p align="center"><img src="/assets/img/storage-advanced-notes/lvm-concepts.png" width="80%"/></p>
<p align="center">Fig. LVM concepts, and how userspace file operations traverse the Linux storage stack. </p>

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

{% for category in site.categories %}
  <div class="archive-group">
    {% capture category_name %}{{ category | first }}{% endcapture %}
    {% if category_name == "storage-note-series" %}
        {% assign posts = site.categories[category_name] | sort: 'date' | sort: 'url' %}
        {% for post in posts %}
            <article class="archive-item">
              <li><a style="text-decoration:none" href="{{ post.url }}">{{post.title}}</a></li>
            </article>
        {% endfor %}
    {% endif %}
  </div>
{% endfor %}

----

* TOC
{:toc}

----

# 1 Device Mapper：内核存储基础设施

## 1.1 内核框架：`物理块设备 -> 虚拟块设备`

[Device mapper](https://en.wikipedia.org/wiki/Device_mapper)（**<mark>设备映射器</mark>**）
是 Linux 内核提供的一个框架，用于将**<mark>物理块设备</mark>**（physical block devices）
映射到更上层的**<mark>虚拟块设备</mark>**（virtual block devices）。

* 是逻辑卷管理器（**<mark><code>LVM</code></mark>**）、**<mark><code>software RAID</code></mark>** 和 dm-crypt 磁盘加密技术的**<mark>基础</mark>**，
* 还提供了诸如**<mark>文件系统快照</mark>**等功能，
* 还可以在传递数据的同时进行修改，例如，在提供磁盘加密，或者模拟不可靠的硬件行为。

## 1.2 在内核存储栈中的位置

<p align="center"><img src="/assets/img/how-hdd-works/Linux-storage-stack-diagram_v6.9.png" width="100%" height="100%"></p>
<p align="center">Fig. Device Mapper 在 Linux 存储栈中的位置（图中间部分）</p>

## 1.3 使用场景及典型应用

* **<mark><code>dm-cache</code></mark>**：组合使用 SSD 和 HDD 的**<mark>混合卷</mark>**（hybrid volume）

    A [hybrid volume](https://en.wikipedia.org/wiki/Logical_volume_management)
    is any volume that intentionally and opaquely makes use of two separate
    physical volumes. For instance, a workload may consist of random seeks so
    an SSD may be used to permanently store frequently used or recently written
    data, while using higher-capacity rotational magnetic media for long-term
    storage of rarely needed data. On Linux, **<mark><code>bcache</code></mark>** or **<mark><code>dm-cache</code></mark>** may be used for
    this purpose.

* **<mark><code>Docker</code></mark>** – 基于 device mapper 给容器创建 **<mark><code>copy-on-write</code></mark>** 存储；
* **<mark><code>LVM2</code></mark>** – 内核最常用的一种逻辑卷管理器（logical volume manager）

# 2 LVM：基于 Device Mapper 创建逻辑卷（设备）

## 2.1 功能

[Logical Volume Manager](https://en.wikipedia.org/wiki/Logical_Volume_Manager_(Linux))
（LVM，逻辑卷管理器）1998 年引入内核，是一个**<mark>基于 device mapper 的框架</mark>**，
为内核提供**<mark>逻辑卷管理能力</mark>**。

LVM 可以认为是物理磁盘和分区之上的一个**<mark>很薄的软件层</mark>**，
能方便换盘、重新分区和备份等等管理工作。

## 2.2 LVM 中的概念/术语图解

<p align="center"><img src="/assets/img/storage-advanced-notes/lvm-concepts.png" width="80%"/></p>
<p align="center">Fig. LVM concepts, and how userspace file operations traverse the Linux storage stack. </p>

## 2.3 使用场景

LVM 使用场景：

* 将多个物理卷（physical volumes）或物理盘创建为一个逻辑卷（logical volume），**<mark>有点类似于 RAID0，但更像 JBOD</mark>**，好处是方便动态调整卷大小。
* 热插拔，能在不停服的情况下添加或替换磁盘，管理非常方便。

## 2.4 使用教程

1. [What is LVM2 in Linux?](https://medium.com/@The_Anshuman/what-is-lvm2-in-linux-3d28b479e250), medium.com, 2023

# 3 文件系统：基于物理或逻辑卷（块设备），创建和管理文件层级

## 3.1 常规文件系统：不能跨 device

常规的文件系统，例如 XFS、EXT4 等等，都**<mark>不能跨多个块设备</mark>**（device）。
也就是说，创建一个文件系统时，只能指定一个特定的 device，比如 `/dev/sda`。

要跨多个盘，只能通过 RAID、JBOD、LVM 等等技术**<mark>将这些块设备合并成一个逻辑卷</mark>**，
然后在这个逻辑卷上初始化文件系统。

## 3.2 Cross-device 文件系统

更高级一些的文件系统，是能够跨多个块设备的，包括，

* **<mark><code>ZFS</code></mark>**
* **<mark><code>BTRFS</code></mark>**

# 4 云计算：块存储是如何工作的

上一节已经介绍到，在**<mark>块设备</mark>**上初始化文件系统，就可以创建文件和目录了。
这里所说的块设备 —— 不管是物理设备，还是逻辑设备 —— 穿透之后终归是一个**<mark>插在本机上硬件设备</mark>**。

有了虚拟化之后，情况就不一样了。
比如有一类特殊的 Linux 设备，它们**<mark>对操作系统呈现的确实是一个块设备</mark>**，
但其实**<mark>底层对接的远端存储系统</mark>**，而不是本机硬件设备。

在云计算中，这种存储类型称为“块存储”。

## 4.1 典型块存储产品

块存储（Block Storage），也称为 block-level storage，是公有云和私有云上都非常常见的一种存储。
各家的叫法或产品名字可能不同，例如，

* AWS EBS（Elastic Block Store）
* 阿里云的 SSD
* Ceph RBD

## 4.2 工作层次：块级别

块存储**<mark>工作在块级别</mark>**（device-level），可以**<mark>直接访问数据并实现高性能I/O</mark>**。 
因此它提供**<mark>高性能、低延迟和快速数据传输</mark>**。

## 4.3 使用场景和使用方式

使用场景：

* **<mark>虚拟机系统盘</mark>**
* **<mark>数据库磁盘</mark>**

使用方式：

1. 在块存储系统（例如 AWS EBS）中创建一个块设备，
2. 将这个块挂载到想使用的机器上，这时呈现给这台机器的操作系统的是一个块设备（**<mark><code>/dev/xxx</code></mark>**），

    <p align="center"><img src="/assets/img/storage-advanced-notes/storage-decision-matrix.png" width="60%"/></p>
    <p align="center">Storage Decision. <a href="https://aws.amazon.com/compare/the-difference-between-block-file-object-storage/">Image Source</a></p>

3. 在这个块设备上初始化文件系统（例如初始化一个 **<mark><code>ext4</code></mark>** 文件系统），然后就可以像普通硬盘一样使用了。

## 4.4 基本设计

AWS 对文件存储、对象存储和块存储有一个不错的[介绍文档](https://aws.amazon.com/compare/the-difference-between-block-file-object-storage/)。
其中提到的块存储的设计：

* 块存储**<mark>将数据划分为固定大小的 block</mark>**进行存储。Block 的大小在初始化块设备时指定，可以是**<mark>几 KB 到几 MB</mark>**；
* 操作系统**<mark>为每个 block 分配一个唯一的地址/序号，记录在一个表中</mark>**。寻址使用这个序号，因此非常快；
* 每个 Block 独立，可以直接访问或修改某个 block，不影响其他 blocks；
* **<mark>存储元数据的设计非常紧凑，以保持高效</mark>**。
    * 非常基本的元数据结构，确保了在数据传输过程中的最小开销。
    * 搜索、查找和检索数据时，使用每个 block 的唯一标识符。
* 块存储**<mark>不依赖文件系统，也不需要独立的进程</mark>**（例如，区别于 JuiceFS [4]），由**<mark>操作系统直接管理</mark>**。

## 4.5 Ceph 块存储（RBD）的设计

### 4.5.1 概念

* [Pool](https://docs.ceph.com/en/reef/rados/operations/pools/)：存储对象的逻辑分区（**<mark><code>logical partitions used to store objects</code></mark>**），有独立的 resilience/placement-groups/CRUSH-rules/snaphots 管理能力；
* **<mark><code>Image</code></mark>**: 一个块，类似 LVM 中的一个 **<mark><code>logical volume</code></mark>**
* PG (placement group): 存储 objects 的副本的基本单位，一个 PG 包含很多 objects，例如 3 副本的话就会有 3 个 PG，存放在三个 OSD 上；

创建一个 RBD 块设备的大致步骤：

```shell
$ ceph osd pool create {pool-name} [{pg-num} [{pgp-num}]] [replicated] \
         [crush-rule-name] [expected-num-objects]
$ rbd pool init {pool-name}
$ rbd create --size {size MB} {pool-name}/{image-name}
```

### 4.5.2 RBD 的后端存储：Ceph 对象存储

Ceph 的设计比较特殊，同时支持三种存储类型：

1. 对象存储（object storage），类似 AWS S3；
2. 文件存储（file storage），类似 JuiceFS [4]；
3. 块存储（block storage），类似 AWS EBS。

    背后，**<mark>每个块存储中的 “block”（4.4 节中介绍的 block 概念）</mark>**，
    实际上最后是一个 Ceph 对象存储中的 **<mark><code>object</code></mark>**。
    也就是 **<mark>Ceph 的块存储是基于 Ceph 的对象存储</mark>**。

### 4.5.3 读写流程

<p align="center"><img src="/assets/img/storage-advanced-notes/rbd-io.png" width="50%"/></p>
<p align="center">Fig. Ceph RBD IO. Each object is <mark><code>fix-sized</code></mark>, e.g. 4MB by default. <a href="https://blog.shunzi.tech/post/ceph-rbd-src/">Image Source</a></p>

### 4.5.4 客户端代码实现

两种使用方式，二选一：

<p align="center"><img src="/assets/img/storage-advanced-notes/rbd-workflow.png" width="60%"/></p>
<p align="center">Fig. Ceph RBD workflow. <a href="https://blog.shunzi.tech/post/ceph-rbd-src/">Image Source</a></p>

1. 用户态库：librbd，这会直接通过 librados 去访问 Ceph 集群；
2. 内核态库：将 RBD 挂载到主机之后，在系统中就可以看到一个 **<mark><code>/dev/rbd{N}</code></mark>** 的设备，
    * 可以像使用本地盘一样，在这个设备上**<mark>初始化一个文件系统</mark>**，然后就能在这个文件系统里面读写文件了；
    * **<mark>RBD 驱动</mark>**会将这些文件操作转换为对 Ceph 集群的操作，比如**<mark>满 4MB 的文件作为一个 object</mark>** 写到 Ceph 对象存储中；
    * 内核驱动源码：[drivers/block/brd.c](https://github.com/torvalds/linux/blob/v5.15/drivers/block/brd.c)。
    * 源码解读：[2,3]

# 参考资料

1. [What’s the Difference Between Block, Object, and File Storage?](https://aws.amazon.com/compare/the-difference-between-block-file-object-storage/), aws.amazon.com, 2024
2. [Ceph-RBD 源码阅读](https://blog.shunzi.tech/post/ceph-rbd-src/), blog.shunzi.tech, 2019
3. [Deep Dive Into Ceph’s Kernel Client](https://engineering.salesforce.com/deep-dive-into-cephs-kernel-client-edea75787528/), engineering.salesforce.com, 2024
4. [JuiceFS 元数据引擎初探：高层架构、引擎选型、读写工作流（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-1-zh.md %})

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
