---
layout    : post
title     : "[笔记] 从 Tokenization 视角看生成式推荐（GR）近几年的发展（2025）"
date      : 2025-11-27
lastupdate: 2025-11-27
categories: ai llm
---

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/model-as-reflection-of-real-world.png" width="90%" height="90%"></p>

| 不同类型的真实世界 | 建模元素 | 对应的模型类型 |
|:-------|:-------|:----------|
| 感知世界（**<mark><code>Perceptual World</code></mark>**） | 视觉（Vision） | <mark>扩散模型</mark>（Diffusion Models, DMs） |
| 认知世界（**<mark><code>Cognitive World</code></mark>**） | 语言（Language） | <mark>大语言模型</mark>（LLMs） |
| 行为世界（**<mark><code>Behavioral World</code></mark>**） | 交互（Interaction） | <mark>用户行为的模型</mark>？ |

从模型和现实世界的对应关系来看，**<mark>感知世界</mark>**（Perceptual World）和
**<mark>认知世界</mark>**（Cognitive World）
都已经有了对应的大模型类型，分别基于**<mark>视觉</mark>**（Vision）和**<mark>语言</mark>**（Language） 建模，
并且基本都是基于**<mark>生成式</mark>**架构，实际效果非常好。

推荐领域属于**<mark>行为世界</mark>**（Behavioral World），
这个场景基于**<mark>交互</mark>**（Interaction）建模，目前还没有跟前两个领域一样成功的模型。
一个思路是：**<mark>如果大量场景已经充分证明了生成式是一把非常好的锤子，
那我们是不是能把还没有很好解决的问题变成钉子</mark>**？—— 具体到推荐场景，
就是通过一些工程和算法手段，把**<mark>推荐任务</mark>**变成一个**<mark>生成任务</mark>**，从而套到生成式框架里。
这就是**<mark>生成式推荐模型</mark>**（generative recommendation models）背后的思想。

最近有一篇很详尽的关于这个领域近几年发展的综述：
[Towards Large Generative Recommendation: A Tokenization Perspective](https://large-genrec.github.io/cikm2025.html)。
本文整理一些阅读笔记和思考。

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----

大型生成式模型（**<mark><code>large generative models</code></mark>**）的出现正在深刻改变**<mark>推荐系统</mark>**领域。
构建此类模型的基础组件之一是 **<mark><code>action tokenization</code></mark>**，
即将**<mark>人类可读数据</mark>**（例如用户-商品交互数据）转换为**<mark>机器可读格式</mark>**（例如离散 token 序列），
这个过程在进入模型之前。

本文介绍几种 action tokenization 技术（将用户行为分别转换为**<mark>物品 ID、文本描述、语义 ID</mark>**），
然后从 action tokenization 的视角探讨生成式推荐领域面临的挑战、开放性问题及未来潜在发展方向，为下一代推荐系统的设计提供启发。

# 1 背景

## 1.1 什么是生成式模型（Generative Models）？

生成式模型**<mark>从大量给定样本中学习</mark>**到**<mark>底层的数据分布</mark>**（underlying distribution of data），
然后就能**<mark>生成新的样本</mark>**（generate new samples）。如下图所示，在学习了大量动物图文之后，
模型就能根据给定指令生成动物照片（“奔跑的猫/狗/马”），

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/what-are-generative-models.png" width="90%" height="90%"></p>

## 1.2 什么是规模定律（Scaling laws）？

Scaling laws 提供了一个框架，通过这框架可以理解 **<mark><code>model size, data volume, test-time computing</code></mark>**
如何影响 **<mark>AI 能力的进化</mark>**。语言建模领域已经验证了这一框架的有效性。

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/scaling-law.png" width="90%" height="90%"></p>
<p align="center">
Scaling Law as a Pathway towards AGI.
Understanding Scaling Laws for Recommendation Models. Arxiv 2022
</p>

## 1.3 模型作为真实世界的映像

三种类型的真实世界：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/model-as-reflection-of-real-world.png" width="90%" height="90%"></p>

做个表格对比，

| 不同类型的真实世界 | 建模元素 | 对应的模型类型 |
|:-------|:-------|:----------|
| 感知世界（**<mark><code>Perceptual World</code></mark>**） | 视觉（Vision） | <mark>扩散模型</mark>（Diffusion Models, DMs） |
| 认知世界（**<mark><code>Cognitive World</code></mark>**） | 语言（Language） | <mark>大语言模型</mark>（LLMs） |
| 行为世界（**<mark><code>Behavioral World</code></mark>**） | 交互（Interaction） | <mark>用户行为的模型</mark>？ |

* 基于 Vision 和 Language 的模型都有了，并且**<mark>生成式占据主导地位</mark>**，也见证了 scaling law，表现非常好；
* **<mark>基于 Interaction 的模型</mark>**还在探索中，是不是也可以**<mark>套用生成式</mark>**？
  也就是构建**<mark>大型生成式推荐模型</mark>**（large generative recommendation models）。

## 1.4 为什么要做“生成式”推荐？

总结起来有两点，

1. **<mark>更好地 scaling</mark>** 行为；
2. **<mark>与其他模态</mark>** (text, image, audio, …) 的**<mark>对齐更好</mark>**；

### 1.4.1 建模：语言建模 vs. 推荐建模

* 语言建模：根据给定的**<mark>文本</mark>**，预测**<mark>接下来的文本</mark>**；
* 推荐建模：根据**<mark>用户的历史行为</mark>**（购买商品、点击链接、浏览笔记等等），预测用户**<mark>接下来的行为</mark>**（购买、点击等等）；

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/language-model-vs-recommendation.png" width="60%" height="60%"></p>

> 这里的 Item 是推荐系统推荐的东西，可以是一个商品，也可以是一个笔记、视频等等。

### 1.4.2 现状：推荐领域的知识非常稀疏

| 建模类型 | 知识密度 | Token 类型 | Token 空间 |
|:-------|:-------|:---------|:------|
| 语言模型 | 稠密的世界知识（Dense world knowledge） | **<mark>文本</mark>** token | 10^5 |
| 推荐模型 | **<mark>稀疏的“用户-物品”交互数据</mark>**（Sparse user-item interactions） | **<mark>Item</mark>** token | **<mark><code>10^9</code></mark>** |

可以看到，相比于语言建模，推荐领域的知识**<mark>非常稀疏</mark>**，因而 **<mark>scaling laws 在传统推荐模型上几乎没什么效果</mark>**。

### 1.4.3 为什么要 token 化 (“Tokenization”)？

Token 化是为了**<mark>方便计算机处理</mark>**。
具体来说，就是将 **<mark><code>human-readable data</code></mark>** (Text, Image, Action, …)
转换成 **<mark><code>machine-readble formats</code></mark>** (Sequence of Tokens)。

语言模型的 tokenize 和 de-tokenize 过程如下，更多信息可参考 [如何训练一个企业级 GPT 助手（OpenAI，2023）]({% link _posts/2023-09-01-how-to-train-a-gpt-assistant-zh.md %})。

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/language-tokenization.png" width="60%" height="60%"></p>

推荐模型的 tokenization 我们后面介绍。

## 1.5 生成式推荐模型 tokenization 方案举例

几种生成式推荐模型的 tokenization 方案（有点早期了）：

1. SASRec [ICDM'18], Kang and McAuley. Self-Attentive Sequential Recommendation. ICDM 2018

    Each item is indexed by a unique item ID, corresponding to a learnable embedding

2. UniSRec [KDD'22], Hou et al. Towards Universal Sequence Representation Learning for Recommender Systems. KDD 2022

    * Each item is indexed by a unique item ID, corresponding to a fixed representation
    * 中国人民大学 & 阿里

3. LLaRA [SIGIR'24], Liao et al. LLaRA: Large Language-Recommendation Assistant. SIGIR 2024

    * Align item representations with text tokens in LLMs

## 1.6 生成式推荐模型 tokenization 面临的问题

### 1.6.1 问题：Token 空间太大，行为数据太稀疏

和语言模型做个对比，典型模型的 token 数量（vocabulary size）：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/llm-token-space.png" width="50%" height="50%"></p>
<p align="center">
https://amazon-reviews-2023.github.io/
</p>

* 典型的大语言模型只有 **<mark><code>128K~256K</code></mark>** tokens；
* 典型的推荐领域，例如 [amazon-reviews-2023](https://amazon-reviews-2023.github.io/)，
  有 48.2M items，如果一个 item 用一个 token 表示，那就是 **<mark><code>48.2M</code></mark>** tokens；
  **<mark>Token 太多导致数据太稀疏</mark>**，很难有效训练一个大型生成式模型。

### 1.6.2 思路：将行为数据 tokenize 为数据分布

是否可以将人类可读的行为数据**<mark>通过 tokenization 变成一种数据分布</mark>**（跟语言建模类似），
然后**<mark>训练一个生成式模型</mark>**来拟合这个分布？

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/tokenize-action-data-into-distribution.png" width="60%" height="60%"></p>

### 1.6.3 方向：LLM-based GenRec vs. SID-based GenRec

如上图所示，在实际实现上有两个方向：

* Tokenize 为**<mark>文本</mark>**：LLM-based Generative Rec（基于**<mark>大语言模型+文本描述</mark>**的生成式推荐）；
* Tokenize 为 **<mark><code>Semantic IDs</code></mark>**：SemID-based Generative Rec（基于语义 ID 的生成式推荐）。

# 2 方向一：基于语言模型+文本描述的生成式推荐（LLM-based GR）

## 2.1 Tokenization 过程

这类方案的 Tokenization 过程：

* 输入（人类可读数据）：用户行为数据；
* 输出（方便计算机处理的数据）：**<mark>这些行为数据对应的纯文本描述</mark>**；

例如在下图的商品推荐场景，输入是用户购买过的四个商品，token 化之后就是四段分别描述这四个商品的纯文本：
<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/action-tokenization-text-description.png" width="80%" height="80%"></p>

一句话总结优缺点：

* 优点：基于文本的推荐本身就是 LLM 的工作机制，**<mark>底层数据分布与 LLM 是对齐的</mark>**；
* 缺点：**<mark>低效</mark>**（inefficient）。

下面详细看一下这类方案的特点。

## 2.2 基于语言模型的生成式推荐的特点

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/llm-features.png" width="40%" height="40%"></p>

### 2.2.1 丰富的世界知识

大语言本身有丰富的世界知识，例如下图的文本中只是出现了一个单词（token） **<mark><code>Titanic</code></mark>**，
它就已经知道这指代的是一部著名电影了 —— 这部电影的**<mark>知识都已经内化在模型里了</mark>**。

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/llm-rec-world-knowledge.png" width="100%" height="100%"></p>
<p align="center">
Liao et al. LLaRA: Large Language-Recommendation Assistant. SIGIR 2024.
</p>

因此，在基于语言模型+文本描述的生成式推荐中，只需少量数据就能得到一个不错的推荐效果，
Few data -> a good recommender

### 2.2.2 强大的自然语言理解和生成

传统推荐系统主要是利用用户的历史购买记录和用户行为来预测接下来的购买行为：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/traditional-recsys.png" width="80%" height="80%"></p>

LLM-based 生成式推荐，则可以利用 LLM 
强大的自然语言理解和生成能力，通过对话方式叠加购买记录/用户行为，给出推荐：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/conversational-recsys.png" width="90%" height="90%"></p>

### 2.2.3 推理能力/执行复杂任务的能力

很好理解，大模型的强项。

### 2.2.4 如何评估推荐效果

如何验证效果？

* 离线评估：数据丰富，但不够准确；
* 在线评估：准确，但代价比较大。

一种评估方式：LLM as user simulator。

## 2.3 基础：LLM as **<mark><code>Sequential Recommender</code></mark>**

早期尝试：直接用通用的预训练模型做推荐：

* Directly use freezed LLMs (e.g., GPT 4) for recommendation
* **<mark>效果明显不及传统推荐系统</mark>**。

因此后续开始在通用预训练的大语言模型上，通过 Continue Pre-Train (CPT)、SFT、RL 等等，
对齐到推荐任务和用户偏好。

### 2.3.1 将 LLM 对齐到推荐任务

这里介绍两个方案，[P5]({% link _posts/2025-12-20-p5-paper-zh.md %}) 和 InstructRec。

P5 如下图所示，**<mark>5 类推荐任务</mark>**及对应的训练样本，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/p5.png" width="100%" height="100%"></p>
<p align="center"> P5 Multi-task Cross-task generalization.</p>

> P5 paper：[用语言模型做推荐：一种统一的预训练、个性化提示和预测范式]({% link _posts/2025-12-20-p5-paper-zh.md %})

InstructRec 的**<mark>训练样本</mark>**：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/instructRec.png" width="100%" height="100%"></p>
<p align="center">
InstructRec: Unify recommendation & search via instruction tuning.<br />
Zhang et al. Recommendation as Instruction Following: A Large Language Model Empowered Recommendation Approach. TOIS
</p>

### 2.3.2 训练目标（SFT/Preference/RL）

#### SFT

SFT 的训练目标是**<mark>预测下一个 token</mark>**。例如，给定输入：

> I have watched Titanic, Roman Holiday, … Gone with the wind. Predict the next movie I will watch:

期望模型依次预测出 `Waterloo` 和 `Bridge` 这两个 token。

优化的目标：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sft-loss-formula.png" width="50%" height="50%"></p>

#### Preference learning

* 通用语言模型：对齐到**<mark>人类</mark>**偏好；
* 推荐模型：对齐到**<mark>用户</mark>**偏好，实现方式一般训练一个**<mark>奖励模型</mark>**，然后基于奖励模型进行**<mark>强化学习</mark>**；

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/preference-learning.png" width="70%" height="70%"></p>

下面是一个例子，对给定的两个推荐结果做出评价（反馈/奖励），好还是坏，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/preference-learning-2.png" width="70%" height="70%"></p>

Preference learning 典型方案：Chen et al. On Softmax Direct Preference Optimization for Recommendation. NeurIPS 2024

#### RL（强化学习）

这一步是通过强化学习激发出**<mark>推理能力</mark>**，典型方案：

* Lin et al. Rec-R1: Bridging Generative Large Language Models and User-Centric Recommendation Systems via Reinforcement Learning. TMLR
* Tan et al. Reinforced Preference Optimization for Recommendation. arXiv:2510.12211


### 2.3.3 推理算法

* Beam Search
* Constrained Beam Search
* Improved Constrained Beam Search (D3)
* Dense Retrieval Grounding (BIGRec)

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/BIGRec.png" width="100%" height="100%"></p>
<p align="center">
Retrieve real items by generated text.<br />
Bao et al. A Bi-Step Grounding Paradigm for Large Language Models in Recommendation Systems. TORS
</p>

### 2.3.4 小结

1. Early efforts: using LLMs in a zero-shot setting
2. Aligning LLMs for recommendation
3. Training objective: SFT, DPO, RL;
4. Inference: (constrained) beam search, retrieval;

## 2.4 应用一：LLM as **<mark><code>Conversational Recommender</code></mark>**

### 2.4.1 LLM 时代之前的对话式推荐

在非常有限的对话数据集上训练，针对具体任务的对话式推荐引擎，缺点：

* 缺少世界知识；
* 需要复杂的推荐策略；
* 缺少泛化能力。

### 2.4.2 基于 LLM 的对话式推荐

* Recommendations with multiple turns conversation
* Interactive; engaging users in the loop

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/conversational-recommender-system.png" width="80%" height="80%"></p>
<p align="center">
Chen et al. All Roads Lead to Rome: Unveiling the Trajectory of Recommender Systems Across the LLM Era. arXiv.2407.10081
</p>

### 2.4.3 面临的挑战

* 数据集：Public datasets for CRS are limited, due to the scarcity of conversational products and real-world CRS datasets
* 评估方式：Traditional metrics like NDCG and BLEU are often insufficient to assess user experience
* 产品形态：ChatBot? Search bar? Independent App?

## 2.5 应用二：LLM as User Simulator

1. Zhang et al. On generative agents in recommendation. SIGIR 2024
1. Zhang et al. AgentCF: Collaborative Learning with Autonomous Language Agents for Recommender Systems. WWW 2024
1. Wang et al. When Large Language Model based Agent Meets User Behavior Analysis: A Novel User Simulation Paradigm. TOIS 2025.
1. Zhang et al. LLM-Powered User Simulator for Recommender System. AAAI 2025.

## 2.6 小结

1. Tokenize actions by text
    * Pros: distribution naturally aligned with LLMs
    * Cons: inefficient
2. From zero-shot to instruction tuning
    * Training objectives: SFT, DPO, RL, …
    * Inference: constrained beam search, retrieval
3. Applications Conversational RS, User Simulator

基于语言模型+文本描述的生成式推荐，效率低，效果也比较有效，因此需要探索其他方式，
其中比较有希望的一种是**<mark>引入特殊的 token （Semantic IDs）来表征 Item</mark>**。

# 3 Semantic ID 简介

## 3.1 语言模型的 Token 设计

再来回顾下语言模型的 tokenize/de-tokenize 过程：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/language-tokenization.png" width="70%" height="70%"></p>

这里需要注意，一般来说 token 和单词并不是一一对应的，有时候一个 token 只是一个完整单词的一部分，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/language-tokenization-2.png" width="70%" height="70%"></p>

问题：

### 3.1.1 为什么 **<mark><code>token:word ≠ 1:1</code></mark>**

也就是说，为什么不设计成一个单词一个 token？

这会导致 **<mark>vocabulary size 非常大</mark>**，例如每个动词都有好几种时态，每个名词一般单复数都不一样； vocabulary size 过大会**<mark>导致模型不健壮</mark>**；

### 3.1.2  为什么 **<mark><code>token:char ≠ 1:1</code></mark>**

也就是说，为什么不设计成一个字符一个 token？

这会导致**<mark>每个句子的 token 太多（上下文窗口非常长）</mark>**；**<mark>建模困难</mark>**。

## 3.2 推荐模型的 Token 设计

推荐模型的 tokenization 可以有几种不同的方式。

### 3.2.1 方案一：每个商品用一个 token 表示

如下图所示：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/action-tokenization-2.png" width="80%" height="80%"></p>

优点是简单直接，缺点是

1. **<mark>没有商品语义信息</mark>**；
2. 商品类型非常多，导致 **<mark>vocabulary 非常非常大</mark>**，比语言模型的 vocabulary 大几个数量级；

因此实际上基本不可用。

### 3.2.2 方案二：每个商品用一段 text 表示

如下图所示，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/action-tokenization-3.png" width="80%" height="80%"></p>

其中的蓝色长文本分别是图中**<mark>四个商品的文本描述</mark>**：

1. **<mark>短袖</mark>**：Premium Men's Short Sleeve Athletic Training T-Shirt Made of Lightweight Breathable Fabric, Ideal for Running, Gym Workouts, and Casual Sportswear in All Seasons;
1. **<mark>长袜</mark>**：High-Performance Breathable Cotton Crew Socks for Men with Arch Support, Cushioned Heel and Toe, and Moisture Control, Perfect for Sports, Walking, and Everyday Comfort; 
1. **<mark>短裤</mark>**：Men's Loose-Fit Basketball Shorts with Elastic Drawstring Waistband, Quick-Dry Mesh Fabric, and Printed Number 11 for Professional and Recreational Play;
1. **<mark>篮球</mark>**：Official Size 7 Composite Leather Basketball Designed for Indoor and Outdoor Use, Deep Channel Design for Enhanced Grip and Ball Control, Ideal for Training and Competitive Matches;

优点是有商品的语义信息；
缺点是每个商品的 token（文本描述）过长，训练/推理非常低效，另外类似商品的区分度很低，
也导致实际上基本不可用。

### 3.2.3 方案三：结合方案一和方案二的优点 `-> SemanticID`

有没有一种方案能结合前两种方案的优点呢？有，这就是我们接下来要重点介绍的 SemanticID。

#### 用几个 token 联合索引一个商品

下图是一个例子，这里是用**<mark>四个连续 token 索引一个商品</mark>**，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-1.png" width="60%" height="60%"></p>

#### 每个 token 来自不同 vocabulary，表征商品的不同维度

还是上面那个例子，其中的四个 token 分别来自**<mark>四个 vocabulary</mark>**，每个 vocabulary 表征商品的不同维度。
例如第二个 token 来自下图中所示的 vocabulary：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-2.png" width="60%" height="60%"></p>

#### vocabulary size 和支持的商品总数

如果每个 vocabulary **<mark><code>256 tokens</code></mark>**，那

* 用四个 token 索引一个商品时，大致能索引的商品量级为 **<mark><code>256^4≈4.3×10^9</code></mark>**，也就是 **<mark>4.3 亿个商品</mark>**；
* 总的 vocabulary 空间为 256x4=1024 tokens，也就是**<mark>只需要引入 1024 个独立 token</mark>**；

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-3.png" width="70%" height="70%"></p>

### 3.2.4 三种方式对应的 vocabulary 大小对比

下图是三种方式的对比（从左到右依次是方案一、三、二），

* 左边是方案一：每个商品一个 token 表示，因此是 4 个 token；
* 右边是方案二：每个商品一段 text 表示；
* **<mark>中间是方案三</mark>**：每个商品 4 token 表示（SemanticID），因此总共 16 tokens；

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-4.png" width="80%" height="80%"></p>

对应的 vocabulary 大小：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-5.png" width="80%" height="80%"></p>

## 3.3 典型 SemanticID 方案

### 3.3.1 TIGER, NeurIPS 2023

详见 paper：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/tiger-1.png" width="90%" height="90%"></p>
<p align="center">
Rajput et al. Recommender Systems with Generative Retrieval. NeurIPS 2023.
</p>

### 3.3.2 将推荐问题转化成 seq-to-seq 生成问题

将 recommendation 转化成 seq-to-seq 生成问题：

* 输入：用户交互的**<mark>商品序列</mark>**（user interacted items），用 SemanticID 序列表示；
* 输出：**<mark>下一个商品</mark>**，也是用 SemanticID 表示。

# 4 方向二：基于 SemanticID 的生成式推荐

## 4.1 Semantic ID 的构建

### 4.1.1 目标：输入 & 输出

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-construct-1.png" width="70%" height="70%"></p>

* 输入：**<mark>所有关于这个商品的信息</mark>**，包括商品描述、标题、用户行为数据、特征 ...；
* 输出：**<mark>商品和它的 SemanticID 之间的映射关系</mark>**（`items <--> SemanticIDs`）；

### 4.1.2 RQ-VAE-based SemIDs (TIGER as example)

其中一类是称为 RQ-VAE-based SemIDs。代表是 TIGER。

如下图所示，**<mark><code>TIGER</code></mark>** 用到了 **<mark><code>ItemID/Title/Description/Categories/Brand</code></mark>** 作为输入信息：

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/tiger-sid-construct-1.png" width="70%" height="70%"></p>
<p align="center">
Rajput et al. Recommender Systems with Generative Retrieval. NeurIPS 2023.
</p>

构建步骤：

#### 步骤一：商品内容信息（Text）

第一步是以规定的顺序组织商品内容信息，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/tiger-step-1.png" width="60%" height="60%"></p>
<p align="center">
Ni et al. Sentence-T5: Scalable Sentence Encoders from Pre-trained Text-to-Text Models. Findings of ACL 2022.
Rajput et al. Recommender Systems with Generative Retrieval. NeurIPS 2023
</p>

#### 步骤二：商品内容信息向量化（Text -> Vector）

第二步是对内容信息进行编码，这里用了一个 **<mark><code>Encoder</code></mark>**，然后再做 **<mark><code>Embedding</code></mark>**，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/tiger-step-2.png" width="90%" height="90%"></p>
<p align="center">
Ni et al. Sentence-T5: Scalable Sentence Encoders from Pre-trained Text-to-Text Models. Findings of ACL 2022.
Rajput et al. Recommender Systems with Generative Retrieval. NeurIPS 2023
</p>

#### 步骤三：残差量化（Vector -> IDs）

RQ-VAE Quantization **<mark>将向量变成 ID</mark>**，图中的 `7, 1, 4` 就是 SemanticIDs，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/tiger-step-3.png" width="90%" height="90%"></p>
<p align="center">
Zeghidour et al. SoundStream: An End-to-End Neural Audio Codec. TASLP 2022.
Rajput et al. Recommender Systems with Generative Retrieval. NeurIPS 2023.
</p>

### 4.1.3 RQ-VAE-based SemIDs 的特性

1. Semantic

    <p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/rq-vae-property-1.png" width="70%" height="70%"></p>

2. Ordered / sequential dependent

    <p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/rq-vae-property-2.png" width="80%" height="80%"></p>

3. Collisions

    <p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/rq-vae-property-3.png" width="70%" height="70%"></p>

### 4.1.4 RQ-VAE-based SemIDs 存在的问题

* Enc-Dec **<mark><code>Training Unstable</code></mark>**
* **<mark><code>Unbalanced IDs</code></mark>**

因此后面陆续有一些变种，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/variants-of-rq.png" width="80%" height="80%"></p>

这里介绍下快手的 **<mark><code>OneRec</code></mark>**，

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/variants-of-rq-oneRec.png" width="80%" height="80%"></p>
<p align="center">
Deng et al. <mark><code>OneRec</code></mark>: Unifying Retrieve and Rank with Generative Recommender and Iterative Preference Alignment. arXiv:2502.18965
</p>

### 4.1.5 小结

几种构建 SemIDs 的方式：

1. Residual Quantization (ordered)
1. Product Quantization (unordered)
1. Hierarchical Clustering
1. LM-based ID Generator

## 4.2 构建 SemID 时的输入

Input: all data associated with the item
What exactly does “all data” mean? 

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-input-1.png" width="80%" height="80%"></p>

### 4.2.1 商品元数据 (Text / Multimodal / Categorical / No Features)

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-input-2.png" width="80%" height="80%"></p>
Zhu et al. Beyond Unimodal Boundaries: Generative Recommendation with Multimodal Semantics. arXiv:2503.23333.

### 4.2.2 商品元数据 + 用户行为

* Regularization / Fusion
* Context-independent -> Context-aware

相关 papers：

* Wang et al. Content-Based Collaborative Generation for Recommender Systems. CIKM 2024.
* Wang et al. Learnable Item Tokenization for Generative Recommendation. CIKM 2024
* Zhu et al. CoST: Contrastive Quantization based Semantic Tokenization for Generative Recommendation. RecSys 2024.
* Liu et al. End-to-End Learnable Item Tokenization for Generative Recommendation. arXiv:2409.05546.
* Wang et al. EAGER: Two-Stream Generative Recommender with Behavior-Semantic Collaboration. KDD 2024.
* Kim et al. SC-Rec: Enhancing Generative Retrieval with Self-Consistent Reranking for Sequential Recommendation. arXiv:2408:08686.
* Liu et al. MMGRec: Multimodal Generative Recommendation with Transformer Model. arXiv:2404.16555.
* Liu et al. Multi-Behavior Generative Recommendation. CIKM 2024.
* Hou et al. ActionPiece: Contextually Tokenizing Action Sequences for Generative Recommendation. ICML 2025.

### 4.2.3 小结

1. First Example: TIGER
2. Construction Techniques
    * RQ, PQ, Clustering, LM-based generator
3. Inputs
    * Item Metadata (Text, Multimodal)
    * `+` Behaviors (Regularization, Fusion, Context)

## 4.3 基于 SemanticID 的生成式推荐模型架构

### 4.3.1 架构

#### Encoder-decoder

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-based-arch-1.png" width="80%" height="80%"></p>
<p align="center">
Rajput et al. Recommender Systems with Generative Retrieval. NeurIPS 2023
</p>

#### Decoder-only (OneRec)

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-based-arch-2.png" width="70%" height="70%"></p>
<p align="center">
Zhou et al. <mark><code>OneRec-V2</code></mark> Technical Report. arXiv:2508.20900.
</p>

### 4.3.2 目标

#### Next-Token Prediction (w/ RQ)

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-based-arch-3.png" width="80%" height="80%"></p>
<p align="center">
Rajput et al. Recommender Systems with Generative Retrieval. NeurIPS 2023.
</p>

#### Multi-Token Prediction (w/ PQ)

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/sid-based-arch-4.png" width="70%" height="70%"></p>
<p align="center">
Hou et al. Generating Long Semantic IDs in Parallel for Recommendation. KDD 2025.
</p>

### 4.3.3 LLM 对齐

方案有好几种，这里介绍两种。

#### OneRec-Think

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/oneRec-think.png" width="80%" height="80%"></p>
<p align="center">
Liu et al. OneRec-Think: In-Text Reasoning for Generative Recommendation. arXiv:2510.11639
</p>

#### MiniOneRec

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/mini-one-rec.png" width="80%" height="80%"></p>
<p align="center">
Kong et al. MiniOneRec: An Open-Source Framework for Scaling Generative Recommendation. arXiv:2510.24431
</p>

### 4.3.4 推理

How to get a ranking list?

* Constrained Beam Search
* https://en.wikipedia.org/wiki/Beam_search

## 4.4 小结

大致分为两个阶段：

1. 训练**<mark>推荐模型</mark>**
    * Objective (NTP, MTP, RL)
    * Inference (Beam Search)
2. 与**<mark>语言模型</mark>**对齐（Align with LLMs）
    * LC-Rec / OneRec-Think / MiniOneRec

# 5 总结

## 5.1 生成式推荐仍然面临的挑战

相比传统推荐系统，生成式推荐模型仍然面临一些不小的挑战。

### 5.1.1 冷启动推荐

基于 SemanticID 的模型，是否在冷启动上表现很好？

### 5.1.2 推理效率

>  Lin et al. Efficient Inference for Large Language Model-based Generative Recommendation. ICLR 2025

推理算法：

* Retrieval Models: **<mark><code>K Nearest Neighbor Search</code></mark>**
* Generative Models (e.g., AR models): **<mark><code>Beam Search</code></mark>**

如何加速 LLM 推理？**<mark><code>Speculative Decoding</code></mark>**

* Use a “cheap” model to generate candidates
* “Expensive” model can accept or reject (and perform inference if necessary)
* Fast Inference from Transformers via Speculative Decoding. ICML 2023

### 5.1.3 模型更新时效（Timely Model Update）

* Recommendation models favor timely updates
* Delayed updates lead to performance degradation
* How to update large generative rec models timely?  (Frequently retraining large generative models may be resource consuming)
* Lee et al. How Important is Periodic Model Update in Recommender Systems? SIGIR 2023

### 5.1.4 商品 Tokenization 方案

Multiple objectives for optimizing item tokenization.
But none of them is directly related to rec performance:

* reconstruction loss ≠ downstream performance
* How to connect tokenization objective with recommendation performance?
* Zipf’s distribution? Entropy? Linguistic metrics?

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/item-tokenization-evolve.png" width="80%" height="80%"></p>
<p align="center">
Hou et al. ActionPiece: Contextually Tokenizing Action Sequences for Generative Recommendation. arXiv:2502.13581
</p>

## 5.2 生成式推荐带来的新机会

生成式推荐模型将给推荐系统带来哪些新的机会？

### 5.2.1 涌现能力

Abilities not present in smaller models but is present in larger models.

Do we have emergent abilities in large generative recommendation models?

### 5.2.2 Test-time Scaling & Reasoning

There have been explorations on model / data scaling of recommendation models.

Test-time scaling is still under exploration

* https://openai.com/index/learning-to-reason-with-llms

There have been explorations on model / data scaling of
recommendation models Test-time scaling is still under being actively exploration

* Reasoning over latent hidden states to scale up test-time computation.
* Reasoning over text tokens - OneRec-Think

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/oneRec-think-reasoning.png" width="80%" height="80%"></p>
Liu et al. OneRec-Think: In-Text Reasoning for Generative Recommendation. arXiv:2510.11639

### 5.2.3 统一检索+排序

Is it possible to replace traditional cascade architecture with a unified generative model?

<p align="center"><img src="/assets/img/large-generative-recommendation-tokenization-perspective/oneRec-unified-retrieval-and-ranking.png" width="80%" height="80%"></p>
<p align="center">
Deng et al. OneRec: Unifying Retrieve and Rank with Generative Recommender and Iterative Preference Alignment. arXiv:2502.18965
</p>

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
