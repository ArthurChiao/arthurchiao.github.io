---
layout    : post
title     : "[译][论文] Transformer paper | Attention Is All You Need（Google，2017）"
date      : 2025-02-23
lastupdate: 2025-02-23
categories: gpt ai llm transformer
---

### 译者序

本文翻译自 2017 年 Google 提出 Transformer 的论文：
[Attention Is All You Need](https://arxiv.org/abs/1706.03762)。

<p align="center"><img src="/assets/img/attention-is-all-you-need/transformer-arch.png" width="60%" height="60%"></p>
<p align="center">Figure 1: Transformer 架构：encoder/decoder 内部细节。</p>

摘录一段来自 [Transformer 是如何工作的：600 行 Python 代码实现两个（文本分类+文本生成）Transformer（2019）]({% link _posts/2023-06-06-transformers-from-scratch-zh.md %})
的介绍，说明 **<mark>Transformer 架构相比当时主流的 RNN/CNN 架构的创新之处</mark>**：

> 在 **<mark>transformer 之前，最先进的架构是 RNN</mark>**（通常是 LSTM 或 GRU），但它们存在一些问题。
>
> RNN <a href="https://colah.github.io/posts/2015-08-Understanding-LSTMs/">展开（unrolled）</a>后长这样：
>
> <p align="center"><img src="/assets/img/transformers-from-scratch/recurrent-connection.png" width="65%" height="65%"></p>
> <p align="center">
> </p>
>
> RNN 最大的问题是**<mark><code>级联</code></mark>**（recurrent connection）：
> 虽然它使得信息能沿着 input sequence 一路传导，
> 但也意味着在计算出 $$i-1$$ 单元之前，无法计算出 $$i$$ 单元的输出。
>
> 与 RNN 此对比，**<mark>一维卷积</mark>**（1D convolution）如下：
>
> <p align="center"><img src="/assets/img/transformers-from-scratch/convolutional-connection.png" width="65%" height="65%"></p>
> <p align="center">
> </p>
>
> 在这个模型中，所有输出向量都可以并行计算，因此速度非常快。但缺点是它们
> 在 long range dependencies 建模方面非常弱。在一个卷积层中，只有距离比 kernel size
> 小的单词之间才能彼此交互。对于更长的依赖，就需要堆叠许多卷积。
>
> **<mark>Transformer 试图兼顾二者的优点</mark>**：
>
> * 可以像对彼此相邻的单词一样，轻松地对输入序列的整个范围内的依赖关系进行建模（事实上，如果没有位置向量，二者就没有区别）；
> * 同时，避免 recurrent connections，因此整个模型可以用非常高效的 feed forward 方式计算。
>
> Transformer 的其余设计主要基于一个考虑因素 —— **<mark>深度</mark>** ——
> 大多数选择都是训练大量 transformer block 层，例如，transformer 中**<mark>只有两个非线性的地方</mark>**：
>
> 1. self-attention 中的 softmax；
> 2. 前馈层中的 ReLU。
>
> 模型的其余部分完全由线性变换组成，**<mark>完美地保留了梯度</mark>**。

**<mark>提出 attention 机制</mark>**的 paper：
[神经机器翻译：联合学习对齐和翻译（Align & Translate）（2014）]({% link _posts/2025-03-01-attention-paper-zh.md %})。

**<mark>相关阅读</mark>**：

{% for category in site.categories %}
  <div class="archive-group">
    {% capture category_name %}{{ category | first }}{% endcapture %}
    {% if category_name == "transformer" %}
        {% assign posts = site.categories[category_name] | sort: 'date' | sort: 'url' %}
        {% for post in posts %}
            <article class="archive-item">
              <li><a style="text-decoration:none" href="{{ post.url }}">{{post.title}}</a></li>
            </article>
        {% endfor %}
    {% endif %}
  </div>
{% endfor %}

水平及维护精力所限，译文不免存在错误或过时之处，如有疑问，请查阅原文。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

以下是译文。

----

* TOC
{:toc}

----

<script type="text/x-mathjax-config">
    MathJax.Hub.Config({
      extensions: ["tex2jax.js"],
      jax: ["input/TeX", "output/HTML-CSS"],
      tex2jax: {
          inlineMath: [ ['$','$'], ["\\(","\\)"] ],
          displayMath: [ ['$$','$$'], ["\\[","\\]"] ],
        processEscapes: true
      },
    "HTML-CSS": {
      availableFonts: [], preferredFont: null,
      webFont: "Neo-Euler",
      mtextFontInherit: true
    },
    TeX: {
      extensions: ["color.js"],
      Macros: {
        lgc: ["{\\color{my-light-green} #1}", 1],
        gc: ["{\\color{my-green} #1}", 1],
        lrc: ["{\\color{my-light-red} #1}", 1],
        rc: ["{\\color{my-red} #1}", 1],
        lbc: ["{\\color{my-light-blue} #1}", 1],
        bc: ["{\\color{my-blue} #1}", 1],
        kc: ["{\\color{my-gray} #1}", 1],
        loc: ["{\\color{my-light-orange} #1}", 1],
        oc: ["{\\color{my-orange} #1}", 1],

        a: ["\\mathbf a"],
        A: ["\\mathbf A"],
        b: ["\\mathbf b"],
        B: ["\\mathbf B"],
        c: ["\\mathbf c"],
        C: ["\\mathbf C"],
        d: ["\\mathbf d"],
        D: ["\\mathbf D"],
        E: ["\\mathbf E"],
        I: ["\\mathbf I"],
        L: ["\\mathbf L"],
        m: ["\\mathbf m"],
        M: ["\\mathbf M"],
        r: ["\\mathbf r"],
        s: ["\\mathbf s"],
        t: ["\\mathbf t"],
        S: ["\\mathbf S"],
        x: ["\\mathbf x"],
        z: ["\\mathbf z"],
        v: ["\\mathbf v"],
        y: ["\\mathbf y"],
        k: ["\\mathbf k"],
        bp: ["\\mathbf p"],
        P: ["\\mathbf P"],
        q: ["\\mathbf q"],
        Q: ["\\mathbf Q"],
        r: ["\\mathbf r"],
        R: ["\\mathbf R"],
        Sig: ["\\mathbf \\Sigma"],
        t: ["\\mathbf t"],
        T: ["\\mathbf T"],
        e: ["\\mathbf e"],
        X: ["\\mathbf X"],
        u: ["\\mathbf u"],
        U: ["\\mathbf U"],
        v: ["\\mathbf v"],
        V: ["\\mathbf V"],
        w: ["\\mathbf w"],
        W: ["\\mathbf W"],
        Y: ["\\mathbf Y"],
        z: ["\\mathbf z"],
        Z: ["\\mathbf Z"],
        p: ["\\,\\text{.}"],
        tab: ["\\hspace{0.7cm}"],

        sp: ["^{\\small\\prime}"],


        mR: ["{\\mathbb R}"],
        mC: ["{\\mathbb C}"],
        mN: ["{\\mathbb N}"],
        mZ: ["{\\mathbb Z}"],

        deg: ["{^\\circ}"],


        argmin: ["\\underset{#1}{\\text{argmin}}", 1],
        argmax: ["\\underset{#1}{\\text{argmax}}", 1],

        co: ["\\;\\text{cos}"],
        si: ["\\;\\text{sin}"]
      }
    }
    });

    MathJax.Hub.Register.StartupHook("TeX color Ready", function() {
       MathJax.Extension["TeX/color"].colors["my-green"] = '#677d00';
       MathJax.Extension["TeX/color"].colors["my-light-green"] = '#acd373';
       MathJax.Extension["TeX/color"].colors["my-red"] = '#b13e26';
       MathJax.Extension["TeX/color"].colors["my-light-red"] = '#d38473';
       MathJax.Extension["TeX/color"].colors["my-blue"] = '#306693';
         MathJax.Extension["TeX/color"].colors["my-light-blue"] = '#73a7d3';
         MathJax.Extension["TeX/color"].colors["my-gray"] = '#999';
         MathJax.Extension["TeX/color"].colors["my-orange"] = '#E69500';
         MathJax.Extension["TeX/color"].colors["my-light-orange"] = '#FFC353';


  });
</script>

<script type="text/javascript"
  src="https://cdnjs.cloudflare.com/ajax/libs/mathjax/2.7.5/MathJax.js">
</script>

# 摘要

主流的 sequence transduction model 都是基于复杂的**<mark>循环或卷积神经网络</mark>**，
其中包括一个 encoder 和一个 decoder。效果最好的模型还会通过 attention 机制将 encoder 和 decoder 连起来。

我们提出一种新的**<mark>简单网络架构</mark>** Transformer，它**<mark>弃用了循环和卷积，完全基于 attention 机制</mark>**。

在两个**<mark>机器翻译</mark>**任务上的实验表明，Transformer 模型的效果好于其他模型，并且更容易并行化，训练时间显著减少。

* Tranformer 在 WMT 2014 英德翻译任务上达到了 28.4 BLEU，比现有最佳结果提高了 2 BLEU 以上。
* 在 WMT 2014 英法翻译任务上，Tranformer 在 8 个 P100 GPU 上训练 3.5 天后，创造了新的单模型最佳性能，
  这个**<mark>训练成本</mark>**也远小于本文引用的性能类似的其他模型。

我们还成功将 Transformer 应用于英语句法分析，展示了 Transformer 在其他任务上的**<mark>泛化能力</mark>**。

# 1 引言

当前，**<mark><code>RNN</code></mark>**（Recurrent Neural Networks，循环神经网络）——
尤其是 **<mark><code>LSTM RNN</code></mark>**（long short-term memory）和 **<mark><code>gated RNN</code></mark>** ——
已经是**<mark>序列建模和 transduction 问题</mark>**（例如语言建模和机器翻译）的最好方式，
现在也仍然有大量的工作在继续扩大 recurrent 类语言模型和 **<mark><code>encoder-decoder</code></mark>** 架构的能力边界。

## 1.1 RNN 架构的内在顺序计算限制（来自 RNN 其中的 `R`）

Recurrent models 通常沿输入和输出序列的符号位置进行因子计算。

* 对于位置 $t$，根据前一个隐藏状态 $h_{t-1}$ 和位置 $t$ 处的 input 生成新的隐藏状态 $h_t$。
* 这种**<mark>内在的顺序性限制了训练数据之间的并行化</mark>**，序列较长时这一点尤为重要。

近期的工作通过分解技巧（factorization tricks）和条件计算（conditional computation）显著提高了计算效率，
此外，后者还提高了模型性能。然而，**<mark>顺序计算（sequential computation）这一根本约束仍然存在</mark>**。

## 1.2 RNN+Attention 架构：更好的模型效果

Attention 机制已经成为很多任务中序列建模和 transduction 模型的一个重要组成部分，
它允许**<mark>直接对依赖进行建模</mark>**（modeling of dependencies），
而不用考虑这些依赖在输入或输出序列中的距离。

但是，绝大部分大部分情况，人们仍然是将 attention 机制与 RNN 一起使用，因而仍然受到顺序计算的约束。

## 1.3 Transformer：避免 `R`，一种完全基于 attention 机制的新架构

本文提出 Transformer —— 一种**<mark>避免循环机制</mark>**、**<mark>完全基于 attention 机制</mark>**
而**<mark>在输入和输出之间建立全局依赖关系的模型架构</mark>**。

相比 RNN，Transformer 的并行能力显著提升，在 8 个 **<mark><code>P100</code></mark>** GPU 上训练 12 小时就能创造新的最高翻译水平。

# 2 背景

## 2.1 CNN：减少顺序计算，但对远距离依赖关系的学习成本很高

Extended Neural GPU、ByteNet 和 ConvS2S 也是想**<mark>减少顺序计算</mark>**，
它们都使用 **<mark><code>CNN</code></mark>**（convolutional neural networks，卷积神经网络）作为基本构建块，
为所有输入和输出位置**<mark>并行计算隐藏表示</mark>**。

但是，在这些模型中，从两个任意输入或输出位置（input or output positions）做信号关联，所需的操作数量随着位置之间的距离增加而增加，

* ConvS2S 线性增长
* ByteNet 对数增长。

这使得学习远距离位置之间的依赖关系变得困难。而在 Transformer 中，

* 所需的操作减少到一个**<mark>常量</mark>**，不过这里的代价是有效分辨率降低，这是 averaging attention-weighted positions 导致的；
* 但是，可以通过 Multi-Head Attention 来缓解。

## 2.2 Self-attention (intra-attention) 机制

Self-attention，有时称为 intra-attention，

* 是一种注意力机制（2014 paper），
* 目的是**<mark>计算序列的一种表示</mark>**（a representation of the sequence）
* 方式是**<mark>对一个输入序列的不同位置做各种关联</mark>**（relating different positions of a single sequence）。

Self-attention 已经成功地应用于各种任务 [4, 27, 28, 22]，包括

* **<mark>阅读理解</mark>**（reading comprehension）
* **<mark>总结抽象</mark>**（abstractive summarization）
* textual entailment
* 学习**<mark>任务无关的句子表示</mark>**（task-independent sentence representations）

## 2.3 Tranformer：避免 RNN 和 CNN

端到端的记忆网络（end-to-end memory networks）是基于一种 recurrent attention
而非 sequence-aligned recurrence 的机制，在 simple-language question answering 和语言建模任务中表现良好。

但据我们所知，Transformer 是**<mark>第一个完全依赖 self-attention</mark>** —— 而不使用 sequence-aligned RNNs 或 CNNs —— 来计算输入和输出表示的 transduction 模型。

# 3 Transformer 模型架构

## 3.0 Encoder-decoder：sequence transduction 模型的基本结构

大部分性能较好的 neural sequence transduction 模型**<mark>都会包含一个 encoder-decoder 结构</mark>**：

* encoder 将一个输入序列 $(x_1, ..., x_n)$ 映射到另一个序列表示 $\mathbf{z} = (z_1, ..., z_n)$。
* 给定 $\mathbf{z}$，decoder 生成一个输出序列 $(y_1,...,y_m)$ —— **<mark>每次生成一个元素</mark>**：
    * 生成下一个元素时，会将 input 连同上一步生成的元素一起，作为新的 input 输入 decoder；
    * 这种机制叫 **<mark><code>auto-regressive</code></mark>**（自回归）。

## 3.1 Encoder/decoder 内部结构

如下图所示，Transformer 沿用了 encoder-decoder 架构，

<p align="center"><img src="/assets/img/attention-is-all-you-need/ModalNet-21.png" width="40%" height="40%"></p>
<p align="center">Figure 1: Transformer 架构，<mark>沿用了业界的 encoder-decoder 架构</mark>。</p>

### 3.1.1 Encoder：`6 * {multi-head-attention + feed-forward}`

<p align="center"><img src="/assets/img/attention-is-all-you-need/transformer-arch.png" width="60%" height="60%"></p>
<p align="center">Figure 1: Transformer 架构：encoder/decoder 内部细节。</p>

Transformer 的 encoder 由 **<mark><code>N=6</code></mark>** 个相同的层组成，每层又分为两个子层（图 1 左边）：

* multi-head self-attention 层；
* 简单的 feed-forward 全连接层。

两个子层后面都会使用 residual connection，然后是 layer normalization。
也就是说，每个子层的输出是 **<mark><code>LayerNorm(x+Sublayer(x))</code></mark>**，
其中 `Sublayer(x)` 是子层本身实现的函数。

为了促进这些残差连接，模型中的所有子层以及 embedding 层，
都产生 **<mark><code>d<sub>model</sub>=512</code></mark>** 维的输出。

### 3.1.2 Decoder：`6 * {masked-multi-head-attention + multi-head-attention + feed-forward}`

Transformer 的 decoder 也由 **<mark><code>N=6</code></mark>** 个相同的层组成，

<p align="center"><img src="/assets/img/attention-is-all-you-need/transformer-arch.png" width="60%" height="60%"></p>
<p align="center">Figure 1: Transformer 架构：encoder/decoder 内部细节。</p>

但与 encoder 不同，decoder 的每层还插入了**<mark>第三个子层</mark>**（图 1 右边），

* 它**<mark>对 encoder 的输出执行 multi-head attention</mark>**。
  具体来说，decoder 的输入是 encoder 的输出往右偏移一个位置（the output embeddings are offset by one position），再加上 position embeddings；
* 这一子层的 self-attention 比较特殊，**<mark>加了个掩码（masking）</mark>**，这是为了**<mark>避免它使用当前位置后面的信息</mark>**（attending to subsequent positions）。
  换句话说，这确保了位置 $i$ 处的预测只能依赖 $i$ 位置前面的已知输出。

其他都与 encoder 类似，decoder 的每个子层后面都使用了残差连接，然后是层归一化。

## 3.2 Attention 内部结构

一个 attention 函数可以描述为**<mark>将一个查询（query）和一组 key-value pairs 映射到一个 output</mark>**，其中：

* 查询、键、值和输出都是向量；
* output 是 values 向量的加权和，其中每个 value 的权重是由 query 与相应 key 的一个 compatibility function 计算得到的。

### 3.2.1 Scaled Dot-Product Attention

如图 2 **<mark>左侧</mark>**所示，我们的 attention 称为 "Scaled Dot-Product Attention"。

<p align="center"><img src="/assets/img/attention-is-all-you-need/2.png" width="90%" height="90%"></p>
<p align="center">Figure 2:(left) Scaled Dot-Product Attention. (right) Multi-Head Attention consists of several attention layers running in parallel.</p>

#### **<mark>输入</mark>**

* queries 和 keys：都是 $d_k$ 维的向量；
* values：$d_v$ 的向量。

#### 计算过程

分为两步：

1. query 与所有 keys 的点积，将每个点积除以 $\sqrt{d_k}$，然后应用 softmax，得到的是 values 的权重；
2. 将这些权重与 values 相乘。

如图右侧，实际中，

* 同时计算一组 queries，将它们打包成一个矩阵 $Q$。
* keys 和 values 也被打包成矩阵 $K$ 和 $V$。

计算输出矩阵为：

\begin{equation}
   \mathrm{Attention}(Q, K, V) = \mathrm{softmax}(\frac{QK^T}{\sqrt{d_k}})V
\end{equation}
<p align="right">(1) </p>

两个最常用的 attention 函数是 additive attention [2] 和 dot-product（multiplicative）attention。

* Dot-product attention 除了缩放因子 $\frac{1}{\sqrt{d_k}}$ 与我们的算法不同，其他都是一样的；
* Additive attention 使用有单个隐藏层的 feed-forward network 来计算 compatibility function。

尽管二者的理论复杂度上类似，但实际上 **<mark><code>dot-product attention</code></mark>**
更快，更节省空间，因为它可以使用**<mark>高度优化的矩阵乘法</mark>**实现。

虽然对于小的 $d_k$ 值，这两种机制的性能相似，但对于较大的 $d_k$ 值，additive attention 优于不缩放的 dot-product attention。
我们猜测是对于较大的 $d_k$ 值，点积变得很大，将 softmax 函数推到到了梯度极小的区域。
为了避免这个问题，我们通过 $\frac{1}{\sqrt{d_k}}$ 缩放点积。

### 3.2.2 Multi-Head Attention 的计算

<p align="center"><img src="/assets/img/attention-is-all-you-need/2.png" width="90%" height="90%"></p>
<p align="center">Figure 2:(left) Scaled Dot-Product Attention. (right) Multi-Head Attention consists of several attention layers running in parallel.</p>

#### 线性变换 query/key，并行 attention 计算，最后再拼接 value

相比于对 $d_{model}$ 维的 keys、values 和 queries 执行单个 attention 函数，
我们发现可以并行计算：

1. **<mark>将 queries、keys 和 values 进行 <code>h</code> 次线性变换（投影）</mark>** —— 每次使用不同的、学习到的变换矩阵 —— 将三者分别变换到 $d_k$、$d_k$ 和 $d_v$ 维度。
1. 对变换之后的 queries、keys 和 values **<mark>并行执行 attention 函数</mark>**，就得到 $d_v$ 维的输出 values。
1. 将这些 **<mark>values 拼接到一起再进行一次线性变换</mark>**，就得到了最终的 values。

#### 公式和参数矩阵

Multi-head attention 允许模型同时 attend（关注）不同位置的不同表示子空间（representation subspaces）的信息。
如果只有一个 attention head，它的平均（averaging）会削弱这种效果。

\begin{align}
    \mathrm{MultiHead}(Q, K, V) &= \mathrm{Concat}(\mathrm{head_1}, ..., \mathrm{head_h})W^O\\
\end{align}

其中，

\begin{align}
    \mathrm{head_i} &= \mathrm{Attention}(QW^Q_i, KW^K_i, VW^V_i)\\
\end{align}

其中，**<mark>线性变换</mark>**（投影）就是下面几个**<mark>参数矩阵</mark>**：

* $W^Q_i \in \mathbb{R}^{d_{model} \times d_k}$
* $W^K_i \in \mathbb{R}^{d_{model} \times d_k}$
* $W^V_i \in \mathbb{R}^{d_{model} \times d_v}$
* $W^O   \in \mathbb{R}^{hd_v \times d_{model}}$

本文中我们使用

* **<mark><code>h=8</code></mark>**，也就是 8 个并行的 attention layers/heads。
* <mark>$d_k=d_v=d_{model}/h=64$</mark>，也就是将 query/key/value 向量都**<mark>分段投影到 64 维向量</mark>**。

由于每个 head 的维度降低，**<mark>总计算成本与完整维度的 single head attention 相似</mark>**。

### 3.2.3 Attention 在模型中的应用

Transformer 以三种不同的方式使用 multi-head attention：

#### "encoder-decoder attention" layers

这一步的用法就是
sequence-to-sequence 模型中 [38, 2, 9] 的典型 encoder-decoder attention 机制。

**<mark>输入</mark>**：

* queries 来自前一个 decoder 层
* memory keys 和 values 来自 encoder 的输出。

这使得 **<mark>decoder 中的每个位置都可以关注输入序列中的所有位置</mark>**。

#### encoder layers

encoder 层包含了 self-attention layers。

**<mark>输入</mark>**：keys、values 和 queries 都来自 encoder 中前一层的输出。

encoder 中的每个位置**<mark>都可以关注 encoder 前一层的所有位置</mark>**。

#### docoder layers

与 encoder 中类似，decoder 中的 self-attention 层允许 decoder 中的每个位置关注 decoder 中到该位置为止的所有位置。

* 为了**<mark>保证自回归特性</mark>**（auto-regressive），需要防止 decoder 中的左向信息流。
* 我们通过屏蔽与非法连接对应的 softmax 输入中的所有值（**<mark>设置为负无穷大</mark>** $-\infty$）来实现这一点。

## 3.3 Position-wise Feed-Forward Networks

除了 attention 子层，encoder 和 decoder 中的每个层都包含一个全连接的 feed-forward 网络，
包括两个线性变换和一个 ReLU 激活。

<p align="center"><img src="/assets/img/attention-is-all-you-need/feed-forward.png" width="20%" height="20%"></p>
<p align="center">Figure 1: Feed-Forward Network (FFN) 内部结构。</p>

对应的数学公式：

\begin{equation}
   \mathrm{FFN}(x)=\max(0, xW_1 + b_1) W_2 + b_2
\end{equation}
<p align="right">(2) </p>

线性变换在不同位置上是功能是相同的，但在不同的层使用的参数不同。
也可以将它们描述为：两个 kernel size 为 1 的卷积。
输入和输出的维度是 $d_{model}=512$，内层的维度是 $d_{ff}=2048$。

## 3.4 Embeddings and Softmax

与其他 sequence transduction models 类似，我们使用 learned embeddings 将输入 tokens 和输出 tokens 转换为维度为 <mark>$d_{model}=512$</mark> 的向量。
我们还使用常见的基于学习的线性变换和 softmax 函数**<mark>将 decoder 输出转换为下一个 token 的预测概率分布</mark>**（predicted next-token probabilities）。
我们的模型中，在两个 embedding 层和 pre-softmax 线性变换之间共享相同的权重矩阵，类似于 [30]。
在 embedding 层中，我们将这些权重乘以 $\sqrt{d_{model}}$。

## 3.5 Positional Encoding（位置编码）

### 3.5.1 目的：向 token 注入位置信息

因为我们的模型不包含循环和卷积，**<mark>为了使模型能够利用到序列的顺序</mark>**，
必须**<mark>向 token 注入一些关于相对或绝对位置的信息</mark>**。

### 3.5.2 编码算法：正弦函数

如下图所示，为了注入位置信息，

<p align="center"><img src="/assets/img/attention-is-all-you-need/transformer-arch.png" width="60%" height="60%"></p>
<p align="center">Figure 1: Transformer 架构，<mark>沿用了业界的 encoder-decoder 架构</mark>。</p>

我们在 encoder/decoder 的入口都添加了 "positional encodings"，它与 input embeddings 相加之后才开始后面的 attention 计算。。
位置编码与 input embedding 具有相同的维度 $d_{model}=512$，因此可以相加。

位置编码有许多选择，有基于学习的，也有固定的。
本文中，我们使用不同频率的正弦和余弦函数：

\begin{align}
    PE_{(pos,2i)} = sin(pos / 10000^{2i/d_{model}}) \\
\end{align}

\begin{align}
    PE_{(pos,2i+1)} = cos(pos / 10000^{2i/d_{model}})
\end{align}

其中，$pos$ 是位置，$i$ 是维度。也就是说，位置编码的每个维度对应于一个正弦波。
波长从 $2\pi$ 到 $10000 \cdot 2\pi$ 形成一个几何级数。

* 选择这个函数是因为我们猜测它可以让模型很容易地学习**<mark>通过相对位置进行 attention</mark>**，
  因为对于任何固定的偏移 $k$，$PE_{pos+k}$ 可以表示为 $PE_{pos}$ 的线性函数。
* 我们还尝试使用 learned positional embeddings，发现结果几乎相同。
* 最终选择了正弦版本，因为它可能会让模型对超出训练期间遇到的序列长度进行外推（extrapolate to sequence lengths）。

# 4 Why Self-Attention

本节我们对 self-attention 层与循环及卷积层（the recurrent and convolutional layers）进行一个比较，
它们都是常用的**<mark>将一个变长序列的符号表示</mark>** $(x_1, ..., x_n)$ **<mark>映射为另一个同样长度的序列</mark>** $(z_1, ..., z_n)$ 的方式，
其中 $x_i, z_i \in \mathbb{R}^d$，例如典型序列转换 encoder/decoder 中的隐藏层。

## 4.1 Motivation

我们设计 self-attention 有三方面原因：

1. **<mark>每层的计算复杂度</mark>**；
2. **<mark>可以并行化的计算量</mark>**，由所需的最小顺序操作数来衡量；
3. 网络中**<mark>长距离依赖（long-range dependencies）的路径长度</mark>**。

    学习 long-range dependencies 是许多序列转换任务的核心挑战。
    影响学习这种依赖的能力的一个核心因素是**<mark>信号在网络中前向和后向传播的路径长度</mark>**。
    输入和输出序列中任意位置的这种路径越短，long-range dependencies 的学习越容易。
    因此，我们还比较了在多层网络中，输入和输出位置之间任意两个位置的 maximum path length。

## 4.2 与循环网络、卷积网络的计算复杂度对比

如下表所示，

<p align="center">
Table 1: Maximum path lengths, per-layer complexity and minimum number of sequential operations for different layer types.
<code>n</code> 序列长度，
<code>d</code> representation 的维度，
<code>k</code> 卷积的 kernel size，
<code>r</code> restricted self-attention 中的 neighborhood size。
</p>
<p align="center"><img src="/assets/img/attention-is-all-you-need/table-1.png" width="90%" height="90%"></p>

对于 sequential operations，

* 一个 self-attention 层连接**<mark>所有位置</mark>**，因此所需的顺序操作是**<mark>常数</mark>**（换句话说，可以完全并行化，一次完成）；
* 一个循环层则需要 $O(n)$ 个顺序操作。

在计算复杂度方面，

* 当序列长度 $n$ 小于表示维度 $d$ 时，self-attention 层比循环层更快，
* 这在机器翻译领域已经得到证明，例如 word-piece 和 byte-pair 表示。

处理非常长的序列方面：

* 为了提高计算性能，可以限制让 self-attention 只考虑 a neighborhood of size $r$ in the input sequence centered around the respective output position。
* 这会将最大路径长度增加到 $O(n/r)$。我们计划在未来的工作中进一步研究这种方法。

> A single convolutional layer with kernel width `k < n`
> does not connect all pairs of input and output positions. Doing so requires a stack of $O(n/k)$ convolutional layers in the case of contiguous kernels, or $O(log_k(n))$ in the case of dilated convolutions , increasing the length of the longest paths between any two positions in the network.
> Convolutional layers are generally more expensive than recurrent layers, by a factor of $k$. Separable convolutions , however, decrease the complexity considerably, to $O(k \cdot n \cdot d + n \cdot d^2)$. Even with $k=n$, however, the complexity of a separable convolution is equal to the combination of a self-attention layer and a point-wise feed-forward layer, the approach we take in our model.

## 4.3 更具可解释性的模型

除了上述优势，self-attention 还能产生**<mark>更具可解释性的模型</mark>**。

我们检查了 Transformer 模型的 attention 分布，并在附录中展示和讨论了一些例子。
不仅每个 attention head 都明显学会了执行不同的任务，许多 head 还表现出与句子的句法和语义结构相关的行为。

# 5 Training

本节描述 Transformer 的训练方案。

## 5.1 Training Data and Batching

* We trained on the standard WMT 2014 English-German dataset consisting of
  about 4.5 million sentence pairs. Sentences were encoded using byte-pair
  encoding [3], which has a shared source-target vocabulary of about 37000
  tokens.
* For English-French, we used the significantly larger WMT 2014 English-French
  dataset consisting of 36M sentences and split tokens into a 32000 word-piece
  vocabulary [38].
* Sentence pairs were batched together by approximate sequence length. Each
  training batch contained a set of sentence pairs containing approximately
  25000 source tokens and 25000 target tokens.

## 5.2 Hardware and Schedule

在一台 **<mark><code>8 * NVIDIA P100 GPU</code></mark>** 的机器上训练。

* 对于本文描述的超参数/尺寸，我们称为基本模型，每个训练步骤大约需要 0.4 秒。整个训练共 100,000 步或 **<mark>12 小时</mark>**。
* 对于尺寸更大的模型，步骤时间为 1.0 秒。整个训练用了 300,000 步（3.5 天）。

## 5.3 Optimizer

我们使用了 Adam 优化器，其中 $\beta_1=0.9$，$\beta_2=0.98$ 和 $\epsilon=10^{-9}$。
根据以下公式在训练过程中改变学习率：

\begin{equation}
lrate = d_{model}^{-0.5} \cdot
  \min({step\_num}^{-0.5},
    {step\_num} \cdot {warmup\_steps}^{-1.5})
\end{equation}

这对应于在前 $warmup\_steps$ 训练步骤中线性增加学习率，然后在此后按比例减少，与步数的倒数平方根成比例。
我们使用了 $warmup\_steps=4000$。

## 5.4 Regularization

我们在训练过程中使用了几种类型的正则化。

### Residual Dropout

* 对每个子层的输出应用 dropout，然后将其添加到子层输入并进行归一化。
* 对 encoder/decoder 中的 **<mark><code>input embeddings + positional encodings</code></mark>** 的结果应用 dropout。

对于 base 模型，我们使用了 $P_{drop}=0.1$。

### Label Smoothing

在训练过程中，我们使用了 $\epsilon_{ls}=0.1$ 的 label smoothing。这会降低 perplexity，因为模型 learns to be more unsure，但会提高准确性和 BLEU 分数。

# 6 结果

## 6.1 Machine Translation

<p align="center">
Table 2:The Transformer achieves better BLEU scores than previous state-of-the-art models on the English-to-German and English-to-French newstest2014 tests at a fraction of the training cost.
</p>
<p align="center"><img src="/assets/img/attention-is-all-you-need/table-2.png" width="70%" height="70%"></p>

On the WMT 2014 English-to-German translation task, the big transformer model (Transformer (big) outperforms the best previously reported models (including ensembles) by more than $2.0$ BLEU, establishing a new state-of-the-art BLEU score of $28.4$.  The configuration of this model is listed in the bottom line of Table 2.  Training took $3.5$ days on $8$ P100 GPUs.  Even our base model surpasses all previously published models and ensembles, at a fraction of the training cost of any of the competitive models.

On the WMT 2014 English-to-French translation task, our big model achieves a BLEU score of $41.0$, outperforming all of the previously published single models, at less than $1/4$ the training cost of the previous state-of-the-art model. The Transformer (big) model trained for English-to-French used dropout rate $P_{drop}=0.1$, instead of $0.3$.

For the base models, we used a single model obtained by averaging the last 5 checkpoints, which were written at 10-minute intervals.  For the big models, we averaged the last 20 checkpoints. We used beam search with a beam size of $4$ and length penalty $\alpha=0.6$ .  These hyperparameters were chosen after experimentation on the development set.  We set the maximum output length during inference to input length + $50$, but terminate early when possible .

Table 2 summarizes our results and compares our translation quality and training costs to other model architectures from the literature.  We estimate the number of floating point operations used to train a model by multiplying the training time, the number of GPUs used, and an estimate of the sustained single-precision floating-point capacity of each GPU.

## 6.2 Model Variations

<p align="center">
Table 3:Variations on the Transformer architecture. Unlisted values are identical to those of the base model. All metrics are on the English-to-German translation development set, newstest2013. Listed perplexities are per-wordpiece, according to our byte-pair encoding, and should not be compared to per-word perplexities.
</p>
<p align="center"><img src="/assets/img/attention-is-all-you-need/table-3.png" width="65%" height="65%"></p>

To evaluate the importance of different components of the Transformer, we varied our base model in different ways, measuring the change in performance on English-to-German translation on the development set, newstest2013. We used beam search as described in the previous section, but no checkpoint averaging.  We present these results in Table 3.

In Table 3 rows (A), we vary the number of attention heads and the attention key and value dimensions, keeping the amount of computation constant, as described in Section multihead. While single-head attention is 0.9 BLEU worse than the best setting, quality also drops off with too many heads.

In Table 3 rows (B), we observe that reducing the attention key size $d_k$ hurts model quality. This suggests that determining compatibility is not easy and that a more sophisticated compatibility function than dot product may be beneficial. We further observe in rows (C) and (D) that, as expected, bigger models are better, and dropout is very helpful in avoiding over-fitting.  In row (E) we replace our sinusoidal positional encoding with learned positional embeddings , and observe nearly identical results to the base model.

## 6.3 English Constituency Parsing

<p align="center">
Table 4:The Transformer generalizes well to English constituency parsing (Results are on Section 23 of WSJ)
</p>
<p align="center"><img src="/assets/img/attention-is-all-you-need/table-4.png" width="70%" height="70%"></p>

To evaluate if the Transformer can generalize to other tasks we performed experiments on English constituency parsing. This task presents specific challenges: the output is subject to strong structural constraints and is significantly longer than the input.
Furthermore, RNN sequence-to-sequence models have not been able to attain state-of-the-art results in small-data regimes.

We trained a 4-layer transformer with $d_{model} = 1024$ on the Wall Street Journal (WSJ) portion of the Penn Treebank , about 40K training sentences. We also trained it in a semi-supervised setting, using the larger high-confidence and BerkleyParser corpora from with approximately 17M sentences . We used a vocabulary of 16K tokens for the WSJ only setting and a vocabulary of 32K tokens for the semi-supervised setting.

We performed only a small number of experiments to select the dropout, both attention and residual, learning rates and beam size on the Section 22 development set, all other parameters remained unchanged from the English-to-German base translation model. During inference, we increased the maximum output length to input length + $300$. We used a beam size of $21$ and $\alpha=0.3$ for both WSJ only and the semi-supervised setting.

Our results in Table 4 show that despite the lack of task-specific tuning our model performs surprisingly well, yielding better results than all previously reported models with the exception of the Recurrent Neural Network Grammar.

In contrast to RNN sequence-to-sequence models , the Transformer outperforms the BerkeleyParser even when training only on the WSJ training set of 40K sentences.

# 7 Conclusion

本文提出了 Transformer，这是第一个**<mark>完全基于 attention 的序列转换模型</mark>**，
用 multi-head attention 替代了 **<mark>encoder-decoder 架构中最常用的循环层</mark>**。

对于**<mark>翻译任务</mark>**，Transformer 的**<mark>训练速度</mark>**比基于循环或卷积层的架构快得多。
在 WMT 2014 英德和英法翻译任务中，我们达到了新的 SOTA 结果。对于英德翻译，我们的最佳模型甚至超过了所有已知模型的结果。

展望未来，我们对**<mark>基于 attention 的模型</mark>**充满期待，并计划将其应用于其他任务。
我们计划将 Transformer 扩展到文本以外的涉及输入/输出模态（involving input and output modalities）的场景，
并研究局部、受限的 attention 机制，以有效处理大输入和输出，如图像、音频和视频。
让生成过程尽量避免顺序执行（making generation less sequential）也是我们的一个研究目标。

The code we used to train and evaluate our models is available at https://github.com/tensorflow/tensor2tensor.

# 致谢

We are grateful to Nal Kalchbrenner and Stephan Gouws for
their fruitful comments, corrections and inspiration.

# 参考文献

1. **Jimmy Lei Ba, Jamie Ryan Kiros, and Geoffrey E Hinton.**
   Layer normalization.
   *arXiv preprint arXiv:1607.06450*, 2016.
2. **Dzmitry Bahdanau, Kyunghyun Cho, and Yoshua Bengio.**
   Neural machine translation by jointly learning to align and translate.
   *CoRR*, abs/1409.0473, 2014.
3. **Denny Britz, Anna Goldie, Minh-Thang Luong, and Quoc V. Le.**
   Massive exploration of neural machine translation architectures.
   *CoRR*, abs/1703.03906, 2017.
4. **Jianpeng Cheng, Li Dong, and Mirella Lapata.**
   Long short-term memory-networks for machine reading.
   *arXiv preprint arXiv:1601.06733*, 2016.
5. **Kyunghyun Cho, Bart van Merrienboer, Caglar Gulcehre, Fethi Bougares, Holger Schwenk, and Yoshua Bengio.**
   Learning phrase representations using RNN encoder-decoder for statistical machine translation.
   *CoRR*, abs/1406.1078, 2014.
6. **Francois Chollet.**
   Xception: Deep learning with depthwise separable convolutions.
   *arXiv preprint arXiv:1610.02357*, 2016.
7. **Junyoung Chung, Çaglar Gülçehre, Kyunghyun Cho, and Yoshua Bengio.**
   Empirical evaluation of gated recurrent neural networks on sequence modeling.
   *CoRR*, abs/1412.3555, 2014.
8. **Chris Dyer, Adhiguna Kuncoro, Miguel Ballesteros, and Noah A. Smith.**
   Recurrent neural network grammars.
   In *Proc. of NAACL*, 2016.
9. **Jonas Gehring, Michael Auli, David Grangier, Denis Yarats, and Yann N. Dauphin.**
   Convolutional sequence to sequence learning.
   *arXiv preprint arXiv:1705.03122v2*, 2017.
10. **Alex Graves.**
    Generating sequences with recurrent neural networks.
    *arXiv preprint arXiv:1308.0850*, 2013.
11. **Kaiming He, Xiangyu Zhang, Shaoqing Ren, and Jian Sun.**
    Deep residual learning for image recognition.
    In *Proceedings of the IEEE Conference on Computer Vision and Pattern Recognition*, pages 770–778, 2016.
12. **Sepp Hochreiter, Yoshua Bengio, Paolo Frasconi, and Jürgen Schmidhuber.**
    Gradient flow in recurrent nets: the difficulty of learning long-term dependencies, 2001.
13. **Sepp Hochreiter and Jürgen Schmidhuber.**
    Long short-term memory.
    *Neural computation*, 9(8):1735–1780, 1997.
14. **Zhongqiang Huang and Mary Harper.**
    Self-training PCFG grammars with latent annotations across languages.
    In *Proceedings of the 2009 Conference on Empirical Methods in Natural Language Processing*, pages 832–841. ACL, August 2009.
15. **Rafal Jozefowicz, Oriol Vinyals, Mike Schuster, Noam Shazeer, and Yonghui Wu.**
    Exploring the limits of language modeling.
    *arXiv preprint arXiv:1602.02410*, 2016.
16. **Łukasz Kaiser and Samy Bengio.**
    Can active memory replace attention?
    In *Advances in Neural Information Processing Systems, (NIPS)*, 2016.
17. **Łukasz Kaiser and Ilya Sutskever.**
    Neural GPUs learn algorithms.
    In *International Conference on Learning Representations (ICLR)*, 2016.
18. **Nal Kalchbrenner, Lasse Espeholt, Karen Simonyan, Aaron van den Oord, Alex Graves, and Koray Kavukcuoglu.**
    Neural machine translation in linear time.
    *arXiv preprint arXiv:1610.10099v2*, 2017.
19. **Yoon Kim, Carl Denton, Luong Hoang, and Alexander M. Rush.**
    Structured attention networks.
    In *International Conference on Learning Representations*, 2017.
20. **Diederik Kingma and Jimmy Ba.**
    Adam: A method for stochastic optimization.
    In *ICLR*, 2015.
21. **Oleksii Kuchaiev and Boris Ginsburg.**
    Factorization tricks for LSTM networks.
    *arXiv preprint arXiv:1703.10722*, 2017.
22. **Zhouhan Lin, Minwei Feng, Cicero Nogueira dos Santos, Mo Yu, Bing Xiang, Bowen Zhou, and Yoshua Bengio.**
    A structured self-attentive sentence embedding.
    *arXiv preprint arXiv:1703.03130*, 2017.
23. **Minh-Thang Luong, Quoc V. Le, Ilya Sutskever, Oriol Vinyals, and Lukasz Kaiser.**
    Multi-task sequence to sequence learning.
    *arXiv preprint arXiv:1511.06114*, 2015.
24. **Minh-Thang Luong, Hieu Pham, and Christopher D Manning.**
    Effective approaches to attention-based neural machine translation.
    *arXiv preprint arXiv:1508.04025*, 2015.
25. **Mitchell P Marcus, Mary Ann Marcinkiewicz, and Beatrice Santorini.**
    Building a large annotated corpus of english: The penn treebank.
    *Computational linguistics*, 19(2):313–330, 1993.
26. **David McClosky, Eugene Charniak, and Mark Johnson.**
    Effective self-training for parsing.
    In *Proceedings of the Human Language Technology Conference of the NAACL, Main Conference*, pages 152–159. ACL, June 2006.
27. **Ankur Parikh, Oscar Täckström, Dipanjan Das, and Jakob Uszkoreit.**
    A decomposable attention model.
    In *Empirical Methods in Natural Language Processing*, 2016.
28. **Romain Paulus, Caiming Xiong, and Richard Socher.**
    A deep reinforced model for abstractive summarization.
    *arXiv preprint arXiv:1705.04304*, 2017.
29. **Slav Petrov, Leon Barrett, Romain Thibaux, and Dan Klein.**
    Learning accurate, compact, and interpretable tree annotation.
    In *Proceedings of the 21st International Conference on Computational Linguistics and 44th Annual Meeting of the ACL*, pages 433–440. ACL, July 2006.
30. **Ofir Press and Lior Wolf.**
    Using the output embedding to improve language models.
    *arXiv preprint arXiv:1608.05859*, 2016.
31. **Rico Sennrich, Barry Haddow, and Alexandra Birch.**
    Neural machine translation of rare words with subword units.
    *arXiv preprint arXiv:1508.07909*, 2015.
32. **Noam Shazeer, Azalia Mirhoseini, Krzysztof Maziarz, Andy Davis, Quoc Le, Geoffrey Hinton, and Jeff Dean.**
    Outrageously large neural networks: The sparsely-gated mixture-of-experts layer.
    *arXiv preprint arXiv:1701.06538*, 2017.
33. **Nitish Srivastava, Geoffrey E Hinton, Alex Krizhevsky, Ilya Sutskever, and Ruslan Salakhutdinov.**
    Dropout: a simple way to prevent neural networks from overfitting.
    *Journal of Machine Learning Research*, 15(1):1929–1958, 2014.
34. **Sainbayar Sukhbaatar, Arthur Szlam, Jason Weston, and Rob Fergus.**
    End-to-end memory networks.
    In C. Cortes, N. D. Lawrence, D. D. Lee, M. Sugiyama, and R. Garnett, editors, *Advances in Neural Information Processing Systems 28*, pages 2440–2448. Curran Associates, Inc., 2015.
35. **Ilya Sutskever, Oriol Vinyals, and Quoc VV Le.**
    Sequence to sequence learning with neural networks.
    In *Advances in Neural Information Processing Systems*, pages 3104–3112, 2014.
36. **Christian Szegedy, Vincent Vanhoucke, Sergey Ioffe, Jonathon Shlens, and Zbigniew Wojna.**
    Rethinking the inception architecture for computer vision.
    *CoRR*, abs/1512.00567, 2015.
37. **Vinyals & Kaiser, Koo, Petrov, Sutskever, and Hinton.**
    Grammar as a foreign language.
    In *Advances in Neural Information Processing Systems*, 2015.
38. **Yonghui Wu, Mike Schuster, Zhifeng Chen, Quoc V Le, Mohammad Norouzi, Wolfgang Macherey, Maxim Krikun, Yuan Cao, Qin Gao, Klaus Macherey, et al.**
    Google's neural machine translation system: Bridging the gap between human and machine translation.
    *arXiv preprint arXiv:1609.08144*, 2016.
39. **Jie Zhou, Ying Cao, Xuguang Wang, Peng Li, and Wei Xu.**
    Deep recurrent models with fast-forward connections for neural machine translation.
    *CoRR*, abs/1606.04199, 2016.
40. **Muhua Zhu, Yue Zhang, Wenliang Chen, Min Zhang, and Jingbo Zhu.**
    Fast and accurate shift-reduce constituent parsing.
    In *Proceedings of the 51st Annual Meeting of the ACL (Volume 1: Long Papers)*, pages 434–443. ACL, August 2013.

# 附录：Attention 的可视化

## Attention 机制学习长距离依赖的例子

<p align="center"><img src="/assets/img/attention-is-all-you-need/x1.png" width="40%" height="40%"></p>
<p align="center"> Figure 3：一个 attention 机制跟踪长距离依赖的例子，来自第 5 层（总共 6 层）中的 encoder self-attention。</p>

这里只展示了 ‘making’ 的 attention。

* **<mark>不同颜色代表不同的 attention head</mark>**。
* 可以看到，多个 attention head 都在关注动词 "making" 的 distant dependency，
  一起凑成短语 **<mark><code>"making … more difficult"</code></mark>**。

## 代词解析（anaphora resolution）

这里展示两个 attention head，也在第 5 层（总共 6 层）中，显然涉及到了**<mark>代词解析</mark>**，

<p align="center"><img src="/assets/img/attention-is-all-you-need/x2-3.png" width="80%" height="80%"></p>
<p align="center">
图 4：<br />
（左）head 5 的完整 attention。<br />
（右）：heads 5 和 6 针对 <mark><code>"its"</code></mark> 这个词的具体 attention。注意到，这个词的 attention 非常集中。
</p>

## 句子结构与 attention head 学习行为

许多 attention head 表现出与句子结构相关的行为。下面给出了两个这样的例子，来自第 5 层（总共 6 层）中的 encoder self-attention。
这些 head 明显学会了执行不同的任务。

<p align="center"><img src="/assets/img/attention-is-all-you-need/x4-5.png" width="80%" height="80%"></p>
<p align="center">图 5：许多 attention head 表现出与句子结构相关的行为。</p>

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>

