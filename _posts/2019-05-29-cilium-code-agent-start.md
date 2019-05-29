---
layout    : post
title     : "Cilium Code Walk Through: Agent Start"
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
  |-initK8sSubsystem
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

## IPAM State Restoration

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
