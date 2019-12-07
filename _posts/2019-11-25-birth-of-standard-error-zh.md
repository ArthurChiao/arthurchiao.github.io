---
layout    : post
title     : "[译] 标准错误 stderr 的诞生（2013）"
date      : 2019-11-25
lastupdate: 2019-12-06
categories: c
---

### 译者序

本文翻译自 2013 年的一篇英文博客 [The Birth of Standard Error](https://www2.dmst.aueb.gr/dds/blog/20131211/index.html)。

从这则故事可以看出，即使是对这些极富传奇色彩的大佬，“需求是第一生产力” 也是
成立的。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

今天早些时候，Stephen Johnson 在 [The Unix Heritage Society](The Unix Heritage
Society)（Unix 遗产学会）的 mailing list 中描述了**标准错误**（`stderr`）概念是
如何诞生的，其思想是：应该通过一个独立于**正常输出**（normal output）的管道（
channel）来发送**错误输出**（error output）。

在过去的四十年中，所有的主流操作系统和语言库都拥抱了这个概念。

<p align="center"><img src="/assets/img/birth-of-stderr/cat.png" width="30%" height="30%"></p>

故事开始于 贝尔实验室在 1970s 年代使用的 Graphic Systems 公司的 [C/A/T
phototypesetter](https://en.wikipedia.org/wiki/CAT_(phototypesetter))（照相排版
机，或称照相排字机）。这个洗衣机大小的设备能够通过闪光灯频闪的方式（flashing a
strobe）对预置在旋转磁鼓上的那些**字符字形**（character glyphs）进行**曝光**，从
而实现对文档的排版。

<p align="center"><img src="/assets/img/birth-of-stderr/drum.png" width="40%" height="40%"></p>
<p align="center">译注：补一张磁鼓存储器的老照片，来自 https://segmentfault.com/a/1190000021052139 </p>

* 磁鼓支持**四种字体**（Times Roman、italic、bold、和 Symbol）
* 有一个**放大镜用于改变字体大小**
* **文本输入**（text input）通常来自**纸质磁带**（paper tape），**输出**（output
  ）会被渲染道到胶卷（film）
* 随后是一个缓慢、肮脏、刺鼻的**显影过程**（development
  process）。[Brian Walden](http://minnie.tuhs.org/pipermail/tuhs/2013-December/002927.html) 称 “
  化学药水池非常难闻，并且还会粘到滚筒上。你需要定期地将显影滚筒（developer
  roller）和内齿轮带到清洁室，在洗刷池里打开水龙头用牙刷不断冲洗。”

大家可能已经猜到，贝尔实验室不会采用纸带输入这么挫的方式。
[根据 Doug McIlroy 的说法](http://minnie.tuhs.org/pipermail/tuhs/2013-December/002929.html)，“那台机器一
到货，Joe Ossanna （`troff` 的作者）就**旁路（bypass）**了纸带读取器（tape
reader），这样就可以**直接从 PDP-11 驱动 C/A/T** 了。制造商对此大吃一惊。” **连
接是单向的，因此相应的设备只有 write-only 权限，并且没有其他方式从这台机器获得
反馈状态**。Doug McIlroy 也记得当时一件很有趣的事，“用 C/A/T 打印出来的第一份
技术论文送到期刊编辑手上后，这位编辑问道：这篇论文之前在其他地方发表过吗？—— 因为
他此前从未见过排版机打印出来的原稿”

这项**费力**但**先进**（arduous but cutting-edge）的照相排版过程为标准错误概
念的诞生提供了基础。[来看 Stephen Johnson 的描述](http://minnie.tuhs.org/pipermail/tuhs/2013-December/002933.html)：

> “照相排版机最有趣和最意想不到的结果是 Unix 标准错误文件（stderr）的诞生！
> 排版之后，你必须取下一个又长又宽的纸带，然后小心翼翼地将其送入一个气味难闻又黏
> 糊糊的机器，过一会（几分钟之后）这个机器把纸带吐出时，上面的字就清晰可见了。”
>
> “某个下午，我们几个人又在重复相同的过程 —— 对一些东西进行排版，将纸带送入处理
> 机器，最终的显影成像非常漂亮，但只有一行内容："cannot open file foobar"。大
> 家忍不住对此大发牢骚 —— 但抱怨是否管用，要看聆听你抱怨的是否是正确的人。几天之
> 后，标准错误文件（stderr）就诞生了 ...”
