---
layout    : post
title     : "[译] 写给工程师：关于证书（certificate）和公钥基础设施（PKI）的一切（SmallStep, 2018）"
date      : 2021-10-07
lastupdate: 2021-10-07
categories: pki security
---

### 译者序

本文翻译自 2018 年的一篇英文博客：
[Everything you should know about certificates and PKI but are too afraid to ask](https://smallstep.com/blog/everything-pki/)，
作者 MIKE MALONE。

这篇长文并不是枯燥、零碎地介绍 PKI、X.509、OID 等概念，而是从前因后果、历史沿革
的角度把这些东西串联起来，逻辑非常清晰，让读者知其然，更知其所以然。

-----

证书和 PKI 的目标其实很简单：**<mark>将名字关联到公钥</mark>**（bind names to public keys）。

**<mark>加密方式的演进</mark>**：

```
 MAC         最早的验证消息是否被篡改的方式，发送消息时附带一段验证码
  |          双方共享同一密码，做哈希；最常用的哈希算法：HMAC
  |
  \/
 Signature   解决 MAC 存在的一些问题；双方不再共享同一密码，而是使用密钥对
  |
  |
  \/
 PKC         公钥加密，或称非对称加密，最常用的一种 Signature 方式
  |          公钥给别人，私钥自己留着；
  |          发送给我的消息：别人用 *我的公钥* 加密；我用我的私钥解密
  \/
 Certificate   公钥加密的基础，概念：CA/issuer/subject/relying-party/...
    |          按功能来说，分为两种
    |
    |---用于 *签名*（签发其他证书） 的证书
    |---用于 *加解密* 的证书
```

**<mark>证书（certificate）相关格式及其关系</mark>**（沉重的历史负担）：

```
  最常用的格式   |      信息比 X.509 更丰富的格式       |       其他格式

  mTLS 等常用        Java 常用            微软常用
                     .p7b .p7c          .pfx .p12

  X.509 v3            PKCS#7               PKCS#12        SSH 证书    PGP 证书     =====>  证书格式
      \                 |                    /                                           （封装格式，证书结构体）
       \                |                   /
        \               |                  /
         \              |                 /
          \-------------+----------------/
                        |
                       ASN.1 （类似于 JSON、ProtoBuf 等）                          =====>  描述格式
                        |
          /-------------+----------------\
         /              |                 \
        /               |                  \
       /                |                   \
      /                 |                    \
   DER                 PEM                                                         =====>  编码格式
二进制格式           文本格式                                                             （序列化）
  .der            .pem .crt .cer

```

一些解释：

1. X.509 **<mark>从结构上定义证书中应该包含的信息</mark>**，例如签发者、秘钥等等；
  但使用哪个格式（例如 JSON 还是 YAML 还是 ASN.1）来描述，并不属于 X.509 的内容；

1. ASN.1 是 X.509 的**<mark>描述格式</mark>**（或者说用 ASN.1 格式来定义 X.509），类似于现在的 protobuf；

    * ASN 中有很多数据类型，除了常见的整形、字符串等类型，还有一个称为 OID 的特殊类型，用点分整数表示，例如
      `2.5.4.3`，有点像 URI 或 IP 地址，在设计上是全球唯一标识符，
    * ASN.1 只是一种描述格式，并未定义如何序列化为比特流，因此又引出了 **<mark>ASN.1 的编码格式</mark>**；
      ASN.1 与其编码格式的关系，类似 **<mark>unicode 与 utf8 的关系</mark>**。

1. ASN.1 的常见**<mark>编码格式</mark>**：

    * DER：一种二进制编码格式
    * PEM：一种文本编码格式，通常以 **<mark><code>.pem</code>、<code>.crt</code> 或 <code>.cer</code></mark>** 为后缀。

1. 某些场景下，X.509 信息不够丰富，因此又设计了一些信息更丰富（例如可以包含证书
   链、秘钥）的证书封装格式，包括 PKCS #7 和 #12。

    * 仍然用 ASN.1 格式描述
    * 基本都是用 DER 编码

以上提到的东西，再加上 CA、信任仓库、信任链、certificate path validation、CSR、证书生命周期管理、
SPIFFE 等还没有提到但也与加密相关的东西，统称为**<mark>公钥基础设施（PKI）</mark>**。

-----

翻译时调整了一些配图，也加了几张新图，以方便展示和理解。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 1 前言

**<mark>证书</mark>**（certificates）与 **<mark>PKI</mark>**（public key
infrastructure，公钥基础设施）很难。我认识的很多非常聪明的人也会绕过这一主题。
我个人也很长时间没去碰这些内容，但说起来很讽刺，我没去碰的原因是不懂：
因为不懂，所以不好意思问 —— 然后更不懂，自然更不好意思问 —— 如此形成恶性循环。

但最终，我还是硬着头皮学习了这些东西。

## 1.1 为什么要学习 PKI

我觉得
**<mark>PKI 能使一个人在加解密层面（乃至更大的安全层面）去思考如何定义一个系统</mark>**。
具体来说，PKI 技术，

* 都是**<mark>通用的、厂商无关的</mark>**（universal and vendor neutral）；
* 适用于任何地方，因此即使系统可分布在世界各地，彼此之间也能安全地通信；
* 在概念上很简单，并且非常灵活；如果使用我们的 <a href="https://smallstep.com/blog/use-tls.html">TLS everywhere</a> 模型，
  那甚至连 VPN 都不需要了。

总之一句话：**<mark>非常强大！</mark>**

## 1.2 本文目的

在深入理解了 PKI 之后，我很**<mark>后悔没有早点学这些东西</mark>**。

1. PKI 非常强大且有趣，虽然它背后的数学原理很复杂，**<mark>一些相关标准也设计地非常愚蠢</mark>**
  （巴洛克式的复杂），但其 **<mark>核心概念其实非常简单</mark>**；
2. **<mark>证书是识别（identify）代码和设备的最佳方式</mark>**，
  而 identity（身份）对安全、监控、指标等很多东西都非常有用；
3. **<mark>使用证书并不是太难</mark>**，不会难于学习一门新语言或一种新数据库。

那为什么大家对这些内容望而却步呢？我认为主要是**<mark>缺少很好的文档</mark>**，所以经常看地云里雾里，半途而弃。

本文试图弥补这一缺失。我认为大部分工程师花一个小时读完本文后，都将了解到
**<mark>关于加解密的那些最重要概念和使用场景</mark>** —— 这正是本文的目的 ——
一小时只是很小的一个投资，而且这些内容是无法通过其他途径学到的。

本文将用到以下两个开源工具：

* <a href="https://smallstep.com/cli">step CLI</a>
* <a href="https://smallstep.com/certificates">step certificates</a>

## 1.3 极简 TL; DR（太长不读）

证书和 PKI 的目的：**<mark>将名字关联到公钥（bind names to public keys）</mark>**。

这是**<mark>关于证书和 PKI 的最高抽象</mark>**，其他都属于实现细节。

# 2 术语

本文将用到以下术语。

## 2.1 Entity（实体）

Entity 是**<mark>任何存在的东西</mark>**（anything that exists） —— 即使
**<mark>只在逻辑或概念上存在</mark>**（even if only exists logically or conceptually）。
例如，

* 你用的计算机是一个 entity，
* 你写的代码也是一个 entity，
* 你自己也是一个 entity，
* 你早餐吃的杂粮饼也是一个 entity，
* 你六岁时见过的**<mark>鬼</mark>**也是一个 entity —— 即使你妈妈告诉你世界上并没有鬼，这只是你的臆想。

## 2.2 Identity（身份）

**<mark>每个 entity（实体）都有一个 identity（身份）</mark>**。
要精确定义这个概念比较困难，这么来说吧：identity 是**<mark>使你之所以为你</mark>**
（what makes you you）的东西，懂吗？

具体到计算机领域，identity 通常**<mark>用一系列属性来表示，描述某个具体的 entity</mark>**，
这里的属性包括 group、age、location、favorite color、shoe size 等等。

## 2.3 Identifier（身份标识符）

Identifier 跟 identity 还不是一个东西：每个 identifier 都是一个**<mark>唯一标识符</mark>**，
也**<mark>唯一地关联到某个有 identity 的 entity</mark>**。

例如，我是 Mike，但 Mike 并不是我的 identity，而只是个 name —— 虽然二者在我们
小范围的讨论中是同义的。

## 2.4 Claim（声明） & Authentication（认证）

* 一个 entity 能 **<mark>claim</mark>**（声明）说，它**<mark>拥有某个或某些 name</mark>**。
* 其他 entity 能够对这个 claim 进行**<mark>认证</mark>**（authenticate），以确认这份声明的真假。

    一般来说，**<mark>认证的目的是确认某些 claim 的合法性</mark>**。

* Claim 不是只能关联到 name，还可以关联到别的东西。例如，我能 claim 任何东西：
  my age, your age, access rights, the meaning of life 等等。

## 2.5 Subscriber & CA & relying party (RP)

* **<mark>能作为一个证书的 subject 的 entity</mark>**，称为 **<mark>subscriber</mark>**（证书 owner）或 **<mark>end entity</mark>**。

    对应地，subscriber 的证书有时也称为 end entity certificates 或 leaf certificates，
    原因在后面讨论 certificate chains 时会介绍。

* CA（certificate authority，证书权威）是**<mark>给 subscriber 颁发证书的 entity</mark>**，是一种 certificate issuer（证书颁发者）。

    CA 的证书，通常称为 **<mark>root certificate</mark>** 或 intermediate certificate，具体取决于 CA 类型。

* Relying party 是 **<mark>使用证书的用户</mark>**（certificate user），它验证由 CA 颁发（给 subscriber）的证书是否合法。

    一个 entity 可以同时是一个 subscriber 和一个 relying party。
    也就是说，单个 entity 既有自己的证书，又使用其他证书来认证 remote peers，
    例如双向 TLS（mutual TLS，**<mark>mTLS</mark>**）场景。

## 2.6 小结

对于我们接下来的讨论，这些术语就够了。下面将进入正题，看如何在实际中实现
**<mark>证书的声明和认证</mark>**。

> 想了解更多相关术语，可参考 <a href="https://tools.ietf.org/html/rfc4949">RFC 4949</a>。

# 3 MAC（消息认证码）和 signature（签名）

## 3.1 MAC（message authentication code）和 HMAC（hash-based MAC）

MAC（消息认证码）是一**<mark>小段数据</mark>**，用于验证某个 entity 发送的消息**<mark>未被篡改</mark>**。
其基本原理如下图所示：

<p align="center"><img src="/assets/img/everything-about-pki/HMAC.png" width="85%" height="85%"></p>
<p align="center">MAC/HMAC 原理。图片来自：<a href="https://www.okta.com/identity-101/hmac/">okta.com</a></p>

1. 对**<mark>消息</mark>**（message）和双方都知道的一个**<mark>密码</mark>**
（shared secret，a password）做哈希，**<mark>得到的哈希值</mark>**就是 MAC；
2. 发送方将消息连带 MAC 一起发给接收方；
3. 接收方收到消息之后，用同一个密码来计算 MAC，然后跟消息中提供的 MAC 对比。如果相同，就证明未被篡改。

关于哈希：

* 哈希是单向的，因此无法从输出反推输入；这一点至关重要，否则截获消息的人就可以根据 MAC 和哈希函数反推 secrets。
* 生成 MAC 的**<mark>哈希算法选择也至关重要</mark>**，本文不会展开，但提醒一点：不要试图用自己设计的 MAC 算法。
* 最常用的 MAC 算法是 <a href="https://en.wikipedia.org/wiki/HMAC">HMAC</a>（**<mark>hash-based message authentication code</mark>**）。

## 3.2 Signature（签名）与不可否认性

讨论 MAC 其实是为了**<mark>引出 signature（签名）</mark>**这一主题。

**<mark>签名在概念上与 MAC 类似</mark>**，但**<mark>不是用共享 secret 的方式</mark>**，
而是使用一对秘钥（key pair）：

* MAC 方式中，至少有两个 entity 需要知道共享的 secret，也就是消息的发送方和接
  收方。**<mark>双方都可以生成 MAC</mark>**，因此给定一个合法的 MAC，我们是
  **<mark>无法知道是谁生成的</mark>**。

* 签名就不同了：签名能用**<mark>公钥（public key）验证</mark>**，但只能用相应的
  **<mark>私钥（private key）生成</mark>**。
  因此对于接收方来说，它只能验证签名是否合法，而无法生成同样的签名。

如果只有一个 entity 知道秘钥，那这种特性称为 **<mark>non-repudiation</mark>**
（不可否认性）：持有私钥的人无法否认（repudiate）数据是由他签名的这一事实。

## 3.3 小结

MAC 与 signature 都叫做签名，是因为它们和现实世界中的签名是很像的。例如，如果想
让某人同意某事，并且事后还能证明他们当时的确同意了，就把问题写下来，然后让他们
手写签字（签名）。

# 4 Public key cryptography（公钥加密，或称非对称加密）

证书和 PKI 的基础是**<mark>公钥加密</mark>**（public key cryptography），
也叫**<mark>非对称加密</mark>**（asymmetric cryptography）。

## 4.1 秘钥对

公钥加密系统使用秘钥对（key pair）加解密。一个秘钥对包含：

1. 一个私钥（private key）：owner 持有，**<mark>解密用，不要分享给任何人</mark>**；

    这一点非常重要，值得重复一遍：**<mark>公钥加密系统的安全性取决于私钥（private key）的机密性</mark>**。

1. 一个公钥（public key）：**<mark>加密用</mark>**，可分发和共享给别人；

秘钥可以做的事情：

1. 加解密：公钥（public key）加密，私钥（private key）解密。
1. **<mark>签名</mark>**：**<mark>私钥（private key）对数据进行签名</mark>**（sign some data）；
   任何有公钥的人都可以**<mark>对签名进行验证</mark>**，证明这个签名确实是私钥生成的。

## 4.2 公钥加密系统使计算机能“看到”对方

公钥加密是数学给计算机科学的神秘礼物，
<a href="https://www.math.auckland.ac.nz/~sgal018/crypto-book/crypto-book.html">其数学基础</a>
显然很复杂，但如果只是使用，那并不理解它的每一步数学原理。
公钥加密使计算机能做一些之前无法做的事情：它们现在能看到对方是谁了。

这句话的意思是说，公钥加密使一台计算（或代码）能向其他计算机或程序证明，
**<mark>不用直接分享某些信息，它也能知道该信息</mark>**。更具体来说，

* 以前要证明你有密码，就必须向别人展示这个密码。但展示之后，任何有这个密码的人就都能使用它了。
* 私钥却与此不同。你能通过私钥对我的身份进行认证（authenticate my identity），但却无法假冒我。

    例如，你发给我一个大随机数，我对这个随机数进行签名，然后将再发送给你。
    你能用公钥对这个签名进行认证，确认这个签名（消息）确实来自我。
    这就是一种证明你在和我（而不是别的其他的人）通信的很好证据。这使得网络上的
    计算机能有效地知道它们在和谁通信。

    这听起来是一件如此理所当然的事情，但仔细地想一下，网络上只有流动的 0 和 1，
    你怎么知道消息来自谁，在和谁通信？因此公钥加密系统是一个非常伟大的发明。

# 5 证书（certificate）：计算机和代码的驾驶证

前面说道，公钥加密系统使我们能知道和谁在通信，但这个的前提是：
**<mark>要知道（有）对方的公钥</mark>**。

那么，如果**<mark>对方不知道我的公钥怎么办</mark>**？
这就轮到证书出场了。

想一下，我们需求其实非常简单：

* 首先要将**<mark>公钥和它的 owner</mark>** 信息发给对方；
* 但光有这个信息还不行，**<mark>还要让对方相信这些信息</mark>**；

    证书就是用来解决这个问题的，解决方式是**<mark>请一个双方都信任的权威机构</mark>**
    对以上信息作出证明（签名）。

## 5.1 证书的内容：（subscriber 的）公钥+名字

* 证书是一个数据结构，其中包含一个 public key 和一个 name；
* **<mark>权威机构</mark>**对证书进行**<mark>签名</mark>**，签名的大概意思是：public key xxx 关联到了 name xx；

    对证书进行签名的 entity 称为 **<mark>issuer</mark>**（或 certificate authority, CA），
   证书中的 entity 称为 **<mark>subject</mark>**。

举个例子，如果某个 Issuer 为 Bob 签发了一张证书，其中的内容就可以解读如下：

> *Some Issuer* says *Bob*'s public key is 01:23:42...

<p align="center"><img src="/assets/img/everything-about-pki/drivers-license-cert.jpg" width="50%" height="50%"></p>
<p align="center">证书是权威机构颁发的身份证明，<mark>并没有什么神奇之处</mark></p>

其中 `Some Issuer` 是证书的签发者（证书权威），证书是为了证明这是 `Bob` 的公钥，
`Some Issuer` 也是这个声明的签字方。

## 5.2 证书的本质：基于对 issuer 公钥的信任来学习其他公钥

由上可知，如果知道 *Some Issuer* 的公钥，就可以通过**<mark>验证签名</mark>**的方式来
**<mark>对它（用私钥）签发的证书</mark>**进行认证（authenticate）。
如果如果你信任 *Some Issuer*，那你就可以信任这个声明。

因此，证书使大家能**<mark>基于对 issuer 公钥的信任和知识，来学习到其他 entity 的公钥</mark>**
（上面的例子中就是 Bob）。这就是证书的本质。

## 5.3 与驾照的类比

证书就像是计算机/代码的驾照或护照。如果你之前从未见过我，但信任车管局，那你可以
用我的驾照做认证：

* 首先验证驾照是真的（检查 hologram 等），
* 然后人脸和照片上对的上，
* 然后看名字是我，等等。

<p align="center"><img src="/assets/img/everything-about-pki/license-vs-cert.jpg" width="80%" height="80%"></p>

计算机用**<mark>证书做类似的事情</mark>**：如果之前从未和其他电脑通信，但信任
一些证书权威，那可以用证书来认证：

* 首先验证证书是合法的（用证书签发者的公钥检查签名等），
* 然后提取证书中的（subscriber 的）公钥和名字，
* 然后用 subscriber 的公钥，通过网络验证该 subscriber 的签名；
* 查看名字是否正确等等。

## 5.4 证书内容解析举例

下面是个真实的证书：

<p align="center"><img src="/assets/img/everything-about-pki/step-certificate-inspect.png" width="80%" height="80%"></p>

还是与驾照类比：

* 驾照：描述了你是否有资格开车；
* 证书：描述你是否是一个 CA，你的公钥能否用来签名或加密。
* 二者都有有效期。

上图中有大量的细节，很多东西将在下面讨论到。但归根结底还是本文最开始总结的那句话
：**<mark>证书不过是一个将名字关联到公钥（bind names to public keys）的东西</mark>**。
其他都是实现细节。

# 6 证书编码格式及历史演进

接下来看一看证书在底层的表示（represented as bits and bytes）。

这部分内容**<mark>复杂且相当令人沮丧</mark>**。事实上，我怀疑**<mark>证书和秘钥诡异的编码方式</mark>**
是导致 PKI 如此混乱和令人沮丧的根源。

## 6.1 X.509 证书

一般来说，人们**<mark>提到“证书”而没有加额外限定词时</mark>**，指的都是 X.509 v3
证书。

* 更准确地说，他们指的是
  <a href="https://tools.ietf.org/html/rfc5280">RFC 5280</a> 中描述、
  CA/Browser Forum <a href="https://cabforum.org/baseline-requirements-documents/">Baseline Requirements</a>中进一步完善的
  PKIX 变种。
* 换句话说，指的是**<mark>浏览器理解并用来做 HTTPS（HTTP over TLS）的那些证书</mark>**。

也有其他的证书格式，例如著名的 **<mark>SSH 和 PGP 都有它们各自的格式</mark>**。

本文主要关注 X.509，理解了 X.509，其他格式都是类似的。
由于这些证书使用广泛，因此有很好的函数库，而且也用在浏览器之外的场景。毫无疑问，它们是
internal PKI 颁发的最常见证书格式。重要的是，这些证书在很多 TLS/HTTPS 客户端/服
务端程序中都是开箱即用的。

### X.509 起源：电信领域

了解一点 X.509 的历史对理解它会有很大帮助。

X.509 在 **<mark>1988</mark>** 年作为国际电信联盟（ITU）X.500 项目的一部分首次标准化。
这是通信（telecom）领域的标准，想通过它构建一个**<mark>全球电话簿</mark>**（global telephone book）。
虽然这个项目没有成功，但却留下了一些遗产，X.509 就是其中之一。

如果查看 X.509 的证书，会看到其中包含了 locality、state、country 等信息，
之前可能会有疑问为什么为 web 设计的证书会有这些东西，现在应该明白了，因为
**<mark>X.509 并不是为 web 设计的</mark>**。

<p align="center"><img src="/assets/img/everything-about-pki/cert-dn.png" width="80%" height="80%"></p>

## 6.2 ASN.1：数据抽象格式

X.509 构建在 ASN.1 （Abstract Syntax Notation，抽象语法标注）之上，后者是另一个
ITU-T 标准 (X.208 and X.680)。

**<mark>ASN.1 定义数据类型</mark>**，

* 可以将 ASN.1 理解成 **<mark>X.509 的 JSON</mark>**，
* 但实际上**<mark>更像 protobuf</mark>**、thrift 或 SQL DDL。

RFC 5280 **<mark>用 ASN.1 来定义 X.509 证书</mark>**，其中包括名字、秘钥、签名等信息。

## 6.3 OID (object identitfier)

ASN.1 除了有常见的数据类型，如整形、字符串、集合、列表等，
还有一个**<mark>不常见但很重要的类型：OID</mark>**（object identifier，**<mark>对象标识符</mark>**）。

* OID **<mark>与 URI 有些像</mark>**，但比 URI 要怪。
* OID （在设计上）是**<mark>全球唯一标识符</mark>**。
* 在结构上，OID 是在一个 hierarchical namespace 中的一个整数序列（例如 `2.5.4.3`）。

可以用 OID 来 tag 一段数据的类型。例如，一个 string 本来只是一个 string，但可
以 tag 一个 OID `2.5.4.3`，然后就**<mark>变成了一个特殊 string</mark>**：这是
**<mark>X.509 的通用名字（common name）</mark>** 字段。

<p align="center"><img src="/assets/img/everything-about-pki/oids.png" width="80%" height="80%"></p>

## 6.4 ASN.1 编码格式

ASN.1 只是**<mark>抽象</mark>**（abstract），因为这个标准并未定义在数据层应该如何表示（represented as bits and bytes）。
ASN.1 与其编码格式的关系，就像 **<mark>unicode 与 utf8 的区别</mark>**。
因此，有很多种**<mark>编码规则</mark>**（encoding rules），描述具体如何表示 ASN.1 数据。
原以为增加这层额外的抽象会有所帮助，但实际证明大部分情况下反而**<mark>徒增烦恼</mark>**。

### DER (distinguished encoding rules)：二进制格式

ASN.1 有<a href="https://en.wikipedia.org/wiki/Abstract_Syntax_Notation_One#Encodings">很多种编码规则</a>，
但用于 X.509 和其他加密相关的，**<mark>只有一种</mark>**常见格式：DER —— 虽然有时也会用到 non-canonical
的 basic encoding rules (BER，基础编码规则) 。

DER 是**<mark>非常简单的 TLV</mark>**（type-length-value）编码，但实际上用户无需
关心这些，因为函数库封装好了。但不要高兴得太早 —— 虽然我们不必关心 DER 的编解码，
但要能判断给定的某个 X.509 证书是 DER 还是其他类型编码的。这里的其他类型包括：

1. 一些比 DER 更友好的格式，
2. 封装了证书及其他额外信息的格式（something more than just a certificate）。

DER 编码的证书通常以 **<mark><code>.der</code></mark>** 为后缀。

### PEM (privacy enhanced email)：文本格式

**<mark>DER 是二进制格式，不便复制粘贴</mark>**。因此**<mark>大部分证书都是以</mark>**
[PEM](https://en.wikipedia.org/wiki/Privacy-Enhanced_Mail) 格式打包的，这是
**<mark>另一个历史怪胎</mark>**。

如果你熟悉 [MIME](https://en.wikipedia.org/wiki/MIME) 的话，二者是比较类似的：
由 header、base64 编码的 payload、footer 三部分组成。
header 中有标签（label）来描述 payload。例如下面是一个 **<mark>PEM 编码的 X.509 证书</mark>**：

```
-----BEGIN CERTIFICATE-----
MIIBwzCCAWqgAwIBAgIRAIi5QRl9kz1wb+SUP20gB1kwCgYIKoZIzj0EAwIwGzEZ
MBcGA1UEAxMQTDVkIFRlc3QgUm9vdCBDQTAeFw0xODExMDYyMjA0MDNaFw0yODEx
BgNVHRMBAf8ECDAGAQH/AgEAMB0GA1UdDgQWBBRc+LHppFk8sflIpm/XKpbNMwx3
SDAfBgNVHSMEGDAWgBTirEpzC7/gexnnz7ozjWKd71lz5DAKBggqhkjOPQQDAgNH
ADBEAiAejDEfua7dud78lxWe9eYxYcM93mlUMFIzbWlOJzg+rgIgcdtU9wIKmn5q
FU3iOiRP5VyLNmrsQD3/ItjUN1f1ouY=
-----END CERTIFICATE-----
```

但令人震惊的时，即便如此简单的功能，在实现上也已经出现混乱：PEM labels 在不同工具之间是不一致的。
[RFC 7468](https://tools.ietf.org/html/rfc7468) 试图标准化 PEM 的使用规范，
但也并不完整，不是所有工具都遵循这个规范。

PEM 编码的证书通常以 **<mark><code>.pem</code>、<code>.crt</code> 或 <code>.cer</code></mark>** 为后缀。
再次提醒，这只是“通常”情况，实际上某些工具可能并不遵循这些惯例。

下面介绍几个前面提到的“其他类型的打包格式”。

## 6.5 比 X.509 信息更丰富的证书打包（封装）格式

X.509 只是一种常用的证书格式，但有人觉得这种格式能装的信息不够多，因此
又定义了一些比 X.509 **<mark>更大的数据结构</mark>**（但**<mark>仍然用 ASN.1</mark>**），
能将证书、秘钥以及其他东西封装（打包）到一起。因此，有时说我需要“一个证书”时，其
实真正说的是包（package）中包含的那个“证书”（a certificate in one of these
envelopes），而不是这个包本身。

### PKCS #7：Java 中常用

你可能会遇到的是一个称为 PKCS（Public Key Cryptography Standards，**<mark>公钥加密标准</mark>**）的标准的一部分，
它由 RSA labs 发布（真实历史要 [更加复杂一些](https://security.stackexchange.com/questions/73156/whats-the-difference-between-x-509-and-pkcs7-certificate)，本文不展开）。

其中的第一个标准是 <a href="https://tools.ietf.org/html/rfc2315">PKCS#7</a>，后面被
IETF 重新冠名为 <a href="https://tools.ietf.org/html/rfc5652">Cryptographic Message Syntax</a> (CMS)
，其中可以包含多个证书（以 full certificate chain 方式编码，后面会看到）。

PKCS#7 **<mark>在 Java 中使用广泛</mark>**。常见扩展名是 `.p7b` and `.p7c`。

### PKCS #12：微软常用

另一个常见的打包格式 <a href=https://tools.ietf.org/html/rfc7292>PKCS#12</a>，
它能将一个**<mark>证书链</mark>**（这一点与 PKCS#7 类似）连同一个（加密之后的）**<mark>私钥</mark>**打包到一起。

**<mark>微软的产品多用这种格式</mark>**，常见后缀`.pfx` and `.p12`。

再次说明，PKCS#7 和 PKCS#12 envelopes **<mark>仍然使用 ASN.1</mark>**，这意味着
它们都能以原始 DER、BER 或 PEM 的格式编码。
从我个人的经验来看，二者**<mark>几乎都是 DER 编码的</mark>**。

## 6.6 秘钥编解码

秘钥编码（Key encoding）的过程与以上描述的类似（复杂）：

* 用某种 ASN.1 数据结构描述秘钥（key）；
* 用 DER 做二进制编码，或用 PEM (hopefully with a useful header) 做一些稍微友好一些的表示。

**<mark>秘钥的解密过程（deciphering），一半是是科学，一半是艺术</mark>**。

如果足够幸运，根据 <a href="https://tools.ietf.org/html/rfc7468">RFC 7468</a> 就能找到其中的 PEM payload；

1. 椭圆曲线秘钥通常符合 RFC 7468 规范，虽然
  <a href="https://tools.ietf.org/html/rfc5915#section-4">这里看起来似乎也并没有什么标准</a>。

    下面是一个 **<mark>PEM 编码的椭圆曲线秘钥</mark>**（PEM-encoded elliptic curve key）：

    ```shell
    $ step crypto keypair --kty EC --no-password --insecure ec.pub ec.prv

    $ cat ec.pub ec.prv
    -----BEGIN PUBLIC KEY-----
    MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEc73/+JOESKlqWlhf0UzcRjEe7inF
    uu2z1DWxr+2YRLfTaJOm9huerJCh71z5lugg+QVLZBedKGEff5jgTssXHg==
    -----END PUBLIC KEY-----
    -----BEGIN EC PRIVATE KEY-----
    MHcCAQEEICjpa3i7ICHSIqZPZfkJpcRim/EAmUtMFGJg6QjkMqDMoAoGCCqGSM49
    AwEHoUQDQgAEc73/+JOESKlqWlhf0UzcRjEe7inFuu2z1DWxr+2YRLfTaJOm9hue
    rJCh71z5lugg+QVLZBedKGEff5jgTssXHg==
    -----END EC PRIVATE KEY-----
    ```

2. 其他秘钥，通常用 PEM label "PRIVATE KEY" 描述

### PEM 编码的 PKCS#8 格式私钥

PEM label "PRIVATE KEY" 描述的秘钥，通常暗示这是一个
<a href="https://tools.ietf.org/html/rfc5208">PKCS#8</a> payload，
这是一种私钥（private key）封装格式，其中包含秘钥类型和其他 metadata。

### 密码加密的私钥

**<mark>用密码来加密私钥</mark>**也很常见（private keys encrypted using a
password），这里的密码可以是 a shared secret or symmetric key。
看起来大致如下（`Proc-Type` and `DEK-Info` 是 PEM 的一部分，表示这个 PEM 的 payload 是用 `AES-256-CBC` 加密的）：

```
-----BEGIN EC PRIVATE KEY-----
Proc-Type: 4,ENCRYPTED
DEK-Info: AES-256-CBC,b3fd6578bf18d12a76c98bda947c4ac9

qdV5u+wrywkbO0Ai8VUuwZO1cqhwsNaDQwTiYUwohvot7Vw851rW/43poPhH07So
sdLFVCKPd9v6F9n2dkdWCeeFlI4hfx+EwzXLuaRWg6aoYOj7ucJdkofyRyd4pEt+
Mj60xqLkaRtphh9HWKgaHsdBki68LQbObLOz4c6SyxI=
-----END EC PRIVATE KEY-----
```

PKCS#8 对象也能被加密，这种情况下 header label 应该是 **<mark>"ENCRYPTED PRIVATE KEY"</mark>** per RFC 7468。
这种情况下不会看到 `Proc-Type` 和 `Dek-Info` headers，因为这些信息此时编码到了 payload 中。

### 公钥、私钥常见扩展名

* 公钥：`.pub` or `.pem`，
* 私钥：`.prv,` `.key`, or `.pem`。

但再次说明，有些工具或组织可能并不遵循业界惯例。

## 6.7 小结

* **<mark>ASN.1 用于定义数据类型</mark>**，例如证书（certificate）和秘钥（key）——
  就像用 JSON 定义一个 request body —— X.509 用 ASN.1  定义。
* DER 是一组将 ASN.1 编码成二进制（比特和字节）的编码规则（encoding rules）。
* PKCS#7 and PKCS#12 是比 X.509 更大的数据结构（封装格式），也用 ASN.1 定义，其
  中能包含除了证书之外的其他东西。二者分别在 Java 和 Microsoft 产品中使用较多。
* DER 编码之后是二进制数据，不方便复制粘贴，因此大部分证书都是用 PEM 编码的，它
  用 base64 对 DER 进行编码，然后再加上自己的 label。
* 私钥通常用是 PEM 编码的 PKCS#8 对象，但有时也会用密码来加密。

如果觉得以上内容理解起来很杂乱，那并不是你的问题，而是加密领域的现状就是如此。我已经尽力了。

# 7 PKI (Public Key Infrastructure)

至此我们已经知道了证书的来历和样子，但这仅仅是本文的一半。
下面看**<mark>证书是如何创建和使用</mark>**的。

<mark>Public key infrastructure</mark> (PKI) 是一个统称，包括了我们在
如下与证书和秘钥管理及交互操作时需要用到的所有东西：签发、分发、存放、使用、验证、撤回等等。
就像“数据库基础设施” 一样，这个名词是有意取的这样模糊的。

* **<mark>证书是大部分 PKI 的构建模块</mark>**，而证书权威是其基础。
* PKI 包括了 libraries, cron jobs, protocols, conventions, clients, servers,
  people, processes, names, discovery mechanisms, and all the other stuff
  you'll need to use public key cryptography effectively。

自己从头开始构建一个 PKI 是一件极其庞大的工作，
但实际上 **<mark>一些简单的 PKI 甚至并不使用证书</mark>**。例如，

* 编辑 **<mark><code>~/.ssh/authorized_keys</code></mark>** 文件时，就是在配置
  一个简单的无证书形式的（certificate-less）PKI，SSH 通过这种方式在扁平文件内
  **<mark>实现 public key 和 name 的绑定</mark>**；
* PGP 用证书，但不用 CA，而是用一个 <a href="https://en.wikipedia.org/wiki/Web_of_trust">web-of-trust</a> model；
* 甚至可以 <a href="http://www.aaronsw.com/weblog/squarezooko">用区块链</a> 来 assign name 并将它们 bind 到 public key。

如果从头开始构建一个 PKI，**<mark>唯一确定的事情是：你需要用到公钥</mark>**（public keys），
其他东西都随设计而异。

下文将**<mark>主要关注 web 领域使用的 PKI</mark>**，以及基于 Web PKI 技术、遵循现有标准的 internal PKI。

证书和 PKI 的目标其实很简单：**<mark>将名字关联到公钥</mark>**（bind names to public keys）。
在下面的内容中，不要忘了这一点。

## 7.1 Web PKI vs Internal PKI

**<mark>浏览器访问 HTTPS 链接时</mark>**会用到 Web PKI。虽然也有一些问题，但它大大提升了 web
的安全性，而且基本上对用户透明。在访问互联网 web 服务时，应该在所有可能的情
况下都启用它。

* Web PKI 由 <a href="https://tools.ietf.org/html/rfc5280">RFC 5280</a> 定义，
  <a href="https://cabforum.org/">CA/Browser Forum</a> (a.k.a., CA/B or CAB Forum) 对其进行了进一步完善。
* 有时也称为 "Internet PKI" 或 **<mark>PKIX</mark>** (after the working group that created it).

PKIX 和 CAB Forum 文档涵盖了很大内容。
它们定义了前面讨论的各种证书、还定义什么是 “name” 以及位于证书中什么位置、能使用什么签名算法、
RP 如何判断 issuer 的证书、如何指定证书的 validity period (issue and expiry dates)、
撤回、certificate path validation、CA 判断某人是否拥有一个域名等等。

Web PKI 很重要，是因为浏览器默认使用 Web PKI 证书。

<mark>Internal PKI</mark> 是**<mark>用户为自己的产品基础设施使用的 PKI</mark>**，这些产品包括

* 服务、容器、虚拟机等；
* 企业 IT 应用；
* 公司终端设备，例如笔记本电脑、手机等；
* 其他需要识别的代码或设备。

Internal PKI 使你能认证和建立加密通道，这样你的服务就可以安全地在公网上的任意位置互相通信了。

## 7.2 有了 Web PKI，为什么还要使用自己的 internal PKI？

首先，简单来说：**<mark>Web PKI 设计中并没有考虑内部使用场景</mark>**。
即使有了 <a href="https://letsencrypt.org/">Let's Encrypt</a> 这样的提供免费证书和自动化交付的 CA，
用户还是需要自己处理 <a href="https://letsencrypt.org/docs/rate-limits/">rate limits</a> 和
<a href="https://statusgator.com/services/lets-encrypt">availability</a> 之类的事情。
如果有很多 service，部署很频繁，就非常不方便。

另外，Web PKI 中，用户对证书生命周期、撤回机制、续约过程、秘钥类型、算法等等很
多重要的细节都没有控制权，或只有很少控制权。而下文将会看到，这些都是非常重要的东西。

最后，CA/Browser Forum Baseline Requirements
实际上**<mark>禁止将 Web PKI CA 关联到 internal IPs</mark>** (e.g., `10.0.0.0/8`)
及 **<mark>internal DNS names</mark>** that aren't fully-qualified and
resolvable in public global DNS (e.g., you can't bind a kubernetes cluster DNS
name like `foo.ns.svc.cluster.local`)。
如果需要在证书中绑定到这些 name，或者签发大量证书，或者控制证书细节，就需要自己的 internal PKI.

下面一节将看到，信任（或缺乏信任）是避免将 Web PKI 用于内部场景的另一个原因。

总结起来，建议：

* **<mark>面向公网的服务或 API</mark>**，使用 Web PKI；
* 其他所有场景，都使用 internal PKI。

# 8 Trust & Trustworthiness

## 8.1 Trust Stores（信任仓库）

前面介绍到，证书可解读为一个 statement 或 claim，例如：

> Issuer（签发者）说，该 subject 的公钥是 xxx。

Issuer 会对这份声明进行签名，relying party 能（通过 issuer 的公钥）验证（authenticate）签名是否合法。
但这里其实跳过了一个重要问题：**<mark>relying party 是如何知道 issuer 的公钥的</mark>**？

### 预配置信任的根证书

答案其实很简单：relying parties 在自己的 trust store（信任库）预先配置了一个它
**<mark>信任的根证书</mark>**（trusted root certificates，也称为 trust anchors）列表，

**<mark>预配置的具体方式</mark>**（the manner in which this pre-configuration occurs），
是 PKI 非常重要的一面：

* 一种方式是从另一个 PKI 来 bootstrap：可以用一些自动化工具，通过 SSH 将 root 证
  书拷贝到 relying party。这里用到里前面提到的 SSH PKI。
* 如果是在 cloud 上，那 PKI 依赖层次（信任链）又深了一步：SSH PKI 是由 Web PKI 加上认证方式
  来 bootstrap 的，这里的认证是你创建 cloud 账户时选择的认证方式。

### 信任链

如果沿着这个信任链（chain of trust）回溯足够远，最后总能找到人（people）：每个
信任链都终结在现实世界（meatspace）。

<p align="center"><img src="/assets/img/everything-about-pki/chain-of-trust.jpg" width="90%" height="90%"></p>

下面这个图画地更清楚一些，

<p align="center"><img src="/assets/img/everything-about-pki/trust-chain.png" width="70%" height="70%"></p>
<p align="center">Image credit: <a href="https://brainbit.io/posts/cilium-tls-inspection/">Cilium TLS inspection</a></p>

### 根证书自签名

信任仓库中的**<mark>根证书是自签名的（self-signed）</mark>**：issuer 和 subject
相同。逻辑上，这种 statement 表示的是：

> Mike 说：Mike 的公钥是 xxx。

自签名的证书保证了该证书的 subject/issuer 知道对应的私钥，
但任何人都可以生成一个自签名的证书，这个证书中可以写任何他们想写的名字（name）。

因此**<mark>证书的起源（provenance）就非常关键</mark>**：一个自签名的证书，只有
当它**<mark>进入信任仓库的过程是可信任时</mark>**，才应该信任这个根证书。

* 在 macOS 上，信任仓库是由 Keychain 管理的。
* 在一些 Linux 发行版上，可能只是 `/etc` 或其他路径下面的一些文件。
* 如果你的用户能修改这些文件，那最好先确认是你信任这些用户的。

### 信任仓库的来源

所以，信任仓库又从哪里来？对于 Web PKI 来说，最重要的
relying parties 就是浏览器。**<mark>主流浏览器默认使用的信任仓库</mark>** ——
及其他任何使用 TLS 的东西 —— 都是**<mark>由四个组织维护</mark>**的：

1. <a href="http://www.apple.com/certificateauthority/ca_program.html">Apple's root certificate</a>：iOS/macOS 程序
1. <a href="https://social.technet.microsoft.com/wiki/contents/articles/31633.microsoft-trusted-root-program-requirements.aspx">Microsoft's root certificate program</a>：Windows 使用
1. <a href="https://www.mozilla.org/en-US/about/governance/policies/security-group/certs/">Mozilla's root certificate program</a>：
  Mozilla 产品使用，由于其开放和透明，也作为其他一些信任仓库从基础 (e.g., for many Linux distributions)
1. Google <a href="https://www.chromium.org/Home/chromium-security/root-ca-policy">未维护 root certificate program</a>
  （Chrome 通常使用所在计算的操作系统的信任仓库），但
   <a href="https://chromium.googlesource.com/chromium/src/+/master/net/data/ssl/blacklist/README.md">维护了自己的黑名单</a>，
   列出了自己不信任的根证书或特定证书。
   (<a href="https://chromium.googlesource.com/chromiumos/docs/+/master/ca_certs.md">ChromeOS builds off of Mozilla's certificate program</a>)

### 操作系统的信任仓库

操作系统中的信任仓库通常都是**<mark>系统自带</mark>**的。

* Firefox 自带了自己的信任仓库（通过 TLS 从 mozilla.org 分发 ——
  bootstrapping off of Web PKI using some other trust store）。
* 编程语言和其他非浏览器的东西例如 `curl`，通过默认用操作系统的信任仓库。

因此，这个信任仓库通常情况下，会被该系统上预装的很多东西默认使用；通过软件更新（
通常使用另一个 PKI 来签名）而更新。

信任仓库中通常包含了超过 100 个由这些程序维护的**<mark>常见证书权威</mark>**（certificate authorities）。
其中一些著名的：

* Let's Encrypt
* Symantec
* DigiCert
* Entrust

如果想编程控制：

* Cloudflare's
  <a href="https://github.com/cloudflare/cfssl">cfssl</a> project maintains a
  <a href="https://github.com/cloudflare/cfssl_trust">github repository</a> that
  includes the trusted certificates from various trust stores to assist with
  certificate bundling (which we'll discuss momentarily).
* For a more human-friendly experience you can query <a href="https://censys.io/">Censys</a>
  to see which certificates are trusted by <a href="https://censys.io/certificates?q=validation.nss.valid%3A+true+AND+parsed.extensions.basic_constraints.is_ca%3A+true">Mozilla</a>,
  <a href="https://censys.io/certificates?q=validation.apple.valid%3A+true+AND+parsed.extensions.basic_constraints.is_ca%3A+true">Apple</a>,
  and <a href="https://censys.io/certificates?q=validation.microsoft.valid%3A+true+AND+parsed.extensions.basic_constraints.is_ca%3A+true">Microsoft</a>.


## 8.2 Trustworthiness（可靠性）

这 100 多个证书权威在理论上是**<mark>可信的</mark>**（trusted） —— 浏览器和其他
一些软件默认情况下信任由这些权威颁发的证书。

但是，这并不意味着它们是**<mark>可靠的</mark>**（trustworthy）。
已经出现过 Web PKI 证书权威向政府机构**<mark>提供假证书</mark>**的事故，以便
窥探流量（snoop on traffic）或仿冒某些网站。
这类“受信任的” CA 中，其中在司法管辖权之外的地方运营 —— 包括民主国家和专制国家。

* NSA 利用每个可能的机会来削弱 Web PKI。2011 年，两个“受信任的”证书权威
  DigiNotar and Comodo  <a href="https://en.wikipedia.org/wiki/DigiNotar">都</a>
  <a href="https://en.wikipedia.org/wiki/Comodo_Group#Certificate_hacking">被攻陷了</a>。
  DigiNotar 证书泄露可能与 NSA 相关。
* 此外，还有大量 CA 签发格式不对或不兼容的证书。因此，虽然按业界规范来说
  这些 CA 是受信的，但**<mark>按照经验来说它们是不可靠（不靠谱）的</mark>**。

我们很快就会看到，**<mark>Web PKI 的安全性取决于安全性最弱的权威</mark>**（the least secure CA）的安全性。
这显然不是我们希望的。

浏览器社区已经在采取行动来解决这些问题。
CA/Browser Forum Baseline Requirements 规定了这些受信的证书权威在签发证书时应该遵守的规则。
作为 WebTrust audit 项目的一部分，在将 CA 加入到某些信任仓库（例如 Mozilla 的）之前，会对 CA 合规性进行审计。

如果**<mark>内部场景</mark>**（internal stuff）已经在使用 TLS，你可能大部分情况下
**<mark>并不需要信任这些 public CA</mark>**。
如果信任了，就为 NSA 和其他组织打开了一扇地狱之门：你的系统安全性将取决于 100 多
个组织中安全性最弱的那一个。

## 8.3 Federation

### 证书欺骗的风险

令事情更糟糕的是，Web PKI relying parties (RPs) 信任它们的信任仓库中任何 CA
签发给任何 subscriber 的证书。结果是 Web PKI 整体的安全性取决于所有 Web PKI CA 中最弱的那个。
**<mark>2011 DigiNotar 攻击</mark>**就说明了这个问题：

* 作为攻击的一部分，**<mark>给 google.com 签发了一个假证书</mark>**，
  这个证书**<mark>被大部分浏览器和操作系统信任</mark>**，而它们不管 google 和
  DigiNotar 没有任何关系这一事实。
* 还有类似的欺骗证书颁发给了 Yahoo!, Mozilla, The Tor Project。
* 最终的解决方式是将 DigiNotar 的根证书从主流信任仓库中移除，但显然在此期间已经造成了大量破坏。

最近，**<mark>森海塞尔</mark>**（Sennheiser）因为在它们的 HeadSetup APP 信任仓库中
<a href="https://medium.com/asecuritysite-when-bob-met-alice/your-headphones-might-break-the-security-of-your-computer-4f304ed86611">安装了一个自签名的根证书</a>
引起了一次重大安全事故，

* 他们将相应的私钥（private key）嵌入在了 app 的配置中，
* 任何人都能从中提取这个私钥，然后颁发证书给任何 domain，
* 因此，任何在自己的信任仓库中添加了 Sennheiser 证书的，都将会信任这些欺骗证书。

这完全摧毁了 TLS 带来的好处，太糟糕了！

### 改进措施

已经有一些机制来减少此类风险：

1. <a href="https://tools.ietf.org/html/rfc6844">Certificate Authority Authorization</a> (CAA) allows you to restrict which CAs can issue certificates
for your domain using a special DNS record.
1. <a href="https://www.certificate-transparency.org/">Certificate Transparency</a>
    (CT) (<a href="https://tools.ietf.org/html/rfc6962">RFC 6962</a>) mandates
    that CAs submit every certificate they issue to an impartial observer that
    maintains a <a href="https://crt.sh/?Identity=smallstep.com">public
    certificate log</a> to detect fraudulently issued certificates.
    Cryptographic proof of CT submission is included in issued certificate
1. <a href="https://tools.ietf.org/html/rfc7469">HTTP Public Key Pinning</a> (HPKP
    or just "pinning") lets a subscriber (a website) tell an RP (a browser) to
    only accept certain public keys in certificates for a particular domain.

这里存在的问题是：**<mark>缺少 RP 端的支持</mark>**。CAB Forum now mandates CAA
checks in browsers. Some browsers also have some support for CT and HPKP. 但对于
其他 RPs (e.g., most TLS standard library implementations) **<mark>这些东西几乎都是没有
贯彻执行的</mark>**。This issue will come up repeatedly: a lot of certificate policy must
be enforced by RPs, and RPs can rarely be bothered. If RPs don't check CAA
records and don't require proof of CT submission this stuff doesn't do much
good.

### Internal PKI 使用单独的信任仓库

在任何情况下，如果使用自己的 internal PKI，都应该为 internal 服务**<mark>维护一个单独的信任仓库</mark>**。
即，

* 不要将你的根证书直接加到系统已有的信任仓库，
* 而应该配置 internal TLS 只使用你自己的根证书。

### Internal PKI 细粒度控制：CAA & SPIFFE

如果想在内部实现更好的联邦（federation） —— 例如限制 internal CA 能签发哪些证书，

* 可以试试 CAA records 然后对 RPs 进行恰当配置。
* 还可以看看 <a href="https://spiffe.io/"><mark>SPIFFE</mark></a>，这是一个还在不断发展的项目，
  目标是对一些 internal PKI 相关的问题进行标准化。

# 9 什么是证书权威（Certificate Authority）？

前面已经讨论了很多 CA 相关的东西，但我们还没定义什么是 CA。

* 一个证书权威（CA）就是一个**<mark>受信任的证书颁发者</mark>**。
* CA 通过对一个证书进行签名，对一个**<mark>公钥和名字之间的绑定关系（binding）做担保</mark>**。
* 本质上来说，一个 CA 只不过是另一个证书加上用来签其他证书的相应私钥。

显然需要一些逻辑和过程来将这些东西串联起来。CA 需要将它的证书分发到信任仓库，接受和处理
证书请求，颁发证书给 subscriber。

* 一个暴露此类 API 给外部调用、自动化这些过程的 CA 称为**<mark>在线证书权威</mark>**（**<mark>online CA</mark>**）。
* 在信任仓库中那些自签名的根证书 称为根证书权威（**<mark>root CA</mark>**）。

## 9.1 Web PKI 不能自动化签发证书

CAB Forum Baseline Requirements 4.3.1 明确规定：一个 Web PKI CA 的 root private key
只能通过 issue a direct command 来签发证书。

* 换句话说，**<mark>Web PKI root CA 不能自动化证书签名（certificate signing）过程</mark>**。
* 对于任何大的 CA operation 来说，无法在线完成都是一个问题。
  不可能每次签发一个证书时，都人工敲一个命令。

这样规定是出于**<mark>安全考虑</mark>**。

* Web PKI root certificates 广泛存在于信任仓库中，很难被撤回。截获一个
  root CA private key 理论上将影响几十亿的人和设备。
* 因此，最佳实践就是，确保 root private keys 是离线的（offline），理想情况下在一些
  <a href="https://en.wikipedia.org/wiki/Hardware_security_module">专用硬件</a>
  上，连接到某些物理空间隔离的设备上，有很好的物理安全性，有严格的使用流程。

**<mark>一些 internal PKI 也遵循类似的实践，但实际上并没有这个必要</mark>**。

* 如果能自动化 root certificate rotation （例如，通过配置管理或编排工具，更新信任仓库），
  你就能轻松地 rotate 一个 compromised root key。
* 由于人们如此沉迷于 internal PKI 的根秘钥管理，导致 internal PKI 的部署效率大大
  降低。你的 AWS root account credentials 至少也是机密信息，你又是如何管理它的呢？

## 9.2 Intermediates, Chains, and Bundling

在 root CA offline 的前提下，为使证书 issuance 可扩展（例如，使自动化成为可能），
root private key 只在很少情况下使用，

1. 用来签发几个*intermediate certificates*。
1. 然后 intermediate CA（也称为 subordinate CAs）用相应的 intermediate private keys 来签发 leaf certificates to subscribers。

如下图所示：

<p align="center"><img src="/assets/img/everything-about-pki/cert-path.png" width="60%" height="60%"></p>
<p align="center">Image credit: <a href="https://brainbit.io/posts/cilium-tls-inspection/">Cilium TLS inspection</a></p>

下面这张图把签发关系展示地更清楚，

<p align="center"><img src="/assets/img/everything-about-pki/trust-chain.png" width="70%" height="70%"></p>
<p align="center">Image credit: <a href="https://brainbit.io/posts/cilium-tls-inspection/">Cilium TLS inspection</a></p>

**<mark>Intermediates 通常并不包含在信任仓库中，所以撤回或 roate 比较容易</mark>**，
因此通过 intermediate CA，就实现了 certificate issuance 的在线和自动化（online and automated）。

这种 leaf、intermediate、root 组成的证书捆绑（bundle）机制，
形成了一个**<mark>证书链</mark>**（certificate chain）。

* leaf 由 intermediate 签发，
* intermediate 又由 root 签发，
* root 自签名（signs itself）。

技术上来说，上面都是简化的例子，你可以创建更长的 chain 和更复杂的图（例如，
<a href="https://docs.microsoft.com/en-us/windows/desktop/seccertenroll/about-cross-certification">cross-certification</a>）。
但不推荐这么做，因为复杂性很快会失控。在任何情况下，
end entity certificates 都是叶子节点，这也是称为叶子证书（leaf certificate）的原因。

当配置一个 **<mark>subscriber</mark>** 时（例如，Apache、Nginx、Linkderd、**<mark>Envoy</mark>**），
通常不仅需要叶子证书，还需要一个**<mark>包含了 intermediates 的 certificate bundle</mark>**。

* 有时会用 `PKCS#7` 和 `PKCS#12`，因为它们能包含一个完整的证书链（certificate chain）。
* 更多情况下，证书链编码成一个简单的空行隔开的 PEM 对象（sequence of line-separated PEM objects）。

    Some stuff expects the certs to be ordered from leaf to root, other stuff expects root to leaf, and some stuff doesn't care.
    More annoying inconsistency. Google and Stack Overflow help here. Or trial and error.

    下面是一个例子：

    ```shell
    $ cat server.crt
    -----BEGIN CERTIFICATE-----
    MIICFDCCAbmgAwIBAgIRANE187UXf5fn5TgXSq65CMQwCgYIKoZIzj0EAwIwHzEd
    ...
    MBsGA1UEAxMUVGVzdCBJbnRlcm1lZGlhdGUgQ0EwHhcNMTgxMjA1MTc0OTQ0WhcN
    HO3iTsozZsCuqA34HMaqXveiEie4AiEAhUjjb7vCGuPpTmn8HenA5hJplr+Ql8s1
    d+SmYsT0jDU=
    -----END CERTIFICATE-----
    -----BEGIN CERTIFICATE-----
    MIIBuzCCAWKgAwIBAgIRAKBv/7Xs6GPAK4Y8z4udSbswCgYIKoZIzj0EAwIwFzEV
    ...
    BRvPAJZb+soYP0tnObqWdplmO+krWmHqCWtK8hcCIHS/es7GBEj3bmGMus+8n4Q1
    x8YmK7ASLmSCffCTct9Y
    -----END CERTIFICATE-----
    ```

    Again, annoying and baroque, but not rocket science.

## 9.3 RP：Certificate path validation

由于 **<mark>intermediate certificates 并未包含在信任仓库中，因此需要与
leaf certificates 一样分发和验证</mark>**。

* 前面已经介绍，配置 subscriber 时需要提供这些 intermediates，**<mark>subscribers 随后再将它们传给 RP</mark>**。
* 如果使用 TLS，那这个过程发生在 **<mark>TLS 握手时</mark>**。
* 当一个 subscriber 将它的证书发给 relying party 时，其中会包含所有能证明来自信任的根证书的 intermediates。
* relying party 通过一个称为 **<mark>certificate path validation</mark>** 的过程来验证 leaf 和 intermediate certificates 。

<p align="center"><img src="/assets/img/everything-about-pki/cert-path-validation.jpg" width="80%" height="80%"></p>

完整的 <a href="https://tools.ietf.org/html/rfc5280#section-6">certificate path validation</a>
算法比较复杂。包括了

* checking certificate expirations
* revocation status
* various certificate policies
* key use restrictions
* a bunch of other stuff

显然，PKI RP 准确实现这个算法是非常关键的。

* 如果**<mark>关闭 certificate path validation</mark>**
  (例如，**<mark><code>curl -k</code></mark>**)，用户将面临重大风险，所以不要关闭。
* 完成正确的 TLS 并没有那么难，certificate path validation 是
  **<mark>TLS 中完成认证（authentication）的部分</mark>**。

可能有人会说，channel 已经是加密的了，因此关闭没关系 —— 错，有关系。
**<mark>没有认证（authentication）的加密是毫无价值的</mark>** —— 这就像在教堂忏悔：
你说的话都是私密的，但却并不知道帘幕后面的人是谁 —— 只不过这里不是教堂，而是互联网。

# 10 秘钥和证书的生命周期

在能通过 TLS 等协议使用证书之前，要先**<mark>配置如何从 CA 获取一个证书</mark>**。
逻辑上来说这是一个相当简单的过程：

1. 需要证书的 subscriber 自己先生成一个 key pair，然后通过请求发送给 CA，
2. CA 检查其中关联的 name 是否正确，如果正确就签名并返回一个证书。

证书会过期，过期的证书就不会被 RP 信任了。如果证书快过期了而还想继续用它，就需要
续期（renew ）并轮转（rotate）它。如果想在一个证书过期之前就让 RP 停止信任它，就需要执行撤销（revoke）。

与 PKI 相关的大部分东西一样，**<mark>这些看似简单的过程实际上都充满坑</mark>**。
其中也隐藏了计算机科学中最难的两个问题：缓存一致性和命名（naming）。
但另一方面，一旦理解了背后的原理，再反过来看实际在用的一些东西就简单多了。

## 10.1 Naming things（命名相关）

### DN (distinguished names)

历史上，X.509 使用 X.500 distinguished names (DN) 来命名证书的使用者（name the subject of a certificate），即 subscriber。
一个 DN 包含了一个 common name （对作者我来说，就是 “Mike Malone”），此外还可以包含
locality、country、organization、organizational unit 及其他一些东西（数字电话簿相关）。

* **<mark>没人理解 DN，它在互联网上也没什么意义</mark>**。
* 应该避免使用 DN。如果真的要用，也要尽量保持简单。
* 无需使用全部字段，实际上，也不应该使用全部字段。
* common name 可能就是**<mark>需要用到的全部字段</mark>**了，如果你是一个 thrill seeker ，可以在用上一个 organization name。

PKIX 规定一个网站的 DNS hostname 应该关联到 DN *common name*。最近，CAB Forum 已
经废弃了这个规定，使整个 DN 字段变成可选的（Baseline Requirements, sections
7.1.4.2）。

### SAN (subject alternative name)

**<mark>现代最佳实践</mark>**使用
<a href="https://tools.ietf.org/html/rfc5280#section-4.2.1.6">subject alternative name (SAN) X.509 extension</a>
来 **<mark>bind 证书中的 name</mark>**。

常用的 SAN 有四种类型，绑定的都是广泛使用的名字：

1. domain names (DNS)
1. email addresse
1. IP addresse
1. URI

在我们讨论的上下文中，这些**<mark>都是唯一的</mark>**，而且它们**<mark>能很好地映射到我们想识别的东西</mark>**：

* email addresses for people
* domain names and IP addresses for machines and code,
* URIs if you want to get fancy

应该使用 SAN。

<p align="center"><img src="/assets/img/everything-about-pki/inspect-san-dns.png" width="65%" height="65%"></p>

注意，Web PKI 允许在一个证书内 bind 多个 name，name 也也允许通配符。也就是说，

* 一个证书可以有多个 SAN，也可以有类似 `*.smallstep.com` 这样的 SAN。
* 这对有多个域名的的网站来说很有用。

## 10.2 生成 key pairs

有了 name 之后，需要先生成一个密钥对，然后才能创建证书。前面提到：PKI 的安全性
在根本上取决于一个简单的事实：**<mark>只有与证书中的 subscriber name 对应的 entity，才应该拥有与该证书对应的私钥</mark>**。
为确保这个条件成立，

* 最佳实践是**<mark>让 subscriber 生成它自己的密钥对，这样就只有它自己知道私钥</mark>**。
* 绝对应该避免通过网络发送私钥。

生成证书时**<mark>使用什么类型的秘钥</mark>**？这一主题值得单独写一篇文章，这里
只提供一点快速指导（截止 2018.12）。

* 如今有一个缓慢但清晰的**<mark>从 RSA 转向椭圆曲线秘钥的趋势</mark>**（
  <a href="https://blog.cloudflare.com/ecdsa-the-digital-signature-algorithm-of-a-better-internet">ECDSA</a>
  或 <a href="https://tools.ietf.org/html/rfc8032">EdDSA</a>）。
* 如果决定使用 RSA 秘钥，确保它们至少是 2048 比特长，但也不要超过 4096 比特。
* 如果使用 ECDSA，那 P-256 曲线可能是最好选择（`secp256k1` or `prime256v1` in openssl），
  除非你担心 NSA，这种情况下你可以选择更 fancier 一些的东西，例如 EdDSA with Curve25519（但对这些秘钥的支持还不是太好）。

下面是用 `openssl` 生成一个椭圆曲线 P-256 key pair 的例子：

```shell
$ openssl ecparam -name prime256v1 -genkey -out k.prv
$ openssl ec -in k.prv -pubout -out k.pub

# 也可以用 step 生成
$ step crypto keypair --kty EC --curve P-256 k.pub k.prv
```

还可以通过编程来生成这些证书，这样能做到证书不落磁盘。

## 10.3 Issuance（确保证书中的信息都是对的）

subscriber 有了一个 name 和一对 key 之后，下一步就是**<mark>从 CA 获取一个 leaf certificate</mark>**。
对 CA 来说，它需要认证（证明）两件事情：

1. subscriber 证书中的公钥，确实是该 subscriber 的公钥（例如，验证该 subscriber 知道对应的私钥）；

   这一步通常通过一个简单的技术机制实现：**<mark>证书签名请求</mark>**（certificate signing request, CSR）。

1. 证书中将要绑定的 name，确实是该 subscriber 的 name。

    这一步要难很多。抽象来说，这个过程称为 **<mark>identity proofing（身份证明）或 registration（注册）</mark>**.

### 10.3.1 Certificate signing requests（证书签名请求，PKCS#10）

Subscriber 请求一个证书时，会向 CA 会提交一个 certificate signing request (CSR)。

* **<mark>CSR 也是一个 ASN.1 结构</mark>**，定义在 <a href="https://tools.ietf.org/html/rfc2986">PKCS#10</a>。
* 与证书类似，CSR 数据结构包括一个公钥、一个名字和一个签名。
* CSR 是**<mark>自签名的</mark>**，用与 CRS 中公钥对应的私钥自签名。

    * 这个签名用于证明该 subscriber 有对应的私钥，能对任何用其公钥加密的东西进行解密。
    * 还使即使 CSR 被复制或转发，都没有能篡改其中的内容（篡改无效）。

* CSR 中包括了很多证书细节配置项。但在实际中，大部分配置项都会被 CA 忽略。大部分 CA 都使用自己的固定模板，
  或提供一个 administrative 接口来收集这些信息。

用 `step` 命令创建一个密钥对和 CSR 的例子：

```shell
$ step certificate create -csr test.smallstep.com test.csr test.key
```

OpenSSL 功能也非常强大，但 <a href="https://www.openssl.org/docs/manmaster/man1/openssl-req.html">用起来不够方便</a>。

### 10.3.2 Identity proofing（身份证明过程）

CA 收到一个 CSR 并验证签名之后，接下来需要确认证书中绑定的 name 是否真的
是这个 subscriber 的 name。这项工作很棘手。
证书的核心功能是**<mark>能让 RP 对 subscriber 进行认证</mark>**。因此，
如果一个**<mark>证书都还没有颁发，CA 如何对这个 subscriber 进行认证呢</mark>**？

答案是：分情况。

#### Web PKI 证明身份过程

Web PKI 有三种类型的证书，它们**<mark>最大的区别就是如何识别 subscriber</mark>**，
以及它们所用到的 **<mark>identity proofing 机制</mark>**。

这三种证书是：

1. domain validation (DV，域验证)

    DV 证书绑定的是 **<mark>DNS name</mark>**，CA 在颁发时需要验证的这个 domain name 确实是由该 subscriber 控制的。

    证明过程通常是通过一个简单的流程，例如

    1. 给 WHOIS 记录中该 domain name 的管理员发送一封确认邮件。
    2. <a href="https://ietf-wg-acme.github.io/acme/draft-ietf-acme-acme.html">ACME protocol</a>
      （最初由 Let's Encrypt 开发和使用）改进了这种方式，更加自动化：不再用邮件验证
      ，而是由 ACME CA 提出一个 challenge，该 subscriber 通过完成这个问题来证明它拥有
      这个域名。challenge 部分属于 ACME 规范的扩展部门，常见的包括：

        * 在指定的 URL 上提供一个随机数（HTTP challenge）
        * 在 DNS TXT 记录中放置一个随机数（DNS challenge）

2. organization validation (OV，组织验证)

    * OV 和下面将介绍的 EV 证书构建在 DV 证书之上，它们包括了 name 和域名
      **<mark>所属组织的位置信息（location）</mark>**。
    * OV 和 EV 证书不仅仅将证书关联到域名，还关联到控制这个域名的法律实体（legal entity）。
    * OV 证书的验证过程，不同的 CA 并不统一。为解决这个问题，CAB Forum 引入了 EV 证书。

3. **<mark>extended validation</mark>** (EV，扩展验证)

    * EV 证书包含的基本信息与 OV 是一样的，但强制要求严格验证（identity proofing）。
    * EV 过程需要几天或几个星期，其中可能包括公网记录搜索（public records searches）和公司人员（用笔）签署的（纸质）证词。

    这些完成之后，当相应网站时，**<mark>某些浏览器会在 URL 栏中显示该组织的名称</mark>**。例如：

    <p align="center"><img src="/assets/img/everything-about-pki/github-ev.png" width="80%" height="80%"></p>

    但除了这个场景之外，EV certificates 并未得到广泛使用，Web PKI RP 也未强依赖它。


**<mark>本质上来说，每个 Web PKI RP 只需要 DV 级别的 assurance</mark>** 就行了，
也就是确保域名是被该 subscriber 控制的。重要的是能理解一个 DV 证书在设计上的意思和在实际上做了什么：

* 在设计上，希望通过它证明：请求这个证书的 entity 拥有对应的域名；
* 在实际上，真正完成的操作是：在某个时间，请求这个证书的 entity 能读一封邮件，或配置一条 DNS 记录，或能通过 HTTP serve 一个指定随机数等等。

但话说回来，DNS、电子邮件和 BGP 这些底层基础设施本身的安全性也并没有做到足够好，
针对这些基础设施的攻击还是
<a href="https://doublepulsar.com/hijack-of-amazons-internet-domain-service-used-to-reroute-web-traffic-for-two-hours-unnoticed-3a6f0dda6a6f">时有发生</a>，
目的之一就是获取证书。

#### Internal PKI 证明身份过程

上面是 Web PKI 的身份证明过程，再来看 internal PKI 的身份证明过程。

实际上，用户可以使用**<mark>任何方式</mark>**来做 internal PKI 的 identity proofing，
并且效果可能比 Web PKI 依赖 DNS 或邮件方式的效果更好。

乍听起来好像很难，但其实不难，因为可以**<mark>利用已有的受信基础设施</mark>**：
用来搭建基础设施的工具，也能用来为这些基础设施之上的服务创建和证明安全身份。

* 如果用户已经信任 Chef/Puppet/Ansible/Kubernetes，允许它们将代码放到服务器上，
  那也应该信任它们能完成 identity attestations
* 如果在 AWS 上，可以用 <a href="https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/instance-identity-documents.html">instance
identity documents</a>
* 如果在 GCP：<a href="https://cloud.google.com/compute/docs/instances/verifying-instance-identity">GCP</a>
* <a href="https://docs.microsoft.com/en-us/azure/active-directory/managed-identities-azure-resources/how-to-use-vm-token">Azure</a>


provisioning infrastructure 必须理解 identity 的概念，这样才能将正确的代码放到正确的位置。
此外，用户必须信任这套机制。基于这些知识和信任，才能配置 RP 信任仓库、将 subscribers 纳入你的 internal PKI 管理范围。
而完成这些功能全部所需做的就是：设计和实现某种方式，能让
provisioning infrastructure 在每个服务启动时，能将它们的 identity 告诉你的 CA。
顺便说一句，这正是
<a href="https://smallstep.com/certificates/">step certificates</a> 解决的事情。

## 10.4 Expiration（过期）

证书通常都会过期。虽然这不是强制规定，但一般都这么做。设置一个过期时间非常重要，

* **<mark>证书都是分散在各处的</mark>**：通常 RP 在验证一个证书时，并没有某个中心式权威能感知到（这个操作）。
* 如果没有过期时间，证书将永久有效。
* 安全领域的一条经验就是：**<mark>时间过的越久，凭证被泄露的概率就越接近 100%</mark>**。

因此，设置过期时间非常重要。具体来说，X.509 证书中包含一个有效时间范围：

1. *issued at*
1. *not before*
1. *not after*：过了这个时间，证书就过期了。

这个机制看起来设计良好，但实际上也是有一些不足的：

* 首先，**<mark>没有什么能阻止 RP</mark>** 错误地（或因为糟糕的设计）**<mark>接受一个过期证书</mark>**；
* 其次，证书是分散的。验证证书是否过期是每个 RP 的责任，而有时它们会出乱子。例如，**<mark>RP 依赖的系统时钟不对</mark>**时。
  **<mark>最坏的情况就是系统时钟被重置为了 unix epoch</mark>**（`1970.1.1`），此时它无法信任任何证书。

在 subscriber 侧，证书过期后，私钥要处理得当：

* 如果一个密钥对之前是**<mark>用来签名/认证</mark>**的（例如，基于 TLS），

    * 应该在不需要这个密钥对之后，**<mark>立即删除私钥</mark>**。
    * 保留已经失效的签名秘钥（signing key）会导致不必要的风险：对谁都已经没有用处，反而会被拿去仿冒签名。

* 如果密钥对是**<mark>用来加密的</mark>**，情况就不同了。

    * 只要还有数据是用这个加密过的，就需要**<mark>留着这个私钥</mark>**。

这就是为什么很多人会说，**<mark>不要用同一组秘钥来同时做签名和加密</mark>**（signing and encryption）。
因为当一个用于签名的私钥过期时，**<mark>无法实现秘钥生命周期的最佳管理</mark>**：
最终不得不保留着这个私钥，因为解密还要用它。

## 10.5 Renewal（续期）

证书快过期时，如果还想继续使用，就需要续期。

### 10.5.1 Web PKI 证书续期

Web PKI 实际上并**<mark>没有标准的续期过期</mark>**：

* 没有一个标准方式来延长证书的合法时间，
* 一般是**<mark>直接用一个新证书替换过期的</mark>**。
* 因此续期过程和 issuance 过程是一样的：**<mark>生成并提交一个 CSR</mark>**，然后完成 identity proofing。

### 10.5.2 Internal PKI 证书续期

对于 internal PKI 我们能做的更好。

最简单的方式是：

* **<mark>用 mTLS 之类的协议对老证书续期</mark>**。
* CA 能对 subscriber 提供的客户端证书进行认证（authenticate），**<mark>重签一个更长的时间</mark>**，然后返回这个证书。
* 这使得续期过程**<mark>很容易自动化</mark>**，而且强制 subscriber 定期与中心权威保持沟通。
* 基于这种机制能轻松**<mark>构建一个证书的监控和撤销基础设施</mark>**。

### 10.5.3 小结

证书的续期过程其实并不是太难，**<mark>最难的是记得续期这件事</mark>**。

几乎每个管理过公网证书的人，都经历过证书过期导致的生产事故，<a href="https://expired.badssl.com/">例如这个</a>。
我的建议是：

1. 发现问题之后，一定要全面排查，解决能发现的所有此类问题。
2. 另外，使用生命周期比较短的证书。这会反过来逼迫你们优化和自动化整个流程。

Let's Encrypt 使自动化非常容易，它签发 90 天有效期的证书，因此对 Web PKI 来说非常合适。
对于 internal PKI，建议有效期签的更短：24 小时或更短。有一些实现上的挑战 ——
<a href="https://diogomonica.com/2017/01/11/hitless-tls-certificate-rotation-in-go/">hitless certificate rotation</a>
可能比较棘手 —— 但这些工作是值得的。

> 用 `step` 检查证书过期时间：
>
> ```shell
> step certificate inspect cert.pem --format json | jq .validity.end
> step certificate inspect https://smallstep.com --format json | jq .validity.end
> ```
>
> 将这种命令行封装到监控采集脚本，就可以实现某种程度的监控和自动化。

## 10.6 Revocation（撤销）

如果一个私钥泄露了，或者一个证书已经不再用了，就需要撤销它。即希望：

1. 明确地将其标记为非法的，
2. 所有 RP 都不再信任这个证书了，即使它还未过期。

但实际上，**<mark>撤销证书过程也是一团糟</mark>**。

### 10.6.1  主动撤销的困难

* 与过期类似，**<mark>执行撤回的职责在 RP</mark>**。
* 与过期不同的是，**<mark>撤销状态无法编码在证书中</mark>**。RP 只能依靠某些带外过程（out-of-band process）
  来判断证书的撤销状态。

除非显式配置，否则大部分 Web PKI TLS RP 并不关注撤销状态。换句话说，默认情况下，
大部分 TLS 实现都乐于接受已经撤销的证书。

### 10.6.2 Internal PKI：被动撤销机制

Internal PKI 的趋势是接受这个现实，然后试图通过**<mark>被动撤销</mark>**（passive revocation）机制来弥补，
具体来说就是**<mark>签发生命周期很短的证书</mark>**，这样就使撤销过程变得不再那么重要了。
想撤销一个证书时，直接不给它续期就行了，过一段时间就会自动过期。

可以看到，**<mark>这个机制有效的前提</mark>**就是使用生命周期很短的证书。具体有多短？

1. 取决于你的威胁模型（安全专家说了算）。
2. 24 小时是很常见的，但也有短到 5 分钟的。
3. 如果生命周期太短，显然也会给可扩展性和可用性带来挑战：**<mark>每次续期都需要与 online CA 交互</mark>**，
  因此 CA 有性能压力。
4. 如果缩短了证书的生命周期，记得**<mark>确保你的时钟是同步的</mark>**，否则就有罪受了。

对于 web 和其他的被动撤销不适合的场景，如果认真思考之后发现**<mark>真的</mark>**
需要撤销功能，那有两个选择：

1. CRL（，**<mark>证书撤销列表</mark>**，RFC 5280）
1. OCSP（Online Certificate Signing Protocol，**<mark>在线证书签名协议</mark>**，RFC 2560）

### 10.6.3 主动检查机制：CRL（Certificate Revocation Lists）

CRL 定义在 RFC 5280 中，这是一个相当庞杂的 RFC，还定义了很多其他东西。
简单来是，CRL 是一个**<mark>有符号整数序列，用来识别已撤销的证书</mark>**。

这个维护在一个 **<mark>CRL distribution point</mark>** 服务中，每个证书中都包含指向这个服务的 URL。
工作流程：每个 RP 下载这个列表并缓存到本地，在对证书进行验证时，从本地缓存查询撤销状态。
但这里也有一些明显的问题：

1. **<mark>CRL 可能很大</mark>**，
2. distribution point 也可能失效。
3. RP 的 CRL 缓存同步经常是天级的，因此如果一个证书撤销了，可能要几天之后才能同步到这个状态。
4. 此外，RP *fail open* 也很常见 —— CRL distribution point 挂了之后，就接受这个证书。
  这显然是一个安全问题：只要对 CRL distribution point 发起 DDoS 攻击，就能让 RP 接受一个已经撤销的证书。

因此，即使已经在用 CRL，也应该考虑使用短时证书来保持 CRL size 比较小。
CRL 只需要包含**<mark>已撤销但还未过期的证书</mark>**的 serial numbers，因此
证书生命周期越短，CRL 越短。

### 10.6.4 主动检查机制：OCSP（Online Certificate Signing Protocol）

主动检查机制除了 CRL 之外，另一个选择是 OCSP，它允许 RP 实时查询一个 *OCSP responder*：
指定证书的 serial number 来获取这个证书的撤销状态。

与 CRL distribution point 类似，OCSP responder URL 也包含在证书中。
这样看，OCSP 似乎更加友好，但实际上它也有自己的问题。对于 Web PKI，它引入了验证的隐私问题：

1. 每次查询 OCSP responder，使得它能看到我正在访问哪个网站。
1. 此外，它还增加了每个 TLS 连接的开销：需要一个额外请求来检查证实的撤销状态。
1. 与 CRL 一样，很多 RPs (including browsers) 会在 OCSP responder 失效时直接认为证书有效（未撤销）。

### 10.6.5 主动检查机制：OCSP stapling（合订，绑定）

OCSP stapling 是 OCSP 的一个变种，目的是解决以上提到的那些问题。

相比于让 RP 每次都去查询 OCSP responder，OCSP stapling 中让证书的 subscriber 来做这件事情。
OCSP response 是一个经过签名的、时间较短的证词（signed attestation），证明这个证书未被撤销。

attestation 包含在 subscriber 和 RP 的 TLS handshake ("stapled to" the certificate) 中。
这给 RP 提供了相对比较及时的撤销状态，而不用每次都去查询 OCSP responder。
subscriber 可以在 signed OCSP response 过期之前多次使用它。这减少了 OCSP 的负担，也解决了 OCSP 的隐私问题。

但是，所有这些东西其实最终都像是一个 **<mark>鲁布·戈德堡装置（Rube Goldberg Device） </mark>**，

> 鲁布·戈德堡机械（Rube Goldberg machine）是一种被设计得过度复杂的机械组合，以
> 迂回曲折的方法去完成一些其实是非常简单的工作，例如倒一杯茶，或打一只蛋等等。
> 设计者必须计算精确，令机械的每个部件都能够准确发挥功用，因为任何一个环节出错
> ，都极有可能令原定的任务不能达成。
>
> 解释来自 [知乎](https://www.zhihu.com/topic/20017497/intro)。

如果让 subscribers 去 CA 获取一些生命周期很短的证词（signed attestation）来证明对应的证书并没有过期，
为什么不直接干掉中间环节，直接使用生命周期很短的证书呢？

# 11 使用证书

虽然理解 PKI 需要以上长篇大论，但在实际中用证书其实是非常简单的。

下面以 TLS 为例，其他方式也是类似的：

1. 配置 PKI relying party 使用哪个根证书；

   对于 Web PKI，通常已经默认配置了正确的根证书，这一步可以跳过。

1. 配置 PKI subscriber 使用哪个证书和私钥（或如何生成自己的密钥对、如何提交 CSR）；

    某个 entity (code, device, server, etc) 既是 RP 又是 subscriber 是很常见的。
    这样的 entities 需要同时配置根证书、证书和私钥。


下面是个完整例子，展示 certificate issuance, root certificate
distribution, and TLS client (RP) and server (subscriber) configuration:

<p align="center"><img src="/assets/img/everything-about-pki/step-ca-certificate-flow.jpg" width="80%" height="80%"></p>

希望这展示了**<mark>使用 internal PKI 和 TLS 是如何简单直接</mark>**。

* 有了这样的基础，就无需使用自签名的证书或做一些危险的事情，例如禁用 certificate path validation（**<mark><code>curl -k</code></mark>**）。
* 几乎每个 TLS client/server 都支持这些参数；但是，它们又几乎都不关注秘钥和证书
  的声明周期：都假设证书会出现在磁盘上的恰当位置，有人或服务会帮它们完成 rotate
  等工作。这项生命周期相关的工作才是难点。

# 12 结束语

公钥加密系统使计算机能在网络上看到对方（"see" across networks）。

* 如果我有公钥，就能“看到”你有对应的私钥，但我自己是无法使用这个私钥的。
* 如果还没有对方的公钥，就需要证书来帮忙。证书将公钥和私钥拥有者的名字（name）相关联，
  它们就像是计算机和代码的驾照。
* 证书权威（CA）用它们的私钥对证书进行签名，对这些绑定关系作出担保，它们就像是车管局（DMV），
* 如果你出示一张车管局颁发的驾照，脸长得也和驾照上的照片一样，那别人就可以认为你就是驾照上这个人（名字）。
  同理，如果你是唯一知道某个秘钥的 entity，你给我的证书也是从我信任的某个 CA 来的，那我就认为证书中的 name 就是你。

现实中，

1. 大部分证书都是 X.509 v3 证书，用 ASN.1 格式定义，通常序列化为 PEM-encoded DER。
2. 相应的私钥通常表示为 PKCS#8 objects，也序列化为 PEM-encoded DER。
3. 如果你用 Java 或微软的产品，可能会遇到 PKCS#7 and PKCS#12 封装格式。

加密领域有**<mark>沉重的历史包袱</mark>**，使当前的这些东西学起来、用起来非常让人沮丧，这比一项技术因为太难而不想学更加令人沮丧。

PKI 是使用公钥基础设施时涉及到的所有东西的统称：names, key types,
certificates, CAs, cron jobs, libraries 等。

* Web PKI 是浏览器默认使用的 PKI。Web PKI CA 是**<mark>受信但不可靠</mark>**的（trusted but not trustworthy）。
* Internal PKI 是用户自己构建和维护的 PKI。需要它是因为 Web PKI 并不是针对 internal 使用场景设计的，
  Internal PKI 更易于自动化和扩展，并且能让用户控制很多细节，例如 naming and certificate lifetime。
* 建议公网上使用 Web PKI，内网使用自己的 internal PKI
（例如，<a href="https://smallstep.com/blog/use-tls.html">use TLS</a> 来替代 VPN）。
* Smallstep Certificate Manager 使构建 internal PKI 非常简单。

要获得一个证书，需要命令和生成证书。建议 name 用 SAN：

* DNS SANs for code and machines
* EMAIL SANs for people
* 如果这些都不能用，就用 URI SAN

**<mark>秘钥类型（key type）</mark>**是很大一个主题，但几乎不重要：你可以随便修改秘钥类型，
而且实际上加密本身（crypto）并不是 PKI 中最弱的一环。

要从 CA 获取一个证书，需要提交一个 CSR 并证明申请者的身份（identity）。
使用生命周期较短的证书和 passive revocation。
自动化证书续期过程。不要禁用 certificate path validation。

最后还是那句话：**<mark>证书和 PKI 将名字关联到公钥</mark>**（bind names to public keys）。
其他都是细节。

# 13 延伸阅读（译注）

更多相关内容或实践，推荐：

1. [Illustrated X.509 Certificate](https://darutk.medium.com/illustrated-x-509-certificate-84aece2c5c2e)，2020

    超详细**<mark>图解 X.509 证书</mark>**。

2. [Cilium TLS inspection](https://brainbit.io/posts/cilium-tls-inspection/)，2021

    图解 X.509 证书、信任链，及 **<mark>Cilium/hubble L7 实战</mark>**。
