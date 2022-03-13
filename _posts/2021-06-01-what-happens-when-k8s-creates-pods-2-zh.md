---
layout    : post
title     : "源码解析：K8s 创建 pod 时，背后发生了什么（二）（2021）"
date      : 2021-06-01
lastupdate: 2022-03-17
categories: k8s
---

本文基于 2019 年的一篇文章
[What happens when ... Kubernetes edition!](https://github.com/jamiehannaford/what-happens-when-k8s)
**<mark>梳理了 k8s 创建 pod</mark>**（及其 deployment/replicaset）**<mark>的整个过程</mark>**，
整理了每个**重要步骤的代码调用栈**，以**<mark>在实现层面加深对整个过程的理解</mark>**。

原文参考的 k8S 代码已经较老（`v1.8`/`v1.14` 以及当时的 `master`），且部分代码
链接已失效；**本文代码基于 [`v1.21`](https://github.com/kubernetes/kubernetes/tree/v1.21.1)**。

由于内容已经不与原文一一对应（有增加和删减），因此标题未加 “[译]” 等字样。感谢原作者（们）的精彩文章。

篇幅太长，分成了几部分：

1. [源码解析：K8s 创建 pod 时，背后发生了什么（一）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-1-zh.md %})
1. [源码解析：K8s 创建 pod 时，背后发生了什么（二）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-2-zh.md %})
1. [源码解析：K8s 创建 pod 时，背后发生了什么（三）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-3-zh.md %})
1. [源码解析：K8s 创建 pod 时，背后发生了什么（四）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-4-zh.md %})
1. [源码解析：K8s 创建 pod 时，背后发生了什么（五）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-5-zh.md %})

----

* TOC
{:toc}

----

# 1 kubectl（命令行客户端）

## 1.0 调用栈概览

```
NewKubectlCommand                                    // staging/src/k8s.io/kubectl/pkg/cmd/cmd.go
 |-matchVersionConfig = NewMatchVersionFlags()
 |-f = cmdutil.NewFactory(matchVersionConfig)
 |      |-clientGetter = matchVersionConfig
 |-NewCmdRun(f)                                      // staging/src/k8s.io/kubectl/pkg/cmd/run/run.go
 |  |-Complete                                       // staging/src/k8s.io/kubectl/pkg/cmd/run/run.go
 |  |-Run(f)                                         // staging/src/k8s.io/kubectl/pkg/cmd/run/run.go
 |    |-validate parameters
 |    |-generators = GeneratorFn("run")
 |    |-runObj = createGeneratedObject(generators)   // staging/src/k8s.io/kubectl/pkg/cmd/run/run.go
 |    |           |-obj = generator.Generate()       // -> staging/src/k8s.io/kubectl/pkg/generate/versioned/run.go
 |    |           |        |-get pod params
 |    |           |        |-pod = v1.Pod{params}
 |    |           |        |-return &pod
 |    |           |-mapper = f.ToRESTMapper()        // -> staging/src/k8s.io/cli-runtime/pkg/genericclioptions/config_flags.go
 |    |           |  |-f.clientGetter.ToRESTMapper() // -> staging/src/k8s.io/kubectl/pkg/cmd/util/factory_client_access.go
 |    |           |     |-f.Delegate.ToRESTMapper()  // -> staging/src/k8s.io/kubectl/pkg/cmd/util/kubectl_match_version.go
 |    |           |        |-ToRESTMapper            // -> staging/src/k8s.io/cli-runtime/pkg/resource/builder.go
 |    |           |        |-delegate()              //    staging/src/k8s.io/cli-runtime/pkg/resource/builder.go
 |    |           |--actualObj = resource.NewHelper(mapping).XX.Create(obj)
 |    |-PrintObj(runObj.Object)
 |
 |-NewCmdEdit(f)      // kubectl edit   命令
 |-NewCmdScale(f)     // kubectl scale  命令
 |-NewCmdCordon(f)    // kubectl cordon 命令
 |-NewCmdUncordon(f)
 |-NewCmdDrain(f)
 |-NewCmdTaint(f)
 |-NewCmdExecute(f)
 |-...
```

## 1.1 参数验证（validation）和资源对象生成器（generator）

### 参数验证

敲下 `kubectl` 命令后，它首先会做一些**<mark>客户端侧</mark>**的验证。
如果命令行参数有问题，例如，[镜像名为空或格式不对](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/kubectl/pkg/cmd/run/run.go#L262)，
这里会直接报错，从而避免了将明显错误的请求发给 kube-apiserver，减轻了后者的压力。

此外，kubectl 还会检查其他一些配置，例如

* 是否需要记录（record）这条命令（用于 rollout 或审计）
* 是否是空跑（`--dry-run`）

### 创建 HTTP 请求

所有**查询或修改 K8s 资源的操作**都需要与 kube-apiserver 交互，后者会进一步和 etcd 通信。

因此，验证通过之后，kubectl 接下来会**<mark>创建发送给 kube-apiserver 的 HTTP 请求</mark>**。

### Generators

**<mark>创建 HTTP 请求用到了所谓的</mark>**
[generator](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/kubectl/pkg/cmd/run/run.go#L300)
（[文档](https://kubernetes.io/docs/user-guide/kubectl-conventions/#generators)）
，它**<mark>封装了资源的序列化（serialization）操作</mark>**。
例如，创建 pod 时用到的 generator 是 [`BasicPod`](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/kubectl/pkg/generate/versioned/run.go#L233)：

```go
// staging/src/k8s.io/kubectl/pkg/generate/versioned/run.go

type BasicPod struct{}

func (BasicPod) ParamNames() []generate.GeneratorParam {
    return []generate.GeneratorParam{
        {Name: "labels", Required: false},
        {Name: "name", Required: true},
        {Name: "image", Required: true},
        ...
    }
}
```

每个 generator 都实现了一个 `Generate()` 方法，用于**<mark>生成一个该资源的运行时对象（runtime object）</mark>**。
对于 `BasicPod`，其[实现](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/kubectl/pkg/generate/versioned/run.go#L259)为：

```go
func (BasicPod) Generate(genericParams map[string]interface{}) (runtime.Object, error) {
    pod := v1.Pod{
        ObjectMeta: metav1.ObjectMeta{  // metadata 字段
            Name:        name,
            Labels:      labels,
            ...
        },
        Spec: v1.PodSpec{               // spec 字段
            ServiceAccountName: params["serviceaccount"],
            Containers: []v1.Container{
                {
                    Name:            name,
                    Image:           params["image"]
                },
            },
        },
    }

    return &pod, nil
}
```

## 1.2 API group 和版本协商（version negotiation）

有了 runtime object 之后，kubectl 需要用合适的 API 将请求发送给 kube-apiserver。

### API Group

K8s 用 API group 来管理 resource API。
这是一种不同于 monolithic API（所有 API 扁平化）的 API 管理方式。

具体来说，**<mark>同一资源的不同版本的 API，会放到一个 group 里面</mark>**。
例如 Deployment 资源的 API group 名为 `apps`，最新的版本是 `v1`。这也是为什么
我们在创建 Deployment 时，需要在 yaml 中指定 `apiVersion: apps/v1` 的原因。

### 版本协商

生成 runtime object 之后，kubectl 就开始
[搜索合适的 API group 和版本](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/kubectl/pkg/cmd/run/run.go#L610-L619)：

```go
// staging/src/k8s.io/kubectl/pkg/cmd/run/run.go

    obj := generator.Generate(params) // 创建运行时对象
    mapper := f.ToRESTMapper()        // 寻找适合这个资源（对象）的 API group
```

然后[创建一个正确版本的客户端（versioned client）](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/kubectl/pkg/cmd/run/run.go#L641)，

```go
// staging/src/k8s.io/kubectl/pkg/cmd/run/run.go

    gvks, _ := scheme.Scheme.ObjectKinds(obj)
    mapping := mapper.RESTMapping(gvks[0].GroupKind(), gvks[0].Version)
```

这个客户端能感知资源的 REST 语义。

以上过程称为**<mark>版本协商</mark>**。在实现上，kubectl 会
**<mark>扫描 kube-apiserver 的 <code>/apis</code> 路径</mark>**
（OpenAPI 格式的 schema 文档），获取所有的 API groups。

出于性能考虑，kubectl 会
[缓存这份 OpenAPI schema](https://github.com/kubernetes/kubernetes/blob/v1.14.0/staging/src/k8s.io/cli-runtime/pkg/genericclioptions/config_flags.go#L234)，
路径是 `~/.kube/cache/discovery`。**<mark>想查看这个 API discovery 过程，可以删除这个文件</mark>**，
然后随便执行一条 kubectl 命令，并指定足够大的日志级别（例如 `kubectl get ds -v 10`）。

### 发送 HTTP 请求

现在有了 runtime object，也找到了正确的 API，因此接下来就是
将请求真正[发送出去](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/kubectl/pkg/cmd/run/run.go#L654)：

```go
// staging/src/k8s.io/kubectl/pkg/cmd/cmd.go

        actualObj = resource.
            NewHelper(client, mapping).
            DryRun(o.DryRunStrategy == cmdutil.DryRunServer).
            WithFieldManager(o.fieldManager).
            Create(o.Namespace, false, obj)
```

发送成功后，会以恰当的格式打印返回的消息。

## 1.3 客户端认证（client auth）

前面其实有意漏掉了一步：客户端认证。它发生在发送 HTTP 请求之前。

**<mark>用户凭证（credentials）一般都放在 kubeconfig 文件中，但这个文件可以位于多个位置</mark>**，
优先级从高到低：

- 命令行 `--kubeconfig <file>`
- 环境变量 `$KUBECONFIG`
- 某些[预定义的路径](https://github.com/kubernetes/client-go/blob/v1.21.0/tools/clientcmd/loader.go#L52)，例如 `~/.kube`。

**<mark>这个文件中存储了集群、用户认证等信息</mark>**，如下面所示：

```yaml
apiVersion: v1
clusters:
- cluster:
    certificate-authority: /etc/kubernetes/pki/ca.crt
    server: https://192.168.2.100:443
  name: k8s-cluster-1
contexts:
- context:
    cluster: k8s-cluster-1
    user: default-user
  name: default-context
current-context: default-context
kind: Config
preferences: {}
users:
- name: default-user
  user:
    client-certificate: /etc/kubernetes/pki/admin.crt
    client-key: /etc/kubernetes/pki/admin.key
```

有了这些信息之后，客户端就可以组装 HTTP 请求的认证头了。支持的认证方式有几种：

- **<mark>X509 证书</mark>**：放到 [TLS](https://github.com/kubernetes/client-go/blob/82aa063804cf055e16e8911250f888bc216e8b61/rest/transport.go#L80-L89) 中发送；
- **<mark>Bearer token</mark>**：放到 HTTP `"Authorization"` 头中
  [发送](https://github.com/kubernetes/client-go/blob/c6f8cf2c47d21d55fa0df928291b2580544886c8/transport/round_trippers.go#L314)；
- **<mark>用户名密码</mark>**：放到 HTTP basic auth
  [发送](https://github.com/kubernetes/client-go/blob/c6f8cf2c47d21d55fa0df928291b2580544886c8/transport/round_trippers.go#L223)；
- **<mark>OpenID Connect (OIDC)</mark>** 认证（例如和外部的 Keystone、Google 账号打通）：需要先由用户手动处理，将其转成一个 token，然后和 bearer token 类似发送。
