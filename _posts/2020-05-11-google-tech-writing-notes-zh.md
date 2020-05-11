---
layout    : post
title     : "[笔记] Google（英文）技术写作课（2020）"
date      : 2020-05-11
lastupdate: 2020-05-11
categories: writing
---

### 编者按

本文是阅读 Google [Technical Writing Courses](https://developers.google.com/tech-writing)
时所做的笔记。

* [课程一](https://developers.google.com/tech-writing/one)
* [课程二](https://developers.google.com/tech-writing/two)

该课程虽然是教授英文技术写作，但很多原则是通用的，与语言无关。

课程中有一些带答案的随堂练习非常不错，建议做一下。

本文内容仅供学习交流，如有侵权立即删除。

----

## 目录

### 课程 1

1. [Intro](#ch_1)
2. [语法](#ch_2)
3. [单词（Words）](#ch_3)
4. [主动时态（Active voice）](#ch_4)
5. [Clear sentences](#ch_5)
6. [Short sentences](#ch_6)
7. [Lists and tables](#ch_7)
8. [Paragraphs](#ch_8)
9. [Audience](#ch_9)
10. [Documents](#ch_10)
11. [标点（Punctuation）](#ch_11)
12. [Markdown](#ch_12)
13. [Summary](#ch_13)

### 课程 2

1. [Intro](#course2_ch_1)
2. [Self-editing](#course2_ch_2)
3. [Organizing large documents](#course2_ch_3)
4. [Illustrating](#course2_ch_4)
5. [Creating sample code](#course2_ch_5)
6. [Summary](#course2_ch_6)

----

# 课程 1

<a name="ch_1"></a>

# 1 Intro

目标：编写**最清晰**的技术文档（how to write clearer technical documentation）。

<a name="ch_2"></a>

# 2 语法

<a name="ch_3"></a>

# 3 单词（Words）


## 3.1 新术语（term）要给出最佳解释或定义

* 既有术语：给出最佳解释，例如 Wikipedia 词条链接
* 新术语：给出定义

## 3.2 术语的使用要前后一致

下面的例子中，一会用 `Protocol Buffers`，一会又用 `protobufs`（未定义过的名词）：

> Protocol Buffers provide their own definition language. Blah, blah, blah. And
> that's why protobufs have won so many county fairs.

解决方式：在定义这个术语的地方给出简写或缩写，后面就可以用缩写了：

> **Protocol Buffers** (or protobufs for short) provide their own definition
> language. Blah, blah, blah. And that's why **protobufs** have won so many
> county fairs.

## 3.3 适当使用缩写，使段落更简洁

> This document is for engineers who are new to the **Telekinetic Tactile Network**
> (**TTN**) or need to understand how to order TTN replacement parts through finger
> motions.

## 3.4 在容易引起歧义的地方，不要使用代词（it/they/this/that 等等）

引起歧义的例子：

下面的句子中，`It` 指 Python 还是 C++？

> Python is interpreted, while C++ is compiled. **It** has an almost cult-like
> following.

下面的句子中，`This` 指什么？

> You may use either Frambus or Foo to calculate derivatives. **This** is not
> optimal.

两种解决方式：

1. 直接使用相应的名词，不要用代词
2. 在代词后面加上相应的名词

> **Overlapping functionality** is not optimal.
>
> **This overlapping functionality** is not optimal.

<a name="ch_4"></a>

# 4 主动时态（Active voice）

## 4.1 Active voice vs. passive voice

* 主动时态：`The cat sat on the mat.`
* 被动时态：`The mat was sat on by the cat.`

大部分技术文档都应该使用**主动时态**。

## 4.2 更复杂的例子

下面的句子，三种不同时态：

1. 全被动
1. 部分被动
1. 全主动

<p align="center"><img src="/assets/img/google-tech-writing/passive-passive.svg" width="70%" height="70%"></p>
<p align="center"><img src="/assets/img/google-tech-writing/active-passive.svg" width="70%" height="70%"></p>
<p align="center"><img src="/assets/img/google-tech-writing/all-active.svg" width="70%" height="70%"></p>

## 4.3 优先使用主动时态

主动时态的好处：

1. 表达更直接，更易理解
1. 句子更简短

<a name="ch_5"></a>

# 5 Clear sentences

## 5.1 使用辨识度高的动词（strong verbs）

技术写作中，**动词（verb）是句子的最重要组成部分**。动词用的好，句子就无需额外解释。

但很多技术作者的动词词汇太贫乏，翻来覆去就那么几个最普通的、已经被用烂的动词。
花点时间来找几个准确且更有亮点的动词，效果会大不一样。

* choose precise, strong, specific verbs
* reduce imprecise, weak, or generic verbs

| Weak Verb | Strong Verb |
|:----------|:------------|
| The error `occurs` when clicking the Submit button. | Clicking the Submit button `triggers` the error.  |
| This error message `happens` when...                | The system `generates` this error message when... |
| We `are` very careful to ensure...                  | We carefully `ensure`...  |

## 5.2 减少使用 there is/are

例子：

> There is a variable called met_trick that stores the current accuracy.
>
> * 改进方式一：A variable named met_trick stores the current accuracy.
> * 改进方式二：The met_trick variable stores the current accuracy.

例子：

> There are two disturbing facts about Perl you should know.
>
> 改进：You should know two disturbing facts about Perl.

例子：

> There is no guarantee that the updates will be received in sequential order.
>
> 改进：Clients might not receive the updates in sequential order.

## 5.2 避免主观形容词或副词，尽量量化

> Setting this flag makes the application run screamingly fast.
>
> 改进：Setting this flag makes the application run 225-250% faster.

<a name="ch_6"></a>

# 6 Short sentences

## 6.1 每个句子只表达一个意思

原句子：

> The late 1950s was a key era for programming languages because IBM introduced
> Fortran in 1957 and John McCarthy introduced Lisp the following year, which
> gave programmers both an iterative way of solving problems and a recursive
> way.

改进：

> The late 1950s was a key era for programming languages. IBM introduced Fortran
> in 1957. John McCarthy invented Lisp the following year. Consequently, by the
> late 1950s, programmers could solve problems iteratively or recursively.

## 6.2 将长句子转换成列表（list）

> To alter the usual flow of a loop, you may use either a `break` statement (which
> hops you out of the current loop) or a `continue` statement (which skips past
> the remainder of the current iteration of the current loop).

改进：

> To alter the usual flow of a loop, call one of the following statements:
>
> * `break`, which hops you out of the current loop.
> * `continue`, which skips past the remainder of the current iteration of the current loop.

## 6.3 精简句子，避免重复性的单词

> An input value greater than 100 causes the triggering of logging.
>
> 改进：An input value greater than 100 triggers logging.

> This design document provides a detailed description of Project Frambus.
>
> 改进：This design document details Project Frambus.

## 6.4 精简从句

## 6.5 that 和 which 的使用区分

that 和 which 从句分别何时使用，有一个经验法则：

* which 从句去掉后不影响主句的意思
* that 去掉后主句的意思不完整

> Python is an interpreted language, **which** means the processor runs the program directly.
>
> FORTRAN is perfect for mathematical calculations **that** don't involve linear algebra.

或者：尝试读句子，如果发现**读到从句时需要停顿，那适合使用 which**；否则使用 that。

<a name="ch_7"></a>

# 7 Lists and tables

# 7.1 列表类型

* bulleted lists（圆点列表，无序）
* numbered lists（数字列表，有序）

选择标准：**调整列表各项的顺序，如果表达不受影响，适合无序列表；否则适合有序列表**。

例如，下面适合无序列表：

> Bash provides the following string manipulation mechanisms:
>
> * deleting a substring from the start of a string
> * reading an entire file into one string variable

下面适合有序列表：

> Take the following steps to reconfigure the server:
>
> 1. Stop the server.
> 2. Edit the configuration file.
> 3. Restart the server.

隐藏在长句中的列表要显式转换有序或无序列表：

> The llamacatcher API enables callers to create and query llamas, analyze
> alpacas, delete vicugnas, and track dromedaries.
>
> 改进：
>
> The llamacatcher API enables callers to do the following:
>
> * Create and query llamas.
> * Analyze alpacas.
> * Delete vicugnas.
> * Track dromedaries.

# 7.2 列表句子类型一致

所有列表项的语法、逻辑类型、大小写、标点等等，都要保持一致。

# 7.3 有序列表：使用 imperative（命令式）动词

举例：

> 1. Download the Frambus app from Google Play or iTunes.
> 2. Configure the Frambus app's settings.
> 3. Start the Frambus app.

# 7.4 适当的标点

* 如果列表项都是**完整的句子**：每个列表项的**首字母大写，句末加句号**。
* 否则，无需首字母大写，也不需要加句号。

# 7.5 表格

适当使用表格，使表达更清晰。

# 7.6 一两句话介绍将给出的列表或表格

> The following list identifies key performance parameters:

或

> Take the following steps to install the Frambus package:

<a name="ch_8"></a>

# 8 Paragraphs

## 8.1 Write a great opening sentence

每一段的第一句非常重要，应该揭示出本段的中心内容。很多人粗看文章的时候，只看每段
的第一句话。

下面的例子：

> A loop runs the same block of code multiple times. For example, suppose you
> wrote a block of code that detected whether an input line ended with a period.
> To evaluate a million input lines, create a loop that runs a million times.

反面教材：

> A block of code is any set of contiguous code within the same function. For
> example, suppose you wrote a block of code that detected whether an input line
> ended with a period. To evaluate a million input lines, create a loop that runs
> a million times.

## 8.2 每段只表达一个主题

练习：下面这段如何改进？

> The Pythagorean Theorem states that the sum of the squares of both legs of a
> right triangle is equal to the square of the hypotenuse. The k-means clustering
> algorithm relies on the Pythagorean Theorem to measure distances. By contrast,
> the k-median clustering algorithm relies on the Manhattan Distance.

参考答案：添加下面这句作为开头：

> Different clustering algorithms measure distances differently.

## 8.3 段落不要过长或过短

## 8.4 Answer what, why, and how

Good paragraphs answer the following three questions:

1. What are you trying to tell your reader?
1. Why is it important for the reader to know this?
1. How should the reader use this knowledge. Alternatively, how should the reader know your point to be true?

<a name="ch_9"></a>

# 9 Audience

好文档的定义：**好文档 = 读者完成某项任务所需的知识和技能 - 读者已具备的知识和技能**。

因此，需要做的是：

* 定义读者群
* 定义读者的学习目标
* 为读者编写恰当的文档

## 9.1 定义读者群

这会决定你用什么领域的描述语言，例如他们是否知道 `O(n)` 是什么意思。

## 9.2 定义读者的学习目标

## 9.3 为读者编写恰当的文档

* 词汇和概念：与读者认知匹配
* 知识诅咒
* 使用简单单词：不要使用古单词、小众单词、过于复杂的表达
* 文化中立和惯用语：不要假设读者对小众背景知识有了解（例如印度板球术语）

**The curse of knowledge（知识诅咒）**：

> Experts often suffer from the curse of knowledge, which means that their expert
> understanding of a topic ruins their explanations to newcomers.

**在专家的层面向普通开发者解释问题，结果非常失败。专家无意识地假设听众具备某些知
识，而事实上他们并不具备**。

<a name="ch_10"></a>

# 10 Documents

如何将众多段落组织成一份有着内在和谐的文档。

## 10.1 State your document's scope

开篇先明确描述本文档的内容，例如：

> This document describes the overall design of Project Frambus.

更好的文档会进一步说明本文档不包括哪些读者可能会寄希望的内容（non-scope），例如：

> This document does not describe the design for the related technology, Project Froobus.

## 10.2 State your audience

描述目标受众，期望的受众预备知识，以及需要读者先看完的文档（如果有）：

> I wrote this document for the test engineers supporting Project Frambus.

> This document assumes that you understand matrix multiplication and how to brew a really good cup of tea.

> You must read "Project Froobus: A New Hope" prior to reading this document.

## 10.3 Establish your key points up front

开篇一定要简洁地概括本文档的重要内容。

> Always write an executive summary (a TL;DR) for long engineering documents. 

**长文档一定要写一个执行摘要**（TL; DR）；**TLDR 虽短，但一定要花时间精细打
磨，否则还不如不写**。

## 10.4 目标读者与文档组织

例如：

> 1. Overview of the algorithm
>     1. Big O
>     1. Implementation in pseudocode
> 2. Sample implementation in C
>     1. Tips in implementing in other languages
>     1. Deeper analysis of algorithm
> 3. Optimal datasets
>     1. Edge case problems

## 10.5 Break your topic into sections

<a name="ch_11"></a>

# 11 标点（Punctuation）

<a name="ch_12"></a>

# 12 Markdown

<a name="ch_13"></a>

# 13. Summary


# 课程 2（Technical Writing Two）

<a name="course2_ch_1"></a>

# 1 Intro

<a name="course2_ch_2"></a>

# 2 Self-editing

* 文档都是一遍遍迭代出来的，从无到有的写出第一份草稿通常是最难的。
* 确保留出足够的时间对文档进行迭代。

## 2.1 Adopt a style guide

统一使用一种文档风格，推荐：[Google Developer Documentation Style
Guide](https://developers.google.com/style)。

## 2.2 Think like your audience

尝试从读者的角度去阅读你的草稿。在此之前，得先确定你的目标读者是哪类人。

## 2.3 Read it out loud

读出来，你可能会发现其中的一些问题：例如句子太长、用词太拗口等。

* getting started guide 强调快速上手，可以偏口语化；
* 开发者文档追求严谨，准确。

## 2.4 Come back to it later

写完初稿后休息一会再回来看，更能发现其中的问题。

## 2.5 Find a peer editor

<a name="course2_ch_3"></a>

# 3 Organizing large documents

## 3.1 When to write large documents

哪些适合长文，哪些适合短文：

* How-to guides, introductory overviews, and conceptual guides：适合短文
* In-depth tutorials, best practice guides, and command-line reference pages：适合长文
* 深入理解 xxx 系列：适合长文，但分解成系列短文可能更好
* 某些长文并不是要求读者一次读完。例如，API 文档只是在用到的适合去搜索相关内容。

## 3.2 Orginaze a document

### Outline a document

> Documents that alternate between conceptual information and practical steps
> can be a particularly engaging way to learn.

讲完理论紧接着来点实践的文档更容易让读者学到东西。

> Consider explaining a concept and then demonstrating how the reader can apply it
> in either a sample project or in their own work.

介绍完一个概念后，考虑如何向用户展示这个概念在实际中是如何应用的。

### Introduce a document

本文：

* 包括哪些内容
* 需要读者具备的背景知识
* 不包括哪些内容

范例：

> This document explains how to publish Markdown files using the Froobus system.
> Froobus is a publishing system that runs on a Linux server and converts
> Markdown files into HTML pages. This document is intended for people who are
> familiar with Markdown syntax. To learn about the syntax, see the Markdown
> reference. You also need to be comfortable running simple commands in a
> Linux terminal. This document doesn't include information about installing or
> configuring a Froobus publishing system. For information on installing Froobus,
> see Getting started.

## 3.3 Add navigation

添加导航栏。

## 3.4 Disclose information progressively

循序渐进地抖出内容。

<a name="course2_ch_4"></a>

# 4 Illustrating

**无图无真相。纯文字近乎自杀。**

## 4.1 Write the caption first（起个好标题）

**好标题的三个特点:**

* 简洁（brief）：通常只有几个单词。
* 描述核心要点（takeaway）：看过这张图后，读者应该记住什么？
* 能引起读者的注意力焦点（focus），尤其是细节太多的时候。

例子：下面是对单向链表的描述，

> A single-linked list holds content and a pointer to the next node.

如果这句话作为一张图的标题，那下面三个配图哪个最好？

<p align="center"><img src="/assets/img/google-tech-writing/linked-list.png" width="60%" height="60%"></p>

## 4.2 Constrain the amount of information in a single drawing

一张图内的信息量不宜过多。

方法：

* 拆分成子系统，以及相应的多张子系统图。
* 或者先来一张总览图，再分部分介绍子系统图。

## 4.3 Focus the reader's attention

callout（图标注）是一种吸引注意力的直接方式，简单有效。

<p align="center"><img src="/assets/img/google-tech-writing/moon_with_callout.png" width="35%" height="35%"></p>

## 4.4 Illustrating is re-illustrating

第一版的图或标题很难是最优的，因此需要反复迭代。

例子：伦敦地铁图的演变。去掉等比例和地理位置标记，专注地铁乘客的核心需求：从地点
A 到地点 B。

<p align="center"><img src="/assets/img/google-tech-writing/metro-1.png" width="50%" height="50%"></p>

<p align="center"><img src="/assets/img/google-tech-writing/metro-2.png" width="50%" height="50%"></p>

<a name="course2_ch_5"></a>

# 5 Creating sample code

Good samples are correct and concise code that your readers can quickly
understand and easily reuse with minimal side effects.

<a name="course2_ch_6"></a>

# 6 Summary

