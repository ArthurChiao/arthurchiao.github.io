---
layout    : post
title     : "Cilium Code Walk Through: Agent Restore Endpoints And Identities"
date      : 2019-05-18
lastupdate: 2019-05-18
categories: cilium
---

----

This post walks you through the endpoint and identity restoring process during
Cilium agent initialization. Code based on Cilium `1.5.0`.

Call flows：

```shell
runDaemon                                       // daemon/daemon_main.go
  |-NewDaemon                                   // daemon/daemon.go
  | |-restoreOldEndpoints                       // daemon/state.go
  |   |-FilterEPDir                             // pkg/endpoint/endpoint.go
  |   |-readEPsFromDirNames                     // daemon/state.go
  |     |-FindEPConfigCHeader                   // pkg/endpoint/endpoint.go
  |     |-ParseEndpoint                         // pkg/endpoint/endpoint.go
  |     | |-SetStateLocked(StateRestoring)      // pkg/endpoint/endpoint.go
  |     |-allocateIPsLocked                     // daemon/state.go
  |-regenerateRestoredEndpoints                 // daemon/state.go
    |-AllocateIdentity                          // daemon/state.go
    |-SetIdentity                               // daemon/state.go
    |-Regenerate                                // pkg/endpoint/policy.go
```

Major steps:

1. Restore endpoint Info: `restoreOldEndpoints`
    1. Read endpoint info from local files: `readEPsFromDirNames`
    1. Parse endpoint info: `ParseEndpoint`
    1. Restore endpoint IP address: `allocateIPsLocked`
1. Re-generate BPF rules: `regenerateRestoredEndpoints`
    1. Allocate identity for endpoints (policy based on identies, not on endpoints): `AllocateIdentity`
    1. Regenerate BPF rules for endpoints: `Regenerate`

## 1 Daemon Start

From Cilium agent restarting.

CLI `cilium-agent <options` calls `runDaemon`.
`runDaemon` first calls `NewDaemon` to create the daemon process, and
**restores endpoint IDs from files in the local file system**;
then, it calls `regenerateRestoredEndpoints` to allocate new identies for
the endpoints, and regenerate BPF rules for identities.

```go
// NewDaemon creates and returns a new Daemon with the parameters set in c.
func NewDaemon(dp datapath.Datapath) (*Daemon, *endpointRestoreState, error) {
	d := Daemon{ }

	d.initMaps() // Open or create BPF maps.
	...

	// restore endpoints before any IPs are allocated to avoid eventual IP
	// conflicts later on, otherwise any IP conflict will result in the
	// endpoint not being able to be restored.
	restoredEndpoints, err := d.restoreOldEndpoints(option.Config.StateDir, true)
	...
}
```

By default, the parameter `option.Config.StateDir` passed to
`restoreOldEndpoints` is ***`/var/run/cilium/`***.

`option.Config.StateDir` reads the list of existing endpoints previously managed
by Cilium and associated it with container workloads. It
performs the first step in restoring the endpoint structure,
allocating their existing IP out of the CIDR block and then inserting the
endpoints into the endpoints list.

If `clean` flag is `true`, endpoints which cannot be associated with a container
workloads are deleted. `daemon/state.go`:

```go
func (d *Daemon) restoreOldEndpoints(dir string, clean bool) (*endpointRestoreState, error) {
	state := &endpointRestoreState{restored: [], toClean:  [] }
	eptsID := endpoint.FilterEPDir(ioutil.ReadDir(dir))
	possibleEPs := readEPsFromDirNames(dir, eptsID) // set all old endpoints to StateRestoring

	for _, ep := range possibleEPs {
		skipRestore := false
		if ep.HasLabels(labels.LabelHealth) // clean health endpoint state unconditionally.
			os.RemoveAll(healthStateDir)    // Remove old health endpoint state directory
			continue
		else if k8serrors.IsNotFound(err)                                     // Pod not found in K8S
		   || ep.HasIpvlanDataPath() && ! os.Stat(ep.BPFIpvlanMapPath())      // interface could not found
		   || ! netlink.LinkByName(ep.IfName)                                 // interface could not found
		   || ! skipRestore && WorkloadsEnabled() && !workloads.IsRunning(ep) // no workload could be associated 
			skipRestore = true

		if clean && skipRestore
			failed++
			state.toClean = append(state.toClean, ep)
			continue

		d.allocateIPsLocked(ep) // restore IP address for endpoint
		if !option.Config.KeepConfig {
			alwaysEnforce := policy.GetPolicyEnabled() == option.AlwaysEnforce
			ep.SetDesiredIngressPolicyEnabledLocked(alwaysEnforce)
			ep.SetDesiredEgressPolicyEnabledLocked(alwaysEnforce)
		}
		ep.SkipStateClean()
		state.restored = append(state.restored, ep)
		delete(existingEndpoints, ep.IPv4.String())
	}

	if existingEndpoints != nil
		for hostIP, info := range existingEndpoints // delete obsolete endpoint from BPF map if !info.IsHost()
			lxcmap.DeleteEntry(net.ParseIP(hostIP))

	return state, nil
}
```

Function `FilterEPDir` selects all the directories whose names match one of the
following patterns:

* file name consists of only numbers, e.g. `2303/`, full path `/var/run/cilium/2303`
* `*_next/`, full path `/var/run/cilium/*_next/`
* `*_next_fail/`, full path `/var/run/cilium/*_next_fail/`

Then it finds the endpoint C header files in the directories,
with full path like `/var/run/cilium/<endpoint_id>/lxc_config.h`.

```go
// readEPsFromDirNames returns a mapping of endpoint ID to endpoint of endpoints
// from a list of directory names that can possible contain an endpoint.
func readEPsFromDirNames(basePath string, eptsDirNames []string) map[uint16]*endpoint.Endpoint {
	possibleEPs := map[uint16]*endpoint.Endpoint{}
	for _, epDirName := range eptsDirNames {
		cHeaderFile := readDir(basePath + epDirName)
		if cHeaderFile == "" {
			scopedLog.Warning("C header file not found. Ignoring endpoint")
			continue
		}

		ep, err := endpoint.ParseEndpoint(common.GetCiliumVersionString(cHeaderFile))
		if _, ok := possibleEPs[ep.ID]; ok {
			if strings.HasSuffix(ep.DirectoryPath(), epDirName)
				possibleEPs[ep.ID] = ep
		} else {
			possibleEPs[ep.ID] = ep
		}
	}
	return possibleEPs
}
```

It calls `endpoint.ParseEndpoint` to parse the endpoint info from the header
file.

## 2 Parse Endpoint Info

Parse endpoint info from header file.
`pkg/endpoint/endpoint.go`:

```c
// ParseEndpoint parses the given strEp which is in the form of:
// common.CiliumCHeaderPrefix + common.Version + ":" + endpointBase64
func ParseEndpoint(strEp string) (*Endpoint, error) {
	ep := Endpoint{ }
	parseBase64ToEndpoint(strEpSlice[1], &ep)

	ep.hasBPFProgram = make(chan struct{}, 0)
	ep.desiredPolicy = &policy.EndpointPolicy{}
	ep.realizedPolicy = &policy.EndpointPolicy{}
	ep.controllers = controller.NewManager()

	if ep.Status == nil || ep.Status.CurrentStatuses == nil || ep.Status.Log == nil {
		ep.Status = NewEndpointStatus()
	}

	if ep.SecurityIdentity == nil {
		ep.SetIdentity(identityPkg.LookupReservedIdentity(identityPkg.ReservedIdentityInit))
	} else {
		ep.SecurityIdentity.Sanitize()
	}

	ep.SetStateLocked(StateRestoring, "Endpoint restoring")
	return &ep, nil
}
```

## 3 Restore (allocate) IP

`daemon/state.go`: `allocateIPsLocked`.

## 4 Allocate Identities

`pkg/endpoint/policy.go`: `regenerateRestoredEndpoints`.

## 5 Regenerate BPF Rules

`pkg/endpoint/policy.go`: `regenerateRestoredEndpoints`.
