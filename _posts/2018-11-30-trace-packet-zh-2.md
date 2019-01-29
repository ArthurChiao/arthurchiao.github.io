---
layout: post
title:  "[译] 网络包内核路径跟踪：2 Perf"
date:   2018-11-30
author: ArthurChiao
categories: eBPF perf tracepoint
---

译者按：本文翻译自一篇英文博客[Tracing a packet's journey using Linux tracepoints, perf and eBPF](https://blog.yadutaf.fr/2017/07/28/tracing-a-packet-journey-using-linux-tracepoints-perf-ebpf/)。由于原文篇幅较长，我将其分成了三篇，并添加了适当的标题。
本文不会100%逐词逐句翻译，那样的翻译太过生硬，看看《TCP/IP详解》中文版就知道了。
例如，有多少人会在讨论网络问题的时候说**"插口"**而不是**"socket"**？在技术领域，过
度翻译反而会带来交流障碍。**如果能看懂英文，我建议你阅读原文，或者和本文对照看。**

----

这篇文章只会非常简单地使用perf做内核跟踪。

## 1 安装perf

我的环境基于Ubuntu 17.04 （Zesty）：

```shell
$ sudo apt install linux-tools-generic
$ perf # test perf
```

## 2 测试环境

我们将使用4个IP，其中2个为外部可路由网段(`192.168`)：

1. localhost，IP 127.0.0.1
1. 一个干净的容器，IP 172.17.0.2
1. 我的手机，通过USB连接，IP 192.168.42.129
1. 我的手机，通过WiFi连接，IP 192.168.43.1

## 3 初体验：跟踪ping包

`perf trace`是 `perf`子命令，能够跟踪packet路径，默认输出类似于 `strace`（头信息少很多）。

跟踪ping向`172.17.0.2`容器的包，这里我们只关心`net`事件，忽略系统调用信息：

```shell
$ sudo perf trace --no-syscalls --event 'net:*' ping 172.17.0.2 -c1 > /dev/null
     0.000 net:net_dev_queue:dev=docker0 skbaddr=0xffff96d481988700 len=98)
     0.008 net:net_dev_start_xmit:dev=docker0 queue_mapping=0 skbaddr=0xffff96d481988700 vlan_tagged=0 vlan_proto=0x0000 vlan_tci=0x0000 protocol=0x0800 ip_summed=0 len=98 data_len=0 network_offset=14 transport_offset_valid=1 transport_offset=34 tx_flags=0 gso_size=0 gso_segs=0 gso_type=0)
     0.014 net:net_dev_queue:dev=veth79215ff skbaddr=0xffff96d481988700 len=98)
     0.016 net:net_dev_start_xmit:dev=veth79215ff queue_mapping=0 skbaddr=0xffff96d481988700 vlan_tagged=0 vlan_proto=0x0000 vlan_tci=0x0000 protocol=0x0800 ip_summed=0 len=98 data_len=0 network_offset=14 transport_offset_valid=1 transport_offset=34 tx_flags=0 gso_size=0 gso_segs=0 gso_type=0)
     0.020 net:netif_rx:dev=eth0 skbaddr=0xffff96d481988700 len=84)
     0.022 net:net_dev_xmit:dev=veth79215ff skbaddr=0xffff96d481988700 len=98 rc=0)
     0.024 net:net_dev_xmit:dev=docker0 skbaddr=0xffff96d481988700 len=98 rc=0)
     0.027 net:netif_receive_skb:dev=eth0 skbaddr=0xffff96d481988700 len=84)
     0.044 net:net_dev_queue:dev=eth0 skbaddr=0xffff96d481988b00 len=98)
     0.046 net:net_dev_start_xmit:dev=eth0 queue_mapping=0 skbaddr=0xffff96d481988b00 vlan_tagged=0 vlan_proto=0x0000 vlan_tci=0x0000 protocol=0x0800 ip_summed=0 len=98 data_len=0 network_offset=14 transport_offset_valid=1 transport_offset=34 tx_flags=0 gso_size=0 gso_segs=0 gso_type=0)
     0.048 net:netif_rx:dev=veth79215ff skbaddr=0xffff96d481988b00 len=84)
     0.050 net:net_dev_xmit:dev=eth0 skbaddr=0xffff96d481988b00 len=98 rc=0)
     0.053 net:netif_receive_skb:dev=veth79215ff skbaddr=0xffff96d481988b00 len=84)
     0.060 net:netif_receive_skb_entry:dev=docker0 napi_id=0x3 queue_mapping=0 skbaddr=0xffff96d481988b00 vlan_tagged=0 vlan_proto=0x0000 vlan_tci=0x0000 protocol=0x0800 ip_summed=2 hash=0x00000000 l4_hash=0 len=84 data_len=0 truesize=768 mac_header_valid=1 mac_header=-14 nr_frags=0 gso_size=0 gso_type=0)
     0.061 net:netif_receive_skb:dev=docker0 skbaddr=0xffff96d481988b00 len=84)
```

只保留事件名和`skbaddr`，看起来清晰很多：

```shell
net_dev_queue           dev=docker0     skbaddr=0xffff96d481988700
net_dev_start_xmit      dev=docker0     skbaddr=0xffff96d481988700
net_dev_queue           dev=veth79215ff skbaddr=0xffff96d481988700
net_dev_start_xmit      dev=veth79215ff skbaddr=0xffff96d481988700
netif_rx                dev=eth0        skbaddr=0xffff96d481988700
net_dev_xmit            dev=veth79215ff skbaddr=0xffff96d481988700
net_dev_xmit            dev=docker0     skbaddr=0xffff96d481988700
netif_receive_skb       dev=eth0        skbaddr=0xffff96d481988700

net_dev_queue           dev=eth0        skbaddr=0xffff96d481988b00
net_dev_start_xmit      dev=eth0        skbaddr=0xffff96d481988b00
netif_rx                dev=veth79215ff skbaddr=0xffff96d481988b00
net_dev_xmit            dev=eth0        skbaddr=0xffff96d481988b00
netif_receive_skb       dev=veth79215ff skbaddr=0xffff96d481988b00
netif_receive_skb_entry dev=docker0     skbaddr=0xffff96d481988b00
netif_receive_skb       dev=docker0     skbaddr=0xffff96d481988b00
```

这里有很多信息。

首先注意，`skbaddr`在中间变了（`0xffff96d481988700 -> 0xffff96d481988b00`）。
变的这里，就是生成了ICMP echo reply包，并作为应答包发送的地方。接下来的时间，这
个包的skbaddr保持不变，说明没有copy。copy非常耗时。

其次，我们可以清楚地看到**packet在内核的传输路径**：首先通过docker0网桥，然后veth pair的
宿主机端（`veth79215ff`)，最后是veth pair的容器端（容器里的`eth0`）；接下来是返回路径。

**至此，虽然我们还没有看到网络命名空间，但已经得到了一个不错的全局视图。**

## 4 进阶：选择跟踪点

上面的信息有一些杂，还有很多重复。我们可以选择几个最合适的跟踪点，使得输出看起来
更清爽。要查看所有可用的网络跟踪点，可以执行perf list：

```shell
$ sudo perf list 'net:*'
```

这个命令会列出tracepoint列表，名字类似于`net:netif_rx`。冒号前面是事件类型，后面是事件名称。

这里我选择了4个：

* `net_dev_queue`
* `netif_receive_skb_entry`
* `netif_rx`
* `napi_gro_receive_entry`

效果：

```shell
$ sudo perf trace --no-syscalls           \
    --event 'net:net_dev_queue'           \
    --event 'net:netif_receive_skb_entry' \
    --event 'net:netif_rx'                \
    --event 'net:napi_gro_receive_entry'  \
    ping 172.17.0.2 -c1 > /dev/null
       0.000 net:net_dev_queue:dev=docker0 skbaddr=0xffff8e847720a900 len=98)
       0.010 net:net_dev_queue:dev=veth7781d5c skbaddr=0xffff8e847720a900 len=98)
       0.014 net:netif_rx:dev=eth0 skbaddr=0xffff8e847720a900 len=84)
       0.034 net:net_dev_queue:dev=eth0 skbaddr=0xffff8e849cb8cd00 len=98)
       0.036 net:netif_rx:dev=veth7781d5c skbaddr=0xffff8e849cb8cd00 len=84)
       0.045 net:netif_receive_skb_entry:dev=docker0 napi_id=0x1 queue_mapping=0
```

漂亮！
