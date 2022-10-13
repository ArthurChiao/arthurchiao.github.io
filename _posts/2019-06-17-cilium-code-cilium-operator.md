---
layout    : post
title     : "Cilium Code Walk Through: Cilium Operator"
date      : 2019-05-30
lastupdate: 2022-10-13
categories: cilium
---

This post walks you through the `cilium-operator` component. Code based on Cilium `1.8 ~ 1.10`.

This post is included in
[Cilium Code Walk Through Series]({% link _posts/2019-06-17-cilium-code-series.md %}).

----

* TOC
{:toc}

----

# 1 Introduction

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

```
init                                                 // operator/flags.go
 |-cobra.OnInitialize(option.InitConfig())           // pkg/option/config.go
    |-option.InitConfig()                            // pkg/option/config.go
      |-ReadDirConfig
      |-MergeConfig
         |-viper.MergeConfigMap

runOperator
 |-onOperatorStartLeading

onOperatorStartLeading
  |-alloc := allocatorProviders[ipamMode]                         // Get IPAM (e.g. vendor specific IPAM)
  |-alloc.Init(ctx)                                               // Init IPAM allocator
  |-nodeManager := alloc.Start(&ciliumNodeUpdateImplementation{}) //
  |                       |-instancesAPIResync
  |                          |-instancesAPIs.Resync()             // vendor specific handler
  |                              |-m.vpcs      = vendorAPI.GetVPCs()
  |                              |-m.subnets   = vendorAPI.GetSubnets()
  |                              |-m.instances = vendorAPI.GetInstances()
  |
  |-startSynchronizingCiliumNodes(nodeManager)                    // maintain ENI/IP pool for node
  |  |-ciliumNodeStore, ciliumNodeInformer = NewInformer(
  |  |  cache.ResourceEventHandlerFuncs{
  |  |      AddFunc: func(obj interface{}) {
  |  |          nodeManager.Create(k8s.ObjToCiliumNode(obj))
  |  |      },
  |  |      UpdateFunc: func(oldObj, newObj interface{}) {... },
  |  |      DeleteFunc: func(obj interface{}) {
  |  |          nodeManager.Delete(k8s.ObjToCiliumNode(obj))
  |  |      },
  |  |  },
  |  | )
  |  |
  |  |-go ciliumNodeInformer.Run(wait.NeverStop)
  |
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
  |  |-go func() {
  |  |   for {
  |  |     RunLocksGC // GC lock-files in kvstore
  |  | }}()
  |  |
  |  |-go func() {
  |      for {
  |        kvstore.Client().Update(HeartbeatPath)
  |    }}()
  |
  |-startKvstoreIdentityGC
  |  |-allocator.RunGC
  |    |-allocator.RunGC
  |       |-backend.RunGC(staleKeyPrevRound)                // pkg/kvstore/allocator/allocator.go
  |          |-allocated := k.backend.ListPrefix()
  |          |-for key, v in allocated:
  |
  |-enableCiliumEndpointSyncGC                              // operator/k8s_cep_gc.go
  | |-ciliumClient.CiliumEndpoints(cep.Namespace).Delete
  |-enableCNPWatcher
  |-enableCCNPWatcher
```

Cilium operator watches 4 kinds of K8S resources:

1. Service
1. Endpoint
1. Node
1. CNP (Cilium Network Policy)

# 2 `runOperator()`

```go
// operator/main.go

func runOperator(cmd *cobra.Command) {
    k8sInitDone := make(chan struct{})
    isLeader.Store(false)

    // Configure API server for the operator.
    srv := api.NewServer(shutdownSignal, k8sInitDone, getAPIServerAddr()...)

    go func() {
        srv.WithStatusCheckFunc(checkStatus).StartServer()
    }()

    initK8s(k8sInitDone)

    // Register the CRDs after validating that we are running on a supported version of K8s.
    client.RegisterCRDs(); err != nil {

    operatorID := os.Hostname()
    operatorID = rand.RandomStringWithPrefix(operatorID+"-", 10)

    ns := option.Config.K8sNamespace
    // If due to any reason the CILIUM_K8S_NAMESPACE is not set we assume the operator
    // to be in default namespace.
    if ns == "" {
        ns = metav1.NamespaceDefault
    }

    leResourceLock := &resourcelock.LeaseLock{
        LeaseMeta: metav1.ObjectMeta{
            Name:      leaderElectionResourceLockName,
            Namespace: ns,
        },
        Client: k8s.Client().CoordinationV1(),
        LockConfig: resourcelock.ResourceLockConfig{
            // Identity name of the lock holder
            Identity: operatorID,
        },
    }

    // Start the leader election for running cilium-operators
    leaderelection.RunOrDie(leaderElectionCtx, leaderelection.LeaderElectionConfig{
        Name: leaderElectionResourceLockName,

        Lock:            leResourceLock,
        ReleaseOnCancel: true,

        LeaseDuration: operatorOption.Config.LeaderElectionLeaseDuration,
        RenewDeadline: operatorOption.Config.LeaderElectionRenewDeadline,
        RetryPeriod:   operatorOption.Config.LeaderElectionRetryPeriod,

        Callbacks: leaderelection.LeaderCallbacks{
            OnStartedLeading: onOperatorStartLeading,       // start working as leader
            OnStoppedLeading: func() { },
            OnNewLeader: func(identity string) {
                if identity == operatorID {
                    log.Info("Leading the operator HA deployment")
                } else {
                    log.WithFields(logrus.Fields{
                        "newLeader":  identity,
                        "operatorID": operatorID,
                    }).Info("Leader re-election complete")
                }
            },
        },
    })
}

func onOperatorStartLeading(ctx ) {
    isLeader.Store(true)
    ...
}
```

# 3 `startSynchronizingCiliumNodes()`:

## 3.1 maintain ENI/IP pool for nodes (ENI mode)

On `CiliumNode` create/update/delete events, node manager's handlers will be
called accordingly, e.g. for `create` events:

```
startSynchronizingCiliumNodes
 |-AddFunc
   |-nodeManager.Create(ciliumnode)
      |-Update
        |-node, ok := n.nodes[ciliumnode.Name] // pkg/ipam/node.Node{}
        |-defer node.UpdatedResource(resource)
        |             |-n.ops.UpdatedNode(resource)
        |             |-n.instanceRunning = true
        |             |-n.recalculate()
        |             |-allocationNeeded := n.allocationNeeded()
        |             |-if allocationNeeded {
        |             |   n.requirePoolMaintenance()
        |             |   n.poolMaintainer.Trigger()
        |             | }
        |
        |-if !ok:
            |-NewTrigger("ipam-pool-maintainer-<nodename>",       func: node.MaintainIPPool)
            |  |-n.maintainIPPool
            |  |   |-determineMaintenanceAction
            |  |      |-PrepareIPAllocation
            |  |      |-n.ops.ReleaseIPs
            |  |      |-n.ops.AllocateIPs
            |  |      |   |-AssignPrivateIPAddresses(eni, numIPs)
            |  |      |       |-vendorAPI
            |  |      |-n.createInterface(ctx, a.allocation)
            |  |          |-n.ops.CreateInterface
            |  |              |-vendorAPI CreateNetworkInterface
            |  |              |-vendorAPI AttachNetworkInterface
            |  |              |-vendorAPI WaitNetworkInterfaceAttached
            |  |              |-n.manager.UpdateVNIC(instanceID, eni)
            |  |
            |  |-n.poolMaintenanceComplete
            |  |-recalculate
            |  |  |-n.ops.ResyncInterfacesAndIPs
            |  |      |-n.manager.ForeachInstance(func {
            |  |        for _, ip := range eni.Addresses
            |  |          available[ip] = ipamTypes.AllocationIP{Resource: e.ID}
            |  |      })
            |  |-n.manager.resyncTrigger.Trigger()
            |
            |-NewTrigger("ipam-pool-maintainer-<nodename>-retry", func: poolMaintainer.Trigger)
            |
            |-NewTrigger("ipam-node-k8s-sync-<nodename>",         func: node.syncToAPIServer)
               |-n.ops.PopulateStatusFields(node)
                   |-n.manager.ForeachInstance(n.node.InstanceID(),
                       func(instanceID, interfaceID string, rev ipamTypes.InterfaceRevision) error {
                         resource.Status.ENIs[interfaceID] = *e.DeepCopy()
                       }
           })
```

For on-premises nodes which do not rely on ENI for networking, this step is much
simpler, as no ENI/IP watermarks needs to be maintained.

## 3.2 Update `CiliumNode` status field

The `ipam-node-k8s-sync-<nodename>` controller calls `PopulateStatusFields()`
to update the `status` field of the CR.

# 4 `startSynchronizingServices()`

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

## 4.1 `JoinSharedStore()`

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
        DoFunc: func(ctx ) error { return s.syncLocalKeys(ctx) },
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

## 4.2 `endpointSlicesInit()` and `endpointsInit()`

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

## 4.3 `k8sServiceHandler()`

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

# 5 `runNodeWatcher()`

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

# 6 `startKvstoreWatchdog()`: GC of unused lock files in kvstore

This method:

1. scans the kvstore for **<mark>unused locks</mark>** and removes them every lock lease (`25s`)
2. updates kvstore heartbeat (`1min`)

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

# 7 `startKvstoreIdentityGC()`

Perform periodic identity GC. **<mark>GC interval</mark>** is configured through
`--identity-gc-interval=<interval>`, which defaults to the value of KVstoreLeaseTTL
(`--kvstore-lease-ttl="15m"`).

And, the GC process will in turn pose periodic QPS peaks on kvstore (default QPS limit=20).
[Configure client-side kvstore QPS limit](https://github.com/cilium/cilium/pull/15742) with:

```yaml
    kvstore-opt: '{"etcd.config": "/tmp/cilium/config-map/etcd-config", "etcd.qps": "100"}'
```

## 7.1 Background: identity allocation in cilium-agent side

### Agent: create identity allocator

```go
// pkg/allocator/allocator.go

func NewAllocator(typ AllocatorKey, backend Backend, opts ...AllocatorOption) (*Allocator, error) {
    a := &Allocator{
        keyType:      typ,
        backend:      backend,                      // kvstore client
        localKeys:    newLocalKeys(),
        stopGC:       make(chan struct{}),          // keepalive master/slave keys in kvstore
        remoteCaches: map[*RemoteCache]struct{}{},
    }

    for _, fn := range opts {
        fn(a)
    }

    a.mainCache = newCache(a)
    a.idPool = idpool.NewIDPool(a.min, a.max)
    a.initialListDone = a.mainCache.start()

    if !a.disableGC {
        go func() {
            select {
            case <-a.initialListDone:
            case <-time.After(AllocatorListTimeout): // List kvstore contents timed out
                log.Fatalf("Timeout while waiting for initial allocator state")
            }
            a.startLocalKeySync()
        }()
    }

    return a, nil
}
```

### Agent: ensure local keys always in kvstore with sync loop

A loop to periodically check and re-create identity keys if they are missing from KVStore:

* **<mark>master key</mark>**: identity ID to value
* **<mark>slave key</mark>**: value to identity ID

```go
// pkg/allocator/allocator.go

func (a *Allocator) startLocalKeySync() {
    go func(a *Allocator) {
        for {
            a.syncLocalKeys() // for k in keys: kvstore.UpdateKey()

            select {
            case <-a.stopGC:
                return        // Stopped master key sync routine
            case <-time.After(KVstorePeriodicSync): // 5min
            }
        }
    }(a)
}

// Check the kvstore and verify that a master key exists for all locally used allocations.
// This will restore master keys if deleted for some reason.
func (a *Allocator) syncLocalKeys() error {
    ids := a.localKeys.getVerifiedIDs()

    for id, value := range ids {
        a.backend.UpdateKey(context.TODO(), id, value, false)
    }
}
```

```go
// pkg/kvstore/allocator/allocator.go

// UpdateKey refreshes the record that this node is using this key -> id mapping.
// When reliablyMissing is set it will also recreate missing master or slave keys.
func (k *kvstoreBackend) UpdateKey(ctx , id idpool.ID, key allocator.AllocatorKey, reliablyMissing bool) error {
    var (
        err        error
        recreated  bool
        keyPath    = path.Join(k.idPrefix, id.String())
        keyEncoded = []byte(k.backend.Encode([]byte(key.GetKey())))
        valueKey   = path.Join(k.valuePrefix, k.backend.Encode([]byte(key.GetKey())), k.suffix)
    )

    // Ensures that any existing potentially conflicting key is never overwritten.
    success := k.backend.CreateOnly(ctx, keyPath, keyEncoded, false)
    switch {
    case err != nil:
        return fmt.Errorf("Unable to re-create missing master key "%s" -> "%s": %s", fieldKey, valueKey, err)
    case success:
        log.Warning("Re-created missing master key")
    }

    // Also re-create the slave key in case it has been deleted.
    if reliablyMissing {
        recreated = k.backend.CreateOnly(ctx, valueKey, []byte(id.String()), true)
    } else {
        recreated = k.backend.UpdateIfDifferent(ctx, valueKey, []byte(id.String()), true)
    }
    switch {
    case err != nil:
        return fmt.Errorf("Unable to re-create missing slave key "%s" -> "%s": %s", fieldKey, valueKey, err)
    case recreated:
        log.Warning("Re-created missing slave key")
    }

    return nil
}
```

### Agent: allocate identity

```go
// pkg/identity/cache/allocator.go

// AllocateIdentity allocates an identity described by the specified labels. If
// an identity for the specified set of labels already exist, the identity is
// re-used and reference counting is performed, otherwise a new identity is allocated via the kvstore.
func (m *CachingIdentityAllocator) AllocateIdentity(ctx, lbls labels.Labels) (*identity.Identity, allocated bool) {
    // This will block until the kvstore can be accessed and all identities were successfully synced
    m.WaitForInitialGlobalIdentities(ctx)

    idp := m.IdentityAllocator.Allocate(ctx, GlobalIdentity{lbls.LabelArray()})

    return identity.NewIdentity(identity.NumericIdentity(idp), lbls), isNew, nil
}
```

```go
// pkg/allocator/allocator.go

// Allocate will retrieve the ID for the provided key. If no ID has been
// allocated for this key yet, a key will be allocated. If allocation fails,
// most likely due to a parallel allocation of the same ID by another user,
// allocation is re-attempted for maxAllocAttempts times.
func (a *Allocator) Allocate(ctx , key AllocatorKey) (idpool.ID, error) {
    k := a.encodeKey(key)

    select {
    case <-a.initialListDone:
    case <-ctx.Done():
        return 0, fmt.Errorf("allocation cancelled while waiting for initial key list to be received")
    }

    for attempt := 0; attempt < maxAllocAttempts; attempt++ {
        if val := a.localKeys.use(k); val != idpool.NoID {
            a.mainCache.insert(key, val)
            return val
        }

        value, isNew, firstUse = a.lockedAllocate(ctx, key) // Create in kvstore
        if err == nil {
            a.mainCache.insert(key, value)
            return value
        }

        boff.Wait(ctx) // back-off wait
    }

    return 0
}

// Return values:
// 1. allocated ID
// 2. whether the ID is newly allocated from kvstore
// 3. whether this is the first owner that holds a reference to the key in
//    localkeys store
// 4. error in case of failure
func (a *Allocator) lockedAllocate(ctx , key AllocatorKey) (idpool.ID, bool, bool, error) {
    k := a.encodeKey(key)

    // fetch first key that matches /value/<key> while ignoring the node suffix
    value := a.GetIfLocked(ctx, key, lock)

    // We shouldn't assume the fact the master key does not exist in the kvstore
    // that localKeys does not have it. The KVStore might have lost all of its
    // data but the local agent still holds a reference for the given master key.
    if value == 0 {
        value = a.localKeys.lookupKey(k)
        if value {
            a.backend.UpdateKeyIfLocked(ctx, value, key, true, lock) // re-create master key
        }
    } else {
        _, firstUse = a.localKeys.allocate(k, key, value)
    }

    if value != 0 { // reusing existing global key
        a.backend.AcquireReference(ctx, value, key, lock)
        a.localKeys.verify(k) // mark the key as verified in the local cache
        return value, false, firstUse, nil
    }

    log.Debug("Allocating new master ID")
    id, strID, unmaskedID := a.selectAvailableID()

    oldID, firstUse := a.localKeys.allocate(k, key, id)

    err = a.backend.AllocateIDIfLocked(ctx, id, key, lock)
    if err != nil {
        // Creation failed. Another agent most likely beat us to allocting this
        // ID, retry.
        releaseKeyAndID()
        return 0, false, false, fmt.Errorf("unable to allocate ID %s for key %s: %s", strID, key, err)
    }

    // Notify pool that leased ID is now in-use.
    a.idPool.Use(unmaskedID)
    a.backend.AcquireReference(ctx, id, key, lock)
    a.localKeys.verify(k) // mark the key as verified in the local cache

    return id, true, firstUse, nil
}
```

## 7.2 Operator: `RunGC`

Now back to cilium-operator, the
rate-limited kvstore identity garbage collector, GC interval `IdentityGCInterval`:

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

Identity key-pair:

* **<mark>ID-to-Value key</mark>**: the so-called **<mark>"allocator master key"</mark>**
    * Key: `"cilium/state/identities/v1/id/12345"`
    * Val: `"label1;label2;labelN"`
* **<mark>Value-to-ID key</mark>**: the so-called **<mark>"allocator slave key"</mark>**
    * Key: `"cilium/state/identities/v1/value/label1;label2;labelN;/<NodeIP>"`
    * Val: `12345`

**<mark>ID-to-Value key will be GC-ed if corresponding Value-to-ID key is missing.</mark>**

```go
// pkg/kvstore/allocator/allocator.go

// RunGC scans the kvstore for unused master keys and removes them
func (k *kvstoreBackend) RunGC(ctx , rateLimit *rate.Limiter, staleKeysPrevRound map[string]uint64) (map[string]uint64) {
    allocated := k.backend.ListPrefix(ctx, k.idPrefix)      // "cilium/state/identities/v1/id/"
    staleKeys := map[string]uint64{}

    for key, v := range allocated {
        prefix2 := path.Join(k.valuePrefix, string(v.Data)) // "cilium/state/identities/v1/value/<labels>"
        pairs := k.backend.ListPrefixIfLocked(ctx, prefix2, lock)

        hasUsers := false
        for prefix := range pairs {
            if prefixMatchesKey(prefix2, prefix) {
                hasUsers = true
                break
            }
        }

        if !hasUsers {
            if modRev, ok := staleKeysPrevRound[key]; ok { // Only delete if this key was previously marked as to be deleted
                // if the v.ModRevision is different than the modRev (which is
                // the last seen v.ModRevision) then this key was re-used in between GC calls.
                if modRev == v.ModRevision {
                    k.backend.DeleteIfLocked(ctx, key, lock); log.Info("Deleted unused allocator master key")
                    rateLimit.Wait(ctx)
                }
            } else {
                staleKeys[key] = v.ModRevision // mark it to be delete in the next RunGC
            }
        }
    }

    return staleKeys, gcStats, nil
}
```

# References

1. [Cilium Doc v1.8: Cilium Operator](https://docs.cilium.io/en/v1.8/concepts/overview/)
2. [Kubernetes Operator](https://coreos.com/operators/)
