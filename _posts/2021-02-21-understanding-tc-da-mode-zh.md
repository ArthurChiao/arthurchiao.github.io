---
layout    : post
title     : "[译] 深入理解 tc ebpf 的 direct-action (da) 模式（2020）"
date      : 2021-02-21
lastupdate: 2021-02-21
categories: bpf tc
---

### 译者序

本文翻译自 2020 年 Quentin Monnet 的一篇英文博客：
[Understanding tc “direct action” mode for BPF](https://qmonnet.github.io/whirl-offload/2020/04/11/tc-bpf-direct-action/)。

Quentin Monnet 是 Cilium 开发者之一。

如作者所说，`da` 模式不仅是使用 tc ebpf 程序的推荐方式，而且（据他所知，截至本文
写作时）也是唯一方式。所以，很多人一直在使用它（包括通过 Cilium 间接使用），却没
有深挖过它到底是什么意思 —— 这样用就行了。

本文结合 tc/ebpf 开发史，介绍了 `da` 模式的来龙去脉，并给出了例子、内核及 iproute2/tc 中的实现。

翻译已获得 Quentin Monnet 授权。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

Linux 的流量控制子系统（Traffic Control, TC）已经在内核中存在多年，并仍处于活跃开发之中。
Kernel `4.1` 的一个重要变化是：**添加了一些新的 hook**，并<mark>支持将 eBPF 程序作为
tc classifier（也称为 filter） 或 tc action 加载到这些 hook 点</mark>。大概六个月之后，
kernel `4.4` 发布时，<mark>iproute2 引入了一个 <code>direct-action</code> 模式</mark>，但**关于这个模式的文档甚少**。

> 本文初稿时，除了 commit log 之外，没有关于 `direct-action` 的其他文档。如今
> [Cilium Guide](http://docs.cilium.io/en/latest/bpf/#tc-traffic-control) 及 `tc-bpf(8)` 中
> 都有了一些简要描述，说这个模式 “instructs
> eBPF classifier to not invoke external TC actions, instead use the TC
> actions return codes (`TC_ACT_OK`, `TC_ACT_SHOT` etc.) for classifiers.”

# 1 背景知识：Linux 流量控制（tc）子系统

在介绍 `direct-action` 之前，需要先回顾一下 Linux TC 的经典使用场景和使用方式。

**流量控制最终是在内核中完成的**：tc 模块根据不同算法对网络设备上的流量进行控制
（限速、设置优先级等等）。用户一般通过 iproute2 中的 `tc` 工具完成配置 —— 这是与
内核 TC 子系统相对应的**用户侧工具** —— <mark>二者之间（大部分情况下）通过
Netlink 消息通信</mark>。

## 1.1 tc 术语

TC 是一个强大但复杂的框架（且[文档](https://qmonnet.github.io/whirl-offload/2016/09/01/dive-into-bpf/#about-tc)较少）。
它的**<mark>几个核心概念</mark>**：

* queueing discipline (qdisc)：排队规则，根据某种算法完成限速、整形等功能
* class：用户定义的流量类别
* classifier (也称为 filter)：分类器，分类规则
* action：要对包执行什么动作

组合以上概念，下面是对某个网络设备上的流量进行分类和限速时，所需完成的大致步骤：

1. 为网络设备**<mark>创建一个 qdisc</mark>**。

    * qdisc 是一个<mark>整流器/整形器</mark>（shaper），**可以包含多个 class**，不同 class 可以应用不同的策略。
    * qdisc 需要附着（attach）到某个网络接口（network interface），及流量方向（ingress or egress）。

2. **<mark>创建流量类别（class）</mark>**，并 attach 到 qdisc。

    * 例如，根据带宽分类，创建高、中、低三个类别。

3. **<mark>创建 filter（classifier）</mark>**，并 attach 到 qdisc。

    filters 用于**对网络设备上的流量进行分类**，并**将包分发（dispatch）到前面定义的不同 class**。

    filter 会对每个包进行过滤，返回下列值之一：

    * `0`：表示 mismatch。如果后面还有其他 filters，则**<mark>继续对这个包应用下一个 filter</mark>**。
    * `-1`：表示这个 filter 上配置的**<mark>默认 classid</mark>**。
    * 其他值：**<mark>表示一个 classid</mark>**。系统接下来应该将包送往这个指定的 class。可以看到，通过这种方式可以实现非线性分类（non-linear classification）。

4. 另外，**<mark>可以给 filter 添加 action</mark>**。例如，将选中的包丢弃（drop），或者将流量镜像到另一个网络设备等等。

1. 除此之外，qdisc 和 class 还可以循环嵌套，即：
   **class 里加入新 qdisc，然后新 qdisc 里又可以继续添加新 class**，
   最终形成的是一个以 root qdisc 为根的树。但对于本文接下来的内容，我们不需要了解这么多。

## 1.2 tc 示例：匹配 IP 和 Port 对流量进行分类

下面是一个例子，（参考了 [HTB shaper 文档](http://luxik.cdi.cz/~devik/qos/htb/manual/userg.htm)）：

```shell
# x:y 格式：
# * x 表示 qdisc, y 表示这个 qdisc 内的某个 class
# * 1: 是 1:0 的简写
#
# "default 11"：any traffic that is not otherwise classified will be assigned to class 1:11
$ tc qdisc add dev eth0 root handle 1: htb default 11

$ tc class add dev eth0 parent 1: classid 1:1 htb rate 100kbps ceil 100kbps
$ tc class add dev eth0 parent 1:1 classid 1:10 htb rate 30kbps ceil 100kbps
$ tc class add dev eth0 parent 1:1 classid 1:11 htb rate 10kbps ceil 100kbps

$ tc filter add dev eth0 protocol ip parent 1:0 prio 1 u32 \
    match ip src 1.2.3.4 match ip dport 80 0xffff flowid 1:10
$ tc filter add dev eth0 protocol ip parent 1:0 prio 1 u32 \
    match ip src 1.2.3.4 action drop
```

以上设置表示以下<mark>顺序逻辑</mark>：

1. 如果包匹配 `src_ip==1.2.3.4 && dst_port==80`，则将其送到第一个队列。这个队列对应的 class 目标速率是 `30kbps`；否则，
1. 如果包匹配 `src_ip==1.2.3.4`，则将其 drop；
1. 所有其他包将被送到第二个队列，对应的 class 目标速率是 `10kbps`。

# 2 tc ebpf 程序

有了以上基础，现在可以讨论 eBPF 了。

本质上，eBPF 是一种类汇编语言，能编写运行在内核的、安全的程序。
eBPF 程序能 attach 到内核中的若干 hook 点，其中大部分 hook 点
都是用于包处理（packet processing）和监控（monitoring）目的的。

这些 hook 中<mark>有两个与 TC 相关</mark>：从内核 4.1 开始，<mark>eBPF
程序能作为 tc classifier 或 tc action 附着（attach）到这两个 hook 点</mark>。

## 2.1 用作 classifier（分类器）

作为分类器使用时，eBPF 能使处理过程更灵活，甚至还能实现有状态处理，或者与用户
态交互（通过名为 map 的特殊数据结构）。

但这种场景下的 eBPF 程序<mark>本质上还是一个分类器</mark>，因此**返回值与普通分类器并无二致**：

* `0`：mismatch
* `-1`：match，表示当前 filter 的默认 classid
* 其他值：表示 classid

## 2.2 用作 action（动作）

用作 action 时，eBPF 程序的返回值
提示系统接下来对这个包执行什么动作（action），下面的内容来自 `tc-bpf(2)`：

1. `TC_ACT_UNSPEC (-1)`：使用 tc 的默认 action（与 classifier/filter 返回 `-1` 时类似）。
1. `TC_ACT_OK (0)`：结束处理过程，放行（allows the packet to proceed）。
1. `TC_ACT_RECLASSIFY (1)`：从头开始，重新执行分类过程。
1. `TC_ACT_SHOT (2)`：<mark>丢弃包</mark>。
1. `TC_ACT_PIPE (3)`：如果有下一个 action，执行之。
1. 其他值：定义在
   [include/uapi/linux/pkt_cls.h](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/pkt_cls.h)。
   [BPF and XDP Reference Guide from Cilium](http://docs.cilium.io/en/latest/bpf/#tc-traffic-control) 有进一步介绍。
1. 没有定义在以上头文件中的值，属于未定义返回值（unspecified return codes）。

# 3 direct-action

有了以上基础，现在可以讨论 direct-action 了。

## 3.1 传统 classifier+action 模式的限制

上面看到，

* classifer 能对包进行匹配，但**<mark>返回的 classid</mark>**；它
  **只能告诉系统接下来把这个包送到那个 class（队列）**，
  但无法让系统对这个包执行动作（drop、allow、mirror 等）。
* action 返回的是动作，告诉系统接下来要对这个包做什么（drop、allow、mirror 等），但它无
  法对包进行分类（规则匹配）。

所以，<mark>如果要实现”匹配+执行动作“的目的</mark> —— 例如，如果源 IP 是 `10.1.1.1`，则 drop 这
个包 —— 就<mark>需要两个步骤：一个 classifier 和一个 action</mark>，即 `classfifier+action` 模式。

## 3.2 为 tc ebpf classifier 引入 direct-action 模式

虽然 eBPF 有一些限制，例如单个程序的指令数是有上限的、只允许有限循环等等，但
它提供了一种数据包处理的**强大**语言。这带来的结果之一是：<mark>对于很多场景，eBPF
classifier 已经有足够的能力完成完成任务处理，无需再 attach 额外的
qdisc 或 class 了</mark>，对于 tc 层的数据包过滤（pass/drop/etc）场景尤其如此。

所以，为了

* **避免因套用 tc 原有流程而引入一个功能单薄的 action**
* **简化那些 classfier 独自就能完成所有工作的场景**
* 提升性能

针对 eBPF classifier，社区为 TC 引入了一个新的 flag：`direct-action`，简写 `da`。
这个 flag 用在 filter 的 attach time，告诉系统：
**<mark>filter（classifier）的返回值应当被解读为 action 类型的返回值</mark>**
（即前面提到的 `TC_ACT_XXX`；本来的话，应当被解读为 classid。）。

这意味着，一个作为 tc classifier 加载的 eBPF 程序，现在可以返回
`TC_ACT_SHOT`, `TC_ACT_OK` 等 tc action 的返回值了。换句话说，现在不需要另一个专门的
tc action 对象来 drop 或 mirror 相应的包了。

* 性能方面，这显然也是更优的，因为 <mark>TC 子系统无需再调用到额外的 action 模块
  ，而后者是在内核之外的（external to the kernel）</mark>。
* 从使用方来说，使用 `direct-action` flag 也是最简单的、最快的，是现在的推荐方式。

## 3.3 能为 tc ebpf action 引入 "direct-classifier" 模式吗？

那么，<mark>TC eBPF action 能完成类似功能吗？</mark>也就是说，能用 action 模块来完成处理包+返回
“pass” 或 “drop” 吗?答案是不行：
<mark>actions 并没有直接 attach 到某个 qdisc，它们只能用于包从某个 classifier 出来的地方</mark>，
这也就意味着：**无论如何都得有个 classifier/filter**。

另一个问题：这意味着 <mark>TC eBPF actions 毫无用处了吗？</mark>也不是。
eBPF action 仍然还可以用在其他 filters 后面。例如下面这个场景，

1. attach 一个 u32 filter 到一个 qdisc，根据包中的某些字段做（初步）过滤
1. 在这个 filter 后面再加一个 ebpf action（做进一步过滤）

    因为 ebpf action 中可以实现逻辑处理，因此可以在这里做额外判断，如果包满
    足某些额外的条件，就返回 drop。

以上就是 ebpf action 可以使用的场景之一。但坦白说，我见过的场景都是 eBPF 程序同
时负责 filtering 和返回 action，而不需要额外的 filters。

## 3.4 tc ebpf classifier 返回值被重新解读，是否因此丢失了 classid 信息？

<mark>正常 classifier 返回的是 classid</mark>，提示系统接下来应该把包送到哪个 class 做进一步处理。
而现在， tc ebpf classifier `direct-action` 模式返回的是 action 结果。

<mark>这是否意味着 eBPF classifier 丢失了 classid 信息？</mark>

答案是：NO，我们**仍然可以从其他地方获得这个 classid 信息**。传递给 filter 程序
的参数是 `struct __skb_buff`，其中有个 `tc_classid` 字段，存储的就是返回的
classid。后面介绍内核实现时会看到。

# 4 新的 qdisc 类型：`clsact`

`direct-action` 模式引入内核和 iproute2 之后几个月，
内核 Linux `4.5` 添加了一个新的 qdisc 类型： `clsact`。

* **`clsact` 与 `ingress` qdisc 类似**，能够以 `direct-action` 模式 attach eBPF 程序，
  其<mark>特点是不会执行任何排队</mark>（does not perform any queuing）。
* 但 `clsact` 是 `ingress` 的超集，因为它<mark>还支持在 egress 上以 direct-action
  模式 attach eBPF 程序</mark>，而在此之前我们是无法做到这一点的。

更多关于 `clsact` *qdisc* 信息见
[commit log](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=1f211a1b929c804100e138c5d3d656992cfd5622)
和 <a href="http://docs.cilium.io/en/latest/bpf/#tc-traffic-control">Cilium Guide</a>。

> `clsact` 是 iproute2/tc 中的 qdisc 名字。与此对应的内核模块名是 `sch_ingress` 和 `sch_clsact`：
>
> ```c
> // net/sched/sch_ingress.c
>
> static int __init ingress_module_init(void)
> {
> 	ret = register_qdisc(&ingress_qdisc_ops);
> 	if (!ret) {
> 		ret = register_qdisc(&clsact_qdisc_ops);
> 		if (ret)
> 			unregister_qdisc(&ingress_qdisc_ops);
> 	}
>
> 	return ret;
> }
>
> module_init(ingress_module_init);
>
> MODULE_ALIAS("sch_clsact");
> ```
>
> 译注。

# 5 完整示例（eBPF 程序 + tc 命令）

下面展示如何编写一个 tc ebpf filter (classifier)，以及如何编译、加载、附着到内核
。

## 5.1 eBPF 程序（tc ebpf classifier/filter）

下面这段程序根据包的大小和协议类型进行处理，可能会 drop、allow 或对包执行其他操
作。

```c
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/pkt_cls.h>
#include <linux/swab.h>

int classifier(struct __sk_buff *skb)
{
    void *data_end = (void *)(unsigned long long)skb->data_end;
    void *data = (void *)(unsigned long long)skb->data;
    struct ethhdr *eth = data;

    if (data + sizeof(struct ethhdr) > data_end)
        return TC_ACT_SHOT;

    if (eth->h_proto == ___constant_swab16(ETH_P_IP))
        /*
         * Packet processing is not implemented in this sample. Parse
         * IPv4 header, possibly push/pop encapsulation headers, update
         * header fields, drop or transmit based on network policy,
         * collect statistics and store them in a eBPF map...
         */
        return process_packet(skb);
    else
        return TC_ACT_OK;
}
```

## 5.2 编译

使用 clang/LLVM 将我们的 ebpf filter 程序编译为编译成目标文件：

```shell
$ clang -O2 -emit-llvm -c foo.c -o - | \
    llc -march=bpf -mcpu=probe -filetype=obj -o foo.o
```

## 5.3 加载到内核

首先需要创建一个 qdisc（因为 <mark>filter 必须 attach 到某个 qdisc</mark>）：

```shell
$ tc qdisc add dev eth0 clsact
```

然后<mark>将我们的 filter 程序 attach 到 qdisc</mark>：

```shell
$ tc filter add dev eth0 ingress bpf direct-action obj foo.o sec .text
```

查看：

```shell
$ tc filter show dev eth0
$ tc filter show dev eth0 ingress
filter protocol all pref 49152 bpf chain 0
filter protocol all pref 49152 bpf chain 0 handle 0x1 foo.o:[.text] direct-action not_in_hw id 11 tag ebe28a8e9a2e747f
```

可以看到 `foo.o` 中的 filter 已经 attach 到 ingress 路径，并且使用了 `direct-action` 模式。
现在这段对流量进行分类+执行动作（classification and action selection）程序已经开始工作了。

## 5.4 清理

```shell
$ tc qdisc del dev eth0 clsact
```

# 6 实现

## 6.1 内核实现

内核对 direct-action 模式的支持出现在 [045efa82ff56](https://github.com/torvalds/linux/commit/045efa82ff56)，
commit log 如下（排版略有调整）：

> cls_bpf: introduce integrated actions
>
> Often cls_bpf classifier is used with single action drop attached.
> Optimize this use case and let cls_bpf return both classid and action.
> For backwards compatibility reasons enable this feature under
> TCA_BPF_FLAG_ACT_DIRECT flag.
>
> Then more interesting programs like the following are easier to write:
>
> ```c
> int cls_bpf_prog(struct __sk_buff *skb)
> {
>   /* classify arp, ip, ipv6 into different traffic classes and drop all other packets */
>   switch (skb->protocol) {
>     case htons(ETH_P_ARP):  skb->tc_classid = 1; break;
>     case htons(ETH_P_IP):   skb->tc_classid = 2; break;
>     case htons(ETH_P_IPV6): skb->tc_classid = 3; break;
>     default: return TC_ACT_SHOT;
>   }
>
>   return TC_ACT_OK;
> }
> ```

尤其值得一提的是下面这段逻辑，

<p align="center"><img src="/assets/img/understanding-tc-da-mode/kernel-commit.png" width="65%" height="65%"></p>

做一点解释：

* `filter_res = BPF_PROG_RUN(prog->filter, skb);` 这个函数<mark>执行 eBPF 程序（classifier/filter），并将返回值存到 filter_res</mark>，
* <mark>在这个 patch 之前，eBPF 程序返回的是 classid</mark>，因此我们看到原有的
  逻辑是：
    * 如果 `filter_res !=0 && filter_res != -1`，那 `res->classid = filter_res;`
    * 然后执行 `ret = tcf_exts_exec(skb, &prog->exts, res);`，这会<mark>调用到相关的 action 模块，对包执行 action</mark>
* <mark>有了这个 patch，并且使用了 da 模式，filter_res 被解读为 action 值</mark>
  (`prog->exts_integrated` 为 `true` 时表示 `direct-action`)。此时，
    * `classid` 是从 `qdisc_skb_cb(skb)->tc_classid` 获取的，其中 `struct __sk_buff *skb` 是传递给 eBPF 程序的上下文
    * 将 filter_res 作为 action 值，执行 `ret = cls_bpf_exec_opcode(filter_res);`（**而非调用外部 action 模块**），然后退出循环

## 6.2 iproute2/tc 实现

相应的 iproute2 commit [faa8a463002f](https://git.kernel.org/pub/scm/network/iproute2/iproute2.git/commit/?id=faa8a463002f)，
添加了对 tc `da|direct-action` 的支持。

# 7 总结

本文介绍了 tc ebpf 中 `da` 模式的来龙去脉，并给出了详细的使用案例。
截至本文发表时，`da` 模式不仅是使用 tc ebpf 的推荐方式，而且
据我所知也是唯一方式。

# 参考资料

1. [On getting tc classifier fully programmable with cls_bpf](http://www.netdevconf.org/1.1/proceedings/slides/borkmann-tc-classifier-cls-bpf.pdf), Daniel Borkmann, netdev 1.1, Sevilla, February 2016
1. Linux kernel commit [045efa82ff56](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=045efa82ff563cd4e656ca1c2e354fa5bf6bbda4) cls_bpf: introduce integrated actions,
   Daniel Borkmann and Alexei Starovoitov, September 2015
1. Linux kernel commit [1f211a1b929c](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=1f211a1b929c804100e138c5d3d656992cfd5622) net, sched: add clsact qdisc,
   Daniel Borkmann, January 2016
1. iproute2 commit [faa8a463002f](https://git.kernel.org/pub/scm/network/iproute2/iproute2.git/commit/?id=faa8a463002fb9a365054dd333556e0aaa022759) f_bpf: allow for optional classid and add flags,
   Daniel Borkmann, September 2015
1. iproute2 commit [8f9afdd53156](https://git.kernel.org/pub/scm/network/iproute2/iproute2.git/commit/?id=8f9afdd531560c1534be44424669add2e19deeec) tc, clsact: add clsact frontend,
   Daniel Borkmann, January 2016
