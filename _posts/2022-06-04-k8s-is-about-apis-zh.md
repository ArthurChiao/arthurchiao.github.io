---
layout    : post
title     : "K8s 的核心是 API 而非容器（一）：从理论到 CRD 实践（2022）"
date      : 2022-06-04
lastupdate: 2023-10-14
categories: k8s crd
---

<p align="center"><img src="/assets/img/k8s-is-about-apis/k8s-is-about-apis.jpeg" width="60%" height="60%"></p>

本文最初串联了以下几篇文章的核心部分，

1. [Kubernetes isn't about containers](https://blog.joshgav.com/2021/12/16/kubernetes-isnt-about-containers.html)，2021
2. [Kubernetes is a Database](https://github.com/gotopple/k8s-for-users-intro/blob/master/database.md), 2019
3. [CRD is just a table in Kubernetes](https://itnext.io/crd-is-just-a-table-in-kubernetes-13e15367bbe4), 2020

论述了 K8s 的核心价值是其通用、跨厂商和平台、可灵活扩展的声明式 API 框架，
而不是容器（虽然容器是它成功的基础）；然后手动创建一个 API extension（CRD），
通过测试和类比来对这一论述有一个更直观的理解。

例子及测试基于 K8s `v1.21.0`，感谢原作者们的精彩文章。

* [K8s 的核心是 API 而非容器（一）：从理论到 CRD 实践（2022）]({% link _posts/2022-06-04-k8s-is-about-apis-zh.md %})
* [K8s 的核心是 API 而非容器（二）：从开源项目看 k8s 的几种 API 扩展机制（2023）]({% link _posts/2022-06-04-k8s-is-about-apis-2-zh.md %})

----

* TOC
{:toc}

----

# 1 K8s 的核心是其 API 框架而非容器

## 1.1 容器是基础

时间回到 2013 年。当一条简单的 `docker run postgre` 命令就能运行起 postgre 这样
复杂的传统服务时，开发者在震惊之余犹如受到天启；以 docker 为代表的实用容器技术的
横空出世，也预示着一扇通往敏捷基础设施的大门即将打开。此后，一切都在往好的方向迅速发展：

* 越来越多的开发者开始采用**<mark>容器作为一种标准构建和运行方式</mark>**，
* 业界也意识到，很容易将这种封装方式引入计算集群，通过
  Kubernetes 或 Mesos 这样的编排器来调度计算任务 —— 自此，**<mark>容器便成为这些调度器最重要的 workload 类型</mark>**。

但本文将要说明，容器并非 Kubernetes 最重要、最有价值的地方，Kubernetes 也并非
仅仅是一个更广泛意义上的 workload 调度器 —— 高效地调度不同类型的 workload 只是
Kubernetes 提供的一种重要价值，但并不是它成功的原因。

## 1.2 API 才是核心

<p align="center"><img src="/assets/img/k8s-is-about-apis/k8s-is-about-apis.jpeg" width="60%" height="60%"></p>

> “等等 —— **<mark>K8s 只是一堆 API？</mark>**”
>
> “不好意思，一直都是！”

K8s 的成功和价值在于，提供了一种标准的编程接口（API），可以用来编写和使用
**<mark>软件定义的基础设施服务</mark>**（本文所说的“基础设施”，**<mark>范围要大于 IAAS</mark>**）：

* Specification + Implementation 构成一个完整的 API 框架 —— 用于设计、实现和使用**<mark>各种类型和规模的基础设施服务</mark>**；
* 这些 API 都基于相同的核心结构和语义：**<mark>typed resources watched and reconciled by controllers</mark>**
 （资源按类型划分，控制器监听相应类型的资源，并将其实际 status 校准到 spec 里期望的状态）。

为了进一步解释这一点，考虑下 Kubernetes 出现之前的场景。

### 1.2.1 K8s 之前：各自造轮子，封装厂商 API 差异

K8s 之前，基础设施基本上是各种不同 API、格式和语义的“云”服务组成的大杂烩：

1. 云厂商只提供了计算实例、块存储、虚拟网络和对象存储等基础构建模块，开发者需要像拼图一样将它们拼出一个相对完整的基础设施方案；
2. 对于其他云厂商，重复过程 1，因为各家的 API、结构和语义并不相同，甚至差异很大。

虽然 Terraform 等工具的出现，提供了一种跨厂商的通用格式，但原始的结构和语义仍然
是五花八门的，—— 针对 AWS 编写的 Terraform descriptor 是无法用到 Azure 的。

### 1.2.2 K8s 面世：标准化、跨厂商的 API、结构和语义

现在再来看 Kubernetes 从一开始就提供的东西：描述各种资源需求的标准 API。例如，

* 描述 pod、container 等**<mark>计算需求</mark>** 的 API；
* 描述 service、ingress 等**<mark>虚拟网络功能</mark>** 的 API；
* 描述 volumes 之类的**<mark>持久存储</mark>** 的 API；
* 甚至还包括 service account 之类的**<mark>服务身份</mark>** 的 API 等等。

这些 API 是跨公有云/私有云和各家云厂商的，各云厂商会将 Kubernetes 结构和语义
对接到它们各自的原生 API。
因此我们可以说，Kubernetes 提供了一种**<mark>管理软件定义基础设施（也就是云）的标准接口</mark>**。
或者说，Kubernetes 是一个针对云服务（cloud services）的标准 API 框架。

### 1.2.3 K8s API 扩展：CRD

提供一套跨厂商的标准结构和语义来声明核心基础设施（pod/service/volume/serviceaccount/...），
是 Kubernetes 成功的基础。在此基础上，它又通过 CRD（Custom Resource Definition），
将这个结构**<mark>扩展到任何/所有基础设施资源</mark>**。

* CRD 在 1.7 引入，允许云厂商和开发者自己的服务复用 K8s 的 spec/impl 编程框架。

    有了 CRD，用户不仅能声明 Kubernetes API 预定义的计算、存储、网络服务，
    还能声明数据库、task runner、消息总线、数字证书 ... 任何云厂商能想到的东西！

* [Operator Framework](https://operatorframework.io/) 以及 [SIG API Machinery](https://github.com/kubernetes/community/tree/master/sig-api-machinery)
 等项目的出现，提供了方便地创建和管理这些 CRD 的工具，最小化用户工作量，最大程度实现标准化。

例如，Crossplane 之类的项目，将厂商资源 RDS 数据库、SQS queue 资源映射到
Kubernetes API，就像核心 K8s controller 一样用自己的 controller 来管理网卡、磁盘等自定义资源。
Google、RedHat 等 Kubernetes 发行商也在它们的基础 Kubernetes 发行版中包含越来越多的自定义资源类型。

## 1.3 小结

我们说 Kubernetes 的核心是其 API 框架，但**<mark>并不是说这套 API 框架就是完美的</mark>**。
事实上，后一点并不是（非常）重要，因为 Kubernetes 模型已经成为一个事实标准：
开发者理解它、大量工具主动与它对接、主流厂商也都已经原生支持它。用户认可度、互操作性
经常比其他方面更能决定一个产品能否成功。

随着 Kubernetes 资源模型越来越广泛的传播，现在已经能够
用一组 Kubernetes 资源来描述一整个**<mark>软件定义计算环境</mark>**。
就像用 `docker run` 可以启动单个程序一样，用 `kubectl apply -f` 就能部署和运行一个分布式应用，
而无需关心是在私有云还是公有云以及具体哪家云厂商上，Kubernetes 的 API 框架已经屏蔽了这些细节。

因此，Kubernetes 并不是关于容器的，而是关于 API。

# 2 K8s 的 API 类型

可以通过 `GET/LIST/PUT/POST/DELETE` 等 API 操作，来创建、查询、修改或删除集群中的资源。
各 controller 监听到资源变化时，就会执行相应的 reconcile 逻辑，来使 status 与 spec 描述相符。

## 2.1 标准 API（针对内置资源类型）

### 2.1.1 Namespaced 类型

这种类型的资源是区分 namespace，也就是可以用 namespace 来隔离。
大部分内置资源都是这种类型，包括：

* pods
* services
* networkpolicies

API 格式：

* 格式：**<mark><code>/api/{version}/namespaces/{namespace}/{resource}</code></mark>**
* 举例：`/api/v1/namespaces/default/pods`

### 2.1.2 Un-namespaced 类型

这种类型的资源是全局的，**<mark>不能用 namespace 隔离</mark>**，例如：

* nodes
* clusterroles (`clusterxxx` 一般都是，表示它是 cluster-scoped 的资源）

API 格式：

* 格式：**<mark><code>/api/{version}/{resource}</code></mark>**
* 举例：`/api/v1/nodes`

## 2.2 扩展 API（`apiextension`）

### 2.2.1 Namespaced 类型

API 格式：

* 格式：**<mark><code>/apis/{apiGroup}/{apiVersion}/namespaces/{namespace}/{resource}</code></mark>**
* 举例：`/apis/cilium.io/v2/namespaces/kube-system/ciliumnetworkpolicies`

### 2.2.2 Un-namespaced 类型

略。

## 2.3 CRD

用户发现了 k8s 的强大之后，希望将越来越多的东西（数据）放到 k8s 里面，
像内置的 Pod、Service、NetworkPolicy 一样来管理，因此出现了两个东西：

1. CRD：用来声明用户的自定义资源，例如它是 namespace-scope 还是 cluster-scope
  的资源、有哪些字段等等，**<mark>K8s 会自动根据这个定义生成相应的 API</mark>**；

    官方文档的[例子](https://kubernetes.io/docs/tasks/extend-kubernetes/custom-resources/custom-resource-definitions/#create-a-customresourcedefinition)，
    后文也将给出一个更简单和具体的例子。

    CRD 是资源类型定义，具体的资源叫 CR。

2. Operator 框架：“operator” 在这里的字面意思是**<mark>“承担运维任务的程序”</mark>**，
  它们的基本逻辑都是一样的：时刻盯着资源状态，一有变化马上作出反应（也就是 reconcile 逻辑）。

这就是扩展 API 的（最主要）声明和使用方式。

至此，我们讨论的都是一些比较抽象的东西，接下来通过一些例子和类比来更直观地理解一下。

# 3 直观类比：K8s 是个数据库，CRD 是一张表，API 是 SQL

在本节中，我们将创建一个名为 `fruit` 的 CRD，它有 `name/sweet/weight` 三个字段，
完整 CRD 如下，

```yaml
apiVersion: apiextensions.k8s.io/v1
kind: CustomResourceDefinition
metadata:
  name: fruits.example.org        # CRD 名字
spec:
  conversion:
    strategy: None
  group: example.org              # REST API: /apis/<group>/<version>
  names:
    kind: Fruit
    listKind: FruitList
    plural: fruits
    singular: fruit
  scope: Namespaced               # Fruit 资源是区分 namespace 的
  versions:
  - name: v1                      # REST API: /apis/<group>/<version>
    schema:
      openAPIV3Schema:
        properties:
          spec:
            properties:
              comment:            # 字段 1，表示备注
                type: string
              sweet:              # 字段 2，表示甜否
                type: boolean
              weight:             # 字段 3，表示重量
                type: integer
            type: object
        type: object
    served: true                  # 启用这个版本的 API（v1）
    storage: true
    additionalPrinterColumns:     # 可选项，配置了这些 printer columns 之后，
    - jsonPath: .spec.sweet       # 命令行 k get <crd> <cr> 时，能够打印出下面这些字段，
      name: sweet                 # 否则，k8s 默认只打印 CRD 的 NAME 和 AGE
      type: boolean
    - jsonPath: .spec.weight
      name: weight
      type: integer
    - jsonPath: .spec.comment
      name: comment
      type: string
```

后面会解释每个 section 都是什么意思。在此之前，先来做几个（直观而粗糙的）类比。

## 3.1 K8s 是个数据库

像其他数据库技术一样，它有自己的持久存储引擎（etcd），以及构建在存储引擎之上的
一套 API 和语义。这些语义允许用户创建、读取、更新和删除（CURD）数据库中的数据。
下面是一些**<mark>概念对应关系</mark>**：

| 关系型数据库 | Kubernetes (as a database) | 说明 |
|:-----------|:----|:-----|
| `DATABASE` | cluster   | 一套 K8s 集群就是一个 database 【注 1】|
| `TABLE`    | `Kind`    | 每种资源类型对应一个表；分为内置类型和扩展类型 【注 2】|
| `COLUMN`   | property  | 表里面的列，可以是 string、boolean 等类型 |
| rows       | resources | 表中的一个具体 record |

> 【注 1】 如果只考虑 namespaced 资源的话，也可以说一个 namespace 对应一个 database。
>
> 【注 2】 前面已经介绍过，
>
> * 内置 `Kind`：Job、Service、Deployment、Event、NetworkPolicy、Secret、ConfigMap 等等；
> * 扩展 `Kind`：各种 CRD，例如 CiliumNetworkPolicy。

所以，和其他数据库一样，本质上 Kubernetes 所做的不过是以 schema 规定的格式来处理 records。

另外，Kubernetes 的表都有**<mark>自带文档</mark>**：

```shell
$ k explain fruits
KIND:     Fruit
VERSION:  example.org/v1

DESCRIPTION:
     <empty>

FIELDS:
   apiVersion   <string>
     APIVersion defines the versioned schema of this representation of an
     object. Servers should convert recognized schemas to the latest internal
     value, and may reject unrecognized values. More info:
     https://git.k8s.io/community/contributors/devel/sig-architecture/api-conventions.md#resources

   kind <string>
     Kind is a string value representing the REST resource this object
     represents. Servers may infer this from the endpoint the client submits
     requests to. Cannot be updated. In CamelCase. More info:
     https://git.k8s.io/community/contributors/devel/sig-architecture/api-conventions.md#types-kinds

   metadata     <Object>
     Standard object's metadata. More info:
     https://git.k8s.io/community/contributors/devel/sig-architecture/api-conventions.md#metadata

   spec <Object>
```

另外，Kubernetes API 还有**<mark>两大特色</mark>**：

1. 极其可扩展：声明 CRD 就会自动创建 API；
2. 支持事件驱动。

## 3.2 CRD 是一张表

CRD 和内置的 Pod、Service、NetworkPolicy 一样，不过是数据库的一张表。
例如，前面给出的 `fruit` CRD，有 `name/sweet/weight` 列，以及 “apple”, “banana” 等 entry，

<p align="center"><img src="/assets/img/k8s-is-about-apis/table-vs-crd.png" width="80%" height="80%"></p>

用户发现了 k8s 的强大，希望将越来越多的东西（数据）放到 k8s 里面来管理。数据类
型显然多种多样的，不可能全部内置到 k8s 里。因此，一种方式就是允许用户创建自己的
“表”，设置自己的“列” —— 这正是 CRD 的由来。

### 3.2.1 定义表结构（CRD spec）

CRD（及 CR）描述格式可以是 YAML 或 JSON。CRD 的内容可以简单分为三部分：

1. **<mark>常规 k8s metadata</mark>**：每种 K8s 资源都需要声明的字段，包括 `apiVersion`、`kind`、`metadata.name` 等。

    ```yaml
    apiVersion: apiextensions.k8s.io/v1
    kind: CustomResourceDefinition
    metadata:
      name: fruits.example.org        # CRD 名字
    ```

2. **<mark>Table-level 信息</mark>**：例如表的名字，最好用小写，方便以后命令行操作；

    ```yaml
    spec:
      conversion:
        strategy: None
      group: example.org              # REST API: /apis/<group>/<version>
      names:
        kind: Fruit
        listKind: FruitList
        plural: fruits
        singular: fruit
      scope: Namespaced               # Fruit 资源是区分 namespace 的
    ```

3. **<mark>Column-level 信息</mark>**：列名及类型等等，遵循 OpenAPISpecification v3 规范。

    ```yaml
      versions:
      - name: v1                      # REST API: /apis/<group>/<version>
        schema:
          openAPIV3Schema:
            properties:
              spec:
                properties:
                  comment:            # 字段 1，表示备注
                    type: string
                  sweet:              # 字段 2，表示甜否
                    type: boolean
                  weight:             # 字段 3，表示重量
                    type: integer
                type: object
            type: object
        served: true                  # 启用这个版本的 API（v1）
        storage: true
        additionalPrinterColumns:     # 可选项，配置了这些 printer columns 之后，
        - jsonPath: .spec.sweet       # 命令行 k get <crd> <cr> 时，能够打印出下面这些字段，
          name: sweet                 # 否则，k8s 默认只打印 CRD 的 NAME 和 AGE
          type: boolean
        - jsonPath: .spec.weight
          name: weight
          type: integer
        - jsonPath: .spec.comment
          name: comment
          type: string
    ```

### 3.2.2 测试：CR 增删查改 vs. 数据库 SQL

1. 创建 CRD：这一步相当于 **<mark><code>CREATE TABLE fruits ...;</code></mark>**，

    ```shell
    $ kubectl create -f fruits-crd.yaml
    customresourcedefinition.apiextensions.k8s.io/fruits.example.org created
    ```

2. 创建 CR：相当于 **<mark><code>INSERT INTO fruits values(...);</code></mark>**，

    `apple-cr.yaml`：

    ```yaml
    apiVersion: example.org/v1
    kind: Fruit
    metadata:
      name: apple
    spec:
      sweet: false
      weight: 100
      comment: little bit rotten
    ```

    `banana-cr.yaml`：

    ```yaml
    apiVersion: example.org/v1
    kind: Fruit
    metadata:
      name: banana
    spec:
      sweet: true
      weight: 80
      comment: just bought
    ```

    创建：

    ```shell
    $ kubectl create -f apple-cr.yaml
    fruit.example.org/apple created
    $ kubectl create -f banana-cr.yaml
    fruit.example.org/banana created
    ```

3. 查询 CR：相当于 **<mark><code>SELECT * FROM fruits ... ;</code></mark>**
    或 **<mark><code>SELECT * FROM fruits WHERE name='apple';</code></mark>**。

    ```shell
    $ k get fruits.example.org # or kubectl get fruits
    NAME     SWEET   WEIGHT   COMMENT
    apple    false   100      little bit rotten
    banana   true    80       just bought

    $ kubectl get fruits apple
    NAME    SWEET   WEIGHT   COMMENT
    apple   false   100      little bit rotten
    ```

4. 删除 CR：相当于 **<mark><code>DELETE FROM fruits WHERE name='apple';</code></mark>**，

    ```shell
    $ kubectl delete fruit apple
    ```

可以看到，CRD/CR 的操作都能对应到常规的数据库操作。

## 3.3 API 是 SQL

上一节我们是通过 `kubectl` 命令行来执行 CR 的增删查改，它其实只是一个外壳，内部
调用的是 **<mark>Kubernetes 为这个 CRD 自动生成的 API</mark>** —— 所以
又回到了本文第一节论述的内容：**<mark>K8s 的核心是其 API 框架</mark>**。

只要在执行 `kubectl` 命令时**<mark>指定一个足够大的 loglevel</mark>**，就能看到
背后的具体 API 请求。例如，

```shell
$ kubectl create -v 10 -f apple-cr.yaml
  ...
  Request Body: {"apiVersion":"example.org/v1","kind":"Fruit",\"spec\":{\"comment\":\"little bit rotten\",\"sweet\":false,\"weight\":100}}\n"},"name":"apple","namespace":"default"},"spec":{"comment":"little bit rotten","sweet":false,"weight":100}}
  curl -k -v -XPOST 'https://127.0.0.1:6443/apis/example.org/v1/namespaces/default/fruits?fieldManager=kubectl-client-side-apply'
  POST https://127.0.0.1:6443/apis/example.org/v1/namespaces/default/fruits?fieldManager=kubectl-client-side-apply 201 Created in 25 milliseconds
  ...
```

# 4 其他

## 4.1 给 CR 打标签（label），根据 label 过滤

和内置资源类型一样，K8s 支持对 CR 打标签，然后根据标签做过滤：

```shell
# 查看所有 frutis
$ k get fruits
NAME     SWEET   WEIGHT   COMMENT
apple    false   100      little bit rotten
banana   true    80       just bought

# 给 banana 打上一个特殊新标签
$ k label fruits banana tastes-good=true
fruit.example.org/banana labeled

# 按标签筛选 CR
$ k get fruits -l tastes-good=true
NAME     SWEET   WEIGHT   COMMENT
banana   true    80       just bought

# 删除 label
$ k label fruits banana tastes-good-
fruit.example.org/banana labeled
```

## 4.2 K8s API 与鉴权控制（RBAC）

不管是内置 API，还是扩展 API，都能用 K8s 强大的 RBAC 来做鉴权控制。

关于如何使用 RBAC 网上已经有大量文档；但如果想了解其设计，可参考
[Cracking Kubernetes RBAC Authorization Model]({% link _posts/2022-04-17-cracking-k8s-authz-rbac.md %})，
它展示了如何从零开始设计出一个 RBAC 鉴权模型（假设 K8s 里还没有）。

# 参考资料

1. [Kubernetes isn't about containers](https://blog.joshgav.com/2021/12/16/kubernetes-isnt-about-containers.html)，2021
2. [Kubernetes is a Database](https://github.com/gotopple/k8s-for-users-intro/blob/master/database.md), 2019
3. [CRD is just a table in Kubernetes](https://itnext.io/crd-is-just-a-table-in-kubernetes-13e15367bbe4), 2020
4. [Cracking Kubernetes RBAC Authorization Model]({% link _posts/2022-04-17-cracking-k8s-authz-rbac.md %}), 2022
