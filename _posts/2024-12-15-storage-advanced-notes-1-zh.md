---
layout    : post
title     : "存储进阶笔记（一）：硬件基础：HDD/SDD、JBOD、RAID 等（2024）"
date      : 2024-12-15
lastupdate: 2024-12-15
categories: storage hardware storage-note-series
---

记录一些平时接触到的存储知识。由于是笔记而非教程，因此内容不求连贯，有基础的同学可作查漏补缺之用。

<p align="center"><img src="/assets/img/storage-advanced-notes/ceph-jbod.png" width="80%" height="80%"></p>
<p align="center">Fig. 12 Left: HDDs as a JBOD, present to OS as 12 independent devices (sd*), running a Ceph OSD service on each device. Right: speedup performance with high-end RAID cards.</p>

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

# 1 磁盘的硬件组成和工作原理

## 1.1 HDD 和 SSD

* [HDD 是如何工作的：旋转硬盘内部结构与工作原理的动画展示（2022）]({% link _posts/2024-11-16-how-hdd-works-zh.md %})
* [SSD 是如何工作的：固态硬盘内部结构与工作原理的动画展示（2020）]({% link _posts/2024-11-16-how-hdd-works-zh.md %})

## 1.2 直接使用 HDD/SDD 面临的问题

1. 单个磁盘的容量、性能等不够
2. 冗余/高可用需求

解决办法：RAID、JBOD、LVM 等等。

# 2 容量不够，JBOD (Just a Bunch Of Disks) 来凑

## 2.1 定义

JBOD 在 Wikipedia 中没有单独的词条，
而是归类在 [Non-RAID drive architectures](https://en.wikipedia.org/wiki/Non-RAID_drive_architectures#JBOD) 中。

JBOD 是一种架构，

* **<mark>往下管理的是多个磁盘</mark>**，这里所说的“磁盘”可以是
    * 物理设备，
    * 逻辑卷（logical volume），又分为几种，
        * 多个物理设备组合成的一个逻辑卷，比如用 LVM 或者 mdadm 之类的工具（后面会介绍）；
        * btrfs 之类的能跨设备的文件系统（device-spanning filesystem）
* **<mark>往上呈现给操作系统的是一个或多个独立设备</mark>**（devices，**<mark><code>/dev/xxx</code></mark>**）。

最简化的理解：使用 JBOD 模式，那机器上插了几个盘，操作系统中就能看到几个 `/dev/sd*` 设备。

比如下图是一台 12 盘的 Ceph 机器。Ceph 的设计中，每个盘由一个独立的进程来管理，也就是它的 OSD 进程，
所以就适合做 JBOD（但 RAID 也是可以的，右边所示 [2]），

<p align="center"><img src="/assets/img/storage-advanced-notes/ceph-jbod.png" width="80%" height="80%"></p>
<p align="center">Fig. 12 Left: HDDs as a JBOD, present to OS as 12 independent devices (sd*), running a Ceph OSD service on each device. Right: speedup performance with high-end RAID cards.</p>

## 2.2 优缺点


* 无冗余：每个盘（或逻辑 volume）都是独立的，可以独立访问，在其他盘上没有冗余，坏了里面的数据就没了；
* 每个盘都是独立的，所以加减盘比较简单和方便（作为对比，RAID 加减盘就得考虑数据重新分布了）；
* 可扩展性和灵活性比较好。可以将不同大小的盘组合到一起；
* 灵活控制数据存储和备份策略；
* 性能上就是多个盘的叠加，没有额外性能提升（相比某些 RAID 之类的）；
* **<mark>便宜</mark>**，不怎么花钱。

## 2.3 使用场景

* 需要独立盘的场景，例如 Ceph OSD；
* 动态扩容比较频繁的场景，例如云存储；
* 需要精确控制备份策略的场景。

## 2.4 类似功能的软件：LVM

JBOD 是硬件特性，主板的存储控制器自带这个功能，一般的 RAID 卡也支持 JBOD 模式。

也有一些具有类似功能的软件，比如 LVM (Logical Volume Manager)。
下一篇再介绍。

# 3 花钱办事：硬件 RAID 卡数据冗余+提升性能

## 3.1 定义

RAID 是 Redundant Array of Independent Disks 的缩写，独立磁盘冗余阵列，可以提供多种级别的数据容易，防止因为单个磁盘故障导致数据丢失或不可用。
RAID 本身只是一种技术。实现上可以是硬件 RAID 卡，也可以是纯软件方案。

我们**<mark>接下来讨论的主要是硬件 RAID 卡</mark>**。

## 3.2 分类

### 3.2.1 按 RAID 模式分类

可参考 [2]，不错的介绍和软件 raid 教程。

## 3.2.2 按有无缓存（write back cache）分类

RAID 卡上有没有内存：

* 无
    * 低端卡，便宜
    * 数据直接写入磁盘（**<mark><code>write-throught</code></mark>**）。无加速能力，但能做硬件 RAID，性能比纯软件的 RAID 还是要好。
* 有
    * 高端卡，贵
    * **<mark>数据写到 RAID 卡内存后直接返回</mark>**（**<mark><code>write-back</code></mark>**)，极大提高性能。

### 查看 WB cache 大小

```
$ ./storcli64 /c0 show all | grep "Current Size"
Current Size of FW Cache (MB) = 6675
```

## 3.3 实物图及使用方式

### 3.3.1 SATA/PCIe RAID

以下是 Broadcom MegaRAID 9560-16i 8GB RAID 卡，自带 **<mark>8C 处理器，8GB 内存</mark>**。

<p align="center"><img src="/assets/img/storage-advanced-notes/MegaRAID-9560-16i-8GB.png" width="60%" height="60%"></p>
<p align="center">Fig. Broadcom MegaRAID 9560-16i 8GB RAID Controller.</p>

RAID 卡本身作为 PCIe 卡插到主板上，磁盘通过 SATA 接口插到右侧（也可以加转换线，将 PCIe 接口的 NVME SSD 插到右侧）。
一些产品参数 [3]：

* PCIe 4.0 RAID 卡
* 单个 RAID 卡最多能支持 240 SAS/SATA devices or 32 NVMe devices
* 支持 RAID 0, 00, 1, 5, 6, 10, 50 and 60
* JBOD mode with RAID 0, 1, 10 and JBOD for SDS environments

### 3.3.2 `M.2` RAID

NVME SSD 有两种常见的接口格式：

1. PCIe 格式：这种 SSD 数据线直接插在主板的 PCIe 插槽上就行了，速度已经很快，例如 PCIe Gen4 的实测写入带宽能打到 3GB/s 左右，Gen5 的写入带宽号称能到 8GB/s。
2. M.2 格式：体积很小，插在主板上的 M.2 插槽上，速度也很快，但容量一般较小；

如果以上速度还不满足业务需求，可以考虑加上 RAID 卡，下面是 M.2 格式的多个 NVME SSD 做成 RAID 的样子：

<p align="center"><img src="/assets/img/storage-advanced-notes/nvme-m2-raid.jpg" width="60%" height="60%"></p>
<p align="center">Fig. Hardware RAID10 over NVME SSDs. <a href="https://www.techpowerup.com/274549/highpoint-announces-ssd7540-8-port-m-2-nvme-ssd-raid-card">Image Source</a></p>

前面 Broadcom 那个卡也支持 NVME RAID，但支持的 PCIe 格式的 NVME，而且需要通过 PCIe 扩展线来连接。

## 3.4 RAID 卡上为什么要配备电池（或超级电容）？

### 3.4.1 突然掉电的问题

对于有 WB cache 的，如果数据写到了 cache，但还没写到磁盘，掉电了怎么办？会导致数据丢失。
所以引入了配套的电池（BBU, Battery Backup Unit），

* 电池的作用**<mark>不是在断电后将数据刷到磁盘 —— 因为这时候磁盘也没电了</mark>** ——
  而是确保**<mark>缓存中数据的安全，等重新上电后，再刷到磁盘</mark>**；
* BBU 可以保持 RAID Cache 中的数据**<mark>几天</mark>**时间，具体看厂商及电池寿命；
* 没有电池或**<mark>电池失效</mark>**，读缓存还可以用，**<mark>写缓存会自动关闭</mark>**（写性能急剧下降）。

### 3.4.2 BBU vs. supercapacitors

电池能解决掉电丢数据问题，但寿命和故障率是个问题。近几年新出来的另一种保持数据的方式是**<mark>超级电容</mark>**（supercapacitors）。

BBU or SuperCapacitor [4]:

* A BBU has a docked battery that powers the volatile cache memory for up to 72 hours. Like all Li-ion batteries, they will age and need to be replaced in a maintenance slot after about three to five years.
* A SuperCapacitor works differently, but also provides higher security: With the energy stored in the capacitor, the data is quickly shifted into a non-volatile memory and is thus ready for the next start.

### 3.4.3 查看 raid 卡超级电容信息

```shell
$ ./storcli64 /c0/cv show all J | jq
```

## 3.5 降本方案

再回到 RAID 卡本身。东西好是好，但贵，有没有降本的方案呢？

### 3.5.1 VROC (Virtual Raid On CPU)

Intel CPU 独有的技术，CPU 内置硬件模块，[官方介绍](https://www.intel.com/content/www/us/en/software/virtual-raid-on-cpu-vroc.html)。

没用过。

# 参考资料

1. [Considerations for using a RAID controller with OSD hosts](https://docs.redhat.com/en/documentation/red_hat_ceph_storage/8/html/installation_guide/red-hat-ceph-storage-considerations-and-recommendations#considerations-for-using-a-raid-controller-with-osd-hosts-install), redhat.com, 2024
2. [An Introduction to RAID in Linux](https://www.baeldung.com/linux/raid-intro), baeldung.com, 2024
3. [Broadcom MegaRAID 9560-16i 8GB RAID Controller](https://www.broadcom.com/products/storage/raid-controllers/megaraid-9560-16i), 2024
4. [Protecting RAID systems with BBU or SuperCapacitor](https://www.starline.de/en/magazine/technical-articles/protecting-raid-systems-with-bbu-or-supercapacitor), 2024

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
