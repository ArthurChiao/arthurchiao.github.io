---
layout    : post
title     : "[译] 大模型训练的中场叙事：从 Reasoning Thinking 转向 Agentic Thinking (2026)"
date      : 2026-07-05
lastupdate: 2026-07-05
categories: ai llm agents
---

### 译者序

本文翻译自 2026 年 Junyang Lin 在 X 上的一篇文章
[From "Reasoning" Thinking to "Agentic" Thinking](https://x.com/JustinLin610/status/2037116325210829168)。

过去两年，有两件事情被重塑了：
我们<strong><mark>如何评估模型</mark></strong>（how we evaluate models），
以及<strong><mark>我们期望从模型得到什么</mark></strong>（what we expect from them）。

从 reasoning thinking 转向 agentic thinking 带来的深层次转变：

* 训练理念：为了想得更久而 thinking -> 为了<strong><mark>行动</mark></strong>而 thinking；
* 训练对象：模型本身 -> <strong><mark>模型+环境</mark></strong>（<strong><mark><code>Agent+Harness</code></mark></strong>）；
* 关注的多样性：数据多样性 -> <strong><mark>环境多样性</mark></strong>。

水平及维护精力所限，译文不免存在错误或过时之处，如有疑问，请查阅原文。
<strong><mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark></strong>。

以下是译文。

----

* TOC
{:toc}

----

# 1 Reasoning thinking（推理式思考）

## 1.1 OpenAI o1：thinking 作为新的一等能力，直接暴露给用户

OpenAI 将 o1 描述为一种<strong><mark>通过强化学习训练出来、能够“先思考再回答”</mark></strong>的模型。

`o1` 证明了 <strong><mark>“thinking” 可以是一种一等能力</mark></strong>，一种可以<strong><mark>专门训练、并直接暴露给用户</mark></strong>的能力。


## 1.2 DeepSeek-R1：对 `thinking` 能力的复现和扩展

DeepSeek 把 R1 定位为一个可以与 o1 竞争的开源推理模型。

它也证明了 <strong><mark>reasoning-style 的后训练</mark></strong>可以被<strong><mark>复现和进一步强化</mark></strong>。

## 1.3 2025 年上半年：行业关注的仍然是推理式思考

以上阶段很重要。
事实上直到 2025 年上半年，行业关注的重点基本仍然是 <strong><mark>“reasoning thinking”</mark></strong>：

1. 如何让模型在 <strong><mark>inference 阶段使用更多算力</mark></strong>，
1. 如何用<strong><mark>更强的奖励</mark></strong>来训练它们，以及
1. 如何把这种额外的 <strong><mark>reasoning 能力暴露出来或加以控制</mark></strong>。

接下来会朝着什么方向演进？

# 2 Agentic thinking（智能体式思考）

我相信答案是“<strong><mark>agentic thinking</mark></strong>”：

1. 为了<strong><mark>执行行动</mark></strong>而思考
1. 在<strong><mark>与环境交互的过程中</mark></strong>思考，并且
1. <strong><mark>根据来自真实世界的反馈不断更新 plan</mark></strong>。

## 2.1 强化学习 scaling 的前提：确定、稳定且可扩展的反馈信号

第一波推理模型让我们明白，

* 如果想把<strong><mark>强化学习</mark></strong>在语言模型上 scale，
  就必须有 <strong><mark>确定、稳定且可扩展的反馈信号</mark></strong>。
* 数学、代码、逻辑以及其他<strong><mark>可验证的领域</mark></strong>之所以变得核心，正是因为这些场景下的奖励信号<strong><mark>远强于宽泛的偏好监督</mark></strong> (generic preference supervision)。
* 这些场景让强化学习优化的是<strong><mark>“正确性”</mark></strong> (correctness)，而不是“看起来像对的” (plausibility)。

## 2.2 强化学习不再是一个可选步骤，而是一个系统工程

一旦我们开始以“<strong><mark>能沿着更长的轨迹进行推理</mark></strong>”去训练一个模型，
强化学习就不再只是<strong><mark>监督微调之上的一个轻量附加组件</mark></strong>，
而会 <strong><mark>变成一个系统工程问题</mark></strong>。
你需要大规模 rollout、高吞吐验证、稳定的策略更新，以及高效采样。

## 2.3 重大转变：从大规模 pre-train 转向大规模 post-train (for reasoning)

推理模型的兴起，既是一个<strong><mark>关于建模的叙事</mark></strong>，也是一个<strong><mark>关于基础设施的叙事</mark></strong>。

* OpenAI 把 o1 描述为一条通过<strong><mark>强化学习训练出来</mark></strong>的<strong><mark>推理线</mark></strong> (reasoning line)，
* DeepSeek R1 后来进一步强化了这个方向，揭示了这种 <strong><mark>reasoning-based RL</mark></strong> 到底需要多少专门的算法与基础设施投入。

第一个重大转变，也由此发生：从扩展预训练 (scaling pretraining)，转向扩展面向推理的后训练 (scaling post-training for reasoning)。

# 3 混合 thinking 的尝试

## 3.1 Qwen 的尝试

在 2025 年初，Qwen 团队雄心勃勃地认为理想的模型应该 <strong><mark>统一 thinking 和 instruct 两种模式</mark></strong>。

* <strong><mark>可指定推理预算</mark></strong>，例如 low / medium / high 这样的推理档位；
* 最好还能从提示词和上下文中自动推断出合适的推理预算，让模型自己判断什么时候应该立刻回答，什么时候需要多想一会儿，什么时候又应该在真正困难的问题上投入更多计算。

从理念上说，这个方向是对的。Qwen3 就是当时最清晰的公开尝试之一：它引入了“混合思维模式”，
在同一模型家族中同时支持 thinking 和 non-thinking 两种行为，强调可控的思考预算，
并描述了一条四阶段后训练流程，其中在长 CoT 冷启动和推理强化学习之后，还显式加入了“thinking mode fusion（思维模式融合）”。

### 3.1.1 困难：thinking 和 non-thinking 模式的数据分布和行为目标很不一样

“融合”这件事，做起来远比说起来难。
真正困难的部分在于数据。

当考虑把 thinking 和 instruct 合并时，大家往往首先想到的是模型侧兼容性：

* 一个 checkpoint 能不能同时支持两种模式，
* 一个 chat template 能不能在两者之间切换，
* 一套服务栈能不能暴露出正确的控制开关。

但这里的一个深层的问题是，<strong><mark>这两种模式对应的数据分布和行为目标本来就有显著差异</mark></strong>。

在尝试平衡模型融合与提升后训练数据质量、多样性的过程中，我们做的并不好。

### 3.1.2 用户行为画像的冲突

在这个阶段，我们也非常关注用户究竟是如何实际使用 thinking 和 instruct 模式的。

* instruct 模型：优点是直接、简洁、格式合规，因此在改写、标注、模板化客服、结构化抽取、运营问答这类高频、批量的企业场景任务上很受客户欢迎；
* thinking 模型：优点是适合长程和复杂任务，有连贯的中间结构、多条备选路径，保留了足够多的内部计算来提升最终正确率，因此也需要花更多 token。

这两种行为画像<strong><mark>天然是彼此拉扯的</mark></strong>。
如果融合数据没有经过足够精细的策划与筛选，结果通常是在两个方向上都做得很平庸：

* “thinking” 会变得噪声更大、更臃肿、也更缺乏决断力；
* “instruct” 则会变得不够干净利落、不够稳定，成本也高于企业用户真正想要的水平。

### 3.1.3 回到独立的 Instruct 和 Thinking 模型

在实践中，还是 thinking 和 non-thinking 模式更有吸引力。因此，
Qwen3 2507 版本发布了独立的 Instruct 和 Thinking 更新，其中包括 30B 和 235B 版本。

在商业部署中，仍然有大量客户更需要那种高吞吐、低成本、高可控的 instruct 行为来处理批量任务。
在这些场景里，融合并没有显而易见的收益。

将两条线分开，反而让团队能够更干净地聚焦解决各自的数据和训练问题。

## 3.2 其他厂商的融合模型

### 3.2.1 Claude / GLM / DeepSeek

* Anthropic 公开主张一种一体化模型哲学
    * Claude 3.7 Sonnet 为混合推理模型，用户既可以选择普通响应，也可以选择 extended thinking，而 API 用户还能设置 thinking budget。
    * Anthropic 明确表示，他们相信<strong><mark>推理</mark></strong> (reasoning) 应该是一种<strong><mark>集成到模型内的能力</mark></strong>，
    <strong><mark>而不是一个独立模型</mark></strong>。
* GLM-4.5 也公开将自己定位为同时具有 thinking 和 non-thinking 模式的混合推理模型，把推理、编码和 agent 能力统一在一起；
* DeepSeek 后来也通过 V3.1 的 “Think & Non-Think” 混合推理走向了类似方向。

## 3.3 背后的关键问题：强硬拼凑还是自然生长出来的

这里有个关键问题是：<strong><mark>这种融合是不是“自然生长”出来的</mark></strong>。

* 如果 thinking 和 instruct 只是被强行塞进同一个 checkpoint 里，却依然像两个生硬拼接起来的人格那样行事，那么产品体验仍然会很别扭。
* 真正成功的融合，需要一条平滑的 reasoning effort 光谱。
    * 模型应当能够表达多个不同层级的 effort 程度，并且理想情况下还能自适应地做出选择。
    * GPT 风格的 effort control 指向的正是这一点：<strong><mark>控制的是一套关于推理预算的策略</mark></strong>，而不是一个非黑即白的二元开关。

Anthropic 对 Claude 3.7 和 Claude 4 的公开表述相对克制。

* 他们强调一体化推理、用户可控的思考预算、真实世界任务、代码质量，以及后来在 extended thinking 过程中使用工具的能力。
* Claude 3.7 呈现为一个具有可控预算的混合推理模型；Claude 4 则进一步扩展到允许<strong><mark>推理与工具使用交替进行</mark></strong>。
* 与此同时，Anthropic 也把<strong><mark>编码、长时间运行任务以及 agent 工作流</mark></strong>明确作为<strong><mark>主要目标</mark></strong>。

### 3.3.1 生成更长的推理轨迹，并不会自动让模型更聪明

* 很多时候，过度外显的推理恰恰意味着计算资源分配不佳。
* 如果模型试图用同一种冗长方式去思考所有事情，那它可能并没有做好优先级排序，没有做好压缩，也没有做好行动。

### 3.3.2 thinking 应该由目标 workload 来决定

Anthropic 的路径暗示了一种更克制、更有纪律性的观点：<strong><mark>thinking 应该由目标 workload 来决定</mark></strong>。

* 如果目标是编码，那么 thinking 就应该帮助代码库导航、规划、任务分解、错误恢复以及工具编排；
* 如果目标是 agent 工作流，那么 thinking 的作用就应该是在长时程里提升执行质量，而不是产出一段看起来很厉害的中间输出物。

# 4 从“训练模型”的时代，迈向“训练 Agent”的时代

这种对“目标导向”的强调，指向了一个更大的变化：<strong><mark>我们正在从“训练模型”的时代，迈向“训练 Agent”的时代</mark></strong>。

我们在 Qwen3 博客中也明确写到，“我们正从一个聚焦训练模型的时代，转向一个以训练智能体为中心的时代”，并把未来强化学习的进步与面向长时程推理的环境反馈联系起来。

## 4.1 Agent 核心：与世界形成闭环交互

所谓 Agent，是一种能够制定计划（plan）、决定何时行动（act）、何时使用工具（tools）、感知环境反馈、修正策略（reflection），
并在长时间范围内持续前进的系统。

<strong><mark>Agent 的核心，是与世界形成闭环交互</mark></strong>。

## 4.2 Agentic thinking 与 Reasoning thinking 的区别

二者对应的优化目标完全不同：

* reasoning thinking 通常是根据最终答案之前<strong><mark>那段内部思考的质量</mark></strong>来判断的：模型能不能解出定理、写出证明、生成正确代码，或者通过某项 benchmark；
* agentic thinking 关心的则是：<strong><mark>模型在与环境交互的过程中，能不能持续前进</mark></strong>。

核心问题由<strong><mark>“模型能不能思考得足够长？”</mark></strong>转变为
<strong><mark>“模型能不能以一种能够支撑有效行动的方式来思考？”</mark></strong>

Agentic thinking 必须处理几类纯推理模型大多不会遇到的问题：

* 决定什么时候停止思考并开始采取行动
* 选择调用哪个工具，以及按什么顺序调用
* 将来自环境的噪声观察或不完整观察纳入决策
* 在失败之后修正计划
* 在多轮交互和多次工具调用中维持一致性

# 5 为什么 Agentic RL 基础设施更难

## 5.1 通过行动来推理（reasoning through action）的模型能力

Agentic thinking，本质上意味着通过行动来推理 (<strong><mark><code>reasoning through action</code></mark></strong>) 的模型能力。

## 5.2 目标转向：打榜 -> 解决交互式任务

一旦目标从“benchmark 打榜”转向<strong><mark>“解决交互式任务”</mark></strong>，整套强化学习栈也会随之改变。
传统推理强化学习所依赖的基础设施已经不够用了。

* reasoning RL 中，往往可以把 rollout 看成相对自包含的轨迹，并且有较为明确的评估器。
* agentic RL 中，<strong><mark>策略被嵌入进一个更大的 harness 之中</mark></strong>：工具服务器、浏览器、终端、搜索引擎、模拟器、执行沙箱、API 层、记忆系统，以及各种编排框架。

## 5.3 新的系统要求：训练与推理必须更加彻底地解耦

Agentic RL 中，<strong><mark>环境不再只是一个静态验证器，而成为训练系统本身的一部分</mark></strong>。

这带来了一个新的系统要求：<strong><mark>训练与推理必须更加彻底地解耦</mark></strong>。如果做不到这种解耦，rollout 吞吐就会崩掉。
设想一个 coding agent，

* 必须把生成的代码拿到真实测试 harness 中执行：推理侧会因为等待执行反馈而停滞，
  训练侧则因为拿不到完成的轨迹而“挨饿”，整条流水线的 GPU 利用率会远低于经典 reasoning RL 中的水平。
* 再叠加工具延迟、部分可观测性和有状态环境，这些低效只会被进一步放大。

结果就是实验迭代慢到令人窒息。

## 5.4 环境本身也开始成为一等研究对象

<strong><mark>环境本身也开始成为一等研究对象</mark></strong>。

* 在 SFT 时代，我们痴迷于<strong><mark>数据多样性</mark></strong>；
* 到了 agent 时代，我们更应该痴迷于<strong><mark>环境质量</mark></strong>：稳定性、真实性、覆盖度、难度、状态多样性、反馈丰富度、抗利用能力，以及 rollout 生成的可扩展性。

构建环境这件事，已经开始从边角项目变成真正的创业赛道。
如果一个 Agent 训练用来在接近生产的场景中工作，那么环境本身就是其核心能力栈的一部分。

# 6 下一个行业前沿：更有用的思考

## 6.1 Agentic thinking 会成为未来主流的 thinking 形式

我的判断是，agentic thinking 会成为未来主导性的 thinking 形式。
我甚至认为，它最终会在很大程度上取代过去那种“<strong><mark>静态独白式</mark></strong>”的 reasoning thinking：

* 通过输出越来越多文本，来弥补<strong><mark>缺乏交互的超长、孤立的内部轨迹</mark></strong>。
* 即便在非常困难的数学或编码任务上，一个真正先进的系统也应当有权去搜索、模拟、执行、检查、验证并修正方向。

目标不该只是“想得很长”，而应该是稳健且高效地解决问题。

## 6.2 Agentic Thinking 训练面临的新挑战

训练这类系统最棘手的挑战，是 <strong><mark>reward hacking</mark></strong>（奖励过程被黑了）。
一旦模型获得了真正有意义的工具访问能力，reward hacking 就会变得危险得多。

* 带搜索能力的模型，可能会在强化学习过程中学会<strong><mark>直接去搜答案</mark></strong>；
* 一个 coding agent 可能会利用仓库中的<strong><mark>未来信息、滥用日志</mark></strong>，找到一些“捷径”；
* 一个存在隐蔽漏洞的环境，可能会让策略看起来像是“超人级”，但实际上训练出来的是<strong><mark>作弊能力</mark></strong>。

这也是为什么 agent 时代会比 reasoning 时代微妙得多。
更强的工具当然会让模型更有用，但也同时<strong><mark>扩大了伪优化的攻击面</mark></strong>。

下一批真正严肃的研究瓶颈，很可能会出现在环境设计、评估器鲁棒性、反作弊协议，以及策略与世界之间的接口设计上。
尽管如此，方向已经很清楚了：<strong><mark>具备工具能力的 thinking，显然比孤立的 thinking 更有用</mark></strong>，也更有机会真正提升现实生产力。

## 6.3 Harness Engineering 变得极其重要

<strong><mark>agentic thinking 也意味着 harness engineering 会变得极其重要</mark></strong>。

未来的核心智能，将越来越多地来自多个 Agent 如何被组织起来：

* 一个负责规划与路由的 orchestrator，
* 若干像领域专家一样行动的 specialized agents，以及
* 执行更窄任务、同时帮助控制上下文、避免污染、保持不同推理层级分离的 sub-agents。

未来的演进路径，将是<strong><mark>训练模型 -> 训练 Agent -> 训练系统</mark></strong>。

# 7 总结

## 7.1 推理的第一阶段：反馈信号 + 基础设施 + 语言模型 + 强化学习 -> 认知能力的质变

推理浪潮的第一阶段已经明确了一件重要事实：
当<strong><mark>反馈信号</mark></strong>足够可靠、<strong><mark>基础设施</mark></strong>能支撑时，
叠加在语言模型之上的<strong><mark>强化学习</mark></strong>，确实可以带来<strong><mark>认知能力的质变</mark></strong>。

## 7.2 推理的第二阶段：训练模型 -> 训练"模型+环境"（Agent+Harness）

从 reasoning thinking 转向 agentic thinking 带来的深层次转变：从“为了想得更久而思考”，转向“为了行动而思考”。

* 训练的核心对象已经变了：不再只是模型本身，而是<strong><mark>“模型加环境”</mark></strong>；
* 更具体地说，是 <strong><mark>Agent 以及围绕它构建的 harness</mark></strong>。

### 7.2.1 改变一：哪些研究对象最重要

这次转向重新定义了“哪些研究对象最重要”：当然仍然包括模型架构和训练数据，但同样也包括环境设计、rollout 基础设施、评估器鲁棒性，以及 Agent 协作接口。

### 7.2.2 改变二：Good thinking 的定义

这次转向也改变了 “good thinking” 的定义：<strong><mark>不再是最长、最显眼的那条轨迹，而是在真实世界约束下最能支撑行动的那条轨迹</mark></strong>。

### 7.2.3 改变三：竞争力的来源

这次转向同样改变了竞争优势的来源。

* reasoning 时代，领先优势主要来自<strong><mark>更好的强化学习算法、更强的反馈信号，以及更可扩展的训练流水线</mark></strong>；
* agentic 时代，优势将更多来自 <strong><mark>更好的环境、更紧密的训练-服务协同、更强的 harness engineering，以及把模型决策与其结果真正闭环起来的能力</mark></strong>。

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
