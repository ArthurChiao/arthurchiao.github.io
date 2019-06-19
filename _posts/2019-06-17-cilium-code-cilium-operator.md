---
layout    : post
title     : "Cilium Code Walk Through: Cilium Operator"
date      : 2019-05-30
lastupdate: 2019-06-19
categories: cilium
---

There are two components in Cilium which has the *operator* word in their name:

1. Cilium etcd operator
1. Cilium operator

Cilium etcd operator is a [**Kubernetes operator**](https://coreos.com/operators/) [2]
implementation, which creates and maintains Cilium's builtin etcd cluster
(if using internal etcd mode).

Cilium operator, although also named "operator", actually has nothing to do
with **Kubernetes operator**. What is Cilium operator? 
According to [official doc](https://docs.cilium.io/en/v1.5/concepts/#cilium-operator) [1]:

> The Cilium Operator is responsible for managing duties in the cluster which
> should logically be handled once for the entire cluster, rather than once for
> each node in the cluster. Its design helps with scale limitations in large
> kubernetes clusters (>1000 nodes). The responsibilities of Cilium operator
> include:
>
> * Synchronizing kubernetes services with etcd for Cluster Mesh
> * Synchronizing node resources with etcd
> * Ensuring that DNS pods are managed by Cilium
> * Garbage-collection of Cilium Endpoints resources

In short, it is **an optional component, aimed at performance optimization
of Cilium in large kubernetes clusters**.

This post walks through the latter one.

## 0 Call Flow

Code based on `1.5.1`.

```shell
runOperator                                                 // operator/main.go
  |-StartServer                                             // operator/api.go
  |-kvstore.Setup
  |-k8s.Init
  |-startSynchronizingServices                              // operator/k8s_service_sync.go
  | |-go JoinSharedStore()                                  // pkg/kvstore/store/store.go
  | |    |-listAndStartWatcher                              // pkg/kvstore/store/store.go
  | |      |-watcher                                        // pkg/kvstore/store/store.go
  | |        |-start                                        // pkg/kvstore/allocator/cache.go
  | |          |-ListAndWatch                               // pkg/kvstore/events.go
  | |            |-ListAndWatch                             // pkg/kvstore/etcd.go
  | |              |-Watch                                  // pkg/kvstore/etcd.go
  | |-k8s.NewInformer(v1.Service).Run
  | |-k8s.NewInformer(v1.Endpoint).Run
  | |-k8sServiceHandler                                     // operator/k8s_service_sync.go
  |
  |-runNodeWatcher                                          // operator/k8s_node.go
  | |-JoinSharedStore                                       // pkg/kvstore/store/store.go
  | | |-listAndStartWatcher                                 // pkg/kvstore/store/store.go
  | |   |-watcher                                           // pkg/kvstore/store/store.go
  | |     |-start                                           // pkg/kvstore/allocator/cache.go
  | |       |-ListAndWatch                                  // pkg/kvstore/events.go
  | |         |-ListAndWatch                                // pkg/kvstore/etcd.go
  | |           |-Watch                                     // pkg/kvstore/etcd.go
  | |-k8s.NewInformer(v1.Node).Run
  |
  |-enableCNPWatcher
    |-k8s.NewInformer(c_v2.CiliumNetworkPolicy).Run         // operator/cnp_event.go
```

Cilium operator watches 4 kinds of K8S resources:

1. Service
1. Endpoint
1. Node
1. CNP (Cilium Network Policy)

## 1 Run Operator

`operator/main.go`:

```go
func runOperator(cmd *cobra.Command) {
	go StartServer(fmt.Sprintf(":%d", apiServerPort), shutdownSignal)

	kvstore.Setup(kvStore, kvStoreOpts, nil)
	k8s.Configure(k8sAPIServer, k8sKubeConfigPath)
	k8s.Init()

	if synchronizeServices
		startSynchronizingServices()
	if enableCepGC
		enableCiliumEndpointSyncGC()

	runNodeWatcher()

	if identityGCInterval != time.Duration(0)
		startIdentityGC()
	if !option.Config.DisableCiliumEndpointCRD
		enableUnmanagedKubeDNSController()

	enableCNPWatcher()
}
```

## 2 Sync Services

Synchronize k8s services to kvstore. This will create the `Service` and
`Endpoint` watchers.

```go
func startSynchronizingServices() {
	go func() {
		store, err := store.JoinSharedStore(store.Configuration{
			Prefix: service.ServiceStorePrefix,
			KeyCreator: func() store.Key { return &service.ClusterService{} },
			SynchronizationInterval: 5 * time.Minute,
		})
		servicesStore = store
	}()

	svcController := k8s.NewInformer( // Watch for v1.Service changes and push changes into ServiceCache
		cache.NewListWatchFromClient()), &v1.Service{}, 0,
		cache.ResourceEventHandlerFuncs{
			AddFunc: func(obj interface{}) {
				k8sSvc := k8s.CopyObjToV1Services(obj) // Received service addition
				k8sSvcCache.UpdateService(k8sSvc)
			},
			UpdateFunc: func(oldObj, newObj interface{}) {
				oldk8sSvc := k8s.CopyObjToV1Services(oldObj)
				newk8sSvc := k8s.CopyObjToV1Services(newObj)
				k8sSvcCache.UpdateService(newk8sSvc)
			},
			DeleteFunc: func(obj interface{}) {
				k8sSvc := k8s.CopyObjToV1Services(obj)
				k8sSvcCache.DeleteService(k8sSvc)
			},
		},
		k8s.ConvertToK8sService,
	)

	go svcController.Run(wait.NeverStop)

	endpointController := k8s.NewInformer( // Watch for v1.Endpoints changes and push changes into ServiceCache
		cache.NewListWatchFromClient(), &v1.Endpoints{}, 0,
		cache.ResourceEventHandlerFuncs{
			AddFunc: func(obj interface{}) {
				k8s.CopyObjToV1Endpoints(obj)
				k8sSvcCache.UpdateEndpoints(k8sEP)
			},
			UpdateFunc: func(oldObj, newObj interface{}) {
				k8s.CopyObjToV1Endpoints(oldObj)
				k8s.CopyObjToV1Endpoints(newObj)
				k8sSvcCache.UpdateEndpoints(newk8sEP)
			},
			DeleteFunc: func(obj interface{}) {
				k8sEP := k8s.CopyObjToV1Endpoints(obj)
				k8sSvcCache.DeleteEndpoints(k8sEP)
			},
		},
		k8s.ConvertToK8sEndpoints,
	)

	go endpointController.Run(wait.NeverStop)
	go func() {
		log.Info("Starting to synchronize Kubernetes services to kvstore")
		k8sServiceHandler()
	}()
}
```


Handle K8S Service events:

```go
func k8sServiceHandler() {
	for {
		event, ok := <-k8sSvcCache.Events
		svc := k8s.NewClusterService(event.ID, event.Service, event.Endpoints)

		log.Debug("Kubernetes service definition changed")
		if !event.Service.Shared { // The annotation may have been added, delete an eventual existing service
			servicesStore.DeleteLocalKey(&svc)
			continue
		}

		switch event.Action {
		case k8s.UpdateService, k8s.UpdateIngress:
			servicesStore.UpdateLocalKeySync(&svc)
		case k8s.DeleteService, k8s.DeleteIngress:
			servicesStore.DeleteLocalKey(&svc)
		}
	}
}
```

## 3 JoinSharedStore

`pkg/kvstore/etcd.go`:

```go
// ListAndWatch implements the BackendOperations.ListAndWatch using etcd
func (e *etcdClient) ListAndWatch(name, prefix string, chanSize int) *Watcher {
	w := newWatcher(name, prefix, chanSize)
	go e.Watch(w)
	return w
}
```

```go
// Watch starts watching for changes in a prefix
func (e *etcdClient) Watch(w *Watcher) {
	localCache := watcherCache{}

	<-e.Connected()
	for {
		res := e.client.Get(w.prefix, ...)
		if res.Count > 0 {
			for _, key := range res.Kvs
				t = EventTypeCreate if !localCache.Exists(key.Key) else EventTypeModify
				localCache.MarkInUse(key.Key)
				w.Events <- KeyValueEvent{Key: key.Key, Value: key.Value, Typ: t}
		}
		if res.More {
			continue // More keys to be read, call Get() again
		}
		localCache.RemoveDeleted(func(k string) {
			event := KeyValueEvent{ Key: k, Typ: EventTypeDelete, }
			w.Events <- event
		})
	recreateWatcher:
		etcdWatch := e.client.Watch(w.prefix, client.WithPrefix())
		for {
			select {
			case <-w.stopWatch:
				return
			case r, ok := <-etcdWatch:
				for _, ev := range r.Events
					event := KeyValueEvent{Key: ev.Kv.Key, Value: ev.Kv.Value}
					switch {
					case ev.Type == client.EventTypeDelete:
						event.Typ = EventTypeDelete
						localCache.RemoveKey(ev.Kv.Key)
					case ev.IsCreate():
						event.Typ = EventTypeCreate
						localCache.MarkInUse(ev.Kv.Key)
					default:
						event.Typ = EventTypeModify
						localCache.MarkInUse(ev.Kv.Key)
					}
					w.Events <- event
			}
		}
	}
}
```

## References

1. [Cilium Doc v1.5: Cilium Operator](https://docs.cilium.io/en/v1.5/concepts/#cilium-operator)
2. [Kubernetes Operator](https://coreos.com/operators/)
