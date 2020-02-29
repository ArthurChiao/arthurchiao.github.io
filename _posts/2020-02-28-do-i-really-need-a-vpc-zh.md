---
layout    : post
title     : "[译] 云原生时代，是否还需要 VPC 做应用安全？（2020）"
date      : 2020-02-28
lastupdate: 2020-02-28
categories: security cloud-native
---

### 译者序

本文翻译自 2020 年的一篇英文文章 [DO I REALLY NEED A
VPC?](https://info.acloud.guru/resources/do-i-really-need-a-vpc)。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

<p align="center"><img src="/assets/img/do-i-really-need-a-vpc-zh/blog-header.webp" width="75%" height="75%"></p>

> **从安全的角度来说，VPC 非但不是一种超能力，反而是另一层责任**（another layer
> of responsibility）。

准备在 AWS 上部署应用？那你需要一个 [VPC](https://docs.aws.amazon.com/vpc/latest/userguide/what-is-amazon-vpc.html)：
这种虚拟私有网络（virtual private network）能够保护你的应用免受来自公网的攻击，
就像它们部署在老式数据中心一样。**这是“虚拟机为王”** —— 即所谓
的 Cloud  1.0 （IaaS 浪潮） —— 时代**的指导哲学**。

但如今，当我在云上构建新应用或与同行交流类似主题时，我们通常都不会提到 “VPC”。这
是因为，人们越来越倾向于**将云原生应用（cloud-native applications）直接部署在更
高层的托管服务之上** —— 例如 Lambda、API Gateway 和 DynamoDB —— 这些服务通过 API
与彼此进行通信。在 AWS上，这种情况下的最佳实践是 **使用
[IAM](https://aws.amazon.com/iam/) 做认证和鉴权**，以保障微服务间的通信安全。

如果需要**将公有云和私有数据中心打通**，那 VPC 是不可或缺的。但**现代云原生应用
的安全，真的还需要 VPC 扮演关键角色吗？**在给出我自己的答案之前，我先陈述几位业
内专家的观点。

<a name="ch_1"></a>

# 1. 需求分析：VPC 是可选还是必需？

也许此刻你正在与安全团队一起，评估你们为**本地部署的应用**（on-prem applications
）设计的云原生方案【译者注 1】。你们的评估方式是：对照一个清单（checklist），逐
一检查方案是否满足其中列出的要求，满足的就打对勾（checking the box）。我们来听听
PurpleBox Security 公司的 Nihat Guven 对此怎么说：“**安全**（security）与**合规**
（compliance）在其中同样重要，二者互相追赶（playing catch-up）”。但是，**相比
于真正去思考 VPC 能否提供安全优势、能提供哪些安全优势**，“大家更多地将精力放在了
合规方面，即 —— **遵循既有标准，只要清单上列出了（例如，VPC），我们就做**”。

> 【译者注 1】
>
> On-premises 表示部署在私有数据中心。这个词来源于单词 “premises”，注意这是一个
> 独立的单词，并不是 “premise”（“前提、假设”）的复数形式（虽然 “premise” 的复数也
> 是“premises”）。单词 “premises” 表示“（企业、机构的）营业场所”，由此引申出两个
> 早期术语：
>
> * on-premises：本地机房
> * off-premises：非本地机房
>
> 到了云计算时代，公有云显然就是 off-premises 模式（不过没人这么叫）；与此相
> 对应，on-premises 指没有部署在公有云上的，一般就是公司自己的数据中心，不管是自建的
> 还是租赁的，也不管是自维护的还是托管的。On-premises 或 on-premises deployment
> 现在一般翻译为“本地部署”，虽然“本地”一词通常让人首先想到的是 “local”。

另一位 AWS hero Teri Radichel（即将出版的 ***Cloud Security for Executives*** 一
书的作者）赞同这样一种观点： **VPC 并没有什么神奇之处**。“VPC 实际上并没有做任何
事情”，她指出。“**你真正需要的是一个包含 NACL、子网和安全组的合理网络架构**。你
需要知道如何构建这样的架构，然后才能针对攻击做好监控。此外，你还要理解网络的各个
分层、攻击的种类，以及攻击者是如何渗透网络的。”

这引出了问题的关键所在：基于 IAM 的安全方案，其暴露面已经是理论上最小的；而为应
用添加 VPC 这件事情，最终都会变成在这个最小暴露面之外，再加额外的防护层（
adding layers of security to the theoretical minimum imposed by IAM）。**所以你
引入 VPC 并不是为了解决某个问题**，而是为了 —— 例如，增加额外的保护层防止数据渗透（
data exfiltration），或者能够对流量模式进行更细粒度的分析。

以上例子都说明，很多时候 VPC 只是可选项，而非必需。遗憾的是，很多工程师并没有理
解到这一层。

<a name="ch_2"></a>

# 2. 利弊权衡：额外的责任而非超能力

**从安全的角度来说，VPC 非但不是一种超能力（superpower），反而是额外的责任
（additional responsibility）**。

> “如果没有业务需求 —— 例如与私有数据中心互联 —— 那最好不要引入 VPC”，否则，“由
> 于 VPC 而引入的额外复杂性对安全配置来说非但无益，反而有害”。 -- Don Magee，前 AWS 安全专家

确实，**对于安全配置来说，越多并非永远意味着越好**（more is not always better）
。如果你连配置 IAM 角色都还没搞熟，那又如何相信你能做好 VPC 安全？如果你连S3
bucket 的 public 属性都不清楚，那又如何确定你能管好安全组、ACL 以及 VPC 引入的
subnets？

VPC 确实会带来一些额外的网络监控工具，例如 flow logs，但问题又来了：你知道如何高
效地使用这些工具吗？如果不知道，那就是在花大价钱抓数据，但又没有如何分析这些数据
的清晰计划。

另外，**并不是说引入了 VPC，它就自动为你的数据提供一层额外的防护**。正如 Magee
提醒我们的：“即使在 VPC 内，数据的保护也仅仅 HTTPS 加密 —— 就像你自己用 HTTPS 加
密一样。你觉得这种安全值得信赖吗？”。

<a name="ch_3"></a>

# 3. 云原生安全：模型抽象与安全下沉

云安全太难了！但我显然不是在鼓励大家因此而放弃。相反，正是因为云安全如此困难且重
要（both hard and important），我才建议你**不要轻易引入自己的网络控制方案**，而
应该**尽可能用好平台提供的安全能力**。

这听起来像是 “serverless” 的套路 —— 事实上，我们确实离此不远了。毕竟，如 AWS Lambda
项目的创始人 Tim Wagner [所乐于指出](https://medium.com/@timawagner/not-using-serverless-yet-why-you-need-to-care-about-re-invent-2019s-serverless-launches-c26fa0263d77)的，**所有 Lambda functions 默认都在 VPC 内运
行** —— 这种 VPC 是 AWS 托管的，因此比大部分人自维护的 VPC 要更安全（我们得
承认这个事实）。

这是目前大的技术趋势。AWS 仍然会维护主机层安全（host-level security），同时也会
提供更上层的服务，例如 AppSync 和 DynamoDB。但我并不是说网络安全在这些领域已经式
微了，而是说**越来越多的职责下沉到了云提供商那里**。你确实会**失去一些控制能力**
，但**换来的是 AWS 最佳实践的保驾护航之下，更快的应用构建速度**。

你可能会说，保护云原生应用的安全其实最后就是：“要么裸奔，要么上云”（letting go
and letting cloud）。确实，但这种职责模型转变（paradigm shift）是[传统的安全团队才需
要关心的](https://containerjournal.com/topics/container-ecosystems/comparing-serverless-and-containers-which-is-best/)
；对于用户来说，只需要用好这种优势，自然就会取得巨大收益。

<p align="center"><img src="/assets/img/do-i-really-need-a-vpc-zh/vpc.png" width="60%" height="60%"></p>

因此，尝试去建立你的威胁模型，理解你面临的风险，对你的团队进行恰当的培训。做完这
些你可能会发现，你最终还是需要 VPC，但那说明你是真的需要它，而不是为了合规或其
他需求而无脑地引入。

如果有一天，你的云原生蜘蛛侠（cloud-native Spidey）意识开始变得模糊，有一点还请
牢记：**有时候，责任越小，能力越大**（sometimes, great power comes from less
responsibility）。

----

全文结束，下面是译者广告。

年前翻译了一本 IBM 资深安全专家编写的云安全相关的书，介绍公有云平台上（AWS、
Azure、IBM Cloud）以及 Kubernetes（内容很少）的一些安全实践。如书名所示
，本书偏实践而非理论，有兴趣的可以关注主流图书电商平台（预计 2020.03 底能印出来
，但受疫情影响，时间也不好说）。

最后，**有兴趣的到时务必先看看目录，不要被上面列的名词误导** —— 尤其是：

* 这本书和这篇译文  **没有**  任何直接关系（例如，节选、同作者等等）
* 这本书关于 Kubernetes 的内容 **很少**

<p align="center"><img src="/assets/img/do-i-really-need-a-vpc-zh/pcs_cover_zh.jpeg" width="35%" height="35%"></p>
