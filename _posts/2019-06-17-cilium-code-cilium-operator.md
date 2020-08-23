---
layout    : post
title     : "Cilium Code Walk Through: Cilium Operator"
date      : 2019-05-30
lastupdate: 2019-08-23
categories: cilium
---

* TOC
{:toc}

----

```shell
runOperator                                                 // operator/api.go
  |-startServer                                             // operator/api.go
  |-startSynchronizingServices                              // operator/k8s_service_sync.go
  | |-go JoinSharedStore(ClusterService{})                  // pkg/kvstore/store/store.go
  | |    |-listAndStartWatcher                              // pkg/kvstore/store/store.go
  | |      |-go s.watcher                                   // pkg/kvstore/store/store.go
  | |        |-updateKey                                    // pkg/kvstore/store/store.go
  | |          |-KeyCreator()                               // operator/k8s_service_sync.go
  | |            |-insert to k8sSvcCache
  | |-k8s.NewInformer(v1.Service).Run
  | |            |-insert to k8sSvcCache
  | |-k8s.NewInformer(v1.Endpoint).Run
  | |            |-insert to k8sSvcCache
  | |-k8sServiceHandler(k8sSvcCache)                        // operator/k8s_service_sync.go
  |   |-create/update/delete key from shared store
  |
  |-runNodeWatcher                                          // operator/k8s_node.go
  | |-JoinSharedStore                                       // pkg/kvstore/store/store.go
  | |    |-listAndStartWatcher                              // pkg/kvstore/store/store.go
  | |      |-go s.watcher                                   // pkg/kvstore/store/store.go
  | |        |-updateKey                                    // pkg/kvstore/store/store.go
  | |          |-KeyCreator()                               //
  | |            |-insert to cache
  | |-k8s.NewInformer(v1.Node).Run
  |    |-create/update/delete key from shared cache
  |
  |-startKvstoreWatchdog                                    // operator/kvstore_watchdog.go
  | |-for { RunLocksGC }
  | |-for { kvstore.Client().Update(HeartbeatPath) }
  |
  |-startKvstoreIdentityGC
  | |-RunGC
  |-enableCiliumEndpointSyncGC                              // operator/k8s_cep_gc.go
  | |-ciliumClient.CiliumEndpoints(cep.Namespace).Delete
  |-enableCNPWatcher
  |-enableCCNPWatcher
```

# 1 Introduction

This post walks through the implementation of cilium-operator.

Code based on `1.8.2`.

## 1.1 Cilium operator

<p align="center"><img src="/assets/img/cilium-code-cilium-operator/cilium-operator.png" width="70%" height="70%"></p>
<p align="center">Fig 1-1. Cilium operator</p>

What is Cilium operator? According to [Cilium documentation](https://docs.cilium.io/en/v1.5/concepts/#cilium-operator) [1]:

> The Cilium Operator is responsible for managing duties in the cluster which
> should logically be handled once for the entire cluster, rather than once for
> each node in the cluster. The Cilium operator is ***not in the critical path
> for any forwarding or network policy decision***. A cluster will generally
> ***continue to function if the operator is temporarily unavailable***.
> However, depending on the configuration, failure in availability of the
> operator can lead to:
>
> * Delays in IP Address Management (IPAM) and thus delay in scheduling of new
>   workloads if the operator is required to allocate new IP addresses
> * Failure to update the kvstore heartbeat key which will lead agents to declare
>   kvstore unhealthiness and restart.

that means,

* cilium-operator is a cluster-wide component, responsible for **cluster-scope**
  affairs, as comparison, cilium-agent manages **node-scope** stuffs
* if cilium-operator is down:
    * for existing Pods on any node, traffic forwarding or network policy
      decision will not be affected 
    * IPAM will be affected if you are using ENI mode (IPs are allocated by
      cilium-operator in this case)
    * health-check of kvstore (cilium-etcd) will be affected

## 1.2 Cilium etcd operator

It's important to distinguish cilium-operator from cilium-etcd-operator.
Cilium etcd operator is a [**Kubernetes operator**](https://coreos.com/operators/) [2]
implementation, which creates and maintains Cilium's builtin etcd cluster
(if using internal etcd mode).

Cilium operator, although also named "operator", actually has nothing to do
with **Kubernetes operator**. 

## 1.3 Call stack

The main call stack of cilium-operator is depicted at the beginning of this
post.

Cilium operator watches 4 kinds of K8S resources:

1. Service
1. Endpoint
1. Node
1. CNP (Cilium Network Policy)

# 2 `runOperator()`

```go
// operator/main.go

func runOperator(cmd *cobra.Command) {
    go startServer(k8sInitDone, ...)            // start health check handlers for itself

    switch ipamMode := Config.IPAM; ipamMode {  // if Cilium is used on AWS, Azure, etc
        ...
    }

    if kvstoreEnabled() {                       // cilium-etcd used
        if Config.SyncK8sServices
            startSynchronizingServices()        // sync Service

        kvstore.Setup(option.Config.KVStore)    // connect to kvstore (cilium-etcd)

        if Config.SyncK8sNodes
            runNodeWatcher(nodeManager)         // sync CiliumNode

        startKvstoreWatchdog()                  // perform identity GC and kvstore heartbeat
    }

    switch Config.IdentityAllocationMode {
    ...
    case IdentityAllocationModeKVstore:
        startKvstoreIdentityGC()                // perform identity GC
    }

    if Config.EnableCEPGC && Config.EndpointGCInterval != 0 {
        enableCiliumEndpointSyncGC()            // perform CiliumEndpoint GC
    }

    enableCNPWatcher(apiextensionsK8sClient)
    enableCCNPWatcher()
}
```

# 3 `startSynchronizingServices()`

The functionality of `startSynchronizingServices()` is: if there are
Service/Endpoint changes in k8s, synchronize them to kvstore (cilium-etcd). To
fulfill this task, it uses two variables:

* `k8sSvcCache`: a local cache of Services in k8s
* `kvs *store.SharedStore`: a local (merged) cache of Services (of all k8s
  clusters) in kvstore.  Services from all k8s clusters in a ClusterMesh will be
  merged into the so-called ClusterService
  (`ClusterSerivce=clustername/Service`).

```go
// operator/k8s_service_sync.go

var (
    k8sSvcCache = k8s.NewServiceCache(nil)
    kvs         *store.SharedStore
)

func startSynchronizingServices() {
    go func() {                                      // list ClusterService from kvstore, merge to kvs
        kvs = store.JoinSharedStore(Configuration{   // ClusterService is used by ClusterMesh
            Prefix: "cilium/states/services/v1" ,
            KeyCreator: func() store.Key { return &serviceStore.ClusterService{} },
            SynchronizationInterval: 5 * time.Minute,
        })
    }()

    svcController := informer.NewInformer(          // Watch for k8s v1.Service changes, save to k8sSvcCache
        cache.NewListWatchFromClient("services", v1.NamespaceAll),
        cache.ResourceEventHandlerFuncs{
            AddFunc: func(obj interface{}) {
                k8sSvc := k8s.ObjToV1Services(obj)
                k8sSvcCache.UpdateService(k8sSvc)
            },
            UpdateFunc: ...
            DeleteFunc: ...
        },
    )
    go svcController.Run(wait.NeverStop)

    switch { // We only enable either "Endpoints" or "EndpointSlice"
    case k8s.SupportsEndpointSlice():
        endpointController, endpointSliceEnabled = endpointSlicesInit(k8s.WatcherCli())
        fallthrough
    default:
        endpointController = endpointsInit(k8s.WatcherCli())
        go endpointController.Run(wait.NeverStop)    // Update endpoint changes to k8sSvcCache
    }

    go func() {
        k8sServiceHandler()  // handle kvs if there are changes in k8sSvcCache
    }()
}
```

## 3.1 `JoinSharedStore()`

This method will listen to the specified resource, merge them with locally, and
start a controller to continuously synchronize the local store with the kvstore
(cilium-etcd):

* on receiving create/update/delete events from kvstore, update the local store
  accordingly
* deletions of stale keys in kvstore rely on periodic GC jobs that is
  independent from this method

```go
// pkg/kvstore/store/store.go

// JoinSharedStore creates a new shared store based on the provided configuration.
// Starts a controller to continuously synchronize the store with the kvstore.
func JoinSharedStore(c Configuration) (*SharedStore, error) {
    s := &SharedStore{
        localKeys:  map[string]LocalKey{},
        sharedKeys: map[string]Key{},
    }
    s.name = "store-" + s.conf.Prefix
    s.controllerName = "kvstore-sync-" + s.name

    s.listAndStartWatcher();   // start watcher

    controllers.UpdateController(
        DoFunc: func(ctx context.Context) error { return s.syncLocalKeys(ctx) },
        RunInterval: s.conf.SynchronizationInterval,
    )
}
```

where,

```go
func (s *SharedStore) listAndStartWatcher() error {
    go s.watcher(listDone)
}
```

and what `watcher()` exactly does:

```go
func (s *SharedStore) watcher(listDone chan bool) {
    s.kvstoreWatcher = s.backend.ListAndWatch(s.name+"-watcher", s.conf.Prefix)

    for event := range s.kvstoreWatcher.Events {
        if event.Typ == ListDone { // Initial list of objects received from kvstore
            close(listDone)
            continue
        }

        keyName := strings.TrimPrefix(event.Key, s.conf.Prefix)
        if keyName[0] == '/'
            keyName = keyName[1:]

        switch event.Typ {
        case Create, Modify:
            s.updateKey(keyName, event.Value);  // insert into shared store, then notify observer
        case Delete:
            if localKey := s.lookupLocalKey(keyName); localKey != nil {
                s.syncLocalKey(s.conf.Context, localKey)
            } else {
                s.deleteSharedKey(keyName)
            }
        }
    }
}
```

`updateKey()` will call the `KeyCreator` method that has been registered in
`startSynchronizingServices()`, which will create a `ClusterService{}` instance:

```go
func (s *SharedStore) updateKey(name string, value []byte) error {
    newKey := s.conf.KeyCreator()
    newKey.Unmarshal(value)

    s.sharedKeys[name] = newKey

    s.onUpdate(newKey) // notify observer if there is
}
```

## 3.2 `endpointSlicesInit()` and `endpointsInit()`

These two methods watch for k8s `v1.Endpoints` changes and push changes into
local cache `k8sSvcCache`, let's look at the latter one:

```go
// operator/k8s_service_sync.go

func endpointsInit(k8sClient kubernetes.Interface) cache.Controller {
    endpointController := informer.NewInformer(
        cache.NewListWatchFromClient("endpoints", v1.NamespaceAll,
            // Don't get any events from kubernetes endpoints.
            fields.ParseSelectorOrDie("metadata.name!=kube-scheduler,metadata.name!=kube-controller-manager"),
        ),
        cache.ResourceEventHandlerFuncs{
            AddFunc: func(obj interface{}) {
                k8sEP := k8s.ObjToV1Endpoints(obj)
                k8sSvcCache.UpdateEndpoints(k8sEP, swgEps)
            },
            UpdateFunc: ...
            DeleteFunc: ...
        },
    )
    return endpointController
}
```

## 3.3 `k8sServiceHandler()`

When there are **Service/Endpoint changes in k8s** (local cache `k8sSvcCache`),
method `k8sServiceHandler()` will update the changes to **the shared store of
kvstore** (cilium-etcd's local cache):

```go
// operator/k8s_service_sync.go

func k8sServiceHandler() {
    serviceHandler := func(event k8s.ServiceEvent) {
        svc := k8s.NewClusterService(event.ID, event.Service, event.Endpoints)
        svc.Cluster = Config.ClusterName

        // Kubernetes service definition changed
        if !event.Service.Shared { // annotation may have been added, delete an eventual existing service
            kvs.DeleteLocalKey(context.TODO(), &svc)
            return
        }

        switch event.Action {      // k8s actions
        case k8s.UpdateService: kvs.UpdateLocalKeySync(&svc)
        case k8s.DeleteService: kvs.DeleteLocalKey(&svc)
        }
    }

    for {
        event, ok := <-k8sSvcCache.Events // k8s Service/Endpoint changed
        if !ok {
            return
        }
        serviceHandler(event)             // trigger update to local cache of kvstore
    }
}
```

# 4 `runNodeWatcher()`

Similar as `startSynchronizingServices()`, `runNodeWatcher()` synchronizing
`CiliumNode` resources from k8s to a local cache of cilium-etcd by
simultaneously listening to them.

Only ENI mode uses this (CiliumNode).

```go
// operator/k8s_node.go

func runNodeWatcher(nodeManager *allocator.NodeEventHandler) error {
    ciliumNodeStore := store.JoinSharedStore(Configuration{  // listen to kvstore, merge to local cache
        Prefix:     nodeStore.NodeStorePrefix,
        KeyCreator: nodeStore.KeyCreator,
    })

    k8sNodeStore, nodeController := informer.NewInformer(    // listen to k8s, save to local cache
        cache.NewListWatchFromClient("ciliumnodes", v1.NamespaceAll, fields.Everything()),
        cache.ResourceEventHandlerFuncs{
            AddFunc: func(obj interface{}) {
                ciliumNode := k8s.ObjToCiliumNode(obj)
                nodeNew := nodeTypes.ParseCiliumNode(ciliumNode)
                ciliumNodeStore.UpdateKeySync(&nodeNew)
            },
            ...
        },
    )
    go nodeController.Run(wait.NeverStop)

    go func() {
        listOfK8sNodes := k8sNodeStore.ListKeys()

        kvStoreNodes := ciliumNodeStore.SharedKeysMap()
        for k8sNode := range listOfK8sNodes { // The remaining kvStoreNodes are leftovers
            kvStoreNodeName := nodeTypes.GetKeyNodeName(option.Config.ClusterName, k8sNode)
            delete(kvStoreNodes, kvStoreNodeName)
        }

        for kvStoreNode := range kvStoreNodes {
            if strings.HasPrefix(kvStoreNode.GetKeyName(), option.Config.ClusterName)
                ciliumNodeStore.DeleteLocalKey(context.TODO(), kvStoreNode)
        }
    }()

    if Config.EnableCNPNodeStatusGC && Config.CNPNodeStatusGCInterval != 0
        go runCNPNodeStatusGC("cnp-node-gc", false, ciliumNodeStore)

    if Config.EnableCCNPNodeStatusGC && Config.CNPNodeStatusGCInterval != 0
        go runCNPNodeStatusGC("ccnp-node-gc", true, ciliumNodeStore)
}
```

# 5 `startKvstoreWatchdog()`

This method performs:

1. identity GC every lock lease (`25s`)
2. kvstore heartbeat (`1min`)

```go
// operator/kvstore_watchdog.go

func startKvstoreWatchdog() {
    backend := NewKVStoreBackend(cache.IdentitiesPath, ...)   // identities in kvstore
    a := allocator.NewAllocatorForGC(backend)

    keysToDelete := map[string]kvstore.Value{}
    go func() {
        for {
            keysToDelete = getOldestLeases(keysToDelete)
            keysToDelete2 := a.RunLocksGC(ctx, keysToDelete)  // perform GC
            keysToDelete = keysToDelete2
            <-time.After(defaults.LockLeaseTTL)               // 25s
        }
    }()

    go func() {
        for {
            kvstore.Client().Update(ctx, kvstore.HeartbeatPath, time.Now())
            <-time.After(kvstore.HeartbeatWriteInterval)      // 1min
        }
    }()
}
```

# 6 `startKvstoreIdentityGC()`

Rate-limited kvstore identity garbage collector, GC interval `IdentityGCInterval`:

```go
// operator/identity_gc.go

func startKvstoreIdentityGC() {
    backend := kvstoreallocator.NewKVStoreBackend(cache.IdentitiesPath)
    a := allocator.NewAllocatorForGC(backend)

    keysToDelete := map[string]uint64{}
    go func() {
        for {
            keysToDelete2 := a.RunGC(identityRateLimiter, keysToDelete)
            keysToDelete = keysToDelete2
            <-time.After(Config.IdentityGCInterval) // 25min

            log.WithFields({
                "identities-to-delete": keysToDelete,
            }).Debug("Will delete identities if they are still unused")
        }
    }()
}
```

# References

1. [Cilium Doc v1.8: Cilium Operator](https://docs.cilium.io/en/v1.8/concepts/overview/)
2. [Kubernetes Operator](https://coreos.com/operators/)
