---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 4"
date:   2018-12-17
author: ArthurChiao
categories: network-stack monitoring tuning
---

## 6 IP协议层

UDP协议层通过调用`ip_send_skb`将skb交给IP协议层，所以我们从这里开始，探索一下IP
协议层。

### 6.1 `ip_send_skb`

`ip_send_skb`函数定义在
[net/ipv4/ip_output.c](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/ip_output.c#L1367-L1380)
中，非常简短。它只是调用`ip_local_out`，如果调用失败，就更新相应的错误计数。让
我们来看看：

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

`net_xmit_errno`函数将低层错误转换为IP和UDP协议层所能理解的错误。如果发生错误，
IP协议计数器`OutDiscards`会递增。稍后我们将看到读取哪些文件可以获取此统计信
息。现在，让我们继续，看看`ip_local_out`带我们去哪。

### 6.2 `ip_local_out` and `__ip_local_out`

幸运的是，`ip_local_out`和`__ip_local_out`都很简单。`ip_local_out`只需调用
`__ip_local_out`，如果返回值为1，则调用路由层`dst_output`发送数据包：

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
1. 调用`ip_send_check`来计算要写入IP头的校验和。`ip_send_check`函数将进一步调用
   名为`ip_fast_csum`的函数来计算校验和。在x86和x86_64体系结构上，此函数用汇编实
   现，代码：[64位实现](https://github.com/torvalds/linux/blob/v3.13/arch/x86/include/asm/checksum_64.h#L40-L73)
   和[32位实现](https://github.com/torvalds/linux/blob/v3.13/arch/x86/include/asm/checksum_32.h#L63-L98)
   。

接下来，IP协议层将通过调用`nf_hook`进入netfilter，其返回值将传递回`ip_local_out`
。 如果`nf_hook`返回1，则表示允许数据包通过，并且调用者应该自己发送数据包。这正
是我们在上面看到的情况：`ip_local_out`检查返回值1时，自己通过调用`dst_output`发
送数据包。

### 6.3 netfilter and nf_hook

简洁起见，我决定跳过对netfilter，iptables和conntrack的深入研究。如果你想深入了解
netfilter的代码实现，可以从 `include/linux/netfilter.h`[这里
](https://github.com/torvalds/linux/blob/v3.13/include/linux/netfilter.h#L142-L147)
和 `net/netfilter/core.c`[这里
](https://github.com/torvalds/linux/blob/v3.13/net/netfilter/core.c#L168-L209)开
始。

简短版本是：`nf_hook`只是一个wrapper，它调用`nf_hook_thresh`，首先检查是否有为这
个**协议族**和**hook类型**（这里分别为`NFPROTO_IPV4`和`NF_INET_LOCAL_OUT`）安装
的过滤器，然后将返回到IP协议层，避免深入到netfilter或更下面，比如iptables和
conntrack。

请记住：如果你有非常多或者非常复杂的netfilter或iptables规则，那些规则将在触发
`sendmsg`系统调的用户进程的上下文中执行。如果对这个用户进程设置了CPU亲和性，相应
的CPU将花费系统时间（system time）处理出站（outbound）iptables规则。如果你在做性
能回归测试，那可能要考虑根据系统的负载，将相应的用户进程绑到到特定的CPU，或者是
减少netfilter/iptables规则的复杂度，以减少对性能测试的影响。

出于讨论目的，我们假设`nf_hook`返回1，表示调用者（在这种情况下是IP协议层）应该
自己发送数据包。

### 6.4 目的（路由）缓存

dst代码在Linux内核中实现**协议无关**的目标缓存。为了继续学习发送UDP数据报的流程
，我们需要了解dst条目是如何被设置的，首先来看dst条目和路由是如何生成的。 目标缓
存，路由和邻居子系统，任何一个都可以拿来单独详细的介绍。我们不深入细节，只是快速
地看一下它们是如何组合到一起的。

我们上面看到的代码调用了`dst_output(skb)`。 此函数只是查找关联到这个skb的dst条目
，然后调用`output`方法。代码如下：

```c
/* Output packet to network from transport.  */
static inline int dst_output(struct sk_buff *skb)
{
        return skb_dst(skb)->output(skb);
}
```

看起来很简单，但是`output`方法之前是如何关联到dst条目的？

首先很重要的一点，目标缓存条目是以多种不同方式添加的。到目前为止，我们已经在代码
中看到的一种方法是从`udp_sendmsg`调用
[ip_route_output_flow](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/route.c#L2252-L2267)
。`ip_route_output_flow`函数调用
[__ip_route_output_key](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/route.c#L1990-L2173)
，后者进而调用
[__mkroute_output](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/route.c#L1868-L1988)
。 `__mkroute_output`函数创建路由和目标缓存条目。当它执行创建操作时，它会判断哪
个`output`方法适合此dst。大多数时候，这个函数是`ip_output`。

### 6.5 `ip_output`

在UDP IPv4情况下，上面的`output`方法指向的是`ip_output`。`ip_output`函数很简单：

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

首先，更新`IPSTATS_MIB_OUT`统计计数。`IP_UPD_PO_STATS`宏将更新字节数和包数统计。
我们将在后面的部分中看到如何获取IP协议层统计信息以及它们各自的含义。接下来，设置
要发送此skb的设备，以及协议。

最后，通过调用`NF_HOOK_COND`将控制权交给netfilter。查看`NF_HOOK_COND`的函数原型
有助于更清晰地解释它如何工作。来自
[include/linux/netfilter.h](https://github.com/torvalds/linux/blob/v3.13/include/linux/netfilter.h#L177-L188)
：

```c
static inline int
NF_HOOK_COND(uint8_t pf, unsigned int hook, struct sk_buff *skb,
             struct net_device *in, struct net_device *out,
             int (*okfn)(struct sk_buff *), bool cond)
```

`NF_HOOK_COND`通过检查传入的条件来工作。在这里条件是`!(IPCB(skb)->flags &
IPSKB_REROUTED`。如果此条件为真，则skb将发送给netfilter。如果netfilter允许包通过
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

如果内核启用了netfilter和数据包转换（XFRM），则更新skb的标志并通过`dst_output`将
其发回。

更常见的两种情况是：

1. 如果数据包的长度大于MTU并且分片不会offload到设备，则会调用`ip_fragment`在发送之前对数据包进行分片
1. 否则，数据包将直接发送到ip_finish_output2

在继续我们的内核之前，让我们简单地谈谈Path MTU Discovery。

#### Path MTU Discovery

Linux提供了一个功能，我迄今为止一直避免提及：[路径MTU发现
](https://en.wikipedia.org/wiki/Path_MTU_Discovery)。此功能允许内核自动确定
路由的最大传输单元（
[MTU](https://en.wikipedia.org/wiki/Maximum_transmission_unit)
）。发送小于或等于该路由的MTU的包意味着可以避免IP分片，这是推荐设置，因为数
据包分片会消耗系统资源，而避免分片看起来很容易：只需发送足够小的不需要分片的数据
包。

你可以在应用程序中通过调用`setsockopt`带`SOL_IP`和`IP_MTU_DISCOVER`选项，在
packet级别来调整路径MTU发现设置，相应的合法值参考IP协议的[man
page](http://man7.org/linux/man-pages/man7/ip.7.html)。例如，你可能想设置的值是
：`IP_PMTUDISC_DO`，表示“始终执行路径MTU发现”。更高级的网络应用程序或诊断工具可
能选择自己实现[RFC 4821](https://www.ietf.org/rfc/rfc4821.txt)，以在应用程序启动
时针对特定的路由做PMTU。在这种情况下，你可以使用`IP_PMTUDISC_PROBE`选项告诉内核
设置“Do not Fragment”位，这就会允许你发送大于PMTU的数据。

应用程序可以通过调用`getsockopt`带`SOL_IP`和`IP_MTU`选项来查看当前PMTU。可以使
用它指导应用程序在发送之前，构造UDP数据报的大小。

如果已启用PMTU发现，则发送大于PMTU的UDP数据将导致应用程序收到`EMSGSIZE`错误。
这种情况下，应用程序只能减小packet大小重试。

强烈建议启用PTMU发现，因此我将不再详细描述IP分片的代码。当我们查看IP协议层统计信
息时，我将解释所有统计信息，包括与分片相关的统计信息。其中许多计数都在
`ip_fragment`中更新的。不管分片与否，代码最后都会调到`ip_finish_output2`，所以让
我们继续。

### 6.7 `ip_finish_output2`

IP分片后调用`ip_finish_output2`，另外`ip_finish_output`也会直接调用它。这个函数
在将包发送到邻居缓存之前处理各种统计计数器。让我们看看它是如何工作的：

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

如果与此数据包关联的路由是多播类型，则使用`IP_UPD_PO_STATS`宏来增加
`OutMcastPkts`和`OutMcastOctets`计数。如果广播路由，则会增加`OutBcastPkts`和
`OutBcastOctets`计数。

接下来，确保skb结构有足够的空间容纳需要添加的任何链路层头。如果空间不够，则调用
`skb_realloc_headroom`分配额外的空间，并且新的skb的费用（charge）记在相关的
socket上。

```c
        rcu_read_lock_bh();
        nexthop = (__force u32) rt_nexthop(rt, ip_hdr(skb)->daddr);
        neigh = __ipv4_neigh_lookup_noref(dev, nexthop);
        if (unlikely(!neigh))
                neigh = __neigh_create(&arp_tbl, &nexthop, dev, false);
```

继续，查询路由层找到下一跳，再根据下一跳信息查找邻居缓存。如果未找到，则
调用`__neigh_create`创建一个邻居。例如，第一次将数据发送到另一
台主机的时候，就是这种情况。请注意，创建邻居缓存的时候带了`arp_tbl`（
[net/ipv4/arp.c](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/arp.c#L160-L187)
中定义）参数。其他系统（如IPv6或
[DECnet](https://en.wikipedia.org/wiki/DECnet)）维护自己的ARP表，并将不同的变量
传给`__neigh_create`。 这篇文章的目的并不是要详细介绍邻居缓存，但注意如果创建，
会导致缓存表增大。本文后面会介绍有关邻居缓存的更多详细信息。 邻居缓存会导出一组
统计信息，以便可以衡量这种增长。有关详细信息，请参阅下面的监控部分。

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

最后，如果创建邻居缓存成功，则调用`dst_neigh_output`继续传递skb；否则，释放skb并返
回`EINVAL`，这会向上传递，导致`OutDiscards`在`ip_send_skb`中递增。让我们继续在
`dst_neigh_output`中接近Linux内核的netdevice子系统。

### 6.8 `dst_neigh_output`

`dst_neigh_output`函数做了两件重要的事情。首先，回想一下之前在本文中我
们看到，如果用户调用`sendmsg`并通过辅助消息指定`MSG_CONFIRM`参数，则会设置一个标
志位以指示目标高速缓存条目仍然有效且不应进行垃圾回收。这个检查就是在这个函数里面
做的，并且邻居上的`confirm`字段设置为当前的jiffies计数。

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

其次，检查邻居的状态并调用适当的`output`函数。让我们看一下这些条件，并尝试了解发
生了什么：

```c
        hh = &n->hh;
        if ((n->nud_state & NUD_CONNECTED) && hh->hh_len)
                return neigh_hh_output(hh, skb);
        else
                return n->output(n, skb);
}
```

邻居被认为是`NUD_CONNECTED`，如果它满足以下一个或多个条件：

1. `NUD_PERMANENT`：静态路由
1. `NUD_NOARP`：不需要ARP请求（例如，目标是多播或广播地址，或环回设备）
1. `NUD_REACHABLE`：邻居是“可达的。”只要[成功处理了](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/arp.c#L905-L923)ARP请求，目标就会被标记为可达

进一步，如果“硬件头”（hh）被缓存（之前已经发送过数据，并生成了缓存），将调
用`neigh_hh_output`。

否则，调用`output`函数。

以上两种情况，最后都会到`dev_queue_xmit`，它将skb发送给Linux网络设备子系统，在它
进入设备驱动程序层之前将对其进行更多处理。让我们沿着`neigh_hh_output`和
`n->output`代码继续向下，直到达到`dev_queue_xmit`。

### 6.9 `neigh_hh_output`

如果目标是`NUD_CONNECTED`并且硬件头已被缓存，则将调用`neigh_hh_output`，在将skb移交
给`dev_queue_xmit`之前执行一小部分处理。 我们来看看
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

这个函数理解有点难，部分原因是[seqlock](https://en.wikipedia.org/wiki/Seqlock)这
个东西，它用于在缓存的硬件头上做读/写锁。可以将上面的`do {} while ()`循环想象成
一个简单的重试机制，它将尝试在循环中执行，直到成功。

循环里处理硬件头的长度对齐。这是必需的，因为某些硬件头（如[IEEE
802.11](https://github.com/torvalds/linux/blob/v3.13/include/linux/ieee80211.h#L210-L218)
头）大于`HH_DATA_MOD`（16字节）。

将头数据复制到skb后，`skb_push`将更新skb内指向数据缓冲区的指针。最后调用
`dev_queue_xmit`将skb传递给Linux网络设备子系统。

### 6.10 `n->output`

如果目标不是`NUD_CONNECTED`或硬件头尚未缓存，则代码沿`n->output`路径向下。
neigbour结构上的`output`指针指向哪个函数？这得看情况。要了解这是如何设置的，我们
需要更多地了解邻居缓存的工作原理。

`struct
neighbour`包含几个重要字段：我们在上面看到的`nud_state`字段，`output`函数和`ops`
结构。回想一下，我们之前看到如果在缓存中找不到现有条目，会从`ip_finish_output2`
调用`__neigh_create`创建一个。当调用`__neigh_creaet`时，将分配邻居，其`output`函
数[最初](https://github.com/torvalds/linux/blob/v3.13/net/core/neighbour.c#L294)
设置为`neigh_blackhole`。随着`__neigh_create`代码的进行，它将根据邻居的状态修改
`output`值以指向适当的发送方法。

例如，当代码确定是“已连接的”邻居时，`neigh_connect`会将`output`设置为
`neigh->ops->connected_output`。或者，当代码怀疑邻居可能已关闭时，`neigh_suspect`
会将`output`设置为`neigh->ops->output`（例如，如果已超过
`/proc/sys/net/ipv4/neigh/default/delay_first_probe_time`自发送探测以来的
`delay_first_probe_time`秒）。

换句话说：`neigh->output`会被设置为`neigh->ops_connected_output`或
`neigh->ops->output`，具体取决于邻居的状态。`neigh->ops`来自哪里？

分配邻居后，调用`arp_constructor`（
[net/ipv4/arp.c](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/arp.c#L220-L313)
）来设置`struct neighbor`的某些字段。特别是，此函数会检查与此邻居关联的设备是否
导出来一个`struct header_ops`实例（[以太网设备是这样做的
](https://github.com/torvalds/linux/blob/v3.13/net/ethernet/eth.c#L342-L348)），
该结构体有一个`cache`方法。

`neigh->ops`设置为
[net/ipv4/arp](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/arp.c#L138-L144)
中定义的以下实例：

```c
static const struct neigh_ops arp_hh_ops = {
        .family =               AF_INET,
        .solicit =              arp_solicit,
        .error_report =         arp_error_report,
        .output =               neigh_resolve_output,
        .connected_output =     neigh_resolve_output,
};
```

所以，不管neighbor是不是“已连接的”，或者邻居缓存代码是否怀疑连接“已关闭”，
`neigh_resolve_output`最终都会被赋给`neigh->output`。当执行到`n->output`时就会调
用它。

#### neigh_resolve_output

此函数的目的是解析未连接的邻居，或已连接但没有缓存硬件头的邻居。我们来看看这个
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

代码首先进行一些基本检查，然后调用`neigh_event_send`。 `neigh_event_send`函数是
`__neigh_event_send`的简单封装，后者干大部分脏话累活。可以在
[net/core/neighbour.c](https://github.com/torvalds/linux/blob/v3.13/net/core/neighbour.c#L964-L1028)
中读`__neigh_event_send`的源代码，从大的层面看，三种情况：

1. `NUD_NONE`状态（默认状态）的邻居：假设
   `/proc/sys/net/ipv4/neigh/default/app_solicit`和
   `/proc/sys/net/ipv4/neigh/default/mcast_solicit`配置允许发送探测（如果不是，
   则将状态标记为`NUD_FAILED`），将导致立即发送ARP请求。邻居状态将更新为
   `NUD_INCOMPLETE`
1. `NUD_STALE`状态的邻居：将更新为`NUD_DELAYED`并且将设置计时器以稍后探测它们（
   稍后是现在的时间+`/proc/sys/net/ipv4/neigh/default/delay_first_probe_time`秒
   ）
1. 检查`NUD_INCOMPLETE`状态的邻居（包括上面第一种情形），以确保未解析邻居的排
   队packet的数量小于等于`/proc/sys/net/ipv4/neigh/default/unres_qlen`。如果超过
   ，则数据包会出列并丢弃，直到小于等于proc中的值。 统计信息中有个计数器会因此
   更新

如果需要ARP探测，ARP将立即被发送。`__neigh_event_send`将返回0，表示邻居被视为“已
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
`dev_hard_header`为skb创建以太网头时将读取该字段。

之后是错误检查：

```c
                if (err >= 0)
                        rc = dev_queue_xmit(skb);
                else
                        goto out_kfree_skb;
        }
```

如果以太网头写入成功，将调用`dev_queue_xmit`将skb传递给Linux网络设备子系统进行发
送。如果出现错误，goto将删除skb，设置并返回错误码：

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

其他（接收数据部分）的统计可以见本文的姊妹篇：[原文
](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#monitoring-ip-protocol-layer-statistics)
，[中文翻译版]()。

#### /proc/net/netstat

```shell
$ cat /proc/net/netstat | grep IpExt
IpExt: InNoRoutes InTruncatedPkts InMcastPkts OutMcastPkts InBcastPkts OutBcastPkts InOctets OutOctets InMcastOctets OutMcastOctets InBcastOctets OutBcastOctets InCsumErrors InNoECTPkts InECT0Pktsu InCEPkts
IpExt: 0 0 0 0 277959 0 14568040307695 32991309088496 0 0 58649349 0 0 0 0 0
```

格式与前面的类似，除了每列的名称都有`IpExt`前缀之外。

一些有趣的统计：

* `OutMcastPkts`: Incremented each time a packet destined for a multicast address is sent.
* `OutBcastPkts`: Incremented each time a packet destined for a broadcast address is sent.
* `OutOctects`: The number of packet bytes output.
* `OutMcastOctets`: The number of multicast packet bytes output.
* `OutBcastOctets`: The number of broadcast packet bytes output.

其他（接收数据部分）的统计可以见本文的姊妹篇：[原文
](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#monitoring-ip-protocol-layer-statistics)
，[中文翻译版]()。

注意这些计数分别在IP层的不同地方被更新。由于代码一直在更新，重复计数或者计数错误
的bug可能会引入。如果这些计数对你非常重要，强烈建议你阅读内核的相应源码，确定它
们是在哪里被更新的，以及更新的对不对，是不是有bug。
