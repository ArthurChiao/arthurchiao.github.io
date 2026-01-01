---
layout    : post
title     : "[译] 软件领域的工业革命：AI 将使软件成为一种新的 UGC（2025）"
date      : 2026-01-01
lastupdate: 2026-01-01
categories: ai software
---

### 译者序

本文翻译自 2025 年的一篇文章 [The rise of industrial software](https://chrisloy.dev/post/2025/12/30/the-rise-of-industrial-software)。

工业化能以极大的规模生产低质量、低成本的产品，

* 印刷工艺的工业化导致了<strong><mark>平装书</mark></strong>的出现
* 农业的工业化导致了<strong><mark>垃圾食品</mark></strong>的出现
* 数字图像传感器的工业化导致了海量<strong><mark>普通人拍摄的图片、视频</mark></strong>等等

LLM 的出现是软件领域的蒸汽机时刻，软件开发正在经历一次属于它的“工业革命”，

* 软件开发正在从<strong><mark>传统手工业</mark></strong>变成<strong><mark>制造业</mark></strong>
* 一旦生产成本足够低，<strong><mark>垃圾</mark></strong>就是能<strong><mark>最大化产量、利润和市场覆盖度</mark></strong>的东西
* 最终市场上流通的<strong><mark>不是丰富的好东西，而是过量的最易消费的东西</mark></strong> —— 我们确实正在消费它们（AI 垃圾）
* 人类程序员未来还有多少市场？未来的创新将是什么？

<p align="center"><img src="/assets/img/rise-of-industrial-software/industry.png" width="70%" height="70%"></p>

水平及维护精力所限，译文不免存在错误或过时之处，如有疑问，请查阅原文。
<strong><mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark></strong>。

以下是译文。

----

* TOC
{:toc}

----

**<mark><code>Industrial</code></mark>** 一词在牛津词典的定义：

> Industrial
>
> adj. (sense 3a)
>
> Of or relating to productive work, trade, or manufacture, esp. mechanical industry or large-scale manufacturing; ( also) resulting from such industry.
>
> —Oxford English Dictionary

# 1 软件开发的“工业革命”：从手工业到制造业

## 1.1 手工业：成本高、开发慢，高度依赖人的专业技能和经验

从历史看，软件开发更接近于<strong><mark>手工业</mark></strong>（craft）而非<strong><mark>制造业</mark></strong>（manufacture）：
<strong><mark>成本高、开发慢，且高度依赖人的专业技能和经验</mark></strong>。

## 1.2 制造业：成本低、开发快、很少依赖人的专业知识

现在，AI coding 正在快速改变这一现状，它使得产品开发更加地<strong><mark>低成本、快速、且越来越不依赖人的专业知识</mark></strong>。

## 1.3 软件开发日益自动化的世界

我之前曾说 [AI coding can be a trap for today's practitioners](https://chrisloy.dev/post/2025/09/28/the-ai-coding-trap) ，
它看似能快速给出一个实现，但经常细看就会发现给出的方案相当不完整，而且后期理解和维护成本很高。
不过随着工具集的不断完善，这些问题都在快速解决，很明显我们正在迈向一个**<mark>软件开发日益自动化的时代</mark>**。

当软件开发经历一次**<mark>“工业革命”</mark>**，会发生什么？

# 2 软件作为一次性商品

## 2.1 现状：劳动力（程序员）贵，生产（软件开发）有规模瓶颈

传统上，软件的生产成本很高，主要是来自**<mark>具备专业技能的专业劳动力</mark>**的成本，简单说就是程序员的成本。

在这个时期，由于强依赖人力，因此从世界范围内看，<strong><mark>程序员的规模也决定了能开发出的软件规模的上限</mark></strong>。
在这个阶段，软件作为一种具备价值属性的商品，由于其开发是有不小成本的，因此公司都把钱花在开发<strong><mark>有价值的软件</mark></strong>上。

## 2.2 工业化的本质：自动化（不依赖人、低成本）

任何领域的工业化都试图同时解决以上两个限制，通过**<mark>流程自动化</mark>**

1. **<mark>减少对人类劳动的依赖</mark>**，既降低成本，
2. 又允许更大规模和更灵活的生产。

这种变化将人类的角色降级为<strong><mark>监督、质量控制和工业流程的优化</mark></strong>。

### 影响一：传统开发模式受到挤压，门槛降低，劳动力（程序员）竞争加剧

这种变化的第一层影响是<strong><mark>传统的高质量的软件生产方式</mark></strong>受到挤压。

行业的进入门槛降低，竞争加剧，变化速度加快 —— 所有这些影响今天都已经开始显现了。

### 影响二：大规模生产低质量、低成本的软件

这种工业化的第二层影响是能够<strong><mark>以极大的规模生产低质量、低成本的产品</mark></strong>。
其他领域的例子包括：

* 印刷工艺的工业化导致了<strong><mark>平装书</mark></strong>的出现
* 农业的工业化导致了<strong><mark>垃圾食品</mark></strong>的出现
* 数字图像传感器的工业化导致了海量<strong><mark>普通人拍摄的图片、视频</mark></strong>等等

## 2.3 一次性软件（disposable software）

软件领域的工业化催生了一类新的编程产物，我们可以称之为**<mark>一次性软件</mark>**（disposable software）：
这种软件的**<mark>所有权、后续维护和长期可理解性</mark>**都是完全没有保证的。

<p align="center"><img src="/assets/img/rise-of-industrial-software/industry-1.svg" width="70%" height="70%"></p>
<p align="center">传统软件：<strong><mark>高成本、高价值</mark></strong>；一次性软件：<strong><mark>低成本、低价值</mark></strong>。</p>

这种产物的支持者可能会将其称为 vibe-coded software，怀疑者则会称为 AI slop（<strong><mark>AI 垃圾、泔水</mark></strong>）。

显然，不管其质量如何，这种软件的<strong><mark>经济学价值</mark></strong>是与传统软件完全不同的，
因为其易于复制，因此单位软件的经济价值较低。

这种低价值属性可能会让一些人认为这一趋势是昙花一现，但这么想就错了。
要理解原因，我们可以看看以前稀缺商品的工业化普及的例子。

# 3 稀缺商品的工业化生产

## 3.1 Jevons 悖论

### 煤炭：单位效率提升，单位成本下降，总消费上升

[Jevons 悖论](https://en.wikipedia.org/wiki/Jevons_paradox)是一个古老的经济学理论，
最近被广泛引用。这一观察可以追溯到十九世纪，
它指出**<mark>单位煤炭效能的提升会导致成本下降，进而会导致用户更大的需求量，最终导致更高的总体煤炭消费</mark>**。

<p align="center"><img src="/assets/img/rise-of-industrial-software/industry-2.svg" width="80%" height="80%"></p>
<p align="center">Jevons 悖论描述了单位效率提高如何导致总体消费增加。</p>

### Token：单位推理成本下降，推理需求变多，总算力消费激增

今天类似的场景是我们<strong><mark>对 AI 计算的需求激增</mark></strong>：
随着模型在预测 token 方面变得更高效，需求激增，导致更大的 token 消费。
同样的效果会波及软件开发本身吗？随着努力成本的降低，是否会推动更高的消费和产出？历史表明会如此。

## 3.2 农业领域的先例：食物生产的工业化：垃圾食品

考虑农业的工业化。

### 消灭饥饿 vs. 垃圾食品

* 二十世纪初，人们认为科学进步将消除饥饿，迎来一个丰富、营养的食物时代。
* 但直到今天，饥饿和饥荒依然存在。
    * 2025 年，仍有 [3.18 亿人经历急性饥饿](https://www.wfp.org/ending-hunger)，即使在农业盈余的国家也是如此。
    * 与此同时，在最富有的国家，工业食品系统产生了另一种丰富：美国的成年人肥胖率为 40%，糖尿病危机日益严重。

极度加工的（ultraprocessed）食品被广泛认为是有害的，然而 [绝大多数美国人每天仍然在消费它们](https://pmc.ncbi.nlm.nih.gov/articles/PMC8408879/)。

### 丰富的好东西 vs. 过量的最易消费的东西

工业系统毫无意外地给传统食物加工系统造成了压力，结果导致了**<mark>过剩、低质量商品</mark>**在市场上的流通。
这个选择权甚至不是生产者所能把控，因为<strong><mark>一旦生产成本足够低，垃圾就是最大化产量、利润和市场触达的东西</mark></strong>。
最终的结果<strong><mark>不是丰富的好东西，而是过量的最易消费的东西</mark></strong> —— 我们确实正在消费它们。

## 3.3 软件领域：AI 垃圾（用户生成的软件/程序）将不可避免地泛滥

我们对 AI 垃圾的青睐也可能会导致与食物领域同样的结果。

<p align="center"><img src="/assets/img/rise-of-industrial-software/industry-3.svg" width="80%" height="80%"></p>
<p align="center">工业化的经济压力将推动一次性软件的流行/泛滥。</p>

如果说智能手机的普及带来的无处不在的用户生成的<strong><mark>照片、视频和音频</mark></strong>（user generated contents），
那软件开始工业化生产之后，我们很可能在社交媒体上看到用户**<mark>海量地创建、共享和丢弃用户生成的软件</mark>**（user generated softwares）。

一但这个齿轮转动起来，
社交媒体和互联网的<strong><mark>新奇和奖励反馈循环</mark></strong>
将推动<strong><mark>用户生产软件的爆炸式增长</mark></strong>，使过去半个世纪的发展相形见绌。

# 4 传统软件未来还有生存空间吗？

## 4.1 再次参考食品、服装领域

垃圾食品当然不是市场上留下的唯一食品选择。仍然有很多人对健康、可持续的食品生产有持续不断的需求，这也主对工业化生产的一种回应。
像“有机食物”一样，软件是否也可能通过"有机软件"运动来抵抗机械化？

如果看看其他行业，我们会发现，<strong><mark>即使是工业化程度最高的行业，也仍然存在小规模、人类主导的生产</mark></strong>，
作为完整生产体系的一部分。

例如，在工业化之前，服装主要由专业匠人制作，通常通过行会和手协调，资源在当地收集，制作耐用织物的专业知识积累多年，并在家族中传承等等。
工业化完全改变了这一模式，原材料在洲际间运输，织物在工厂中大规模生产，衣服由机器组装，所有这些都导致了今天快速、一次性、剥削性的时尚世界。
然而，手工制作的服装仍然存在：从定制西装到针织围巾，小规模、慢生产的纺织品仍然有一席之地，
原因包括合身定制、彰显财富、耐用，以及享受手工艺产品等等。

## 4.2 创新：人类的自留地？

那么，人类编写的软件是否会和高级时装或自制针织品类似，成为一个区别与大众市场的精品市场？

<p align="center"><img src="/assets/img/rise-of-industrial-software/industry-4.svg" width="80%" height="80%"></p>
<p align="center">未来，人工编写的软件是否会变成精品店？</p>

### 无形产品：开放的方案空间

如果软件是有形的产品，情况可能就是类似的，工业化导致<strong><mark>可重用（物理）组件的大规模生产</mark></strong>。
但是，软件是**<mark>无形的商品</mark>**，与其他领域不同，它本身就有着**<mark>组件重用的悠久历史</mark>**，这是软件商品本身固有的属性。

创新不仅限于让现有的产品（例如服装）更好或更便宜，还包括<strong><mark>解决方案空间的扩大</mark></strong>，
例如，蒸汽机的出现使人类能够重用机器组件，造出了后来的<strong><mark>生产线、汽车</mark></strong>等。

### 创新：发现和解决新问题，获得更大价值的唯一路径

因此，软件开发的进步不仅仅是<strong><mark>工业化</mark></strong>，还包括**<mark>创新</mark>**。
<strong><mark>研发虽然昂贵</mark></strong>，但随着时间的推移提供了<strong><mark>获得更大价值的唯一路径</mark></strong>。

<p align="center"><img src="/assets/img/rise-of-industrial-software/industry-5.svg" width="100%" height="100%"></p>
<p align="center">创新是未来人工开发软件的价值增长点。</p>

创新从根本上不同于工业化，因为它不是专注于**<mark>更有效地复制今天已经存在的东西</mark>**。
而是在以前的基础上，它通过<strong><mark>发现和解决新问题</mark></strong>来提供以前没有的新能力。

# 5 创新+规模化/商品化：进步的无限循环

创新提供了以前没有的新能力之后，接下来就又轮到工业化入场了，它把这种新能力规模化和商品化，为下一轮创新建立基础。
这两种力量的相互作用就是我们所说的**<mark>进步</mark>**。

## 5.1 大模型是软件领域的蒸汽机，大量工作不再依赖人力劳动

[大语言模型](https://chrisloy.dev/post/2025/08/03/context-engineering)的出现是软件领域的**<mark>蒸汽机时刻</mark>**。
它们降低了<strong><mark>以前完全依赖稀缺的人类劳动</mark></strong>的那些工作的成本，从而<strong><mark>解锁了的非凡加速度</mark></strong>。

## 5.2 蒸汽机并不是凭空出现的，而是一个拐点，自动化、规模和资本在此对齐

但注意，蒸汽机并不是凭空出现的。

* 风车和水车在涡轮机之前几个世纪就出现了
* 机械化并不是从煤炭和钢铁开始的

蒸汽机只是刚好达到了一个拐点，<strong><mark>在这个拐点上，自动化、规模和资本对齐</mark></strong>，推动了经济转型。

## 5.3 软件领域的巨大加速时刻

同样，软件也已经工业化很长时间了：可重用组件（开源代码）、可移植性（容器化、云）、大众化（低代码/无代码工具）、互操作性（API 标准、包管理器）和许多其他方式。

因此，我们正在进入软件的工业革命，不是作为断裂的时刻，而是**<mark>巨大的加速时刻</mark>**。

* 工业化不会取代技术进步，但它将大大加速新思想的吸收和新能力的商品化。
* 反过来，能更快地解锁创新，因为在新技术基础上构建的成本下降得更快。

进步的循环继续，但在<strong><mark>大规模自动化时代，轮子比以往任何时候都转得更快</mark></strong>。

<p align="center"><img src="/assets/img/rise-of-industrial-software/industry-6.svg" width="100%" height="100%"></p>
<p align="center">进步的循环：创新+工业化同时驱动。</p>

## 5.4 工业化生产的软件占据主导地位之后，对周围生态系统的影响

至此，剩下的开放问题不是工业软件是否会占主导地位，而是<strong><mark>这种主导地位对周围生态系统将造成怎样的影响</mark></strong>。

* 以前的工业革命将其影响外化到看似无限的环境中，刚开始不会引人注目，但越到后面越明显；
* 软件生态系统也是类似的：依赖链、维护负担、安全等等问题，都会随着生产出的软件规模不断增加而越来越严重。

导致的技术债是**<mark>对数字世界的污染</mark>**，直到严重到足以扼杀依赖它的那些系统。

## 5.5 最难的不再是生产，而是管理

在大规模自动化时代，我们可能会发现最困难的问题不是生产，而是**<mark>管理</mark>**。
<strong><mark>谁来维护那些海量的没有 owner 的软件</mark></strong>？

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
