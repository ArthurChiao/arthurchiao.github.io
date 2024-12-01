---
layout    : post
title     : "[译] HDD 是如何工作的：旋转硬盘内部结构与工作原理的动画展示（2022）"
date      : 2024-11-16
lastupdate: 2024-12-01
categories: storage hardware
---

### 译者序

本文翻译自 2022 年 Branch Education 的一个科普视频 [How do Hard Disk Drives Work?](https://www.youtube.com/watch?v=wtdnatmVdIg) (Youtube)，
强烈推荐观看原视频（上不了油管的，B 站也有搬运）。本文整理个图文版方便查阅与思考，

<p align="center"><img src="/assets/img/how-hdd-works/write-head-1.png" width="60%" height="60%"></p>

* [HDD 是如何工作的：旋转硬盘内部结构与工作原理的动画展示（2022）]({% link _posts/2024-11-16-how-hdd-works-zh.md %})
* [SSD 是如何工作的：固态硬盘内部结构与工作原理的动画展示（2020）]({% link _posts/2024-11-16-how-hdd-works-zh.md %})

水平及维护精力所限，译文不免存在错误或过时之处，如有疑问，请查阅原视频。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

以下是译文。

----

* TOC
{:toc}

----

原视频由 PCBWay 赞助，感谢赞助商。

# 1 硬盘拆解

## 1.1 盘片（platter）

盘片是存储数据的地方，

<p align="center"><img src="/assets/img/how-hdd-works/platter-1.png" width="80%" height="80%"></p>
<p align="center">Disk/platter</p>

* 根据存储容量的不同，硬盘可能会有**<mark>多个盘片</mark>**堆叠，如上面右图所示；
* 磁盘由铝镁合金（aluminum magnesium alloy）和其他合金的多个涂层组成，

    <p align="center"><img src="/assets/img/how-hdd-works/platter-materials.png" width="60%" height="60%"></p>
    <p align="center">Disk/platter</p>

* 磁性功能层是 **<mark><code>120nm</code></mark>** 的钴铬钽合金薄层（cobalt chromium tantalum alloy），
  它由磁性微块组成，磁极方向能变，

    <p align="center"><img src="/assets/img/how-hdd-works/platter-materials-2.png" width="60%" height="60%"></p>
    <p align="center">Disk/platter</p>

* 盘片安装在主轴上，主轴使用中心的**<mark>无刷直流电机</mark>**（brushless DC motor）以 **<mark><code>7200rpm</code></mark>** 的等速度旋转。

## 1.2 机械臂装置

机械臂装置包括好几个组成部分，分别来看下。

### 1.2.1 机械臂（arm）

每个盘（platter）**<mark>上下各有一个臂</mark>**（arm），

<p align="center"><img src="/assets/img/how-hdd-works/arm-stack.png" width="60%" height="60%"></p>

### 1.2.2 滑橇（slider）和读写头（read/write head）

每个臂的末端有一个称为 slider（滑橇、滑块）的模块，它里面又包括了一个读/写头
（注意，**<mark>读头和写头是分开的两个部件</mark>**，后面会详细介绍），

<p align="center"><img src="/assets/img/how-hdd-works/slider-rw-head.png" width="60%" height="60%"></p>

**<mark>磁盘高速旋转产生的气流</mark>**能使这个**<mark>滑块（和读写头）浮起来</mark>**，
稳定运行在离磁盘表面 **<mark><code>15nm</code></mark>**（约 100 个原子）的地方，如下面的动图所示，

<p align="center"><img src="/assets/img/how-hdd-works/slider-float.gif" width="70%" height="70%"></p>
<p align="center">Fig. 高速旋转的盘片产生的气流使滑橇和读写头飘起来</p>

### 1.2.3 读写头停靠装置

只有当**<mark>盘片全速旋转时</mark>**（有数据读写任务），机械臂才会转到磁盘表面上。
平时盘片不旋转时（没有读写任务），机械臂会停在磁盘边上的一个小塑料装置上。

<p align="center"><img src="/assets/img/how-hdd-works/arm-park.png" width="50%" height="50%"></p>

### 1.2.4 尾部音圈电机（马达）

机械臂的尾部有一个
**<mark>音圈电机</mark>**（voice coil motor），或称音圈马达，它由线圈（coil of wire）和上下两个强钕磁铁（strong neodymium magnets）组成，

> VCM（Voice Coil Motor）一种特殊形式的直接驱动电机，原理和扬声器类似，固得名。
> 通电线圈在磁场内就会产生力，力的大小与施加在线圈上的电流成比例，运动轨迹可以是直线也可以是弧线。
> 具有结构简单、体积小、速度快、响应快等特点。译注。

<p align="center"><img src="/assets/img/how-hdd-works/arm-magnets.png" width="60%" height="60%"></p>

线圈通电之后会产生一个力，使机械臂在磁盘上移动（可以正向也可以反向），

<p align="center"><img src="/assets/img/how-hdd-works/arm-force.png" width="60%" height="60%"></p>

这种马达的速度和精度：

* **<mark>速度</mark>**：读/写头能够在不同磁道上来回移动 **<mark>~20 次/秒</mark>**；
* **<mark>精度</mark>**：读/写头位置精度 **<mark><code>~30nm</code></mark>**。

## 1.3 机械臂-电路板之间的数据线

如下图所示，一条柔性电线（a flexible ribbon of wires）沿着机型臂的侧面布线，

<p align="center"><img src="/assets/img/how-hdd-works/arm-pcb-connector.png" width="60%" height="60%"></p>

* 一边连接到**<mark>读/写头</mark>**，
* 一边连接到一个**<mark>连接器</mark>**（connector），该 connector 进一步连接到硬盘的主板，或称**<mark>印刷电路板</mark>**（PCB）。

## 1.4 PCB 和上面的芯片

PCB 上面的东西如下图所示，

<p align="center"><img src="/assets/img/how-hdd-works/pcb-chips.png" width="70%" height="70%"></p>

这里主要介绍三个芯片：

1. **<mark>主处理器芯片</mark>**；
1. **<mark>内存芯片</mark>**，作为主处理器的 cache；
1. 控制**<mark>音圈马达</mark>**和**<mark>磁盘主轴电机</mark>**的芯片。

## 1.5 数据线接口（e.g. SATA）和电源线接口

PCB 边缘还有两个硬件接口，

<p align="center"><img src="/assets/img/how-hdd-works/pcb-interfaces.png" width="65%" height="65%"></p>

* **<mark>数据接口</mark>**：例如 SATA 接口，用于和电脑主板相连传输数据；
* **<mark>电源接口</mark>**：用于给 HDD 供电。

## 1.6 防尘装置

再看一下硬盘的两个防尘装置，

<p align="center"><img src="/assets/img/how-hdd-works/de-dust.png" width="50%" height="50%"></p>

1. **<mark>垫圈</mark>**：将磁盘密封起来；
1. **<mark>灰尘过滤器</mark>**：用于捕获灰尘颗粒。

密封和过滤都是非常必要的，因为读写头距离盘片仅 **<mark><code>15nm</code></mark>**，
而灰尘颗粒的大小可达 **<mark><code>10,000nm</code></mark>**，
如果与 7200rpm 高速旋转磁盘碰撞，可能会造成严重损坏，

<p align="center"><img src="/assets/img/how-hdd-works/de-dust-2.png" width="60%" height="60%"></p>
<p align="center">Fig. 读写头正常运行时，距离盘片仅 15nm。</p>

# 2 盘片的微观组成

了解了粗粒度的硬件构成之后，现在让来深入到盘片的内部，看看它的微观组成。

## 2.1 磁盘（disk） -> 磁道（track）

首先，每个**<mark>磁盘</mark>**以同心圆的方式分割为多个**<mark>磁道</mark>**（concentric circles of tracks），

<p align="center"><img src="/assets/img/how-hdd-works/tracks.png" width="45%" height="45%"></p>
<p align="center">Fig. 磁盘分割为大量磁道。</p>

每个磁盘的磁道数量能达到 **<mark><code>500,000</code></mark>** 个甚至更多。

## 2.2 磁道（track） -> 扇区（sector）

然后，沿着直径的方向，所有磁道又被分割为多个扇区，

<p align="center"><img src="/assets/img/how-hdd-works/sectors.png" width="60%" height="60%"></p>
<p align="center">Fig. 磁道进一步分割为扇区。</p>

## 2.3 扇区内

现在看一下每个扇区内的结构，

<p align="center"><img src="/assets/img/how-hdd-works/sector-internals.png" width="60%" height="60%"></p>
<p align="center">Fig. 每个扇区的内部结构。</p>

如上图所示，每个扇区中，依次包含五部分。

### 2.3.1 前导/同步区（preamble or synchronization zone）

记录这个**<mark>旋转磁盘的确切速度</mark>**和**<mark>每个比特位的长度</mark>**（length of each bit of data）。

### 2.3.2 地址区

帮助读/写头确定当前位于**<mark>哪个磁道和扇区</mark>**。

### 2.3.3 数据区

#### 扇区大小

扇区的大小**<mark>因盘而异</mark>**，例如老一些的盘是 512 字节或 2KB，新一些的通常是 4KB。

#### 查看磁盘扇区大小（译注）

有很多工具可以查看，**<mark><code>lsblk</code></mark>** 指定显示磁盘名字、物理扇区大小和逻辑扇区大小：

```shell
$ lsblk -o NAME,PHY-SeC,LOG-SeC
NAME                   PHY-SEC LOG-SEC
sda                       4096     512  # 这块是 SATA SSD
sdb                        512     512  # 这块是 SATA HDD
```

**<mark><code>fdisk -l</code></mark>**，这个命令好记：

```shell
$ fdisk -l
Disk /dev/sdb: 2.18 TiB, 2399276105728 bytes, 4686086144 sectors
Disk model: XXX                                                    # 硬盘型号
Units: sectors of 1 * 512 = 512 bytes                              # 当前扇区大小
Sector size (logical/physical): 512 bytes / 512 bytes              # 逻辑值 & 物理支持的最大值
I/O size (minimum/optimal): 512 bytes / 512 bytes
```

#### `iostat` 磁盘读写带宽（译注）

可以通过 [`cat /proc/diskstats`](https://www.kernel.org/doc/Documentation/ABI/testing/procfs-diskstats)
查看磁盘的读写情况，其中就包括了每个磁盘已经读写的 sectors 数量：

```shell
$ cat /proc/diskstats
#                            r_sectors               w_sectors
   8       0 sda 31663 10807 2928442     8471 203024 106672     6765800 ...
```

这个数量乘以 sector 大小，就是已经读写的字节数，**<mark>iostat 等工具显示的磁盘读写带宽</mark>**，就是根据这个来计算（估算）的。

#### 一个扇区只会属于一个文件（译注）

根据 wikipedia [Disk sector](https://en.wikipedia.org/wiki/Disk_sector)，
对于绝大部分文件系统来说，任何一个文件都是占用整数个扇区的 —— 也就是说一个扇区只会属于一个文件，
如果没用满，后面的就空着。所以在调整扇区大小时，这是一个需要考虑的因素。

#### 扇区与 block 的关系（译注）

这里说的 block 是文件系统的概念，比如常见的一个 block 是 4KB，如果磁盘格式化的时候，扇区大小选择的 512B，那一个 block 就对应 8 个扇区。
对操作系统屏蔽了底层的硬件细节。

### 2.3.4 纠错码（ECC）区

<p align="center"><img src="/assets/img/how-hdd-works/sector-internals.png" width="60%" height="60%"></p>
<p align="center">Fig. 每个扇区的内部结构。</p>

用于校验存储在块中的数据。

### 2.3.5 扇区之间的间隔区

给了读/写磁头一定的容错能力。

# 3 写数据

现在让我们进一步看看读/写磁头的内部机制，以及写头（write head）是是如何写数据的。

## 3.1 磁场微块和磁化

扇区是由一个个磁场微块组成的，
写头通过**<mark>改变磁盘微块的磁化方向</mark>**来实现数据写入，

<p align="center"><img src="/assets/img/how-hdd-works/write-head-1.png" width="60%" height="60%"></p>

每个磁盘微块大小约为 **<mark><code>90nm x 100nm x 125nm</code></mark>**，

<p align="center"><img src="/assets/img/how-hdd-works/write-head-2.png" width="35%" height="35%"></p>

磁化之外，微块内原子的南北极是随机的；
磁化之，微块所有原子的北南极都指向同一方向，

<p align="center"><img src="/assets/img/how-hdd-works/write-head-3.png" width="35%" height="35%"></p>

每个微块对应的就是一个 bit 数据，

<p align="center"><img src="/assets/img/how-hdd-works/write-head-4.png" width="80%" height="80%"></p>

## 3.2 写入 1bit 的过程

下面具体看一下如何磁化一个微块（相当于写入 1bit 数据）。

电流施加到 write head 的线圈之后，就会在此处产生一个强磁场，

<p align="center"><img src="/assets/img/how-hdd-works/write-head-5.png" width="40%" height="40%"></p>

这个磁场沿着 write head 向下，聚焦到尖端的一个小点，改变它正下方的磁盘微块极性
（中间的缝隙就是前面提到过的读写头 **<mark><code>15nm</code></mark>** 悬浮高度），

<p align="center"><img src="/assets/img/how-hdd-works/write-head-6.png" width="40%" height="40%"></p>

磁化之后的微块变成**<mark>永磁体</mark>**，能保持这个状态很多年，也就是数据已经**<mark>持久化</mark>**，
以后可以重复用读头感应这个永久磁场，读出存储的数据。

<p align="center"><img src="/assets/img/how-hdd-works/write-head-7.png" width="50%" height="50%"></p>

## 3.3 覆盖写

原理跟上面一样，也是逐 bit 来。
如果新写入的 bit 跟已经存储的一样，磁极就不变，否则就改变一下方向。

# 4 读数据

再来看看如何从磁盘读数据。

## 4.1 如何表示 0 和 1

### 4.1.1 不是用南北极指向表示

前面我们假设了不同南北极的磁块分别表示 0 和 1，

<p align="center"><img src="/assets/img/how-hdd-works/bit-representation-1.png" width="30%" height="30%"></p>

这在概念上非常简单，但实际实现并非如此。

### 4.1.2 用南北极指向的变化表示

实际的 read head，检测的是**<mark>相邻两个微块的磁极变化</mark>**，
这是因为**<mark>磁极变化的强度</mark>**比**<mark>单个微块的磁场强度</mark>**要大得多，所以这种方式的**<mark>检测准确率非常高</mark>**。

<p align="center"><img src="/assets/img/how-hdd-works/bit-representation-2.png" width="65%" height="65%"></p>

所以，如上图所示，

* 相邻微块磁场方向变化，表示 1；
* 相邻微块磁场方向不变，表示 0。

## 4.2 读头（read head）内部结构

那么，检测这些磁场的读头内部结构是怎样的呢？

<p align="center"><img src="/assets/img/how-hdd-works/read-head-1.png" width="70%" height="70%"></p>

如上图所示，

* 读头里面是**<mark>多层导电材料</mark>**，由铁磁材料和非磁性材料的交替组成。
* 这种多层材料具有一种称为巨磁阻（giant magnetoresistance, **<mark><code>GMR</code></mark>**）的特性，
  简单来说，穿过它的**<mark>磁场强度发生变化时，它的电阻率就会变化</mark>**。

## 4.3 读取数据：GMR 和读头电阻率

基于 GMR 特性，根据**<mark>读头的电阻率</mark>**就能判断下面存储的 0 还是 1，

<p align="center"><img src="/assets/img/how-hdd-works/read-head-3.png" width="60%" height="60%"></p>

* 电阻率较低时，表示读取头下方磁场变化强，对应存储的是 bit `1`；
* 电阻率较高且无磁场时，对应存储的是 bit `0`。

## 4.4 连续 0 的问题

以上过程有一个问题：如果**<mark>较长连续区域的磁极都一样</mark>**，对应的就是一长串的 `0`，由于读头的精度，有可能会导致多读或少读几个 0，导致数据错乱。

解决方少：利用每个 sector 的前导区和纠错码区中的信息。

# 5 致谢

原作者 Branch Education 感谢所有个人赞助者和会员赞助商，让他们制作了如此精良的科普视频。

# 6 Linux 存储相关的子系统和软件栈（译注）

## 6.1 从进程 read/write 请求到 HDD 读写数据

来自 [Linux Storage Stack Diagram](https://www.thomas-krenn.com/en/wiki/Linux_Storage_Stack_Diagram)，
涵盖了 3.x ~ 6.x 多个内核版本，这里先贴一个 **<mark><code>3.x</code></mark>** 的，因为简单，
方便看出从用户进程发出 read/write 请求到 HDD 读写数据的内核模块链路：

<p align="center"><img src="/assets/img/how-hdd-works/Linux-io-stack-diagram_v0.1.svg" width="100%" height="100%"></p>

虚拟文件系统（VFS）里面分为几类：

1. **<mark>常规文件系统</mark>**（ext4, xfs, btrfs, ...）；
2. **<mark>网络文件系统</mark>**（NFS, CIFS, ...）；
3. **<mark>伪文件系统</mark>**（procfs, sysfs, ...）；
4. **<mark>特殊文件系统</mark>**（tmpfs, devtmpfs, ...）。

再贴一个 **<mark><code>kernel v6.9</code></mark>** 的，

<p align="center"><img src="/assets/img/how-hdd-works/Linux-storage-stack-diagram_v6.9.png" width="100%" height="100%"></p>

## 6.2 内核 block layer 深入解读

1. [A block layer introduction part 1: the bio layer](https://lwn.net/Articles/736534/), LWN.net, 2017
1. [A block layer introduction part 2: the request layer](https://lwn.net/Articles/738449/), LWN.net, 2017

## 6.3 其他优质文章

1. [How does a hard drive work](https://www.explainthatstuff.com/harddrive.html), https://www.explainthatstuff.com/, 2024

    除了硬件拆解和介绍工作原理，还对比了 HDD 和 SDD，并且更重要的，介绍了 **<mark>IBM 发明硬盘的历史</mark>**。

2. [How a Hard Drive Works](https://cs.stanford.edu/people/nick/how-hard-drive-works/), cs.stanford.edu, 2012

    斯坦福的一个老师实物教学，**<mark>开盖展示读写数据时，硬盘的工作过程</mark>**（然后这个盘就报废了）。

3. [HDD from Inside: Hard Drive Main Parts](https://hddscan.com/doc/HDD_from_inside.html), https://hddscan.com/

    硬件拆解部分比本文更详细，想了解更多硬件细节的，可作为补充。

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
