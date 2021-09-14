---
layout    : post
title     : "[译] 基于角色的访问控制（RBAC）：演进历史、设计理念及简洁实现（Tailscale, 2021）"
date      : 2021-09-14
lastupdate: 2021-09-14
categories: rbac security
---

### 译者序

本文翻译自 2021 年的一篇英文博客：
[RBAC like it was meant to be](https://tailscale.com/blog/rbac-like-it-was-meant-to-be/)。

很多系统（例如 **<mark>Kubernetes</mark>**、AWS）都在使用某种形式的 RBAC 做权限/访问控制。

本文基于 access control 的发展历史，从设计层面分析了
**<mark><code>DAC -> MAC -> RBAC -> ABAC</code></mark>**的演进历程及各模型的优缺点、适用场景等，
然后从实际需求出发，一步步地设计出一个实用、简洁、真正符合 RBAC 理念的访问控制系统。

作为对比，如果想看看表达能力更强（但也更复杂）的 RBAC/ABAC 系统是什么样子，可以研究一下
[AWS 的访问控制模型](https://docs.aws.amazon.com/IAM/latest/UserGuide/access.html)。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

大部分人都听说过**基于角色的访问控制**（role-based access control, RBAC）以及它
的后继演进版**基于属性的访问控制**（attribute-based access control, ABAC），
但我们经常**<mark>遗忘或不懂得欣赏其中的伟大思想</mark>**。

大部分如今**<mark>常见的 RBAC 系统都经过了某种程度的简化</mark>**，因此比最初的设计要弱一些。
而本文想要说明，只要**<mark>回到 RBAC 最初的设计</mark>**，我们就能构建一个
[真正的 RBAC/ABAC 安全模型](https://tailscale.com/kb/1018/acls/)
，它比你能见到的那些系统更**<mark>简单而强大</mark>**，而且不管网络规模大还是小，它都能适用。

客户经常跟我们反馈说，他们如何震惊于如下事实：在 Tailscale 平台上，
**<mark>只用如此少的规则就能表达他们的安全策略</mark>**。这并非偶然！
但在解释为什么之前，我们先来回顾一些历史。

# 1 从 DAC 到 MAC

RBAC/ABAC 的概念和术语都源自几十年前的**<mark>美国军方</mark>**。
<a href="https://www.researchgate.net/publication/24164143_Role-Based_Access_Controls">Role-Based Access Controls (Ferraiolo and Kuhn, 1992)</a>
是一篇很好的介绍。下面来看一下它们的一些演进过程。

## 1.1 DAC（自主访问控制）：各文件 owner 自主设置文件权限

最早出现的是 DAC（Discretionary Access Control），直到**<mark>今天仍然很常见</mark>**。

### 设计

如下图所示，在 DAC 中 **<mark>object owner</mark>** 有权**<mark>设置该 object 的访问权限</mark>**。

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_01.png" width="60%" height="60%"></p>
<p align="center"> DAC：通过授予 individuals/groups 以 read/write/execute 权限，
object (file) 的创建者能完全控制该 object 的内容和权限。</p>

例如，

1. **<mark>在 Unix 系统中，设置 file permission</mark>**（“模式”，这也是 `chmod` change mode 的来历）
  就能授予别人 `读/写/执行` 这个文件的权限。
2. 在 Google Doc 中，点击 share 按钮能授予权限。

### 使用场景：普通用户的文件权限控制

* **<mark>军方</mark>**不怎么喜欢 DAC，因为这种方式中，**<mark>合规性很难保证，机密文件很容易被恶意 reshare 出去</mark>**。
* 但在**<mark>普通用户</mark>**场景中，这种方式还是很常用也很合理的。

## 1.2 MAC（强制访问控制）：（强制由）专门的 admin 设置文件权限

> 注意：不要把 MAC (mandatory access control) 与网络术语 "MAC address" 中的 MAC
> (media access address) 搞混了，二者没有任何关系，只是碰巧缩写相同。

### 设计：DAC 基础上引入专门的 admin 角色

MAC (Mandatory access control) **<mark>对 DAC 做了增强</mark>**。如下图所示，
由 **<mark>administrator</mark>**（管理员）或 **<mark>administrative rule</mark>**（管理员级别的规则）
来**<mark>定义 rules</mark>**。

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_02.png" width="70%" height="70%"></p>
<p align="center"> MAC：文件 owner 只能设置一个文件 type，这个 type 包含了哪些<mark>权限是由 admin 或 policy 设置的</mark>。
用户能编辑文件内容，但无法修改文件权限。</p>

因此在 MAC 模型中，**<mark>一个人做某些事情的
能力是无法再分享给其他人</mark>**的，从而避免了文件被 reshare 的问题。

### 例子：TCP/UDP 端口号

**<mark>MAC 很难解释</mark>**，因为在实际中很少看到它，甚至看到了之后，你都不觉得它是“访问控制”。

Wikipedia 给了一个很好的例子：TCP 或 UDP 端口号。当你占用了一个 local port 之后（假设没设置
<a href="https://man7.org/linux/man-pages/man7/socket.7.html">SO_REUSEADDR</a>），
这台机器上的其他任何人就都无法再用这个端口号了 —— 不管他们是什么级别的特权用户。
这里，**<mark>端口范围不可重叠这一条件，就是强制性的</mark>**（mandatory）。

### 适用场景：文档/系统访问控制

<a href="https://apenwarr.ca/log/20101213">之前关于 file locking</a> 的文章中，我讨论了
advisory locks 和 mandatory locks 之间的区别：

* advisory lock：**<mark>其他 apps 可以安全地读</mark>**这个文件；
* mandatory lock：按照规则，其他**<mark>不允许 apps 读任何内容</mark>**。

可以看出，MAC 适用于对**<mark>文档或系统的访问控制</mark>**，这就不难理解为什么
军方对 MAC —— 至少在理论上 —— 如此兴奋了。理想场景：

* 一个带锁的房间，门口有警卫站岗，
* 出示门禁卡能进入这个房间，
* 但警卫**<mark>禁止携带相机进入房间</mark>**。

在这种场景下，你自己有权限查看房间内的文档，但无法将其分享给其他人。

这个例子给我们的一个启示是：**<mark>数字系统中，MAC 在理论要比在实际中简单</mark>**
（easier in theory than in practice）。

* 一个功能完整的（full-on）MAC 系统是很难真正实现的。
* **<mark>Digital restrictions management</mark>** (DRM，数字限制管理) 是 MAC 的
  一种，在这种模型中，文件的**<mark>接收方无法再将文件分享给别人</mark>** —— 每个
  BitTorrent 用户都能体会到这种方式是如何奏效的。

## 1.3 MAC 之双因素登录（two-factor login as MAC）

大家可能没意识到，另一种 MAC 是 multi-factor authentication (**<mark>MFA or 2FA</mark>**):

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_03.png" width="70%" height="70%"></p>
<p align="center"> 2FA as MAC：密码可以共享，但硬件 token 不能。<mark>密码是 DAC，而硬件 token 是 MAC</mark>。</p>

用 MFA 能允许特定的人登录一台计算机或服务，如果这个人不是管理员（admin），那他
自己能登录，但将无法进一步将计算机共享给其他人，将密码告诉他们也不行。

**<mark>这种 login 是强制性的</mark>**（mandatory，单有密码不行，还必须有硬件 token 才能登录）。
在这个模型中，假设了第二因素（the second factor，即硬件 token）是不可分享的。

## 1.4 图片分享：DAC/MAC 模型比较

另一个例子是分享图片。

* 在某些服务中，任何有正确 secret URL 的人都能访问给定的图片/消息/文件，并且
  **<mark>任何有这个 URL 的人都能继续分享它，这是 DAC 模式</mark>**。

* 在另一些服务中，单有这个 URL 还不行，必须要**<mark>登录有权限查看这个文件的账号之后</mark>**，
  才能 reshare：**<mark>这 MAC 模式</mark>**。虽然某些人能通过特定的 URL 访问
  这个文件，但 reshre 这个 URL 并不能让其他人看到这个文件。

> 当然，如果一个人能下载这个文件，然后发送副本给别人，那结果还是泄露了这个文件
> 。这也是为什么一些人认为 secret URL 的安全性在数学上与 MAC 是等价的，因为现在
> 分享 URL 已经和分享文件一样难了。但二者有一个区别：
> 你可以关闭一个 URL 的共享，但无法追回一个已经发送出去的文件副本。

## 1.5 MAC 概念：限制太多，又好像没什么限制

历史上，军方中的 MAC 是围绕
<a href="https://en.wikipedia.org/wiki/Multilevel_security">multi-level security</a>
构建的，这里的**<mark>设计思想</mark>**是：
**<mark>并非只有 admin 和 non-admin 两种用户，实际上有很多层的访问</mark>**。
他们最初将其设想为同心圆（“最高机密许可”、“机密许可” 等等），但最后证明
表达力太弱（too unexpressive）。

如今的访问控制更像是**<mark>独立的 flags 或 subgroups</mark>**。例如，
<a href="https://en.wikipedia.org/wiki/Security-Enhanced_Linux">SELinux</a>
提供了对**<mark>每个进程内的每个权限</mark>**的细粒度控制，而传统 Unix/Linux
上只有 root 和常规用户权限的区分。但最终证明 SELinux 这套东西是**<mark>噩梦般的复杂</mark>**，
难以真正实用 —— 除非你在 **<mark>NSA</mark>**（发明 SELinux 的机构）工作，但即使
你在 NSA 也不一定会用。

最终来说，MAC 的概念证明是**<mark>过于限制又过于模糊</mark>**（both too restrictive and too vague）。
当人们谈论 MAC 时，我们很难搞清楚他们到底指的是什么，唯一知道是：这东西
**<mark>用起来非常让人抓狂</mark>**。

# 2 第一次尝试：基于 RBAC/ABAC

## 2.1 RBAC（基于角色的访问控制）

RBAC 是 **<mark>MAC 的一个子集</mark>**，它是一种特殊类型的 MAC，更加具体，因此
在讨论及使用上会更加方便。

RBAC **<mark>与常见的 users/groups 模型类似</mark>**。在 RBAC 中，

* **<mark>admin</mark>** 将某些 user 放到一个 group，然后
* 可以指定将**<mark>某些资源</mark>**（文件、计算机等）共享给**<mark>某个 group（role）</mark>**；
* 系统确保只有指定的 role 能访问指定的资源；
* 文件的接收方没有 reshare 权限 —— 除非拷贝一份，否则是无法 reshare 的。

## 2.2 ABAC（基于属性的访问控制）

<a href="https://www.researchgate.net/publication/273393378_Attribute-Based_Access_Control">Attribute-based access control (Hu, Kuhn, Ferraiolo, 2015)</a> 是**<mark>对 RBAC 的改进，加了一些细节</mark>**（属性，Attributes）。

* **<mark>属性</mark>**可以是位置、客户端设备平台、认证类型、用户的 http cookies 等。
* 当系统判断是否授予某个用户对某资源的访问权限时，ABAC 系统
  **<mark>除了检查他们的 RBAC role（group）</mark>**，还会检查**<mark>这个人携带的各种属性</mark>**。

如果你遇到过下面这种情况 —— 登录某个服务时弹出额外的**<mark>图片识别认证</mark>**
<a href="https://www.google.com/recaptcha/about/">reCAPTCHA</a>，
而你旁边的朋友登录时却不用 —— 就**<mark>说明你遇到了 ABAC</mark>**。

**<mark>ABAC 很有用</mark>**，因为这些额外的属性能给我们带来很多有用信息，尤其
是对于那些连接到互联网的、攻击矢量特别多的系统。但在概念上，ABAC 与 RBAC 类似，只是稍微向前演进了一点。
**<mark>属性的解析和认证</mark>**工作是**<mark>中心式的</mark>**，大部分都实现
在各家的 **<mark>identity provider</mark>** 中。有鉴于此，接下来我们的讨论重点扔将放在 RBAC。

## 2.3 也许你从未用过真正的 RBAC

RBAC 与前面提到的 users/groups 模型类似。接下来看一个具体的文件系统安全模型，例如 Windows。

> 这里也可以拿 Unix 作为例子，但经典 Unix 文件安全与常见的安全模型不同，
> 它只支持单个 owner、单个 group，以及 self/group/other 文件模式。
> 如今 Linux 也支持 <a href="https://www.usenix.org/legacy/publications/library/proceedings/usenix03/tech/freenix03/full_papers/gruenbacher/gruenbacher_html/main.html">facls</a>，
> 这算是 RBAC，但**<mark>没人知道怎么用</mark>**，因此这个也不算数。

### Windows 文件安全模型：每个文件一个 ACL

在 Windows 中，

1. **<mark>每个文件</mark>**（或目录）都有一个 **<mark>users 和 groups 列表</mark>**，以及
2. 每个**<mark>列表中的成员可以对这个文件做什么操作</mark>**。

这是一种访问控制列表（access control list，ACL）。**<mark>owner 设置 ACL，操作系
统执行 ACL。这是 MAC，对吧？</mark>**

对的 —— 大部分情况下。想一下，任何有文件读权限的人，都可以拷贝一份，然后在副本上
设置权限，因此这是**<mark>某种形式的 DAC</mark>**，或者说在执行上充满漏洞的 MAC。
但**<mark>在真实文件上</mark>**（而非 API 上）**<mark>执行 MAC 非常难</mark>**。
我们将这个难题留给军方，现在把关注点放在 **<mark>ACL 语义</mark>**上。

在一个 Windows filesystem ACL 中，有如下概念：

1. **<mark>User</mark>**：在这个文件上执行操作的用户。在经典 RBAC 术语中，称为 **<mark>subject</mark>**。
1. **<mark>Group 或 Role</mark>**：由管理员定义的一组 user。
1. **<mark>File</mark>**：需要做访问控制的资源（**<mark>resource</mark>**）。也称为 **<mark>object</mark>**。subject 对 object 进行操作。
1. **<mark>Permission 或 Entitlement</mark>**： 一条 `subject-action-object`（用户-动作-目标文件）**<mark>规则</mark>**。
  有时会说某个 subject **<mark>有</mark>**一条 entitlement，或者说某个 object **<mark>允许</mark>**某个
  permission，这两种表达方式本质上是一样的，只是从不同的角度描述。
1. **<mark>ACL</mark>**：一个 **<mark>entitlements 列表</mark>**。

### 控制谁能访问哪个文件

**<mark>每个文件都有一个 ACL</mark>**（permission 列表）。

> * 每个文件都有一个 ACL。该 ACL 可能从文件所在子目录的 ACL中继承某些 entry，也
>   可能不会，这些对我们目前的讨论来说不重要。
> * ACL 相同的文件，它们的 ACL 可能在磁盘上是分别存储的，这些是实现细节，我们这里
>   也不关心。

如果想**<mark>控制谁能访问这些文件</mark>**，可通过以下任一种方式：

1. 找到 ACL 对应的 groups/roles，在其中添加或删除 user（称为修改 group/role 的 membership）；或者，
1. 直接修改 ACL，添加或删除 permissions。

如果想**<mark>一次修改一组文件的 ACL</mark>**，可以

1. 修改 group/role membership（简单），或者
2. 找到所有相关文件，逐个修改对应的 ACL（慢且易出错）。

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_04.png" width="60%" height="60%"></p>
<p align="center">文件多了之后，逐个修改 ACL 就不切实际了。</p>

## 2.4 存在的问题：ACL 太多，到处重复，批量修改麻烦

最后一点，也是访问控制开始出现漏洞的地方。

* 几乎所有系统，不管是不是 RBAC，都支持**<mark>寻找文件系统中的 objects，然后修改它们的 ACL</mark>**，
  但配套的 object 管理系统可能做的很差。
* 在分布式系统中，这些 objects 可能分散在世界各地，放在各种不同的存储系统中，而
  它们的共同之处就是**<mark>都依赖你的 identity 系统</mark>**。
* 如果某天发现一个 permission 给错了，就必须找到这个 permission 的所有副本并解
  决之，否则就遗留了一个安全问题。但如果 objects 管理系统做得比较糟糕，这里做起
  来就会很麻烦。

# 3 第二次尝试：每个 ACL 对应一个用户组

被以上问题折磨多次之后，你可能会尝试一些新东西：

* **<mark>将尽量多的信息从 ACL（分散在各处）中移出</mark>**，
* **<mark>将尽量多的东西移入 user groups（集中式存储，而且能审计）</mark>**。

## 3.1 仍以 Windows 文件系统为例

仍然以 Windows 文件系统为例，如下图所示，你可能会创建两个 group `report-readers`
和 `report-writers`：

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_05.png" width="60%" height="60%"></p>
<p align="center">将尽量多的东西从 ACL 中移出，将尽量多的东西移入 groups 中。</p>

效果是：所有 reports 文件能被 `report-readers` 组内的用户读，能被 `report-writers`
组内的用户写。

> 经验不足的人在这里会犯的一个错误是：只创建一个名为 `report` 的 group，然后给
> 予这个 group read/write 权限。通常来说，**<mark>需要文件读权限的用户，要比需要
> 写权限的用户更多</mark>**。甚至在某些情况下，writer 和 reader 用户之间都
> **<mark>没有重叠</mark>**（例如审计日志场景）。

这种 per-file-type group（每种文件访问类型一个单独的 user group）结构是
**<mark>Don't Repeat Yourself</mark>** (DRY) 原则在实际应用中的一个例子：
上一节 RBAC/ABAC 模型中，根源问题是**<mark>每个文件都有自己的 ACL</mark>**，
这些 ACL 到处重复，因此这里**<mark>提取出了重复部分放到了一个公共的地方</mark>**。

## 3.2 存在的问题

这个改进比较合理，尤其是在有很多 objects 的大公司中工作良好，但也有几个问题：

1. 现在**<mark>需要有某种形式的 IAM admin 访问控制</mark>**，也就是对
   **<mark>用户组的增删查改</mark>**做控制。

    上一节的 RBAC/ABAC 模型中无需这种功能，因为它直接修改文件的 ACL。IAM admin
    管控带来的一个新问题是：

    * 如果管控太松，会导致很多人都有 IAM 的访问权限，存在风险；
    * 如果管控太紧，大部分都无权修改 group membership，又会使得这种模型的好处大打折扣。

1. End users 仍然能四处游荡，在需要时**<mark>能修改每个 report 文件的 ACL</mark>**
  （“Alice 真的真的需要查看这个文件”），破坏了你精心设计的系统 —— 而你自己都
  **<mark>无法察觉</mark>**。

1. 现在需要**<mark>为每个 ACL 组合创建一个 user group</mark>**。

    最后会发现，公司的每个工程师都属于 975 个 group，每个 group 都需要定义
    read/write 两种类型。你必须 review 每个 group 的 membership。这种方式虽然比
    老的 ad-hoc 文件权限方式审计性要好，但也好不了太多。

# 4 第三次尝试：重拾被忽视的概念：object tags

至此，我们决定**<mark>放弃文件系统的 ACL</mark>**，原因是：文件系统已经设计成这样了，
基于文件系统的 ACL 我们只能做到目前这样。你大概率无法解决现有的文件系统和操作系统中这些问题。

但接下来的好消息是：
<a href="/blog/modules-monoliths-and-microservices/">如今的服务都运行在无状态容器内</a>，
大部分 VM <a href="https://www.qubes-os.org/doc/vm-sudo/">都无需密码就能执行 sudo</a>，
因此我们不用再对文件系统进行控制，而是对 web 应用和 NoSQL 的 API 做控制。
这也许不是巧合，因为**<mark>对细粒度分布式安全</mark>**（fine-grained distributed security）
**<mark>的需求一直在增长，而文件系统还停留在 1980s 年代</mark>**。

那么，接下来就开始设计我们想要的 permission 系统！

## 4.1 根据 user type 而非 file type 创建 user group

首先，注意到，前面两节的文件系统 ACL 方案其实**<mark>并不是真正意义上基于角色的（role-based）访问控制</mark>**。
为什么呢？它把 user groups 作为 roles —— 这没有问题 —— 但如果你有 975 个像 `report-readers` 和 `report-writers`
一样的 group，那这些就不算不上是真正的 **<mark>human-relevant roles</mark>**。HR 并不知道
你的新员工是否应该是 report-reader，这个决策太底层了（low-level）。

因此我们得到的第一个启示就是：应该根据**<mark>用户类型</mark>**（user types）而非
**<mark>文件类型</mark>**（file types）来创建 user groups。如下图所示：

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_06.png" width="50%" height="50%"></p>

## 4.2 Roles 去扁平化，增强表达力：将 ACL 定义为一组策略规则

以上 group-per-user-type 格式还是**<mark>过于扁平</mark>**了（too flat）：它已经丢失了
“**<mark>为什么</mark>**某人会在某 group” 的语义含义（semantic meaning）。如果
Bob 离职了，我们必须修改所有可能包含 Bob 的 groups。这虽然已经比跟踪每个 `report` 类型的文件
然后 double check 它的 permissions 是否还正确要好，但仍然**<mark>很容易出错
</mark>**。

我们假设有如下角色（roles）：Accounting（审计人员）、DevOps（研发运维人员）、Engineering（工程师）、Executive（高管）。

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_07.png" width="50%" height="50%"></p>

然后我们就可以**<mark>将 ACL 定义为一组策略规则</mark>**（a set of policy rules）：

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_08.png" width="55%" height="55%"></p>

这种模型与最初的 flat 模型**<mark>表达的东西是一样的</mark>**，但通过增加一个间接层（indirection），
它表达了我们**<mark>一直想表达（而没有表达出来）的东西</mark>**。有了这个模型，
接下来就可以讨论：

* 由 HR 部门定义的 human-relevant roles，以及
* 由安全部门定义的标签（tags），以及
* 二者是如何联系到一起的。

## 4.3 关于策略规则的进一步解释

我们正在设计一个新的权限系统。

现在，先将刚才设计的**<mark>能转换成的 roles 的 policy rules</mark>** 进一步表示为：

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_09.png" width="50%" height="50%"></p>

有了这样一种格式的描述之后，当我们需要满足 SOC2 合规性要求时，只需将 `database`
的 readers 改为，例如 `[DevOps, Prod]`，这将会立即锁定所有数据库相关的对象。

## 4.4 其他特性

最后，我们来加两个其他特性：

首先，与文件只有一种 type（读或写）不同，一个对象可以有零或**<mark>多个 tags</mark>**。
因此，与数据库相关的源文件可以打上 `database` 和 `sourcefile` 两个 tag，对应地，
它获得的是两种 **<mark>permission set 的交集</mark>**。

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_10.png" width="50%" height="50%"></p>

第二，**<mark>只有 tag 的 owner 有权限增加或删除</mark>**任何对象上的**<mark>该 tag</mark>**。
例如在下图中，只有 Engineering 可以在某个对象打 `sourcefile` tag。
这能够避免意外将对象分享给应该完全隔离的人，或在不期望的地方错误地应用已有策略。

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_13.png" width="65%" height="65%"></p>

## 4.5 MAC 归来

至此，我们看到了 **<mark>MAC 回归的身影</mark>**。但是，现在它，

1. 不需要一个针对 security policy 的 global admin access control。
2. 每个 tag owner 能直接对他们的 objects 进行授权，但他们能授予哪些访问权限，是
   由整体上的安全策略（the overall security policy，即 **<mark>roles</mark>**）控制的。

## 4.6 例子：API 访问控制

在类似 Tailscale 的网络系统中，我们其实并不会用 readers和 writers 这样的文件系统术语。
我们**<mark>定义node 和 port，以及允许谁连接到这些 node 和 port</mark>**。
例如可能会如下规则：

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_11.png" width="55%" height="55%"></p>

有了以上规则，

1. Engineering 中的任何人都可以启动一个`dev-api-server` node，
2. 该 node 能接受从任何 `dev-api-client` node 来的非加密连接（TLS 太难了！开发环境就放行非加密连接吧），但反之并不亦然。
3. 只有 Ops 中的人能启动 `prod-api-server` 和 `prod-api-client` nodes，它们只处理 https 流量，拒绝非加密 http。

下面是效果：

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_12.png" width="65%" height="65%"></p>

这里注意：我们递归地用一些 tag names 来定义
permissions for other tags。Ops 中的某个人可以启动一个 node 并打上 `prod-api-server` tag，
这个 node 就会获得与 `prod-api-server` 而不是 Ops 相关联的 permissions 和 entitlements（
这很重要，因为`prod-api-server` instance 无法像 Ops 一样启动更多 instance）。

[真实的 Tailscale ACLs 和 tags](https://tailscale.com/kb/1018/acls/) 与此很像，但更加具体。

# 5 职责分离

## 5.1 根据 policy rules 和 user groups 自动生成访问权限

如果试图将这个模型反向适配到 legacy-style filesystem permissions，
我们就会发现 **<mark>roles 和 tag definitions 其实是相同类型的对象</mark>**（都是 lists of users），
二者之间通过一个（“安全策略”）算法进行单向转换：

<p align="center"><img src="/assets/img/rbac-as-it-meant-to-be/RBAC_14.png" width="60%" height="60%"></p>
<p align="center">将 roles 扩展成 tags，然后适配到传统文件系统的权限控制模型。</p>

你可以类似地写一些脚本，将给定的 roles 和 group membership rules
**<mark>自动生成你的 /etc/group 内容</mark>**，我知道有些公司就是这样做的。
这不是标准方式，维护很痛苦，而且通常用定时任务来批量执行，这意味着当修改
一个 tag 或 group membership 之后，必须要等上一段时间才能生效。但本质上来说，这
种方式是能工作的，而且比典型的操作系统默认值要好多了。

## 5.2 Tags 和 roles 各自的适用场景

前面说 **<mark>tags（用于 ACL 目的）</mark>**和 **<mark>roles（用于 user management 目的）</mark>**
都是“用户列表”（lists of users），其实这种说法有误导性。二者用于不同场景。最重要的是，
**<mark>不同的人</mark>**负责系统的不同部分：

1. **<mark>Roles</mark>** 描述的是 **<mark>identity system (authentication) 中的人</mark>**。
  Roles **<mark>变化很少</mark>**，通常在入职、晋升或转岗时由 HR 部门设置。
1. **<mark>Object types (tags)</mark>** 由 **<mark>object owner</mark>** 在这个 object 创建时设置。
1. **<mark>Entitlements</mark>** 用 `(Role, Tag)` 描述，由简单的程序（安全策略）来定义，由**<mark>安全团队设置</mark>**。

在这个架构中，这三种类型的人只有很少时候才需要交互：

1. Accounting 部门中的财报 writer 并不关心谁是 Executive，也不关心 Executive 是否
  有权查看或编辑财报。他们只需知道**<mark>要给 report 文件打上 financial-report tag</mark>**。
2. 安全团队并不关心哪个文件打了 `financial-report`（讨论一般情况下），也不关心谁是Executive。
  他们需要的是

    * **<mark>能读、写对应的安全策略，以及确保策略生效</mark>**：
    * **<mark>确保 financial-report tag 只能被 Accounting 部门打</mark>**，对应的文件只能被
      Executives 和 Accounting 读（read only）。

3. HR 团队不知道也不关心文件或安全策略，他们只关心**<mark>这周招了一个 Accounting role 的人</mark>**。

## 5.3 小结

回到 network permissions 场景：在大公司中，正确地围绕这些概念设计你的模型，就能避免大量摩擦。

我们在实际工作中可能会遇到如下类似的例子：工程师创建了一个新的开发（`dev`）集群后，
**<mark>还要去提个工单，让安全团队给他开防火墙端口</mark>**。为什么会这样？
因为在这些公司中，安全团队维护的策略并不规范，没有收敛到以上模型：

1. 允许 Engineers 运行 dev API servers，接受来自本机或 dev API clients 的 incoming 连接 —— 这个没问题；
1. 通常不允许创建 outgoing connections —— 这个也没问题；
1. 噢对了，Carol 的 dev API server 需要主动访问数据库服务器，只能开单独策略了 —— 问题来了。

如果安全团队能将这些安全规则固化成代码片段，结果将会更好，能确保它们在整张
网络上得到一致执行。

# 6 结束语

以上提到的所有东西，users、roles、object types、policies **<mark>都不是新概念</mark>**，
它们都来自 1992 提出 RBAC 模型的那篇论文，只是术语稍有不同。

如今，几乎每个人都在使用 users、groups、ACLs 了。一些人认为，我们实现的东西已经
是 RBAC，但事实告诉我们：并不是。**<mark>还没有谁实现过完整的 RBAC 模型</mark>**：

1. 每个人都是一个 User (subject)。
1. 每个 user 都有一个或多个 Roles。
1. 每个 object 都有一个或多个 Tags。
1. 一条 "security policy" **<mark>定义一个</mark>**将 `(Role, Tag)` 转换成 Entitlements 的
   **<mark>公式</mark>**。
1. 一个执行层（enforcement layer）负责 enforce security policy，并为每个 object 生成有效 entitlements 列表（ACL）。

但另一方面，实现这样一个模型比实现常见的 users+groups 模型**<mark>并没有复杂多少
</mark>** —— 只要**<mark>从一开始就将其放到系统的核心</mark>**。

最后回到文初，这就是为什么 [Tailscale RBAC、ABAC 和 security policy 不同寻常的地方](https://tailscale.com/kb/1018/acls/)。
Tailscale objects 都是设备和端口（devices and ports），而非文件，但所有概念在使用上与在文件系统中是一样的。
最终的产品在**<mark>理念设计上很简洁</mark>**：

1. Device 或 container 的 owner 可以设置 tag；
2. 安全团队决定谁 own 哪些 tag、每个 tag 关联了哪些 permissions、tags 会授权给哪些 roles；
3. Identity/HR 团队决定哪些 users 应该属于哪些 roles。

# 附录（译者注）：Tailscale 的安全策略模型

ACL rules 格式：

```shell
{
  "action": "accept",
  "users": [ list-of-sources... ],      # 广义的访问来源，相当于 RBAC 模型中的 users/subjects
  "ports": [ list-of-destinations... ], # 广义的访问目标，相当于 RBAC 模型中的 objects/resources
}
```

以上 json 中的 `users` 和 `ports` 都是为了兼容公司的历史 API，它们实际上包含的
范围要比字面意思大的多，具体见 [官方文档](https://tailscale.com/kb/1018/acls/)。
