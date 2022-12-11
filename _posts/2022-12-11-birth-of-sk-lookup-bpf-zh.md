---
layout    : post
title     : "[译] Socket listen 多地址需求与 SK_LOOKUP BPF 的诞生（LPC, 2019）"
date      : 2022-12-11
lastupdate: 2022-12-11
categories: kernel tcp bpf
---

### 译者序

本文组合翻译 Cloudflare 的几篇分享，介绍了他们面临的独特网络需求、解决方案的演进，
以及终极解决方案 `SK_LOOKUP` BPF 的诞生：

1. [Programming socket lookup with BPF](https://linuxplumbersconf.org/event/4/contributions/487/), LPC, 2019
2. [It's crowded in here](https://blog.cloudflare.com/its-crowded-in-here/), Cloudflare blog, 2019
3. [Steering connections to sockets with BPF socket lookup hook](https://github.com/jsitnicki/ebpf-summit-2020)，eBPF Summit，2020

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 1 引言

## 1.1 现状：Cloudflare 边缘架构

Cloudflare 的边缘服务器里运行着大量程序，不仅包括很多**<mark>内部应用</mark>**，
还包括很多**<mark>公网服务</mark>**（public facing services），例如：

1. <a href="https://www.cloudflare.com/cdn/" target="_blank">HTTP CDN</a> (tcp/80)
1. <a href="https://www.cloudflare.com/ssl/" target="_blank">HTTPS CDN</a> (tcp/443, <a href="https://cloudflare-quic.com/" target="_blank">udp/443</a>)
1. <a href="https://www.cloudflare.com/dns/" target="_blank">authoritative DNS</a> (udp/53)
1. <a href="https://blog.cloudflare.com/dns-resolver-1-1-1-1/">recursive DNS</a> (udp/53, 853)
1. <a href="https://blog.cloudflare.com/secure-time/">NTP with NTS</a> (udp/1234)
1. <a href="https://blog.cloudflare.com/roughtime/">Roughtime time service</a> (udp/2002)
1. <a href="https://blog.cloudflare.com/distributed-web-gateway/">IPFS Gateway</a> (tcp/443)
1. <a href="https://blog.cloudflare.com/cloudflare-ethereum-gateway/">Ethereum Gateway</a> (tcp/443)
1. <a href="https://blog.cloudflare.com/spectrum/">Spectrum proxy</a> (tcp/any, udp/any)1
1. <a href="https://blog.cloudflare.com/announcing-warp-plus/">WARP</a> (udp)

这些应用通过横跨 **<mark>100+ 网段</mark>**的 **<mark>100 万个 Anycast</mark>**
[<mark>公网 IPv4 地址</mark>](https://www.cloudflare.com/ips)
提供服务。为了保持一致性，Cloudflare 的**<mark>每台服务器都运行所有服务</mark>**，
每台机器都能处理每个 Anycast 地址的请求，这样能**<mark>充分利用服务器硬件资源</mark>**，
在所有服务器之间做请求的负载均衡，如下图所示，

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/edge_data_center.png" width="80%" height="80%"></p>

之前的分享中已经介绍了 **<mark>Cloudflare 的边缘网络架构</mark>**，感兴趣可移步：

1. [No Scrubs: The Architecture That Made Unmetered Mitigation Possible](https://blog.cloudflare.com/no-scrubs-architecture-unmetered-mitigation/)
2. [(译) Cloudflare 边缘网络架构：无处不在的 BPF（2019）]({% link _posts/2019-06-12-cloudflare-arch-and-bpf-zh.md %})

## 1.2 需求：如何让一个服务监听至少几百个 IP 地址

**<mark>如何能让一个服务监听在至少几百个 IP 地址上</mark>**，而且能确保内核网络栈稳定运行呢？

这是 Cloudflare 工程师过去几年一直在思考的问题，其答案也在驱动着我们的网络不断演进。
特别是，它让我们**<mark>更有创造性地去使用</mark>**
<a href="https://en.wikipedia.org/wiki/Berkeley_sockets" target="_blank">Berkeley sockets API</a>，
这是一个给应用分配 IP 和 port 的 POSIX 标准。

下面我们来回顾一下这趟奇妙的旅程。

# 2 场景需求与解决方案演进

## 2.1 简单场景：一个 socket 监听一个 IP 地址

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/mapping_1_to_1.png" width="60%" height="60%"></p>
<p align="center">Fig. 最简单场景：一个 socket 绑定一个 <code>IP:Port</code></p>

这是最简单的场景，`(ip,port)` 与 service 一一对应。

* Service 监听在某个已知的 <mark><code>IP:Port</code></mark> 提供服务；
* Service 如果要支持多种协议类型（TCP、UDP），则需要**<mark>为每种协议类型打开一个 socket</mark>**。

例如，我们的[权威 DNS](https://www.cloudflare.com/dns/) 服务会分别针对 TCP 和 UDP 创建一个 socket：

```
    (192.0.2.1, 53/tcp) -> ("auth-dns", pid=1001, fd=3)
    (192.0.2.1, 53/udp) -> ("auth-dns", pid=1001, fd=4)
```

但是，对于 Cloudflare 的规模来说，我们需要**<mark>在至少 4K 个 IP</mark>**
上分别创建 socket，才能满足业务需求，也就是说 DNS 这个服务需要监听一个至少 `/20` 的网段。

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/mapping_many_1_to_1.png" width="55%" height="55%"></p>
<p align="center">Fig. 为了支撑业务规模，需要为 DNS 这样的服务在至少 4000 个 IP 地址上创建 socket</p>

如果用 `ss` 之类的工具看，就会看到非常长的 socket 列表：

```shell
$ sudo ss -ulpn 'sport = 53'
State  Recv-Q Send-Q  Local Address:Port Peer Address:Port
...
UNCONN 0      0           192.0.2.40:53        0.0.0.0:*    users:(("auth-dns",pid=77556,fd=11076))
UNCONN 0      0           192.0.2.39:53        0.0.0.0:*    users:(("auth-dns",pid=77556,fd=11075))
UNCONN 0      0           192.0.2.38:53        0.0.0.0:*    users:(("auth-dns",pid=77556,fd=11074))
UNCONN 0      0           192.0.2.37:53        0.0.0.0:*    users:(("auth-dns",pid=77556,fd=11073))
UNCONN 0      0           192.0.2.36:53        0.0.0.0:*    users:(("auth-dns",pid=77556,fd=11072))
UNCONN 0      0           192.0.2.31:53        0.0.0.0:*    users:(("auth-dns",pid=77556,fd=11071))
...
```

显然，这种方式非常原始和粗暴；不过也有它的优点：当其中一个 IP 遭受 UDP 泛洪攻击时，
其他 IP 不受影响。

## 2.2 进阶场景：一个 socket 监听多个 IP 地址

以上做法显然太过粗糙，一个服务就使用这么多 IP 地址。但更大的问题是：
监听的 **<mark>socket 越多，hash table 中的 chain 就越长，socket lookup 过程就越慢</mark>**。
我们在 <a href="https://blog.cloudflare.com/revenge-listening-sockets/">The revenge of the listening sockets</a>
中经历了这一问题。那么，有没有更好的办法解决这个问题呢？有。

### 2.2.1 `bind(INADDR_ANY)` 或 `bind(0.0.0.0)`

Socket API 中有个叫 **<mark><code>INADDR_ANY</code></mark>** 的东西，能让我们
避免以上那种 one-ip-per-socket 方式，如下图所示：

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/mapping_inaddr_any.png" width="55%" height="55%"></p>
<p align="center">Fig. INADDR_ANY socket：监听这台机器上所有 IP 地址的某个端口</p>

当指定一个 socket 监听在 `INADDR_ANY` 或 `0.0.0.0` 时，
这个 socket 会监听这台**<mark>机器上的所有 IP 地址</mark>**，
此时只需要提供一个 listen port 就行了：

```python
    s = socket(AF_INET, SOCK_STREAM, 0)
    s.bind(("0.0.0.0", 12345))
    s.listen(16)
```

除此之外，还有没有其他 bind 所有 local 地址的方式？有，但不是使用 `bind()`。

### 2.2.2 `listen()` unbound socket

直接在一个 **<mark>unbound socket 上调用 <code>listen()</code></mark>**，
其效果等同于 `INADDR_ANY`，但监听在哪个 **<mark>port 是由内核自动分配的</mark>**。

看个例子：首先用 `nc -l` 创建一个 listening socket，用 `strace` 跟踪其中的几个系统调用，

```shell
$ strace -e socket,bind,listen nc -l
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3
listen(3, 1)                            = 0
^Z
[1]+  Stopped                 strace -e socket,bind,listen nc -l
```

然后查看我们创建的 socket：

```shell
$ ss -4tlnp
State      Recv-Q Send-Q Local Address:Port               Peer Address:Port
LISTEN     0      1            *:42669
```

可以看到 listen 地址是 `*:42669`， 其中

* `*` 表示监听这个主机上所有 IP 地址，与 `INADDR_ANY` 等价；
* `42669` 就是内核为我们分配的 port。

### 2.2.3 技术原理：内核 socket lookup 逻辑

这里介绍下内核中的 socket lookup 逻辑，也就是当 TCP 层收到一个包时，
**<mark>如何判断这个包属于哪个 socket</mark>**。逻辑其实非常简单：
[两阶段](https://github.com/torvalds/linux/blob/v5.10/include/net/inet_hashtables.h#L342-L361)，
先精确匹配，再模糊匹配：

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/tcp_socket_lookup.png" width="90%" height="90%"></p>
<p align="center">Fig. 内核 socket lookup 逻辑：判断一个包应该送到哪个 socket</p>

1. 首先是 `(src_ip,src_port,dst_ip,dst_port)` 4-tuple 精确匹配，看能不能找到 **<mark>connected 状态的 socket</mark>**；如果找不到，
2. 再尝试 `(dst_ip,dst_port)` 2-tuple，寻找有没有 **<mark>listening 状态的 socket</mark>**；如果还是没找到，
3. 再尝试 `(INADDR_ANY)` 1-tuple，寻找有没有 **<mark>listening 状态的 socket</mark>**。

### 2.2.4 优缺点比较

如果我们给每个服务器都分配一个 IP 段（例如 `/20`），那通过以上两种方式，
我们都能实现**<mark>一次 socket 调用就监听在整个 IP 段</mark>**。
而且 INADDR_ANY 的好处是无需关心服务器的 IP 地址，加减 IP 地址不需要重新配置服务。

但另一方面，缺点也比较多：

1. **<mark>不是每个服务都需要 4000 个地址</mark>**，因此 `0.0.0.0` 是浪费的，
   可能还会不小心将一些重要内部服务暴露到公网；
2. 安全方面：**<mark>任何一个 IP 被攻击，都有可能导致这个 socket 的 receive queue 被打爆</mark>**。

    这是因为现在一台机器上只有一个 socket，监听在 4000 个地址上，攻击可能会命中任何一个 IP 地址。
    这种情况下
  <a href="https://blog.cloudflare.com/syn-packet-handling-in-the-wild/">TCP 还有办法应对</a>，
  但 UDP 就麻烦很多，需要特别关注，否则非法流量泛洪很容易将 socket 打挂。

3. **<mark>INADDR_ANY 使用的 port 是全局独占的</mark>**，一个 socket 使用了之后，
   这台机器上的其他 socket 就无法使用了。

    常见的是 `bind()` 时报 `EADDRINUSE` 错误：

    ```c
    bind(3, {sa_family=AF_INET, sin_port=htons(12345), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
    bind(4, {sa_family=AF_INET, sin_port=htons(12345), sin_addr=inet_addr("127.0.0.1")}, 16) = -1 EADDRINUSE (Address already in use)
    ```

    除非是 UDP-only 应用，否则设置 `SO_REUSEADDR` 也没用。

## 2.3 魔鬼场景：同一台机器上不同 service 使用同一个 port（IP 不重叠）

这就是上面提到的 “不是每个服务都需要 4000 个地址” 场景：如果两个服务使用了不同的一组 IP，
那它们使用相同的端口，也应该没有问题 —— 这正是 Cloudflare 的现实需求。

边缘服务器中确实存在多个服务使用相同的端口号，但监听在不同的 IP 地址段。
**<mark>典型的例子</mark>**：

1. [<mark><code>1.1.1.1</code></mark>](https://blog.cloudflare.com/dns-resolver-1-1-1-1/)：recursive DNS resolver
2. [<mark>权威 DNS 服务</mark>](https://www.cloudflare.com/dns)：提供给所有客户使用

这两个服务总是相伴运行的。但不幸的是，
<a href="http://man7.org/linux/man-pages/man2/bind.2.html" target="_blank">Sockets API</a>
**<mark>不支持同一主机上的多个服务使用不同的 IP 段，而共享相同的端口号</mark>**。
好在 Linux 的开发历史表明，任何不支持的功能，都能通过一个新的 
[socket option](https://github.com/torvalds/linux/blame/master/include/uapi/asm-generic/socket.h) 来支持（现在已经有 60 多个 options）。

因此，2016 年我们内部引入了 `SO_BINDTOPREFIX`，能 listen 一个 `(ipnetwork, port)`，

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/mapping_bindtoprefix.png" width="60%" height="60%"></p>

但这个功能**<mark>不够通用，内核社区不接受</mark>**，我们只能内部维护 patch。

## 2.4 地狱场景：一个 service 监听所有 65535 个端口

前面讨论的都是一个 socket 监听在多个 IP，但同一个 port。虽然有点怪，但常人还能理解。

* —— 听说过一个 socket 同时监听在多个 port 吗？
* —— 多个是多少个？
* —— 整个目的端口空间（**<mark><code>16bit dst port</code></mark>**），**<mark>65535 个</mark>**。
* —— 好家伙！

这也是 Cloudflare 的现实需求。这个产品是个反向代理（reverse proxy），叫 
[Spectrum](https://www.cloudflare.com/products/cloudflare-spectrum)。

`bind()` 系统调用显然并未考虑这种需求，在将一个给定的 socket 关联到一个 port 时，

* 要么自己指定**一个** port；
* 要么让系统网络栈给你分一个；

我们不禁开个脑洞：能不能和 INADDR_ANY 类似，搞个 INPORT_ANY 之类的东西来选中所有 ports 呢？

### 2.4.1 `iptables + TPROXY`

利用一个叫 [TPROXY](https://www.kernel.org/doc/Documentation/networking/tproxy.txt)
的 **<mark>netfilter/iptables extension</mark>**是能做到的，在 forward path 上拦截流量，

```shell
$ iptables -t mangle -I PREROUTING \
         -d 192.0.2.0/24 -p tcp \
         -j TPROXY --on-ip=127.0.0.1 --on-port=1234
```

更多信息见 [How we built spectrum](https://blog.cloudflare.com/how-we-built-spectrum)。

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/mapping_tproxy.png" width="70%" height="70%"></p>
<p align="center">Fig. TPROXY 拦截不同端口的流量，透明转发到本机最终 socket</p>

### 2.4.2 `TPROXY` 方案缺点

TPROXY 方案是有代价的：

首先，服务需要**<mark>特殊权限才能创建支持 TPROXY 功能的 socket</mark>**，见 [IP_TRANSPARENT](http://man7.org/linux/man-pages/man7/ip.7.html)；

其次，需要深入理解 TPROXY 和流量路径之间的交互，例如

1. TPROXY 重定向的 flow，会不会被 connection tracking 记录？
1. is listening socket contention during a SYN flood when using TPROXY a concern?
1. 网络组其他部分，例如 XDP programs，是否需要感知 TPROXY 重定向包这件事情？

虽然我们把这个方案最终推到了生产，但不得不说，这种方式太 hack 了，很难 hold 住。

### 2.4.3 有没有银弹？

上面提到的 TPROXY 方案虽然 hack，但传达出一个**<mark>极其重要的思想</mark>**：

**<mark>不管一个 socket 监听在哪个 IP、哪个 port，我们都能通过在更底层的网络栈上“做手脚”，将任意连接、任意 socket 的包引导给它</mark>**。
socket 之上的应用对此是无感的。

先理解这句话，再往下走。

意识到这一点是相当重要的，这意味着只需要一些 TPROXY（或其他）规则，我们就可以
**<mark>完全掌控和调度 (ip,port) 和 socket 之间的映射关系</mark>**。这里我们所说的 socket 是在本机内的 socket。

而要**<mark>更好地调度这种映射关系，就轮到 BPF 出场了</mark>**。

> BPF is absolutely the way to go here, as it allows for whatever user
> specified tweaks, like a list of destination subnetwork, or/and a list of
> source network, or the date/time of the day, or port knocking without
> netfilter, or ... you name it.
>
> —— Suggestions from the kernel community

# 3 `SK_LOOKUP` BPF：对 socket lookup 过程进行编程

## 3.1 设计思想

想法很简单：**<mark>编写一段 BPF 程序来决定如何将一个包映射到一个 socket</mark>** ——
不管这个 socket 监听在哪个地址和端口。

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/idea_program_socket_lookup_with_bpf.png" width="75%" height="75%"></p>
<p align="center">Fig. 通过自定义 BPF 程序将数据包送到期望的 socket</p>

如上图例子所示，

* 所有目的地址是 `192.0.2.0/24 :53` 的包，都转发给 `sk:2`
* 所有目的地址是 `203.0.113.1 :*` 的包，都转发给 `sk:4`

## 3.2 引入新的 BPF 程序类型 SK_LOOKUP

要实现这个效果，就需要一个**<mark>新的 BPF 程序类型</mark>**。

### 3.2.1 程序执行位置

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/bpf_inet_lookup_hook.png" width="100%" height="100%"></p>

* 在收包路径上**<mark>给一个包寻找（lookup）合适的 socket</mark>**，因此叫 SK_LOOKUP；
* 位置是在**<mark>包到达 socket 的 rxq 之前</mark>**。

### 3.2.2 工作原理

前面提到过 Linux 内核的[两阶段 socket lookup 过程](https://github.com/torvalds/linux/blob/v5.10/include/net/inet_hashtables.h#L342-L361)：

1. 先用 4-tuple 查找有没有 connected 状态 socket；如果没有，
2. 再用 2-tuple 查找有没有 listening 状态的 socket。

SK_LOOKUP 就是**<mark>对上面第二个过程进行编程</mark>**，也就是查找 listening socket 过程。

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/tcp_socket_lookup_with_bpf.png" width="75%" height="75%"></p>

* 如果 BPF 程序找到了 socket(s)，就选择一个合适的 socket，然后终止内核 lookup 过程（HIT）；
* BPF 也可以忽略某些包，不做处理，这些包继续走内核原来的逻辑。

具体 BPF 信息：

* BPF 程序类型：**<mark><code>BPF_PROG_TYPE_SK_LOOKUP</code></mark>**
* Attach 类型：BPF_SK_LOOKUP
* 更多信息见 [BPF 进阶笔记（一）：BPF 程序（BPF Prog）类型详解：使用场景、函数签名、执行位置及程序示例]({% link _posts/2021-07-04-bpf-advanced-notes-1-zh.md %})

## 3.3 BPF 程序示例

见 [<mark>Pidfd and Socket-lookup (SK_LOOKUP) BPF Illustrated (2022)</mark>]({% link _posts/2022-12-11-pidfd-and-socket-lookup-bpf-illustrated.md %})。

## 3.4 Demo

### 3.4.1 效果：单个 socket 同时监听 4 个端口

单个 TCP socket 同时监听在 7, 77, 777, 7777 四个端口。

### 3.4.2 创建服务端 echo server

两个工具：

* `ncat`：Concatenate and redirect sockets
* `nc`： arbitrary TCP and UDP connections and listens

```
NAME
       ncat - Concatenate and redirect sockets

SYNOPSIS
       ncat [OPTIONS...] [hostname] [port]

OPTIONS SUMMARY
             -4                         Use IPv4 only
             -e, --exec <command>       Executes the given command
             -l, --listen               Bind and listen for incoming connections
             -k, --keep-open            Accept multiple connections in listen mode
             ...
```

注意在有的发行版上，可能是用 `nc` 命令，二者的大部分参数都是一样的：

```shell
NAME
     nc — arbitrary TCP and UDP connections and listens
```

如果你的环境上 `nc` 支持 `-e` 选项，那可以直接用 `nc` 即可。
不过实际测试发现在 Ubuntu 20.04 上， `nc` 不支持 `-e` 参数。因此我们这里用 `ncat`。

```shell
$ sudo apt install ncat
```

现在**<mark>创建 server</mark>**：

```shell
$ ncat -4lke $(which cat) 127.0.0.1 7777
```

查看 listening socket 信息：

```shell
$ ss -4tlpn sport = 7777
State    Recv-Q   Send-Q  Local Address:Port   Peer Address:Port      Process
LISTEN   0        1       127.0.0.1:7777       0.0.0.0:*              users:(("nc",pid=91994,fd=3))
```

### 3.4.3 客户端访问测试

```shell
$ nc 127.0.0.1 7777
hello
hello
^C
```

### 3.4.4 编译、加载 BPF 程序

见 [<mark>Pidfd and Socket-lookup (SK_LOOKUP) BPF Illustrated (2022)</mark>]({% link _posts/2022-12-11-pidfd-and-socket-lookup-bpf-illustrated.md %})。

加载完 BPF 程序之后，测试：

```shell
$ echo 'Steer'    | timeout 1 nc -4 127.0.0.1 7;   \
  echo 'on'       | timeout 1 nc -4 127.0.0.1 77;  \
  echo 'multiple' | timeout 1 nc -4 127.0.0.1 777; \
  echo 'ports'    | timeout 1 nc -4 127.0.0.1 7777
Steer
on
multiple
ports
```

# 4 总结

本文整理了 Cloudflare 三篇文章，介绍了他们面临的独特需求、解决方案的演进，以及
终极解决方案 SK_LOOKUP BPF 的诞生。对资深网络工程师和网络架构师有较大参考价值。
