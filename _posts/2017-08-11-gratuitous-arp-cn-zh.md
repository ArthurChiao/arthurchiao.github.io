---
layout: post
title:  "免费ARP (Gratuitous ARP)"
date:   2017-08-11
last_modified: 2017-08-11
categories: network
---

## 1. 基础知识

以太网内，在2层(Link Layer)，唯一标识一个主机（更准确地说，是标识主机的一个端口
）的是它的`MAC地址`。在3层，唯一的标识是`IP地址`。

MAC地址是出厂时设定的，理论上在全球范围内唯一，出厂后也不会被更改（当然，也有例
外的情况，后面会提到）。IP地址则是网络管理员分配的，通过自动或手动方式配到某个主
机上。显然，**MAC地址和IP地址并没有公式可以转换**，完全是随主机环境而定的。

```shell
# show MAC (HWaddr) and IP (inet addr) address
$ ifconfig eth0 | head -n 2
eth0      Link encap:Ethernet  HWaddr fa:16:3e:39:8c:fd
          inet addr:10.18.5.13  Bcast:10.18.5.255  Mask:255.255.255.0
```

那么，**给定一个主机的IP地址，如何得到它的MAC地址呢？**

正解是：`ARP` (Address Resolution Protocol, 地址解析协议)，将给定的IP地址转换为MAC地址。


## 2. ARP

`ARP`，是一个2层协议，通过**2层广播查找给定IP对应的MAC地址**。

例如，已知Host B的IP地址，在Host A上通过ARP查找Host B的MAC地址。

在Host A上发送ARP请求，内容为`who has [IP_B], tell [IP_A]`,
包里携带了主机B的IP地址，以及主机A的IP和MAC。收到广播包的所有主机会检查请求的IP
地址是否是自己的，如果是，就会发送一个ARP应答（单播，从B到A），内容为 `[IP_B] is
at [MAC_B]`，包里携带了主机A和B的MAC及IP地址。

`arping`是一个命令行实现,比如，要查找`10.18.5.14`对应的MAC地址:

```shell
$ arping -I eth0 10.18.5.14
ARPING 10.18.5.14 from 10.18.5.13 eth0
Unicast reply from 10.18.5.14 [FA:16:3E:1C:95:E0]  1.142ms
Unicast reply from 10.18.5.14 [FA:16:3E:1C:95:E0]  0.945ms
Unicast reply from 10.18.5.14 [FA:16:3E:1C:95:E0]  0.987ms
```

同时在eth0上面抓包：

```shell
$ sudo tcpdump -n -i eth0 'arp'
15:13:30.592188 ARP, Request who-has 10.18.5.14 tell 10.18.5.13, length 28
15:13:30.592544 ARP, Reply 10.18.5.14 is-at fa:16:3e:1c:95:e0, length 46
```

可以看出，`10.18.5.14`对应的主机的MAC地址是`FA:16:3E:1C:95:E0`。

当需要和某个主机通信，只知道它的IP，不知道MAC时候，操作系统就会通过
ARP来查找MAC，这些都是kernel协议栈自动完成的。

## 3. Gratuitous ARP

从上节可以看出，ARP请求带两个IP地址，对端IP和自己的IP。
如果**对端IP也是自己**，即`who has [IP A], tell [IP A]`，会发生
什么情况呢？

首先，这种情况是协议允许的，并且有个正式的名字，就是我们要介绍的Gratuitous ARP，中文译作免费
ARP。（中文名字有点费解, 
**正常的ARP是向其他主机请求信息，而免费ARP是主动向其他主机广播自己的信息，我理解
这大概就是“免费”的原因吧。**）。

举个具体的例子来说明GARP的用途。

一个新员工A入职时，他会有**两个在公司内的唯一标识**：自己的**邮件地址Email_A**和**员工
编号ID_A**。如果他这时候给全公司群发一个邮件，内容为：`员工编号为ID_A的同事，请
回复此邮件`, 那么正常情况下，他是收不到回应的（请忽略欺骗行为）。但假如他收到回
复了，那：**擦，我的员工ID和别人冲突了**，在一个公司内，这是绝对不允许发生的。

第二种情况，虽极少发生，但也有可能，就是他**因为某种原因换了个Email地址**，
那么，他就要第一时间通知给全公司。这时，他用**新邮箱Email_X**发一个同样的邮件
给全公司：`员工ID为ID_A的同事，请回复此邮件`。这里我们假设，收到邮件的人都会看一
眼，然后记录下，**噢，老铁A换邮箱了，新邮箱是这个(Email_X)**。以后，其他人就
会通过新邮箱和他联系。

做如下类比，就是GARP的原理：

* 公司 -> Subnet
* Email -> MAC Address
* Employee ID -> IP Address

所以，**GARP解决了两个问题**：

* **IP冲突检测**
* **MAC地址变化时主动通知其他主机**

就这两句话。更多细节，参见《TCP/IP详解》。

## References

1. ***TCP/IP Illustrated, 1st Edition, Volume 1***, Chapter 4, ARP

    **第一版，第一版，第一版。**重要的事情说三遍，建议看第一版。第二版是另一个人写的，
    篇幅大量删减，章节胡乱调整，虽有部分可取之处，但和经典的《TCP/IP详解》相比，已是
    另一本书。
