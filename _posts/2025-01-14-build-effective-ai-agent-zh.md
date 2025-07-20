---
layout    : post
title     : "[译] AI Workflow & AI Agent：架构、模式与工程建议（Anthropic，2024）"
date      : 2025-01-14
lastupdate: 2025-01-14
categories: ai llm
---

### 译者序

本文翻译自 2024 年 Anthropic（开发 Claude 大模型的公司）的一篇文章 [Building Effective Agents](https://www.anthropic.com/research/building-effective-agents)。

> Agents 只是一些**<mark>“在一个循环中，基于环境反馈来选择合适的工具，最终完成其任务”</mark>**的大模型。

水平及维护精力所限，译文不免存在错误或过时之处，如有疑问，请查阅原文。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

以下是译文。

----

* TOC
{:toc}

----

过去一年中，我们与几十个团队合作过，构建了很多不同行业的大模型 Agent。
我们从中得到的经验是：**<mark>成功的 Agent</mark>** 并不是依靠复杂的框架或库，
而是**<mark>基于简单、可组合的模式逐步构建</mark>**的。

本文总结我们在此过程中积累的一些 **<mark>Agent 方法论</mark>**，并给出一些**<mark>实用的工程建议</mark>**。

# 1 什么是 AI `Agent`/`Workflow`？

目前关于 AI Agent 并**<mark>没有一个统一的定义</mark>**：

* 有人将 Agent 定义为**<mark>完全自主的系统</mark>**，这些系统可以在较长时间内**<mark>独立运行，使用各种工具来完成复杂任务</mark>**。
* 有人则用这个术语来描述一种遵循预定义**<mark>工作流</mark>**的规范实现（prescriptive implementations that follow predefined workflows）。

在 Anthropic，我们将所有这些统一归类为 **<mark><code>agentic systems</code></mark>**。

## 1.1 Workflow	vs. Agent

虽然统一称为“智能体系统”，但我们还是对 Workflow 和 Agent 做出了重要的**<mark>架构区分</mark>**，
因此二者属于两类不同的系统：

* Workflow：**<mark>通过预定义的代码路径</mark>**来**<mark>编排大模型和和工具</mark>**
  （systems where LLMs and tools are orchestrated through predefined code paths）；
* Agent：**<mark>大模型动态决定自己的流程及使用什么工具，自主控制如何完成任务</mark>**
  （systems where LLMs dynamically direct their own processes and tool usage, maintaining control over how they accomplish tasks）。

## 1.2 何时使用/不使用 Agent & Workflow

在使用大模型构建应用程序时，我们建议**<mark>寻找尽可能简单的方案，只有在必要时才增加复杂性</mark>**。

* 这意味着**<mark>如无必要，不要试图构建 Agent/Workflow</mark>**。
* Agent/Workflow 虽然在处理任务时效果更好，但通常也会有更高的延迟和成本，因此需要权衡利弊。

如果确实是要解决复杂场景的问题，

* Workflow 为明确定义的任务提供了可预测性和一致性，
* Agent 则在需要**<mark>大规模灵活性和模型驱动的决策</mark>**时是一个更好的选择。

但是，对于很多应用程序来说，大模型本身加上 **<mark>RAG、in-context examples</mark>**
等技术通常就足以解决问题了。

## 1.3 何时以及如何使用框架

许多框架可以简化 Agent/Workflow 的实现，包括：

* [LangGraph](https://langchain-ai.github.io/langgraph/) from LangChain;
* Amazon Bedrock's [AI Agent framework](https://aws.amazon.com/bedrock/agents/);
* [Rivet](https://rivet.ironcladapp.com/), a drag and drop GUI LLM workflow builder; and
* [Vellum](https://www.vellum.ai/), another GUI tool for building and testing complex workflows.

这些框架通过简化标准的底层任务（如调用 LLM、定义和解析工具以及链接调用）使用户更容易入门。
但是，它们通常会**<mark>创建额外的抽象层</mark>**，这可能会使**<mark>底层的提示和响应变得难以调试</mark>**，增加了不必要的复杂性。

我们建议开发者，

* **<mark>首选直接使用 LLM API</mark>**：本文接下来介绍的许多模式几行代码就能实现；
* 如果**<mark>确实要用框架，要确保理解这些框架的底层代码</mark>**。对底层代码的错误假设是常见的问题来源。

## 1.4 一些例子

见 [anthropic-cookbook](https://github.com/anthropics/anthropic-cookbook/tree/main/patterns/agents)。

# 2 Workflow & Agent 的基础构建模块

## 2.1 增强型大模型（augmented LLM）

如下图所示，Agent/Workflow 的基本构建模块是一个**<mark>增强型大语言模型</mark>**，

<p align="center"><img src="/assets/img/build-effective-ai-agents/augmented-llm.webp" width="100%" height="100%"></p>

这个模型具有**<mark>检索、工具和记忆</mark>**等增强功能。
模型可以主动使用这些功能，例如搜索查询、选择适当的工具、保存必要的信息到记忆模块中等等。

## 2.2 功能选型建议

关于以上提到的增强功能如何选择，我们有如下建议：

1. 不是所有功能都需要用上，而应该**<mark>根据你的实际需求，只保留最必要的部分</mark>**；
2. 尽量使用那些**<mark>文档完善的组件</mark>**，否则就是给自己挖坑。

最后，实现这些增强功能有很多方式，我们最近发布的
[Model Context Protocol](https://www.anthropic.com/news/model-context-protocol) 也是其中一种。
开发者只需要实现简单的客户端 [client implementation](https://modelcontextprotocol.io/tutorials/building-a-client#building-mcp-clients)，
就能与不断增长的第三方工具生态系统进行集成。

## 2.3 小结

基于增强型大模型，我们就可以构建出各种 AI Workflow & Agent。

# 3 Workflow

本节来看一些常见的 AI Workflow 范式。

## 3.1 提示链（Prompt chaining）

<p align="center"><img src="/assets/img/build-effective-ai-agents/prompt-chaining.webp" width="100%" height="100%"></p>

提示链将任务分解为**<mark>一系列顺序的子任务</mark>**，

* 每个 LLM call 处理前一个 LLM call 的输出；
* 可以在中间任何步骤添加检查点（图中的 “Gate”），以确保处理过程仍在正轨上。

### 3.1.1 适用场景

适用于能**<mark>干净地将任务分解为固定子任务</mark>**的场景。

背后的逻辑：相比于一整个大任务，**<mark>拆解后的每个 LLM call 都是一个准确率更高、延迟更低、更容易完成的任务</mark>**。

### 3.1.2 场景举例

#### 生成营销文案

生成营销文案，然后将其翻译成不同的语言。

#### 按大纲编写文档

首先编写文档大纲，确保大纲符合某些标准，然后根据大纲编写文档。

## 3.2 路由（Routing）

<p align="center"><img src="/assets/img/build-effective-ai-agents/routing-workflow.webp" width="100%" height="100%"></p>

通过路由对输入进行分类，并将其转发到**<mark>专门的后续任务</mark>**（specialized followup task）。

* 将任务的关注点进行拆解，从而**<mark>针对每个具体任务设计和调整提示词</mark>**。
* 否则，（all-in-one）提示词不仅很长，而且针对任何一种任务的提示词优化都可能会导致其他任务的性能下降。

### 3.2.1 适用场景

* 适用于**<mark>存在不同类别的复杂任务</mark>**，而且这些类别分开处理时，都能得到更好的效果。
* 前提是**<mark>能够准确分类</mark>**，至于是使用大模型分类，还是使用传统模型/算法分类，关系不大。

### 3.2.2 场景举例

#### 智能客服

将不同类型的用户问题（一般问题、请求退款、技术支持）转发到不同的下游流程、提示和工具。

#### 大小模型路由

将简单/常见问题路由到较小的模型，如 Claude 3.5 Haiku，将困难/不寻常问题路由到更强大的模型，如 Claude 3.5 Sonnet，以优化成本和速度。

## 3.3 并行化（Parallelization）

<p align="center"><img src="/assets/img/build-effective-ai-agents/parallelization-workflow.webp" width="100%" height="100%"></p>

多个任务同时进行，然后**<mark>对输出进行聚合处理</mark>**。考虑两个场景：

1. 分段（Sectioning）：类似 MapReduce，将任务分解为独立的子任务并行运行，最后对输出进行聚合。
2. 投票（Voting）：相同的任务并行执行多次，以获得多样化的输出。

### 3.3.1 适用场景

分为两类：

1. 并行化可以提高任务的最终完成速度，
2. 需要多种视角或尝试，对所有结果进行对比，取最好的结果。

背后的逻辑：如果一个复杂任务需要考虑很多方面，那针对每个方面单独调用 LLM 效果通常会更好，
因为每个 LLM 都可以更好地关注一个具体方面。

### 3.3.2 场景举例

#### 旁路安全检测

属于 Sectioning。

一个模型实例处理用户查询，另一个模型实例筛选是否包含不当的内容或请求。这通常比让同一个模型实例同时请求响应和安全防护效果更好。

#### 大模型性能评估的自动化

属于 Sectioning。

针对给到的提示词，每个 LLM 调用评估模型不同方面的性能。

#### Code review

属于 voting。

几个不同的提示审查并标记代码，寻找漏洞。

#### 生成的代码的质量评估

属于 voting。

评估输出的代码是否恰当：使用多个提示词，分别评估生成的代码的不同方面，
或通过不同的投票阈值，以平衡误报和漏报（false positives and negatives）。

## 3.4 编排者-工作者（Orchestrator-workers）

<p align="center"><img src="/assets/img/build-effective-ai-agents/orchestrator-workers-workflow.webp" width="80%" height="80%"></p>

在这种 Workflow 中，一个中心式 LLM 动态地分解任务，将其委托给 worker LLM，并汇总它们的结果。

### 3.4.1 适用场景

适用于无法预测所需子任务的复杂任务。例如，在编程中，修改的文件数量。

虽然在拓扑上与 Parallelization Workflow 相似，但关键区别在于其灵活性 —— 子任务不是预先定义的，而是由协调者/编排者根据特定输入确定的。

### 3.4.2 场景举例

#### Code review

编程产品：每次对多个文件（数量不确定）进行修改。

#### 智能搜索

搜索任务：从多个来源收集和分析信息。

## 3.5 评估者-优化者（Evaluator-optimizer）

<p align="center"><img src="/assets/img/build-effective-ai-agents/evaluator-optimizer-workflow.webp" width="100%" height="100%"></p>

在这种 Workflow 中，一个 LLM call 生成响应，而另一个提供评估和反馈，形成一个闭环。

### 3.5.1 适用场景

**<mark>有明确的评估标准，并且迭代式改进确实有效</mark>**（可衡量）。

两个适用于此模式的标志，

1. 当人类给出明确反馈时，LLM 响应可以明显改进；
2. LLM 也能提供此类反馈。

类似于作家写一篇文章并不断润色的过程。

### 3.5.2 场景举例

#### 文学翻译

承担翻译任务的 LLM 可能没有捕捉到细微差别，但承担评估任务的 LLM 可以提供有用的批评。

#### 复杂的搜索任务

需要多轮搜索和分析以收集全面信息，评估者决定是否需要进一步搜索。

## 3.6 AI Workflow 小结

Workflow 是基于增强型大模型的一种应用形式，可以帮助用户**<mark>将任务分解为更小的子任务，以便更好地处理</mark>**。
虽然 Workflow 也有一些动态的能力，例如路由和并行化，但这种程度的动态能力还是**<mark>预定义的</mark>**。
下面将出场的 AI Agent，则在动态上与此完全不同了。

# 4 Agent

随着 LLM 在关键能力上的不断成熟 —— 理解复杂输入、进行推理和规划、可靠地使用工具以及自动从错误中恢复 ——
人们开始将 Agent 应用到生产环境中。

## 4.1 原理

Agent 一般从下面场景收到任务并开始执行：

1. **<mark>收到明确的人类指令</mark>**；
2. **<mark>与人类交流到一定程度时，理解了自己接下来应该做什么</mark>**。

一旦任务明确，Agent 就会独立规划和执行，中间也可能会问人类一些问题，以获取更多信息或帮助它自己做出正确判断。

* 在 Agent 执行过程中，对它来说最重要的是每一步执行之后，都能**<mark>从环境中获得“真实信息”</mark>**（例如工具调用或执行代码），以帮助它评估任务的进展。
* Agent 可以在检查点或遇到障碍时暂停，然后**<mark>向人类获取帮助</mark>**。
* 任务通常在完成时终止，但也可以包括**<mark>停止条件</mark>**（例如最大迭代次数），以避免 Agent 行为不可控。

## 4.2 抽象层次：Agent vs. LLM

Agent 可以处理复杂的任务，但其**<mark>实现通常很简单</mark>** ——
它们通常只是一些**<mark>“在一个循环中，基于环境反馈来选择合适的工具，最终完成其任务的大模型”</mark>**。
因此，给 Agent 设计工具集时，其文档时必须清晰，否则这些工具大模型用起来可能会效果欠佳。

附录 2 介绍了工具开发的最佳实践。

## 4.3 何时使用 Agent

首先，必须**<mark>对大模型的决策有一定程度的信任</mark>**，否则就不要用 Agent 了。

其次，Agent 的自主性使它们非常适合**<mark>在受信任的环境中执行任务</mark>**。
Agent 的自主性质意味着更高的成本和潜在的错误累积。建议在沙箱环境中进行广泛测试，并设置适当的保护措施。

场景：难以或无法预测需要多少步的**<mark>开放式问题</mark>**，以及无法 hardcode 处理路径的情况。

## 4.4 Agent 设计三原则

在实现 Agent 时，建议遵循三个核心原则：

1. Agent **<mark>设计的简洁性</mark>**。
1. Agent **<mark>工作过程的透明性</mark>**，例如能明确显示 Agent 的规划和步骤。
1. 通过完善的文档和测试，**<mark>精心设计 Agent 与计算机之间的接口</mark>**（agent-computer interfaces, ACI）。

开源框架可以帮助你快速入门，但落地生产时，要**<mark>极力减少抽象层，尽量使用基本组件</mark>**。
遵循这些原则，就能创建出强大、可靠、可维护并受到用户信任的 Agent。

## 4.5 场景举例

<p align="center"><img src="/assets/img/build-effective-ai-agents/coding-agent.webp" width="100%" height="100%"></p>

我们自己的 Agent 例子：

* 一个解决 [SWE-bench tasks](https://www.anthropic.com/research/swe-bench-sonnet) 任务的 Coding Agent：会根据任务描述对多个文件进行编辑；
* 我们的 [“computer use” reference implementation](https://github.com/anthropics/anthropic-quickstarts/tree/main/computer-use-demo)，其中 Claude 大模型使用计算机来完成任务。

# 5 总结

本文介绍的内容，不管是 Workflow 还是 Agent，都是一种**<mark>模式，而不是规范</mark>**，
开发者可以组合和改造这些模式来实现自己的 AI 系统。
成功的关键，是**<mark>能衡量系统的性能，然后不断对实现进行改进和迭代</mark>**。

大模型领域的成功并**<mark>不是构建最复杂的系统，而是构建符合你需求的系统</mark>**。
从简单的提示词开始，不断评估和优化，只有在简单的解决方案真的解决不了问题时，才应该考虑引入 multi-step agentic systems。
或者换句话说，**<mark>只有在性能有明显改善时，才应该考虑增加复杂性</mark>**。

# 致谢

Written by Erik Schluntz and Barry Zhang. This work draws upon our experiences building agents at Anthropic and the valuable insights shared by our customers, for which we're deeply grateful.

# 附录 1：真实 Agent 举例

本附录介绍在我们的客户案例中，两个特别有价值的领域。

我们与客户的工作揭示了两个特别有前景的 AI Agent 应用，展示了上述模式的实际价值。
这两个应用都说明了 Agent 在满足以下条件的任务中非常有价值：

* require both conversation and action
* have clear success criteria
* enable feedback loops
* integrate meaningful human oversight

## A. AI 客服

AI 客服将聊天机器人与工具集成到一起。这是非常典型的开放式 Agent 场景，因为：

1. 客服场景天然就是对话流程，同时需要访问外部信息和执行行动；
1. 可以集成工具以获取客户数据、订单历史和知识库文章；
1. 行动（如退款或更新工单）可以程序化处理；
1. 通过用户反馈，可以明确衡量成功与否。

几家公司在 usage-based pricing models 中展示了这种方法的可行性，在这种定价模型中，
他们仅在 **<mark>AI 客服成功给出用户解决方案时才收费</mark>**，
显示出这些公司**<mark>对这种 Agent 的效果非常有信心</mark>**。

## B. Coding Agent

软件开发领域展示了 LLM 功能的显著潜力，功能从代码补全发展到自主问题解决。
Agent 在编程领域特别有效，因为：

1. 代码解决方案可以通过自动化测试来验证；
1. Agent 可以使用测试结果作为反馈来迭代解决方案；
1. 问题空间是明确定义和结构化的；
1. 输出质量可以客观衡量。

在我们自己的实现中，Agent 现在可以仅根据 Pull Request 描述，就能解决
[SWE-bench Verified](https://www.anthropic.com/research/swe-bench-sonnet)
中的真实 GitHub 问题。

不过，虽然自动化测试能验证功能，但还少不了人类 review，这对于确保解决方案与更系统要求的对齐至关重要。

# 附录 2：工具的提示词工程（Prompt engineering your tools）

无论构建哪种 Agent/Workflow ，工具很可能都是其中重要的组成部分。
[工具](https://www.anthropic.com/news/tool-use-ga)能让我们在使用 Claude 时，以标准 API 的方式指定工具的结构和定义，Claude 就能与外部服务和 API 进行交互。
当 Claude 响应时，如果它计划调用工具，它将在 API 响应中包含一个 [tool use block](https://docs.anthropic.com/en/docs/build-with-claude/tool-use#example-api-response-with-a-tool-use-content-block)。

**<mark>工具的定义和规范</mark>**（tool definitions and specifications）
**<mark>也需要提示工程</mark>**，需要给到足够的关注度。

本附录接下来介绍如何通过提示工程来描述你的工具。

## 输出格式的选择

同一个 action，通常可以有不同的实现方式。例如，

* 修改文件：可以通过提供 diff，也可以直接重写整个文件；
* 结构化输出：可以用 markdown，也可以用 JSON 格式。

在软件工程中，这样的差异问题不大，几种格式都可以无损转换。
但**<mark>对于大模型来说，某些格式的输出比其他格式更难</mark>**。例如，

* 输出 diff 格式，需要知道在新代码之前，前面改动了多少行；
* 输出 JSON 格式，需要额外处理字符转义问题（相比 markdown）。

## 建议

我们对工具输出格式的建议如下：

1. 给模型足够的 token 来“思考”，从而避免它进入死胡同；
1. 文本的输出格式，与此类文本在互联网上的常见格式保持一致，因为大模型就是在互联网数据上进行训练的；
1. 确保没有任何格式“开销”（例如需要准确记录几千行代码，或对代码进行转义）。

一个经验法则：在人机界面（HCI）上投入了多少努力，就在 agent-computer interfaces（ACI）上投入同样多的努力。
如何做到这一点：

1. 换位思考，多**<mark>站在模型的角度思考问题</mark>**。

    * 根据给定的描述和参数，**<mark>作为自然人是一看就懂，还是需要思考一下才能判断</mark>**？自然人是什么反应，模型也很可能是什么反应。
    * 一个好的工具定义通常包括示例用法、边界情况、输入格式要求以及明确与其他工具的界限。

2. 如何**<mark>重命名参数或改进文档，使工具的描述更简洁直白</mark>**？可以将这个过程当做为团队中的新人编写一个优秀的 docstring。当工具很多而且存在一些类似时，这一点尤其重要。
3. 测试**<mark>模型如何使用你的工具</mark>**：运行一些示例输入，看看模型犯了什么错误，并进行迭代。
4. **<mark>工具的防呆</mark>**（[Poka-yoke](https://en.wikipedia.org/wiki/Poka-yoke)）。

我们在构建 SWE-benchAgent 时，实际上花在优化工具上的时间比在整体提示上的时间还要多。
例如，我们发现模型在 Agent 移出根目录后仍然会使用相对文件路径，导致调用工具出错。
为了解决这个问题，我们将工具的设计改为永远使用绝对文件路径。

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
