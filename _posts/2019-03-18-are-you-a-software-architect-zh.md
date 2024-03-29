---
layout    : post
title     : "[译] 你是软件架构师吗？（InfoQ，2010）"
date      : 2019-03-18
lastupdate: 2022-01-07
categories: architect architecture
---

### 译者序

本文翻译自 2010 年 InfoQ 上的一篇英文博客
[Are You A Software Architect?](https://www.infoq.com/articles/brown-are-you-a-software-architect)

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 1 开发者和架构师

**<mark>软件开发</mark>**（software development）和**<mark>软件架构</mark>**（software
architecture）之间有一条微妙的界线。

一些人说，这条线根本不存在，**<mark>架构</mark>**只是开发者
**<mark>设计过程的简单延伸</mark>**（an extension of the design process）；
另一些人则说，这是一条巨大的鸿沟（a massive gaping chasm），只有少数出类拔
萃的开发者才能跨越 —— 为此必须**<mark>不断地向上抽象</mark>**（always
abstract your abstractions），而避免陷入令人生厌的实现细节的泥沼。

如果以务实的眼光看，那这两种意见之间必定存在某个平衡点，但这接着也引出了一个问题：
**<mark>如何从开发者变成架构师？</mark>**

将**<mark>软件架构</mark>**从软件设计和开发中
**<mark>区分开来的关键因素</mark>包括**：软件规模的上升、
抽象层次的上升，以及做出正确的设计决策带来的影响的上升等等。
软件架构就在于能有一个全局视角（holistic view）、能看到更大的图景
，以理解软件系统作为一个整体是如何工作的。但是，这些因素对区分“软件开发”和“软件架构”
也许有帮助，但还是无法解释**<mark>一些人如何从开发者成长为架构师</mark>**。另外
，它无助于识别出**<mark>哪些人将会成为出色的架构师</mark>**、如果你是 HR 如何
寻找这样的好苗子，以及**<mark>你是否是一个架构师</mark>**。

# 2 经验作为一个衡量指标

**<mark>经验</mark>**是一个很好的衡量指标，但对于这一点的理解不能浮于表面。

没有人是在一夜之间或一次升职就成为软件架构师的。**<mark>架构师是一个角色（role），
而非级别（rank）</mark>**。它是一个逐步的过程，在这个过程中你会不断增加承担这个
角色所需的经验与自信。

架构师身上有许多不同的品质，而他们过去的经验通常是承担这个角色所需能力的一种
很好度量（gauge）。架构师的角色包括很多方面，因此你需要更深层次地去观察和理解他们
在不同方面展现出来的**<mark>参与度、影响力、领导力和责任担当</mark>**。
宽泛地说，大部分项目的软件架构过程可以分成两个阶段：**<mark>定义阶段</mark>**和
**<mark>交付阶段</mark>**（the architecture is defined and then it's delivered）。

# 3 软件架构的定义（definition）

架构的定义过程似乎相当直接：**<mark>确定需求，然后设计一个满足这些需求的系统</mark>**。

但实际过程并不会这样简单：随着**<mark>参与程度</mark>**（how engaged you are）和
**<mark>对待这一角色的严谨程度</mark>**（how seriously you view your role）的不同，
软件架构的角色也有很大变化。如下图所示，角色的架构定义部分可以进一步分解为几
个子部分：

<p align="center"><img src="/assets/img/are-you-a-software-architect/role-definition.png" width="40%" height="40%"></p>

1. 非功能（non-functional）需求的管理
1. 架构定义（architecture definition）
1. 技术选择（technology selection）
1. 架构评估（architecture evaluation）
1. 架构合作（architecture collaboration）

## 3.1 非功能（non-functional）需求的管理

软件项目经常将注意力放在用户的**<mark>功能需求</mark>**（features）上，而很少问用户有什么
**<mark>非功能需求</mark>**（或系统性能）。有时需求方会告诉我们说“系统必须足够快”，但这种
表述太主观了。要满足非功能需求，那这些需要必须是**<mark>具体的、可测量的、能实现的
以及可测试的</mark>**（specific，measurable，achievable and testable）。

大部分非功能需求**<mark>本质上是技术性的</mark>**，而且通常对软件架构有很大影响。
**<mark>理解非功能需求是架构师角色的核心能力之一</mark>**，但作为架构师，准确理解
用户提出的非功能需求是一回事，是否要全盘接受则是另一回事。毕竟，你见过多少真正需要
7x24 小时运行的系统？

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-definition-1.png" width="60%" height="60%"></p>

## 3.2 架构定义（architecture definition）

弄清了非功能需求后，下一步就要思考如何定义架构，解决需求方提出的问题。
我们可以说**<mark>每个软件系统都有架构</mark>**，但**<mark>不是每个软件系统都有定义出来的架构</mark>**
（defined architecture）—— 这才是关键点。

软件定义过程需要思考如何在给定的限制下满足提出的需求，进而解决问题。架构定义过程
是在项目的**<mark>技术方面引入结构、规范、原则和领导力的过程</mark>**。定义架构
是架构师的工作，但是，**<mark>从头设计一个软件系统</mark>**和**<mark>扩展一个已有系统</mark>**
还是有很大差别的。

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-definition-2.png" width="60%" height="60%"></p>

## 3.3 技术选择（technology selection）

技术选择通常是一个愉快的过程，但是，当考虑到成本、授权、供应商关系、技术策略
、兼容性、互操作性、支持、部署、升级策略、终端用户环境等等问题时，挑战也是很大的。
这些因素综合起来，经常把一个简单的选择某些东西（例如一个功能丰富的客户端）的任
务变成一个十足的噩梦。

除了以上因素，还有另一个问题：这些技术能否工作。

**<mark>技术选择是管理风险的过程</mark>**（technology selection is all about managing
risk）；在高复杂度或不确定性的地方需要减少风险，在可能带来收益的地方允许适度引入风险。
技术决策需要考虑、评审和评估各种因素，包括软件项目的主要组成模块，以及开
发过程会用到的库和框架。如果你在定义架构，那你需要确信自己的技术选择是正确的。

此外，与“架构定义”类似，为一个新系统评估技术和向现有系统添加技术是有很大区别的。

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-definition-3.png" width="60%" height="60%"></p>

## 3.4 架构评估（architecture evaluation）

作为架构师设计软件时，需要首先问自己：我的架构能否 work？

对于我来说，**<mark>满足如下几点的架构就算是 work 的</mark>**：

1. 满足非功能需求；
2. 为其他部分的代码（the rest of the code）提供了必要的基础；
3. 为解决底层的业务问题提供了一个平台。

软件最大的问题之一就是它的**<mark>复杂性和抽象性</mark>**，这使得很难从 UML 图或
代码去联想出（visualize）软件的运行时特点。因此在软件开发的过程中，我们会采用多
种测试技术，以确保交付的系统在上线之后能正常工作。

为什么不对**<mark>架构设计</mark>**采用同样的方式呢？如果可以**<mark>对架构进行测试</mark>**，
就能证明它的有效性。这项工作做地越早，就越可以降低项目失败的风险，而不是寄希望于它能正常工作。

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-definition-4.png" width="60%" height="60%"></p>

## 3.5 架构合作（architecture collaboration）

与世隔绝的软件系统很少见，大部分软件系统都是需要人去理解它的。

* 开发人员需要理解它，并按照架构实现它；
* 需求方出于安全、数据库、运维、支持等角度，也可能对它的实现感兴趣。

要使软件成功，就需要与这些需求方紧密合作，保证架构能够和环境成功集成。不幸的是，
**<mark>开发团结与开发团队之间都很少讲求架构合作</mark>**，更遑论与外部的需求方了。

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-definition-5.png" width="60%" height="60%"></p>

# 4 软件架构的交付（delivery）

架构交付的部分也是类似，软件架构的角色会随着参与度（level of engagement）的不同
而不同。

<p align="center"><img src="/assets/img/are-you-a-software-architect/role-delivery.png" width="35%" height="35%"></p>

1. 把控全局（ownership of the bigger picture）
1. 领导力（leadership）
1. 培训团队和指导下属（coaching and mentoring）
1. 质量保障（quality assurance）
1. 设计、开发和测试（design，development and testing）

## 4.1 把控全局（ownership of the bigger picture）

要确保架构成功落地，必须得有人在软件开发的整个生命周期内把握整张大图、向大家兜售
前景（sells the vision）。如有必要，要跟随项目一起演进，承担将它成功交付的责任
。如果你定义了一个架构，那**<mark>始终保持对架构的参与和演进</mark>**是很有意义的，
而不是将它完全交给**<mark>“实现团队”</mark>**（implementation team）。

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-delivery-1.png" width="60%" height="60%"></p>

## 4.2 领导力（leadership）

把控全局是技术领导力的一部分，但软件项目的交付期间还有其它一些事情要做，包括
向教导大家责任（的重要性）、提供技术规范、做技术决策，以及具备做这种决策的权威。

作为架构师，你需要承担**<mark>技术领导力</mark>**，确保所有的事情都考虑到了，
团队走在正确的道路上。**<mark>软件架构师这一位置天然就是关于领导力的</mark>**，
这虽然听起来显而易见，但很多团队中架构师可能认为，成功的交付并不是一个他们需要考虑
的问题，因而并不具备所需的技术领导力。

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-delivery-2.png" width="60%" height="60%"></p>

## 4.3 培训团队和指导下属（coaching and mentoring）

培训团队和指导下属是大部分软件开发项目中**<mark>容易被忽视</mark>**的一项活动，
导致的后果就是，一些**<mark>团队成员并没有得到他们应该得到的帮助</mark>**。
虽然技术领导力是关于对项目整体进行掌舵（steering），但也有一些时候个人需要帮助。
而且，培训团队和指导下属提供了一种增强队员技能和提升他们职业生涯的方式。

这是架构师职责的一部分（this is something that should fall squarely within the
remit of the software architect），而且很显然，向团队**<mark>传授架构与设计技能</mark>**
与帮他们**<mark>解决代码问题</mark>**之间还是有明显区别的。

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-delivery-3.png" width="60%" height="60%"></p>

## 4.4 质量保障（quality assurance）

如果交付工作做的太差，那即使有世界上最好的架构和最强的领导力，项目仍然会失败。

质量保障是架构师角色中的很大一部分，但它远非仅仅是 code review。例如，需要有基准
的性能指标，这意味着需要引入标准和工作惯例（standards and working
practices）。从软件开发的角度讲，这包括编码标准、设计原则以及源代码分析工具
等等。

我们可以肯定地说，大部分项目的质量保障做的并不够，因此你需要辨别出哪些是重
要的，并优先保证这些部分被执行。对于我来说，一切对架构有重要影响的，或对业务非常
关键的，或复杂的，或高度可视化的东西都是重要的。你需要务实，意识到你无法保障所有
方面，但是做一部分总是比什么都不做好。

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-delivery-4.png" width="60%" height="60%"></p>

## 4.5 设计、开发和测试（design，development and testing）

软件架构师角色的最后任务就是设计、开发和测试。

作为一名工作在一线的架构师，并不意味着必须参与每天的写代码任务，而是说要持续地参
与到项目中，积极主动地去帮助打造和交付它。话已至此，那我们不禁要说问：
**<mark>为什么每天写代码不应该成为架构师角色的一部分呢？</mark>**

大部分架构师都是经验丰富的程序员，因此保持这项技能的状态是有意义的。除此之外
，架构师也会经历团队成员经历的痛苦，这有助于架构师们从开发的角度来看待和理解自己
设计的架构。一些公司明令禁止他们的架构师参与编码工作，因为觉得他们的架构
师太宝贵了，不应该从事编码这样初级的工作。显然，这种态度是错误的，如果你不让他
们参与成功交付的过程，那又为什么让他花精力设计架构呢？

当然，有些情况下让架构师参与到写代码的程度确实不太可行。例如，大型项目通常意
味着需要考虑很大的图景，因而不一定有时间参与到实现过程。但通常来说，
**<mark>写代码的架构师比只是旁观的架构师更加高效和快乐</mark>**。

<p align="center"><img src="/assets/img/are-you-a-software-architect/architecture-delivery-5.png" width="60%" height="60%"></p>

# 5 你是软件架构师吗？

不管将软件开发和软件架构之间的那条界线看作是神话还是鸿沟，本文讨论的内容都说明：
软件架构师在这个角色上的经验，都随着他们**<mark>参与项目的程度</mark>**和
**<mark>对待架构师这一角色的认真程度</mark>**而异。大部分开发者都不会周一早上醒来
就宣称自己是一名架构师了。我自己当然不是这样，我成为架构师也是一个逐步的过程。
其实，**<mark>一些开发者可能已经在承担部分软件架构师的角色了</mark>**，虽然他们的
title 上并没有体现这一点。

**<mark>参与</mark>**一个软件系统的架构（contributing to the architecture of a
software system）和**<mark>亲自设计</mark>**一个软件的架构之间有很大不同。
**<mark>持续精进的技能、知识和跨多领域的经验，成就了软件架构师的角色</mark>**。

跨越软件工程师和软件架构师的主动性在于自己，而首先要做的，就是理解自己目前的经验
所处的层次。
