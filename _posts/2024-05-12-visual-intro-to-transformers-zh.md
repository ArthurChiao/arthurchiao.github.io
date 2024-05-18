---
layout    : post
title     : "[译] 什么是 GPT？Transformer 工作原理的动画展示（2024）"
date      : 2024-05-12
lastupdate: 2024-05-12
categories: llm gpt transformer
---

### 译者序

本文翻译自 2024 年的一个视频（前半部分）：
[But what is a GPT? Visual intro to transformers](https://www.youtube.com/watch?v=wjZofJX0v4M)。
这是原作者 Deep Learning 系列的第 5 章，强烈推荐原视频。
上不了油管的，B 站也有不少搬运。B站的3Blue1Brown官方账号 [官方双语】GPT是什么？直观解释Transformer | 深度学习第5章](https://www.bilibili.com/video/BV13z421U7cs)。

<p align="center"><img src="/assets/img/visual-intro-to-transformers/transformer-modules.gif" width="100%" height="100%"></p>
<p align="center">Transformer 预测下一个单词四部曲。MLP 也称为 feed-forward。</p>

作者以深厚的技术积累，将一些复杂系统以可视化的方式讲给普通人，这种能力是极其难得的。
本译文希望通过“文字+动图”这种可视化又方便随时停下来思考的方式介绍 Transformer 的内部工作原理。
如果想进一步从技术和实现上了解 Transformer/GPT/LLM，可参考：

* [GPT 是如何工作的：200 行 Python 代码实现一个极简 GPT（2023）]({% link _posts/2023-05-21-gpt-as-a-finite-state-markov-chain-zh.md %})
* [Transformer 是如何工作的：600 行 Python 代码实现 self-attention 和两类 Transformer（2019）]({% link _posts/2023-06-06-transformers-from-scratch-zh.md %})
* [InstructGPT：基于人类反馈训练语言模型遵从指令的能力（OpenAI，2022）]({% link _posts/2024-03-24-instructgpt-paper-zh.md %}) 
* [大语言模型（LLM）综述与实用指南（Amazon，2023）]({% link _posts/2023-07-23-llm-practical-guide-zh.md %})
* [如何训练一个企业级 GPT 助手（OpenAI，2023）]({% link _posts/2023-09-01-how-to-train-a-gpt-assistant-zh.md %})

翻译不免存在遗漏或错误之处，如有疑问请查阅原视频。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

以下是译文。

----

* TOC
{:toc}

----

# 1 图解 “Generative Pre-trained Transformer”（GPT）

GPT 是 Generative Pre-trained Transformer 的缩写，直译为“生成式预训练 transformer”，
我们先从字面上解释一下它们分别是什么意思。

## 1.1 Generative：生成式

“Generative”（**<mark>生成式</mark>**）意思很直白，就是给定一段输入（例如，最常见的文本输入），
模型就能**<mark>续写</mark>**（“编”）下去。

### 1.1.1 可视化

下面是个例子，给定 <mark>“The most effective way to learn computer science is”</mark> 作为输入，
模型就开始续写后面的内容了。

<p align="center"><img src="/assets/img/visual-intro-to-transformers/generative-meaning.gif" width="90%" height="90%"></p>
<p align="center">“Generative”：生成（续写）文本的能力。</p>

### 1.1.2 生成式 vs. 判别式（译注）

文本续写这种生成式模型，区别于 BERT 那种**<mark>判别式</mark>**模型（用于分类、完形填空等等），

* [<mark>BERT：预训练深度双向 Transformers 做语言理解</mark>（Google，2019）]({% link _posts/2024-03-10-bert-paper-zh.md %})

## 1.2 Pre-trained：预训练

"Pre-trained"（预训练）指的是模型是**<mark>用大量数据训练出来的</mark>**。

### 1.2.1 可视化

<p align="center"><img src="/assets/img/visual-intro-to-transformers/pre-trained-meaning.gif" width="90%" height="90%"></p>
<p align="center">“Pre-trained”：用大量数据进行训练。<br/>
图中的大量旋钮/仪表盘就是所谓的<mark>“模型参数”</mark>，训练过程就是在不断优化这些参数，后面会详细介绍。</p>

### 1.2.2 预训练 vs. 增量训练（微调）

**<mark>“预”</mark>**这个字也暗示了模型还有在特定任务中**<mark>进一步训练</mark>**的可能 ——
也就是我们常说的**<mark>“微调”</mark>**（finetuning）。

> 如何对预训练模型进行微调：
> [InstructGPT：基于人类反馈训练语言模型遵从指令的能力（OpenAI，2022）]({% link _posts/2024-03-24-instructgpt-paper-zh.md %})。
> 译注。

## 1.3 Transformer：一类神经网络架构

“GPT” 三个词中最重要的其实是最后一个词 Transformer。
Transformer 是一类**<mark>神经网络</mark>**/机器学习模型，作为近期 AI 领域的核心创新，
推动着这个领域近几年的极速发展。

> Transformer 直译为**<mark>“变换器”</mark>**或“转换器”，通过数学运算不断对输入数据进行变换/转换。另外，变压器、变形金刚也是这个词。
> 译注。

<p align="center"><img src="/assets/img/visual-intro-to-transformers/transformer-detailed-1.gif" width="90%" height="90%"></p>
<p align="center">Transformer：一类神经网络架构的统称。</p>

<p align="center"><img src="/assets/img/visual-intro-to-transformers/transformer-detailed-2.gif" width="90%" height="90%"></p>
<p align="center">Transformer 最后的输出层。后面还会详细介绍</p>

## 1.4 小结

如今已经可以基于 Transformer 构建许多不同类型的模型，不限于文本，例如，

* 语音转文字
* 文字转语音
* 文生图（text-to-image）：DALL·E、MidJourney 等在 2022 年风靡全球的工具，都是基于 Transformer。

    [文生图（text-to-image）简史：扩散模型（diffusion models）的崛起与发展（2022）]({% link _posts/2024-01-21-rise-of-diffusion-based-models-zh.md %})

    <p align="center"><img src="/assets/img/visual-intro-to-transformers/dalle-pi.gif" width="85%" height="85%"></p>
    <p align="center">虽然无法让模型真正理解 "物种 π"是什么（本来就是瞎编的），但它竟然能生成出来，而且效果很惊艳。</p>

本文希望通过“文字+动图”这种可视化又方便随时停下来思考的方式，解释 Transformer 的内部工作原理。

# 2 Transformer 起源与应用

## 2.1 Attention Is All You Need, Google, 2017，机器翻译

Transformer 是 Google 2017 年在 [Attention Is All You Need](https://arxiv.org/abs/1706.03762) paper 中提出的，
当时主要用于**<mark>文本翻译</mark>**：

<p align="center"><img src="/assets/img/visual-intro-to-transformers/machine-translation.gif" width="85%" height="85%"></p>

## 2.2 **<mark><code>Generative</code></mark>** Transformer

之后，Transformer 的应用场景扩展到了多个领域，例如 ChatGPT 背后也是 Transformer，
这种 Transformer 接受一段文本（或图像/音频）作为输入，然后就能预测接下来的内容。
以预测下一个单词为例，如下图所示，下一个单词有多种可能，各自的概率也不一样：

<p align="center"><img src="/assets/img/visual-intro-to-transformers/generative-transformer.gif" width="85%" height="85%"></p>

但有了一个这样的**<mark>预测下一个单词</mark>**模型，就能通过如下步骤让它生成更长的文字，非常简单：

1. 将**<mark>初始文本</mark>**输入模型；
2. 模型**<mark>预测出下一个可能的单词列表及其概率</mark>**，然后通过某种算法（不一定挑概率最大的）
  从中选一个作为下一个单词，这个过程称为**<mark>采样</mark>**（sampling）；
3. **<mark>将新单词追加到文本结尾</mark>**，然后将整个文本**<mark>再次输入模型</mark>**；转 2；

以上 step 2 & 3 不断重复，得到的句子就越来越长。

## 2.3 **<mark><code>GPT-2/GPT-3</code></mark>** 生成效果（文本续写）预览

来看看生成的效果，这里拿 GPT-2 和 GPT-3 作为例子。

下面是在我的笔记本电脑上运行 **<mark><code>GPT-2</code></mark>**，不断预测与采样，逐渐补全为一个故事。
但**<mark>结果比较差</mark>**，生成的故事基本上没什么逻辑可言：

<p align="center"><img src="/assets/img/visual-intro-to-transformers/gpt2-output-1.gif" width="75%" height="75%"></p>

下面是换成 **<mark><code>GPT-3</code></mark>**（模型不再开源，所以是通过 API），
GPT-3 和 GPT-2 基本架构一样，只是规模更大，
但**<mark>效果突然变得非常好</mark>**，
生成的故事不仅合乎逻辑，甚至还暗示 “物种 π” 居住在一个数学和计算王国：

<p align="center"><img src="/assets/img/visual-intro-to-transformers/gpt3-output-1.gif" width="75%" height="75%"></p>

## 2.4 **<mark><code>ChatGPT</code></mark>** 等交互式大模型

以上这个不断重复“预测+选取”来生成文本的过程，就是 ChatGPT 或其他类似大语言模型（LLM）
的底层工作原理 —— **<mark>逐单词（token）生成文本</mark>**。

## 2.5 小结

以上是对 GPT 及其背后的 Transformer 的一个感性认识。接下来我们就深入到 Transformer 内部，
看看它是如何根据给定输入来预测（计算）出下一个单词的。

# 3 Transformer 数据处理四部曲

为理解 Transformer 的内部工作原理，本节从端到端（从最初的用户输入，到最终的模型输出）的角度看看数据是如何在 Transformer 中流动的。
从宏观来看，输入数据在 Transformer 中经历如下四个处理阶段：

<p align="center"><img src="/assets/img/visual-intro-to-transformers/transformer-modules.gif" width="100%" height="100%"></p>
<p align="center">Transformer 数据处理四部曲</p>

下面分别来看。

## 3.1 Embedding：分词与向量表示

首先，输入内容会被拆分成许多小片段（这个过程称为 tokenization），这些小片段称为 **<mark><code>token</code></mark>**，

* 对于文本：token 通常是单词、词根、标点符号，或者其他常见的字符组合；
* 对于图片：token 可能是一小块像素区域；
* 对于音频：token 可能是一小段声音。

然后，将每个 token 用一个**<mark>向量（一维数组）</mark>**来表示。

### 3.1.1 token 的向量表示

这实际上是以某种方式在**<mark>编码</mark>**该 token；

<p align="center"><img src="/assets/img/visual-intro-to-transformers/embedding-1.gif" width="80%" height="80%"></p>
<p align="center">Embedding：每个 token 对应一个 <code>N*1</code> 维度的数值格式表示的向量。</p>

### 3.1.2 向量表示的直观解释

如果把这些向量看作是在**<mark>高维空间中的坐标</mark>**， 那么含义相似的单词在这个高维空间中是**<mark>相邻的</mark>**。

<p align="center"><img src="/assets/img/visual-intro-to-transformers/embedding-2.gif" width="80%" height="80%"></p>
<p align="center">词义相近的四个单词 “leap/jump/skip/hop” 在向量空间中是相邻的</p>

将输入进行 tokenization 并转成向量表示之后，输入就从一个句子就变成了一个**<mark>向量序列</mark>**。
接下来，这个向量序列会进行一个称为 attention 的运算。

## 3.2 Attention：embedding 向量间的语义交流

### 3.2.1 语义交流

attention 使得**<mark>向量之间能够相互“交流”信息</mark>**。这个交流是双向的，在这个过程中，每个向量都会更新自身的值。

<p align="center"><img src="/assets/img/visual-intro-to-transformers/attention-1.gif" width="80%" height="80%"></p>

这种信息“交流”是有上下文和语义理解能力的。

### 3.2.2 例子："machine learning **<mark><code>model</code></mark>**" / "fashion **<mark><code>model</code></mark>**"

例如，“model” 这个词在 “machine learning model”（机器学习模型）和在 “fashion model”（时尚模特）中的意思就完全不一样，
因此**<mark>虽然是同一个单词（token），但对应的 embedding 向量是不同的</mark>**，

<p align="center"><img src="/assets/img/visual-intro-to-transformers/attention-2.gif" width="80%" height="80%"></p>

Attention 模块的作用就是确定上下文中哪些词之间有语义关系，以及如何准确地理解这些含义（更新相应的向量）。
这里说的“含义”（meaning），指的是编码在向量中的信息。

## 3.3 Feed-forward / MLP：向量之间无交流

Attention 模块让输入向量们彼此充分交换了信息（例如，单词 “model” 指的应该是“模特”还是“模型”），
然后，这些向量会进入第三个处理阶段：

<p align="center"><img src="/assets/img/visual-intro-to-transformers/mlp-1.gif" width="80%" height="80%"></p>
<p align="center">第三阶段：多层感知机（<mark><code>multi-layer perceptron</code></mark>），也称为前馈层（<mark><code>feed-forward layer</code></mark>）。</p>

### 3.3.1 针对所有向量做一次性变换

这个阶段，向量之间没有互相“交流”，而是并行地经历同一处理：

<p align="center"><img src="/assets/img/visual-intro-to-transformers/mlp-2.gif" width="80%" height="80%"></p>

### 3.3.2 直观解释

后面会看，从直观上来说，这个步骤有点像**<mark>对每个向量都提出一组同样的问题</mark>**，然后**<mark>根据得到的回答来更新对应的向量</mark>**：

<p align="center"><img src="/assets/img/visual-intro-to-transformers/mlp-3.gif" width="80%" height="80%"></p>

以上解释中省略了归一化等一些中间步骤，但已经可以看出：
attention 和 feed-forward 本质上都是**<mark>大量的矩阵乘法</mark>**，

<p align="center"><img src="/assets/img/visual-intro-to-transformers/matmul-1.gif" width="80%" height="80%"></p>

本文的一个目的就是让读者理解这些矩阵乘法的直观意义。

### 3.3.3 重复 Attention + Feed-forward 模块，组成多层网络

Transformer 基本上是不断复制 Attention 和 Feed-forward 这两个基本结构，
这两个模块的组合成为神经网络的一层。在每一层，

<p align="center"><img src="/assets/img/visual-intro-to-transformers/repeat-blocks.gif" width="100%" height="100%"></p>

* 输入向量通过 attention 更新彼此；
* feed-forward 模块将这些更新之后的向量做统一变换，得到这一层的输出向量；

## 3.4 Unembedding：概率

### 3.4.1 最后一层 feed-forward 输出中的最后一个向量

如果一切顺利，最后一层 feed-forward 输出中的最后一个向量（the very last vector in the sequence），
就已经包含了句子的核心意义（essential meaning of the passage）。对这个向量进行 unembedding 操作（也是一次性矩阵运算），
得到的就是下一个单词的备选列表及其概率：

<p align="center"><img src="/assets/img/visual-intro-to-transformers/last-vector.gif" width="80%" height="80%"></p>
<p align="center">图：原始输入为 "To date, the cleverest thinker of all time was"，让模型预测下一个 token。经过多层 attention + feed-forward 之后，
最后一层输出的<mark>最后一个向量已经学习到了输入句子表达的意思</mark>，（经过简单转换之后）就能作为<mark>下一个单词的概率</mark>。</p>

### 3.4.2 下一个单词的选择

根据一定的规则选择一个 token，

* 注意这里不一定选概率最大的，根据工程经验，一直选概率最大的，生成的文本会比较呆板；
* 实际上由一个称为 `temperature` 的参数控制；

## 3.5 小结

以上就是 Transformer 内部的工作原理。

前面已经提到，有了一个这样的**<mark>预测下一个单词</mark>**模型，就能通过如下步骤让它生成更长的文字，非常简单：

1. 将**<mark>初始文本</mark>**输入模型；
2. 模型**<mark>预测出下一个可能的单词列表及其概率</mark>**，然后通过某种算法（不一定挑概率最大的）
  从中选一个作为下一个单词，这个过程称为**<mark>采样</mark>**（sampling）；
3. **<mark>将新单词追加到文本结尾</mark>**，然后将整个文本**<mark>再次输入模型</mark>**；转 2；

# 4 GPT -> ChatGPT：从文本补全到交互式聊天助手

GPT-3 的早期演示就是这样的：给 GPT-3 一段起始文本，它就自动补全（续写）故事和文章。
这正式以上介绍的 Transformer 的基本也是核心功能。

ChatGPT 的核心是 GPT 系列（GPT 3/3.5/4），但它怎么实现聊天这种工作方式的呢？

## 4.1 系统提示词，伪装成聊天

其实很简单，将输入文本稍作整理，弄成聊天内容，然后把这样的文本再送到 GPT/Transformer，
它就会把这个当前是聊天内容，续写下去。最后只需要把它续写的内容再抽出来返回给用户，
对用户来说，就是在聊天。

<p align="center"><img src="/assets/img/visual-intro-to-transformers/system-prompt.gif" width="70%" height="70%"></p>

这段文本设定用户是在与一个 AI 助手交互的场景，这就是所谓的系统提示词（**<mark><code>system prompt</code></mark>**）。

## 4.2 如何训练一个企业级 GPT 助手（译注）

OpenAI 官方对 GPT->ChatGPT 有过专门分享：[如何训练一个企业级 GPT 助手（OpenAI，2023）]({% link _posts/2023-09-01-how-to-train-a-gpt-assistant-zh.md %})

> **<mark>基础模型不是助手</mark>**，它们**<mark>不想回答问题，只想补全文档</mark>**。
> 因此，如果让它们“写一首关于面包和奶酪的诗”，它们不仅不“听话”，反而会有样学样，列更多的任务出来，像下面左图这样，
>
> <p align="center"><img src="/assets/img/how-to-train-a-gpt-assistant/base-model-not-assistant.png" width="80%" height="80%"></p>
>
> 这是因为它只是在**<mark>忠实地补全文档</mark>**。
> 但如果你能**<mark>成功地提示它</mark>**，例如，**<mark>开头就说“这是一首关于面包和奶酪的诗”</mark>**，
> 那它接下来就会真的补全一首这样的诗出来，如右图。
>
> 我们还可以通过 few-shot 来**<mark>进一步“欺骗”它</mark>**。把你想问的问题整理成一个**<mark>“提问+回答”的文档格式</mark>**，
> 前面给一点正常的论述，然后突然来个问题，它以为自己还是在补全文档，其实已经把问题回答了：
>
> <p align="center"><img src="/assets/img/how-to-train-a-gpt-assistant/base-model-not-assistant-2.png" width="80%" height="80%"></p>
>
> 这就是把基础模型**<mark>调教成一个 AI 助手</mark>**的过程。

# 5 总结

本文整理翻译了原视频的前半部分，通过可视化方式解释 GPT/Transformer 的内部工作原理。
原视频后面的部分是关于 general deep learning, machine learning 等等的基础，想继续学习的，强烈推荐。

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
