---
layout    : post
title     : "tcpdump/wireshark 抓包及分析（2019）"
date      : 2018-12-14
lastupdate: 2024-08-19
author    : ArthurChiao
categories: tcpdump wireshark
---

本文将展示如何使用 tcpdump 抓包，以及如何用 tcpdump 和 wireshark 分析网络流量。
文中的例子比较简单，适合作为入门参考。

----

* TOC
{:toc}

----

# 1 基础环境准备

为方便大家跟着上手练习，本文将搭建一个容器环境。

## 1.1 Pull Docker 镜像

```shell
$ sudo docker pull alpine:3.8
```

## 1.2 运行容器

```shell
$ sudo docker run -d --name ctn-1 alpine:3.8 sleep 3600d
$ sudo docker ps
CONTAINER ID    IMAGE        COMMAND         CREATED        STATUS          PORTS  NAMES
233bc36bde4b    alpine:3.8   "sleep 3600d"   1 minutes ago  Up 14 minutes           ctn-1
```

进入容器：

```shell
$ sudo docker exec -it ctn-1 sh
```

查看容器网络信息：

```shell
/ # ifconfig
eth0      Link encap:Ethernet  HWaddr 02:42:AC:11:00:09
          inet addr:172.17.0.9  Bcast:0.0.0.0  Mask:255.255.0.0
```

## 1.3 安装 tcpdump

```shell
/ # apk update
/ # apk add tcpdump
```

# 2 HTTP/TCP 抓包

接下来我们用 wget 获取一个网站的首页文件（index.html），同时 tcpdump 抓包，对抓
到的网络流量进行分析。

## 2.1 HTTP 请求：下载测试页面

[example.com](www.example.com) 是一个测试网站，wget 是一个 linux 命令行工
具，可以下载网络文件。

如下命令可以下载一个 example.com 网站的首页文件 index.html：

```shell
/ # wget http://example.com
Connecting to example.com (93.184.216.34:80)
index.html           100% |*****************************|  1270   0:00:00 ETA
```

虽然这看起来极其简单，但背后却涵盖了很多复杂的过程，例如：

1. **域名查找**：通过访问 DNS 服务查找 `example.com` 服务器对应的 IP 地址
2. **TCP 连接参数初始化**：临时端口、初始序列号的选择等等
3. 客户端（容器）通过 **TCP 三次握手协议**和服务器 IP 建立 TCP 连接
2. 客户端发起 HTTP GET 请求
1. 服务器返回 HTTP 响应，包含页面数据传输
2. 如果页面超过一个 MTU，会分为多个 packet 进行传输（后面会看到，确实超过 MTU 了）
2. TCP 断开连接的**四次挥手**

## 2.2 抓包：打到标准输出

用下面的 tcpdump 命令抓包，另一窗口执行 `wget http://example.com`，能看到如下类
似的输出。为了方便后面的讨论，这里将一些字段去掉了，并做了适当的对齐：

```shell
/ # tcpdump -n -S -i eth0 host example.com
1  02:52:44.513700 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [S] , seq 3310420140,                            length 0
2  02:52:44.692890 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [S.], seq 1353235534,            ack 3310420141, length 0
3  02:52:44.692953 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [.] ,                            ack 1353235535, length 0
4  02:52:44.693009 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [P.], seq 3310420141:3310420215, ack 1353235535, length 74: HTTP: GET / HTTP/1.1
5  02:52:44.872266 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [.] ,                            ack 3310420215, length 0
6  02:52:44.873342 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [.] , seq 1353235535:1353236983, ack 3310420215, length 1448: HTTP: HTTP/1.1 200 OK
7  02:52:44.873405 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [.] ,                            ack 1353236983, length 0
8  02:52:44.874533 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [P.], seq 1353236983:1353237162, ack 3310420215, length 179: HTTP
9  02:52:44.874560 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [.] ,                            ack 1353237162, length 0
10 02:52:44.874705 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [F.], seq 3310420215,            ack 1353237162, length 0
11 02:52:45.053732 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [.] ,                            ack 3310420216, length 0
12 02:52:45.607825 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [F.], seq 1353237162,            ack 3310420216, length 0
13 02:52:45.607869 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [.] ,                            ack 1353237163, length 0
```

参数说明：

* `-n`：打印 IP 而不是 hostname，打印端口号而不是协议（例如打印 80 而不是 http）
* `-S`：打印绝对时间戳
* `-i eth0`：指定从 eth0 网卡抓包
* `host example.com`：抓和 `example.com` 通信的包（双向）

更多 tcpdump 的常用命令，可以参考[tcpdump: An Incomplete Guide](https://arthurchiao.github.io/blog/tcpdump/)。

## 2.3 抓包：存文件

`-w` 命令可以将抓到的包写到文件，注意这和用重定向方式将输出写到文件是不同的。
后者写的只是标准输出打印的 LOG，而 `-w` 写的是原始包。

```shell
/ # tcpdump -i eth0 host example.com -w example.pcap
^C
13 packets captured
13 packets received by filter
0 packets dropped by kernel
```

生成的 pcap 文件可以用 `tcpdump` 或者 `wireshark` 之类的网络流量分析工具打开。

# 3 流量分析: tcpdump

如果不指定输出的话，tcpdump 会直接将信息打到标准输出，就是我们上面看到的那样。从
这些输出里，我们看到很多信息。

## 3.1 每列说明

第 1 列是为了讨论方便而加的行号，实际的 tcpdump 输出并没有这一列。接下来将用 `#`
号加数字表示第几个包，例如 `#3` 表示第 3 个包。

接下来依次为：

* packet **时间戳**，例如 `02:52:44.513700` 表示抓到这个包的时间是** 02 时 52 分 44 秒 513 毫秒**
* packet **类型**，这里是 `IP` 包
* 源 (SRC) IP 和端口，目的 (DST) IP 和端口
* packet TCP flags，其中
    * `S` 表示 `syn` 包
    * `.` 表示 `ack` 包
    * `F` 表示 `fin` 包
    * `P` 表示 `push` 包（发送正常数据）
* 序列号（seq）
* 应答号（ack）
* 包的 payload 长度
* 包的部分内容（ASCII）

## 3.2 三次握手（1～3）

wget 是基于 HTTP 协议，因此它在下载文件之前，必定要和服务端建立一个连接。

而 TCP 建立连接的过程就是著名的**三次握手** [4]：

1. client -> server: SYN
1. server -> client: SYN+ACK
1. client -> server: ACK

我们可以看到，这刚好对应于前三个包：

```shell
1  02:52:44.513700 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [S] , seq 3310420140,                 length 0
2  02:52:44.692890 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [S.], seq 1353235534, ack 3310420141, length 0
3  02:52:44.692953 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [.] ,                 ack 1353235535, length 0
```

### 第一次握手: SYN

`#1` 包含以下信息：

1. `02:52:44.513700` 时刻，客户端主动向 server（93.184.216.34）发起一个 SYN 请求，请求建立连接
1. 客户端请求的**服务端端口是 80**（HTTP 服务默认 80 端口），客户端使用的是**临时端口**（大于 1024）41038
1. `#1` 序列号是 `3310420140`，这是客户端的初始序列号（客户端和服务端分别维护自己的序列号，两者没有关系；另外，初始序列号是系统选择的，一般不是 0）
1. `#1` length 为 0，因为 SYN 包不带 TCP payload，所有信息都在 TCP header

### 第二次握手: SYN+ACK

`#2` 的 ack 是 `3310420141`，等于 `#1` 的 seq 加 1，这就说明，`#2` 是 `#1` 的应
答包。

这个应答包的特点：

1. TCP flags 为 `S.`，即 SYN+ACK
1. length 也是 0，说明没有 payload
1. seq 为 `1353235534`，这是**服务端的初始序列号**
1. 到达 eth0 的时间为 `02:52:44.692890`，说明时间过了 `18ms`

### 第三次握手: ACK

同理，`#3` 的 ack 等于 `#2` 的 seq 加 1，说明 `#3` 是 `#2` 的应答包。

这个包的特点：

1. TCP flags 为 `.`，即 ACK
1. 长度为 0，说明没有 TCP payload

至此，三次握手完成。

## 3.3 正常数据传输

三次握手完成后，client 和 server 开始 HTTP 通信，客户端通过 HTTP GET 方法下载 index.html。

```shell
4  02:52:44.693009 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [P.], seq 3310420141:3310420215, ack 1353235535, length 74: HTTP: GET / HTTP/1.1
5  02:52:44.872266 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [.] ,                            ack 3310420215, length 0
6  02:52:44.873342 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [.] , seq 1353235535:1353236983, ack 3310420215, length 1448: HTTP: HTTP/1.1 200 OK
7  02:52:44.873405 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [.] ,                            ack 1353236983, length 0
8  02:52:44.874533 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [P.], seq 1353236983:1353237162, ack 3310420215, length 179: HTTP
9  02:52:44.874560 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [.] ,                            ack 1353237162, length 0
```

这里可以看到：

1. `#4`: client 向 server 发起 HTTP GET 请求，请求路径为根路径（`/`），这个 packet 长度为 74 字节
2. `#5`: 发送了 ACK 包，对 `#4` 进行确认
3. `#6`: 发送了 1448 字节的数据给 client
4. `#7`: client 对 server 的 `#6` 进行应答
5. `#8`: server 向 client 端继续发送 179 字节数据
6. `#9`: client 对 server 的 `#8` 进行应答

## 3.4 四次挥手

最后是四次挥手 [5]：

1. client -> server: FIN （我们看到的是 FIN+ACK，这是因为这个 FIN 包除了正常的关闭连接功能之外，还被用于应答 client 发过来的前一个包）
1. server -> client: ACK
1. server -> client: FIN+ACK
1. client -> server: ACK

```shell
10 02:52:44.874705 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [F.], seq 3310420215, ack 1353237162, length 0
11 02:52:45.053732 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [.] ,                 ack 3310420216, length 0
12 02:52:45.607825 IP 93.184.216.34.80 > 172.17.0.9.41038: Flags [F.], seq 1353237162, ack 3310420216, length 0
13 02:52:45.607869 IP 172.17.0.9.41038 > 93.184.216.34.80: Flags [.] ,                 ack 1353237163, length 0
```

# 4 流量分析: wireshark

tcpdump 可以指定 `-r` 读取 pcap 文件，并以指定的格式输出包的信息，最后输出的内容
和上面看到的类似。我们上面的流量非常简单，所以看 tcpdump 的输出就够了。

对于复杂的 pcap，例如，其中包含了上百个 IP 地址、上千个端口、上万个连接的 pcap，
通过 tcpdump 看输出可能就比较低效了。

这时，[wireshark](https://www.wireshark.org) 这样带图形用户界面，且功能强大的网
络流分析工具就派上了用场。

wireshark 支持强大的过滤功能，支持按 IP、端口、协议、连接、TCP flag 以及它们的各
种组合进行过滤，然后进行分析，大大节省网络排障的时间。

wireshark 官方维护了一个 sample pcap[列表
](https://wiki.wireshark.org/SampleCapturesA)，我们拿
[iperf-mptcp-0-0.pcap](https://wiki.wireshark.org/SampleCaptures?action=AttachFile&do=get&target=iperf-mptcp-0-0.pcap)
作为例子来展示如何使用 wireshark。

## 4.1 追踪 TCP 流

下载后双击就可以用 wireshark 打开。看到有重传（TCP Retransmition）的包：

<p align="center"><img src="/assets/img/tcpdump-practice/retrans.jpg" width="80%" height="80%"></p>

在重传的包上，`右键 -> Follow -> TCP Stream`，会过滤出**只属于这个连接的包**：

<p align="center"><img src="/assets/img/tcpdump-practice/retrans-stream.jpg" width="80%" height="80%"></p>

我们看到，这个连接只有 3 个包：

1. `#1` 在 `08:00:05.125` 发送出去，请求建立连接
1. 大约 `1s` 后，客户端仍然没有收到服务端的 ACK 包，触发客户端 **TCP 超时重传**
1. 又过了大约 `2s`，仍然没有收到 ACK 包，**再次触发超时重传**
1. 这里其实还可以看出 TCP 重传的机制：**指数后退**，比如第一次等待 1s，第二次等
   待 2s，第三次等待 4s，第四次 8s

因此，从这个抓包文件看，这次连接没有建立起来，而直接原因就是 client 没有收到
server 的应答包。要跟进这个问题，就需要在 server 端一起抓包，看应答包是否有发出来
。本文不对此展开。

## 4.2 过滤流

上面的截图我们看到 wireshark 里有 `tcp.stream eq 1`，这其实就是其强大的过滤表达式。

我们可以直接手写表达式，然后回车，符合条件的包就会显示出来。而且，在编辑表达式的
时候，wireshark 有自动提示，还是比较方便的。这些表达式和 tcpdump 的 filter 表达
式很类似，如果熟悉 tcpdump，那这里不会有太大困难。

下面举一些例子：

1. `ip.addr == 192.168.1.1` 过滤 SRC IP **或** DST IP 是 `192.168.1.1` 的包
1. `ip.src_host == 192.168.1.1 and ip.dst_host == 192.168.1.2` 过滤 SRC IP 是
   `192.168.1.1`，并且 DST IP 是 `192.168.1.2` 的包
1. `tcp.port == 80` 源端口或目的端口是 80 的包
1. `tcp.flags.reset == 1` 过滤 TCP RST 包。先找到 RST 包，然后右键 Follow -> TCP
   Stream 是常用的排障方式
1. `tcp.analysis.retransmission` 过滤所有的重传包

## 4.3 导出符合条件的包

有时 pcap 文件太大，导致 wireshark 非常慢，而大部分数据包可能是不需要的。在这种情况
下，可以先用过滤条件筛选出感兴趣的包，然后 `File -> Export Specified Packets ...`
，弹出的对话框里，可以选择当前显示的包，或者某个指定区间的包另存为新 pcap。

然后就可以关闭原来的 pcap，打开新的 pcap 进行分析。

# 5 总结

tcpdump 和 wireshark 功能非常强大，组合起来更是网络排障的首选利器。这里介绍的内
容只是九牛一毛，更多的时候，你需要 **tcpdump+wireshark+google**。

# References

1. [Man Page of tcpdump](https://www.tcpdump.org/manpages/tcpdump.1.html)
2. [Wireshark](https://www.wireshark.org)
3. [Wireshark: Sample Pcaps](https://wiki.wireshark.org/SampleCapturesA)
4. [TCP 3-way Handshaking](https://en.wikipedia.org/wiki/Handshaking#TCP_three-way_handshake)
5. [TCP 4-times Close](https://wiki.wireshark.org/TCP%204-times%20close)
6. [tcpdump: An Incomplete Guide](https://arthurchiao.github.io/blog/tcpdump/)
