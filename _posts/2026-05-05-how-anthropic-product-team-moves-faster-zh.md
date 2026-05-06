---
layout    : post
title     : "[译] Anthropic 的产品团队为什么能比其他公司更快（2026）"
date      : 2026-05-05
lastupdate: 2026-05-05
categories: ai anthropic
---

### 译者序

本文整理翻译自 2026 年的一档播客
[How Anthropic’s product team moves faster than anyone else | Cat Wu (Head of Product, Claude Code)](https://www.youtube.com/watch?v=PplmzlgE0kg)，
嘉宾是 Claude Code 的产品主管 Cat Wu。

文中多次提到"产品品味"，这一点可以 callback [关于 AI 下半场的思考（二）：商业/应用篇（2025）]({% link _posts/2025-07-06-ai-2nd-half-2-zh.md %})：

> AI 使得执行力不再稀缺，那以后工作的关键是什么
> 1. 你要做什么（主观能动性，Agency）
> 2. 你选择什么（品味，Taste）

水平及维护精力所限，译文不免存在错误或过时之处，如有疑问，请查阅原视频。
<strong><mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark></strong>。

以下是译文。

----

* TOC
{:toc}

----

# 1 Anthropic 的 PM 角色是什么样的？

> 主持人：介绍一下你在团队中的角色，你和 Boris（Anthropic 创始人）是如何合作和分工的？在这个团队里 PM 的角色是什么样的？

作为 Head of Product，我很幸运能和 Boris 合作。他是一位非常棒的 thought partner，也是我们的 tech lead，更是产品愿景
(<strong><mark><code>product visionary</code></mark></strong>) 的核心制定者，
例如<strong><mark>他非常擅长定义 3~6 个月后产品该是什么样子</mark></strong>。

## 1.1 老板定 3～6 个月愿景，PM 拆成可执行计划

* 我的工作很大一部分，就是搞清楚<strong><mark>从今天到 Boris 定的 3~6 个月愿景之间的路径</mark></strong>是什么。
* 我大部分时间花在<strong><mark>跨职能协作</mark></strong>上：确保 marketing、sales、finance、capacity 等各个
  团队都认可这个计划，大家朝同一个方向努力；<strong><mark>一旦功能就绪，发布路上没有任何阻塞</mark></strong>。

## 1.2 方向和思路一致，分工存在一定模糊区间

这种分工在大部分方面都运作得不错，因为我们基本上是思路是一致的。
但实际上<strong><mark>这条分工线相当模糊</mark></strong> ——

* 大概 80% 的事情我们看法一致，
* 剩下 20% 里，有些我更在意，我就主导；有些他更在意，他就主导。

# 2 Anthropic 的 PM 岗位需要什么思维？

> 主持人：Anthropic 大概是现在大家最想去的公司了。
> 你之前跟我说，你看到<strong><mark>很多人理解的如何做好一个 AI PM 其实是错误的</mark></strong>。
> 能聊聊你观察到了什么，大家需要理解的到底是什么吗？

在 AI 之前，技术变化相对很慢。

* 那时候写代码的成本非常高，功能发布的节奏相对较慢，也依赖多个团队并行，协作开发；
* 你可以按 6 到 12 个月的时间跨度做规划。
* 当时 PM 的工作重心更多是<strong><mark>和所有兄弟团队协作</mark></strong>，
  确保他们每次正常发布之后，我们这边就少了一个阻塞项。

有了 AI 之后，工程效率大幅加速，模型能力提升得也非常快，

* 我们很多产品功能的交付周期从 <strong><mark>6 个月压缩到了 1 个月，1 周，甚至 1 天</mark></strong>。
* 这种节奏下，我们必须确保产品能<strong><mark>快速发布</mark></strong>。
  这意味着作为 PM，重点应该从<strong><mark>和兄弟团队对齐多季度路线图</mark></strong>
  转向<strong><mark>如何用最快的方式把东西推出去</mark></strong>。

怎么在产品矩阵里开辟一个"概念试验区" (concept corner)，
让工程师或 PM 有了想法以后，当周之内就能交到用户手里？

## 2.1 思维一：快速行动（Moving fast）

我认为做 AI native 产品表现最好的 PM，需要满足两点：

1. 能缩短<strong><mark>"从有一个想法到把这个功能交到用户手中"</mark></strong>的时间，
2. 能定义清楚<strong><mark>"我的产品里哪些功能必须做到'开箱即用'"</mark></strong>。

拿我们团队来说，我觉得第一点是<strong><mark>设定清晰的目标</mark></strong>。
因为 LLM 太通用了，这本身就带来了很多模糊性：

* 我们到底在为哪些用户做这个产品？
* 要解决什么问题？
* 最重要的 use case 是什么？

一个<strong><mark>优秀的 PM</mark></strong> 会这样回答：

* 我们的核心用户是专业开发者；
* 这个功能要解决的主要问题是 permission prompt 太多、用户感到疲劳；
* 我们的 use case 是，让企业里的专业开发者能安全地把 permission prompt 降到零。

这其实就<strong><mark>设定了一个相当清晰的目标</mark></strong>，
因为它<strong><mark>排除了大量其他可能的方案</mark></strong>，从而让用户能用一个 prompt 做成更多事。

## 2.2 思维二：建立一个快速上线新功能的机制

claude code 的做法是<strong><mark>几乎所有功能都先以 research preview 形式发布</mark></strong>。
上线时会明确打上这个标签，让用户明白这是一个早期产品，我们只是想收集反馈、持续迭代，并且这个功能未必会持续支持下去。

这样做<strong><mark>极大降低了我们发布一个东西时的承诺负担</mark></strong>，
从而能做到一两周内就能把一个新东西推向用户。

## 2.3 思维三：建立一个高效的上下游团队协作框架

什么时候拉哪些跨职能伙伴进来，对他们的期待是什么。
比如，我们 engineering、marketing、docs 团队之间有一套<strong><mark>非常紧凑的流程</mark></strong>：

1. 当工程师觉得某个功能就绪、并且内部已经 dogfood 过了，他们就把它发到我们的 evergreen launch room。然后,
2. 负责 docs 的同事、负责 PMM 的同事以及发布的同事就会进来，<strong><mark>第二天就能把 marketing announcement 搞出来</mark></strong>。

正是因为有这样紧凑的流程，才把任何一个工程师发布功能的摩擦降到了最低。
<strong><mark>搭建这套流程，正是 PM 该做的事</mark></strong>。

# 3 Anthropic 的 PM 还写 PRD 吗？

> 主持人：PRD 在以上流程里是什么位置？你们还写 PRD 吗？是只写几个要点就行，还是说这种东西在 AI 世界里已经完全演化成另一种形态了？

## 3.1 比 PRD 更重要的两件事

我们做两件事。

### <strong><mark>指标驱动</mark></strong>，每周通晒

<strong><mark>我们有非常严格的产品指标体系，并且每周向整个团队做一次 metrics readout</mark></strong>。

目的是让每个人都深刻理解我们业务的方方面面，包括关键目标是什么、当前走势如何、驱动因素是什么。

### 统一认知，符合团队的原则就可以自主决策，不受 PM 卡点

我们有一份<strong><mark>团队原则清单（team principles）</mark></strong>，
里面清楚地写了<strong><mark>我们的核心用户是谁、为什么是他们</mark></strong>。

* 把这些讲清楚是为了让团队每个人都理解我们的业务怎么运转、什么对我们最重要、我们愿意在哪些地方做取舍。
* 这样大家就能<strong><mark>自主决策，而不会被 PM 或其他干系人卡住</mark></strong>。

## 3.2 有时候也写 PRD：模糊功能、超大基建功能

我们有时候也写 PRD。

* 对那些<strong><mark>特别模糊的功能</mark></strong>，
  写<strong><mark>一页纸</mark></strong>把目标、理想 use case、当前需要修复的问题梳理清楚，在这种场景下确实是有帮助的。
* 偶尔会有一些项目——尤其是那些需要大量基础设施支持、要持续好几个月的——对这些情况，我们还是会写 PRD。

# 4 Anthropic 为什么能迭代这么快？

> 主持人：Anthropic 几乎每天都有一个重大功能或产品上线。有很多人怀疑：你们是不是用了最强的 Mythos 模型？除了这个，还有哪些原因？

其实<strong><mark>我们已经连续好几个季度保持这种速度了</mark></strong>。

## 4.1 确实有 mythos 的原因

mythos 是一个非常强的模型。我们确实在内部使用这些模型，这一点对我们发布速度是有帮助的，但<strong><mark>这不是迭代速度的大头</mark></strong>。

## 4.2 更重要的原因：上线流程简单，鼓励每个人都能"从想法到上线"

更重要的原因是
<strong><mark>上线流程和团队的预期</mark></strong>。

* 我们的<strong><mark>流程非常轻</mark></strong>，我们希望干掉发布路上的每一个障碍。
* 我们希望团队里的<strong><mark>每一个人都能觉得自己有权把一个想法在不到一周甚至一天之内推到世界面前</mark></strong>。

# 5 Anthropic 的 PM team 组织形式是怎样的？

我们有好几个 PM 团队，现在总共大概在 30 到 40 人左右。

## 5.1 research PM team

我们有 <strong><mark>research PM team</mark></strong>。

* 这个团队负责<strong><mark>客户对我们模型的所有反馈，把这些反馈传达给 research 团队去处理</mark></strong>；
* 这个团队也是 model launch 的主导者。

## 5.2 claude developer platform (CDP) team

<strong><mark>CDP team</mark></strong> 维护 claude code 所依赖的那些 API。

也负责诸如 managed agents 之类的能力——用户可以构建自己的 agent，我们帮他们 host。

## 5.3 claude code team

<strong><mark>claude code team</mark></strong>，既负责 claude code，也负责 co-work 的核心产品。

## 5.4 enterprise team

<strong><mark>enterprise team</mark></strong> 的职责是让 claude code 和 co-work 更容易被企业客户采购。
这里面包括成本控制、RBAC、安全控制等方方面面，目的是让企业客户在使用我们工具时非常有信心、非常放心。

## 5.5 growth team

<strong><mark>growth team</mark></strong> 负责整个产品矩阵的增长。

# 6 你觉得未来是需要更少 PM，还是更多 PM？

> 主持人：未来需要的 PM 会变多还是变少，两种观点：
> 1. 变少："为什么还需要 PM？工程师自己发布就行了。"
> 2. 变多：工程师推进得太快，每天都有新功能上线，PM 和设计师跟不上所有事情了，所以需要更多 PM。
>
> 你怎么看？

## 6.1 角色在融合，最高效的方式是招<strong><mark>有产品品味的工程师</mark></strong>，放手让他们去干

<strong><mark>我觉得所有角色都在融合</mark></strong>：PM 在做一部分工程工作，
工程师在做 PM 的工作，设计师既在做 PM 的事有时候也在写代码。

有两条路可选：

1. 多招<strong><mark>有产品品味的工程师</mark></strong>；

    我们团队选择是这种方式，好处是可以把产品发布的 overhead 降到最低。

    我们团队里有<strong><mark>很多工程师完全可以端到端地搞定需求</mark></strong>：
    在 Twitter 上看到用户反馈，当周末就把一个产品发布出去，<strong><mark>几乎不需要产品方面的介入</mark></strong>。

    我认为这其实是<strong><mark>最高效的产品发布方式</mark></strong>。

2. 工程招聘保持不变，但多招 PM 来指导他们的一部分工作。


## 6.2 产品品味仍然是一项非常稀缺的技能

我觉得工程师和 PM 其实是重叠的，多招哪一边都会有用。

但<strong><mark>产品品味仍然是一项非常稀缺的技能</mark></strong>：
只要我们认为一个人在这方面有强有力的证明，我们基本上都会招进来。

## 6.3 我们的几乎所有 PM 以前是都是研发，设计师也是

> 主持人：你的背景是工程师对吧？

我以前做了多年工程师。之后短暂做过一段 VC，然后加入了 Anthropic。

其实<strong><mark>我们团队几乎所有 PM 要么以前是工程师</mark></strong>，
即使以前不是工程师，现在也在 claude code 里实际写代码。
我觉得这是一个<strong><mark>能和团队建立信任的重要因素</mark></strong>，也让我们能快得多。

另外，<strong><mark>我们的设计师之前也都是前端工程师</mark></strong>。

## 6.4 工程背景转产品是个自然&有价值的事情吗？<strong><mark>再论产品品味</mark></strong>

> 主持人：很多人最关心的问题是——如果你的背景是工程、产品或设计，这三种核心技能
> 里哪一种最有价值？在 Anthropic 和 claude code，工程显然非常有价值。我很好奇在
> 其他公司是不是也一样。

我仍然觉得<strong><mark>归根到底还是产品品味</mark></strong>。

### 产品品味可以来自任何背景

* 随着写代码的成本大幅下降，<strong><mark>真正变得更有价值的是知道"写什么"</mark></strong> —— 这个功能的合理 UX 是什么？用户体验它时最愉悦的方式是什么？
* 我们每天收到<strong><mark>上万个 GitHub issue</mark></strong>，什么都有，需要很强的心思和品味，才能判断出哪些值得做、用什么方式做。

这项技能可以来自任何背景，但它是最重要的。

### 工程背景的一个好处：对"一件事应该有多难"更有直觉

我觉得<strong><mark>工程背景在接下来几个月里特别优势的一点是：对"一件事应该有多难"更有直觉</mark></strong>。

这对决定做什么、不做什么很关键。如果一件事很容易做，那不用争论，直接花一小时把它做了；
但如果一件事很难做，你事先就评估<strong><mark>这对团队来说代价有多大</mark></strong>。

## 6.5 <strong><mark>第一性原理</mark></strong>：判断技术格局正在如何变化、团队真正需要你做什么

我前面说工程背景的人"接下来几个月"特别有优势，但不是说一直有。随着时间推移，一定会发生大的变化。

> 主持人：你是说 mythos 一出来就会改变一切、我们就不再需要懂工程了？

不是，我只是说<strong><mark>每隔几个月，coding 能力似乎都会有一次大的跃升，然后各角色的价值就会经历一次重新洗牌</mark></strong>。

我觉得最重要的是<strong><mark>具备第一性原理思维</mark></strong>：能判断技术格局正在如何变化、团队真正需要你做什么、然后进去把那个洞补上。

* 工作正变得越来越<strong><mark>无定形（amorphous）</mark></strong>，
* 一个优秀的 PM 要能<strong><mark>看出所有的 gap、判断哪些最重要</mark></strong>，
  然后想清楚"我怎么学到那项技能"或者"我现有的哪些技能可以用到这个挑战上"。

所以我觉得当下的环境看重的是那些<strong><mark>能戴多顶帽子、能随时切换、并且对自己做什么工作没有执念（low ego）的人</mark></strong>——只要能帮团队更快。

### 达到 AGI 之前，人脑还有哪些发挥空间？

> 主持人：在我们到达 super intelligence 之前，人脑会在哪些地方继续发挥作用？我听你说的，本质上是<strong><mark>选择做什么、判断市场走向、决定优先级</mark></strong>；然后是判断你做出来的东西是不是好的、对的，并且至少把它的一个早期版本推出去。这样理解对吗？

我觉得<strong><mark>人类仍然能提供模型所不具备的那种常识</mark></strong>。

任何一次产品发布都有上千个大大小小的变化 —— 很多都很小，但总有大量的地方可能会出问题。
模型对"干系人是谁、他们彼此之间什么关系、各自的偏好、什么场合沟通最合适"这类事情，并不总是能判断得很到位。

### 大模型的情商仍待提高

那些<strong><mark>更偏隐性的常识、EQ 层面的知识，人脑仍然非常有价值</mark></strong>。
当然我们希望模型在这方面也能变得更强，它们也会变得更强，但目前仍然有差距。

### 如何跟上 AI 的变化？—— "这已经是未来世界最正常的一天了"

> 主持人：作为身处暴风眼的人，你怎么应对这种持续不断的变化？也许风暴中心是平静的，但你怎么持续跟上在发生的事？怎么在这种疯狂中保持清醒？

我们团队都是愿意拥抱当前这种混乱 (<strong><mark><code>lean into the chaos</code></mark></strong>) 的人。

* 我们尽量微笑着去面对每一个挑战，因为总有那么多事情在发生、那么多风险和棘手的情况——如果你对每件事都过度焦虑，一定会 burn out。
* 我们会找那种面对挑战会说"这会很难，但我很兴奋、我会尽最大努力，<strong><mark>我知道做不到完美，但我晚上睡得着，因为我已经尽力</mark></strong>"的人。

> 主持人：我忘记是谁说过——也许是 Ben Mann——<strong><mark>"这已经是未来世界最正常的一天了"</mark></strong>。

是的，只会越来越难。我感觉有很多周都是这样：周日晚上来了个 P0，周一又来一个 P00，周一下午来一个 P000——然后就会想："哇，我居然为周日那个 P0 担心过，真可笑。"

### 不影响核心功能就先上线 —— 我们发布的一些产品并没有我期望的那么精致

必须承认：你能做的事情是有限的。

<strong><mark>你需要睡好，才能在第二天做出好决定；你必须非常果断地排优先级，决定时间花在哪里</mark></strong>——最重要的是把什么事做对，并且要能接受放手。

我们发布的一些产品<strong><mark>并没有我期望的那么精致</mark></strong>。

* <strong><mark>回到第一性原理</mark></strong>，我们的首要目标是赋能专业开发者。
  因此<strong><mark>一个产品即使不完美，但只要没阻塞核心 use case，那就可以接受</mark></strong>——因为我们会收到反馈、在下个版本里修掉。
* "上线一个带着 bug 的功能"以前会让我彻夜难眠，但现在我能接受，
  因为我知道<strong><mark>我们能拿到快速反馈并在下个版本里把 bug 修掉</mark></strong>。

## 6.6 Anthropic 的人都非常 chill 和乐观

这是一个非常有意思的洞察，我们确实有这种平静和乐观，而不是"天啊一切都疯了、要崩溃了"。
如果没有这种特质，你会很快 burn out。

我们也倾向于招<strong><mark>在行业里已经做了一段时间、经历过很多起伏</mark></strong>的人——
他们对<strong><mark>什么能给自己带来能量、如何长期维持自己的能量</mark></strong>有很清晰的感知。这对我们帮助很大。

# 7 岗位融合之后，我们将失去什么？

> 主持人：现在各种角色正在模糊。在这样的世
> 界里我们会失去什么？会失去职业阶梯、清晰的晋升通道吗？会失去设计一致性、
> 代码质量吗？有哪些事情是你觉得"我们为了更大的目标正在牺牲掉"的？

## 7.1 <strong><mark>我们正在牺牲产品一致性</mark></strong>

我们在牺牲产品一致性。

### 写代码贵时：非常仔细地规划产品矩阵

<strong><mark>历史上，写代码是很贵的</mark></strong>，因此 PM 会<strong><mark>非常仔细地规划产品矩阵</mark></strong>里的一切：

* 每个产品之间的关系
* 每一个的 use case 是什么
* 怎么集成——基本上每个 use case 对应一个产品。

### 写代码白菜价之后：同时尝试多个可能，让用户帮我们选择产品的走向

现在 AI 迭代得很快、我们要验证的想法也很多，有时候会出现<strong><mark>多个功能互相重叠</mark></strong>。
很多时候是因为我们自己都喜欢或拿不定主意，因此<strong><mark>希望外部用户来告诉我们哪一个更好</mark></strong>。

## 7.2 大量堆功能的代价：对新用户不友好，老用户也可能跟不上

但以上方式<strong><mark>对新用户来说不够友好</mark></strong>，因为我们给了他太多选择，他不知道要完成一个功能用哪种方式最好。

因此，我们需要做更多的教程，帮大家理解核心功能是什么、最佳实践是什么。
这就是<strong><mark>大量堆功能的代价</mark></strong>。用户也会觉得跟不上最新的东西。

### 传统 PM：季度或月度交付功能

传统 PM 模式下，大概每月或每季度发一个功能。

用户很容易理解："每月看一次就行，学点新东西；就算半年不看也没事，不会觉得错过了什么。"

### AI PM：天级别交付

在 agentic 工具这个生态里——不只是 claude code 和 co-work，而是整个生态——<strong><mark>大家会觉得必须每天刷 Twitter 看最新的东西</mark></strong>。

## 7.3 `/powerup` 功能（新手引导）：功能太多更新太快，<strong><mark>"好的产品直觉到不需要教程"</mark></strong>不再成立

我觉得我们应该做更多的事情，让大家不觉得自己站上了<strong><mark>一个越来越快的跑步机上</mark></strong>。

我希望大家感受到的是<strong><mark>打开工具，工具自己会教你想知道的东西</mark></strong>——让他们感到是被带着往前走的。

> 主持人：对，我看到你们前几天发布了一个很有意思的功能—— `/powerup`，基本上会带你走一遍使用 claude code 的 cool 玩法和最佳实践。这是不是就是你说的那个方向？

是的，就是这个方向。过去我们其实不愿意做像 PowerUp 这样的东西，因为我们觉得<strong><mark>好的产品应该直觉到不需要任何教程</mark></strong>。

但随着时间推移，我们意识到<strong><mark>功能真的太多了</mark></strong>，
大家对一个内置的 onboarding 体验有非常强的需求，所以我们从"不做 onboarding flow"的原初原则上稍微偏了一点，加入了这个功能。
因为确实有非常多的用户想知道："这里有 100 个功能，其中我必须要用的 10 个是哪些？"于是我们就把它做出来了。

# 8 Anthropic 为什么能脱颖而出？

> 主持人：Anthropic 在 B2B 企业市场非常成功，
>
> * 传统上 B2B 并不是"一堆东西往外发"——通常最多每季度一次  release，几乎是"每天一个新东西"的反面。
> * 另一方面，Anthropic 这一路的运势简直像来自另一个世界。刚起步时远远落后。融资最少的公司之一、没有渠道、不是最先出手的那一个；OpenAI 遥遥领先，当时看起来 Anthropic 根本没可能在长期竞争中占到一席之地。而现在它做得非常好——以这种增速击败各路大公司的团队。

## 8.1 最重要的两件事情

Anthropic 能这么成功、能从后面追上来、做得这么好，有两件最重要的事情。

### 使命：带给全人类的是一个安全的 AGI，最高原则，招人硬门槛

第一件是<strong><mark>统一的使命（unifying mission）——它的重要性怎么强调都不为过</mark></strong>。

我们招的是那些<strong><mark>最认同"把安全的 AGI 带给全人类"的人</mark></strong>。

* 在决定整个产品矩阵该重点发布什么时，我们会非常频繁地参照这一条。
* 我们把这个使命放在<strong><mark>任何一条具体产品线之上</mark></strong>，
  所以我们能做出<strong><mark>横跨整个组织的快速决策</mark></strong>，并以一种统一的方式去执行。

在我们这个规模的公司里，据我所知这是独一无二的。

我们的头号使命是 <strong><mark><code>safety alignment</code></mark></strong>，是让 AI 对世界有益。有这样一条清晰的使命，决策就会容易很多。
例如，如果有两个优先级在竞争，<strong><mark>我们会回到"哪一个对 Anthropic 的使命更重要"</mark></strong>。

这让"二选一"变得容易得多，大家都会选择同一边。
有时候这意味着："我们本来想发 claude code 的某个东西，但另一件事更重要——那这个就降优先级、晚点再做。"

### 专注：做好最核心的业务场景，不发散

> 主持人：有意思。这正好解释了和 OpenAI 的不同。
> 你说的本质上是："我们不做社交网络、不做信息流，因为它们不符合这个使命。" 正是这一点让 Anthropic 保持了专注，而专注似乎是成功的核心要素之一。

当我谈使命的时候，我想的是把 <strong><mark>Anthropic 的目标放在任何个人、任何一条产品线之上</mark></strong>。

所以对我来说，我们擅长的第二件事是<strong><mark>专注</mark></strong>。

### 使命和专注的区别

在我的理解里使命和专注稍有不同——
<strong><mark>使命意味着团队愿意做出那种伤害自己目标和 KR，去服务 Anthropic 的目标和 KR</mark></strong>，
并且大家非常乐意做这种取舍。

#### 极端例子：如果 claude code 失败了但 Anthropic 成功了，我们都会非常高兴

举一个极端例子：<strong><mark>如果 claude code 失败了但 Anthropic 成功了，我会非常开心</mark></strong>——整个团队也都非常愿意按这个思路做决策。

## 8.2 禁用 OpenClaw 的决定，是否与此冲突？

> 主持人：不知道你能不能深入聊这件事——你觉得禁用 OpenClaw 的决定是不是出于这种考虑?

这件事没有在推进 Anthropic 的使命，所以我们停止了对 OpenClaw 的支持，因为它并没有按我们希望的方式在工作。

我觉得对 Anthropic 而言，<strong><mark>最重要的事情之一是增加我们能触达的用户数量</mark></strong>。其中一条路径就是通过 Claude 订阅和第一方产品。
我们非常希望在这个方向上加倍投入，但这有时<strong><mark>会以牺牲第三方产品为代价</mark></strong>。

# 9 你分别在什么场景下使用 claude code, desktop, co-work?

## 9.1 Claude Code

我一般是在<strong><mark>要启动一个一次性的 coding 任务、并且想用上所有最新功能时</mark></strong>，用 claude code。
它是一个命令行工具，CLI 是我们最初的产品形态，也是新功能通常最先落地的地方，所以它是所有工具里最强的。
我在同时启动一个、或者少数几个任务时，更倾向用它。

## 9.2 Claude Desktop：前端开发，preview

desktop 最亮眼的场景是<strong><mark>前端相关的工作</mark></strong>。

我特别喜欢用我们的 preview 功能——如果我在写一个 web app，我经常用 claude code on desktop，把 preview pane 固定在右边，这样一边和 Claude 聊，一边<strong><mark>实时看到自己在做的那个 web app</mark></strong>。它也非常适合那些希望界面更图形化一些的人。

对非技术用户来说，terminal 相当陌生——会冒出一堆吓人的弹窗，并且不能像其他产品那样自由点击。所以有很多人在 terminal 里就是不自在。
如果你是这种人，我强烈推荐 claude code on desktop。

它是一个<strong><mark>一站式的 control plane</mark></strong>，你能看到所有任务。
web 和 mobile 版本的价值则是<strong><mark>在路上也能启动任务</mark></strong>。CLI 和 desktop 都要求你在本地笔记本前。

## 9.3 co-work：管理邮箱、做 PPT 等

处理 Slack 到 zero、邮箱到 zero；做 slide；写文档等。

这些任务的输出都是非代码的，<strong><mark>co-work 最适合这类场景</mark></strong>。

我大脑里的产品划分是这样的

* <strong><mark>输出是代码</mark></strong>，用 claude code 或 desktop 或 claude code on mobile；
* 输出是非代码的东西，用 co-work。

如果你刚开始用 co-work，第一件要做的事是<strong><mark>把你所有的数据源都连上</mark></strong>，
这会<strong><mark>大幅提升结果的质量</mark></strong>。

* co-work 只有拿到足够的 context，才能帮你产出好的东西。
* 对我来说就是把它连上我的 Google Calendar、Slack、Gmail、Google Drive。

### co-work 最佳实践举例

> 主持人：能不能分享几个你作为 PM 的 use case？在用 co-work 有哪些特别有意思、甚至出乎意料的用法？

我用它做的事情——比如昨晚我在准备一个叫 "Code with Claude" 的大会，我要做几个 talk，其中一个是讲 <strong><mark>claude code 从 assistant 到完整 agent 的演进</mark></strong>。
我想在 talk 里展示所有促成这一演进的产品，同时找出内部那些可以当作 demo 的成功案例。我把 Google Drive 和 Slack 连上了，
我们的 PMM Alex 草拟了一份他觉得应该覆盖的要点。我把这些全部丢给 co-work，告诉它我想讲的叙事。<strong><mark>然后它真的就独立工作了一个小时</mark></strong>。

它<strong><mark>扫了 Twitter 看我们发过什么、翻了 evergreen launch room 和 claude code announce 频道（团队发 demo 的地方）</mark></strong>，然后把所有信息综合起来，做成一份 20 页的 slide。今早我醒来读了一遍，相当不错。

一些细节要调，我给了它一轮反馈——我喜欢 slide 上的字极少，它做得有点啰嗦。
<strong><mark>而且因为 co-work 能访问我们整套 design system，它做出来的东西看起来就像 Anthropic 设计师亲手做的</mark></strong>。

视觉上一看就觉得"哇，这个非常精致"。这类事情现在快得多——这份 slide 如果我自己做要好几个小时，现在它给我出一个相当好的 draft，<strong><mark>我可以把时间花在确保里面的 demo 足够惊艳</mark></strong>。

> 主持人：你给它生成这份 slide 时大致用的 prompt 是什么？

大致内容：

```
帮我做一份 Code with Cloud 大会的 slide；这是我们 PMM 建议覆盖的内容；这是我自己写的一版 draft 我不喜欢；这是我手动做的一版我也不喜欢

请先产出一份带细节的候选大纲；并且确保它不要和 keynote talk 重叠太多，因为 keynote 更重要
```

然后 Claude 读了我给它的一堆链接，产出了一份候选大纲。
我把它的方案和所有它生成的备选想法过了一遍，<strong><mark>然后做出"最终 deck 里要放什么"的决定</mark></strong>。

我觉得这就是一个<strong><mark>今天 PM 角色的缩影</mark></strong>：

* Claude 是一个很棒的 brainstorming partner，能极快地综合海量信息、把所有可能性摆给你；
* 但"最终应该放什么到产品里"这个决定，仍然是 PM 的角色。

我最终的决定是：这个 talk 要覆盖这样一条演进——<strong><mark>从让本地任务成功，到让每个 PR 都绿，再到帮工程师 land 更多 PR</mark></strong>；并且为每一步挑出最有说服力的 demo。这个大纲定了之后，co-work 就自己跑了几个小时，把整份 slide 做出来。

## 9.4 与视觉设计的集成

> 主持人：design system 这部分你是怎么做的？它是怎么知道 Anthropic 的 design system 的？

我是这样做的——我们其实已经有<strong><mark>一份用于所有对外场合的标准化 deck</mark></strong>。
我把它给 Claude 访问权，它就能看到我们用什么颜色、什么字体、有哪些可选的 slide 格式。
这份 deck 里大概有 20 种示例 slide。

你也可以连 Figma MCP——如果你的 slide 格式存在那里，它可以从那边拉进来。

# 10 在 Anthropic，token 消耗大户（团队）都是干啥的

> 主持人：在 Anthropic，除了工程团队之外 —— 我猜工程是最大的 token 消耗方 —— 哪个团队第二多？这会很有意思。

## 10.1 Applied AI 团队

Applied AI 团队，他们在 co-work 和 claude code 上消耗都非常大。

<strong><mark>Applied AI 团队在拓展 claude code 和 co-work 边界上做的很不错</mark></strong>。

* 他们很多人会花时间和客户一起工作，帮他们落地我们的 API。
* <strong><mark>有时候会代客户做 prototype</mark></strong>——而 claude code 让这件事比以前快得多。
* 他们还同时要管理大量客户沟通、客户 inbound、以及历史的通话记录和 context。

> 主持人：applied AI 是不是类似 "forward-deployed engineering" 的角色？它的工作大概怎么描述？

对，它的职责是<strong><mark>帮客户在公司内部落地最新的 API 和模型能力</mark></strong>——既用来驱动客户自己的产品，也用来加速客户的内部工作。

> 主持人：懂了——所以它像是一种 customer success / GTM 的 forward-deployed engineering 的角色？

完全正确。它是一个非常技术化的 GTM 人员。

### 举例

我们也看到他们在把 co-work 的边界往外推。比如——他们很多人同时对接多个客户，忙的时候一天可能有 5 到 10 场客户 engagement。他们经常用 co-work 做的一件事情是：<strong><mark>前一晚让 co-work 做一份总结</mark></strong>——明天我有哪些客户会议？这个客户此前问过什么？他们最关心什么？上次会议的 action item 是什么？co-work 会把这些整合成一份<strong><mark>"进会议前该知道什么" 的简报（dossier）</mark></strong>。

co-work 还能去找答案——如果客户问"feature X 什么时候发布"，co-work 可以在 Slack 里帮这位同事查到最新 ETA，写到笔记里；这样客户 call 的时候，这位同事手里就是绝对最新的信息。这些都是大家自己搭的工作流，然后分享给团队其他人。

## 10.2 token 费用超过自己的工资

> 主持人：最近有个话题经常被提起——有些人用 token 花费已经超过了他们自己的工资。Anthropic 内部有没有一些数据，比如工程师或 PM 每月、每天花多少 token？

我们非常清楚地看到 <strong><mark>随着模型变强，大家委派给它的任务越来越多，在 claude code、co-work 这类工具里花的小时数也越来越多</mark></strong>。

每当有一次模型跃升或重大产品改进，我们就能看到 <strong><mark>每个工程师、或者每个 knowledge worker 的 token 成本都在上升</mark></strong>。
现在整体还远低于一个普通工程师的平均薪资，但这个占比在持续上升。

## 10.3 你们的 token 量有限制吗？

我们的 token 上限非常高，但也有限制，有些人确实会撞到上限。

# 11 作为 Anthropic PM，你的技能栈是哪些？

> 主持人：作为 Anthropic 的 PM，你的工具栈大概是什么样？除此 claude 系列你还在用什么？

我重度依赖 claude code 和 co-work。Anthropic 很大程度上跑在 Slack 上，
我觉得它是我们公司的<strong><mark>"核心操作系统"</mark></strong>。

## 11.1 大量使用 co-work，对它哪里不够好有非常强的直觉

日常工作里，我大概有 30% 的时间花在"把 co-work 的能力边界往外推"上，
这样我<strong><mark>对"我们哪里还不够好"会有非常强的直觉</mark></strong>。

## 11.2 大量和 claude 对话，理解它为什么会那些犯错误

我花很多时间和模型对话，去理解它为什么会犯它所犯的那些错误。

## 11.3 Claude code 极大地降低了"做一个自定义 app"的门槛

我们其实自己做了很多内部工具——我觉得 claude code 为整个公司解锁的一件事情，
是<strong><mark>它极大地降低了"做一个自定义 app"的门槛</mark></strong>。

我们看到的结果是：<strong><mark>个性化的工作软件在激增</mark></strong>——大家在为自己的定制 use case 做工具，而不是忍着用那些并不完全贴合需求的现成工具。

> 主持人：有哪些具体例子？你自己或别人做过哪些特别受欢迎、特别有用的东西？

claude code 销售团队里有一位同事，他意识到自己在<strong><mark>反反复复做结构一样的 deck</mark></strong>。
所以他做了一个 web app：里面放着那些效果很好的 deck 模板。
然后他的 web app 支持把客户的 context 输入进去，例如从 Salesforce 或其他笔记软件里拉，就能针对具体客户定制这份 deck。

正常情况下这是一个要花 20~30 分钟的手工活。而<strong><mark>有了这个工具，几秒钟就能拿到一份量身定制的 deck</mark></strong>。

## 11.4 不会重写一个 Slack，它有自己的核心竞争力

> 主持人：大家聊 Salesforce 时会说："我们不再需要 SaaS 软件了，我们自己做。" 但 Slack 是那种<strong><mark>没人想和它竞争、没人想去做一个"更好版本"的耐用工具</mark></strong>。

我觉得 Slack 是非常重要的一套通信基础设施 —— 在<strong><mark>"让每个人能拿到实时通知"</mark></strong>这个核心任务上它做得极好。

它还把<strong><mark>可定制、可 hack 做得特别容易</mark></strong>。
我们很爱写 Slack bot——这种可 hack 性意味着我们能按自己想要的方式和 Slack 集成。非常感谢 Slack 在这方面的工作。


# 12 你觉得 PM 应该关注哪些技能？

> 主持人：回到 PM 这个角色。<strong><mark>你觉得 PM 现在最需要发展的那些新兴技能是什么？AI 公司在招 PM 时最看重什么？</mark></strong>

## 12.1 最难的技能：<strong><mark>能定义未来一个月，你的产品应该长什么样</mark></strong>

<strong><mark>我觉得最难的技能，是能定义"一个月之后你的产品应该长什么样"</mark></strong>。

在这个时间尺度上，模型能力会变成什么样、用户行为会怎么变，都非常模糊。

### 很难，但<strong><mark>最好的 PM 能看出一些规律</mark></strong>

但<strong><mark>最好的 PM 能看出一些规律</mark></strong> —— 来自观察<strong><mark>"用户如何重度使用现有产品的边界"</mark></strong>。
他们能感受到方向、设定路径、稳步执行；并且在模型能力好于或差于预期时，<strong><mark>及时调整路径</mark></strong>。

## 12.2 给一个 super AGI 级别的模型做产品其实很容易 —— 难的是给当下这个模型做产品

我觉得 the right amount of AGI pilled 是一件很难的事情。
每个人都能看到这样一个未来：模型极度聪明、几乎什么都能做——在那种未来里，你其实根本不需要复杂的产品，一个文本框就够了，把你想要的告诉模型。

> Being "AGI-pilled" refers to a mindset centered on the belief that Artificial
> General Intelligence (AGI) is not just possible but inevitable. It often
> involves prioritizing or redesigning one's work, strategy, or worldview to
> account for a future where AI possesses human-level or superhuman cognitive
> abilities.

* 它聪明到能自己加任何需要的 tool 和 integration 来把事情做成；
* 它知道自己什么时候不确定，会主动问澄清性问题。

<strong><mark>给一个 super AGI 级别的强模型做产品其实很容易——难的是给当下这个模型做产品</mark></strong>：
如何激发它的最大能力？如何帮用户走上 golden path？如何引导用户去用模型的强项、同时弥补它的弱点？<strong><mark>这项技能相当稀缺</mark></strong>。

## 12.3 你如何打造这项技能？—— 花大量时间和模型对话、使用模型
> 主持人：那这项技能要怎么练？是靠大量使用每个模型、理解它们的边界吗？就像你说
> 的"taste"——对模型能做什么、强在哪、弱在哪、哪里变了，有一种直觉？

我觉得是<strong><mark>花大量时间和模型对话、使用模型</mark></strong>。

### 一、让模型反思它自己的行为，找到为什么不 work 的原因并解决

我特别喜欢做的一件事，是<strong><mark>让模型对自己的行为做内省（introspect）</mark></strong>。
比如我有时候会注意到模型做出一些出乎意料的行为——像是改完前端、跑了测试，但并没有真去用那个 UI。这时候让模型反思"为什么你这么做"是非常有用的。

有时它会说："system prompt 里有一段让我困惑"、"我没意识到前端验证也是这个任务的一部分"、"我把验证 delegate 给了一个 sub agent，但它没做、我也没 check"。
<strong><mark>很多时候，只要你对"模型为什么做了那个决定"保持强烈的好奇</mark></strong>，就能看到什么把它带偏了——然后你就可以改 harness 来把这个 gap 补上。

### 二、<strong><mark>找到你最信任的用户群体</mark></strong>，收集他们的真实反馈

另一件有帮助的事情是<strong><mark>找到"你最信任"的那群用户——他们能给你关于模型的准确反馈</mark></strong>。

通常会有那么一小撮人，他们在说清楚<strong><mark>某个模型或某个 model-harness 组合为什么好</mark></strong>这件事上比其他人强得多。
给你反馈的人会很多，但并<strong><mark>不是每个人的反馈同样有质量</mark></strong>。

<strong><mark>找到那么五个你信任的人，对拿到快速、高质量反馈非常重要</mark></strong>。

### 三、构建评估（evals），很多 PM 不愿意做

第三件有用但并不是每个人都喜欢做的事情是<strong><mark>构建 evals</mark></strong>。

<strong><mark>不需要上百个 evals —— 做 10 个足够好的 evals 就足以帮团队量化"目标是什么、目前进展如何、缺什么"</mark></strong>。

我觉得 eval 是一件<strong><mark>被低估的事情</mark></strong>，应该有更多 PM 和工程师投入到里面。

> 主持人：现在有一个趋势是——<strong><mark>"产品管理的未来就是写 evals"</mark></strong>——因为 evals 本质上回答的正是"成功是什么样子"。

把它具体地定义出来，然后我们能知道它工作地对不对，好不好。

> 主持人：你自己花在写 evals 上的时间大概占多少？

evals 的重要性要看你在做什么功能、要解决什么问题。
我们团队有不少人花大量时间做 evals。
我们有一个小团队来负责更精准地理解 claude code 的行为、找出最大的改进空间、并把这些东西具体地量化出来。

我个人会在<strong><mark>一个功能我觉得需要更明确的产品定义时</mark></strong>去做 evals。

#### PM 的 evals 输出

我作为 PM 的输出往往是这样：<strong><mark>"这是我做的五个 evals；这是运行方式；这些通过、这些没通过；这是我用来提升通过率的 prompt"</mark></strong>。

具体到每个功能差异很大——不是每个功能都需要，但像 memory 这样的功能从中获益很多。

#### evals 做的特别好的人/团队举例

> 主持人：有谁是你想特别表扬的、在这件事上做得特别好的人吗？

有两个人我觉得非常厉害。一个是 <strong><mark>Amanda，她负责塑造 Claude 的 character</mark></strong>——这是一个极难的角色，因为任务本身就非常模糊。

做 coding 更容易——因为很好验证。而塑造 character 需要你对<strong><mark>"Claude 应该是谁"</mark></strong>有极强的信念。
我觉得她不仅具备极强的塑造能力，还能把目标、character、什么算成功、什么不算成功清晰地表达出来。

另一群我非常信任的人，是整个 claude code 团队。
我们经常一起吃团队午餐，每次有新模型要测试的时候，<strong><mark>拿反馈最快的方式之一就是在午餐上问每一个人："你对这个模型的 vibe 怎么样？"</mark></strong>。
我们常会得到这样的反馈：

* "这个模型没把自己的 thinking 完全讲清楚，有点太突兀了"
* "这个模型特别喜欢写大量 memory，但我们不确定这些 memory 的质量是否高"
* "这个模型很喜欢自测自己的改动，这很棒"
* "这个模型自测得不够"

这些反馈会<strong><mark>告诉我们应该去看哪些数据来验证</mark></strong>，其实是不是有大机会或大问题。

* 我们手上有海量数据，但提取 insight 非常难；
* 这群人的反馈帮我们决定"要验证哪些假设"，然后我们才能从数据里抽取东西去验证。

# 13 Claude 的性格（character）

## 13.1 Personality 是 Claude 在很多任务上表现好的根本原因

> 主持人：很多人一开始没意识到 character 有多重要，直到后来——比如 OpenClaw 火了
> 之后，大家对比之下才发现，<strong><mark>Claude 的 personality 特别好、特别有趣</mark></strong>、和其他产品
> 很不一样。Ben Mann 的说法是，这种
> <strong><mark>personality 正是 Claude 在很多任务上表现好的根本原因。它看起来像一件"无足轻重的附加项"，但其实不是</mark></strong>。
>
> 主持人：这种"会风趣、会用有意思的方式说话"，看起来只是表面，但其实对 Claude 的成功至关重要。为什么 character 和 personality 这么关键，你有什么见解？

当你回顾你合作过的人，总有一些人你会觉得"我真的喜欢他们身上的那种能量、那种 vibe"。
大家谈到 Claude 和 claude code 时，提得最多的正是这一点——<strong><mark>他们很喜欢 Claude 轻松、有趣，同时执行能力又极强</mark></strong>。

## 13.2 Claude 的特质（灵魂）

人们特别喜欢 <strong><mark>Claude 的 low ego</mark></strong>。

* 如果你告诉它"你这里做错了"，它会真诚地道歉："啊糟了，谢谢你告诉我——我来修，咱们一起。" 它也非常正向。
* 如果你觉得"这任务难到无从下手"，Claude 会说："没事的——我觉得我们应该这样一步步来——要不要我先开始？"

<strong><mark>一个好的合作者的核心特质</mark></strong>，
恰恰是这种正向、bias towards action、愿意给你真诚反馈而不是对你说的每句话都附和。

我们试着把这些特质注入 Claude，因为我们认为这让和它一起工作变得更令人愉悦。

# 14 Anthropic 新模型发布前后的工作

> 主持人：你前面说每次新模型发布，你经常要回头重新审视你们之前做过的东西。
> 这很有意思，也可能有点崩溃——"该死，我们都发了这东西了，现在还得重新想一遍"。
> 每次新模型出来之后，你们要重做几个月前上线的产品的频率大概是什么样？

## 14.1 删掉不再需要的功能（模型的拐杖）

新模型出来以后，我们做的很多改动其实是<strong><mark>删掉不再需要的功能</mark></strong>。
很多功能是我们作为<strong><mark>模型的拐杖（crutch）</mark></strong>加上去的——因为它自己不会自发地这么做。

一个经典的例子是 to-do list。claude code 刚上线时，用户会让它做大规模 refactor，claude code 会说：
"好，我要改这 20 个调用的地方"，然后它改了 5 个就停了。我们就想："怎么强制它把这 20 个全改完？"

* 我们团队想：想一下人类会怎么做 —— 人会先列一个需要改的清单。
  就像在 VS Code 里查所有调用的地方，左边会出一个列表，你可以逐个过一遍、全部替换。
* 怎么给 Claude 一个这样的工具？" 于是他加了一个 to-do list，结果发现有了 to-do list 之后，Claude 真的能把这 20 个 call site 全改完。

但到了 Opus 4 以及之后的模型，我们发现<strong><mark>不再需要强迫它用 to-do list，它会自然地自己用</mark></strong>。
对更早的模型，我们得反复提醒它："to-do list 上的事都做完了吗？没做完之前你不能停"。

现在，to-do list 对用户仍然是一个"有了更好"的东西，因为你可以更清楚地看到 Claude 在做什么。
但说实话，<strong><mark>它在产品里已经被大大弱化了</mark></strong>——模型可能用，也可能不用，它已经不需要靠这个来完成彻底的修改了。

## 14.2 <strong><mark><code>"model will eat your harness for breakfast"</code></mark></strong>

> 主持人：我忘了谁说过 <strong><mark>"the model will eat your harness for breakfast"</mark></strong>。
> 我听你讲的本质上是——随着时间推移，你们在不断移除那些曾经加在模型之上的东西
> （为了"模型没按预期的方式工作"而加的 harness 工程）。随着模型变聪明，让它按预期工作会变得越来越简单。

是的。<strong><mark>每次模型变强，我们都能移除很多 prompting 干预</mark></strong>。
我们每次发布新模型时都会做这件事——<strong><mark>把整份 system prompt 从头到尾读一遍，对每一段去反思：模型真的还需要这条提醒吗？不需要就删掉</mark></strong>。

## 14.3 新模型解锁新能力

但新模型更令人兴奋的，是它们能<strong><mark>解锁全新的功能</mark></strong>。
有很多功能我们用更早的模型试过，但准确率还不够到可以发布。
一个例子是 code review ——

* 我们试过好几次构建 code review 产品，之前也发过更简单的版本，比如 `/code-review` 命令。
* 但直到最近这几代模型，我们才觉得：这个 code review 好到整个工程团队愿意在 merge PR 之前依赖它通过。

我们一直希望 Claude 能成为一个可靠的 code reviewer，能让我们有信心相信它捕捉到了绝大多数 bug。直到 Opus 4.5、4.6 和 Sonnet 4.6 这一代模型，
我们才能做到 <strong><mark>同时运行多个 code review agent，遍历整个 codebase，综合出一组"merge 前工程师必须处理的真实问题"</mark></strong>。

这就是最新模型解锁的新能力。

## 14.4 构建六个月之后的东西

> 主持人：另一个趋势是：去构建<strong><mark>未来六个月内可能会行得通的东西</mark></strong>。
> 先站到<strong><mark>刚好勉强能跑</mark></strong>的那条线，之后模型会追上来，那它就会变成一个惊艳的产品，你也会领先所有人。

### 构建那些"暂时还行不通"的产品非常重要

完全正确。<strong><mark>去构建那些"暂时还行不通"的产品非常重要</mark></strong>——
你能看清楚这个产品要 work 的话还缺什么。
新模型出来时，你只要把它换进已经做好的 prototype，看看这个新模型能不能把那个 gap 补上。

### Claude 有什么可以分享的中长期愿景

> 主持人：关于 claude 和 co-work 的长期愿景，感觉你们在不断往上加令人惊艳的功能
> ——从手机下发任务和控制，到各种 mobile app 的东西。有没有一个框架可以帮我们理
> 解这一切背后的长期愿景？

我们用 <strong><mark>building blocks</mark></strong> 来思考这件事。 
对 claude code 和 co-work 来说，

1. 最核心的 building block 是<strong><mark>让单个任务成功</mark></strong> —— 你想产出某个输出、给它一段清晰的 prompt，它能否稳定地产出可以接受的、你能直接 merge 或直接分享给同事/外部受众的输出？
2. 模型变聪明后，任务成功率大幅提高；然后我们看到大家开始<strong><mark>并行做多个任务</mark></strong>。
  2025 年末 multi-coding 是一个很大的趋势，之后只增不减。我们看到的是：单任务 work 了，现在你可以同时跑 6 个任务。
3. 随着模型进一步变聪明，我们的外推是：<strong><mark>下一步用户可能会同时跑 50 个甚至上百个 Claude</mark></strong>。
  那么支撑它需要什么样的基础设施？到了那一步，你大概<strong><mark>不会再把所有东西都跑在本地机器上</mark></strong>—— 内存根本不够。

所以我们在思考：<strong><mark>怎么让你更轻松地管理这一切？</mark></strong> 这些任务大概率会远程运行。

* 我们怎么设计界面，让你作为人类能知道"哪些任务需要我去看一眼"？
* 怎么确保 agent 完整验证了自己的工作，这样当你看到一个任务显示"完成"时，你能非常快地验证、并完全信任它确实按你的 spec 做完了？
* 怎么确保<strong><mark>这个流程是自我改进的</mark></strong> —— 当你看到一个任务做得不合心意，你给一个反馈，模型就能把这个反馈纳入之后每一次运行，再也不犯同样的错？

这就是我们正在带着用户往前走的那条路径。

# 15 对大家的建议，怎么挺过这次 AI 革命

> 主持人：你会给产品经理、创始人、跨职能人士等什么建议？
> <strong><mark>不只是"挺过"</mark></strong>向 AI 驱动世界的这次转变，而是在这个未来里真正成功？ 他们需要听到什么？需要做什么？

## 15.1 有重复多次的工作，应该想到用 AI 工具解决

<strong><mark>AI 给每个人带来的杠杆比以前大得多</mark></strong>。
所以我会这样 push 你：<strong><mark>每当意识到自己在重复做某件手动的工作，就想一想如何用 claude code、co-work 或其他 AI 工具把它自动化</mark></strong>。
大部分人的工作里都有一部分是"我真的很喜欢的创造性的那部分"，也有"我真的很讨厌的琐碎的那部分"。

AI 的美妙之处在于它可以 帮你做那些琐碎的部分 ——
它可以从你每一次手工完成这个任务中<strong><mark>学习、泛化，之后自动地跑</mark></strong>。
这样你就能专注于创造性的部分，能做的事情比从前多得多。

## 15.2 找出你工作里可以交给 Claude 的重复部分，把自动化成功率打磨到很高

所以我对大家最直接的建议是：<strong><mark>找出你工作里可以交给 Claude 的重复部分，把自动化成功率打磨到很高</mark></strong>——
然后去想，你还可以为你的团队、产品、公司多做些什么？比如那些一直没人有精力去接的事、或者你一直觉得公司应该做但自己没有带宽去做的 pet project。

如果 AI 能帮你搞定这些，你就能<strong><mark>比过去多出 20% 时间</mark></strong>。
所以我的建议是：拥抱这些工具，把你动力不足的工作交出去，搞清楚 AI 能如何加速，你能做的事情会越来越多。

## 15.3 从哪里开始？

> 主持人：这些工具的潜力巨大，但对很多人来说最难的部分恰恰是"我到底该做什么"。
> 核心建议就是"先为自己解决一个问题"。

以让 Claude 帮你整理和分析邮件为例——<strong><mark>你需要知道怎么定义一个 skill、怎么使用它并给它反馈、怎么告诉 co-work 基于你的反馈来更新这个 skill、以及怎么去读 skill 来确认反馈是否被按你想要的方式吸收了</mark></strong>。

让这个流程变得流畅、不让人感到痛苦，也是我们（作为产品团队）的工作。

我会强烈推大家去<strong><mark>构建那些你每天真的在用的 app</mark></strong>——因为只有通过真实使用，你才能拿到真正的价值。
如果你做的 prototype 并没有让你完成更多事情，那 AI 并没有给你真正加分。

只有真正做出来，你才能从里面学到东西。

## 15.4 避免两个方向的极端

我也注意到有很多人花大量时间折腾自己的 workflow。其实<strong><mark>有两个极端</mark></strong>。

* 是从不做任何定制、从不搭建自动化的人；
* 对"定制自己工具"近乎执念的人 —— 他们在工具上加一堆 skill、MCP，以及各种 workflow 改进。
  我觉得这有时甚至会让你偏离核心目标——比如发布一个产品、做完一个功能。

定制本身是很有乐趣的，我们也确实希望我们的产品非常可 hack，让你能把它打磨到非常适合自己。
<strong><mark>但"有用"是有极限的</mark></strong>。我觉得有一类人花在定制上的时间太多，以至于他们睡眠不足、偏离了自己最初想做的核心任务。

> 主持人：Karpathy 昨天发了一条推文，很有意思。他谈到了一种分化：一部分人当初试
> 过 ChatGPT / Claude——觉得"就那样"，甚至"太差了"——然后他们就放弃了 AI 能为自己
> 做什么的想法，变得非常 cynical，"这没什么大不了的"。另一部分人——主要是在用它
> 来写代码的——看到的是它的全部威力、有多强。两边彼此不理解对方为什么这么看世界。
> 所以你这里的建议很到位：<strong><mark>拿它去做真实的事情，看看它到底多有用</mark></strong>。

是的。我觉得<strong><mark>真正的转变是——2024 年那一代产品是 chat-based 的，而 claude code 这一代产品是 action-based 的</mark></strong>。

<strong><mark>人们最大的 aha moment 是——当 Claude 真的可以代替你去做事情</mark></strong>。
意识到 agent 不只是"告诉你该做什么"，而是"真的能自己去做"——这是一种非常震撼的感觉。我觉得这是让大家眼界被打开的时刻。

# 16 QA

## Q1：你最常推荐给别人的两到三本书是什么？

* 《How Asia Works》。讲的是经济发展，以及<strong><mark>哪些政策和政府造就了长期成功的经济体</mark></strong>。
* 《The Technology Trap》。讲过去几次技术革命——工业革命、计算机革命——以及这些革命是如何影响劳动者的。
* 《Paper Menagerie》。稍微轻松一点，一本短篇小说集，讲成长、AI 以及自我发现。

## Q2：最近看过、特别喜欢的电影或剧？

* 《Drive to Survive》，没有深意，但<strong><mark>看一群人对单一工程目标如此痴迷、这种纯粹的追求，本身就让人很满足</mark></strong>。
* 《Free Solo》—— 讲 Alex Honnold 无保护徒手攀登 El Capitan。
  同样地，能完成这样一条极其艰难、致命的路线，并且在"一个失误就会摔死"的前提下保持那样的心智专注——是一种纯粹的成就。

    我自己是个攀岩爱好者。第一次看《Free Solo》是在我自己攀岩之前——当时觉得很厉害，但没真正理解它有多厉害。它是少数几部<strong><mark>"你懂得越多，就越觉得疯狂"</mark></strong>的电影。他在那面墙上做的那些动作——就算挪到室内岩馆、离地只有一英尺——我觉得这辈子我都做不出来。

## Q3：最近让你爱不释手的产品？

除了 Claude 系产品之外，<strong><mark>最改变我生活的产品大概是 Waymo</mark></strong>。
我是 Waymo 的死忠，每天上下班各用一次。我喜欢它的两点：

1. <strong><mark>如果 Waymo 在等我，我不会觉得不好意思</mark></strong>。我不再有"它到了我必须立刻站在路边"的压力。
2. <strong><mark>它让我工作更有效率</mark></strong>。车里有人类司机时，我一般不会接工作电话——如果我全程在笔记本上干活，会觉得有点失礼。
  但在 Waymo 里，我可以直接进一个工作电话——不担心有人偷听、不担心失礼、不担心自己声音太大、不需要请人换一下音乐。
  <strong><mark>感觉这相当于每天给我多出了 30 分钟</mark></strong>。

我原本设想 Waymo 要比 Uber 和 Lyft 便宜才能成功，但实际上
<strong><mark>我愿意为它支付 2 倍的溢价</mark></strong>。

第一次见到它时你会想"哇这太疯狂了"。然后你很快就习惯了——坐进去："这太疯狂了"，然后就忘记了它的疯狂。

## Q4：你在工作和生活里的人生座右铭是什么？

<strong><mark>Just do things.</mark></strong>

我觉得<strong><mark>第一性原理思维非常有价值</mark></strong>——
如果你清楚自己在优化什么、并且有一套坚定的第一性原理，你通常就能推导出正确的任务拆解，
能把它清晰地讲给所有干系人——然后你就该直接去做。

<strong><mark>"岗位"其实是假的</mark></strong> (jobs are fake)——
如果你理解约束条件，你就能想清楚自己能做什么，<strong><mark>然后快速去做、从错误中学习、犯了错就道歉或者修</mark></strong>。

我觉得跟别人讲这句话，其实是一种<strong><mark>解放</mark></strong>。

在很多公司里，<strong><mark>角色被严格定义——PM 做什么、设计师做什么、工程师做什么</mark></strong>；甚至团队 scope 都是硬性划分的——"这块 codebase 我们改，这块我们不能碰"。
<strong><mark>"just do things" 让人们感到自己被授权去做决定、被授权跨越团队边界，只为把事情做成</mark></strong>。

> 主持人：这感觉是一项很重要的技能——大家叫它 agency、<strong><mark><code>bias towards action</code></mark></strong> ——总之就是"不要等允许"。

对。我觉得这是我最推荐<strong><mark>人生某个阶段去创业公司工作</mark></strong>的原因。在 Scale（AI）只有 20 个人的时候工作，那段经历改变了我的人生。
当时完全没有流程，但要解决的问题又非常大。我很感激 Alex 和整个团队，他们让我和其他人<strong><mark>没有任何"销售该做什么、运营该做什么、工程师该做什么"的边界限制，就把事情想清楚、做出来</mark></strong>。

你所有工具都在手边、摆在你面前的是一个宏大而棘手的问题，你可以做任何必要的事把它解掉。
<strong><mark>你几乎需要这样一段经历，才能养成那种"自在地跨界行动"的习惯</mark></strong>——因为很多人从小在学校、大学里，接受的都是"按我说的做、就能拿高分"的训练。

## Q5：Claude 的 thinking words

> 主持人：thinking words 都在那次源码泄露里曝光了。你有最喜欢的 thinking word 吗？

我很喜欢 <strong><mark>manifesting</mark></strong>——我最喜欢的贴纸上也是这个词。

## Q6：如果 AGI 在我们有生之年到来、你可能不必再工作，你会做什么？你会怎么打发时间？

我觉得 <strong><mark>AGI 扩散到整个社会会花很长时间</mark></strong>，
所以眼下真正要做的是<strong><mark>帮助整个世界跟上</mark></strong>。

如果真到了那一天，我的"不正经"回答大概是——我会去大量攀岩。
我大概会搬去 Fontainebleau，活在 10,000 块抱石之间，爬上一段时间。

还有很多书我想读——我的目标是<strong><mark>每周读一到两本，但现在大概是 0.5 本</mark></strong>，backlog 非常大。我觉得从历史里能学的东西太多了，还有很多领域我都没有像自己希望的那样理解得够深。
比如物理、机器人、任何硬件、航天——我都几乎一无所知。有太多有意思的话题。<strong><mark>即便知道 AI 已经懂得远多于我，我仍然兴奋于亲自去学这些东西</mark></strong>。

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
