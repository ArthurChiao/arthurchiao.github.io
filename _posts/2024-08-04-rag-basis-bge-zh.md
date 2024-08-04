---
layout    : post
title     : "大模型 RAG 基础：信息检索、文本向量化及 BGE-M3 embedding 实践（2024）"
date      : 2024-08-04
lastupdate: 2024-08-04
categories: ai llm
---

本文整理一些文本向量化（embedding）和信息检索的知识，它们是如今大模型生成文本时常用的技术 —— “增强检索生成”（RAG）—— 的基础：

<p align="center"><img src="/assets/img/rag-basis-bge/bert-embedding-similarity.svg" width="100%"/></p>
<p align="center">Fig. Similarity score based on BERT embedding. <a href="https://docs.kolena.com/metrics/bertscore/">Image source</a></p>

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----

RAG (Retrieval-Augmented Generation，检索增强生成)，是一种利用信息检索（Information Retrieval）
技术增强大模型生成效果（generation）的技术。RAG 在步骤上很简单，

1. **<mark>搭建高质量文档数据库</mark>**
    * 对优质文档进行某种格式的转换（或称编码），例如基于 BERT 将文本段落转换成
      **<mark>数值格式的向量</mark>**（这个过程称为 **<mark><code>embedding</code></mark>**），然后
    * 将这些 embeddings 存储到合适的数据库（例如 ES 或**<mark>向量数据库</mark>**）；
2. **<mark>针对用户输入进行数据库检索</mark>**
    * 对用户输入的 query 进行相同的转换（embedding），然后
    * 利用最近邻等相似性算法，在文档库中**<mark>寻找最相似的文本段落</mark>**（与给定问题最相关的段落）；
3. **<mark>大模型生成返回给用户的内容</mark>**
    * 将找到文本段落送到大模型，辅助生成最终的输出文本，返回给用户。

本文主要关注以上 1 & 2 步骤中的 embedding & retrieval 阶段。

# 1 信息检索（information retrieval）技术三大发展阶段

信息检索的技术发展大致可分为三个阶段：

1. **<mark>基于统计信息</mark>**的**<mark>关键字匹配</mark>**（statistical keyword matching）

    * 是一种 **<mark><code>sparse embedding</code></mark>** —— embedding 向量的大部分字段都是 0；

2. 基于**<mark>深度学习</mark>**模型的**<mark>上下文和语义理解</mark>**，

    * 属于 **<mark><code>dense embedding</code></mark>** —— embedding 向量的大部分字段都非零；

3. 所谓的“学习型”表示，组合上面两种的优点，称为 **<mark><code>learned sparse embedding</code></mark>**

    * 既有深度学习模型的上下文和语义理解能力；
    * 又具备稀疏表示的可解释性（interpretability of sparse representations）和低计算复杂度。

下面分别来看。

## 1.1 基于统计信息和关键词匹配（`1970s-2010s`）

### 1.1.1 典型算法：`TF-IDF`、`BM25`

早期信息检索系统主要是**<mark>基于统计信息</mark>** + **<mark>匹配关键词</mark>**，算法包括，

* [TF-IDF](https://en.wikipedia.org/wiki/Tf%E2%80%93idf) (term frequency - inverse document frequency), 1970s
* [BM25](https://en.wikipedia.org/wiki/Okapi_BM25) (Best Matching), 1980s

### 1.1.2 原理

分析**<mark>语料库的词频和分布</mark>**（term frequency and distribution），
作为评估**<mark>文档的相关性</mark>**（document relevance）的基础。

### 1.1.3 优缺点

* 优点：方法简单，效果不错，所以使用很广泛。
* 缺点：单纯根据词频等统计和关键字检索做判断，不理解语义。

## 1.2 基于深度学习和上下文语义

### 1.2.1 `Word2Vec` (Google, 2013)

2013 年，谷歌提出了 [Word2Vec](https://zilliz.com/learn/transforming-text-the-rise-of-sentence-transformers-in-nlp)，

* 首次尝试**<mark>使用高维向量来表示单词</mark>**，能分辨它们细微的语义差别；
* 标志着向**<mark>机器学习驱动</mark>**的信息检索的转变。

### 1.2.2 `BERT` (Google, 2019)

基于 transformer 的**<mark>预训练（pretrain）语言模型</mark>** BERT 的出现，彻底颠覆了传统的信息检索范式。

#### 核心设计和优点

1. transformer 的核心是 self-attention，
    * self-attention 能**<mark>量化给定单词与句子中其他单词的关联性程度</mark>**，
    * 换句话说就是：能在上下文中分辨单词的含义；
2. BERT 是双向（前向+后向）transformer，
    * 可以理解为在预训练时，每个句子正向读一遍，反向再读一遍；
    * 能更好地捕获句子的上下文语义（contextual semantics）；
    * 最终输出是一个 **<mark>dense vector</mark>**，本质上是对语义的压缩；
3. 基于 dense vector 描述，用最近邻算法就能对给定的 query 进行检索，强大且语义准确。

#### 局限性：领域外（Out-of-Domain）信息检索效果差

BERT 严重依赖**<mark>预训练数据集</mark>**的领域知识（domain-specific knowledge），
预训练过程使 BERT 偏向于预训练数据的特征，
因此在领域外（Out-Of-Domain），例如没有见过的文本片段，表现就不行了。

解决方式之一是**<mark><code>fine-tune</code></mark>**（精调/微调），但成本相对较高，
因为准备高质量数据集的成本是很高的。

另一方面，尽管传统 sparse embedding 在词汇不匹配问题时虽然也存在挑战，
但在领域外信息检索中，它们的表现却优于 BERT。
这是因为在这类算法中，未识别的术语不是靠“学习”，而是单纯靠“匹配”。

## 1.3 学习型：组合前两种的优点

### 1.3.1 原理：传统 sparse vector 与上下文化信息的融合

1. 先通过 BERT 等深度学习模型生成 dense embedding；
2. 再引入额外的步骤对以上 dense embedding 进行稀疏化，得到一个 sparse embedding；

代表算法：BGE-M3。

### 1.3.2 与传统 sparse embedding 的区别

根据以上描述，乍一看，这种 learned sparse embedding 与传统 sparse embedding 好像没太大区别，
但实际上二者有着本质不同，这种 embedding，

* 引入了 Token Importance Estimation；
* 既保留了关键词搜索能力，又利用上下文信息，丰富了 embedding 的稀疏表示；
* 能够辨别相邻或相关的 token 的重要性，即使这些 token 在文本中没有明确出现。

### 1.3.3 优点

* 将稀疏表示与学习上下文结合，同时具备精确匹配和语义理解两大能力，在领域外场景有很强的泛化能力；
* 与 dense embedding 相比更简洁，只保留了最核心的文本信息；
* 固有的稀疏性使向量相似性搜索所需的计算资源极少；
* 术语匹配特性还增强了可解释性，能够更精确地洞察底层的检索过程，提高了系统的透明度。

# 2 信息检索：三种 embedding 的对比

简单来说，
vector embedding，或称向量表示，是一个单词或句子在**<mark>高维向量空间</mark>**中的**<mark>数值表示</mark>**。

* 高维空间：一个维度能代表一个特征或属性，高维意味着分辨率高，能区分细微的语义差异；
* 数值表示：一个 embedding 一般就是一个**<mark>浮点数数组</mark>**，所以方便计算。

对应上一节介绍的三个主要发展阶段，常见的有三种 embedding 类型：

1. traditional sparse embedding
2. dense embedding
3. learned sparse embedding

## 2.1 Sparse embedding (lexical matching)

* 映射成一个高维（维度一般就是 vocabulary 空间大小）向量
* 向量的大部分元素都是 0，非零值表明 token 在特定文档中的相对重要性，只为那些输入文本中出现过的 token 计算权重
* 典型模型：BM25（对 TF-IDF 的改进）

非常适合**<mark>关键词匹配</mark>**任务（keyword-matching tasks）。

## 2.2 Dense embedding (e.g. BERT-based)

* 映射到一个（相对低维）向量，所有维度都非零
* 相比 sparse embedding 维度要低很多，例如基于 BERT 默认 `1x768` 维度；
* 典型模型：BGE-v1.5


所有维度都非零，包含语义理解，信息非常丰富，因此适用于
**<mark>语义搜索</mark>**任务（semantic search tasks）。

> Multi-vector retrieval
>
> * 用多个向量表示一段文本，可以看做是对 dense retrieval 的一种扩展
> * 模型：ColBERT

## 2.3 Learned sparse embedding

结合了传统 sparse embedding 的精确度和 dense embedding 的语义丰富性，

* 可以通过深度学习模型“学习”相关 token 的重要性，即使是一些并未出现过的 token，
* 生成的“学习型”稀疏表示，能有效捕捉 query 和 doc 中的关键词。

# 3 Embedding & retrieval 工作原理详解

这里主要介绍 BGE-M3 模型的原理。**<mark>BGE-M3 建立在 BERT 之上</mark>**，因此需要先回顾 BERT 的基本原理。

## 3.1 `BERT` 是如何工作的

### 3.1.1 理论基础

* BERT 论文：[<mark>BERT：预训练深度双向 Transformers 做语言理解</mark>（Google，2019）]({% link _posts/2024-03-10-bert-paper-zh.md %})
* BERT 基于 transformer，后者的核心是 self-attention
    * [Transformer 是如何工作的：600 行 Python 代码实现 self-attention 和两类 Transformer（2019）]({% link _posts/2023-06-06-transformers-from-scratch-zh.md %})
    * [什么是 GPT？Transformer 工作原理的动画展示（2024）]({% link _posts/2024-05-12-visual-intro-to-transformers-zh.md %})

### 3.1.2 BERT dense embedding 工作流

以输入 **<mark><code>"Milvus is a vector database built for scalable similarity search"</code></mark>** 为例，工作过程 [2]：

<p align="center"><img src="/assets/img/rag-basis-bge/bert-dense-embedding.png" width="90%"/></p>
<p align="center">Fig. BERT dense embedding.</p>

1. **<mark><code>Tokenization</code></mark>**
    1. 将输入文本转成 token 序列
    2. BERT 还会插入两个特殊的 token：`[CLS]` token 表示开始，`[SEP]` token 表示一个句子的结束。
2. **<mark><code>Embedding</code></mark>**：使用 embedding matrix 将每个 token 转换为一个向量，详见 BERT 论文；
3. **<mark><code>Encoding</code></mark>**：这些向量通过多层 encoder，每层由 self-attention 和 feed-forward 神经网络组成
    1. 会根据所有其他 token 提供的上下文细化每个 token 的表示。
4. **<mark><code>Output</code></mark>**：输出一系列最终的 **<mark>embedding vectors</mark>**。

最终生成的 dense embedding 能够捕捉单个单词的含义及其在句子中的相互关系。

理解 BERT 是如何生成 dense embedding 之后，接下来看看基于 BERT dense embedding 的信息检索是如何工作的。

## 3.2 基于 BERT dense embedding 的文档检索是如何工作的

有了 dense embedding 之后，针对给定文本输入检索文档就很简单了，只需要再加一个最近邻之类的算法就行。

下面是两个句子的相似度判断，原理跟文档检索是一样的：

<p align="center"><img src="/assets/img/rag-basis-bge/bert-embedding-similarity.svg" width="100%"/></p>
<p align="center">Fig. Similarity score based on BERT embedding. <a href="https://docs.kolena.com/metrics/bertscore/">Image source</a></p>

下面看个具体的 embedding & retrieval 模型：BGE-M3。

## 3.3 `BGE-M3`（BERT-based learned sparse embedding）是如何工作的？

BGE 是一系列 embedding 模型，扩展了 BERT 的能力。[BGE-M3](https://github.com/FlagOpen/FlagEmbedding/tree/master/FlagEmbedding/BGE_M3)
是目前最新的一个，3 个 M 是强调的多个 `multi-` 能力：

* Multi-Functionality
* Multi-Linguisticity
* Multi-Granularity

### 3.3.1 设计 & 特点

BGE-M3 通过更精细的方法来捕捉每个 token 的重要性，

1. **<mark><code>Token importance estimation</code></mark>**：BERT 在分类/相似性比较时仅关注第一个 token（`[CLS]`）， BGE-M3 则扩大到关注序列中的每个 token <code>H<sub>i</sub></code>；
2. 线性变换：在 encoder 的输出层上又增加一个线性层，计算每个 token 的 importance weights <code>W<sub>lex</sub></code>；
3. 激活函数：
    * <code>W<sub>lex</sub></code> 和 <code>H<sub>i</sub></code> 的乘积经过 Rectified Linear Unit (ReLU) 激活函数，得到每个 token 的术语权重 <code>W<sub>t</sub></code>。
    * ReLU 的结果是非负的，有助于 embedding 的稀疏性。
4. **<mark><code>learned sparse embedding</code></mark>**：以上输出的是一个 sparse embedding，其中每个 token 都有一个相关的 weights，表明在整个输入文本上下文中的重要性。

下面看个例子。

### 3.3.2 BGE-M3 生成 learned sparse embedding 的过程

还是前面例子提到的输入，

1. 先走 BERT dense embedding 的流程，
2. 最后加一个 linear 层，得到 learned sparse embedding。

<p align="center"><img src="/assets/img/rag-basis-bge/bgem3-embedding-output.webp" width="75%"/></p>
<p align="center">Fig. BGE-M3 <mark>learned sparse embedding</mark>.
<a href="https://medium.com/@zilliz_learn/exploring-bge-m3-and-splade-two-machine-learning-models-for-generating-sparse-embeddings-0772de2c52a7">Image source</a></p>

# 4 BGE-M3 实战

## 4.1 相似度判断（检索）

```shell
$ pip install FlagEmbedding peft sentencepiece
```

来自官方的代码，稍作修改：

```python
from FlagEmbedding import BGEM3FlagModel

model = BGEM3FlagModel('/root/bge-m3', use_fp16=True)

queries = ["What is BGE M3?",
           "Defination of BM25"]
docs = ["BGE M3 is an embedding model supporting dense retrieval, lexical matching and multi-vector interaction.",
        "BM25 is a bag-of-words retrieval function that ranks a set of documents based on the query terms appearing in each document"]

query_embeddings = model.encode(queries, batch_size=12, max_length=8192,)['dense_vecs']
docs_embeddings  = model.encode(docs)['dense_vecs']
similarity = query_embeddings @ docs_embeddings.T
print(similarity)
```

这个例子是两个问题，分别去匹配两个答案，看彼此之间的相似度（四种组合），运行结果：

```shell
[[0.626  0.348 ]
 [0.3499 0.678 ]]
```

* 问题 1 和答案 1 相似度是 0.6265
* 问题 2 和答案 2 相似度是 0.678
* 问题 1 和答案 2，以及问题 2 和答案 1，相似度只有 0.3x

符合预期。

## 4.2 精调（fine-tune）

精调的目的是让正样本和负样本的分数差变大。

### 4.2.1 官方文档

1. [fine-tune the dense embedding](https://github.com/FlagOpen/FlagEmbedding/tree/master/examples/finetune)
2. [fine-tune all embedding function of m3 (dense, sparse and colbert)](https://github.com/FlagOpen/FlagEmbedding/tree/master/examples/unified_finetune)

### 4.2.2 训练数据格式及要求

1. 文件为 **<mark><code>jsonl</code></mark>** 格式，每行一个 sample；
    * 例子：[toy_train_data/toy_train_data1.jsonl](https://github.com/FlagOpen/FlagEmbedding/blob/master/examples/unified_finetune/toy_train_data/toy_train_data1.jsonl)
2. 每个 sample 的格式：**<mark><code>{"query": str, "pos": List[str], "neg":List[str]}</code></mark>**
    * `query`：用户问题；
    * `pos`：正样本列表，简单说就是期望给到用户的回答；不能为空，也就是说必需得有正样本；
    * `neg`：负样本列表，是避免给到用户的回答。
        * 空要写成 **<mark><code>"neg": [""]</code></mark>**，写 **<mark><code>"neg": []</code></mark>** 会报错。
        * 另外为空时试过删掉 `"neg": []` 也不行，必须得留着这个字段。

注意：

1. **<mark>不是标准 json 格式</mark>**，所以 python 直接导出一个 json 文件作为训练数据集是不行的。
2. sample 不能分行，一个 sample 一行。

### 4.2.3 精调命令及参数配置

从 huggingface 或国内的 modelscope 下载 BGE-M3 模型，

```shell
$ git lfs install
$ git clone https://www.modelscope.cn/Xorbits/bge-m3.git
```

精调命令：

```shell
$ cat sft.sh
#!/bin/bash

num_gpus=1
output_dir=/root/bge-sft-output
model_path=/root/bge-m3
train_data=/data/share/bge-dataset
batch_size=2
query_max_len=128    # max 8192
passage_max_len=1024 # max 8192

torchrun --nproc_per_node $num_gpus \
    -m FlagEmbedding.BGE_M3.run \
    --output_dir $output_dir \
    --model_name_or_path $model_path \
    --train_data $train_data \
    --learning_rate 1e-5 \
    --fp16 \
    --num_train_epochs 5 \
    --per_device_train_batch_size $batch_size \
    --dataloader_drop_last True \
    --normlized True \
    --temperature 0.02 \
    --query_max_len $query_max_len \
    --passage_max_len $passage_max_len \
    --train_group_size 2 \
    --negatives_cross_device \
    --logging_steps 10 \
    --same_task_within_batch True \
    --save_steps 10000 \
    --unified_finetuning True \
    --use_self_distill True
```

几个参数要特别注意下：

1. query & doc 最大长度

    * **<mark><code>query_max_len</code></mark>**：支持的最长 query，最大 `8192`；
    * **<mark><code>passage_max_len</code></mark>**：支持的最长文档（一条 pos 或 neg 记录）长度，最大 `8192`

    BGE-M3 会分别针对 query 和 doc 初始化两个 tokenizer，以上两个参数其实对应
    **<mark>tokenizer 的 max_length</mark>**，而 tokenizer 最大支持 8192（见模型目录 `tokenizer_config.json`）。

2. **<mark><code>batch_size</code></mark>**：并行度，直接决定了显存占用大小和精调快慢；
    * BGE-M3 跑起来之后显存占用是恒定的，所以可以多试几个 batch size 配置，把显存用到最大；
3. **<mark><code>save_steps</code></mark>**：多少个 step 保存一次 checkpoint，默认值 500 太小，每个 checkpoint `~7GB`，多了之后可能会打爆磁盘导致任务失败。

精调快慢取决于 GPU 算力、显存和参数配置，精调开始之后也会打印出预估的完成时间，还是比较准的。

### 4.2.4 测试精调之后的效果

还是用 4.1 的代码，稍微改一下，不要把 queries 和 docs 作为列表，而是针对每个 query 和 pos/neg 计算相似度得分。
然后针对测试集跑一下，看相似性分数是否有提升。

数据集质量可以的话，精调之后区分度肯定有提升。

## 4.3 CPU 运行速度优化：将模型转 onnx 格式

如果是在 CPU 上跑模型（不用 GPU），
根据之前实际的 BERT 工程经验，转成 onnx 之后能快几倍，尤其是在 Intel CPU 上
（Intel 公司做了很多优化合并到社区库了）。

但 BGE-M3 官方没有转 onnx 文档，根据[第三方的库](https://huggingface.co/aapot/bge-m3-onnx/tree/main)能成功（稍微改点代码，从本地加载模型），效果待验证。

# 5 总结

本文整理了一些 BGE-M3 相关的 RAG 知识。前两篇参考资料非常好，本文很多内容都来自它们，感谢作者。

# 参考资料

1. [Enhancing Information Retrieval with Sparse Embeddings](https://zilliz.com/learn/enhancing-information-retrieval-learned-sparse-embeddings), zilliz.com/learn, 2024
2. [Exploring BGE-M3 and Splade: Two Machine Learning Models for Generating Sparse Embeddings](https://medium.com/@zilliz_learn/exploring-bge-m3-and-splade-two-machine-learning-models-for-generating-sparse-embeddings-0772de2c52a7), medium.com/@zilliz_learn, 2024
3. [BGE-M3 paper](https://arxiv.org/html/2402.03216v4)

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
