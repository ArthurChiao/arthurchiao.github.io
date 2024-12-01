---
layout    : post
title     : "[译] SSD 是如何工作的：固态硬盘内部结构与工作原理的动画展示（2020）"
date      : 2024-12-01
lastupdate: 2024-12-01
categories: storage hardware
---

### 译者序

本文翻译自 2020 年 Branch Education 的一个科普视频
[How do SSDs Work? How does your Smartphone store data? Insanely Complex Nanoscopic Structures!](https://www.youtube.com/watch?v=5Mh3o886qpg)，
强烈推荐观看原视频。本文整理个图文版方便查阅与思考。

* [HDD 是如何工作的：旋转硬盘内部结构与工作原理的动画展示（2022）]({% link _posts/2024-11-16-how-hdd-works-zh.md %})
* [SSD 是如何工作的：固态硬盘内部结构与工作原理的动画展示（2020）]({% link _posts/2024-11-16-how-hdd-works-zh.md %})

<p align="center"><img src="/assets/img/how-ssd-works/all-concepts-in-one.png" width="50%" height="50%"></p>

水平及维护精力所限，译文不免存在错误或过时之处，如有疑问，请查阅原视频。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

以下是译文。

----

* TOC
{:toc}

----

手机的存储、平板电脑的存储、SSD 硬盘，其实都类似，核心都是一个**<mark>固态</mark>**（Solid State） **<mark>存储芯片</mark>**：

<p align="center"><img src="/assets/img/how-ssd-works/memory-storage-microchip.png" width="60%" height="60%"></p>

称为“固态”是相对于旋转（rotational）磁盘（也就是普通 HDD 硬盘）那种“动态”而言的。

本文将深入到这个芯片内部，看看它是如何工作的。

# 1 存储材料 & 结构：`Charge Trap`

将 SSD 芯片放大到**<mark>纳米级</mark>**，就能看到它存储电荷的**<mark>基本结构</mark>**。

* 根据技术路线的不同，存储结构/材料的选择也不同，
* 本文介绍的是比较新的一种，称为 **<mark><code>Charge Trap</code></mark>**（电荷捕获，或电荷陷阱），
  它使用的是**<mark>氮化矽</mark>**（silicon nitride），这是一种**<mark>绝缘体</mark>**。

下图中的“工”字结构就是 Charge Trap，它的基本原理是**<mark>将电子吸附到氮化矽上</mark>**，
吸附的电子数量不一样，电荷的高低就不一样，从而可以用于表示不同的数字，

<p align="center"><img src="/assets/img/how-ssd-works/charge-trap-1.png" width="80%" height="80%"></p>

图中黄色部分就是吸附的电子，

* 较老的技术只能存储**<mark>2 个不同的电荷级别</mark>**，即电子很多或很少，
  因此**<mark>只能表示两种数值</mark>**，也就是 **<mark><code>1bit</code></mark>** `0` 和 `1`； 
* 较新的 Charge Trap 可以存储 **<mark>8 个或 16 个电荷级别</mark>**，
  也就是每个 Charge Trap 可以表示 **<mark><code>3bit 或 4bit</code></mark>**。

被吸附的电荷可以**<mark>保持几十年</mark>**之久，这也是它被称为电荷陷阱的原因。

# 2 SSD 芯片硬件组成

下面从小到大，看看是如何基于 Charge Trap 这样一个最基本单元构建出一个最终的 SSD 芯片的。

## 2.1 `Charge Trap` -> 基本存储单元 `Memory Cell`

Charge Trap 是 SSD 的基本存储单元 —— **<mark><code>memory cell</code></mark>** —— 的核心。

在本文接下来的内容中，我们假设一个 charge trap 支持 8 个不同的电荷级别，也就是说可以表示 3bit，
比如吸附的电子很少对应 `111`，吸附的电子很多对应 `000`。

下面简单介绍下读取和删除数据对应的底层操作。

### 2.1.1 读取数据

读取一个 memory cell 存储的数据，就是**<mark>测量这个 Charge Trap 上的的电荷量</mark>**，

<p align="center"><img src="/assets/img/how-ssd-works/charge-trap-2.png" width="50%" height="50%"></p>

这需要先通过 control gate 锁定该 Charge Trap，然后信息就可以从中间的传输线送上去。
后面会详细介绍。

### 2.1.2 删除数据

删除一个 memory cell 存储的数据，就是**<mark>清除这个 Charge Trap 上的的电荷量</mark>**，
使其回到最低电平（`111`）。 

## 2.2 纵向堆叠 `Memory Cell` -> `String`

有了能表示 3bit 的基本单元，接下来我们将 N 个 cell 垂直堆叠起来，
就得到一个称为 **<mark><code>String</code></mark>**（“串”）的结构。

下图是 10 个 memory cell 堆叠成的 string，

<p align="center"><img src="/assets/img/how-ssd-works/vertical-string-2.png" width="60%" height="60%"></p>

一个 String 内的所有 cell 共享顶部的 **<mark><code>bit line</code></mark>**（“bit 传输线”，读取或写入 cell 数据的线），

<p align="center"><img src="/assets/img/how-ssd-works/bit-line.png" width="60%" height="60%"></p>

一个 String 有很多 cell，但它们共享同一根 bit line，
因此，在任一时间只能激活 String 中的一个 cell。为此，需要引入了 **<mark><code>control gate</code></mark>**。

* control gate 控制 String 上的哪个 cell 可以读写数据，此时称为“激活”状态；
  如上图所示，读取第 10 层的 cell 信息时，就激活第 10 层的 control gate：
* 但注意，control gate 只是用来**<mark>激活 cell</mark>**，而不是用来读取 cell 的信息：
  比如在读数据场景，被激活的 cell 会将它保存的信息通过 String 中心的数据线（每个“工”字的中心线）发送给顶部的 **<mark><code>bit line</code></mark>**。

## 2.3 横向堆叠 `Memory Cell` -> `Page`

将多个 String 水平连到一起，就得到一个二维 cell 空间。

横向的每一排 memory cell，称为一个 **<mark><code>Page</code></mark>**（“页”），如下图所示：

<p align="center"><img src="/assets/img/how-ssd-works/memory-cell-string-page.png" width="60%" height="60%"></p>

## 2.4 String+Page 组成 2D 存储矩阵 -> Row

String+Page 组成的 2D 存储矩阵，称为 **<mark><code>Row</code></mark>**（虽然在这里直觉上叫“Page”更合适，后面会看到这个名称的由来），

<p align="center"><img src="/assets/img/how-ssd-works/row.png" width="60%" height="60%"></p>

### 2.4.1 bit line 和 control gate

再来看下 bit-line/control-gate 和 String/Row 的关系，

<p align="center"><img src="/assets/img/how-ssd-works/32x-bit-lines.png" width="60%" height="60%"></p>

* 每个 String 有独立的 bit line；
* 每个 Row 上的所有 cell 共享一个 control gate，

### 2.4.2 读写一个 Page：仅需一次 control gate 操作

由上图可知，向 Row 写入或读取数据时，**<mark>横向的 cell 能同时被激活</mark>**，它们能通过顶上的 bit lines 并行传输。

换句话说，**<mark>一个 Page 内的数据仅需一次操作就能全部读出或写入</mark>**。

## 2.5 多个 Row（2D）堆叠成 3D 存储模块 -> `Block`

将 N 个 Row 并排连起来，就得到一个 block。下面是 6 个 Row 组成的 block，

<p align="center"><img src="/assets/img/how-ssd-works/block.png" width="50%" height="50%"></p>

下面是 12 个 Row 组成的 block，

<p align="center"><img src="/assets/img/how-ssd-works/column-row-layer.png" width="50%" height="50%"></p>

### 2.5.1 渲染图（3D-NAND / V-NAND）

这种立体的 Block 有个专业名词叫 **<mark><code>3D-NAND</code></mark>** 或 **<mark><code>V-NAND</code></mark>**（垂直堆叠 NAND），
以为以前的芯片都是二维的，

<p align="center"><img src="/assets/img/how-ssd-works/v-nand.png" width="50%" height="50%"></p>

NAND 本身是 **<mark><code>Not AND</code></mark>**（“与非”门）的缩写，是一种逻辑门，后来泛指一类存储技术。

### 2.5.2 Block 能存储多少数据：`~1.5KB`

现在让我们来算一下，一个 block 能存储多少数据。

<p align="center"><img src="/assets/img/how-ssd-works/total-bits.png" width="50%" height="50%"></p>

* 3bit/cell
* 10 cells/string
* 32 cells/page
* 6 rows/block
* 2 block

最终是 3,840 个 memory cell， 总共能够存储 **<mark><code>11,520 bit</code></mark>**，约 **<mark><code>1.4KB</code></mark>**。 

## 2.6 小结

回顾下我们目前为止介绍的所有概念，

<p align="center"><img src="/assets/img/how-ssd-works/all-concepts-in-one.png" width="50%" height="50%"></p>

从小到大的结构是：cell -> String / Page -> Row -> Block。
这里还有 Column 和 Layer 的概念，这个图加上这俩概念，就不难理解为什么一个 2D cell 矩阵叫 Row 而不叫 Page 了。

# 3 真实 SSD 产品的参数

## 3.1 Block

### 3.1.1 高度（Cells per String）：100~200 cells

图中画的是 96~136 层高，**<mark>右边是一张纸</mark>**，可以直观理解 100~200 层大概是什么概念。

<p align="center"><img src="/assets/img/how-ssd-works/96-136-layers.png" width="60%" height="60%"></p>

### 3.1.2 宽度（Cells per Page）: 30K~60K cells

一个 Page 的宽度约为 30,000~60,000 个 memory cell。

<p align="center"><img src="/assets/img/how-ssd-works/real-ssd-width.png" width="50%" height="50%"></p>

这意味着有 **<mark><code>30,000~60,000</code></mark>** 可并行读写的 **<mark><code>bit lines</code></mark>**。

### 3.1.3 深度（Rows per Block）：4~8 Rows

4~8 个 Row 组成一个 Block，

<p align="center"><img src="/assets/img/how-ssd-works/real-ssd-depth.png" width="50%" height="50%"></p>

## 3.2 Blocks per Chip Unit: 4K~6K

一个最基础的芯片单元有大约 **<mark><code>4000~6000</code></mark>** 个 Block（后面还将重复这个基础单元很多次，最终封装成一个芯片）。

## 3.3 Row decoder, **<mark><code>Page Buffer</code></mark>**

<p align="center"><img src="/assets/img/how-ssd-works/row-decoder.png" width="40%" height="40%"></p>

* 两侧的 control gate & bit line selector 组成了所谓的行解码器，通过这两组选择器就可以访问**<mark>任意 Page</mark>**；
* 一个 **<mark><code>Page</code></mark>**（约 45,000 个 memory cell）能同时使用上方并行的 bit line 来读取或写入信息；
* 上万条 bit line 将 Page 中的数据送到 **<mark><code>Page cache</code></mark>**。

下图是对应到实际芯片的结构，

<p align="center"><img src="/assets/img/how-ssd-works/ssd-1.png" width="50%" height="50%"></p>

图中的产品为了提高存储容量，将 3.2 介绍的模块复制了一倍。
这样一个模块的读写速度约为 **<mark><code>500MB/s</code></mark>**，

<p align="center"><img src="/assets/img/how-ssd-works/ssd-2.png" width="50%" height="50%"></p>

## 3.4 多层 Chip Unit，封装到最终的一块 SSD 芯片

为了进一步提高存储容量，在一个芯片中放 8 个（层）上一节那样的子芯片，
然后通过外围接口芯片（下图最左侧）来协调这 8 个子芯片，

<p align="center"><img src="/assets/img/how-ssd-works/ssd-3.png" width="60%" height="60%"></p>

这样一个结构再加个外壳封装，才是我们**<mark>拆开 SSD 时在电路板上看到的芯片</mark>**：

<p align="center"><img src="/assets/img/how-ssd-works/ssd-4.png" width="60%" height="60%"></p>

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
