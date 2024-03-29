---
layout    : post
title     : "[译] 星巴克不使用两阶段提交（2004）"
date      : 2020-07-15
lastupdate: 2020-07-15
categories: distributed-system
---

### 译者序

本文翻译自 2004 年的一篇文章: [Starbucks Does Not Use Two-Phase
Commit](https://www.enterpriseintegrationpatterns.com/ramblings/18_starbucks.html).

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 1 请给我一杯热巧克力（Hotto Cocoa o Kudasai）

刚结束了一次为期两周的日本之旅。印象深刻的是数量多到难以置信的星巴克 —— 尤其是在
新宿和六本木地区。在等待咖啡制作时，我开始思考**星巴克是如何处理订单的**。

与大多数商业公司一样，星巴克主要关心的也是**订单最大化**。更多的订单就意味着更
多的收入。因此，他们采用**异步的方式处理订单**：

* 点好咖啡后，收银员会拿出一个杯子，将你的订单**在杯子上做个标记**，然后
  **将杯子放到一个队列**。这里所说的队列其实就是咖啡机上的一排杯子。
* **队列将收银员和咖啡师解耦**，使收银员能够不断接单，即使咖啡师已经有点忙不过来了。

在这种方式中，如果咖啡师真的忙不过来了，可以再加几个咖啡师，这就是所谓的
[Competing Consumer](https://www.enterpriseintegrationpatterns.com/CompetingConsumers.html)
场景。

# 2 关联（Correlation）

享受异步带来的好处的同时，星巴克也需要解决异步方式内在的挑战。例如，关联（correlation）问题。

**咖啡制作完成的顺序不一定与下单的顺序一致**。这有两个可能的原因：

1. 多位咖啡师可能在分别使用不同的咖啡机同时制作。另外，不同类型的咖啡所需的
   时间也不同，例如调配型咖啡会比已经磨好、拿杯子直接接就行的咖啡所花的时间要长。
2. 咖啡师可能会将同一咖啡类型的多个订单放到同一批制作，以节省整体的制作时间。

因此，星巴克会面临**咖啡与顾客之间的关联问题**。咖啡制作完成的顺序是不确定的，需
要将每一杯咖啡分别对应到正确的顾客。星巴克解决这个问题的方式与我们在消息系统
中所使用的“模式”（pattern）是一样的：使用某种关联 ID（[Correlation
Identifier](https://www.enterpriseintegrationpatterns.com/CorrelationIdentifier.html)）。

* 在美国，大部分星巴克都会将顾客的名字作为显式关联 ID（explicit correlation
  identifier）写到杯子上，咖啡制作完成后服务员会叫顾客的名字；
* 在其他国家，可能会用咖啡的类型来做关联（例如，服务员会喊“大杯摩卡好了”）。

# 3 异常处理（Exception Handling）

异步消息系统中的异常处理是很困难的。如果说现实世界中已经很好的解决了这个问题，那
我们可以通过观察星巴克如何处理异常学到一些东西。

1. 如果付款失败，他们会怎么做？
    * 如果咖啡已经做好了，他们会倒掉
    * 如果还没有开始做，他们会将杯子从“队列”中拿走
1. 如果咖啡做错了，或者对咖啡不满意？他们会重新做一杯。
1. 如果咖啡机坏了，做不了咖啡？他们会退款。

这些场景分别描述了几种常见的错误处理策略。

## 3.1 销账（Write-off）

这是所有错误处理策略中**最简单**的：**什么都不用做。或者是，丢弃已经做的所有东西**。

听起来似乎不靠谱，但实际业务中，有时这种方式是可接受的。如果销账带来的损失很小，
那相比斥巨资实现一种复杂的错误处理机制，销账的方式还是更划算的。

例如，我曾为多家因特网服务提供商（ISP）工作，在他们的业务中，如果计费（billing /
provisioning cycle）发生错误，它们就会选择销账的方式。其导致的结果是，客户可能会
享受了某些服务，但没有被收费。

这种处理方式给他们带来的营业损失足够小，因此业务能够保持运营。另外，公司会定期地
对账（reconciliation），主动检测这些“免费”账户并将其关闭。

## 3.2 重试（Retry）

当一大组操作（例如，一次事务）中的某些操作失败时，我们基本有两种选择：

1. 回退（undo）已完成的操作
1. 重试（retry）失败的操作

如果重试有较大的概率能成功，那就可以考虑重试方式。例如，

* 如果失败的原因是违反了业务规则，那重试就不太可能会成功。
* 如果失败的原因是某个外部系统挂了，那重试就有可能会成功。

这里有一种特殊的重试：[幂等接收器](https://www.enterpriseintegrationpatterns.com/IdempotentReceiver.html)
重试（retry with Idempotent Receiver）。在这种场景中，我们可以简单地重试所有操
作，因为接收器成功之后便会忽略重复的消息。

## 3.3 补偿（Compensating Action）

最后一种方式是回退所有已完成的操作（undo operations that were already completed），
让系统回到一致的状态。

例如，在金融系统中，这些“补偿动作”能在交易失败时对已扣款进行退款处理。

# 4 两阶段提交（two-phase commit）

以上所有策略都与两阶段提交不同。**两阶段提交包含前后两个步骤**：

1. 准备（prepare）阶段
2. 执行（execute）阶段

如果在**星巴克中使用两阶段提交**，那买一杯咖啡的过程将变为：

1. 准备阶段：前台点单，打印小票，然后将现金和小票都放到台面上，等待咖啡做好。
2. 执行阶段：咖啡做好后，现金、小票和咖啡同时易手（change hands in one swoop），完成交易。

在“事务”（transaction）完成之前，收银员和顾客都不能离开。

显然，如果使用这种提交方式，星巴克的业务量将急剧下降，因为相同时间内能服务的
顾客数量将锐减。

这个例子也提醒我们，两阶段提交会让生活变得加更简单（因为错误处理非常简单），但它
也会妨碍消息的自由流动（以及自由流动带来的可扩展性），因为它必须**将多个异步操作
封装成一个有状态事务**。

# 5 会话模式（Conversations）

咖啡店交互的过程其实也是一个简单但很常见的
[Conversation](https://www.enterpriseintegrationpatterns.com/ramblings/09_correlation.html) 模式的例子。

双方（顾客和咖啡店）之间由两次交互组成：

* **时间较短的同步交互**（a short synchronous interaction）：完成下单和支付
  （ordering and paying）
* **时间较长的异步交互**（a longer, asynchronous interaction）：完成咖啡的制
  作和交付（making and receiving the drink）

这种类型的会话（conversation）在电商场景中是非常普遍的。例如，在 Amazon 买东
西时，时间较短的异步交互过程会分配订单号，而所有的后续步骤（信用卡扣款、打包、配
送）都是异步完成的。这些额外的异步步骤完成后，你会收到邮件方式（异步）的通
知。如果中间发生任何差错，Amazon 通常会

* **补偿**：退款到信用卡，或
* **重试**：补发配送过程中丢失的物品。

可以看到，真实世界往往都是异步的。我们的日常生活是由许多协调但异步的（
coordinated, but asynchronous）过程组成的，例如读取和回复电子邮件，购买咖啡等等
。这意味着，异步消息模型（asynchronous messaging architecture）通常能很自然地对
这些类型的交互进行建模。

此外，这还意味着，**经常观察日常生活有助于设计出成功的消息系统**（
messaging solutions）。

感谢阅读！
