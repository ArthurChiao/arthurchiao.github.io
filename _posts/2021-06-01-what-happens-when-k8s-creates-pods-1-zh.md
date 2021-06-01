---
layout    : post
title     : "源码解析：K8s 创建 pod 时，背后发生了什么（一）（2021）"
date      : 2021-06-01
lastupdate: 2021-06-01
categories: k8s cni
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

本文试图回答以下问题：**敲下 `kubectl run nginx --image=nginx --replicas=3` 命令后**，
**<mark>K8s 中发生了哪些事情？</mark>**

要弄清楚这个问题，我们需要：

1. 了解 k8s 几个核心组件的启动过程，它们分别做了哪些事情，以及
2. 从客户端发起请求到 pod ready 的整个过程。

# 0 K8s 组件启动过程

首先看几个核心组件的启动过程分别做了哪些事情。

## 0.1 kube-apiserver 启动

### 调用栈

创建命令行（`kube-apiserver`）入口：

```
main                                         // cmd/kube-apiserver/apiserver.go
 |-cmd := app.NewAPIServerCommand()          // cmd/kube-apiserver/app/server.go
 |  |-RunE := func() {
 |      Complete()
 |        |-ApplyAuthorization(s.Authorization)
 |        |-if TLS:
 |            ServiceAccounts.KeyFiles = []string{CertKey.KeyFile}
 |      Validate()
 |      Run(completedOptions, handlers) // 核心逻辑
 |    }
 |-cmd.Execute()
```

`kube-apiserver` 启动后，会执行到其中的 `Run()` 方法：

```
Run()          // cmd/kube-apiserver/app/server.go
 |-server = CreateServerChain()
 |           |-CreateKubeAPIServerConfig()
 |           |   |-buildGenericConfig
 |           |   |   |-genericapiserver.NewConfig()     // staging/src/k8s.io/apiserver/pkg/server/config.go
 |           |   |   |  |-return &Config{
 |           |   |   |       Serializer:             codecs,
 |           |   |   |       BuildHandlerChainFunc:  DefaultBuildHandlerChain, // 注册 handler
 |           |   |   |    } 
 |           |   |   |
 |           |   |   |-OpenAPIConfig = DefaultOpenAPIConfig()  // OpenAPI schema
 |           |   |   |-kubeapiserver.NewStorageFactoryConfig() // etcd 相关配置
 |           |   |   |-APIResourceConfig = genericConfig.MergedResourceConfig
 |           |   |   |-storageFactoryConfig.Complete(s.Etcd)
 |           |   |   |-storageFactory = completedStorageFactoryConfig.New()
 |           |   |   |-s.Etcd.ApplyWithStorageFactoryTo(storageFactory, genericConfig)
 |           |   |   |-BuildAuthorizer(s, genericConfig.EgressSelector, versionedInformers)
 |           |   |   |-pluginInitializers, admissionPostStartHook = admissionConfig.New()
 |           |   |
 |           |   |-capabilities.Initialize
 |           |   |-controlplane.ServiceIPRange()
 |           |   |-config := &controlplane.Config{}
 |           |   |-AddPostStartHook("start-kube-apiserver-admission-initializer", admissionPostStartHook)
 |           |   |-ServiceAccountIssuerURL = s.Authentication.ServiceAccounts.Issuer
 |           |   |-ServiceAccountJWKSURI = s.Authentication.ServiceAccounts.JWKSURI
 |           |   |-ServiceAccountPublicKeys = pubKeys
 |           |
 |           |-createAPIExtensionsServer
 |           |-CreateKubeAPIServer
 |           |-createAggregatorServer    // cmd/kube-apiserver/app/aggregator.go
 |           |   |-aggregatorConfig.Complete().NewWithDelegate(delegateAPIServer)   // staging/src/k8s.io/kube-aggregator/pkg/apiserver/apiserver.go
 |           |   |  |-apiGroupInfo := NewRESTStorage()
 |           |   |  |-GenericAPIServer.InstallAPIGroup(&apiGroupInfo)
 |           |   |  |-InstallAPIGroups
 |           |   |  |-openAPIModels := s.getOpenAPIModels(APIGroupPrefix, apiGroupInfos...)
 |           |   |  |-for apiGroupInfo := range apiGroupInfos {
 |           |   |  |   s.installAPIResources(APIGroupPrefix, apiGroupInfo, openAPIModels)
 |           |   |  |   s.DiscoveryGroupManager.AddGroup(apiGroup)
 |           |   |  |   s.Handler.GoRestfulContainer.Add(discovery.NewAPIGroupHandler(s.Serializer, apiGroup).WebService())
 |           |   |  |
 |           |   |  |-GenericAPIServer.Handler.NonGoRestfulMux.Handle("/apis", apisHandler)
 |           |   |  |-GenericAPIServer.Handler.NonGoRestfulMux.UnlistedHandle("/apis/", apisHandler)
 |           |   |  |-
 |           |   |-
 |-prepared = server.PrepareRun()     // staging/src/k8s.io/kube-aggregator/pkg/apiserver/apiserver.go
 |            |-GenericAPIServer.AddPostStartHookOrDie
 |            |-GenericAPIServer.PrepareRun
 |            |  |-routes.OpenAPI{}.Install()
 |            |     |-registerResourceHandlers // staging/src/k8s.io/apiserver/pkg/endpoints/installer.go
 |            |         |-POST: XX
 |            |         |-GET: XX
 |            |
 |            |-openapiaggregator.BuildAndRegisterAggregator()
 |            |-openapiaggregator.NewAggregationController()
 |            |-preparedAPIAggregator{}
 |-prepared.Run() // staging/src/k8s.io/kube-aggregator/pkg/apiserver/apiserver.go
    |-s.runnable.Run()
```

### 一些重要步骤

1. **创建 server chain**。Server aggregation（聚合）是一种支持多 apiserver 的方式，其中
   包括了一个 [generic apiserver](https://github.com/kubernetes/kubernetes/blob/v1.21.0/cmd/kube-apiserver/app/server.go#L219)，作为默认实现。
1. **<mark>生成 OpenAPI schema</mark>**，保存到 apiserver 的 [Config.OpenAPIConfig 字段](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/server/config.go#L167)。
1. 遍历 schema 中的所有 API group，为每个 API group 配置一个
   [storage provider](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/kube-aggregator/pkg/apiserver/apiserver.go#L204)，
   这是一个通用 backend 存储抽象层。
1. 遍历每个 group 版本，为每个 HTTP route
   [配置 REST mappings](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/endpoints/groupversion.go#L92)。
   稍后处理请求时，就能将 requests 匹配到合适的 handler。

## 0.2 controller-manager 启动

### 调用栈

```
NewDeploymentController
NewReplicaSetController
```

## 0.3 kubelet 启动

### 调用栈

```
main                                                                            // cmd/kubelet/kubelet.go
 |-NewKubeletCommand                                                            // cmd/kubelet/app/server.go
   |-Run                                                                        // cmd/kubelet/app/server.go
      |-initForOS                                                               // cmd/kubelet/app/server.go
      |-run                                                                     // cmd/kubelet/app/server.go
        |-initConfigz                                                           // cmd/kubelet/app/server.go
        |-InitCloudProvider
        |-NewContainerManager
        |-ApplyOOMScoreAdj
        |-PreInitRuntimeService
        |-RunKubelet                                                            // cmd/kubelet/app/server.go
        | |-k = createAndInitKubelet                                            // cmd/kubelet/app/server.go
        | |  |-NewMainKubelet
        | |  |  |-watch k8s Service
        | |  |  |-watch k8s Node
        | |  |  |-klet := &Kubelet{}
        | |  |  |-init klet fields
        | |  |
        | |  |-k.BirthCry()
        | |  |-k.StartGarbageCollection()
        | |
        | |-startKubelet(k)                                                     // cmd/kubelet/app/server.go
        |    |-go k.Run()                                                       // -> pkg/kubelet/kubelet.go
        |    |  |-go cloudResourceSyncManager.Run()
        |    |  |-initializeModules
        |    |  |-go volumeManager.Run()
        |    |  |-go nodeLeaseController.Run()
        |    |  |-initNetworkUtil() // setup iptables
        |    |  |-go Until(PerformPodKillingWork, 1*time.Second, neverStop)
        |    |  |-statusManager.Start()
        |    |  |-runtimeClassManager.Start
        |    |  |-pleg.Start()
        |    |  |-syncLoop(updates, kl)                                         // pkg/kubelet/kubelet.go
        |    |
        |    |-k.ListenAndServe
        |
        |-go http.ListenAndServe(healthz)
```

## 0.4 小结

以上核心组件启动完成后，就可以从命令行发起请求创建 pod 了。
