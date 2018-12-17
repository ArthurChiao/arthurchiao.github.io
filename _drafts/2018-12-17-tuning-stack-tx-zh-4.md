---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 4"
date:   2018-12-17
author: ArthurChiao
categories: linux network stack monitoring tuning
---

## 6 IP协议层

UDP协议层调用`ip_send_skb`将skb交给IP协议层，所以就让我们从这里开始，探索一下IP
层的地图。

### 6.1 `ip_send_skb`

`ip_send_skb`函数位于
[net/ipv4/ip_output.c](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/ip_output.c#L1367-L1380)
中，非常简短。 它只是调用ip_local_out，如果调用返回了某些错误，就更新一下错误计
数。 让我们来看看：

```c
int ip_send_skb(struct net *net, struct sk_buff *skb)
{
        int err;

        err = ip_local_out(skb);
        if (err) {
                if (err > 0)
                        err = net_xmit_errno(err);
                if (err)
                        IP_INC_STATS(net, IPSTATS_MIB_OUTDISCARDS);
        }

        return err;
}
```

如上所示，调用ip_local_out并处理返回值。 `net_xmit_errno`可以将任何低层错误“转换
”为IP和UDP协议层所能理解的错误。如果发生任何错误，则IP协议统计信息`OutDiscards`
会递增。 稍后我们将看到要读取哪些文件以获取此统计信息。现在，让我们继续，看看
`ip_local_out`带我们去哪。

### 6.2 `ip_local_out` and `__ip_local_out`

幸运的是，`ip_local_out`和`__ip_local_out`都很简单。`ip_local_out`只需调用
`__ip_local_out`并根据返回值调用路由层输出数据包：

```c
int ip_local_out(struct sk_buff *skb)
{
        int err;

        err = __ip_local_out(skb);
        if (likely(err == 1))
                err = dst_output(skb);

        return err;
}
```

我们来看看`__ip_local_out`的代码：

```c
int __ip_local_out(struct sk_buff *skb)
{
        struct iphdr *iph = ip_hdr(skb);

        iph->tot_len = htons(skb->len);
        ip_send_check(iph);
        return nf_hook(NFPROTO_IPV4, NF_INET_LOCAL_OUT, skb, NULL,
                       skb_dst(skb)->dev, dst_output);
}
```

可以看到，该函数首先做了两件重要的事情：

1. 设置IP数据包的长度
1. 调用ip_send_check来计算要写入IP包头的校验和。 `ip_send_check`函数将调用名为
   `ip_fast_csum`的函数来计算校验和。 在x86和x86_64体系结构上，此函数用汇编实现
   。你可以在此处阅读[64位实现
   ](https://github.com/torvalds/linux/blob/v3.13/arch/x86/include/asm/checksum_64.h#L40-L73)
   和[32位实现
   ](https://github.com/torvalds/linux/blob/v3.13/arch/x86/include/asm/checksum_32.h#L63-L98)
   。

接下来，IP协议层将通过调用`nf_hook`调用netfilter。 `nf_hook`函数的返回值将传递回
`ip_local_out`。 如果`nf_hook`返回1，则表示允许数据包通过，并且调用者应该自己传
递数据包。 正如我们在上面看到的，这正是发生的情况：`ip_local_out`检查返回值1并通
过调用`dst_output`本身传递数据包。

### 6.3 netfilter and nf_hook

简洁起见，我决定跳过对netfilter，iptables和conntrack的深入研究。你可以从[这里
](https://github.com/torvalds/linux/blob/v3.13/include/linux/netfilter.h#L142-L147)
和[这里
](https://github.com/torvalds/linux/blob/v3.13/net/netfilter/core.c#L168-L209)开
始深入了解netfilter。

简短版本是：`nf_hook`只是一个wrapper，它调用`nf_hook_thresh`，首先检查是否有为这
个**协议族**和**hook类型**（这里分别为`NFPROTO_IPV4`和`NF_INET_LOCAL_OUT`）安装
的过滤器，然后将返回到IP协议层，避免深入到netfilter或更下面，比如iptables和
conntrack。

请记住：如果您有非常多或者非常复杂的netfilter或iptables规则，那些规则将和sendmsg
系统调用相同的用户进程的CPU上下文中执行。如果设置了CPU亲和性，将此进程绑定到到特
定CPU（或一组CPU）执行，请注意CPU将花费系统时间处理出站iptables规则。如果你在做
性能回归测试，那应该考虑根据系统的负载将进程绑到到特定的CPU，或者是减少
netfilter/iptables规则的复杂度。

出于讨论的目的，我们假设`nf_hook`返回1，表示调用者（在这种情况下，IP协议层）应该
自己传递数据包。

### 6.4 目的（路由）缓存

dst代码在Linux内核中实现**协议无关**的目标缓存。为了继续学习发送UDP数据报的流程
，我们需要了解dst条目是如何被设置的，首先查看dst条目和路由是如何生成的。 目标缓
存，路由和邻居子系统，任何一个都可以拿来单独详细的介绍。我们不深入细节，只是快速
地看一下它们是如何组合到一起的。

我们上面看到的代码调用了`dst_output(skb)`。 此函数只是查找关联到这个skb的dst条目
，然后调用输出函数。 让我们来看看：

```c
/* Output packet to network from transport.  */
static inline int dst_output(struct sk_buff *skb)
{
        return skb_dst(skb)->output(skb);
}
```

看起来很简单，但是输出函数之前是如何关联到dst条目的？

首先很重要的一点，目标缓存条目是以多种不同方式添加的。到目前为止，我们已经在代码
中看到的一种方法是从`udp_sendmsg`调用
[ip_route_output_flow](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/route.c#L2252-L2267)
。`ip_route_output_flow`函数调用
[__ip_route_output_key](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/route.c#L1990-L2173)
，后者进而调用
[__mkroute_output](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/route.c#L1868-L1988)
。 `__mkroute_output`函数创建路由和目标缓存条目。 当它执行创建操作时，它会判断哪
个输出函数适合此目标。 大多数时候，这个函数是`ip_output`。

### 6.5 `ip_output`

在UDP IPv4情况下上面的`output()`方法指向的是`ip_output`。 `ip_output`函数很简单：

```c
int ip_output(struct sk_buff *skb)
{
        struct net_device *dev = skb_dst(skb)->dev;

        IP_UPD_PO_STATS(dev_net(dev), IPSTATS_MIB_OUT, skb->len);

        skb->dev = dev;
        skb->protocol = htons(ETH_P_IP);

        return NF_HOOK_COND(NFPROTO_IPV4, NF_INET_POST_ROUTING, skb, NULL, dev,
                            ip_finish_output,
                            !(IPCB(skb)->flags & IPSKB_REROUTED));
}
```

首先，更新`IPSTATS_MIB_OUT`统计计数。`IP_UPD_PO_STATS`宏将更新字节数和包数。 我们将在后面
的部分中看到如何获取IP协议层统计信息以及它们各自的含义。接下来，设置要发送此skb
的设备，以及协议。

最后，通过调用`NF_HOOK_COND`将控制传递给netfilter。 查看`NF_HOOK_COND`的函数原型
有助于更清晰地解释它如何工作。 来自
[include/linux/netfilter.h](https://github.com/torvalds/linux/blob/v3.13/include/linux/netfilter.h#L177-L188)
：

```c
static inline int
NF_HOOK_COND(uint8_t pf, unsigned int hook, struct sk_buff *skb,
             struct net_device *in, struct net_device *out,
             int (*okfn)(struct sk_buff *), bool cond)
```

`NF_HOOK_COND`通过检查传入的条件来工作。在这里条件是`!(IPCB(skb)->flags &
IPSKB_REROUTED`。如果此条件为真，则skb将传递给netfilter。如果netfilter允许包通过
，`okfn`回调函数将被调用。在这里，`okfn`是`ip_finish_output`。

### 6.6 `ip_finish_output`

```c
static int ip_finish_output(struct sk_buff *skb)
{
#if defined(CONFIG_NETFILTER) && defined(CONFIG_XFRM)
        /* Policy lookup after SNAT yielded a new policy */
        if (skb_dst(skb)->xfrm != NULL) {
                IPCB(skb)->flags |= IPSKB_REROUTED;
                return dst_output(skb);
        }
#endif
        if (skb->len > ip_skb_dst_mtu(skb) && !skb_is_gso(skb))
                return ip_fragment(skb, ip_finish_output2);
        else
                return ip_finish_output2(skb);
}
```

如果内核启用了netfilter和数据包转换（XFRM），则更新skb的标志并通过dst_output将其
发回。两种更常见的情况是：

1. 如果数据包的长度大于MTU并且数据包的分段不会offload到设备，则会调用ip_fragment在传输之前对数据包进行分段
1. 否则，数据包将直接传递到ip_finish_output2

在继续我们的内核之前，让我们简单地谈谈Path MTU Discovery。

#### Path MTU Discovery

Linux提供了一个功能，我迄今为止一直避免：[路径MTU发现
](https://en.wikipedia.org/wiki/Path_MTU_Discovery)。此功能允许内核自动确定特定
路由的最大传输单元（
[MTU](https://en.wikipedia.org/wiki/Maximum_transmission_unit)
）。发送小于或等于该路由的MTU的packet意味着可以避免IP分片。这是推荐设置，因为分
段数据包会消耗系统资源，并且看起来很容易避免：只需发送足够小的数据包并且不需要分
段。

你可以在应用程序中通过调用`setsockopt`带`SOL_IP`和`IP_MTU_DISCOVER`选项，在
packet级别来调整路径MTU发现设置。 相应的合法值参考IP协议的[man
page](http://man7.org/linux/man-pages/man7/ip.7.html)。例如，你可能想设置的值是
：`IP_PMTUDISC_DO`，表示“始终执行路径MTU发现”。更高级的网络应用程序或诊断工具可
以选择自己实现[RFC 4821](https://www.ietf.org/rfc/rfc4821.txt)，以在应用程序启动
时针对特定路由的做PMTU。在这种情况下，你可以使用`IP_PMTUDISC_PROBE`选项告诉内核
设置“Do not Fragment”位，但允许你发送大于PMTU的数据。

应用程序可以通过调用getsockopt带SOL_IP和IP_MTU optname选项来查看当前PMTU。可以使
用它来指导应用程序在发送之前，构造UDP数据报的大小。

如果已启用PMTU发现，则发送大于PMTU的UDP数据将导致应用程序收到错误代码EMSGSIZE。
之后，应用程序只能减小packet大小重试。

强烈建议启用PTMU发现，因此我将不再详细描述IP分段的代码。当我们查看IP协议层统计信
息时，我将解释所有统计信息，包括与分片相关的统计信息。其中许多都在ip_fragment中
递增。在分片或不分片情况下，都会调用ip_finish_output2，所以让我们继续。

### 6.7 `ip_finish_output2`

IP分段后调用`ip_finish_output2`，另外`ip_finish_output`也会直接调用它。此功能在
将数据包传递到邻居缓存之前处理各种统计计数器。 让我们看看它是如何工作的：

```c
static inline int ip_finish_output2(struct sk_buff *skb)
{

                /* variable declarations */

        if (rt->rt_type == RTN_MULTICAST) {
                IP_UPD_PO_STATS(dev_net(dev), IPSTATS_MIB_OUTMCAST, skb->len);
        } else if (rt->rt_type == RTN_BROADCAST)
                IP_UPD_PO_STATS(dev_net(dev), IPSTATS_MIB_OUTBCAST, skb->len);

        /* Be paranoid, rather than too clever. */
        if (unlikely(skb_headroom(skb) < hh_len && dev->header_ops)) {
                struct sk_buff *skb2;

                skb2 = skb_realloc_headroom(skb, LL_RESERVED_SPACE(dev));
                if (skb2 == NULL) {
                        kfree_skb(skb);
                        return -ENOMEM;
                }
                if (skb->sk)
                        skb_set_owner_w(skb2, skb->sk);
                consume_skb(skb);
                skb = skb2;
        }
```

如果与此数据包关联的路由结构是多播类型，则使用`IP_UPD_PO_STATS`宏来增加
`OutMcastPkts`和`OutMcastOctets`计数器。否则，如果广播路由类型，则会增加
`OutBcastPkts`和`OutBcastOctets`计数器。

接下来，执行检查以确保skb结构有足够的空间容纳需要添加的任何链路层头。如果没有空
间，则通过调用`skb_realloc_headroom`分配额外的空间，并且新的skb的费用由相关的套
接字支付。

```c
        rcu_read_lock_bh();
        nexthop = (__force u32) rt_nexthop(rt, ip_hdr(skb)->daddr);
        neigh = __ipv4_neigh_lookup_noref(dev, nexthop);
        if (unlikely(!neigh))
                neigh = __neigh_create(&arp_tbl, &nexthop, dev, false);
```

继续，我们可以看到通过查询路由层找到下一跳，然后查找邻居缓存。 如果未找到，则通
过调用`__neigh_create`创建一个邻居。例如，可能是这种情况，第一次将数据发送到另一
台主机。请注意，使用arp_tbl（在
[net/ipv4/arp.c](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/arp.c#L160-L187)
中定义）调用此函数以在ARP表中创建邻居条目。其他系统（如IPv6或
[DECnet](https://en.wikipedia.org/wiki/DECnet)）维护自己的ARP表，并将不同的结构
传递给`__neigh_create`。 这篇文章的目的并不是要详细介绍邻居缓存，但注意如果必须
创建邻居，那么这个创建会导致缓存增长。 本文将在下面的部分中介绍有关邻居缓存的更
多详细信息。 无论如何，邻居缓存会导出自己的一组统计信息，以便可以衡量这种增长。
有关详细信息，请参阅下面的监视部分。

```c
        if (!IS_ERR(neigh)) {
                int res = dst_neigh_output(dst, neigh, skb);

                rcu_read_unlock_bh();
                return res;
        }
        rcu_read_unlock_bh();

        net_dbg_ratelimited("%s: No header cache and no neighbour!\n",
                            __func__);
        kfree_skb(skb);
        return -EINVAL;
}
```

最后，如果没有返回错误，则调用`dst_neigh_output`继续传递skb。 否则，释放skb并返
回EINVAL。 此处的错误将会回退并导致`OutDiscards`在`ip_send_skb`中以递增的方式递
增。 让我们继续在`dst_neigh_output`中继续接近Linux内核的netdevice子系统。

### 6.8 `dst_neigh_output`

dst_neigh_output函数为我们做了两件重要的事情。 首先，回想一下之前在这篇博文中我
们看到，如果用户通过辅助消息指定MSG_CONFIRM来发送函数，则会翻转一个标志以指示目
标高速缓存条目仍然有效且不应进行垃圾回收。 该检查在此处发生，并且邻居上的确认字
段设置为当前的jiffies计数。

```c
static inline int dst_neigh_output(struct dst_entry *dst, struct neighbour *n,
                                   struct sk_buff *skb)
{
        const struct hh_cache *hh;

        if (dst->pending_confirm) {
                unsigned long now = jiffies;

                dst->pending_confirm = 0;
                /* avoid dirtying neighbour */
                if (n->confirmed != now)
                        n->confirmed = now;
        }
```

其次，检查邻居的状态并调用适当的`output`函数。 让我们看一下这些条件，并尝试了解发生了什么：

```c
        hh = &n->hh;
        if ((n->nud_state & NUD_CONNECTED) && hh->hh_len)
                return neigh_hh_output(hh, skb);
        else
                return n->output(n, skb);
}
```

邻居被认为是`NUD_CONNECTED`，如果它满足是以下一个或多个条件：

1. `NUD_PERMANENT`：静态路由
1. `NUD_NOARP`：不需要ARP请求（例如，目标是多播或广播地址，或环回设备）
1. `NUD_REACHABLE`：邻居是“可达的。”只要[成功处理了](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/arp.c#L905-L923)ARP请求，目标就会被标记为可达

并且“硬件头”（hh）被缓存（因为我们之前已经发送过数据并且之前已经生成过它），将调
用neigh_hh_output。 否则，调用`output`函数。 以上两种情况，最后都会到
dev_queue_xmit，它将skb传递给Linux网络设备子系统，在它进入设备驱动程序层之前将对
其进行更多处理。 让我们跟随neigh_hh_output和n->输出代码路径，直到达到
dev_queue_xmit。

### 6.9 `neigh_hh_output`

如果目标是NUD_CONNECTED并且硬件头已被缓存，则将调用neigh_hh_output，在将skb移交
给dev_queue_xmit之前执行一小部分处理。 我们来看看
[include/net/neighbour.h](https://github.com/torvalds/linux/blob/v3.13/include/net/neighbour.h#L336-L356)
：

```c
static inline int neigh_hh_output(const struct hh_cache *hh, struct sk_buff *skb)
{
        unsigned int seq;
        int hh_len;

        do {
                seq = read_seqbegin(&hh->hh_lock);
                hh_len = hh->hh_len;
                if (likely(hh_len <= HH_DATA_MOD)) {
                        /* this is inlined by gcc */
                        memcpy(skb->data - HH_DATA_MOD, hh->hh_data, HH_DATA_MOD);
                 } else {
                         int hh_alen = HH_DATA_ALIGN(hh_len);

                         memcpy(skb->data - hh_alen, hh->hh_data, hh_alen);
                 }
         } while (read_seqretry(&hh->hh_lock, seq));

         skb_push(skb, hh_len);
         return dev_queue_xmit(skb);
}
```

这个函数理解有点难，部分原因是用于在缓存的硬件头上同步读/写锁原语，此代码使用称
为[seqlock](https://en.wikipedia.org/wiki/Seqlock)的东西。可以将上面的`do {}
while ()`循环想象成一个简单的重试机制，它将尝试在循环中执行操作，直到它可以成功
    执行。

循环以确定在复制之前是否需要对齐硬件头的长度。这是必需的，因为某些硬件标头（如
[IEEE
802.11](https://github.com/torvalds/linux/blob/v3.13/include/linux/ieee80211.h#L210-L218)
头）大于HH_DATA_MOD（16字节）。

将数据复制到skb后，skb_push将更新skb内指向数据缓冲区的指针。将skb传递给
dev_queue_xmit以进入Linux网络设备子系统。

### 6.10 `n->output`

如果目标不是NUD_CONNECTED或硬件标头尚未缓存，则代码沿n->输出路径向下进行。
neigbour结构上的`output`指针指向哪个函数？这得看情况。要了解这是如何设置的，我们
需要更多地了解邻居缓存的工作原理。

`struct
neighbour`包含几个重要字段。我们在上面看到的nud_state字段，output函数和ops结构。
回想一下，我们之前看到如果在缓存中找不到现有条目，会从`ip_finish_output2`调用
`__neigh_create`。当调用`__neigh_creaet`时，将分配邻居，其`output`函数[最初
](https://github.com/torvalds/linux/blob/v3.13/net/core/neighbour.c#L294)设置为
`neigh_blackhole`。随着`__neigh_create`代码的进行，它将根据邻居的状态调整输出值
以指向适当的输出函数。

例如，当代码确定要连接的邻居时，neigh_connect将用于将输出指针设置为
`neigh->ops->connected_output`。或者，当代码怀疑邻居可能已关闭时，neigh_suspect
将用于将输出指针设置为`neigh->ops->output`（例如，如果已超过
`/proc/sys/net/ipv4/neigh/default/delay_first_probe_time`自发送探测以来的
`delay_first_probe_time`秒。

换句话说：`neigh->output`设置为另一个指针，`neigh->ops_connected_output`或
`neigh->ops->output`，具体取决于它的状态。 `neigh->ops`来自哪里？

分配邻居后，调用arp_constructor（来自
[net/ipv4/arp.c](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/arp.c#L220-L313)
）来设置struct neighbor的某些字段。特别是，此函数检查与此邻居关联的设备，如果设
备公开包含cache[函数]()的header_ops结构，则（[以太网设备会
](https://github.com/torvalds/linux/blob/v3.13/net/ethernet/eth.c#L342-L348)）
`neigh->ops`设置为
[net/ipv4/arp](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/arp.c#L138-L144)
中定义的以下结构。

```c
static const struct neigh_ops arp_hh_ops = {
        .family =               AF_INET,
        .solicit =              arp_solicit,
        .error_report =         arp_error_report,
        .output =               neigh_resolve_output,
        .connected_output =     neigh_resolve_output,
};
```

所以，不管neighbor是不是被认为“connected”，或者邻居缓存代码是否查看，
neigh_resolve_output 最终都会被赋值给neigh->output。当执行到`n->output`时就会调
用它。

#### neigh_resolve_output

此函数的目的是解析未连接的邻居，或已连接但没有缓存硬件头的邻居。 我们来看看这个
函数是如何工作的：

```c
/* Slow and careful. */

int neigh_resolve_output(struct neighbour *neigh, struct sk_buff *skb)
{
        struct dst_entry *dst = skb_dst(skb);
        int rc = 0;

        if (!dst)
                goto discard;

        if (!neigh_event_send(neigh, skb)) {
                int err;
                struct net_device *dev = neigh->dev;
                unsigned int seq;
```

代码首先进行一些基本检查，然后继续调用neigh_event_send。 neigh_event_send函数是
__neigh_event_send的简单封装，它将解决heavy lifting问题。可以在
[net/core/neighbour.c](https://github.com/torvalds/linux/blob/v3.13/net/core/neighbour.c#L964-L1028)
中读取__neigh_event_send的源代码，从大的层面看，代码中的三种内容用户最感兴趣：

1. `NUD_NONE`状态（默认状态）的邻居：假设
   `/proc/sys/net/ipv4/neigh/default/app_solicit`和
   `/proc/sys/net/ipv4/neigh/default/mcast_solicit`配置允许发送探测（如果不是，
   则将状态标记为NUD_FAILED），将导致立即发送ARP请求。邻居状态将更新为
   NUD_INCOMPLETE
1. `NUD_STALE`状态的邻居：将更新为`NUD_DELAYED`并且将设置计时器以稍后探测它们（
   稍后是现在的时间+`/proc/sys/net/ipv4/neigh/default/delay_first_probe_time`秒
   ）
1. 检查`NUD_INCOMPLETE`状态的邻居（包括上面案例1中的内容），以确保未解析邻居的排
   队packet的数量小于等于`/proc/sys/net/ipv4/neigh/default/unres_qlen`。如果超过
   ，则数据包会出列并丢弃，直到小于等于proc中的值。 统计信息中的有个计数器会因此
   更新

如果需要立即ARP探测，它将被发送。 `__neigh_event_send`将返回0，表示邻居被视为“已
连接”或“已延迟”，否则返回1。返回值0允许`neigh_resolve_output`继续：

```c
                if (dev->header_ops->cache && !neigh->hh.hh_len)
                        neigh_hh_init(neigh, dst);
```

如果邻居关联的设备的协议实现（在我们的例子中是以太网）支持缓存硬件头，并且当前没
有缓存，`neigh_hh_init`将缓存它。

```c
                do {
                        __skb_pull(skb, skb_network_offset(skb));
                        seq = read_seqbegin(&neigh->ha_lock);
                        err = dev_hard_header(skb, dev, ntohs(skb->protocol),
                                              neigh->ha, NULL, skb->len);
                } while (read_seqretry(&neigh->ha_lock, seq));
```

接下来，seqlock锁控制对邻居的硬件地址字段（`neigh->ha`）的访问。
dev_hard_header将读取该字段，用于为skb创建以太网头时。

之后是错误检查：

```c
                if (err >= 0)
                        rc = dev_queue_xmit(skb);
                else
                        goto out_kfree_skb;
        }
```

如果以太网头写入成功，skb将传递给dev_queue_xmit以通过Linux网络设备子系统进行发送。
如果出现错误，goto将删除skb，设置返回码并返回错误：

```c
out:
        return rc;
discard:
        neigh_dbg(1, "%s: dst=%p neigh=%p\n", __func__, dst, neigh);
out_kfree_skb:
        rc = -EINVAL;
        kfree_skb(skb);
        goto out;
}
EXPORT_SYMBOL(neigh_resolve_output);
```

在我们进入Linux网络设备子系统之前，让我们看看一些用于监控和转换IP协议层的文件。

### 6.11 监控: IP层

#### /proc/net/snmp

```shell
$ cat /proc/net/snmp
Ip: Forwarding DefaultTTL InReceives InHdrErrors InAddrErrors ForwDatagrams InUnknownProtos InDiscards InDelivers OutRequests OutDiscards OutNoRoutes ReasmTimeout ReasmReqds ReasmOKs ReasmFails FragOKs FragFails FragCreates
Ip: 1 64 25922988125 0 0 15771700 0 0 25898327616 22789396404 12987882 51 1 10129840 2196520 1 0 0 0
...
```

这个文件包扩多种协议的统计，IP层的在最前面，每一列代表什么有说明。

前面我们已经看到IP协议层有一些地方会更新计数器。这些计数器的类型是C枚举类型，定
义在[include/uapi/linux/snmp.h](https://github.com/torvalds/linux/blob/v3.13/include/uapi/linux/snmp.h#L10-L59):

```c
enum
{
  IPSTATS_MIB_NUM = 0,
/* frequently written fields in fast path, kept in same cache line */
  IPSTATS_MIB_INPKTS,     /* InReceives */
  IPSTATS_MIB_INOCTETS,     /* InOctets */
  IPSTATS_MIB_INDELIVERS,     /* InDelivers */
  IPSTATS_MIB_OUTFORWDATAGRAMS,   /* OutForwDatagrams */
  IPSTATS_MIB_OUTPKTS,      /* OutRequests */
  IPSTATS_MIB_OUTOCTETS,      /* OutOctets */

  /* ... */
```

一些有趣的统计：

* `OutRequests`: Incremented each time an IP packet is attempted to be sent. It appears that this is incremented for every send, successful or not.
* `OutDiscards`: Incremented each time an IP packet is discarded. This can happen if appending data to the skb (for corked sockets) fails, or if the layers below IP return an error.
* `OutNoRoute`: Incremented in several places, for example in the UDP protocol layer (udp_sendmsg) if no route can be generated for a given destination. Also incremented when an application calls “connect” on a UDP socket but no route can be found.
* `FragOKs`: Incremented once per packet that is fragmented. For example, a packet split into 3 fragments will cause this counter to be incremented once.
* `FragCreates`: Incremented once per fragment that is created. For example, a packet split into 3 fragments will cause this counter to be incremented thrice.
* `FragFails`: Incremented if fragmentation was attempted, but is not permitted (because the “Don’t Fragment” bit is set). Also incremented if outputting the fragment fails.

其他（接收数据部分）的统计可以见本文的姊妹篇：[原文](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#monitoring-ip-protocol-layer-statistics)，[中文翻译版]()。

#### /proc/net/netstat

```shell
$ cat /proc/net/netstat | grep IpExt
IpExt: InNoRoutes InTruncatedPkts InMcastPkts OutMcastPkts InBcastPkts OutBcastPkts InOctets OutOctets InMcastOctets OutMcastOctets InBcastOctets OutBcastOctets InCsumErrors InNoECTPkts InECT0Pktsu InCEPkts
IpExt: 0 0 0 0 277959 0 14568040307695 32991309088496 0 0 58649349 0 0 0 0 0
```

格式与类似，除了每列的名称都有`IpExt`前缀之外。

一些有趣的统计：

* `OutMcastPkts`: Incremented each time a packet destined for a multicast address is sent.
* `OutBcastPkts`: Incremented each time a packet destined for a broadcast address is sent.
* `OutOctects`: The number of packet bytes output.
* `OutMcastOctets`: The number of multicast packet bytes output.
* `OutBcastOctets`: The number of broadcast packet bytes output.

其他（接收数据部分）的统计可以见本文的姊妹篇：[原文](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#monitoring-ip-protocol-layer-statistics)，[中文翻译版]()。

注意这些计数分别在IP层的不同地方被更新。由于代码一直在更新，重复计数或者计数错误
的bug可能会引入。如果这些计数对你非常重要，强烈建议你阅读内核的相应源码，确定它
们是在哪里被更新的，以及更新的对不对，是不是有bug。
