---
layout    : post
title     : "源码解析：K8s 创建 pod 时，背后发生了什么（四）（2021）"
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

# 5 Control loops（控制循环）

至此，对象已经在 etcd 中了，所有的初始化步骤也已经完成了。
下一步是设置资源拓扑（resource topology）。例如，一个 Deployment 其实就是一组 ReplicaSet，而一个 ReplicaSet 就是一组 Pod。
K8s 是如何根据一个 HTTP 请求创建出这个层级关系的呢？靠的是 **<mark>K8s 内置的控制器</mark>**（controllers）。

K8s 中大量使用 "controllers"，

* 一个 controller 就是一个**<mark>异步脚本</mark>**（an asynchronous script），
* 不断检查资源的**当前状态**（current state）和**期望状态**（desired state）是否一致，
* 如果不一致就尝试将其变成期望状态，这个过程称为 **<mark>reconcile</mark>**。

每个 controller 负责的东西都比较少，**<mark>所有 controller 并行运行，
由 kube-controller-manager 统一管理</mark>**。

## 5.1 Deployments controller

### Deployments controller 启动

当一个 Deployment record 存储到 etcd 并（被 initializers）初始化之后，
kube-apiserver 就会将其置为对外可见的。此后，
Deployment controller 监听了 Deployment 资源的变动，因此此时就会检测到这个新创建的资源。

```go
// pkg/controller/deployment/deployment_controller.go

// NewDeploymentController creates a new DeploymentController.
func NewDeploymentController(dInformer DeploymentInformer, rsInformer ReplicaSetInformer,
    podInformer PodInformer, client clientset.Interface) (*DeploymentController, error) {

    dc := &DeploymentController{
        client:        client,
        queue:         workqueue.NewNamedRateLimitingQueue(),
    }
    dc.rsControl = controller.RealRSControl{ // ReplicaSet controller
        KubeClient: client,
        Recorder:   dc.eventRecorder,
    }

    // 注册 Deployment 事件回调函数
    dInformer.Informer().AddEventHandler(cache.ResourceEventHandlerFuncs{
        AddFunc:    dc.addDeployment,    // 有 Deployment 创建时触发
        UpdateFunc: dc.updateDeployment,
        DeleteFunc: dc.deleteDeployment,
    })
    // 注册 ReplicaSet 事件回调函数
    rsInformer.Informer().AddEventHandler(cache.ResourceEventHandlerFuncs{
        AddFunc:    dc.addReplicaSet,
        UpdateFunc: dc.updateReplicaSet,
        DeleteFunc: dc.deleteReplicaSet,
    })
    // 注册 Pod 事件回调函数
    podInformer.Informer().AddEventHandler(cache.ResourceEventHandlerFuncs{
        DeleteFunc: dc.deletePod,
    })

    dc.syncHandler = dc.syncDeployment
    dc.enqueueDeployment = dc.enqueue

    return dc, nil
}
```

### 创建 Deployment：回调函数处理

在本文场景中，触发的是 controller [注册的 addDeployment() 回调函数](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/controller/deployment/deployment_controller.go#L122)
其所做的工作就是将 deployment 对象放到一个内部队列：

```go
// pkg/controller/deployment/deployment_controller.go

func (dc *DeploymentController) addDeployment(obj interface{}) {
    d := obj.(*apps.Deployment)
    dc.enqueueDeployment(d)
}
```

### 主处理循环

worker 不断遍历这个 queue，从中 dequeue item 并进行处理：

```go
// pkg/controller/deployment/deployment_controller.go

func (dc *DeploymentController) worker() {
    for dc.processNextWorkItem() {
    }
}

func (dc *DeploymentController) processNextWorkItem() bool {
    key, quit := dc.queue.Get()
    dc.syncHandler(key.(string)) // dc.syncHandler = dc.syncDeployment
}

// syncDeployment will sync the deployment with the given key.
func (dc *DeploymentController) syncDeployment(key string) error {
    namespace, name := cache.SplitMetaNamespaceKey(key)

    deployment := dc.dLister.Deployments(namespace).Get(name)
    d := deployment.DeepCopy()

    // 获取这个 Deployment 的所有 ReplicaSets, while reconciling ControllerRef through adoption/orphaning.
    rsList := dc.getReplicaSetsForDeployment(d)

    // 获取这个 Deployment 的所有 pods, grouped by their ReplicaSet
    podMap := dc.getPodMapForDeployment(d, rsList)

    if d.DeletionTimestamp != nil { // 这个 Deployment 已经被标记，等待被删除
        return dc.syncStatusOnly(d, rsList)
    }

    dc.checkPausedConditions(d)
    if d.Spec.Paused { // pause 状态
        return dc.sync(d, rsList)
    }

    if getRollbackTo(d) != nil {
        return dc.rollback(d, rsList)
    }

    scalingEvent := dc.isScalingEvent(d, rsList)
    if scalingEvent {
        return dc.sync(d, rsList)
    }

    switch d.Spec.Strategy.Type {
    case RecreateDeploymentStrategyType:             // re-create
        return dc.rolloutRecreate(d, rsList, podMap)
    case RollingUpdateDeploymentStrategyType:        // rolling-update
        return dc.rolloutRolling(d, rsList)
    }
    return fmt.Errorf("unexpected deployment strategy type: %s", d.Spec.Strategy.Type)
}
```

controller 会通过 label selector 从 kube-apiserver 查询 
与这个 deployment 关联的 ReplicaSet 或 Pod records（然后发现没有）。

如果发现当前状态与预期状态不一致，就会触发同步过程（（synchronization process））。
这个同步过程是无状态的，也就是说，它并不区分是新记录还是老记录，一视同仁。

### 执行扩容（scale up）

如上，发现 pod 不存在之后，它会开始扩容过程（scaling process）：

```go
// pkg/controller/deployment/sync.go

// scale up/down 或新创建（pause）时都会执行到这里
func (dc *DeploymentController) sync(d *apps.Deployment, rsList []*apps.ReplicaSet) error {

    newRS, oldRSs := dc.getAllReplicaSetsAndSyncRevision(d, rsList, false)
    dc.scale(d, newRS, oldRSs)

    // Clean up the deployment when it's paused and no rollback is in flight.
    if d.Spec.Paused && getRollbackTo(d) == nil {
        dc.cleanupDeployment(oldRSs, d)
    }

    allRSs := append(oldRSs, newRS)
    return dc.syncDeploymentStatus(allRSs, newRS, d)
}
```

大致步骤：

1. Rolling out (例如 creating）一个 ReplicaSet resource
2. 分配一个 label selector
3. 初始版本好（revision number）置为 1

ReplicaSet 的 PodSpec，以及其他一些 metadata  是从 Deployment 的 manifest 拷过来的。

最后会更新 deployment 状态，然后重新进入
reconciliation 循环，直到 deployment 进入预期的状态。

### 小结

由于 **<mark>Deployment controller 只负责 ReplicaSet 的创建</mark>**，因此下一步
（ReplicaSet -> Pod）要由 reconciliation 过程中的另一个 controller —— ReplicaSet controller 来完成。

## 5.2 ReplicaSets controller

上一步周，Deployments controller 已经创建了 Deployment 的第一个
ReplicaSet，但此时还没有任何 Pod。 下面就轮到 ReplicaSet controller 出场了。
它的任务是监控 ReplicaSet 及其依赖资源（pods）的生命周期，实现方式也是注册事件回调函数。

### ReplicaSets controller 启动

```go
// pkg/controller/replicaset/replica_set.go

func NewReplicaSetController(rsInformer ReplicaSetInformer, podInformer PodInformer,
    kubeClient clientset.Interface, burstReplicas int) *ReplicaSetController {

    return NewBaseController(rsInformer, podInformer, kubeClient, burstReplicas,
        apps.SchemeGroupVersion.WithKind("ReplicaSet"),
        "replicaset_controller",
        "replicaset",
        controller.RealPodControl{
            KubeClient: kubeClient,
        },
    )
}

// 抽象出 NewBaseController() 是为了代码复用，例如 NewReplicationController() 也会调用这个函数。
func NewBaseController(rsInformer, podInformer, kubeClient clientset.Interface, burstReplicas int,
    gvk GroupVersionKind, metricOwnerName, queueName, podControl PodControlInterface) *ReplicaSetController {

    rsc := &ReplicaSetController{
        kubeClient:       kubeClient,
        podControl:       podControl,
        burstReplicas:    burstReplicas,
        expectations:     controller.NewUIDTrackingControllerExpectations(NewControllerExpectations()),
        queue:            workqueue.NewNamedRateLimitingQueue()
    }

    rsInformer.Informer().AddEventHandler(cache.ResourceEventHandlerFuncs{
        AddFunc:    rsc.addRS,
        UpdateFunc: rsc.updateRS,
        DeleteFunc: rsc.deleteRS,
    })
    rsc.rsLister = rsInformer.Lister()

    podInformer.Informer().AddEventHandler(cache.ResourceEventHandlerFuncs{
        AddFunc: rsc.addPod,
        UpdateFunc: rsc.updatePod,
        DeleteFunc: rsc.deletePod,
    })
    rsc.podLister = podInformer.Lister()

    rsc.syncHandler = rsc.syncReplicaSet
    return rsc
}
```

### 创建 ReplicaSet：回调函数处理

### 主处理循环

当一个 ReplicaSet 被（Deployment controller）创建之后，

```go
// pkg/controller/replicaset/replica_set.go

// syncReplicaSet will sync the ReplicaSet with the given key if it has had its expectations fulfilled,
// meaning it did not expect to see any more of its pods created or deleted.
func (rsc *ReplicaSetController) syncReplicaSet(key string) error {

    namespace, name := cache.SplitMetaNamespaceKey(key)
    rs := rsc.rsLister.ReplicaSets(namespace).Get(name)

    selector := metav1.LabelSelectorAsSelector(rs.Spec.Selector)

    // 包括那些不匹配 rs selector，但有 stale controller ref 的 pod
    allPods := rsc.podLister.Pods(rs.Namespace).List(labels.Everything())
    filteredPods := controller.FilterActivePods(allPods) // Ignore inactive pods.
    filteredPods = rsc.claimPods(rs, selector, filteredPods)

    if rsNeedsSync && rs.DeletionTimestamp == nil { // 需要同步，并且没有被标记待删除
        rsc.manageReplicas(filteredPods, rs)        // *主处理逻辑*
    }

    newStatus := calculateStatus(rs, filteredPods, manageReplicasErr)
    updatedRS := updateReplicaSetStatus(AppsV1().ReplicaSets(rs.Namespace), rs, newStatus)
}
```

RS controller 检查 ReplicaSet 的状态，
发现当前状态和期望状态之间有偏差（skew），因此接下来调用 `manageReplicas()` 来 reconcile
这个状态，在这里做的事情就是增加这个 ReplicaSet 的 pod 数量。

```go
// pkg/controller/replicaset/replica_set.go

func (rsc *ReplicaSetController) manageReplicas(filteredPods []*v1.Pod, rs *apps.ReplicaSet) error {
    diff := len(filteredPods) - int(*(rs.Spec.Replicas))
    rsKey := controller.KeyFunc(rs)

    if diff < 0 {
        diff *= -1
        if diff > rsc.burstReplicas {
            diff = rsc.burstReplicas
        }

        rsc.expectations.ExpectCreations(rsKey, diff)
        successfulCreations := slowStartBatch(diff, controller.SlowStartInitialBatchSize, func() {
            return rsc.podControl.CreatePodsWithControllerRef( // 扩容
                // 调用栈 CreatePodsWithControllerRef -> createPod() -> Client.CoreV1().Pods().Create()
                rs.Namespace, &rs.Spec.Template, rs, metav1.NewControllerRef(rs, rsc.GroupVersionKind))
        })

        // The skipped pods will be retried later. The next controller resync will retry the slow start process.
        if skippedPods := diff - successfulCreations; skippedPods > 0 {
            for i := 0; i < skippedPods; i++ {
                // Decrement the expected number of creates because the informer won't observe this pod
                rsc.expectations.CreationObserved(rsKey)
            }
        }
        return err
    } else if diff > 0 {
        if diff > rsc.burstReplicas {
            diff = rsc.burstReplicas
        }

        relatedPods := rsc.getIndirectlyRelatedPods(rs)
        podsToDelete := getPodsToDelete(filteredPods, relatedPods, diff)
        rsc.expectations.ExpectDeletions(rsKey, getPodKeys(podsToDelete))

        for _, pod := range podsToDelete {
            go func(targetPod *v1.Pod) {
                rsc.podControl.DeletePod(rs.Namespace, targetPod.Name, rs) // 缩容
            }(pod)
        }
    }

    return nil
}
```

增加 pod 数量的操作比较小心，每次最多不超过 burst count（这个配置是从 ReplicaSet 的父对象 Deployment 那里继承来的）。

另外，创建 Pods 的过程是 [批处理的](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/controller/replicaset/replica_set.go#L487),
“慢启动”操，开始时是 `SlowStartInitialBatchSize`，每执行成功一批，下次的 batch size 就翻倍。
这样设计是为了避免给 kube-apiserver 造成不必要的压力，例如，如果由于 quota 不足，这批 pod 大部分都会失败，那
这种方式只会有一小批请求到达 kube-apiserver，而如果一把全上的话，请求全部会打过去。
同样是失败，这种失败方式比较优雅。

### Owner reference

K8s **<mark>通过 Owner Reference</mark>**（子资源中的一个字段，指向的是其父资源的 ID）
**维护对象层级**（hierarchy）。这可以带来两方面好处：

1. 实现了 cascading deletion，即父对象被 GC 时会确保 GC 子对象；
2. 父对象之间不会出现竞争子对象的情况（例如，两个父对象认为某个子对象都是自己的）

另一个隐藏的好处是：Owner Reference 是有状态的：如果 controller 重启，重启期间不会影响
系统的其他部分，因为资源拓扑（resource topology）是独立于 controller 的。
这种隔离设计也体现在 controller 自己的设计中：**<mark>controller 不应该操作
其他 controller 的资源</mark>**（resources they don't explicitly own）。

有时也可能会出现“孤儿”资源（"orphaned" resources）的情况，例如

1. 父资源删除了，子资源还在；
2. GC 策略导致子资源无法被删除。

这种情况发生时，**<mark>controller 会确保孤儿资源会被某个新的父资源收养</mark>**。
多个父资源都可以竞争成为孤儿资源的父资源，但只有一个会成功（其余的会收到一个 validation 错误）。

## 5.3 Informers

很多 controller（例如 RBAC authorizer 或 Deployment controller）需要将集群信息拉到本地。

例如 RBAC authorizer 中，authenticator 会将用户信息保存到请求上下文中。随后，
RBAC authorizer 会用这个信息获取 etcd 中所有与这个用户相关的 role 和 role bindings。

那么，controller 是如何访问和修改这些资源的？在 K8s 中，这是通过 informer 机制实现的。

**<mark>informer 是一种 controller 订阅存储（etcd）事件的机制</mark>**，能方便地获取它们感兴趣的资源。

* 这种方式除了提供一种很好的抽象之外，还负责处理缓存（caching，非常重要，因为可
  以减少 kube-apiserver 连接数，降低 controller 测和 kube-apiserver 侧的序列化
  成本）问题。
* 此外，这种设计还使得 controller 的行为是 threadsafe 的，避免影响其他组件或服务。

关于 informer 和 controller 的联合工作机制，可参考
[这篇博客](http://borismattijssen.github.io/articles/kubernetes-informers-controllers-reflectors-stores)。

## 5.4 Scheduler（调度器）

以上 controllers 执行完各自的处理之后，etcd 中已经有了一个 Deployment、一个 ReplicaSet
和三个 Pods，可以通过 kube-apiserver 查询到。
但此时，**<mark>这三个 pod 还卡在 Pending 状态，因为它们还没有被调度到任何节点</mark>**。
**另外一个 controller —— 调度器** —— 负责做这件事情。

scheduler 作为控制平面的一个独立服务运行，但**<mark>工作方式与其他 controller 是一样的</mark>**：
监听事件，然后尝试 reconcile 状态。

### 调用栈概览

```
Run // pkg/scheduler/scheduler.go 
  |-SchedulingQueue.Run()
  |
  |-scheduleOne()
     |-bind
     |  |-RunBindPlugins
     |     |-runBindPlugins
     |        |-Bind
     |-sched.Algorithm.Schedule(pod)
        |-findNodesThatFitPod
        |-prioritizeNodes
        |-selectHost
```


### 调度过程

```go
// pkg/scheduler/core/generic_scheduler.go

// 将 pod 调度到指定 node list 中的某台 node 上
func (g *genericScheduler) Schedule(ctx context.Context, fwk framework.Framework,
    state *framework.CycleState, pod *v1.Pod) (result ScheduleResult, err error) {

    feasibleNodes, diagnosis := g.findNodesThatFitPod(ctx, fwk, state, pod) // 过滤可用 nodes
    if len(feasibleNodes) == 0 {
        return result, &framework.FitError{}
    }

    if len(feasibleNodes) == 1 { // 可用 node 只有一个，就选它了
        return ScheduleResult{SuggestedHost:  feasibleNodes[0].Name}, nil
    }

    priorityList := g.prioritizeNodes(ctx, fwk, state, pod, feasibleNodes)
    host := g.selectHost(priorityList)

    return ScheduleResult{
        SuggestedHost:  host,
        EvaluatedNodes: len(feasibleNodes) + len(diagnosis.NodeToStatusMap),
        FeasibleNodes:  len(feasibleNodes),
    }, err
}

// Filters nodes that fit the pod based on the framework filter plugins and filter extenders.
func (g *genericScheduler) findNodesThatFitPod(ctx context.Context, fwk framework.Framework,
    state *framework.CycleState, pod *v1.Pod) ([]*v1.Node, framework.Diagnosis, error) {

    diagnosis := framework.Diagnosis{
        NodeToStatusMap:      make(framework.NodeToStatusMap),
        UnschedulablePlugins: sets.NewString(),
    }

    // Run "prefilter" plugins.
    s := fwk.RunPreFilterPlugins(ctx, state, pod)
    allNodes := g.nodeInfoSnapshot.NodeInfos().List()

    if len(pod.Status.NominatedNodeName) > 0 && featureGate.Enabled(features.PreferNominatedNode) {
        feasibleNodes := g.evaluateNominatedNode(ctx, pod, fwk, state, diagnosis)
        if len(feasibleNodes) != 0 {
            return feasibleNodes, diagnosis, nil
        }
    }

    feasibleNodes := g.findNodesThatPassFilters(ctx, fwk, state, pod, diagnosis, allNodes)
    feasibleNodes = g.findNodesThatPassExtenders(pod, feasibleNodes, diagnosis.NodeToStatusMap)
    return feasibleNodes, diagnosis, nil
}
```

它会过滤 [过滤 PodSpect 中 NodeName 字段为空的 pods](https://github.com/kubernetes/kubernetes/blob/v1.21.0/plugin/pkg/scheduler/factory/factory.go#L190)
，尝试为这样的 pods 挑选一个 node 调度上去。

### 调度算法

下面简单看下内置的默认调度算法。

#### 注册默认 predicates

这些 predicates 其实都是函数，被调用到时，执行相应的
[过滤](https://github.com/kubernetes/kubernetes/blob/v1.21.0/plugin/pkg/scheduler/core/generic_scheduler.go#L117)。
例如，**<mark>如果 PodSpec 里面显式要求了 CPU 或 RAM 资源，而一个 node 无法满足这些条件</mark>**，
那就会将这个 node 从备选列表中删除。

```go
// pkg/scheduler/algorithmprovider/registry.go

// NewRegistry returns an algorithm provider registry instance.
func NewRegistry() Registry {
    defaultConfig := getDefaultConfig()
    applyFeatureGates(defaultConfig)

    caConfig := getClusterAutoscalerConfig()
    applyFeatureGates(caConfig)

    return Registry{
        schedulerapi.SchedulerDefaultProviderName: defaultConfig,
        ClusterAutoscalerProvider:                 caConfig,
    }
}

func getDefaultConfig() *schedulerapi.Plugins {
    plugins := &schedulerapi.Plugins{
        PreFilter: schedulerapi.PluginSet{...},
        Filter: schedulerapi.PluginSet{
            Enabled: []schedulerapi.Plugin{
                {Name: nodename.Name},        // 指定 node name 调度
                {Name: tainttoleration.Name}, // 指定 toleration 调度
                {Name: nodeaffinity.Name},    // 指定 node affinity 调度
                ...
            },
        },
        PostFilter: schedulerapi.PluginSet{...},
        PreScore: schedulerapi.PluginSet{...},
        Score: schedulerapi.PluginSet{
            Enabled: []schedulerapi.Plugin{
                {Name: interpodaffinity.Name, Weight: 1},
                {Name: nodeaffinity.Name, Weight: 1},
                {Name: tainttoleration.Name, Weight: 1},
                ...
            },
        },
        Reserve: schedulerapi.PluginSet{...},
        PreBind: schedulerapi.PluginSet{...},
        Bind: schedulerapi.PluginSet{...},
    }

    return plugins
}
```

plugin 的实现见 `pkg/scheduler/framework/plugins/`，以 `nodename` filter 为例：

```go
// pkg/scheduler/framework/plugins/nodename/node_name.go

// Filter invoked at the filter extension point.
func (pl *NodeName) Filter(ctx context.Context, pod *v1.Pod, nodeInfo *framework.NodeInfo) *framework.Status {
    if !Fits(pod, nodeInfo) {
        return framework.NewStatus(UnschedulableAndUnresolvable, ErrReason)
    }
    return nil
}

// 如果 pod 没有指定 NodeName，或者指定的 NodeName 等于该 node 的 name，返回 true；其他返回 false
func Fits(pod *v1.Pod, nodeInfo *framework.NodeInfo) bool {
    return len(pod.Spec.NodeName) == 0 || pod.Spec.NodeName == nodeInfo.Node().Name
}
```

#### 对筛选出的 node 排序

选择了合适的 nodes 之后，接下来会执行一系列 priority function **<mark>对这些 nodes 进行排序</mark>**。
例如，如果算法是希望将 pods 尽量分散到整个集群，那 priority
会选择资源尽量空闲的节点。

这些函数会给每个 node 打分，**<mark>得分最高的 node 会被选中</mark>**，调度到该节点。

```go
// pkg/scheduler/core/generic_scheduler.go

// 运行打分插件（score plugins）对 nodes 进行排序。
func (g *genericScheduler) prioritizeNodes(ctx context.Context, fwk framework.Framework,
    state *framework.CycleState, pod *v1.Pod, nodes []*v1.Node,) (framework.NodeScoreList, error) {

    // 如果没有指定 priority 配置，所有 node 将都得 1 分。
    if len(g.extenders) == 0 && !fwk.HasScorePlugins() {
        result := make(framework.NodeScoreList, 0, len(nodes))
        for i := range nodes {
            result = append(result, framework.NodeScore{ Name:  nodes[i].Name, Score: 1 })
        }
        return result, nil
    }

    preScoreStatus := fwk.RunPreScorePlugins(ctx, state, pod, nodes)       // PreScoe 插件
    scoresMap, scoreStatus := fwk.RunScorePlugins(ctx, state, pod, nodes)  // Score 插件

    result := make(framework.NodeScoreList, 0, len(nodes))
    for i := range nodes {
        result = append(result, framework.NodeScore{Name: nodes[i].Name, Score: 0})
        for j := range scoresMap {
            result[i].Score += scoresMap[j][i].Score
        }
    }

    if len(g.extenders) != 0 && nodes != nil {
        combinedScores := make(map[string]int64, len(nodes))
        for i := range g.extenders {
            if !g.extenders[i].IsInterested(pod) {
                continue
            }
            go func(extIndex int) {
                prioritizedList, weight := g.extenders[extIndex].Prioritize(pod, nodes)
                for i := range *prioritizedList {
                    host, score := (*prioritizedList)[i].Host, (*prioritizedList)[i].Score
                    combinedScores[host] += score * weight
                }
            }(i)
        }

        for i := range result {
            result[i].Score += combinedScores[result[i].Name] * (MaxNodeScore / MaxExtenderPriority)
        }
    }

    return result, nil
}
```

### 创建 `v1.Binding` 对象

算法选出一个 node 之后，调度器会
[创建一个 Binding 对象](https://github.com/kubernetes/kubernetes/blob/v1.21.0/plugin/pkg/scheduler/scheduler.go#L336-L342)，
Pod 的 **<mark>ObjectReference 字段的值就是选中的 node 的名字</mark>**。

```go
// pkg/scheduler/framework/runtime/framework.go

func (f *frameworkImpl) runBindPlugin(ctx context.Context, bp BindPlugin, state *CycleState,
    pod *v1.Pod, nodeName string) *framework.Status {

    if !state.ShouldRecordPluginMetrics() {
        return bp.Bind(ctx, state, pod, nodeName)
    }

    status := bp.Bind(ctx, state, pod, nodeName)
    return status
}
```

```go
// pkg/scheduler/framework/plugins/defaultbinder/default_binder.go

// Bind binds pods to nodes using the k8s client.
func (b DefaultBinder) Bind(ctx, state *CycleState, p *v1.Pod, nodeName string) *framework.Status {
    binding := &v1.Binding{
        ObjectMeta: metav1.ObjectMeta{Namespace: p.Namespace, Name: p.Name, UID: p.UID},
        Target:     v1.ObjectReference{Kind: "Node", Name: nodeName}, // ObjectReference 字段为 nodeName
    }

    b.handle.ClientSet().CoreV1().Pods(binding.Namespace).Bind(ctx, binding, metav1.CreateOptions{})
}
```

如上，最后 `ClientSet().CoreV1().Pods(binding.Namespace).Bind()` 通过一个 **<mark>POST 请求发给 apiserver</mark>**。

### kube-apiserver 更新 pod 对象

kube-apiserver 收到这个 Binding object 请求后，registry 反序列化对象，更新 Pod 对象的下列字段：

* 设置 NodeName
* 添加 annotations
* 设置 `PodScheduled` status 为 `True`

```go
// pkg/registry/core/pod/storage/storage.go

func (r *BindingREST) setPodHostAndAnnotations(ctx context.Context, podID, oldMachine, machine string,
    annotations map[string]string, dryRun bool) (finalPod *api.Pod, err error) {

    podKey := r.store.KeyFunc(ctx, podID)
    r.store.Storage.GuaranteedUpdate(ctx, podKey, &api.Pod{}, false, nil,
        storage.SimpleUpdate(func(obj runtime.Object) (runtime.Object, error) {

        pod, ok := obj.(*api.Pod)
        pod.Spec.NodeName = machine
        if pod.Annotations == nil {
            pod.Annotations = make(map[string]string)
        }
        for k, v := range annotations {
            pod.Annotations[k] = v
        }
        podutil.UpdatePodCondition(&pod.Status, &api.PodCondition{
            Type:   api.PodScheduled,
            Status: api.ConditionTrue,
        })

        return pod, nil
    }), dryRun, nil)
}
```

### 自定义调度器

> predicate 和 priority function 都是可扩展的，可以通过 `--policy-config-file` 指定。
>
> K8s 还可以自定义调度器（自己实现调度逻辑）。
> **<mark>如果 PodSpec 中 schedulerName 字段不为空</mark>**，K8s 就会
> 将这个 pod 的调度权交给指定的调度器。

## 5.5 小结

总结一下前面已经完成的步骤：

1. HTTP 请求通过了认证、鉴权、admission control
2. Deployment, ReplicaSet 和 Pod resources 已经持久化到 etcd
3. 一系列 initializers 已经执行完毕，
4. 每个 Pod 也已经调度到了合适的 node 上。

但是，**<mark>到目前为止，我们看到的所有东西（状态），还只是存在于 etcd 中的元数据</mark>**。
下一步就是将这些状态同步到计算节点上，然后计算节点上的 agent（kubelet）就开始干活了。
