---
layout    : post
title     : "Cilium Code Walk Through: Agent Init"
date      : 2019-05-29
lastupdate: 2019-05-29
categories: cilium
---

Code based on `1.5.1`.

```shell
runDaemon                                       // daemon/daemon_main.go
  |-NewDaemon                                   // daemon/daemon.go
  | |-restoreOldEndpoints                       // daemon/state.go
  |   |-FilterEPDir                             // pkg/endpoint/endpoint.go
  |   |-readEPsFromDirNames                     // daemon/state.go
  |   | |-FindEPConfigCHeader                   // pkg/endpoint/endpoint.go
  |   | |-ParseEndpoint                         // pkg/endpoint/endpoint.go
  |   | | |-SetStateLocked(StateRestoring)      // pkg/endpoint/endpoint.go
  |   |-allocateIPsLocked                       // daemon/state.go
  |-EnableConntrackGC
  |-initK8sSubsystem                            // daemon/k8s_watcher.go
  | |-EnableK8sWatcher                          // daemon/k8s_watcher.go
  | |-waitForCacheSync                          // daemon/k8s_watcher.go
  |-kvstore.Setup
  |-regenerateRestoredEndpoints                 // daemon/state.go
  | |-AllocateIdentity                          // daemon/state.go
  | |-SetIdentity                               // daemon/state.go
  | |-Regenerate                                // pkg/endpoint/policy.go
  |-workloads.EnableEventListener
  |-initHealth
  |-server.Serve
```

## 1 Agent Start

```go
func runDaemon() {
	// Initializing daemon
	d, restoredEndpoints, err := NewDaemon(linuxdatapath.NewDatapath(datapathConfig))

	// Starting connection tracking garbage collector
	endpointmanager.EnableConntrackGC(restoredEndpoints.restored)
	endpointmanager.EndpointSynchronizer = &endpointsynchronizer.EndpointSynchronizer{}

	// CNP with CIDRs rely on the allocator, which itself relies on the kvstore
	k8sCachesSynced := d.initK8sSubsystem()
	kvstore.Setup(option.Config.KVStore, option.Config.KVStoreOpt, goopts)

	// Wait only for certain caches, but not all! Check Daemon.initK8sSubsystem() for more info
	<-k8sCachesSynced
	if option.Config.RestoreState {
		d.regenerateRestoredEndpoints(restoredEndpoints)
	}

	// workload event listener, update endpoint manager metadata such as K8S pod name and namespace
	eventsCh, err := workloads.EnableEventListener()
	d.workloadsEventsCh = eventsCh

	d.initHealth()

	d.startStatusCollector()

	api := d.instantiateAPI()
	server := server.NewServer(api)
	server.Serve()
}
```

## 2 New Daemon

See [Cilium Code Walk Through: Agent CIDR Init]({% link _posts/2019-06-17-cilium-code-agent-cidr-init.md %}).

Specifically, it will start an event loop to process K8S resource changes by
`d.runK8sServiceHandler()`, the function will further call
`k8sServiceHandler()`, which implements the event loop.

## 3 Enable Conntrack GW

## 4 Init K8S Subsystem

The K8S subsystem initialization process contains two parts:

1. spawn listeners for the interested K8S resources, e.g. NetworkPolicy,
   Service, Endpoint, etc
1. wait for some critical resource to be synchronized between K8S and Cilium

```go
func (d *Daemon) initK8sSubsystem() <-chan struct{} {
	d.EnableK8sWatcher(option.Config.K8sWatcherQueueSize)

	cachesSynced := make(chan struct{})
	go func() {
		d.waitForCacheSync(
			k8sAPIGroupServiceV1Core,
			...
			k8sAPIGroupPodV1Core,
		)
		close(cachesSynced)
	}()

	go func() {
		select {
		case <-cachesSynced:
			log.Info("All pre-existing resources related to policy have been received; continuing")
		case <-time.After(cacheSyncTimeout):
			log.Fatalf("Timed out waiting for pre-existing resources related to policy to be received; exiting")
		}
	}()

	return cachesSynced
}
```

### 4.1 Spawn Listeners

The agent will watch following resources on K8S apiserver:

1. NetworkPolicy
1. Service
1. Endpoint
1. Ingress
1. CNP (Cilium Network Policy)
1. Pod
1. Node
1. Namespace

```go
// EnableK8sWatcher watches for policy, services and endpoint changes on the Kubernetes
// api server defined in the receiver's daemon k8sClient. queueSize specifies the queue length used to serialize k8s events.
func (d *Daemon) EnableK8sWatcher(queueSize uint) error {
	policyController := k8s.NewInformer(&networkingv1.NetworkPolicy{})
	go policyController.Run(wait.NeverStop)
	...
	namespaceController := k8s.NewInformer(&v1.Namespace{},
	go namespaceController.Run(wait.NeverStop)
}
```

Handlers are registered (as `NewInformer()`'s parameters, not shown in the above
code snippet because of the limited space) for each resource's
*create/update/delete* event, thus whenever the resource is changed, the
corresponding handlers will be invoked.

### 4.2 Wait for Synchronization

Including:

1. k8sAPIGroupServiceV1Core
1. k8sAPIGroupEndpointV1Core
1. k8sAPIGroupCiliumV2
1. k8sAPIGroupNetworkingV1Core
1. k8sAPIGroupNamespaceV1Core
1. k8sAPIGroupPodV1Core

```go
		d.waitForCacheSync(
			k8sAPIGroupServiceV1Core,
			...
			k8sAPIGroupPodV1Core,
		)
```

Where `waitForCacheSync` is defined in the same file:

```go
// waitForCacheSync waits for k8s caches to be synchronized for the given
// resource. Returns once all resourcesNames are synchronized with cilium-agent.
func (d *Daemon) waitForCacheSync(resourceNames ...string) {
	for _, resourceName := range resourceNames {
		c, ok := d.k8sResourceSynced[resourceName]
		if !ok {
			continue
		}
		<-c
	}
}
```

### 4.3 Example: Service Enforcement

When a service is added/updated/deleted from K8S, how does Cilium perceived it,
and respond correspondingly?

Three steps:

1. Register handlers for K8S events, in `EnableK8sWatcher()` in `daemon/k8s_watcher.go`
1. Start event loop for processing K8S events, in `d.runK8sServiceHandler()` in `daemon/daemon.go`
1. Specific event processing , in `k8sServiceHandler()` in `daemon/k8s_watcher.go`

## 5 Re-regenerate Restored Endpoints

See [Cilium Code Walk Through: Agent Restore Endpoints And Identities]({% link _posts/2019-06-17-cilium-code-agent-restore-endpoints-and-policies.md %}).

## 6 Init cilium-health

See [Cilium Code Walk Through: Cilium Health]({% link _posts/2019-06-17-cilium-code-cilium-health.md %}).

## 7 Misc

### IPAM State Restoration

IPAM manages IP address allocation, in short, it tracks two states:

```json
{
    "allocated_ips": [], // IPs have been allocated out
    "available_ips": [], // IPs avaiable for allocation
}
```

IPAM saves its state **in memory**. Then, how does it survive agent restart or
host reboot? The secret lies in the **local file system**.

For each allocated IP, Cilium will create an endpoint for it, and write the
endpoint info into a local file (C header file).

When agent restarts, or host reboots, IPAM state will be reset. Then on
restarting, the agent will loop over all endpoint files, parsing the IP info
inside it, then reserve the IP address in IPAM. In the end, the IPAM recovers to
the state before restart.
