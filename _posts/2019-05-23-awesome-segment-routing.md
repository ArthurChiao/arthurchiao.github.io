---
layout    : post
title     : "Awesome Segment Routing Resources"
date      : 2019-05-23
lastupdate: 2019-06-06
categories: segment-routing
---

## 1 Introduction & Overview

1. Cisco Doc, **[Segment Routing
   Overview](https://www.cisco.com/c/en/us/td/docs/routers/asr920/configuration/guide/segment-routing/segment-routing-book/overview.pdf)**

    Chapter from ***Segment Routing for Cisco ASR 920 Series Aggregation Services Routers***.

### Chinese Resources

1. **[一文读懂网络界新贵 Segment Routing 技术化繁为简的奥秘
   ](http://net.yesky.com/148/703613148.shtml), 2018**

    非常好的介绍文章，逻辑清晰，详略得当，对 Segment Routing 的前身、场景、解
    决的问题等都讲得很清楚。**如果之前对 Segment Routing 没有任何了解，建议先
    从这篇读起。**

    ----

    SR 的类比：机场行李标签。

    将行李从西雅图寄到柏林 (TXL)，途径墨西哥城 (MEX) 和马德里 (MAD)。在始发机场
    给行李贴一个标签：`{MEX; MAD; TXL}`。航空系统**无需识别单个行李**，而**只需
    识别机场代码**，根据代码将行李发送到下一站。

    SR 类似。始发机场是源节点，机场代码是中间节点标签。SR 会**在源节点压入转发标
    签路径**，中间节点只需**根据标签转发**。几个特点：

    * 源路由：在始发机场加上标签路径
    * 无状态：中间机场无需知道行李从哪来到哪去，只需根据标签转发
    * 集中控制：机场代码由航空系统集中分配和维护（SR 世界里路径标签也是集中计算
      和下发的）

1. 苏远超，**[分段路由（Segment Routing）大规模 SDN 部署必备技术](http://m.mp.huandengpai.com/apps/player/pai/70463)**, 2016

    Cisco 面向客户和合作伙伴的培训视频，主讲人也是 ***Segment Routing Part I***
    中文版译者之一。

## 2 Deep Dive

1. Clarence Filsfils, Kris Michielsen, Ketan Talaulikar, [Segment Routing Part
   I](https://www.amazon.com/Segment-Routing-Part-Clarence-Filsfils-ebook/dp/B01I58LSUO), 2016

   The definitive book.

## 3 Implementation in Linux

1. Lebrun, David, and Olivier Bonaventure. ["Implementing ipv6 segment routing in
   the linux kernel."](https://inl.info.ucl.ac.be/system/files/paper_10.pdf)
   Proceedings of the Applied Networking Research Workshop.  ACM, 2017.

   SR implementation in Linux kernel `4.10`.
