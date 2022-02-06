---
layout    : post
title     : "[译] [论文] Raft 共识算法（及 etcd/raft 源码解析）（USENIX, 2014）"
date      : 2022-02-06
lastupdate: 2022-02-06
categories: raft etcd
---

### 译者序

本文翻译自 USENIX 2014 论文
[In Search of an Understandable Consensus Algorithm (Extended Version)](
https://www.usenix.org/conference/atc14/technical-sessions/presentation/ongaro)
，文中提出了如今已广泛使用的 Raft 共识算法。

在 Raft 之前，Paxos 几乎是共识算法的代名词，但它有两个严重缺点：

1. **<mark>很难准确理解</mark>**（即使对专业研究者和该领域的教授）
2. **<mark>很难正确实现</mark>**（复杂 + 某些理论描述比较模糊）

结果正如 Chubby（基于 Paxos 的 Google 分布式锁服务，是 Google 众多全球分布式系
统的基础）开发者所说：**<mark>“Paxos 的算法描述和真实需求之间存在一个巨大鸿沟，......
最终的系统其实将建立在一个没有经过证明的协议之上”</mark>** [4]。
对于大学教授来说，还有一个更实际的困难：Paxos 复杂难懂，但除了它之外，又没有其他
**<mark>适合教学</mark>**的替代算法。

因此，从学术界和工业界两方面需求出发，斯坦福大学博士生 Diego Ongaro 及其导师
John Ousterhout 提出了 Raft 算法，它的最大设计目标就是**<mark>可理解性</mark>**，
这也是为什么这篇文章的标题是《寻找一种可理解的共识算法》。

与原文的可理解性目标类似，此译文也是出于**<mark>更好地理解 Raft 算法</mark>**这一目的。
因此，除了翻译时调整排版并加入若干小标题以方便网页阅读，本文还**<mark>对照了</mark>**
[<mark>etcd/raft v0.4</mark>](https://github.com/etcd-io/etcd/tree/release-0.4/third_party/github.com/goraft/raft)
**<mark>的实现</mark>**，这个版本已经实现了 Raft 协议的大部分功能，但还未做工程优化，
函数、变量等大体都能对应到论文中，对理解算法有很大帮助。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 摘要

Raft 是一个管理**<mark>复制式日志</mark>**（replicated log）的**<mark>共识算法
</mark>**（consensus algorithm）。它的最终结果与 (multi-) Paxos 等价，也与 Paxos
一样高效，但**<mark>结构</mark>**（structure）与 Paxos 不同 ——
这使得它**<mark>比 Paxos 更好理解，也更易于构建实际系统</mark>**。

为易于理解，Raft 在设计上将共识拆分为 leader election、log replication、safety 等几个模块；
另外，为减少状态数量，它要求有**<mark>更强的一致性</mark>**（a stronger degree of coherency）。
从结果来看，学生学习 Raft 要比学习 Paxos 容易。

Raft 还引入了一个新的**<mark>集群变更</mark>**（添加或删除节点）机制，利用
**<mark>重叠大多数</mark>**（overlapping majorities）特性来保证安全（不会发生冲突）。

# 1 引言

共识算法使多个节点作为一个整体协同工作，这样部分节点出故障时，系统作为一个整体
仍然可用。在构建可靠的大规模软件系统时，共识算法扮演着核心角色。

## 1.1 本文背景与目的

过去十几年，共识算法领域一直被 **<mark>Paxos 牢牢统治</mark>** [15, 16]：大部分
共识算法的实现都基于 Paxos 或者受其影响，而 Paxos 也成了**<mark>教学</mark>**的
首要选择。

不幸的是，虽然已经有很多更通俗解释 Paxos 的尝试，但 Paxos 仍然**<mark>难以理解</mark>**。
此外，Paxos 需要做很复杂的**<mark>架构改动</mark>**才能应用到真实系统中。因此，
系统开发者和学生都对 Paxos 感到困惑 —— 我们自己也是如此，在经历了一番挣扎之后，我
们最终决定**<mark>寻找一种新的</mark>**、能为真实系统和日常教学提供更好基础的**<mark>共识算法</mark>**。

* 这个新算法的**<mark>首要目标是可理解性</mark>**（understandability）：
  能否提出一个可用于实际系统、能以恰当方式描述、比 Paxos 更容易学习的共识算法？
* 此外，这个算法要能被系统架构师和开发者**<mark>更直观地理解</mark>**。
  算法能工作固然很重要，但这还不够，还要让人能很直观地理解它为什么可以工作。

## 1.2 研究成果简介

这项工作的成果就是本文将介绍的 Raft 共识算法。
我们特意采取了一些技术**<mark>来使它更容易理解</mark>**，包括
**<mark>功能模块分解</mark>**（leader election, log replication, and safety) 和
**<mark>状态空间简化</mark>**（相比于 Paxos，Raft 中不确定性程度和节点不一致的场景数量都变少了）。
一项针对两个学校共计 43 名学生的调查显示，**<mark>Raft 要比 Paxos 好理解地多</mark>**：
在学习了这两种算法之后，33 个同学回答 Raft 的问题要比回答 Paxos 的问题好。

Raft 在很多方面与现有的共识算法（例如 Viewstamped Replication [29, 22]）类似，但也有几个新特征：

1. **<mark>Strong leader</mark>**（强领导者）: 使用更强的 leadership 模型。例如，
   **<mark>log entries 只会从 leader 往其他节点同步</mark>**。这简化了 replicated log 的管理，使 Raft 理解上更简单。
2. **<mark>Leader election</mark>**（领导选举）: 使用**<mark>随机定时器</mark>**来选举 leader，并复用了
   任何共识算法本来就都有 heartbeat 机制（只是在 heartbeat 里增加了一点新东西），还能简单、快速地解决冲突。
3. **<mark>Membership changes</mark>**（节点变更）: 使用一种新的**<mark>联合共识</mark>**
  （joint consensus）方式，特点是新老配置所覆盖的两个大多数（majorities）子集
   在变更期间是有重叠的，并且集群还能正常工作。

我们认为不管用于教学还是真实系统，Raft 都比 Paxos 和其他共识算法更好，它的优点包括，

* 更简单、更易理解；
* 描述足够详细，易于工业界实现（已经有几种开源实现并在一些公司落地）；
* 安全性（无冲突）有规范化描述并经过了证明；
* 效率与其他算法相当。

## 1.3 本文组织结构

本文接下来的内容安排如下：

* Section 2：介绍 replicated state machine；
* Section 3：讨论 Paxos 的优缺点；
* Section 4：介绍我们针对可理解性所采取的一些设计；
* Section 5-8：描述 Raft 共识算法；
* Section 9：实现与评估；
* Section 10：相关工作。

# 2 复制式状态机（replicated state machines）

讨论 replicated state machines [37] 时，通常都会涉及到共识算法。在这种模型中，
若干节点上的状态机**<mark>计算同一状态的相同副本</mark>**（compute identical
copies of the same state），即使其中一些节点挂掉了，系统整体仍然能继续工作。

## 2.1 使用场景

复制式状态机**<mark>用于解决分布式系统中的一些容错（fault tolerance）问题</mark>**。

例如，GFS [8]、HDFS [38]、RAMCloud [33] 等单 leader 大型系统，通常用一个
独立的复制式状态机来管理 leader election 及存储配置信息。
Chubby [2] 和 ZooKeeper [11] 也用到了复制式状态机。

## 2.2 状态机架构

复制式状态机**<mark>通常用 replicated log</mark>**（复制式日志）实现，如图 1 所示，

<p align="center"><img src="/assets/img/raft-paper/1.png" width="45%" height="45%"></p>
<p align="center">图 1. <mark>复制式状态机架构</mark>。
共识算法管理着<mark>客户端发来的状态机命令</mark>的一个<mark>日志副本</mark>
（replicated log），状态机以完全相同的顺序执行日志中的命令，因此产生<mark>完全相同的输出</mark>。
</p>

几点解释：

1. 每台 server 都保存着一个由**<mark>命令序列组成的 log 文件</mark>**，状态机按
   顺序执行其中的命令。由于每个 log 都以完全相同的顺序保存了完全相同的命令，而
   且状态机是确定性的（deterministic），因此每个状态机计算得到的是相同的状态，
   产生的是相同的结果。
2. **<mark>保持 replicated log 的一致性</mark>**是共识算法的职责。某个节点上的
   **<mark>共识模块</mark>**从客户端接收命令，然后写入 log 中；并与其他
   节点上的共识模块通信，以保证即使某些机器挂掉，每个 replicated log 最终也以
   相同的顺序包含相同的请求。
3. 命令正确地复制到其他节点之后，每台机器的状态机会按顺序处理他们，然后将结果返回给客
   户端。从客户端的角度来看，这些节点形成的是高度可靠的单个状态机。

## 2.3 共识算法的典型特征

对于实际系统来说，共识算法有如下典型特性：

* 在任何 **<mark>non-Byzantine 条件下都能保证安全</mark>**（从不返回错误结果），
  这里所说的 non-Byzantine 条件包括网络延迟、partitions、丢包、重复、乱序。
* 只要多数节点能工作、彼此之间及与客户端之间能通信，那整体系统的功能就完全正常（完全可用）；
  因此，一个有 5 台节点的集群，能容忍任意 2 台机器的挂掉。
* 机器挂掉后，能够从持久存储中恢复，然后重新加入集群；
* **<mark>不依赖时序（timing）来保证日志的一致性</mark>**：在最坏情况下，时钟不准
  和极大的消息延迟都会导致可用性问题，因此避免依赖时序来保证一致性。
* 通常情况下，**<mark>一条命令收到了集群大多数节点的响应</mark>**时，这条命令就算完成了；
  **<mark>少量响应慢的机器不影响整体的系统性能</mark>**。

# 3 Paxos 有什么问题？

在过去的十几年，Leslie Lamport 的 Paxos [15] 协议几乎是共识算法的代名词：
学校里教的基本上都是它，大部分共识算法的实现也是以它为起点。

## 3.1 Paxos 简介

* 首先**<mark>定义了一个协议</mark>**，能让**<mark>单个决议</mark>**（
  single decision）达成一致，例如**<mark>单条 log entry</mark>**。
  这个子集称为 single-decree（单决议） Paxos。
* 通过组合**<mark>协议的多个实例</mark>**（multiple instances of this protocol）
  提供**<mark>决议序列</mark>**的功能，例如一个 **<mark>log file</mark>**
  （而非单个 log entry），称为 multi-decree（多决议）Paxos。
* 同时保证保证 safety and liveness，支持集群扩缩容节点。
* **<mark>正确性已经得到证明</mark>**，在一般情况下也是高效的（efficient）。

## 3.2 Paxos 的缺点

不幸的是，Paxos 有两个致命缺点：

1. 出名地难理解（exceptionally difficult）；
2. 没有考虑真实系统的实现，很难用于实际系统。

下面分别解释。

### 3.2.1 理解异常困难

Paxos 的完整解释 [15] 众所周知地模糊，只有**<mark>少数人</mark>** —— 并且
是在付出了极大的努力之后 —— 才成功理解了它。因此，出现了很多用更简单的术语
来解释 Paxos 的尝试 [16, 20, 21]，这些解释关注在单决议子集上，但仍然很有挑战性。

在 NSDI 2012 的一次正式调研上，我们发现很少有人对 Paxos 感到舒适（comfortable）
，甚至包括那些资深研究者。我们自己也非常挣扎 —— 实际上直到
**<mark>阅读了一些简化版解释性文章、并自己设计和实现了一种替代协议</mark>**之后，
我们才**<mark>完全理解</mark>**了整个 Paxos 协议，整个过程用了几乎**<mark>一年</mark>**。

我们认为 **<mark>Paxos 的模糊性</mark>**来源于它把单决议子集作为了它的基础。

* 单决议 Paxos 比较 dense 和 subtle，它分为了**<mark>没有简单直观解释</mark>**
  的两个阶段，二者无法独立理解。由于这个原因，很难直观地解释为什么 single-decree 协议是工作的。
* 多决议 Paxos 的组合规则**<mark>显著增加了额外的复杂性和模糊性</mark>**。
  我们认为**<mark>在多个决策上</mark>**（例如一个 log 而非仅仅一个 log entry）
  **<mark>达到共识这一整体问题</mark>**，能以其他更直接和浅白的方式进行分解。

### 3.2.2 没有考虑真实系统的实现

Paxos 没有为构建真实系统提供一个良好的基础，

1. 一个原因是**<mark>没有一个普遍接受的 multi-decree Paxos 算法</mark>**：

    * Lamport 的描述大部分是关于 single-decree Paxos 的，
    * 他简要描述了实现 multi-decree Paxos 的几种可能方式，但很多细节是缺失的，
    * 一些算法尝试对 Paxos 进行具体化和优化 [26] [39] [13]，但这些算法并不统一，与 Lamport 的概要描述也并不一致，
    * Chubby [4] 等系统已经实现了类似 Paxos 的算法，但大部分此类系统的**<mark>实现细节都没有公开</mark>**。

2. Paxos 架构对于构建真实系统来说非常糟糕，其实也是 **<mark>multi-decree 靠 single-decree 来组合</mark>**的另一个结果。
  例如，独立选择多个 log entry 然后合并成一个顺序 log 并没有多少好处，只会增加复杂性。
  **<mark>围绕单个 log 设计系统会更加简单和高效</mark>**，新的 entry 以严格方式顺序追加到这个 log 文件。

3. **<mark>Paxos 的核心是一个对称点对点模型</mark>**（symmetric peer-to-peer） ——
  虽然它最终出于性能考虑建议了一个**<mark>弱领导力模型</mark>**。
  在一个**<mark>简化的、只需做出单个决定的世界中，这样是有意义的</mark>**，但实际系统并不满足这个前提。
  如果需要做出多个决策，那还是先选举一个 leader，然后由 leader 来协调决策更加简单和快速。

这些问题导致的最终结果就是：**<mark>实际系统与 Paxos 算法本身差异非常大</mark>**，

* 每种实现都是以 Paxos 为起点，但在实现过程中遇到各种困难，最后开发出的是一个与最初设想迥异的架构。
* 非常花时间，而且很容易出错，加剧了 Paxos 的理解难度。
* Paxos 的公式对于定理证明来说可能不错，但实际实现与公式是如此不同，导致证明并没有多少意义。

Chubby 开发者的评论很有代表性：

> **<mark>Paxos 的算法描述和真实需求之间存在一个巨大鸿沟，......
> 最终的系统其实将建立在一个没有经过证明的协议之上</mark>** [4]。

## 3.3 小结

由于以上问题，我们认为 Paxos 既不能为开发真实系统提供良好基础，又不适用于教学。
而考虑到**<mark>大型软件系统中共识的重要性</mark>**，我们决定自己设计一种替代
Paxos、有更好特性的共识算法。

Raft 就是这一实验的产物。

# 4 面向可理解性的设计

## 4.1 设计目标

设计 Raft 时我们有几个目标：

1. 必须为**<mark>构建真实系统</mark>**提供完整基础，能显著降低系统开发者所需的设计工作；
2. 必须在所有情况下确保安全（不会导致冲突），在典型场景下确保可用；
3. 对于常见操作必须很高效，
4. 必须确保**<mark>可理解性</mark>**（understandability），这才是我们**<mark>最重要的目标，同时也是最大的挑战</mark>**。

对于一大部分受众来说，这个算法要很好理解。另外，它必须很直观，这样系统构建者在
实现时能方便地进行扩展 —— 这一需求在真实世界中是必然存在的。

## 4.2 可理解性的评估

在设计 Raft 时，有好多次我们都必须在多种可选方案中做出抉择，我们的评估依据
就是可理解性：

* 每种方案**<mark>解释起来的难易程度</mark>**（例如，该方案的状态空间有多复杂，该方案有没有模糊之处等）
* 读者能**<mark>多轻松地完全理解这种方案</mark>**？

这其中必然有很大的主观性，但我们使用的两种方式还是比较通用的：

1. **<mark>问题分解</mark>**：将问题尽可能分解为独立的可解决、可解释和可理解的模块。

   例如，将 leader election、log replication、safety、membership changes 拆分开来。

2. **<mark>简化状态空间</mark>**：减少需要考虑的状态数量，使系统更加清晰，尽量消除非确定性（nondeterminism）。

    具体来说，**<mark>log 不允许有空洞</mark>**（holes），而且 Raft 会避免各机器的 log 出现不一致。

虽然在大部分情况下我们极力避免**<mark>非确定性</mark>**，但在某些场景下，它却会**<mark>提高可理解性</mark>**。
尤其是，随机化方式（randomized approaches）会引入非确定性，但它们通过
“以同一种方式处理所有情况”（choose any; it doesn’t matter），让状态空间变得更小。
我们用随机化来**<mark>简化 leader 选举</mark>**算法。

# 5 Raft 共识算法

Raft 是一个管理 replicated log 的算法。图 2 提炼了它的核心点作为索引和参考，
图 3 总结了该算法的一些核心特性；后文将详细介绍其中的细节。

> 图 2 和 3 都是用大文本框描述算法，很不直观，这里直接拆成段落作为 5.0 小节，
> 并对照 etcd/raft 的实现，方便理解。译注。

**<mark>Raft 实现共识的机制</mark>**是，

1. 共同选举出一个 leader，
2. 给予这个 leader 管理 replicated log 的完全职责，
3. **<mark>Leader 接受来自客户端的 log entries</mark>**，然后复制给其他节点，
  并在安全（不会导致冲突）时，告诉这些节点将这些 entries 应用到它们各自的状态机。

**<mark>只有一个 leader 的设计简化了 replicated log 的管理</mark>**。例如，

1. Leader 能决定将新的 entry 放到 log 中的什么位置，而不用咨询其他机器，
2. 数据流也是从 leader 到其他节点的简单单向方式。

Leader 可能会挂掉（fail）或从集群中失联（disconnected），这种情况下会选举一个新 leader。

**<mark>基于以上 leader 机制</mark>**，Raft 将共识问题分解为三个相对独立的子问题，

1. Leader election (Section 5.2)
2. Log replication (Section 5.3)
3. Safety (Section 5.4)

介绍了共识算法之后，本节还会讨论可用性（availability）问题，以及时序（timing）在系统中的角色。

## 5.0 Raft 核心知识点（提前）汇总

### 5.0.1 不同节点的状态参数

1. 所有节点上的**<mark>持久状态</mark>**：处理客户端请求时，需要
  **<mark>先更新这些持久状态（存储在稳定介质上），再响应请求</mark>**。

    1. `currentTerm`：**<mark>该节点已知的当前任期</mark>**。节点启动时初始化为 0，然后单调递增。

        ```go
        // https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/log.go#L112

        func (l *Log) currentTerm() uint64 {
            if len(l.entries) == 0 {
                return l.startTerm
            }
        
            return l.entries[len(l.entries)-1].Term // 最后一个已提交 entry 所属的任期
        }
        ```

    1. `votedFor`：**<mark>投票给谁</mark>**（candidateId）；如果没有就是 none；
    1. `log[]`：log entries，索引从 1 开始；每个 entry 包含了状态机命令和 leader 收到这个 entry 时的 term 信息。

2. 所有节点上的**<mark>易失状态</mark>**：

    1. `commitIndex`：最后**<mark>提交的 entry 的 index</mark>**；初始化为 0，然后单调递增；
    1. `lastAppliedIndex`：最后 **<mark>apply 到状态机的 index</mark>** ；初始化为 0，单调递增；

3. **<mark>Leader</mark>** 节点上的**<mark>易失状态</mark>**：选举之后重新初始化

    1. `nextIndex[]`，**<mark>为每个节点分别维护的编号，下次 replicate entry 时用</mark>**；初始化为 `leader_last_log_index + 1`
    1. `matchIndex[]` ，为每个节点分别维护的编号，表示**<mark>已知的、复制成功的最大 index</mark>**；初始化为 0，单调递增。

### 5.0.2 `AppendEntries` RPC

* 用途：由 leader 发起，用于 replicate log entries (§5.3)，**<mark>也用作心跳</mark>** §5.2；
* 参数：

    1. `term`：leader 的任期编号；
    1. `leaderId`：**<mark>follower 重定向客户端</mark>**时会用到；
    1. `prevLogIndex`：上一个 log entry 的 index；
    1. `prevLogTerm`：prevLogIndex entry 的 term；
    1. `entries[]`：需要追加到 log 的新 entry（如果是 heartbeat，那数组为空；否则出于效率考虑可能会有多个）；
    1. `leaderCommit`：leader 的 commitIndex；

* 返回结果

    1. `term`：currentTerm，leader 用来更新它自己；
    1. `success`：如果 follower 包含了匹配 prevLogIndex and prevLogTerm 的 entry，则返回 `true`。

看一段代码就比较清楚了：

```go
// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/log.go#L467

func (l *Log) appendEntries(entries []*protobuf.LogEntry) error {
    startPosition, _ := l.file.Seek(0, os.SEEK_CUR) // 定位到起始写入位置

    for i := range entries { // Append each entry util hit an error.
        logEntry := &LogEntry{
            log:       l,             // 日志文件
            Position:  startPosition, // 起始写入位置
            pb:        entries[i],    // 待写入 log entry
        }

        size = l.writeEntry(logEntry, w)
        startPosition += size
    }

    return nil
}

func (l *Log) writeEntry(entry *LogEntry, w io.Writer) (int64, error) {
    if len(l.entries) > 0 {
        lastEntry := l.entries[len(l.entries)-1] // 上一个已经写入日志的 entry

        if entry.Term < lastEntry.Term           // 待写入 entry 所带的任期号不能小于前一 entry 所带的任期号
            return -1, Errorf("raft.Log: Cannot append entry with earlier term")
        if entry.Term == lastEntry.Term && entry.Index <= lastEntry.Index // 写入位置必须在前一个 entry 之后
            return -1, Errorf("raft.Log: Cannot append entry with earlier index in the same term")
    }

    size := entry.Encode(w) // 写到持久存储，然后就可以 append 到 entries list 了
    l.entries.append(entry)

    return int64(size), nil
}
```

### 5.0.3 `RequestVote` RPC

* 用途：由 candidate 发起，用于收集选票（gather votes），将在 §5.2 介绍；
* 参数：
    * `term` candidate’s term
    * `candidateId` candidate requesting vote
    * `lastLogIndex` index of candidate’s last log entry (§5.4)
    * `lastLogTerm` term of candidate’s last log entry (§5.4)
* 返回结果
    * `term` currentTerm, for candidate to update itself
    * `voteGranted` true means candidate received vote

同样，看一段代码就很清楚了：

```go
// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/server.go#L1071

func (s *server) processRequestVoteRequest(req *RequestVoteRequest) (*RequestVoteResponse, bool) {
    if _, ok := s.peers[req.CandidateName]; !ok // Candidate 节点不在本集群，直接 deny
        return _, false

    if req.Term < s.Term   // 请求来自更早的任期（old term），直接拒绝
        return _, false

    if req.Term > s.Term { // 看到了比本节点还要新的任期号（term number），update 到本节点
        s.updateCurrentTerm(req.Term, "")
    } else if s.votedFor != "" && s.votedFor != req.CandidateName { // 当前节点已经投给了其他 candidate
        return _, false
    }

    lastIndex, lastTerm := s.log.lastInfo()
    if lastIndex > req.LastLogIndex || lastTerm > req.LastLogTerm // 如果 candidate 的 log 比我们的要老，则不投给它
        return _, false

    // 投票给该 candidate，然后重置本节点的 election timeout
    s.votedFor = req.CandidateName
    return newRequestVoteResponse(s.currentTerm, true), true
}
```

### 5.0.4 Follower 处理循环（§5.2）

* 对来自 candidate 和 leader 的 RPC 请求做出响应；
* 如果**<mark>直到 election timeout</mark>** 都没从当前 leader 收到
  `AppendEntries` RPC，也没有投票给某个 candidate，则**<mark>转入 candidate 状态</mark>**。

状态机：

```go
// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/server.go#L664

func (s *server) followerLoop() {
    for s.State() == Follower {
        select {
        case e := <-s.c:
            switch req := e.target.(type) {
            case JoinCommand:
                //If no log entries exist and a self-join command is issued then immediately become leader and commit entry.
                if s.log.currentIndex() == 0 && req.NodeName() == s.Name() {
                    s.setState(Leader)
                    s.processCommand(req, e)
                }
            case *AppendEntriesRequest:
                // If heartbeats get too close to the election timeout then send an event.
                if elapsedTime > electionTimeout*ElectionTimeoutThresholdPercent {
                    s.DispatchEvent(ElectionTimeoutThresholdEventType)
                }
                s.processAppendEntriesRequest(req)
            case *RequestVoteRequest:
                s.processRequestVoteRequest(req)
            case *SnapshotRequest:
                s.processSnapshotRequest(req)
            }

        case <-timeoutChan:
            s.setState(Candidate)
        }

        timeoutChan = afterBetween(s.ElectionTimeout(), s.ElectionTimeout()*2)
    }
}
```

### 5.0.5 Candidate 处理循环（§5.2）

转成 candidate 角色之后，立即**<mark>开始选举</mark>**，

1. 增大 `currentTerm`
1. **<mark>投票给自己</mark>**
1. 重置选举定时器
1. 发送 `RequestVote` RPCs 给其他所有节点

根据结果，

* 如果收到了大多数节点的赞成票，则成为 leader；
* 如果从新 leader 收到了 `AppendEntries` RPC，则转入 follower 角色；
* 如果 election timeout，再次开始选举。

状态机：

```go
// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/server.go#L730

// The event loop that is run when the server is in a Candidate state.
func (s *server) candidateLoop() {
    prevLeader := s.leader
    s.leader = ""

    lastLogIndex, lastLogTerm := s.log.lastInfo()
    doVote := true
    votesGranted := 0

    for s.State() == Candidate {
        if doVote {
            s.currentTerm++      // Increment current term, vote for self.
            s.votedFor = s.name

            // Send RequestVote RPCs to all other servers.
            respChan = make(chan *RequestVoteResponse, len(s.peers))
            for _, peer := range s.peers {
                 sendVoteRequest(s.currentTerm, s.name, lastLogIndex, lastLogTerm, respChan)
            }

            // Wait for either:
            //   * Votes received from majority of servers: become leader
            //   * AppendEntries RPC received from new leader: step down.
            //   * Election timeout elapses without election resolution: increment term, start new election
            //   * Discover higher term: step down (§5.1)
            votesGranted = 1
            timeoutChan = afterBetween(s.ElectionTimeout(), s.ElectionTimeout()*2)
            doVote = false
        }

        // If we received enough votes then stop waiting for more votes.
        if votesGranted == s.QuorumSize() {
            s.setState(Leader)
            return
        }

        // Collect votes from peers.
        select {
        case resp := <-respChan:
            if success := s.processVoteResponse(resp); success
                votesGranted++

        case e := <-s.c:
            var err error
            switch req := e.target.(type) {
            case Command:
                err = NotLeaderError
            case *AppendEntriesRequest:
                s.processAppendEntriesRequest(req)
            case *RequestVoteRequest:
                s.processRequestVoteRequest(req)
            }

            // Callback to event.
            e.c <- err

        case <-timeoutChan:
            doVote = true
        }
    }
}
```

### 5.0.6 Leader 处理循环（§5.2 ~ §5.4）

* 定期发送心跳（空的 AppendEntries）给其他节点，防止它们 election timeout (§5.2)
* 从客户端接受请求，将 entry 追加到 local log，**<mark>等 entry 应用到状态机之后再发送响应</mark>** (§5.3)
* 对于 follower `i`，如果 `lastLogIndex ≥ nextIndex[i]`：将从 `nextIndex[i]` 开始的所有 log entry 发送给节点 i
    * 如果成功：更新 nextIndex[i] 和 matchIndex[i] (§5.3)
    * 如果因为 log inconsistency 失败：`nextIndex[i]--` 然后重试以上过程 (§5.3)
* 如果存在 N 满足 `N > commitIndex`、`matchIndex[i] ≥ N` 对大部分 i 成立、`log[N].term == currentTerm`：设置 `commitIndex = N` (§5.3, §5.4).

状态机：

```go
// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/server.go#L811

func (s *server) leaderLoop() {
    logIndex, _ := s.log.lastInfo()

    // Update the peers prevLogIndex to leader's lastLogIndex and start heartbeat.
    for _, peer := range s.peers {
        peer.setPrevLogIndex(logIndex)
        peer.startHeartbeat() // 定期发送心跳
    }

    // Commit a NOP after the server becomes leader.
    // "Upon election: send initial empty AppendEntries RPCs (heartbeat) to each server."
    s.Do(NOPCommand{})

    // Begin to collect response from followers
    for s.State() == Leader {
        select {
        case e := <-s.c:
            switch req := e.target.(type) {
            case Command:
                s.processCommand(req, e)
                continue
            case *AppendEntriesRequest:
                s.processAppendEntriesRequest(req)
            case *AppendEntriesResponse:
                s.processAppendEntriesResponse(req)
            case *RequestVoteRequest:
                s.processRequestVoteRequest(req)
            }
        }
    }

    s.syncedPeer = nil
}
```

### 5.0.7 Raft 五大特性（图 3）

1. **<mark>Election Safety</mark>**（选举安全）：在任意 term（任期）内，**<mark>最多只会有一个</mark>** leader 被选出来。§5.2
2. **<mark>Leader Append-Only</mark>**（只追加）：leader 从不覆盖或删除它的日志中的 entry；只会追加（append）。§5.3
3. **<mark>Log Matching</mark>**（日志匹配）：如果两个日志包含了 **<mark>index 和 term 完全相同的 entry</mark>**，
  那**<mark>从这个 index 往前的那些 entry</mark>** 也都是完全相同的。§5.3
4. **<mark>Leader Completeness</mark>**：如果一个 entry 在某个 term 被提交，那它将出现在所有 term 更大的 leaders 的 log 中。§5.4
5. **<mark>State Machine Safety</mark>**（状态机安全）：如果一个节点在特定
   index 应用了一个 entry 到它的状态机，那其他节点不会在相同 idnex 应用另一个不同的 entry。§5.4.3

以上都是总结，下面看具体细节。

## 5.1 Raft 基础

一个 Raft cluster 包括若干台节点，例如典型的 5 台，这样一个集群能容忍 2 台节点发生故障。

### 5.1.1 状态机

在任意给定时刻，每个节点处于以下**<mark>三种状态</mark>**之一：
leader、follower、**<mark>candidate</mark>**。

* 正常情况下，集群中有且只有一个 leader，剩下的都是 follower。
* **<mark>Follower 都是被动的</mark>**（passive）：它们**<mark>不会主动发出请求</mark>**，只会简单响应来自 leader 和 candidate 的请求。
* **<mark>Leader 负责处理所有的客户端请求</mark>**；如果一个客户端向 follower 发起请求，后者会将它重定向到 leader。
* Candidate 是一个特殊状态，选举新 leader 时会用到，将在 5.2 节介绍。

图 4 是相应的状态机，

<p align="center"><img src="/assets/img/raft-paper/4.png" width="50%" height="50%"></p>
<p align="center">图 4. <mark>Raft 状态机</mark>。</p>

1. Follower 只会响应来自其他节点的请求；如果一个 follower 某段时间内收不到
   leader 的请求，它会<mark>变成一个 candidate 然后发起一轮选举</mark>；
2. 获得大多数选票的 candidate 将成为新的 leader；
3. 通常情况下，**<mark>除非发生故障，否则在任的 leader 会持续担任</mark>**下去。

### 5.1.2 任期（term）

Raft 将时间划分为**<mark>长度不固定的任期</mark>**，任期用连续的整数表示，
如图 5 所示，

<p align="center"><img src="/assets/img/raft-paper/5.png" width="45%" height="45%"></p>
<p align="center">图 5. 时间被划分为多个任期（term）。
</p>

几点说明：

1. **<mark>每个任期都是从选举开始的</mark>**，此时多个 candidate 都试图成为 leader。
2. 某个 candidate 赢得选举后，就会成为该任期内的 leader。
   **<mark>Raft 保证了在任意一个任期内，最多只会有一个 leader</mark>**。
3. **<mark>有时选举会失败（投票结果会分裂），这种情况下该任期内就没有 leader</mark>**。
    但问题不大，因为很快将开始新一轮选举（产生一个新任期）。
4. **<mark>不同节点上看到的任期转换时刻可能会不同</mark>**。
    在某些情况下，一个节点**<mark>可能观察不到某次选举甚至整个任期</mark>**。

另外，

* Raft 中，任期是一个**<mark>逻辑时钟</mark>** [14]，用来让各节点**<mark>检测过期信息</mark>**，例如过期的 leader。
* 每个节点都**<mark>记录了当前任期号</mark>** currentTerm，这个编号随着时间单调递增。
* 节点之间通信时，会带上它们各自的 currentTerm 信息；

    * 如果一个节点**<mark>发现自己的 currentTerm 小于其他节点的，要立即更新自己的</mark>**；
    * 如果一个 candidate 或 leader **<mark>发现自己的任期过期了，要立即切换到 follower 状态</mark>**。
    * 如果一个节点接收到携带了过期任期编号（stale term）的请求，会拒绝这个请求。

### 5.1.3 节点之间通信：RPC

Raft 服务器之间通过 RPC 通信，基础的共识算法只需要两种 RPC（前面已经给出了参考实现）：

1. **<mark><code>RequestVote</code></mark>**：由 candidates 在选举期间发起 (Section 5.2)；
2. **<mark><code>AppendEntries</code></mark>**：由 leader 发起，用于 replicate log entries 并作为一种 heartbeat 方式 (Section 5.3). 

另外，Section 7 还会看到一种在服务器之间传输 snapshot 用的 RPC。

如果在一段时间内没有收到响应，服务器会对 RPC 进行重试；另外，为了最佳性能，服务器会并发执行 RPC。

## 5.2 Leader 选举

### 5.2.1 Heartbeat 和选举触发流程

Raft **<mark>使用一种 heartbeat 机制来触发 leader 选举</mark>**。

1. **<mark>节点启动时是 follower 状态</mark>**；只要能持续从 leader 或
   candidate 收到合法的 RPC 请求，就会一直保持在 follower 状态；
2. Leaders 定期**<mark>发送心跳</mark>**给（空的 **<mark><code>AppendEntries</code></mark>** 消息）所有的 follower，以保持它的权威；
3. 如果一个 follower 在 **<mark>election timeout</mark>** 时间段内都没有收到来
   自 leader/candidate 的通信，就认为当前已经没有有效 leader 了，然后发起一次选举。

### 5.2.2 选举过程

对于一个 follower，当开始选举时，

1. **<mark>首先增大自己当前的 term</mark>**，
2. 然后切换到 candidate 状态，
3. 然后选举自己作为 leader，同时并发地向集群其他节点发送 RequestVote RPC，
4. 然后它将处于 candidate 状态，直到发生以下三种情况之一，下文会分别讨论：

    1. 该 follower 赢得此次选举，成为 leader；
    2. 另一个节点赢得此次选举，成为 leader；
    3. 选举超时，没有产生有效 leader。

### 5.2.3 获胜的判断条件

如果一个 candidate 获得了**<mark>集群大多数节点针对同一任期（term）的投票</mark>**，
那它就赢得了这个任期内的选举。

针对给定的任期，每个节点最多只能投一票，**<mark>投票的标准是先到先得</mark>**（
first-come-first-served，注意，5.4 小节会引入一个额外限制）。“期得获该任期的大
多数选票”这一限制条件，决定了最多只会有一个 candidate 赢得选举 （也就是图 3 中
的选举安全特性）。一个 candidate 赢得选举之后，就会为 leader，然后发生心跳消息
给所有其他节点来建立它的权威，防止新的选举发生。

在等待投票期间，一个 candidate 可能会从其他服务器收到一个 AppendEntries RPC 声称自己是
leader。如果这个 leader 的任期 term （包含在 RPC 消息中），

1. **<mark>大于等于这个 candidate 的 currentTerm</mark>**：那该 candidate 就承认
   这个 leader 是合法的，然后回归到 follower 状态。
2. 小于这个 candidate 的 currentTerm：拒绝这个 RPC ，仍然留在 candidate 状态。

第 3 种可能的结果是：该 candidate 既没有赢得也没有输掉这次选举。
如果多个 followers 在同一时间成为 candidates，投票就会很分散，最终没有谁能赢得大多数选票。
当发生这种情况时，每个 candidate 都会超时，然后各自增大 term 并给其他节点发送 RequestVote 请求，开始一轮新选举，
但**<mark>如果没有额外的预防措施，这种投票分裂的情况看可能会无限持续下去</mark>**。

### 5.2.4 避免无限循环的投票分裂：随机选举超时

Raft 使用**<mark>随机选举超时</mark>**来确保投票分裂
**<mark>只会非常偶然地发生，并且发生之后能很快恢复正常</mark>**。

首先是从一些固定时长（例如 `150-300ms`）中随机选择一个选举超时时间，这使得节点的
超时时刻比较分散，在大部分情况下**<mark>同一时刻最多只有一台会超时</mark>**；
这台超时的节点会**<mark>在其他节点超时之前赢得选举</mark>**（因为它的 term 更大，其他节点都会投票给它）。

随机化机制也同样用于**<mark>处理投票分裂</mark>**。每个 candidate 在一轮选举开始时，
会随机重置它的 election timeout，等到 timeout 之后才开始发起新一轮选举；这减少
了新一轮选举也发生投票分裂的可能性。Section 9.3 会看到，这种方式**<mark>很快就能选出 leader</mark>**。

选举是**<mark>可理解性如何指导了我们的设计</mark>**的一个例子。
最初我们计划使用的是一个 ranking system: each candidate was assigned a
unique rank, which was used to select between competing candidates. If a
candidate discovered another candidate with higher rank, it would return to
follower state so that the higher ranking candidate could more easily win the
next election. We found that this approach created subtle issues around
availability (a lower-ranked server might need to time out and become a
candidate again if a higher-ranked server fails, but if it does so too soon, it
can reset progress towards electing a leader). We made adjustments to the
algorithm several times, but after each adjustment new corner cases appeared.
Eventually we concluded that the randomized retry approach is more obvious and
understandable.

## 5.3 Leader 向其他节点复制日志（log）

### 5.3.1 复制流程

选出一个 leader 之后，它就开始服务客户端请求。

* 每个客户端请求中都包含一条**<mark>命令</mark>**，将由 **<mark>replicated state machine 执行</mark>**。
* leader 会将这个命令追加到它自己的 log，然后并发地通过 AppendEntries RPC 复制给其他节点。
* **<mark>复制成功</mark>**（无冲突）之后，**<mark>leader 才会将这个 entry 应用到自己的状态机，然后将执行结果返回给客户端</mark>**。
* 如果 follower 挂掉了或很慢，或者发生了丢包，leader 会**<mark>无限重试 AppendEntries 请求（即使它已经给客户端发送了响应）</mark>**，
  直到所有 follower 最终都存储了所有的 log entries。

### 5.3.2 Log 文件组织结构

如下图所示：

<p align="center"><img src="/assets/img/raft-paper/6.png" width="45%" height="45%"></p>
<p align="center">图 6：Log 文件组织方式</p>

1. Log 由 log entry 组成，每个 entry 都是**<mark>顺序编号</mark>**的，这个整数索引标识了该 entry 在 log 中的位置。
2. 每个 entry 包含了
    1. Leader **<mark>创建该 entry 时的任期</mark>**（term，每个框中的数字），用于检测 logs 之间的不一致及确保图 3 中的某些特性；
    2. **<mark>需要执行的命令</mark>**。
3. 当一条 entry 被**<mark>安全地应用到状态机</mark>**之后，就认为这个 entry 已经**<mark>提交</mark>**了（committed）。

    Leader 来判断何时将一个 log  entry 应用到状态机是安全的。

### 5.3.3 提交（commit）的定义

Raft 保证**<mark>已提交的记录都是持久的</mark>**，并且最终会被所有可用的状态机执行。

* 只要创建这个 entry 的 leader 将它成功**<mark>复制到大多数节点</mark>**（例如图 6 中的 entry 7），这个 entry 就算提交了。
* 这**<mark>也提交了</mark>** leader log 中的**<mark>所有前面的 entry</mark>**，包括那些之前由其他 leader 创建的 entry。

5.4 小节会讨论 ldeader 变更之后应用这个规则时的情况，那时将会看到这种对于 commit 的定义也是安全的。
**<mark>follower 一旦确定某个 entry 被提交了，就将这个 entry 应用到它自己的状态机</mark>**（in log order）。

### 5.3.4 Log matching 特性（保证 log 内容一致）

Raft 这种 log 机制的设计是为了保持**<mark>各节点 log 的高度一致性</mark>**（coherency）。
它不仅简化了系统行为，使系统更加可预测，而且是保证安全（无冲突）的重要组件。

如果不同 log 中的两个 entry 有完全相同的 index 和 term，那么

1. 这两个 entry 一定**<mark>包含了相同的命令</mark>**；
    这来源于如下事实：leader 在任意给定 term 和 log index 的情况下，最多只会创建
    一个 entry，并且其在 log 中的位置永远不会发生变化。

2. 这两个 log 中，**<mark>从该 index 往前的所有 entry 都分别相同</mark>**。
   这一点是通过 AppendEntries 中简单的**<mark>一致性检查</mark>**来保证的：

    * `AppendEntries` 请求中，leader 会带上 log 中**<mark>前一个紧邻 entry 的 index 和 term 信息</mark>**。
    * 如果 follower log 中以相同的 index 位置没有 entry，或者有 entry 但 term 不同，follower 就会拒绝新的 entry。

以上两个条件组合起来，用归纳法可以证明图 3 中的 Log Matching Property：

1. 新加一个 entry 时，简单一致性检查会确保这个 entry 之前的所有 entry 都是满足 Log Matching 特性的；因此只要初始状态也满足这个特性就行了；
2. 初始状态：各 log 文件都是空的，满足 Log Matching 特性；

因此得证。

### 5.3.5 Log 不一致场景

正常情况下，leader 和 follower 的 log 能保持一致，但 leader 挂掉会导致 log 不一致
（leader 还未将其 log 中的 entry 都复制到其他节点就挂了）。
这些不一致会导致一系列复杂的 leader 和 follower crash。
Figure 7 展示了 follower log 与新的 leader log 的几种可能不同：

<p align="center"><img src="/assets/img/raft-paper/7.png" width="50%" height="50%"></p>
<p align="center">Fig 7. Leader 挂掉后，follower log 的一些可能场景。</p>

图中每个方框表示一个 log entry，其中的数字表示它的 term。可能的情况包括：

* 丢失记录(a–b) ；
* 有额外的未提交记录 (c–d)；
* 或者以上两种情况发生 (e–f)。
* log 中丢失或多出来的记录可能会跨多个 term。

例如，scenario (f) 可能是如下情况：从 term 2 开始成为 leader，然后向自己的 log 添加了一些 entry，
但还没来得及提交就挂了；然后重启后迅速又成为 term 3 期间的 leader，然后又加了一些 entry 到自己的 log，
在提交 term 2& 3 期间的 entry 之前，又挂了；随后持续挂了几个 term。

### 5.3.6 避免 log 不一致：`AppendEntries` 中的一致性检查

Raft 处理不一致的方式是**<mark>强制 follower 复制一份 leader 的 log</mark>**，
这意味着 follower log 中**<mark>冲突的 entry 会被 leader log 中的 entry 覆盖</mark>**。
Section 5.4 将会看到再加上另一个限制条件后，这个日志复制机制就是安全的。

解决冲突的具体流程：

1. 找到 leader 和 follower 的**<mark>最后一个共同认可的 entry</mark>**，
2. 将 follower log 中从这条 entry 开始**<mark>往后的 entries 全部删掉</mark>**，
3. 将 leader log 中从这条记录开始往后的所有 entries 同步给 follower。

整个过程都发生在 **<mark>AppendEntries RPC 中的一致性检查</mark>**环节。

```go
// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/server.go#L939

// Processes the "append entries" request.
func (s *server) processAppendEntriesRequest(req *AppendEntriesRequest) (*AppendEntriesResponse, bool) {
    if req.Term < s.currentTerm
        return _, false

    if req.Term == s.currentTerm {
        if s.state == Candidate  // step-down to follower when it is a candidate
            s.setState(Follower)
        s.leader = req.LeaderName
    } else {
        s.updateCurrentTerm(req.Term, req.LeaderName)
    }

    // Reject if log doesn't contain a matching previous entry.
    if err := s.log.truncate(req.PrevLogIndex, req.PrevLogTerm); err != nil {
        return newAppendEntriesResponse(s.currentTerm, false, s.log.currentIndex(), s.log.CommitIndex()), true
    }

    s.log.appendEntries(req.Entries)      // Append entries to the log.
    s.log.setCommitIndex(req.CommitIndex) // Commit up to the commit index.

    // once the server appended and committed all the log entries from the leader
    return newAppendEntriesResponse(s.currentTerm, true, s.log.currentIndex(), s.log.CommitIndex()), true
}

// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/log.go#L399
// Truncates the log to the given index and term. This only works if the log
// at the index has not been committed.
func (l *Log) truncate(index uint64, term uint64) error {
    if index < l.commitIndex // Do not allow committed entries to be truncated.
        return fmt.Errorf("raft.Log: Index is already committed (%v): (IDX=%v, TERM=%v)", l.commitIndex, index, term)

    if index > l.startIndex + len(l.entries) // Do not truncate past end of entries.
        return fmt.Errorf("raft.Log: Entry index does not exist (MAX=%v): (IDX=%v, TERM=%v)", len(l.entries), index, term)

    // If we're truncating everything then just clear the entries.
    if index == l.startIndex {
        l.file.Truncate(0)
        l.file.Seek(0, os.SEEK_SET)
        l.entries = []*LogEntry{}
    } else {
        // Do not truncate if the entry at index does not have the matching term.
        entry := l.entries[index-l.startIndex-1]
        if len(l.entries) > 0 && entry.Term != term
            return fmt.Errorf("raft.Log: Entry at index does not have matching term (%v): (IDX=%v, TERM=%v)", entry.Term, index, term)

        // Otherwise truncate up to the desired entry.
        if index < l.startIndex+uint64(len(l.entries)) {
            position := l.entries[index-l.startIndex].Position
            l.file.Truncate(position)
            l.file.Seek(position, os.SEEK_SET)
            l.entries = l.entries[0 : index-l.startIndex]
        }
    }

    return nil
}
```

* Leader 为每个 follower 维护了后者下一个要使用的 log entry index，即 `nextIndex[followerID]` 变量；
* 一个节点成为 leader 时，会将整个 `nextIndex[]` 都初始化为它自己的 log 文件的下一个 index （图 7 中就是 `11`）。
* 如果一个 follower log 与 leader 的不一致，AppendEntries 一致性检查会失败，从
  而拒绝这个请求；leader 收到拒绝之后，将减小 `nextIndex[followerID]` 然后重试这个
  AppendEntries RPC 请求；如此下去，直到某个 index 时成功，这说明此时 leader
  log 和 follower logs 已经匹配了。
* 然后 follower log 会删掉 index 之后的所有 entry，并将 leader 中的 entry 应用到 follower log 中（如果有）。
  此时 follower log 就与 leader 一致了，在之后的整个 term 中都会保持一致。

### 5.3.7 优化

还可以对协议进行优化，来减小被拒的 AppendEntries RPC 数量。
例如，当拒绝一个 AppendEntries 请求时，follower 可以将冲突 entry 的 term 以及 log 中相同 term 的最小 index 信息包含到响应中。
有了这个信息，leader 就可以直接跳过这个 term 中的所有冲突记录，将 
`nextIndex[followerID]` 降低到一个合理值；这样每个 term 内所有的冲突记录只需要一次触发 AppendEntries RPC ，
而不是每个记录一个 RPC。

但实际中，我们怀疑是否有必要做这种优化，因为故障很少发生，不太可能有很多的不一致记录。
**<mark>有了这个机制，新 leader 上台之后无需执行任何特殊操作来恢复 log 一致性</mark>**。
它只需要执行正常操作，logs 会通过 AppendEntries 一致性检查来收敛。
**<mark>leader 永远不会覆盖或删除自己 log 中的记录</mark>**（Leader Append-Only Property in Figure 3）。

这种 log replication 机制展示了第二节中描述的理想共识特性：

1. 只要集群的大多数节点都是健康的，Raft 就能接受、复制和应用新的 log  entry；
1. 正常情况下**<mark>只需一轮 RPC</mark>** 就能将一个新 entry 复制到集群的大多数节点；
1. **<mark>个别比较慢的 follower 不影响集群性能</mark>**。

## 5.4 安全：确保状态机以相同顺序执行相同命令流

前面几节介绍了 Raft 如何选举 leader 及如何 replicate log entry 的。但是，到目前为止
我们**<mark>已经描述的那些机制，还不足以确保每个状态机以相同的顺序执行完全相同的命令流</mark>**。
例如，考虑下面这个例子：

1. 一个 follower 挂了，
2. 故障期间，leader 提交了几个 log entry，
3. leader 挂了，然后
4. 这个 follower 恢复之后被选为了新 leader，然后用新的 entries 覆盖了老 leader 提交的那些。

导致的结果是，不同的状态机可能会执行不同的命令流。

为了解决这个问题，本节将给 leader election 添加一个限制条件，这也是
**<mark>Raft 算法的最后一块拼图</mark>**。
具体来说，这个限制条件确保了**<mark>任何 term 内的 leader 都包含了前面所有 term 内提交的 entries</mark>**，
也就是图 3 中提到的 Leader Completeness 特性。本节还将给出简要的证明过程。

### 5.4.1 限制一：包含所有已提交 entry 的节点才能被选为 leader

在任何基于 leader 的共识算法中，leader 最终都必须存储了所有的已提交 entry。

在某些共识算法中，例如 Viewstamped Replication [22]，一个节点即使并未包含全部的
已提交 entries 也仍然能被选为 leader。这些算法有特殊的机制来识别缺失 entries，
并在选举期间或选举结束后立即发送给新 leader。不幸的是，这会导致额外的复杂性。

Raft 采取了一种更简单的方式：**<mark>除非前面所有 term 内的已提交 entry 都已经
在某个节点上了，否则这个节点不能被选为 leader</mark>**（后面将介绍如何保证这一点）。
这意味着**<mark>无需从 non-leader 节点向 leader 节点同步数据</mark>**，换句话说
**<mark>log entries 只会从 leader 到 follower 单向流动</mark>**。

那这个是怎么做到的呢？通过投票过程。

* 首先，刚才已经提到，除非 log 中已经包含了集群的**<mark>所有已提交 entries</mark>**，否则一个 candidate 是不能被选为 leader 的。
* 其次，**<mark>还活着的（即参与选举的）节点中，至少有一个节点保存了集群的所有已提交 entries</mark>**
 （因为覆盖大多数节点的 entry，才算是提交成功的）。
* 那么，只要一个 candidate 的 log 与大多数节点相比**<mark>至少不落后</mark>**（at
  least as up-to-date，这个词下文会有精确定义），**<mark>那它就持有了集群的所有已提交记录</mark>**。

因此，只要能确保这里提到的“至少不落后”语意，就能确保选出来的 leader 拥有集群的所有已提交 entries。
**<mark>RequestVote RPC 实现了这个过程</mark>**：请求中包含了发送方的 log 信息，如果当前
节点自己的 log 比对方的更新，会拒绝对方成为 leader 的请求。具体到实现上，
**<mark>判断哪个 log 更加新，依据的是最后一个 entry 的 index 和 term</mark>**：

* 如果 term 不同，那 term 新的那个 log 胜出；
* 如果 term 相同，那 index 更大（即更长）的那个 log 胜出。

### 5.4.2 限制二：当前任期+副本数量过半，entry 才能提交

新 leader 如何提交之前任期内遗留下来的、副本数量过集群半数的 entries？

5.3 小节提到，如果一个 entry 已经存储到了集群中的大多数节点上，leader 就认为这个 entry（在这个 term 内）提交成功了。
如果 leader 在提交这个 entry 之前挂了（即没有同步到大多数节点上），那下一个 leader 将承担这个 entry 的同步和提交任务。
但这里有一些新的问题，图 8 是一个例子：**<mark>即使某个 entry 已经存储到了大多数节点，它仍然可能被新 leader 覆盖掉</mark>**：

<p align="center"><img src="/assets/img/raft-paper/8.png" width="50%" height="50%"></p>
<p align="center">Fig 8. 时序图：举例说明为什么一个新 leader 上任之后，无法判断是否要提交<mark>前任 leader 遗留下来的未提交 entries</mark></p>

最上面一行数字是 log entry index，每个框中的数字是 term，

* 时刻 (a)：S1 是当前 leader，并将 `index=2` 的 entry 复制到了 S2；
* 时刻 (b)：**<mark>S1 挂了，S5 被 S3/S4/S5 选为新 leader</mark>**，任期 `term=3`；然后 S5 在 `index=2` 的位置接受了一个**<mark>来自客户端的新 entry</mark>**；
* 时刻 (c)：**<mark>S5 挂了；S1 被选为了下一任 leader</mark>**，任期 `term=4`；
  然后 **<mark>S1 继续同步挂掉之前的那个 entry</mark>**（`index=2`），它被**<mark>成功同步到大部分节点上（S1/S2/S3），但还未提交</mark>**；

接下来分两种情况：

1. 时刻 (d)：**<mark>S1 又挂了，S5 被 S2/S3/S4 选为下一任 leader</mark>**，
  这种情况下，`index=2,term=2` 的 entry 会被 `index=2,term=3` 的 entry **<mark>覆盖掉</mark>**。
2. 时刻 (e)：S1 在挂掉之前将 `index=3,term=4` 的一条新记录也成功复制到了大部分节点上，
  这种情况下，**<mark>即使 S1 之后挂掉了，S5 还是无法赢得选举</mark>**。
  因此，`index=3,term=4` 的 entry 将会被（不管是 S1 还是其他新 leader）提交，log 中所有之前的记录（例如这里的 `index=2,term=2` 的 entry）也都会被提交。

这里的问题在于：**<mark>判断是否要提交的唯一标准是“已同步的副本数量”</mark>**：数量超过了集群半数，则认为
应该提交，**<mark>没有考虑到任期信息，“任期不同但副本数量都超过集群半数”的情况是可能的</mark>**，即上面的 (d) 情况。
因此，为了避免这个问题，Raft 做了一个限制：**<mark>只有提交当前任期内的记录时，
才能用这种计算副本数量（counting replicas）的方式</mark>**。对应到图 8 中就是 (d) 时刻，
`index=2,term=2` 的三个副本可以被合理覆盖，因为此时已经是 `term=3` 了。
一旦当前任期内的一个 entry 已这种方式被提交了，那所有之前的 entries 都会被 Log
Matching Property 间接提交，对应图 8 中的 (e) 场景。

解决这个问题还有其他方式，但 Raft 这种方式更简单保守，在提交规则中进行处理，因为
**<mark>entries 中保留了它们原始的 term 信息</mark>**。

* 在其他共识算法中，如果一个新 leader 再次复制（rereplicates）之前任期的记录，它们必须用新的任期号。
* Raft 的方式使得判断 entry 更加容易，因为 **<mark>term 号是不会随着时间或 log 文件变的</mark>**。

此外，相比于其他算法，Raft 中的新 leader 发送的 entry 数量更少。(other
algorithms must send redundant log entries to renumber them before they can be
committed).

### 5.4.3 安全性的简要证明

有了以上基础，现在能更准确地证明 Leader Completeness 特性（需要用到 9.2 小节的 safety proof）。
我们用**<mark>反证法</mark>**，先假设它不成立。

假设任期 T 内的 leader leaderT 在它的任期内提交了一个 entry，但这个 entry 没有被后面任期内的 leader 存储。
考虑没有存储这个 entry 的最小的任期 U，`U > T`，

1. leaderU 当选为 leader 的时刻，这个 entry 一定不在其 log 中，因为 leader 不会删除或覆盖 entries；
2. leaderT 已经将这个 entry 同步到大部分节点上，而 leaderU 已经收到了大部
   分节点的投票。因此，**<mark>至少一个节点（“某个特定的投票者”）既接受了 leaderT 复制过来的记录，又投票给了 leaderU</mark>**，
   如图 9 所示。这个特殊的投票者是导致矛盾关键。

    <p align="center"><img src="/assets/img/raft-paper/9.png" width="50%" height="50%"></p>
    <p align="center">Fig 9. 如果 S1 (leader for term T) 在自己的任期内提交了一个记录，而 S5 被选为 term U 内的 leader，那至少有一个节点 (S3) 既接受了那个记录，又投票给了 S5。</p>

3. 这个投票者一定是在投票给 leaderU 之前接受的这个 entry，否则它会拒绝那次来自 leaderT 的
    AppendEntries 请求（它当前的 term 会被 T 更大）；
4. 这个投票者在投票给 leaderU 之前仍然存储着这个 entry，因为每个
   后面的 leader 都会包含这个记录 (by assumption), leaders 从不删除记录，
   而 followers 只会在与 leader 冲突时才会删除记录。
5. 这个投票者将选票给了 leaderU，这**<mark>说明 leaderU 的 log 至少与该投票者是一样新的</mark>**
  （as up-to-date as the voter’s）。这会导致如下两个矛盾：

    1. 如果 voter 和 leaderU 的最后一个 log term 是一样的，那 leaderU 的 log 至
       少与投票者的 log 一样长，因此它包含了投票者 log 中的所有记录 —— 这与前面的
       假设矛盾：前面假设了投票者包含了这个记录，而 leader U 没有。因此，
       leaderU 的最后一个 log term 必须比该投票者的要大。不仅如此，它还要比 T
       大，因为投票者的最后一个 log term 至少是 T（它包含了 term T 内的已提交记录）。
    2. 最早创建了 leaderU 的最后一个 entry 的 leader，一定在它的 log 中包含了这个已提交的 entry（根据假设）。 
     那么，根据 Log Matching Property，leaderU’s log 一定也包含了这个已提交的 entry，导出矛盾。

证明结束。

因此，大于 T 的所有 terms 内的 leader，一定包含 term T 内已提交的所有 entries。
Log Matching Property 保证了未来的 leaders 也会包含间接提交的记录，例如图 8 (d) 中的 index=2 记录。

Given leader Completeness Property, we can prove the State Machine Safety
Property from Figure 3, which states that if a server has applied a log entry
at a given index to its state machine, no other server will ever apply a
different log entry for the same index. At the time a server applies a log
entry to its state machine, its log must be identical to leader’s log up
through that entry and the entry must be committed. Now consider the lowest
term in which any server applies a given log index; the Log Completeness
Property guarantees that leaders for all higher terms will store that same log
entry, so servers that apply the index in later terms will apply the same
value.  Thus, the State Machine Safety Property holds.

Finally, Raft requires servers to apply entries in log index order. Combined
with the State Machine Safety Property, this means that all servers will
apply exactly the same set of log entries to their state machines, in the same order.

## 5.5 Follower/candidate 故障：无限重试 + 请求幂等

到目前为止讨论的都是 leader 挂掉的情况。
这一节讨论下 follower 和 candidate 挂掉的情况，这种其实更简单，而且处理方式是一样的。

如果一个 follower 或 canditate 挂掉了，那后面来的 RequestVote 和 AppendEntries 请求都会失败。

* Raft 处理这种场景的方式是**<mark>无限重试</mark>**，因此只要这个节点能起来，这个 RPC 请求就一定会成功完成。
* 如果一个节点在完成了 RPC 之后、响应之前挂了，那它会在重启之后收到完全相同的 RPC 。**<mark>Raft RPC 是幂等的</mark>**，因此这不会导致问题。
  例如，如果一个 follower 收到了一个 AppendEntries 请求，而它的 log 中已经包含了待 append 的 entry，
  那它会**<mark>直接忽略</mark>**这个请求。

## 5.6 时序和可用性

Raft 的一个设计要求是**<mark>安全性（无冲突）绝对不能依赖时序</mark>**（safety must not depend on timing）：
绝对不能因为某些事件发生的快或慢了，就导致系统产生不正确的结果。

但另一方面，**<mark>可用性</mark>**（系统及时对客户端作出响应）**<mark>对时序的依赖是不可避免的</mark>**。
例如，有节点挂掉时，消息交换过程肯定会耗时更长，任何一个 candidate 可能都无法等到一个选举结果；
而没有一个稳定 leader 的话，Raft 就无法处理任何请求。

Raft 中**<mark>最依赖时序的就是 leader 选举部分</mark>**。
只要系统满足如下时序条件，Raft 就能选出和保持一个稳定的 leader：

<p align="center"><mark><i>broadcastTime ≪ electionTimeout ≪ MTBF</i></mark></p>

* broadcastTime：一个节点并发给其他节点发送请求并收到响应的平均时间；
* electionTimeout： 5.2 小节定义的选举超时时间；
* MTBF：单个节点的平均故障时间（average time between failures）

这个不等式表达的意思很好理解：

1. 广播耗时要比选举超时低一个数量级，这样 **<mark>leader 才能可靠地发送心跳消息给 follower，避免它们发起来新的选举</mark>**。
   考虑到选举超时是随机化的，这个不等式也使得投票分裂不太可能发生。
2. 选举超时要比 MTBF 低几个数量级，这样系统才能稳步前进。
   当 **<mark>leader 挂掉后，系统大致会经历一个 election timeout 时间段的不可用</mark>**，我们希望这个时间段只占到总时间段的很小一部分。

broadcastTime 和 MTBF 都是底层系统的特性，而 electionTimeout 是我们需要设置的。

* Raft 一般要求接收方将请求**<mark>持久化</mark>**到稳定存储中，因此取决于存储技术，broadcastTime 可能需要 **<mark><code>0.5ms ~ 20ms</code></mark>**。
* 因此，electionTimeout 通常选择 **<mark><code>10ms ~ 500ms</code></mark>**。
* 典型的节点 **<mark>MTBF 是几个月或更长时间</mark>**，因此很容易满足时序要求。

# 6 集群节点数量变化（membership changes）

到目前为止，我们都是假设了**<mark>集群配置（节点集合）是不变的</mark>**。
但在实际场景中，有时需要增加或删除节点，例如节点故障时用新节点替换某个老节点，或者直接添加或删除节点。
显然，`关闭集群 -> 更新配置文件 -> 重庆开启集群` 的方式可以工作，但问题是操作期间集群不可用，
而且还可能因为其中的手动操作引发故障。为避免这些问题，我们决定**<mark>自动化配置变更，并将其包含到共识算法中</mark>**。

## 6.1 增删节点可能导致集群分裂

这里的本质问题就是**<mark>避免在增删节点期间同时出现两个及以上 leader</mark>**。
不幸的是，**<mark>不管用什么方式，这个过程都是不安全的</mark>**（unsafe）：我们
无法在同一时刻原子地切换所有节点，因此在变更时，集群可能会分裂为两个独立的大多数
（two independent majorities），见图 10，

<p align="center"><img src="/assets/img/raft-paper/10.png" width="45%" height="45%"></p>
<p align="center">Fig 10. 节点数量从 3 个增加到 5 个。
这个<mark>过程是不安全的</mark>，因为不同节点的切换发生在不同时刻：例如
箭头指向的时刻，<mark>集群分裂成了两个大多数</mark>，分别用的老配置
C<sub>old</sub>（server 1/2）和新配置 C<sub>new</sub>（server 3/4/5），
<mark>各自选出了一个 leader</mark>。
</p>

## 6.2 解决办法：两阶段方式

为确保安全，配置变更**<mark>必须使用一种两阶段方式</mark>**（a two-phase approach）来完成。
有很多种实现两阶段的方式，例如，某些系统 (e.g., [22]) 先在第一个阶段禁用老配置，这样它就不能继续处理新的客户端请求了；然后再通过
第二个阶段来启用新配置。在 Raft 中，

1. 集群首先切换到一个我们称为**<mark>联合共识</mark>**（joint consensus）的
  **<mark>事务型配置</mark>**（transitional configuration）；
2. 一旦**<mark>联合共识提交</mark>**，系统就**<mark>切换到新配置</mark>**。

联合共识 combines both 老的和新的配置：

* 不管是新配置还是老配置，log entries 都会被复制到其他所有节点；
* 不管是新配置还是老配置，任何节点都可能会成为 leader；
* 不管是新配置还是老配置，选举或提交都需要大多数节点同意；

联合共识使得**<mark>每个节点可以在任意时刻切换配置，而不会牺牲安全性</mark>**；
而且**<mark>集群变更期间，仍然能继续服务客户端请求</mark>**。

集群配置用 replicated log 中的特殊 entry 来存储和通信，Figure 11 展示了配置变更的过程，

<p align="center"><img src="/assets/img/raft-paper/11.png" width="50%" height="50%"></p>
<p align="center">Fig 11.一次配置变更的时间线。</p>

> 虚线表示配置**<mark>已创建但还未提交</mark>**；实线表示**<mark>已提交</mark>**。
> Leader 
>
> 1. 首先在自己的 log 中创建一个配置项 C<sub>old,new</sub> 并提交；这个配置会被集群大多数节点接受；
> 2. 然后创建一个 C<sub>new</sub> 并提交到大多数节点；
> 3. 在任何时刻，Cold 和 Cnew 都不可能同时独立做出决策，也就是说最多只有一个 leader。

下面具体解释。

* 当 leader 收到一个 <code>C<sub>old</sub> -> C<sub>new</sub></code> 配置变更请求时，
  会将这个请求作为一个 log entry 存储起来，用作联合共识（图中的  C<sub>old,new</sub>）；
  然后用前面介绍的 log replication 机制将其同步到其他节点。
* 任何一个节点将这个新配置应用到自己的 log 之后，接下来它将用这个配置来做所有未来决策
  （每个节点**<mark>永远用它的 log 中的最新配置，不管这个 entry 是否已提交</mark>**）。
  这意味着 leader 会用 C<sub>old,new</sub> 的规则来决定何时 the log entry for Cold,new is committed.

如果 leader 挂了，使用  C<sub>old</sub> 或 C<sub>old,new</sub> 配置的节点们可能会选出一个新 leader，
取决于获胜的那个 candidate 是否收到了 C<sub>old,new</sub>。
但不管哪种情况下，此时（这个时间段内）C<sub>new</sub> 都无法做出单边决策。

 C<sub>old,new</sub> 提交之后，除非有其他节点的同意，否则 Cold 或 Cnew 都无法做出决策，
并且 Leader Completeness Property 确保了只有那些有 C<sub>old,new</sub> entry 的节点才能被选为 leader。
现在 leader 可以安全地创建一个 log entry 来描述 Cnew 并将其同步到集群其他节点。
同样，这份配置被每个节点看到之后就生效了（take effect）。新配置在 Cnew 的规则下被提交之后，
老的配置就无关紧要了，那些没有在新配置中的节点就都可以关掉了。
如图 11 所示，任何时刻都不会发生 Cold and Cnew
都能做出单边决策（unilateral decisions）的情况；这保证了安全性。

## 6.3 三个问题

关于节点变更，还有三个问题要解决。

### 6.3.1 新节点状态为空，需要一段时间同步 log：引入“非投票成员”

新节点加入集群时，它的 log 是空白的，因此需要一段时间来追赶到最新状态。在这
段时间内它是无法提交新 entry 的 —— 包括 C<sub>old,new</sub> entry —— 也就是说有一段时间不可用。

为避免这个问题，Raft 引入了一个特殊的、用在**<mark>配置变更前</mark>**的新阶段。在这个阶段中，
新节点是以**<mark>非投票成员</mark>**（non-voting members）加入集群的：leader 会
同步 entry 给它们，但它们并不参与投票，计算集群节点数量（大多数）时也不考虑它们。
等到新节点赶上集群其他节点的状态之后，才开始前面介绍的配置变更过程。

### 6.3.2 从集群中移除 leader 节点

这种情况下，leader 在提交了 C<sub>new</sub> 之后就**<mark>卸任</mark>**（steps down），变成 follower 状态。

这意味着会有一个时间窗口（这个 leader 提交 C<sub>new</sub> 期间），这个 leader 在
**<mark>管理着一个不包括自己的集群</mark>**；它在同步 log entries 给其他节点，但自己却并不被算作大多数。
Leader 切换发生在 C<sub>new</sub> 提交之后，因为这是新配置能独立工作的最早时刻（
从 C<sub>new</sub> 开始，肯定能选出一个 leader）；在这个时刻之前，有可能只有
C<sub>old</sub> 中的某个节点才能被选为 leader。

### 6.3.3 被移除的节点不断超时（timeout）并触发选举

被移除的节点是指不在 C<sub>new</sub> 中的节点。
这些节点**<mark>不会收到来自 leader 的心跳</mark>**，因此**<mark>会 timeout 然后开始新的选举</mark>**。
它们会带着一个**<mark>更大的 term</mark>** 发起 RequestVote 轻轻，这会迫使当前
**<mark>leader 回退到 follower 状态</mark>**。最终虽然会选出一个新 leader，但由
于这些被移除的节点会**<mark>再次 timeout 然后再次发起新一轮选举</mark>**，因此
这个**<mark>新 leader 会再次被赶下台</mark>** —— 如此循环，导致可用性很差。

为避免这个问题，节点相信已经有一个当前 leader 的情况下，会忽略 RequestVote RPC。
具体来说，如果一个节点还没有到 election timeout 状态，就收到了一条 RequestVote RPC，
它会忽略这个消息（不会更新自己的 term 或投出自己的选票）。

这对正常的选举是没影响的，因为每台节点在开始一轮选举之前，会等待至少一个最小 election timeout。
但能解决这里提到的问题：**<mark>只要 leader 能向集群其他节点正常发送心跳，就不会被更大的 term 的 candidate 赶下台</mark>**。

# 7 日志压缩（log compaction）

Raft 的 log 会随着处理客户端请求而不断增大，因此占用的存储空间越来越大，replay
时间也越来越长。如果没有一种机制来处理这些日志，最终将引发可用性故障。

## 7.1 Snapshot

Snapshot 是最简单的压缩方式。

在这种方式中，会对当前的整个日志做一次快照，将其写到持久存储上，
然后将已经做过快照的日志全部清空。**<mark>Chubby 和 ZooKeeper</mark>** 都使用了这种方式，接下来介绍 Raft 的快照方式。

```go
// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/server.go#L865

// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/server.go#L1188
func (s *server) TakeSnapshot() error {
    lastIndex, lastTerm := s.log.commitInfo()

    // check if there is log has been committed since the last snapshot.
    if lastIndex == s.log.startIndex {
        return nil
    }

    // Attach snapshot to pending snapshot and save it to disk.
    s.pendingSnapshot = &Snapshot{lastIndex, lastTerm, nil, nil, s.SnapshotPath(lastIndex, lastTerm)}

    state := s.stateMachine.Save()

    // Clone the list of peers.
    peers := make([]*Peer, 0, len(s.peers)+1)
    for _, peer := range s.peers {
        peers = append(peers, peer.clone())
    }
    peers = append(peers, &Peer{Name: s.Name(), ConnectionString: s.connectionString})

    // Attach snapshot to pending snapshot and save it to disk.
    s.pendingSnapshot.Peers = peers
    s.pendingSnapshot.State = state
    s.saveSnapshot()

    // We keep some log entries after the snapshot.
    // We do not want to send the whole snapshot to the slightly slow machines
    if lastIndex-s.log.startIndex > NumberOfLogEntriesAfterSnapshot {
        compactIndex := lastIndex - NumberOfLogEntriesAfterSnapshot
        compactTerm := s.log.getEntry(compactIndex).Term()
        s.log.compact(compactIndex, compactTerm)
    }

    return nil
}

// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/log.go#L567
// compact the log before index (including index)
func (l *Log) compact(index uint64, term uint64) error {
    var entries []*LogEntry

    // the index may be greater than the current index if we just recovery from on snapshot
    if index >= l.internalCurrentIndex() {
        entries = make([]*LogEntry, 0)
    } else {
        entries = l.entries[index-l.startIndex:] // get all log entries after index
    }

    // create a new log file and add all the entries
    new_file_path := l.path + ".new"
    file := os.OpenFile(new_file_path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0600)
    for _, entry := range entries {
        position, _ := l.file.Seek(0, os.SEEK_CUR)
        entry.Position = position

        entry.Encode(file)
    }
    file.Sync()

    old_file := l.file
    os.Rename(new_file_path, l.path) // rename the new log file
    l.file = file
    old_file.Close() // close the old log file

    // compaction the in memory log
    l.entries = entries
    l.startIndex = index
    l.startTerm = term
    return nil
}
```

<p align="center"><img src="/assets/img/raft-paper/12.png" width="40%" height="40%"></p>
<p align="center">Fig 12.
A server replaces the committed entries in its log
(indexes 1 through 5) with a new snapshot, which stores just
the current state (variables x and y in this example). The snapshot’s last included index and term serve to position the snapshot in the log preceding entry 6.
</p>

Figure 12 shows the basic idea of snapshotting in Raft.
Each server takes snapshots independently, covering just
the committed entries in its log. Most of the work consists of the state machine writing its current state to the
snapshot. Raft also includes a small amount of metadata
in the snapshot: the last included index is the index of the
last entry in the log that the snapshot replaces (the last entry the state machine had applied), and the last included
term is the term of this entry. These are preserved to support the AppendEntries consistency check for the first log
entry following the snapshot, since that entry needs a previous log index and term. To enable cluster membership
changes (Section 6), the snapshot also includes the latest
configuration in the log as of last included index. Once a
server completes writing a snapshot, it may delete all log
entries up through the last included index, as well as any
prior snapshot.

Although servers normally take snapshots independently, leader must occasionally send snapshots to
followers that lag behind. This happens when leader
has already discarded the next log entry that it needs to
send to a follower. Fortunately, this situation is unlikely
in normal operation: a follower that has kept up with the
leader would already have this entry. However, an exceptionally slow follower or a new server joining the cluster
(Section 6) would not. The way to bring such a follower
up-to-date is for leader to send it a snapshot over the
network.

> InstallSnapshot RPC
>
> Invoked by leader to send chunks of a snapshot to a follower.  Leaders always send chunks in order.
>
> Arguments:
> 
> * term leader’s term
> * leaderId so follower can redirect clients
> * lastIncludedIndex the snapshot replaces all entries up through and including this index
> * lastIncludedTerm term of lastIncludedIndex
> * offset byte offset where chunk is positioned in the snapshot file
> * data[] raw bytes of the snapshot chunk, starting at offset
> * done true if this is the last chunk
>
> Results:
>
> * term currentTerm, for leader to update itself
>
> Receiver implementation:
>
> 1. Reply immediately if `term < currentTerm`
> 2. Create new snapshot file if first chunk (offset is 0)
> 3. Write data into snapshot file at given offset
> 4. Reply and wait for more data chunks if done is false
> 5. Save snapshot file, discard any existing or partial snapshot with a smaller index
> 6. If existing log entry has same index and term as snapshot’s last included entry, retain log entries following it and reply
> 7. Discard the entire log
> 8. Reset state machine using snapshot contents (and load snapshot’s cluster configuration)

<p align="center">Fig 13.
A summary of the InstallSnapshot RPC. Snapshots are split into chunks for
transmission; this gives the follower a sign of life with each chunk, so it can
reset its election timer.
</p>

The leader uses a new RPC called InstallSnapshot to
send snapshots to followers that are too far behind; see
Figure 13. When a follower receives a snapshot with this
RPC, it must decide what to do with its existing log entries. Usually the snapshot will contain new information
not already in the recipient’s log. In this case, the follower
discards its entire log; it is all superseded by the snapshot
and may possibly have uncommitted entries that conflict
with the snapshot. If instead the follower receives a snapshot that describes a prefix of its log (due to retransmission or by mistake), then log entries covered by the snapshot are deleted but entries following the snapshot are still
valid and must be retained.

This snapshotting approach departs from Raft’s strong
leader principle, since followers can take snapshots without the knowledge of leader. However, we think this
departure is justified. While having a leader helps avoid
conflicting decisions in reaching consensus, consensus
has already been reached when snapshotting, so no decisions conflict. Data still only flows from leaders to
followers, just followers can now reorganize their data.

We considered an alternative leader-based approach in
which only leader would create a snapshot, then it
would send this snapshot to each of its followers. However, this has two disadvantages. First, sending the snapshot to each follower would waste network bandwidth and
slow the snapshotting process. Each follower already has
the information needed to produce its own snapshots, and
it is typically much cheaper for a server to produce a snapshot from its local state than it is to send and receive one
over the network. Second, leader’s implementation
would be more complex. For example, leader would
need to send snapshots to followers in parallel with replicating new log entries to them, so as not to block new
client requests.

There are two more issues that impact snapshotting performance. First, servers must decide when to snapshot. If
a server snapshots too often, it wastes disk bandwidth and
energy; if it snapshots too infrequently, it risks exhausting its storage capacity, and it increases the time required
to replay the log during restarts. One simple strategy is
to take a snapshot when the log reaches a fixed size in
bytes. If this size is set to be significantly larger than the
expected size of a snapshot, then the disk bandwidth overhead for snapshotting will be small.

The second performance issue is that writing a snapshot can take a significant amount of time, and we do
not want this to delay normal operations. The solution is
to use copy-on-write techniques so that new updates can
be accepted without impacting the snapshot being written. For example, state machines built with functional data
structures naturally support this. Alternatively, the operating system’s copy-on-write support (e.g., fork on Linux)
can be used to create an in-memory snapshot of the entire
state machine (our implementation uses this approach).

## 7.2 增量压缩

增量压缩方式，例如 log cleaning [36] and log-structured merge trees [30, 5]，也是可行的。
这些只对一部分数据进行操作，因此压缩所占用的负载是随时间分布更加均匀。

* 首先选择一块已经积累了一些已删除和已覆盖对象（deleted and overwritten objects）的数据区域，
* 然后以更紧凑的方式，重写（rewrite）这些区域内还活着的对象（live objects from that region）

这比 snapshot 方式要更精细但也更复杂，前者是直接作用在整个数据集上。
虽然 log cleaning 需要对 Raft 做出改动，但 Raft state machines 可以用与 snapshot 相同的接口来实现 LSM
trees。

# 8 客户端交互

本节介绍客户端如何寻找集群 leader、Raft 如何支持 linearizable semantics [10] 等客户端交互内容，
这些是所有**<mark>基于共识的系统</mark>**（consensus-based systems）共同面临的问题，
Raft 的处理方式也**<mark>与其他系统类似</mark>**。

## 8.1 寻找 leader

Raft 中，客户端会将**<mark>所有请求都发给 leader</mark>**。但 leader 可能会发生切换，
如何确保客户端每次都能找到 leader 呢？

客户端启动时，首先会**<mark>随机</mark>**选择一个 raft 节点进行连接，

* 如果该节点是 leader，就会直接处理请求；
* 如果该节点不是 leader，就会拒绝这个请求，并**<mark>将 leader 信息告诉客户端</mark>**。

> `AppendEntries` 请求包含了 leader 信息，因此每个 follower 都知道哪个节点是 leader。
>
> ```go
> // https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/append_entries.go#L11
> 
> // The request sent to a server to append entries to the log.
> type AppendEntriesRequest struct {
>     Term         uint64
>     PrevLogIndex uint64
>     PrevLogTerm  uint64
>     CommitIndex  uint64
>     LeaderName   string
>     Entries      []*protobuf.LogEntry
> }
> ```

如果 leader 挂了，客户端请求会超时，然后重新随机选择一个节点进行连接。

## 8.2 可线性化语义（避免重复执行同一客户端的相同命令）

Raft 的目标之一是实现线性语义（linearizable semantics）：每个操作看起来都是
**<mark>立即执行</mark>**（execute instantaneously）、**<mark>精确执行一次</mark>**（exactly once）。
但前面也提到，Raft 可能会**<mark>多次执行同一条命令</mark>**：例如，leader 在提交了
来自客户端的 log entry 但还没返回响应时就挂了，那客户端会向新 leader 重试这个命令，导致被第二次执行。

我们的解决方案是：

1. 给客户端的每个命令分配**<mark>唯一的顺序编号</mark>**（unique serial numbers），
2. 状态机为每个客户端记录**<mark>最后已执行的命令序号</mark>**，放到响应中，
3. 状态机如果发现某个序号的命令已经执行过，就会直接返回，不会再执行一遍。

```go
// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/append_entries.go#L21

// The response returned from a server appending entries to the log.
type AppendEntriesResponse struct {
    pb     *protobuf.AppendEntriesResponse
    peer   string
    append bool
}

// https://github.com/etcd-io/etcd/blob/release-0.4/third_party/github.com/goraft/raft/protobuf/append_entries_responses.pb.go#L35
type AppendEntriesResponse struct {
    Term             *uint64 `protobuf:"varint,1,req" json:"Term,omitempty"`
    Index            *uint64 `protobuf:"varint,2,req" json:"Index,omitempty"`
    CommitIndex      *uint64 `protobuf:"varint,3,req" json:"CommitIndex,omitempty"`
    Success          *bool   `protobuf:"varint,4,req" json:"Success,omitempty"`
    XXX_unrecognized []byte  `json:"-"`
}
```

## 8.3 只读操作

只读操作**<mark>无需向 log 文件写入任何内容</mark>**。
但如果没有额外措施，可能会有**<mark>返回过期数据的风险</mark>**，因为处理这次请求的
leader 可能已经被一个新 leader 取代了，而它自己还不知道。

**<mark>可线性化读</mark>**（linearizable reads）能确保不会返回过期数据，
要支持这一特性，Raft 需要针对 leader 引入两个额外措施：

1. Leader 必须有 **<mark>“哪些 entry 已经被提交”</mark>** 的最新信息。

    Leader Completeness 特性保证了 leader 有所有的已提交 entry，但在它的任期刚开始时，
    it may not know which those are. To find out, it needs to commit an entry from its term.

    Raft 的处理方式是，让每个 **<mark>leader 在任期开始时，提交一个空的 no-op entry</mark>** 到 log。

2. leader 在处理只读请求之前，必须能**<mark>检查自己是否被剥夺了 leader 位置</mark>**。

    Raft 的处理方式是，在处理只读请求之前，让 leader 与集群的大多数节点交换心跳消息。
    或者，leader 可以依靠心跳机制提供一种租约形式 [9]，但这需要依赖 timing for safety (it assumes bounded clock skew).

# 9 实现与评估

We have implemented Raft as part of a replicated state machine that stores
configuration information for RAMCloud [33] and assists in failover of the
RAMCloud coordinator. The Raft implementation contains roughly
**<mark>2000 lines of C++ code</mark>**, not including tests, comments, or
blank lines. The [source code](http://github.com/logcabin/logcabin) is freely available [23]. There
are also about 25 independent third-party open source implementations [34] of
Raft in various stages of development, based on drafts of this paper. Also,
various companies are deploying Raft-based systems [34].  The remainder of this
section **<mark>evaluates Raft using three criteria</mark>**:

1. understandability
2. correctness
3. performance

## 9.1 可理解性

To measure Raft’s understandability relative to Paxos, we conducted an
experimental study using upper-level undergraduate and graduate students in an
Advanced Operating Systems course at Stanford University and a Distributed
Computing course at U.C. Berkeley. We recorded a video lecture of Raft and
another of Paxos, and created corresponding quizzes.

* The Raft lecture covered the content of this paper except for log compaction;
* The Paxos lecture covered enough material to create an equivalent replicated
  state machine, including single-decree Paxos, multi-decree Paxos,
  reconfiguration, and a few optimizations needed in practice (such as leader
  election).

The quizzes tested basic understanding of the algorithms and also required
students to reason about corner cases. Each student watched one video, took the
corresponding quiz, watched the second video, and took the second quiz.  About
half of the participants did the Paxos portion first and the other half did the
Raft portion first in order to account for both individual differences in
performance and experience gained from the first portion of the study.

We compared participants’ scores on each quiz to determine whether participants
showed a better understanding of Raft.  We tried to make the comparison between
Paxos and Raft as fair as possible. The experiment favored Paxos in two ways:
15 of the 43 participants reported having some prior experience with Paxos, and
the Paxos video is 14% longer than the Raft video. As summarized in Table 1, we
have taken steps to mitigate potential sources of bias.

| Concern | Steps taken to mitigate bias | Materials for review [28, 31] |
|:----|:-----|:----|
| Equal lecture quality | Same lecturer for both. Paxos lecture based on and improved from existing materials used in several universities. Paxos lecture is 14% longer. | videos |
| Equal quiz difficulty | Questions grouped in difficulty and paired across exams. | quizzes |
| Fair grading Used rubric. | Graded in random order, alternating between quizzes. | rubric |

<p align="center">Table 1: Concerns of possible bias against Paxos in the study, steps taken to counter each, and additional materials available.</p>

All of our materials are available for review [28, 31].  On average,
participants scored 4.9 points higher on the Raft quiz than on the Paxos quiz
(out of a possible 60 points, the mean Raft score was 25.7 and the mean Paxos
score was 20.8); Figure 14 shows their individual scores.

<p align="center"><img src="/assets/img/raft-paper/14.png" width="40%" height="40%"></p>
<p align="center">Fig 14.
A scatter plot comparing 43 participants’ performance on the Raft
and Paxos quizzes. Points above the diagonal (33) represent participants who
scored higher for Raft.
</p>

A paired t-test states that, with 95% confidence, the true distribution of Raft
scores has a mean at least 2.5 points larger than the true distribution of
Paxos scores.  We also created a linear regression model that predicts a new
student’s quiz scores based on three factors: which quiz they took, their
degree of prior Paxos experience, and the order in which they learned the
algorithms. The model predicts that the choice of quiz produces a 12.5-point
difference in favor of Raft. This is significantly higher than the observed
difference of 4.9 points, because many of the actual students had prior Paxos
experience, which helped Paxos considerably, whereas it helped Raft slightly
less.  Curiously, the model also predicts scores 6.3 points lower on Raft for
people that have already taken the Paxos quiz; although we don’t know why, this
does appear to be statistically significant.

We also surveyed participants after their quizzes to see which algorithm they
felt would be easier to implement or explain; these results are shown in Figure 15.

<p align="center"><img src="/assets/img/raft-paper/15.png" width="60%" height="60%"></p>
<p align="center">Fig 15. Using a 5-point scale, participants were asked
(left) which algorithm they felt would be easier to implement
in a functioning, correct, and efficient system, and (right)
which would be easier to explain to a CS graduate student.
</p>

An overwhelming majority of participants reported Raft would be easier to
implement and explain (33 of 41 for each question). However, these
self-reported feelings may be less reliable than participants’ quiz scores, and
participants may have been biased by knowledge of our hypothesis that Raft is
easier to understand.  A detailed discussion of the Raft user study is
available at [31].

## 9.2 正确性

We have developed a formal specification and a proof of safety for the
consensus mechanism described in Section 5. The formal specification [31] makes
the information summarized in Figure 2 completely precise using the
**<mark>TLA+ specification language</mark>** [17]. It is about 400 lines
long and serves as the subject of the proof. It is also useful on its own for
anyone implementing Raft. We have mechanically proven the Log Completeness
Property using the TLA proof system [7]. However, this proof relies on
invariants that have not been mechanically checked (for example, we have not
proven the type safety of the specification). Furthermore, we have written an
informal proof [31] of the State Machine Safety property which is complete (it
relies on the specification alone) and relatively precise (it is about 3500 words long).

## 9.3 性能

Raft’s performance is similar to other consensus algorithms such as Paxos. The
most important case for performance is when an established leader is
replicating new log entries. Raft achieves this using the minimal number of
messages (a single round-trip from leader to half the cluster). It is also
possible to further improve Raft’s performance. For example, it easily supports
batching and pipelining requests for higher throughput and lower latency.
Various optimizations have been proposed in the literature for other
algorithms; many of these could be applied to Raft, but we leave this to future
work.  We used our Raft implementation to measure the performance of Raft’s
leader election algorithm and answer two questions. First, does the election
process converge quickly? Second, what is the minimum downtime that can be
achieved after leader crashes?

To measure leader election, we repeatedly crashed the
leader of a cluster of five servers and timed how long it
took to detect the crash and elect a new leader (see Figure 16).

<p align="center"><img src="/assets/img/raft-paper/16.png" width="50%" height="50%"></p>
<p align="center">Fig 16. 检测与替换一个挂掉的 leader 所需的时间。
The top graph varies the amount of randomness in election
timeouts, and the bottom graph scales the minimum election
timeout. Each line represents 1000 trials (except for 100 trials for “150–150ms”) and corresponds to a particular choice
of election timeouts; for example, “150–155ms” means that
election timeouts were chosen randomly and uniformly between 150ms and 155ms. The measurements were taken on a
cluster of five servers with a broadcast time of roughly 15ms.
Results for a cluster of nine servers are similar.
</p>

To generate a worst-case scenario, the servers in each trial had different log
lengths, so some candidates were not eligible to become leader. Furthermore, to
encourage split votes, our test script triggered a synchronized broadcast of
heartbeat RPCs from leader before terminating its process (this approximates
the behavior of leader replicating a new log entry prior to crashing). leader
was crashed uniformly randomly within its heartbeat interval, which was half of
the minimum election timeout for all tests. Thus, the smallest possible
downtime was about half of the minimum election timeout.

The top graph in Figure 16 shows that a small amount of randomization in the
election timeout is enough to avoid split votes in elections. In the absence of
randomness, leader election consistently took longer than 10 seconds in our
tests due to many split votes. Adding just 5ms of randomness helps
significantly, resulting in a median downtime of 287ms. Using more randomness
improves worst-case behavior: with 50ms of randomness the worstcase completion
time (over 1000 trials) was 513ms.

The bottom graph in Figure 16 shows that downtime can be reduced by reducing
the election timeout. With an election timeout of 12–24ms, it takes only 35ms
on average to elect a leader (the longest trial took 152ms).

However, lowering the timeouts beyond this point violates Raft’s timing
requirement: leaders have difficulty broadcasting heartbeats before other
servers start new elections.  This can cause unnecessary leader changes and
lower overall system availability. We recommend using a conservative election
timeout such as 150–300ms; such timeouts are unlikely to cause unnecessary
leader changes and will still provide good availability.

# 10 相关工作

There have been numerous publications related to consensus algorithms, many of which fall into one of the following categories:

* Lamport’s original description of Paxos [15], and attempts to explain it more clearly [16, 20, 21].
* Elaborations of Paxos, which fill in missing details and modify the algorithm
  to provide a better foundation for implementation [26, 39, 13].
* Systems that implement consensus algorithms, such as Chubby [2, 4], ZooKeeper
  [11, 12], and Spanner [6]. The algorithms for Chubby and Spanner have not
  been published in detail, though both claim to be based on Paxos. ZooKeeper’s
  algorithm has been published in more detail, but it is quite different from Paxos.
* Performance optimizations that can be applied to Paxos [18, 19, 3, 25, 1, 27].
* Oki and Liskov’s Viewstamped Replication (VR), an alternative approach to
  consensus developed around the same time as Paxos. The original description
  [29] was intertwined with a protocol for distributed transactions, but the
  core consensus protocol has been separated in a recent update [22]. VR uses a
  leaderbased approach with many similarities to Raft.

The greatest difference between Raft and Paxos is Raft’s strong leadership:
Raft uses leader election as an essential part of the consensus protocol, and
it concen- 15trates as much functionality as possible in leader. This approach
results in a simpler algorithm that is easier to understand. For example, in
Paxos, leader election is orthogonal to the basic consensus protocol: it serves
only as a performance optimization and is not required for achieving consensus.
However, this results in additional mechanism: Paxos includes both a two-phase
protocol for basic consensus and a separate mechanism for leader election.  In
contrast, Raft incorporates leader election directly into the consensus
algorithm and uses it as the first of the two phases of consensus. This results
in less mechanism than in Paxos.

Like Raft, VR and ZooKeeper are leader-based and therefore share many of Raft’s
advantages over Paxos.  However, Raft has less mechanism that VR or ZooKeeper
because it minimizes the functionality in non-leaders. For example, log entries
in Raft flow in only one direction: outward from leader in AppendEntries RPCs.
In VR log entries flow in both directions (leaders can receive log entries
during the election process); this results in additional mechanism and
complexity. The published description of ZooKeeper also transfers log entries
both to and from leader, but the implementation is apparently more like Raft
[35].

Raft has fewer message types than any other algorithm for consensus-based log
replication that we are aware of. For example, we counted the message types VR
and ZooKeeper use for basic consensus and membership changes (excluding log
compaction and client interaction, as these are nearly independent of the
algorithms). VR and ZooKeeper each define 10 different message types, while
Raft has only 4 message types (two RPC requests and their responses). Raft’s
messages are a bit more dense than the other algorithms’, but they are simpler
collectively. In addition, VR and ZooKeeper are described in terms of
transmitting entire logs during leader changes; additional message types will
be required to optimize these mechanisms so that they are practical.

> Raft’s strong leadership approach simplifies the algorithm, but it precludes some
> performance optimizations.  For example, Egalitarian Paxos (EPaxos) can achieve
> higher performance under some conditions with a leaderless approach [27].
> EPaxos exploits commutativity in state machine commands. Any server can commit
> a command with just one round of communication as long as other commands that
> are proposed concurrently commute with it. However, if commands that are
> proposed concurrently do not commute with each other, EPaxos requires an
> additional round of communication. Because any server may commit commands,
> EPaxos balances load well between servers and is able to achieve lower latency
> than Raft in WAN settings. However, it adds significant complexity to Paxos.

Several different approaches for cluster membership changes have been proposed
or implemented in other work, including Lamport’s original proposal [15], VR
[22], and SMART [24]. We chose the joint consensus approach for Raft because it
leverages the rest of the consensus protocol, so that very little additional
mechanism is required for membership changes. Lamport’s α-based approach was
not an option for Raft because it assumes consensus can be reached without a
leader. In comparison to VR and SMART, Raft’s reconfiguration algorithm has the
advantage that membership changes can occur without limiting the processing of
normal requests; in contrast, VR stops all normal processing during
configuration changes, and SMART imposes an α-like limit on the number of
outstanding requests. Raft’s approach also adds less mechanism than either VR
or SMART.

# 11 总结

Algorithms are often designed with correctness, efficiency, and/or conciseness
as the primary goals. Although these are all worthy goals, we believe that
understandability is just as important. None of the other goals can be achieved
until developers render the algorithm into a practical implementation, which
will inevitably deviate from and expand upon the published form. Unless
developers have a deep understanding of the algorithm and can create intuitions
about it, it will be difficult for them to retain its desirable properties in
their implementation.

In this paper we addressed the issue of distributed consensus, where a widely
accepted but impenetrable algorithm, Paxos, has challenged students and
developers for many years. We developed a new algorithm, Raft, which we have
shown to be more understandable than Paxos.

We also believe that Raft provides a better foundation for system building.
Using understandability as the primary design goal changed the way we
approached the design of Raft; as the design progressed we found ourselves
reusing a few techniques repeatedly, such as decomposing the problem and
simplifying the state space. These techniques not only improved the
understandability of Raft but also made it easier to convince ourselves of its
correctness.

# 12 致谢

The user study would not have been possible without the support of Ali Ghodsi,
David Mazieres, and the students of CS 294-91 at Berkeley and CS 240 at
Stanford. Scott Klemmer helped us design the user study, and Nelson Ray advised
us on statistical analysis. The Paxos slides for the user study borrowed
heavily from a slide deck originally created by Lorenzo Alvisi. Special thanks
go to David Mazieres and Ezra Hoch for finding subtle bugs in Raft. Many people
provided helpful feedback on the paper and user study materials, including Ed
Bugnion, Michael Chan, Hugues Evrard, Daniel Giffin, Arjun Gopalan, Jon Howell,
Vimalkumar Jeyakumar, Ankita Kejriwal, Aleksandar Kracun, Amit Levy, Joel
Martin, Satoshi Matsushita, Oleg Pesok, David Ramos, Robbert van Renesse,
Mendel Rosenblum, Nicolas Schiper, Deian Stefan, Andrew Stone, Ryan Stutsman,
David Terei, Stephen Yang, Matei Zaharia, 24 anonymous conference reviewers
(with duplicates), and especially our shepherd Eddie Kohler. Werner Vogels
tweeted a link to an earlier draft, which gave Raft significant exposure. This
work was supported by the Gigascale Systems Research Center and the Multiscale
Systems Center, two of six research centers funded under the Focus Center
Research Program, a Semiconductor Research Corporation program, by STARnet, a
Semiconductor Research Corporation program sponsored by MARCO and DARPA, by the
National Science Foundation under Grant No. 0963859, and by grants from
Facebook, Google, Mellanox, NEC, NetApp, SAP, and Samsung. Diego Ongaro is
supported by The Junglee Corporation Stanford Graduate Fellowship.

# 参考文献

* [1] BOLOSKY, W. J., BRADSHAW, D., HAAGENS, R. B., KUSTERS, N. P., AND LI, P. Paxos replicated state machines as the basis of a high-performance data store.  In Proc. NSDI’11, USENIX Conference on Networked Systems Design and Implementation (2011), USENIX, pp. 141–154.
* [2] BURROWS, M. The Chubby lock service for looselycoupled distributed systems. In Proc. OSDI’06, Symposium on Operating Systems Design and Implementation (2006), USENIX, pp. 335–350.
* [3] CAMARGOS, L. J., SCHMIDT, R. M., AND PEDONE, F.  Multicoordinated Paxos. In Proc. PODC’07, ACM Symposium on Principles of Distributed Computing (2007), ACM, pp. 316–317.
* [4] CHANDRA, T. D., GRIESEMER, R., AND REDSTONE, J.  Paxos made live: an engineering perspective. In Proc.  PODC’07, ACM Symposium on Principles of Distributed Computing (2007), ACM, pp. 398–407.
* [5] CHANG, F., DEAN, J., GHEMAWAT, S., HSIEH, W. C., WALLACH, D. A., BURROWS, M., CHANDRA, T., FIKES, A., AND GRUBER, R. E. Bigtable: a distributed storage system for structured data. In Proc. OSDI’06, USENIX Symposium on Operating Systems Design and Implementation (2006), USENIX, pp. 205–218.
* [6] CORBETT, J. C., DEAN, J., EPSTEIN, M., FIKES, A., FROST, C., FURMAN, J. J., GHEMAWAT, S., GUBAREV, A., HEISER, C., HOCHSCHILD, P., HSIEH, W., KANTHAK, S., KOGAN, E., LI, H., LLOYD, A., MELNIK, S., MWAURA, D., NAGLE, D., QUINLAN, S., RAO, R., ROLIG, L., SAITO, Y., SZYMANIAK, M., TAYLOR, C., WANG, R., AND WOODFORD, D. Spanner: Google’s globally-distributed database. In Proc. OSDI’12, USENIX Conference on Operating Systems Design and Implementation (2012), USENIX, pp. 251–264.
* [7] COUSINEAU, D., DOLIGEZ, D., LAMPORT, L., MERZ, S., RICKETTS, D., AND VANZETTO, H. TLA+ proofs.  In Proc. FM’12, Symposium on Formal Methods (2012), D. Giannakopoulou and D. M´ery, Eds., vol. 7436 of Lecture Notes in Computer Science, Springer, pp. 147–154.
* [8] GHEMAWAT, S., GOBIOFF, H., AND LEUNG, S.-T. The Google file system. In Proc. SOSP’03, ACM Symposium on Operating Systems Principles (2003), ACM, pp. 29–43.
* [9] GRAY, C., AND CHERITON, D. Leases: An efficient faulttolerant mechanism for distributed file cache consistency.  In Proceedings of the 12th ACM Ssymposium on Operating Systems Principles (1989), pp. 202–210.
* [10] HERLIHY, M. P., AND WING, J. M. Linearizability: a correctness condition for concurrent objects. ACM Transactions on Programming Languages and Systems 12 (July 1990), 463–492.
* [11] HUNT, P., KONAR, M., JUNQUEIRA, F. P., AND REED, B. ZooKeeper: wait-free coordination for internet-scale systems. In Proc ATC’10, USENIX Annual Technical Conference (2010), USENIX, pp. 145–158.
* [12] JUNQUEIRA, F. P., REED, B. C., AND SERAFINI, M.  Zab: High-performance broadcast for primary-backup systems. In Proc. DSN’11, IEEE/IFIP Int’l Conf. on Dependable Systems & Networks (2011), IEEE Computer Society, pp. 245–256.
* [13] KIRSCH, J., AND AMIR, Y. Paxos for system builders.  Tech. Rep. CNDS-2008-2, Johns Hopkins University, 2008.
* [14] LAMPORT, L. Time, clocks, and the ordering of events in a distributed system. Commununications of the ACM 21, 7 (July 1978), 558–565.
* [15] LAMPORT, L. The part-time parliament. ACM Transactions on Computer Systems 16, 2 (May 1998), 133–169.
* [16] LAMPORT, L. Paxos made simple. ACM SIGACT News 32, 4 (Dec. 2001), 18–25.
* [17] LAMPORT, L. Specifying Systems, The TLA+ Language and Tools for Hardware and Software Engineers. AddisonWesley, 2002.
* [18] LAMPORT, L. Generalized consensus and Paxos. Tech.  Rep. MSR-TR-2005-33, Microsoft Research, 2005.
* [19] LAMPORT, L. Fast paxos. Distributed Computing 19, 2 (2006), 79–103.
* [20] LAMPSON, B. W. How to build a highly available system using consensus. In Distributed Algorithms, O. Baboaglu and K. Marzullo, Eds. Springer-Verlag, 1996, pp. 1–17.
* [21] LAMPSON, B. W. The ABCD’s of Paxos. In Proc.  PODC’01, ACM Symposium on Principles of Distributed Computing (2001), ACM, pp. 13–13.
* [22] LISKOV, B., AND COWLING, J. Viewstamped replication revisited. Tech. Rep. MIT-CSAIL-TR-2012-021, MIT, July 2012.
* [23] LogCabin source code. http://github.com/logcabin/logcabin.  17[24] LORCH, J. R., ADYA, A., BOLOSKY, W. J., CHAIKEN, R., DOUCEUR, J. R., AND HOWELL, J. The SMART way to migrate replicated stateful services. In Proc. EuroSys’06, ACM SIGOPS/EuroSys European Conference on Computer Systems (2006), ACM, pp. 103–115.
* [25] MAO, Y., JUNQUEIRA, F. P., AND MARZULLO, K.  Mencius: building efficient replicated state machines for WANs. In Proc. OSDI’08, USENIX Conference on Operating Systems Design and Implementation (2008), USENIX, pp. 369–384.
* [26] MAZIERES, D. Paxos made practical. http: //www.scs.stanford.edu/˜dm/home/ papers/paxos.pdf, Jan. 2007.
* [27] MORARU, I., ANDERSEN, D. G., AND KAMINSKY, M.  There is more consensus in egalitarian parliaments. In Proc. SOSP’13, ACM Symposium on Operating System Principles (2013), ACM.
* [28] Raft user study. http://ramcloud.stanford.  edu/˜ongaro/userstudy/.
* [29] OKI, B. M., AND LISKOV, B. H. Viewstamped replication: A new primary copy method to support highly-available distributed systems. In Proc. PODC’88, ACM Symposium on Principles of Distributed Computing (1988), ACM, pp. 8–17.
* [30] O’NEIL, P., CHENG, E., GAWLICK, D., AND ONEIL, E.  The log-structured merge-tree (LSM-tree). Acta Informatica 33, 4 (1996), 351–385.
* [31] ONGARO, D. Consensus: Bridging Theory and Practice.  PhD thesis, Stanford University, 2014 (work in progress).  http://ramcloud.stanford.edu/˜ongaro/ thesis.pdf.
* [32] ONGARO, D., AND OUSTERHOUT, J. In search of an understandable consensus algorithm. In Proc ATC’14, USENIX Annual Technical Conference (2014), USENIX.
* [33] OUSTERHOUT, J., AGRAWAL, P., ERICKSON, D., KOZYRAKIS, C., LEVERICH, J., MAZIERES, D., MITRA, S., NARAYANAN, A., ONGARO, D., PARULKAR, G., ROSENBLUM, M., RUMBLE, S. M., STRATMANN, E., AND STUTSMAN, R. The case for RAMCloud. Communications of the ACM 54 (July 2011), 121–130.
* [34] Raft consensus algorithm website.  http://raftconsensus.github.io.
* [35] REED, B. Personal communications, May 17, 2013.
* [36] ROSENBLUM, M., AND OUSTERHOUT, J. K. The design and implementation of a log-structured file system. ACM Trans. Comput. Syst. 10 (February 1992), 26–52.
* [37] SCHNEIDER, F. B. Implementing fault-tolerant services using the state machine approach: a tutorial. ACM Computing Surveys 22, 4 (Dec. 1990), 299–319.
* [38] SHVACHKO, K., KUANG, H., RADIA, S., AND CHANSLER, R. The Hadoop distributed file system.  In Proc. MSST’10, Symposium on Mass Storage Systems and Technologies (2010), IEEE Computer Society, pp. 1–10.
* [39] VAN RENESSE, R. Paxos made moderately complex.  Tech. rep., Cornell University, 2012.
