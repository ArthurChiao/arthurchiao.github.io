---
layout    : post
title     : "[译][论文] DeepSeek-R1：通过强化学习激励大模型的推理能力（DeepSeek，2024）"
date      : 2025-02-15
lastupdate: 2025-02-15
categories: ai llm
---

### 译者序

本文翻译自 2024 年 DeepSeek AI 的 paper [DeepSeek-R1: Incentivizing Reasoning Capability in LLMs via Reinforcement Learning](https://arxiv.org/abs/2501.12948)。
介绍了 DeepSeek 第一代**<mark>推理模型（reasoning models）</mark>**
（所以缩写为 R1）的设计和训练过程：

<p align="center"><img src="/assets/img/deepseek-r1-paper/training-pipelines.png" width="100%" height="100%"></p>
<p align="center">Fig. How <code>DeepSeek-R1</code>-series models were trained.</p>

要理解 DeepSeek-R1 的创新之处，可以先阅读 [如何训练一个企业级 GPT 助手（OpenAI，2023）]({% link _posts/2023-09-01-how-to-train-a-gpt-assistant-zh.md %})，
里面介绍了典型的大模型训练 pipeline，其中包括**<mark>预训练、SFT、RM、RL</mark>**等步骤。

<p align="center"><img src="/assets/img/how-to-train-a-gpt-assistant/training-pipeline.png" width="100%" height="100%"></p>
<p align="center">OpenAI：训练一个 GPT 助手的流程</p>

* **<mark><code>DeepSeek-R1-Zero</code></mark>** 的创新之处在于完全**<mark>跳过了 SFT 步骤</mark>**，
  直接在基座模型上进行大规模 RM+RL 训练，性能达到了 **<mark><code>OpenAI-o1-0912</code></mark>** 的水平。
  [LLaMA 2：开放基础和微调聊天模型（Meta/Facebook，2023）]({% link _posts/2023-08-06-llama2-paper-zh.md %})
  对基于人类反馈的强化学习（HFRL）有较详细的介绍，DeepSeek 这里用的 RL 没有 HF，离 AGI 更进了一步。
* 为了解决 DeepSeek-R1-Zero 存在的一些问题（可读性差，语言混用），又引入了少量的 SFT 数据作为冷启动，
  再参考 R1-Zero 的过程，训练了 **<mark><code>DeepSeek-R1</code></mark>**，
  在推理任务上的表现与 **<mark><code>OpenAI-o1-1217</code></mark>** 不相上下。
* 将 DeepSeek-R1 的推理能力蒸馏到 Qwen/LLaMA 等小型 dense 模型上，性能也很好。

总结下和 OpenAI 的性能对标：

| DeepSeek Models | OpenAI Models |
|:---------|:-------|
| DeepSeek-R1-Zero | OpenAI-o1-0912 |
| DeepSeek-R1 | OpenAI-o1-1217 |
| DeepSeek-R1 Distilled Models | OpenAI-o1-mini |

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

本文介绍我们的第一代推理模型，**<mark><code>DeepSeek-R1-Zero</code></mark>** 和 **<mark><code>DeepSeek-R1</code></mark>**。

* DeepSeek-R1-Zero

    * 这是一个**<mark>跳过监督微调</mark>**（SFT）步骤，
      直接通过大规模**<mark>强化学习</mark>**（RL）训练得到的模型，具备卓越的推理能力。

        > 译注：下图来自 [如何训练一个企业级 GPT 助手（OpenAI，2023）]({% link _posts/2023-09-01-how-to-train-a-gpt-assistant-zh.md %})，
        > 展示了 OpenAI 从预训练开始逐步训练出一个 GPT 助手的步骤，
        > **<mark><code>pre-training -> SFT -> RM -> RL</code></mark>** 也是典型的大模型训练过程。
        > R1-Zero 是在 DeepSeek-V3 基座大模型上直接进行 RM+RL，跳过中间的 SFT，
        >
        > <p align="center"><img src="/assets/img/how-to-train-a-gpt-assistant/training-pipeline.png" width="100%" height="100%"></p>
        > <p align="center">OpenAI：训练一个 GPT 助手的流程</p>

    * 通过大规模 RL，DeepSeek-R1-Zero 自然地涌现出许多强大且有趣的推理行为。不过，它也存在可读性差、混用语言等问题。

* DeepSeek-R1

    * 为了解决以上提到的 R1-Zero 存在的问题，并进一步提升推理性能，
      在 RL 阶段之前引入了多阶段训练和冷启动数据，训练得到的模型称为 DeepSeek-R1。
    * DeepSeek-R1 在推理任务上的表现与 **<mark><code>OpenAI-o1-1217</code></mark>** 不相上下。

        <p align="center"><img src="/assets/img/deepseek-r1-paper/1.png" width="80%" height="80%"></p>
        <p align="center"> Figure 1 | Benchmark performance of DeepSeek-R1.  </p>

为了支持研究社区，我们此次开源了 [8 个推理模型](https://huggingface.co/collections/deepseek-ai/deepseek-r1-678e1e131c0169c0bc89728d)：

1. DeepSeek-R1
1. DeepSeek-R1-Zero
1. DeepSeek-R1-**<mark><code>Distill-Llama-70B</code></mark>**
1. DeepSeek-R1-**<mark><code>Distill-Qwen-32B</code></mark>**
1. DeepSeek-R1-**<mark><code>Distill-Qwen-14B</code></mark>**
1. DeepSeek-R1-**<mark><code>Distill-Llama-8B</code></mark>**
1. DeepSeek-R1-**<mark><code>Distill-Qwen-7B</code></mark>**
1. DeepSeek-R1-**<mark><code>Distill-Qwen-1.5B</code></mark>**

其中，**<mark>后面 6 个是以 Qwen/Llama 作为基座模型</mark>**，利用 DeepSeek-R1
**<mark>蒸馏出来的 dense 模型</mark>**。


# 1 引言

近年来，大模型的迭代与演进速度非常快（OpenAI, 2024a；Anthropic, 2024；Google, 2024）。

## 1.0 Post-Training：完整 training pipeline 的重要组成部分

现在，**<mark><code>post-training</code></mark>** 已成为完整 training pipeline 的一个重要组成部分。

### 1.0.1 作用

Post-Training 的好处：

1. **<mark>提高推理任务的准确性</mark>**，
2. 与人类社会价值观对齐，
3. 能适应用户偏好，
4. 相对于预训练，所需的计算资源极少。

### 1.0.2 提高推理能力：与 OpenAI-o1 的思路区别

具体到提高推理能力方面，

* OpenAI 的 o1（OpenAI, 2024b）系列模型首次通过**<mark>增加推理过程中的思维链长度</mark>**（Chain-of-Thought, CoT）
  来引入 **<mark><code>inference-time scaling</code></mark>**。
  这种方法在数学、编码和科学推理等推理任务上取得了显著的效果。
* 但是，有效的 **<mark><code>test-time scaling</code></mark>** 仍然是社区的一个开放性问题。
  此前，业界已经探索了很多方法，包括
  process-based reward models (Uesato et al., 2022; Lightman et al., 2023; Wang
  et al., 2023), reinforcement learning (Kumar et al., 2024), and search
  algorithms such as Monte Carlo Tree Search and Beam Search (Feng et al., 2024;
  Xin et al., 2024; Trinh et al., 2024)，但这些方法**<mark>都没有达到与 OpenAI o1 相当的通用推理性能</mark>**。

本文迈出了**<mark>通过纯强化学习（pure RL）提高模型推理能力</mark>**的第一步。

* 我们的目标是探索**<mark>大模型在没有任何监督数据的情况下 —— 单纯通过 RL 过程自我进化 —— 发展出推理能力的潜力</mark>**。
* 具体来说，我们使用 **<mark><code>DeepSeek-V3-Base</code></mark>** 作为基础模型，采用 GRPO（Shao 等，2024）作为 RL 框架，来**<mark>提高模型在推理方面的表现</mark>**。
* 在训练过程中，DeepSeek-R1-Zero 自然地涌现出许多强大且有趣的推理行为。经过几千步的 RL 训练后，
  DeepSeek-R1-Zero 在推理基准测试中表现出色。例如，AIME 2024 的 pass@1 得分从 15.6% 提高到 71.0%，加上多数投票，得分进一步提高到 86.7%，与 OpenAI-o1-0912 表现相当。

然而，DeepSeek-R1-Zero 面临着诸如可读性差、语言混用等挑战。为了解决这些问题并进一步提升推理性能，
我们引入了少量的冷启动数据和一个 multi-stage training pipeline，训练得到 DeepSeek-R1，
其性能与 OpenAI-o1-1217 相当。

最后，我们还进一步探索了从 DeepSeek-R1 蒸馏较小的 dense models。
例如，使用 Qwen2.5-32B（Qwen, 2024b）作为基础模型，两种思路：

1. 直接在 Qwen-32B 上进行强化学习（RL），得到一个推理模型；
1. 从 DeepSeek-R1 进行蒸馏（把 DeepSeek-R1 的知识“传授”给 Qwen2.5-32B），得到一个推理模型；

我们发现后者（蒸馏）的性能优于前者（直接 RL）。
这表明**<mark>尺寸更大的基础模型发现的推理模式</mark>**对于提高推理能力至关重要。

我们开源了基于 Qwen/Llama（Dubey 等，2024）的蒸馏模型。
值得注意的是，我们蒸馏出的 14B 模型在 AIME 2024 上的表现大幅超过了现有的开源模型 QwQ-32B-Preview（Qwen, 2024a），
而蒸馏出的 32B 和 70B 模型在针对 dense models 的推理基准测试中创下了新纪录。

## 1.1 贡献

### 1.1.1 post-training：在基础模型上进行大规模强化学习

我们跳过监督微调（SFT）步骤，直接在基础模型（base model）上应用 RL。
这会使模型去**<mark>探索解决复杂问题时的思维链</mark>**（CoT），用这种方式训练得到的就是 DeepSeek-R1-Zero。

* DeepSeek-R1-Zero 展现出**<mark>自我验证、反思和生成长 CoT</mark>** 等能力，为社区研究树立了一个重要的里程碑。
* 值得注意的是，这是首个证实**<mark>大模型的推理能力可以通过纯 RL 激励实现（无需 SFT）</mark>**的公开研究，这一突破为该领域的未来发展铺平了道路。

此外，我们还介绍了开发 DeepSeek-R1 的 pipeline。

<p align="center"><img src="/assets/img/deepseek-r1-paper/training-pipelines.png" width="100%" height="100%"></p>
<p align="center">Fig. How DeepSeek-R1-Zero and DeepSeek-R1 were trained (based on the same base model).</p>

该 pipeline 包含，

1. **<mark>两个 RL stage</mark>**
   * 一个用于发现更强的推理模式（stage 2）
   * 一个用于与人类偏好对齐（stage 4）
2. **<mark>两个 SFT stage</mark>**：用于激发出模型的 reasoning and non-reasoning 能力。

### 1.1.2 蒸馏：小型模型也可以很强大

我们证明了**<mark>大型模型的推理模式可以被蒸馏到小型模型中</mark>**，

* 与在小型模型上进行 RL 发现的推理模式相比，蒸馏可以取得更好的性能。
* 开源的 DeepSeek-R1 及其 API 将有助于社区在未来蒸馏出更好的小模型。

**<mark>利用 DeepSeek-R1 生成的推理数据</mark>**，我们微调了几个在社区中广泛使用的小型 dense 模型。
结果显示，这些经过蒸馏的小型 dense model 在基准测试中表现非常好。

* DeepSeek-R1-Distill-Qwen-7B achieves 55.5% on AIME 2024, surpassing QwQ-32B-Preview.
* DeepSeek-R1-Distill-Qwen-32B scores 72.6% on AIME 2024, 94.3% on MATH-500, and 57.2% on LiveCodeBench.
* These results significantly outperform previous open-source models and are comparable to **<mark><code>o1-mini</code></mark>**.

## 1.2 性能评估结果

### 1.2.1 推理任务

1. DeepSeek-R1 achieves a score of 79.8% Pass@1 on AIME 2024, slightly
   surpassing OpenAI-o1-1217. On MATH-500, it attains an impressive score of
   97.3%, performing on par with OpenAI-o1-1217 and significantly outperforming
   other models.
2. On coding-related tasks, DeepSeek-R1 demonstrates expert level in code
   competition tasks, as it achieves 2,029 Elo rating on Codeforces
   outperforming 96.3% human participants in the competition. For
   engineering-related tasks, DeepSeek-R1 performs slightly better than
   DeepSeek-V3, which could help developers in real world tasks.

### 1.2.2 知识

On benchmarks such as MMLU, MMLU-Pro, and GPQA Diamond, DeepSeek-R1 achieves outstanding results, significantly outperforming DeepSeek-V3 with scores of 90.8% on MMLU, 84.0% on MMLU-Pro, and 71.5% on GPQA Diamond. While its performance is slightly below that of OpenAI-o1-1217 on these benchmarks, DeepSeek-R1 surpasses other closed-source models, demonstrating its competitive edge in educational tasks. On the factual benchmark SimpleQA, DeepSeek-R1 outperforms DeepSeek-V3, demonstrating its capability in handling fact-based queries. A similar trend is observed where OpenAI-o1 surpasses 4o on this benchmark.

### 1.2.3 其他

DeepSeek-R1 also excels in a wide range of tasks, including creative writing, general question answering, editing, summarization, and more. It achieves an impressive length-controlled win-rate of 87.6% on AlpacaEval 2.0 and a win-rate of 92.3% on ArenaHard, showcasing its strong ability to intelligently handle non-exam-oriented queries. Additionally, DeepSeek-R1 demonstrates outstanding performance on tasks requiring long-context understanding, substantially outperforming DeepSeek-V3 on long-context benchmarks.

# 2 方法

## 2.1 概述

以往的研究重度依赖于**<mark>大量的监督数据（人类标注数据）</mark>**来提升模型性能。
本文的研究证明：

1. **<mark>不使用监督微调（SFT），单纯通过大规模强化学习（RL）</mark>**也能显著提升**<mark>推理能力</mark>**。
2. 通过引入少量冷启动数据（SFT 训练数据），还可以**<mark>进一步增强性能</mark>**。

## 2.2 DeepSeek-R1-Zero：在基础模型（base model）上进行强化学习

之前的研究（Wang 等，2023；Shao 等，2024）已经证明，**<mark>强化学习对提高推理性能非常有用</mark>**。
但是，这些前期研究都**<mark>重度依赖监督数据</mark>**，而收集监督数据是个费事费力的过程。

本节探索在没有任何监督数据的情况下（单纯通过 RL 过程自我进化），大模型发展出推理能力的过程。

### 2.2.1 强化学习算法：Group Relative Policy Optimization (**<mark><code>GRPO</code></mark>**)

为了降低 RL 训练成本，我们采用了 **<mark><code>GRPO</code></mark>**（组相对策略优化）算法（Shao 等，2024），
该方法放弃了 critic model（通常尺寸与 policy model 大小相同），而是用 group scores 来估计基线。

具体来说，对于每个问题 $q$, GRPO 从老的 policy $\pi_{\theta_{old}}$
中采样得到一组输出 $\{o_1, o_2, \cdots, o_G\}$，
然后用下面的目标函数优化 policy model $\pi_{\theta}$：

<p align="center"><img src="/assets/img/deepseek-r1-paper/equation-1.png" width="100%" height="100%"></p>

### 2.2.2 奖励建模（Reward Modeling）：**<mark><code>rule-based reward system</code></mark>**

奖励是 training signal 的来源，它**<mark>决定了强化学习的优化方向</mark>**。
训练 DeepSeek-R1-Zero 时，我们采用了一个**<mark>基于规则的奖励系统</mark>**（rule-based reward system），
该系统主要由两种类型的奖励组成。

#### 类型一：准确性奖励（Accuracy rewards）

准确性奖励模型**<mark>评估响应是否正确</mark>**（whether the response is correct）。例如，

1. 对于具有确定性结果的**<mark>数学问题</mark>**，要求模型以**<mark>指定格式</mark>**提供最终答案，从而能可靠地基于规则验证正确性。
1. 对于 **<mark><code>LeetCode</code></mark>** 问题，可以**<mark>使用编译器</mark>**对生成的程序进行编译，然后运行预定义的测试用例。

#### 类型二：格式奖励（Format rewards）

我们还采用了一个格式奖励模型，**<mark>强制推理模型将其思考过程放在</mark>**
`<think>` 和 `</think>` tag 内。

这里没有使用结果或过程神经奖励模型（**<mark><code>outcome or process neural reward model</code></mark>**），
因为我们发现神经奖励模型可能会在大规模强化学习过程中出现 reward hacking 行为，
并且重新训练奖励模型需要额外的训练资源，也会使整个训练流程变得更加复杂。

### 2.2.3 训练模板（提示词模板）

我们设计了一个简单直白的模板，指导基础模型遵循我们的具体指令。如表 1 所示，

```
A conversation between User and Assistant. The user asks a question, and the Assistant solves it.
The assistant first thinks about the reasoning process in the mind and then provides the user
with the answer. The reasoning process and answer are enclosed within <think> </think> and
<answer> </answer> tags, respectively, i.e., <think> reasoning process here </think>
<answer> answer here </answer>. User: prompt. Assistant:

表 1：DeepSeek-R1-Zero 的模板。在训练期间，将用具体的推理问题替换提示。
```

可以看到，这个模板要求 DeepSeek-R1-Zero **<mark>首先生产一个推理过程，然后再给出最终答案</mark>**。
我们有意将约束限制在这一结构内，避免任何 content-specific biases ——
例如，mandating reflective reasoning or promoting particular problem-solving strategies ——
以确保我们能够**<mark>准确地观察模型在 RL 过程中的自然进化</mark>**。

### 2.2.4 DeepSeek-R1-Zero 的性能、自我进化过程和顿悟时刻

#### 性能

下图展示了 DeepSeek-R1-Zero 在 AIME 2024 基准测试中的性能轨迹，

<p align="center"><img src="/assets/img/deepseek-r1-paper/plot_aime_with_maj.png" width="60%" height="60%"></p>
<p align="center">Figure 2:AIME accuracy of DeepSeek-R1-Zero during training. For each question, we sample 16 responses and calculate the overall average accuracy to ensure a stable evaluation.</p>

可以看到，随着 RL 训练的进行，DeepSeek-R1-Zero 的性能稳步提升。
AIME 2024 pass@1 得分从 15.6% 跃升至 71.0%，达到了与 OpenAI-o1-0912 相当的性能水平，
说明了我们的 RL 算法在优化模型性能方面的有效性。

表 2 是 DeepSeek-R1-Zero 与 OpenAI o1-0912 在多种推理基准测试上的性能对比，

<p align="center"><img src="/assets/img/deepseek-r1-paper/table-2.png" width="70%" height="70%"></p>
<p align="center">表 2：DeepSeek-R1-Zero 与 OpenAI o1 在推理相关基准测试上的性能对比。</p>

几点结论，

* 通过 RL，DeepSeek-R1-Zero 能够在**<mark>无需任何监督微调数据的情况下获得强大的推理能力</mark>**，
  也就是说模型**<mark>仅通过 RL 就能有效学习和泛化</mark>**。
* DeepSeek-R1-Zero 的性能还可以通过**<mark>多数投票</mark>**（majority voting）进一步提升。
  例如，在 AIME 基准测试中采用多数投票时，DeepSeek-R1-Zero 的性能从 71.0% 上升至 86.7%，超过了 OpenAI-o1-0912 的性能。
* DeepSeek-R1-Zero 在有无多数投票的情况下都能取得如此高的性能，突显了其**<mark>强大的基础能力以及在推理任务中进一步发展的潜力</mark>**。

#### 自我进化过程

DeepSeek-R1-Zero 的自我进化过程非常好地展示了**<mark>强化学习是如何驱动模型自主提升推理能力的</mark>**。

* 直接从基础模型启动 RL 训练，使得我们免受监督微调（SFT）阶段的影响，从而能直观监测模型的进化过程。
* 这种方法为我们提供了一个观察模型随时间演变的清晰视角，特别是在处理复杂推理任务方面。

<p align="center"><img src="/assets/img/deepseek-r1-paper/plot_length.png" width="70%" height="70%"></p>
<p align="center"> Figure 3:The average response length of DeepSeek-R1-Zero on the training set during the RL process. DeepSeek-R1-Zero naturally learns to solve reasoning tasks with more thinking time.  </p>

如图 3 所示，DeepSeek-R1-Zero 的**<mark>思考时间</mark>**在整个训练过程中呈现出持续改进（增加）的趋势。

* 这种进步并非外部调整的结果，而是模型内部的自然发展。
* DeepSeek-R1-Zero 自然获得了**<mark>通过增加 test-time computation 来解决越来越复杂的推理任务</mark>**的能力。
* 这里所说的 computation 是指生成几百到几千个不等的**<mark>推理 token</mark>**，使模型能够**<mark>更深入地探索和完善其思考过程</mark>**。

随着 test-time computation 的增加，这种自我进化过程中最显著的方面之一是**<mark>出现了复杂行为</mark>**。
例如，观察到下面两个行为同时**<mark>自发出现</mark>**了，

* **<mark>反思行为</mark>**：模型重新审视和评估自己先前的步骤
* 模型**<mark>主动探索解决问题的替代方法</mark>**

这些行为并非明确编程的结果，而是**<mark>模型与强化学习环境互动的结果</mark>**。
这种自发的发展显著增强了 DeepSeek-R1-Zero 的推理能力，使其能够以更高的效率和准确性应对更具挑战性的任务。

#### 顿悟时刻

在 DeepSeek-R1-Zero 的训练过程中，观察到的一个奇特现象是所谓的 “顿悟时刻”。如表 3 所示，

<p align="center"><img src="/assets/img/deepseek-r1-paper/table-3.png" width="70%" height="70%"></p>
<p align="center">
Table 3:An interesting “aha moment” of an intermediate version of DeepSeek-R1-Zero. The model learns to rethink using an anthropomorphic tone. This is also an aha moment for us, allowing us to witness the power and beauty of reinforcement learning.
</p>

这一时刻出现在模型的一个中间版本中。在这个阶段，DeepSeek-R1-Zero 学会了通过重新评估其初始处理方法，为问题分配更多的思考时间。
这种行为不仅是模型逐步增长的推理能力的证明，也是强化学习能够带来意外且复杂结果的一个迷人例证。

这对于模型和观察其行为的研究者来说都是一个 “顿悟时刻”，它凸显了**<mark>强化学习的力量和美感</mark>**：

* 我们并没有明确地教导模型如何解决问题，而是仅仅提供了正确的激励，模型便能够自主地发展出高级的问题解决策略。
* “顿悟时刻” 有力地提醒了我们 RL 激发人工智能系统新智能水平的潜力，为未来更具自主性和适应性的模型铺平了道路。

#### 缺点和解决方式

尽管 DeepSeek-R1-Zero 展示了强大的推理能力，并且能够自主发展出意外且强大的推理行为，但它也面临一些问题。例如，DeepSeek-R1-Zero 遇到了诸如可读性差、语言混用等挑战。
为了使推理过程更具可读性，我们探索了 DeepSeek-R1。

## 2.3 DeepSeek-R1：带冷启动的强化学习

DeepSeek-R1-Zero 的结果令人鼓舞，关于如何进一步提升性能，自然会产生两个问题：

1. 引入少量高质量数据作为冷启动，是否可以**<mark>进一步提升推理性能</mark>**或加速收敛？
2. 如何训练一个用户友好的模型，该模型不仅能够产生清晰连贯的思维链（CoT），而且**<mark>还能展现出强大的通用能力</mark>**？

为了回答这些问题，我们设计了一个新的 pipeline，训练得到的模型称为 **<mark><code>DeepSeek-R1</code></mark>**。

该 pipeline 包含四个阶段。

### 2.3.1 阶段一：冷启动

为了避免从基础模型直接开始 RL 训练导致的**<mark>不稳定冷启动</mark>**阶段，
我们构建了**<mark>一定量的长 CoT 数据集</mark>**并**<mark>对模型进行微调（SFT）</mark>**，
得到一个 initial RL actor。

#### 数据源

几种方式：

1. 提供一个 CoT 作为示例，然后使用 **<mark><code>few-shot prompting</code></mark>** 生成更多例子；
2. **<mark>直接提示模型</mark>**（directly prompting models），让它生成**<mark>带有反思和验证过程的详细回答</mark>**；
3. **<mark>收集 DeepSeek-R1-Zero 输出的一些回答</mark>**，并通过**<mark>人工标注</mark>**对输出的质量进行增强。

我们收集了几千个冷启动数据，拿来微调 DeepSeek-V3-Base，得到的模型作为接下来的 RL 过程的起点。

#### 冷启动数据的好处

冷启动数据的好处包括：

1. 提升输出的可读性

    DeepSeek-R1-Zero 的主要问题之一是输出的内容经常可读性很差。可能会混杂多种语言，或者不是 markdown 格式，无法高亮一些重点。

    因此，在为 DeepSeek-R1 创建冷启动数据时，我们设计了一种可读性很好的格式，在每个响应的末尾包含一个总结，并过滤出对读者不友好的响应。
    在这里，我们定义输出格式为
    `|special_token|<reasoning_process>|special_token|<summary>`，其中 `<reasoning_process>` 是用户输入的 query 对应的 CoT（推理过程），而 `<summary>` 用于总结推理结果。

2. 潜力

    * 基于人的先验知识（human priors）精心设计冷启动数据，观察到训练出来的模型比 DeepSeek-R1-Zero 表现更好。
    * 我们相信**<mark>迭代式训练</mark>**（iterative training）是很好的训练**<mark>推理模型</mark>**的方式。

### 2.3.2 阶段二：面向 reasoning 的强化学习

在使用冷启动数据对 DeepSeek-V3-Base 进行微调后，第二阶段的训练过程**<mark>与 DeepSeek-R1-Zero 相同</mark>**：
使用大规模强化学习进行后训练。
这一阶段**<mark>专注于提升模型的 reasoning 能力</mark>**，特别是在推理密集型任务中，如编码、数学、科学和逻辑推理，这些任务具有明确定义的问题和解决方案。

在训练过程中，我们观察到 CoT 经常出现语言混用（language mixing），特别是在 RL 提示词涉及多种语言时。
为了缓解这个问题，我们在 RL 训练中引入了一种语言一致性奖励（language consistency reward），计算方式是 CoT 中目标语言单词的比例（proportion of target language words in the CoT）。
尽管消融实验表明，这种对齐会导致模型性能略有下降，但这种奖励与人类偏好一致，使其更具可读性。

最后，我们直接将推理任务的准确性与语言一致性奖励相加来形成最终奖励。
然后，我们在微调后的模型上应用 RL 训练，直到它在推理任务上收敛。

这个阶段的 RL 收敛时，保存一个 checkpoint 供第三阶段使用。

### 2.3.3 阶段三：拒绝采样和监督微调

> **<mark><code>Rejection sampling</code></mark>** is a technique where the LLM
> generates multiple candidate answers and then filters out those that do not
> meet certain criteria, retaining only the "good" results。It is used to
> enhance the quality and reliability of the model's outputs, making them more
> aligned with desired standards or distributions
>
> 更多信息，可参考 [LLaMA 2：开放基础和微调聊天模型（Meta/Facebook，2023）]({% link _posts/2023-08-06-llama2-paper-zh.md %})，里面对 rejection sampling 有较详细的介绍。
>
> 译注。

**<mark>利用第二阶段的 checkpoint 收集 SFT（监督微调）数据</mark>**。

初始冷启动数据主要关注推理，而这一阶段则纳入了来自**<mark>其他领域的数据</mark>**，
以增强模型在写作、角色扮演和其他通用任务中的能力。
具体来说，我们按照以下方式生成数据并微调模型。

#### 推理数据（Reasoning data）：600k

人工整理一批推理提示词，从上述 RL 训练的 checkpoint 进行拒绝采样来生成推理轨迹。

在第二阶段，我们只纳入了可以使用**<mark>基于规则的奖励</mark>**进行评估的数据。

在这一阶段，

* 引入额外数据来扩展数据集，其中一些数据使用生成式奖励模型 —— 将事实和模型预测输入 DeepSeek-V3 进行判断。
* 由于模型输出有时会杂乱无章且难以阅读，我们会过滤掉带有混合语言、冗长段落和代码块的思维链。
* 对于每个提示，我们采样多个响应，并且只保留正确的响应。

总共，我们收集了大约 **<mark><code>600k</code></mark>** 个与推理相关的训练样本。

#### 非推理数据（Non-Reasoning data）：200k

对于非推理数据，如写作、事实问答、自我认知和翻译，我们采用 DeepSeek-V3 pipeline，
并复用 DeepSeek-V3 的一部分 SFT 数据集。

* 对于某些非推理任务，我们调用 DeepSeek-V3 来生成一个潜在的思维链，然后通过提示回答问题。
* 对于更简单的查询，如 “hello”，我们不会在响应中提供 CoT。

最终，我们收集了总共大约 **<mark><code>200k</code></mark>** 个与推理无关的训练样本。

我们使用上述整理的数据集（约 800k 样本）对 DeepSeek-V3-Base 进行了**<mark>两个 epoch 的微调</mark>**。

### 2.3.4 阶段四：所有场景的强化学习

为了进一步使模型与人类偏好对齐，我们又进行了一轮强化学习，在完善模型推理能力的同时，
提高模型的有用性和无害性（**<mark><code>helpfulness and harmlessness</code></mark>**）。

<p align="center"><img src="/assets/img/deepseek-r1-paper/training-pipelines.png" width="100%" height="100%"></p>
<p align="center">Fig. How DeepSeek-R1-Zero and DeepSeek-R1 were trained (based on the same base model).</p>

具体来说，我们组合使用 reward signals 和多样化的 prompt distributions 来训练模型。

* 对于推理数据，遵循 DeepSeek-R1-Zero 中的方法，利用基于规则的奖励来指导数学、编码和逻辑推理领域的学习过程。
* 对于通用数据，借助奖励模型，以捕捉复杂微妙场景中的人类偏好。我们基于 DeepSeek-V3 pipeline，并采用类似的偏好对和训练提示分布。
* 对于有用性，仅关注最终总结，确保评估强调响应对用户的实用性和相关性，同时尽量减少对底层推理过程的干扰。
* 对于无害性，评估模型的整个响应，包括推理过程和总结，以识别和减轻在生成过程中可能出现的任何潜在风险、偏见或有害内容。

这些方式组合起来，最终使我们训练出一个在推理方面表现出色、同时还会优先考虑有用性和无害性的模型。

## 2.4 蒸馏：赋予小型模型推理能力

为了使小型模型具备类似 DeepSeek-R1 的推理能力，
我们**<mark>直接用 DeepSeek-R1 生成的 800k 样本对开源模型进行微调</mark>**。

我们的研究发现，这种**<mark>直接蒸馏的方法能显著提升小型模型的推理能力</mark>**。
我们使用的基础模型包括：

1. Qwen2.5-Math-1.5B
2. Qwen2.5-Math-7B
3. Qwen2.5-14B
1. Qwen2.5-32B
3. Llama-3.1-8B
1. Llama-3.3-70B-Instruct。选择 Llama-3.3 是因为其推理能力略优于 Llama-3.1。

蒸馏过程：在以上**<mark>基础模型上进行监督微调（SFT）</mark>**，

* 这里不再进行强化学习（RL），尽管叠加 RL 可能会进一步提升模型性能。
* 我们的主要目的是**<mark>展示蒸馏技术的有效性</mark>**，叠加 RL 阶段的探索就留给更社区研究。

# 3 实验（略）

## 3.1 DeepSeek-R1 评估

## 3.2 蒸馏模型评估

# 4 讨论

## 4.1 蒸馏与强化学习的性能对比

前面已经看到，通过蒸馏 DeepSeek-R1，小型模型可以取得非常好的效果。
但这里还有一个问题待解答：通过本文讨论的**<mark>大规模 RL 对小模型训练，和蒸馏方式相比，哪个效果来的更好？</mark>**

为了回答这个问题，我们在 Qwen-32B-Base 上进行了大规模 RL 训练，使用数学、编码和 STEM 数据，训练了超过 10K 步，
得到了 **<mark><code>DeepSeek-R1-Zero-Qwen-32B</code></mark>**。
两种方式得到的模型，性能对比如下，

<p align="center"><img src="/assets/img/deepseek-r1-paper/table-6.png" width="80%" height="80%"></p>
<p align="center"> Table 6:Comparison of distilled and RL Models on Reasoning-Related Benchmarks.  </p>

* 大规模 RL 训练的 32B 基础模型，在性能上与 QwQ-32B-Preview 相当。
* 从 DeepSeek-R1 蒸馏而来的模型，在所有基准测试中都显著优于 DeepSeek-R1-Zero-Qwen-32B。

因此，我们可以得出两个结论：

1. **<mark>将更强大的模型蒸馏到小型模型中，可以让小模型获得出色的性能</mark>**。
  对小型模型进行大规模 RL 也能取得不错的性能，但需要的算力比蒸馏要多很多，而且可能无法达到蒸馏取得的效果。
2. 蒸馏是一种既经济又高效的方式，但要突破智能边界，可能仍需要**<mark>更强大的基础模型和更大规模的强化学习</mark>**。

## 4.2 失败的尝试

在开发 DeepSeek-R1 早期，我们也遇到了一些失败和挫折。
这里分享一些失败经验，提供一些见解，但这并不意味着这些方法无法开发出有效的推理模型。

### 4.2.1 Process Reward Model (PRM)

PRM is a reasonable method to guide the model toward better approaches for solving reasoning tasks (Uesato et al., 2022; Lightman et al., 2023; Wang et al., 2023). However, in practice, PRM has three main limitations that may hinder its ultimate success. First, it is challenging to explicitly define a fine-grain step in general reasoning. Second, determining whether the current intermediate step is correct is a challenging task. Automated annotation using models may not yield satisfactory results, while manual annotation is not conducive to scaling up. Third, once a model-based PRM is introduced, it inevitably leads to reward hacking (Gao et al., 2022), and retraining the reward model needs additional training resources and it complicates the whole training pipeline. In conclusion, while PRM demonstrates a good ability to rerank the top-N responses generated by the model or assist in guided search (Snell et al., 2024), its advantages are limited compared to the additional computational overhead it introduces during the large-scale reinforcement learning process in our experiments.

### 4.2.2 Monte Carlo Tree Search (MCTS)

Inspired by AlphaGo (Silver et al., 2017b) and AlphaZero (Silver et al., 2017a), we explored using Monte Carlo Tree Search (MCTS) to enhance test-time compute scalability. This approach involves breaking answers into smaller parts to allow the model to explore the solution space systematically. To facilitate this, we prompt the model to generate multiple tags that correspond to specific reasoning steps necessary for the search. For training, we first use collected prompts to find answers via MCTS guided by a pre-trained value model. Subsequently, we use the resulting question-answer pairs to train both the actor model and the value model, iteratively refining the process.

However, this approach encounters several challenges when scaling up the training. First, unlike chess, where the search space is relatively well-defined, token generation presents an exponentially larger search space. To address this, we set a maximum extension limit for each node, but this can lead to the model getting stuck in local optima. Second, the value model directly influences the quality of generation since it guides each step of the search process. Training a fine-grained value model is inherently difficult, which makes it challenging for the model to iteratively improve. While AlphaGo’s core success relied on training a value model to progressively enhance its performance, this principle proves difficult to replicate in our setup due to the complexities of token generation.

In conclusion, while MCTS can improve performance during inference when paired with a pre-trained value model, iteratively boosting model performance through self-search remains a significant challenge.

# 5 结论、局限性和未来工作

In this work, we share our journey in enhancing model reasoning abilities
through reinforcement learning. DeepSeek-R1-Zero represents a pure RL approach
without relying on cold-start data, achieving strong performance across various
tasks. DeepSeek-R1 is more powerful, leveraging cold-start data alongside
iterative RL fine-tuning. Ultimately, DeepSeek-R1 achieves performance
comparable to OpenAI-o1-1217 on a range of tasks.

We further explore distillation the reasoning capability to small dense models.
We use DeepSeek-R1 as the teacher model to generate 800K training samples, and
fine-tune several small dense models. The results are promising:
DeepSeek-R1-Distill-Qwen-1.5B outperforms GPT-4o and Claude-3.5-Sonnet on math
benchmarks with 28.9% on AIME and 83.9% on MATH. Other dense models also
achieve impressive results, significantly outperforming other instruction-tuned
models based on the same underlying checkpoints.

In the future, we plan to invest in research across the following directions for DeepSeek-R1.

* General Capability: Currently, the capabilities of DeepSeek-R1 fall short of
  DeepSeek-V3 in tasks such as function calling, multi-turn, complex
  role-playing, and JSON output. Moving forward, we plan to explore how long
  CoT can be leveraged to enhance tasks in these fields.
* Language Mixing: DeepSeek-R1 is currently optimized for Chinese and English,
  which may result in language mixing issues when handling queries in other
  languages. For instance, DeepSeek-R1 might use English for reasoning and
  responses, even if the query is in a language other than English or Chinese.
  We aim to address this limitation in future updates.
* Prompting Engineering: When evaluating DeepSeek-R1, we observe that it is
  sensitive to prompts. Few-shot prompting consistently degrades its
  performance. Therefore, we recommend users directly describe the problem and
  specify the output format using a zero-shot setting for optimal results.
* Software Engineering Tasks: Due to the long evaluation times, which impact
  the efficiency of the RL process, large-scale RL has not been applied
  extensively in software engineering tasks. As a result, DeepSeek-R1 has not
  demonstrated a huge improvement over DeepSeek-V3 on software engineering
  benchmarks. Future versions will address this by implementing rejection
  sampling on software engineering data or incorporating asynchronous
  evaluations during the RL process to improve efficiency.


# 参考文献

<ul class="ltx_biblist">
  <li id="bib.bib1" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      AI@Meta (2024)
    </span>
    <span class="ltx_bibblock">AI@Meta.</span>
    <span class="ltx_bibblock">Llama 3.1 model card, 2024.</span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://github.com/meta-llama/llama-models/blob/main/models/llama3_1/MODEL_CARD.md"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://github.com/meta-llama/llama-models/blob/main/models/llama3_1/MODEL_CARD.md
      </a>
      .
    </span>
  </li>
  <li id="bib.bib2" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Anthropic (2024)
    </span>
    <span class="ltx_bibblock">Anthropic.</span>
    <span class="ltx_bibblock">Claude 3.5 sonnet, 2024.</span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://www.anthropic.com/news/claude-3-5-sonnet"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://www.anthropic.com/news/claude-3-5-sonnet
      </a>
      .
    </span>
  </li>
  <li id="bib.bib3" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Chen et&nbsp;al. (2021)
    </span>
    <span class="ltx_bibblock">
      M.&nbsp;Chen, J.&nbsp;Tworek, H.&nbsp;Jun, Q.&nbsp;Yuan, H.&nbsp;P. et al
    </span>
    <span class="ltx_bibblock">
      Evaluating large language models trained on code.
    </span>
    <span class="ltx_bibblock">
      , abs/2107.03374, 2021.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://arxiv.org/abs/2107.03374"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://arxiv.org/abs/2107.03374
      </a>
      .
    </span>
  </li>
  <li id="bib.bib4" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Dubey et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      A.&nbsp;Dubey, A.&nbsp;Jauhri, A.&nbsp;Pandey, A.&nbsp;Kadian,
      A.&nbsp;Al-Dahle, A.&nbsp;Letman, A.&nbsp;Mathur, A.&nbsp;Schelten,
      A.&nbsp;Yang, A.&nbsp;Fan, et&nbsp;al.
    </span>
    <span class="ltx_bibblock">The llama 3 herd of models.</span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2407.21783
      , 2024.
    </span>
  </li>
  <li id="bib.bib5" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Dubois et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      Y.&nbsp;Dubois, B.&nbsp;Galambosi, P.&nbsp;Liang, and T.&nbsp;B.
      Hashimoto.
    </span>
    <span class="ltx_bibblock">
      Length-controlled alpacaeval: A simple way to debias automatic evaluators.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2404.04475
      , 2024.
    </span>
  </li>
  <li id="bib.bib6" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Feng et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      X.&nbsp;Feng, Z.&nbsp;Wan, M.&nbsp;Wen, S.&nbsp;M. McAleer, Y.&nbsp;Wen,
      W.&nbsp;Zhang, and J.&nbsp;Wang.
    </span>
    <span class="ltx_bibblock">
      Alphazero-like tree-search can guide large language model decoding and
      training, 2024.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://arxiv.org/abs/2309.17179"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://arxiv.org/abs/2309.17179
      </a>
      .
    </span>
  </li>
  <li id="bib.bib7" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Gao et&nbsp;al. (2022)
    </span>
    <span class="ltx_bibblock">
      L.&nbsp;Gao, J.&nbsp;Schulman, and J.&nbsp;Hilton.
    </span>
    <span class="ltx_bibblock">
      Scaling laws for reward model overoptimization, 2022.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://arxiv.org/abs/2210.10760"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://arxiv.org/abs/2210.10760
      </a>
      .
    </span>
  </li>
  <li id="bib.bib8" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Gema et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      A.&nbsp;P. Gema, J.&nbsp;O.&nbsp;J. Leang, G.&nbsp;Hong, A.&nbsp;Devoto,
      A.&nbsp;C.&nbsp;M. Mancino, R.&nbsp;Saxena, X.&nbsp;He, Y.&nbsp;Zhao,
      X.&nbsp;Du, M.&nbsp;R.&nbsp;G. Madani, C.&nbsp;Barale, R.&nbsp;McHardy,
      J.&nbsp;Harris, J.&nbsp;Kaddour, E.&nbsp;van Krieken, and
      P.&nbsp;Minervini.
    </span>
    <span class="ltx_bibblock">Are we done with mmlu?</span>
    <span class="ltx_bibblock">
      , abs/2406.04127, 2024.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://doi.org/10.48550/arXiv.2406.04127"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://doi.org/10.48550/arXiv.2406.04127
      </a>
      .
    </span>
  </li>
  <li id="bib.bib9" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Google (2024)
    </span>
    <span class="ltx_bibblock">Google.</span>
    <span class="ltx_bibblock">
      Our next-generation model: Gemini 1.5, 2024.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://blog.google/technology/ai/google-gemini-next-generation-model-february-2024"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://blog.google/technology/ai/google-gemini-next-generation-model-february-2024
      </a>
      .
    </span>
  </li>
  <li id="bib.bib10" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      He et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      Y.&nbsp;He, S.&nbsp;Li, J.&nbsp;Liu, Y.&nbsp;Tan, W.&nbsp;Wang,
      H.&nbsp;Huang, X.&nbsp;Bu, H.&nbsp;Guo, C.&nbsp;Hu, B.&nbsp;Zheng,
      et&nbsp;al.
    </span>
    <span class="ltx_bibblock">
      Chinese simpleqa: A chinese factuality evaluation for large language
      models.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2411.07140
      , 2024.
    </span>
  </li>
  <li id="bib.bib11" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Hendrycks et&nbsp;al. (2020)
    </span>
    <span class="ltx_bibblock">
      D.&nbsp;Hendrycks, C.&nbsp;Burns, S.&nbsp;Basart, A.&nbsp;Zou,
      M.&nbsp;Mazeika, D.&nbsp;Song, and J.&nbsp;Steinhardt.
    </span>
    <span class="ltx_bibblock">
      Measuring massive multitask language understanding.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2009.03300
      , 2020.
    </span>
  </li>
  <li id="bib.bib12" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Huang et&nbsp;al. (2023)
    </span>
    <span class="ltx_bibblock">
      Y.&nbsp;Huang, Y.&nbsp;Bai, Z.&nbsp;Zhu, J.&nbsp;Zhang, J.&nbsp;Zhang,
      T.&nbsp;Su, J.&nbsp;Liu, C.&nbsp;Lv, Y.&nbsp;Zhang, J.&nbsp;Lei,
      et&nbsp;al.
    </span>
    <span class="ltx_bibblock">
      C-Eval: A multi-level multi-discipline chinese evaluation suite for
      foundation models.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2305.08322
      , 2023.
    </span>
  </li>
  <li id="bib.bib13" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Jain et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      N.&nbsp;Jain, K.&nbsp;Han, A.&nbsp;Gu, W.&nbsp;Li, F.&nbsp;Yan,
      T.&nbsp;Zhang, S.&nbsp;Wang, A.&nbsp;Solar-Lezama, K.&nbsp;Sen, and
      I.&nbsp;Stoica.
    </span>
    <span class="ltx_bibblock">
      Livecodebench: Holistic and contamination free evaluation of large
      language models for code.
    </span>
    <span class="ltx_bibblock">
      , abs/2403.07974, 2024.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://doi.org/10.48550/arXiv.2403.07974"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://doi.org/10.48550/arXiv.2403.07974
      </a>
      .
    </span>
  </li>
  <li id="bib.bib14" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Krishna et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      S.&nbsp;Krishna, K.&nbsp;Krishna, A.&nbsp;Mohananey, S.&nbsp;Schwarcz,
      A.&nbsp;Stambler, S.&nbsp;Upadhyay, and M.&nbsp;Faruqui.
    </span>
    <span class="ltx_bibblock">
      Fact, fetch, and reason: A unified evaluation of retrieval-augmented
      generation.
    </span>
    <span class="ltx_bibblock">
      , abs/2409.12941, 2024.
    </span>
    <span class="ltx_bibblock">
      <a
        title=""
        href="https:/doi.org/10.48550/ARXIV.2409.12941"
        class="ltx_ref"
      >
        10.48550/ARXIV.2409.12941
      </a>
      .
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://doi.org/10.48550/arXiv.2409.12941"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://doi.org/10.48550/arXiv.2409.12941
      </a>
      .
    </span>
  </li>
  <li id="bib.bib15" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Kumar et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      A.&nbsp;Kumar, V.&nbsp;Zhuang, R.&nbsp;Agarwal, Y.&nbsp;Su, J.&nbsp;D.
      Co-Reyes, A.&nbsp;Singh, K.&nbsp;Baumli, S.&nbsp;Iqbal, C.&nbsp;Bishop,
      R.&nbsp;Roelofs, et&nbsp;al.
    </span>
    <span class="ltx_bibblock">
      Training language models to self-correct via reinforcement learning.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2409.12917
      , 2024.
    </span>
  </li>
  <li id="bib.bib16" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Li et&nbsp;al. (2023)
    </span>
    <span class="ltx_bibblock">
      H.&nbsp;Li, Y.&nbsp;Zhang, F.&nbsp;Koto, Y.&nbsp;Yang, H.&nbsp;Zhao,
      Y.&nbsp;Gong, N.&nbsp;Duan, and T.&nbsp;Baldwin.
    </span>
    <span class="ltx_bibblock">
      CMMLU: Measuring massive multitask language understanding in Chinese.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2306.09212
      , 2023.
    </span>
  </li>
  <li id="bib.bib17" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Li et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      T.&nbsp;Li, W.-L. Chiang, E.&nbsp;Frick, L.&nbsp;Dunlap, T.&nbsp;Wu,
      B.&nbsp;Zhu, J.&nbsp;E. Gonzalez, and I.&nbsp;Stoica.
    </span>
    <span class="ltx_bibblock">
      From crowdsourced data to high-quality benchmarks: Arena-hard and
      benchbuilder pipeline.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2406.11939
      , 2024.
    </span>
  </li>
  <li id="bib.bib18" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Lightman et&nbsp;al. (2023)
    </span>
    <span class="ltx_bibblock">
      H.&nbsp;Lightman, V.&nbsp;Kosaraju, Y.&nbsp;Burda, H.&nbsp;Edwards,
      B.&nbsp;Baker, T.&nbsp;Lee, J.&nbsp;Leike, J.&nbsp;Schulman,
      I.&nbsp;Sutskever, and K.&nbsp;Cobbe.
    </span>
    <span class="ltx_bibblock">Let’s verify step by step.</span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2305.20050
      , 2023.
    </span>
  </li>
  <li id="bib.bib19" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Lin (2024)
    </span>
    <span class="ltx_bibblock">B.&nbsp;Y. Lin.</span>
    <span class="ltx_bibblock">
      ZeroEval: A Unified Framework for Evaluating Language Models, July 2024.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://github.com/WildEval/ZeroEval"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://github.com/WildEval/ZeroEval
      </a>
      .
    </span>
  </li>
  <li id="bib.bib20" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      MAA (2024)
    </span>
    <span class="ltx_bibblock">MAA.</span>
    <span class="ltx_bibblock">
      American invitational mathematics examination - aime.
    </span>
    <span class="ltx_bibblock">
      In
        American Invitational Mathematics Examination - AIME 2024
      , February 2024.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://maa.org/math-competitions/american-invitational-mathematics-examination-aime"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://maa.org/math-competitions/american-invitational-mathematics-examination-aime
      </a>
      .
    </span>
  </li>
  <li id="bib.bib21" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      OpenAI (2024a)
    </span>
    <span class="ltx_bibblock">OpenAI.</span>
    <span class="ltx_bibblock">Hello GPT-4o, 2024a.</span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://openai.com/index/hello-gpt-4o/"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://openai.com/index/hello-gpt-4o/
      </a>
      .
    </span>
  </li>
  <li id="bib.bib22" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      OpenAI (2024b)
    </span>
    <span class="ltx_bibblock">OpenAI.</span>
    <span class="ltx_bibblock">Learning to reason with llms, 2024b.</span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://openai.com/index/learning-to-reason-with-llms/"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://openai.com/index/learning-to-reason-with-llms/
      </a>
      .
    </span>
  </li>
  <li id="bib.bib23" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      OpenAI (2024c)
    </span>
    <span class="ltx_bibblock">OpenAI.</span>
    <span class="ltx_bibblock">Introducing SimpleQA, 2024c.</span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://openai.com/index/introducing-simpleqa/"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://openai.com/index/introducing-simpleqa/
      </a>
      .
    </span>
  </li>
  <li id="bib.bib24" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      OpenAI (2024d)
    </span>
    <span class="ltx_bibblock">OpenAI.</span>
    <span class="ltx_bibblock">
      Introducing SWE-bench verified we’re releasing a human-validated subset of
      swe-bench that more, 2024d.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://openai.com/index/introducing-swe-bench-verified/"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://openai.com/index/introducing-swe-bench-verified/
      </a>
      .
    </span>
  </li>
  <li id="bib.bib25" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Qwen (2024a)
    </span>
    <span class="ltx_bibblock">Qwen.</span>
    <span class="ltx_bibblock">
      Qwq: Reflect deeply on the boundaries of the unknown, 2024a.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://qwenlm.github.io/blog/qwq-32b-preview/"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://qwenlm.github.io/blog/qwq-32b-preview/
      </a>
      .
    </span>
  </li>
  <li id="bib.bib26" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Qwen (2024b)
    </span>
    <span class="ltx_bibblock">Qwen.</span>
    <span class="ltx_bibblock">
      Qwen2.5: A party of foundation models, 2024b.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://qwenlm.github.io/blog/qwen2.5"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://qwenlm.github.io/blog/qwen2.5
      </a>
      .
    </span>
  </li>
  <li id="bib.bib27" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Rein et&nbsp;al. (2023)
    </span>
    <span class="ltx_bibblock">
      D.&nbsp;Rein, B.&nbsp;L. Hou, A.&nbsp;C. Stickland, J.&nbsp;Petty,
      R.&nbsp;Y. Pang, J.&nbsp;Dirani, J.&nbsp;Michael, and S.&nbsp;R. Bowman.
    </span>
    <span class="ltx_bibblock">
      GPQA: A graduate-level google-proof q&amp;a benchmark.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2311.12022
      , 2023.
    </span>
  </li>
  <li id="bib.bib28" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Shao et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      Z.&nbsp;Shao, P.&nbsp;Wang, Q.&nbsp;Zhu, R.&nbsp;Xu, J.&nbsp;Song,
      M.&nbsp;Zhang, Y.&nbsp;Li, Y.&nbsp;Wu, and D.&nbsp;Guo.
    </span>
    <span class="ltx_bibblock">
      Deepseekmath: Pushing the limits of mathematical reasoning in open
      language models.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2402.03300
      , 2024.
    </span>
  </li>
  <li id="bib.bib29" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Silver et&nbsp;al. (2017a)
    </span>
    <span class="ltx_bibblock">
      D.&nbsp;Silver, T.&nbsp;Hubert, J.&nbsp;Schrittwieser, I.&nbsp;Antonoglou,
      M.&nbsp;Lai, A.&nbsp;Guez, M.&nbsp;Lanctot, L.&nbsp;Sifre,
      D.&nbsp;Kumaran, T.&nbsp;Graepel, T.&nbsp;P. Lillicrap, K.&nbsp;Simonyan,
      and D.&nbsp;Hassabis.
    </span>
    <span class="ltx_bibblock">
      Mastering chess and shogi by self-play with a general reinforcement
      learning algorithm.
    </span>
    <span class="ltx_bibblock">
      , abs/1712.01815, 2017a.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="http://arxiv.org/abs/1712.01815"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        http://arxiv.org/abs/1712.01815
      </a>
      .
    </span>
  </li>
  <li id="bib.bib30" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Silver et&nbsp;al. (2017b)
    </span>
    <span class="ltx_bibblock">
      D.&nbsp;Silver, J.&nbsp;Schrittwieser, K.&nbsp;Simonyan,
      I.&nbsp;Antonoglou, A.&nbsp;Huang, A.&nbsp;Guez, T.&nbsp;Hubert,
      L.&nbsp;Baker, M.&nbsp;Lai, A.&nbsp;Bolton, Y.&nbsp;Chen, T.&nbsp;P.
      Lillicrap, F.&nbsp;Hui, L.&nbsp;Sifre, G.&nbsp;van&nbsp;den Driessche,
      T.&nbsp;Graepel, and D.&nbsp;Hassabis.
    </span>
    <span class="ltx_bibblock">
      Mastering the game of go without human knowledge.
    </span>
    <span class="ltx_bibblock">
      , 550(7676):354–359, 2017b.
    </span>
    <span class="ltx_bibblock">
      <a title="" href="https:/doi.org/10.1038/NATURE24270" class="ltx_ref">
        10.1038/NATURE24270
      </a>
      .
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://doi.org/10.1038/nature24270"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://doi.org/10.1038/nature24270
      </a>
      .
    </span>
  </li>
  <li id="bib.bib31" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Snell et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      C.&nbsp;Snell, J.&nbsp;Lee, K.&nbsp;Xu, and A.&nbsp;Kumar.
    </span>
    <span class="ltx_bibblock">
      Scaling llm test-time compute optimally can be more effective than scaling
      model parameters, 2024.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://arxiv.org/abs/2408.03314"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://arxiv.org/abs/2408.03314
      </a>
      .
    </span>
  </li>
  <li id="bib.bib32" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Trinh et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      T.&nbsp;Trinh, Y.&nbsp;Wu, Q.&nbsp;Le, H.&nbsp;He, and T.&nbsp;Luong.
    </span>
    <span class="ltx_bibblock">
      Solving olympiad geometry without human demonstrations.
    </span>
    <span class="ltx_bibblock">
      , 2024.
    </span>
    <span class="ltx_bibblock">
      <a
        title=""
        href="https:/doi.org/10.1038/s41586-023-06747-5"
        class="ltx_ref"
      >
        10.1038/s41586-023-06747-5
      </a>
      .
    </span>
  </li>
  <li id="bib.bib33" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Uesato et&nbsp;al. (2022)
    </span>
    <span class="ltx_bibblock">
      J.&nbsp;Uesato, N.&nbsp;Kushman, R.&nbsp;Kumar, F.&nbsp;Song,
      N.&nbsp;Siegel, L.&nbsp;Wang, A.&nbsp;Creswell, G.&nbsp;Irving, and
      I.&nbsp;Higgins.
    </span>
    <span class="ltx_bibblock">
      Solving math word problems with process-and outcome-based feedback.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2211.14275
      , 2022.
    </span>
  </li>
  <li id="bib.bib34" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Wang et&nbsp;al. (2023)
    </span>
    <span class="ltx_bibblock">
      P.&nbsp;Wang, L.&nbsp;Li, Z.&nbsp;Shao, R.&nbsp;Xu, D.&nbsp;Dai,
      Y.&nbsp;Li, D.&nbsp;Chen, Y.&nbsp;Wu, and Z.&nbsp;Sui.
    </span>
    <span class="ltx_bibblock">
      Math-shepherd: A label-free step-by-step verifier for llms in mathematical
      reasoning.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2312.08935
      , 2023.
    </span>
  </li>
  <li id="bib.bib35" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Wang et&nbsp;al. (2022)
    </span>
    <span class="ltx_bibblock">
      X.&nbsp;Wang, J.&nbsp;Wei, D.&nbsp;Schuurmans, Q.&nbsp;Le, E.&nbsp;Chi,
      S.&nbsp;Narang, A.&nbsp;Chowdhery, and D.&nbsp;Zhou.
    </span>
    <span class="ltx_bibblock">
      Self-consistency improves chain of thought reasoning in language models.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2203.11171
      , 2022.
    </span>
  </li>
  <li id="bib.bib36" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Wang et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      Y.&nbsp;Wang, X.&nbsp;Ma, G.&nbsp;Zhang, Y.&nbsp;Ni, A.&nbsp;Chandra,
      S.&nbsp;Guo, W.&nbsp;Ren, A.&nbsp;Arulraj, X.&nbsp;He, Z.&nbsp;Jiang,
      T.&nbsp;Li, M.&nbsp;Ku, K.&nbsp;Wang, A.&nbsp;Zhuang, R.&nbsp;Fan,
      X.&nbsp;Yue, and W.&nbsp;Chen.
    </span>
    <span class="ltx_bibblock">
      Mmlu-pro: A more robust and challenging multi-task language understanding benchmark.
    </span>
    <span class="ltx_bibblock">
      , abs/2406.01574, 2024.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://doi.org/10.48550/arXiv.2406.01574"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://doi.org/10.48550/arXiv.2406.01574
      </a>
      .
    </span>
  </li>
  <li id="bib.bib37" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Xia et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      C.&nbsp;S. Xia, Y.&nbsp;Deng, S.&nbsp;Dunn, and L.&nbsp;Zhang.
    </span>
    <span class="ltx_bibblock">
      Agentless: Demystifying llm-based software engineering agents.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint
      , 2024.
    </span>
  </li>
  <li id="bib.bib38" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Xin et&nbsp;al. (2024)
    </span>
    <span class="ltx_bibblock">
      H.&nbsp;Xin, Z.&nbsp;Z. Ren, J.&nbsp;Song, Z.&nbsp;Shao, W.&nbsp;Zhao,
      H.&nbsp;Wang, B.&nbsp;Liu, L.&nbsp;Zhang, X.&nbsp;Lu, Q.&nbsp;Du,
      W.&nbsp;Gao, Q.&nbsp;Zhu, D.&nbsp;Yang, Z.&nbsp;Gou, Z.&nbsp;F. Wu,
      F.&nbsp;Luo, and C.&nbsp;Ruan.
    </span>
    <span class="ltx_bibblock">
      Deepseek-prover-v1.5: Harnessing proof assistant feedback for
      reinforcement learning and monte-carlo tree search, 2024.
    </span>
    <span class="ltx_bibblock">
      URL
      <a
        title=""
        href="https://arxiv.org/abs/2408.08152"
        class="ltx_ref ltx_url ltx_font_typewriter"
      >
        https://arxiv.org/abs/2408.08152
      </a>
      .
    </span>
  </li>
  <li id="bib.bib39" class="ltx_bibitem">
    <span class="ltx_tag ltx_role_refnum ltx_tag_bibitem">
      Zhou et&nbsp;al. (2023)
    </span>
    <span class="ltx_bibblock">
      J.&nbsp;Zhou, T.&nbsp;Lu, S.&nbsp;Mishra, S.&nbsp;Brahma, S.&nbsp;Basu,
      Y.&nbsp;Luan, D.&nbsp;Zhou, and L.&nbsp;Hou.
    </span>
    <span class="ltx_bibblock">
      Instruction-following evaluation for large language models.
    </span>
    <span class="ltx_bibblock">
        arXiv preprint arXiv:2311.07911
      , 2023.
    </span>
  </li>
</ul>


----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
