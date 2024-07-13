---
layout    : post
title     : "源码解析：K8s 创建 pod 时，背后发生了什么（一）（2021）"
date      : 2021-06-01
lastupdate: 2023-01-25
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
1. [图解 JuiceFS CSI 工作流：K8s 创建带 PV 的 Pod 时，背后发生了什么]({% link _posts/2024-07-13-k8s-juicefs-csi-workflow-zh.md %})

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----

<p align="center"><img src="/assets/img/what-happens-when-k8s-creates-pods/kube-scheduler.svg" width="100%" height="100%"></p>
<p align="center">Fig. What happens when create a Pod, image credit <a href="https://kubernetes.io/blog/2023/01/12/protect-mission-critical-pods-priorityclass/">Kubernetes Blog</a></p>

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
 |           |   |   |       BuildHandlerChainFunc:  DefaultBuildHandlerChain, // 注册 handler，例如 AuthN
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

一些重要步骤：

1. 注册命令行参数
1. **创建 server chain**。Server aggregation（聚合）是一种支持多 apiserver 的方式，其中
   包括了一个 [generic apiserver](https://github.com/kubernetes/kubernetes/blob/v1.21.0/cmd/kube-apiserver/app/server.go#L219)，作为默认实现。
1. **<mark>生成 OpenAPI schema</mark>**，保存到 apiserver 的 [Config.OpenAPIConfig 字段](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/server/config.go#L167)。
1. 遍历 schema 中的所有 API group，为每个 API group 配置一个
   [storage provider](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/kube-aggregator/pkg/apiserver/apiserver.go#L204)，
   这是一个通用 backend 存储抽象层。
1. 遍历每个 group 版本，为每个 HTTP route
   [配置 REST mappings](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/endpoints/groupversion.go#L92)。
   稍后处理请求时，就能将 requests 匹配到合适的 handler。

### 注册命令行参数

这里特别介绍下 AuthN 相关的配置，后面
[源码解析：K8s 创建 pod 时，背后发生了什么（三）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-3-zh.md %})
会用到：

```go
// https://github.com/kubernetes/kubernetes/blob/v1.23.1/pkg/kubeapiserver/options/authentication.go#L48

// BuiltInAuthenticationOptions contains all build-in authentication options for API Server
type BuiltInAuthenticationOptions struct {
    Anonymous       *AnonymousAuthenticationOptions
    BootstrapToken  *BootstrapTokenAuthenticationOptions
    ClientCert      *genericoptions.ClientCertAuthenticationOptions
    OIDC            *OIDCAuthenticationOptions
    RequestHeader   *genericoptions.RequestHeaderAuthenticationOptions
    ServiceAccounts *ServiceAccountAuthenticationOptions
    TokenFile       *TokenFileAuthenticationOptions
    WebHook         *WebHookAuthenticationOptions
    ...
}

// WithAll set default value for every build-in authentication option
func (o *BuiltInAuthenticationOptions) WithAll() *BuiltInAuthenticationOptions {
    return o.
        WithAnonymous().
        WithBootstrapToken().
        WithClientCert().
        WithOIDC().
        WithRequestHeader().
        WithServiceAccounts().
        WithTokenFile().
        WithWebHook()
}

// 注册各种 AuthN 相关的命令行参数到 API Server
func (o *BuiltInAuthenticationOptions) AddFlags(fs *pflag.FlagSet) {
    BoolVar("anonymous-auth")
    BoolVar("enable-bootstrap-token-auth")

    // Client certificate flags
    ClientCert.AddFlags
      |-StringVar("client-ca-file")

    // OIDC flags

    // Request header flags
    RequestHeader.AddFlags
      |-StringSliceVar("requestheader-username-headers") // e.g. `X-Remote-User`
      |-StringSliceVar("requestheader-group-headers")    // e.g. `X-Remote-Group`
      |-StringSliceVar("requestheader-extra-headers-prefix") // e.g. `X-Remote-Extra-`
      |-StringVar("requestheader-client-ca-file")
      |-StringSliceVar("requestheader-allowed-names")

    // ServiceAccount flags
    StringVar("service-account-key-file") // e.g. --service-account-key-file=/etc/kubernetes/pki/sa.pub

    // Token file
    StringVar("token-auth-file")

    // Webhook
}
```

K8s 支持多种认证方式，并且不同认证方式可以一起使用，这种情况下，任何一种方式认证成功就算成功。
因此，这些配置最终形成一个 authenticator list，例如，

* 如果指定了 `--service-account-key-file=/etc/kubernetes/pki/sa.pub`，就会将这个公钥加到这个列表；
* 如果指定了 `--client-ca-file`，就会将 x509 证书加到这个列表；
* 如果指定了 `--token-auth-file`，就会将 token 加到这个列表；

### 注册各种 handler

`NewConfig()` 里面会调用下面的方法注册认证、鉴权、审计等等各种 handler，

```go
// https://github.com/kubernetes/kubernetes/blob/v1.23.0/staging/src/k8s.io/apiserver/pkg/server/config.go#L764

func DefaultBuildHandlerChain(apiHandler http.Handler, c *Config) http.Handler {
    handler = genericapifilters.WithAuthorization(handler, c.Authorization.Authorizer, c.Serializer)
    handler = genericapifilters.WithAudit(handler, c.AuditBackend, c.AuditPolicyRuleEvaluator, c.LongRunningFunc)
    handler = genericapifilters.WithAuthentication(handler, c.Authentication.Authenticator, failedHandler, c.Authentication.APIAudiences)
    ...
}
```

例如 `WithAuthentication()` 注册成功之后，就会对客户端的每个请求执行认证。
[源码解析：K8s 创建 pod 时，背后发生了什么（三）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-3-zh.md %})
将有进一步介绍。

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

### `NewContainerManager()`

```go
// cmd/kubelet/app/server.go

func run() {
        if s.CgroupsPerQOS && s.CgroupRoot == "" {
            s.CgroupRoot = "/" // if --cgroups-per-qos enabled but --cgroup-root not specified, default to /
        }
    ...
        kubeDeps.ContainerManager = cm.NewContainerManager(
            kubeDeps.Mounter,
            kubeDeps.CAdvisorInterface,
            cm.NodeConfig{
                RuntimeCgroupsName:    s.RuntimeCgroups,
                SystemCgroupsName:     s.SystemCgroups,
                KubeletCgroupsName:    s.KubeletCgroups,
                KubeletOOMScoreAdj:    s.OOMScoreAdj,
                CgroupsPerQOS:         s.CgroupsPerQOS,
                CgroupRoot:            s.CgroupRoot,
                CgroupDriver:          s.CgroupDriver,
                KubeletRootDir:        s.RootDirectory,
                ProtectKernelDefaults: s.ProtectKernelDefaults,
                NodeAllocatableConfig: cm.NodeAllocatableConfig{
                    KubeReservedCgroupName:   s.KubeReservedCgroup,
                    SystemReservedCgroupName: s.SystemReservedCgroup,
                    EnforceNodeAllocatable:   sets.NewString(s.EnforceNodeAllocatable...),
                    KubeReserved:             kubeReserved,
                    SystemReserved:           systemReserved,
                    ReservedSystemCPUs:       reservedSystemCPUs,
                    HardEvictionThresholds:   hardEvictionThresholds,
                },
                QOSReserved:                              *experimentalQOSReserved,
                CPUManagerPolicy:                         s.CPUManagerPolicy,
                CPUManagerPolicyOptions:                  cpuManagerPolicyOptions,
                CPUManagerReconcilePeriod:                s.CPUManagerReconcilePeriod.Duration,
                ExperimentalMemoryManagerPolicy:          s.MemoryManagerPolicy,
                ExperimentalMemoryManagerReservedMemory:  s.ReservedMemory,
                ExperimentalPodPidsLimit:                 s.PodPidsLimit,
                EnforceCPULimits:                         s.CPUCFSQuota,
                CPUCFSQuotaPeriod:                        s.CPUCFSQuotaPeriod.Duration,
                ExperimentalTopologyManagerPolicy:        s.TopologyManagerPolicy,
                ExperimentalTopologyManagerScope:         s.TopologyManagerScope,
                ExperimentalTopologyManagerPolicyOptions: topologyManagerPolicyOptions,
            },
            s.FailSwapOn,
            kubeDeps.Recorder,
            kubeDeps.KubeClient,
        )
}
```

```go
// pkg/kubelet/cm/container_manager_linux.go

func NewContainerManager(mountUtil mount.Interface, cadvisorInterface cadvisor.Interface, nodeConfig NodeConfig, failSwapOn bool, recorder record.EventRecorder, kubeClient clientset.Interface) (ContainerManager, error) {
    subsystems := GetCgroupSubsystems()

    if failSwapOn { // Check whether swap is enabled. The Kubelet does not support running with swap enabled.
        swapFile := "/proc/swaps"
        swapData := os.ReadFile(swapFile)
        ...
    }

    machineInfo := cadvisorInterface.MachineInfo()
    capacity := cadvisor.CapacityFromMachineInfo(machineInfo)
    for k, v := range capacity {
        internalCapacity[k] = v
    }

    cgroupRoot    := ParseCgroupfsToCgroupName(nodeConfig.CgroupRoot)      // ""
    cgroupManager := NewCgroupManager(subsystems, nodeConfig.CgroupDriver) // "cgroupfs"

    if nodeConfig.CgroupsPerQOS { // true by default
        cgroupManager.Validate(cgroupRoot)
        cgroupRoot = NewCgroupName(cgroupRoot, defaultNodeAllocatableCgroupName) // -> "/kubepods"
    }
    Info("Creating Container Manager object based on Node Config", "nodeConfig", nodeConfig)

    qosContainerManager := NewQOSContainerManager(subsystems, cgroupRoot, nodeConfig, cgroupManager)

    cm := &containerManagerImpl{
        cadvisorInterface:   cadvisorInterface,
        mountUtil:           mountUtil,
        NodeConfig:          nodeConfig,
        subsystems:          subsystems,
        cgroupManager:       cgroupManager,
        capacity:            capacity,
        internalCapacity:    internalCapacity,
        cgroupRoot:          cgroupRoot,
        recorder:            recorder,
        qosContainerManager: qosContainerManager,
    }

    cm.topologyManager = topologymanager.NewManager()
    cm.deviceManager   = devicemanager.NewManagerImpl(machineInfo.Topology, cm.topologyManager)
    cm.draManager      = dra.NewManagerImpl(kubeClient) // initialize DRA manager
    cm.cpuManager      = cpumanager.NewManager() // Initialize CPU manager
    cm.memoryManager   = memorymanager.NewManager()

    return cm, nil
}
```

kubelet 要求必须关闭 swap，`cat /proc/swaps`。

## 0.4 小结

以上核心组件启动完成后，就可以从命令行发起请求创建 pod 了。
