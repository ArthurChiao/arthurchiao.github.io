---
layout    : post
title     : "[译] BGP 泄露和加密货币（2018）"
date      : 2020-03-22
lastupdate: 2020-03-22
categories: bgp
---

### 译者序

本文翻译自 2018 年 Cloudflare 的一篇博客
[BGP leaks and cryptocurrencies](https://blog.cloudflare.com/bgp-leaks-and-crypto-currencies/)。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

<p align="center"><img src="/assets/img/bgp-leak-and-crypto-currencies/header.jpg" width="60%" height="60%"></p>
<p align="center">CC BY 2.0 image by elhombredenegro</p>

过去几个小时涌现出很多报导同一故事的[新闻
](https://www.forbes.com/sites/thomasbrewster/2018/04/24/a-160000-ether-theft-just-exploited-a-massive-blind-spot-in-internet-security/)：
某个黑客如何（或者[已经](https://twitter.com/killeswagon/status/988795209361252357)）
**通过 BGP 泄露来盗取加密货币**。

# 1. BGP 是什么？

互联网是由路由（routes）组成的。例如，对于我们的 DNS 解析服务器
[1.1.1.1](https://cloudflare-dns.com/?_ga=2.99645726.1455429069.1584838951-1412456748.1553949817)，
我们会告诉全世界：`1.1.1.0 ~ 1.1.1.255` 的所有 IP 地址都能够通过 Cloudflare 的
PoP 点直接访问。

没有[与我们的路由器直接相连](https://blog.cloudflare.com/think-global-peer-local-peer-with-cloudflare-at-100-internet-exchange-points/)
也没关系，你仍然能收到来自中转供应商（transit providers）的路由，后者直接或间接
地与 Cloudflare 及互联网的其他部分相连，因此能够将数据包正确地发送到我们的 DNS 解
析服务器。

这就是**正常情况下互联网的运行方式**。

世界上有几个**权威机构**（Regional Internet Registries, or RIRs，**地区性互联网
注册机构**）负责 IP 地址的分配，以避免 IP 地址有冲突。这些机构包括

* IANA
* RIPE
* ARIN
* LACNIC
* APNIC
* AFRINIC

# 2. 什么是 BGP 泄露？

<p align="center"><img src="/assets/img/bgp-leak-and-crypto-currencies/leak.jpg" width="60%" height="60%"></p>
<p align="center">CC BY 2.0 image by elhombredenegro</p>

BGP 泄露（BGP leak）的广义定义为：

> **某个路由器向网络通告它拥有某段 IP 地址空间，但实际上拥有该地址空间的另有其人**。

中转供应商收到 Cloudflare 通告的 `1.1.1.0/24` 后，可以继续转发给互联网的其他部分
，这样的行为是合法的。这些中转供应商也会**用 RIR 信息来验证只有 Cloudflare 能向它
们通告这条路由**。

但路由通告的合法性验证可能会**比较耗时**，尤其是考虑到**互联网目前的路由条目（
records）规模有 `700K+`**。

本质上来说，路由泄露是局部性的（route leaks are localized）。你的连接越局部（
more locally connected），接受泄露的路由的风险就越低。

**路由的特点**：

* 前缀越精确（掩码越长），优先级越高（10.0.0.1/32 = 1 IP vs 10.0.0.0/24 = 256 IPs）
* 前缀相同的情况下，metrics 更好（路径更短）的优先级越高

BGP 泄露**通常都是配置错误导致的**，例如某个路由器突然对外通告了：

* 它学到的路由（a router suddenly announces the IPs it learned）
* 它内部使用的某条用于流量工程（traffic engineering）的子路由（smaller prefixes）

但有时也可能是恶意行为：

* BGP 泄露之后流量会经过通告者的路由器，这样就可以**对流经的数据进行分析**。
* 或者，还可以**对经过的流量进行非法重放**（reply illegitimately），这是以前发生过的。

# 3. 今天发生了什么？

Cloudflare 维护了许多 BGP collector，用于从全世界的路由器收集 BGP 信息。

在今天大约 `11:05:00 UTC ~ 12:55:00 UTC`，我们看到了下面这些通告：

```shell
BGP4MP|04/24/18 11:05:42|A|205.251.199.0/24|10297
BGP4MP|04/24/18 11:05:42|A|205.251.197.0/24|10297
BGP4MP|04/24/18 11:05:42|A|205.251.195.0/24|10297
BGP4MP|04/24/18 11:05:42|A|205.251.193.0/24|10297
BGP4MP|04/24/18 11:05:42|A|205.251.192.0/24|10297
...
BGP4MP|04/24/18 11:05:54|A|205.251.197.0/24|4826,6939,10297
```

这些 `/24` 的路由都是下面这些 `/23` 路由通告的拆分（more specifics announcements）：

* `205.251.192.0/23`
* `205.251.194.0/23`
* `205.251.196.0/23`
* `205.251.198.0/23`

这些 `/23` IP 空间属于 **Amazon**（AS16509），但从自治系统编号（ASN）可以查到，
上面的通告来自 **eNet Inc**（AS10297），后者通告给了它的 BGP 邻居，并转发给了
**Hurricane Electric** (AS6939)。

这些 IP 是 [Route53 Amazon DNS 服务器的地址空间](https://ip-ranges.amazonaws.com/ip-ranges.json)。
当请求 Amazon 的服务时，这些服务器就会响应。

**在发生路由泄露的两个小时期间，这些 IP 地址只响应对 `myetherwallet.com` 的查询请求**，
正如大家在 [SERVFAIL](https://puck.nether.net/pipermail/outages/2018-April/011257.html) 看到的。

Route53 处理 DNS 请求时都会去查询权威服务器，而此时**由于 BGP 泄露，权威服务器已
经被接管（劫持）了**。如果一个路由器接受了这些路由，那依赖这个路由器的 DNS 服务
器都会受到污染（poisoned），这其中就包括 Cloudflare DNS resolver 1.1.1.1。我们在
Chicago, Sydney, Melbourne, Perth, Brisbane, Cebu, Bangkok, Auckland, Muscat,
Djibouti and Manilla 都受到了影响。而在其他地方，1.1.1.1 工作是正常的。

<p align="center"><img src="/assets/img/bgp-leak-and-crypto-currencies/twitter-1.png" width="60%" height="60%"></p>

<p align="center"><img src="/assets/img/bgp-leak-and-crypto-currencies/twitter-2.png" width="60%" height="60%"></p>

例如，正常情况下，下面的查询会返回正确的 Amazon IP 地址：

```shell
$ dig +short myetherwallet.com @205.251.195.239
54.192.146.xx
```

但在被劫持期间，这个查询返回的是某个俄罗斯供应商（AS48693 and AS41995）的 IP。需
要注意的是，**不管你有没有接受被劫持的路由，你都是这次攻击的受害者，因为你用到的
DNS 服务器被污染了**。

<p align="center"><img src="/assets/img/bgp-leak-and-crypto-currencies/chrome.png" width="60%" height="60%"></p>

如果你使用的是 HTTPS，那假网站将显示一个**来自未知机构的 TLS 证书**（证书中列出
的 domain 是正确的，但证书是自签名的）。但只有你点击“接受风险并继续”之后，这种攻
击才会真正起作用。从此时起，你发送的所有内容都是加密的，但攻击者有解密的秘钥。

如果你已经登录了，那你的浏览器会发送 Cookie 中的登录信息。否则，如果你在登录页面
输入账号和密码，那它们将会被发送给攻击者。

攻击者拿到登录信息后就可以合法地登录网站，转移和盗取以太币。

# 4. 正常和被劫持状态下的路由示意图

正常情况：

<p align="center"><img src="/assets/img/bgp-leak-and-crypto-currencies/Slide1.png" width="75%" height="75%"></p>

BGP 路由泄露后：

<p align="center"><img src="/assets/img/bgp-leak-and-crypto-currencies/Slide2.png" width="75%" height="75%"></p>

# 5. 受影响区域

如前文所述，AS10279 通告了这条路由。但只有部分区域（region）收到了影响。出于成本
原因，Hurricane Electric 公司在澳大利亚建设了有很多接入点。芝
加哥受到了影响，因为 AS10279 有一条物理接入点，导致了 direct peering。

下图显示了受影响和未受影响区域的收包情况（Y 轴作了归一化）。中间的丢包是因为权威
服务器不再响应我们的请求（它只响应对自己网站的请求，所有对 Amazon domain 的请求
都被无视了）。

<p align="center"><img src="/assets/img/bgp-leak-and-crypto-currencies/traffic.png" width="75%" height="75%"></p>

eNet 使用的其他中转节点（CenturyLink, Cogent and NTT）似乎并未受到这条路由的影响
，原因可能是他们**设置了路由过滤**，并且/或者将 Amazon 作为他们的 customer（
and/or Amazon as a customer）。eNet 提供 IP 服务，因此他们的某个 customer 应该已
经通告了这条路由。

# 6. 责任在谁？

这次故障涉及到多方，很难说责任在谁。这其中包括：

* ISP 通告了一个它并未拥有（own）的子网（subnet）
* 中转供应商没有检查合法性就继续传递（relay）这个通告
* 那些接受了这条路由的 ISP
* DNS 解析器和 DNS 权威缺乏应有的保护
* 部署在俄罗斯供应商上面的钓鱼网站
* 以太坊网站并没有强制要求合法 TLS 证书
* 虽然浏览器已经提示 TLS 非法，但用户还是点击了“接受风险并继续”

和区块链（blockchain）类似，网络的每次变动通常都是可见并被记录的（visible and
archived）。RIPE 维护了[用于此目的的数据库](https://ripe.net/ris/)。

# 7. 有办法避免这种问题吗？

这个问题很难回答。业内已经有保护 BGP 安全的提案：**向 RIR 数据库添加一些额外
的字段**，生成一个**允许源列表**（a list of allowed sources）：

```shell
$ whois -h whois.radb.net ' -M 205.251.192.0/21' | egrep '^route:|^origin:|source:' | paste - - - | sort
route:      205.251.192.0/23	origin:     AS16509	source:     RADB
route:      205.251.192.0/23	origin:     AS16509	source:     REACH
```

对于一条路由路径（the path of a route），应该以 RIR 作为信任源设置 RPKI/ROA 记录（
Setting up RPKI/ROA records with the RIR as a source of truth），虽然
不是所有人都会去添加或验证这些记录。**IP 和 BGP 都是几十年前发明的东西**，在完整
性（integrity ）和贴近真实世界方面（authenticity，没考虑到路由数量会这么多）的考虑
是不能与今天同日而语的。

但在网络层之上（on the upper levels of the network stack），有一些工作可以做：

* DNS 方面，可以**使用 [DNSSEC](https://en.wikipedia.org/wiki/Domain_Name_System_Security_Extensions)对记录进行签名**。
  假的 DNS 服务器返回的 IP 是没有签名的，因为它们没有私钥。如果你使用的
  Cloudflare DNS，[在面板上点击几下](https://cloudflare.com/a/dns)就设置好了。
* 对于 HTTPS，浏览器会检查网站提供的 TLS 证书。如果启用了 [HSTS](https://en.wikipedia.org/wiki/HTTP_Strict_Transport_Security)，
  浏览器会**强制要求合法证书**。为一个 domain 生成一个合法 TLS 证书的唯一方式是：污染
  证书权威的某个 non-DNSSEC DNS 解析器的缓存（poison the cache of a non-DNSSEC
  DNS resolver of the Certificate Authority）。
* [DANE](https://en.wikipedia.org/wiki/DNS-based_Authentication_of_Named_Entities)
  提供了一种使用 DNS **将某个证书绑定（pinning）到某个 domain 的功能**。
* [DNS over HTTPS](https://developers.cloudflare.com/1.1.1.1/dns-over-https/)
  也能验证是否在与正确的解析器通信，在发生路由泄露时能区分出响应是来自 DNS 权威
  还是 DNS 解析器。

**没有完美或唯一的解决方案**。但总的来说，采取的防御措施越多，攻击者得逞的概率就越小。
