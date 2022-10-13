---
layout    : post
title     : "Cilium Code Walk Through: Restore Endpoints and Identities"
date      : 2019-05-18
lastupdate: 2020-09-01
categories: cilium
---

This post walks you through the endpoint and identity restoring process during
Cilium agent restart. Code bases on Cilium `1.8.2`.

It's recommended to read
[Cilium Code Walk Through: Agent Start Process]({% link _posts/2019-06-17-cilium-code-agent-start.md %})
before reading this post.

This post is included in
[Cilium Code Walk Through Series]({% link _posts/2019-06-17-cilium-code-series.md %}).

----

* TOC
{:toc}

----

```shell
runDaemon                                                                    //    daemon/cmd/daemon_main.go
  |-NewDaemon                                                                // -> daemon/cmd/daemon.go
  | |-restoredEndpoints := d.restoreOldEndpoints
  |     |-ioutil.ReadDir
  |     |-endpoint.FilterEPDir // filter over endpoint directories
  |     |-for ep := range possibleEPs
  |         validateEndpoint(ep)
  |           |-allocateIPsLocked
  |-initRestore(restoredEndpoints)                                           // -> daemon/cmd/state.go
     |-regenerateRestoredEndpoints                                           //    daemon/cmd/state.go
        |-for ep
        |   Expose
        |    |-NewEventQueueBuffered
        |    |-eventQueue.Run ------------------>---|
        |                                           |
        |-for ep                                    |
            RegenerateAfterRestore                  |                        // -> pkg/endpoint/restore.go
             |-restoreIdentity                      |                        //
             |-Regenerate                           |                        // -> pkg/endpoint/policy.go
                |-eventQueue.Enqueue(epEvent)       |
                  /\                                |
                  ||                                |
                  \/                                |
   eventQueue.Run()   <-----------------------<-----|                        //    pkg/endpoint/events.go
    |-for ev := range q.events
        metadata.Handle
         |-EndpointRegenerationEvent.Handle                                  //    pkg/endpoint/events.go
           |-regenerate                                                      // -> pkg/endpoint/policy.go
              |-runPreCompilationSteps
              |-updateAndOverrideEndpointOptions
              |-writeHeaderfile
              |  |-ctmap.WriteBPFMacros()
              |-regenerateBPF                                                //    pkg/endpoint/bpf.go
                |-realizeBPFState                                            //    pkg/endpoint/bpf.go
                   |-if   CompileAndLoad                                     //    pkg/datapath/loader/loader.go
                           |-compileDatapath                                 // -> pkg/datapath/loader/compile.go
                           |-reloadDatapath                                  //    pkg/datapath/loader/loader.go
                              |-replaceDatapath                              //    pkg/datapath/loader/loader.go
                                 |-cmd.exec("cilium-map-migrate -s <objPath>")
                                 |-cmd.exec("tc filter replace xx ..")
                                 |-cmd.exec("cilium-map-migrate -e <objPath> -r <retCode>")
                     elif CompileOrLoad                                      //    pkg/datapath/loader/loader.go
                           |-ReloadDatapath                                  //    pkg/datapath/loader/loader.go
                              |-reloadDatapath                               //    pkg/datapath/loader/loader.go
                                |-replaceDatapath
                                   |-cmd.exec("cilium-map-migrate -s <objPath>")
                                   |-cmd.exec("tc filter replace xx ..")
                                   |-cmd.exec("cilium-map-migrate -e <objPath> -r <retCode>")
                     else ReloadDatapath                                     //    pkg/datapath/loader/loader.go
                           |-reloadDatapath                                  //    pkg/datapath/loader/loader.go
                              |-replaceDatapath
                                 |-cmd.exec("cilium-map-migrate -s <objPath>")
                                 |-cmd.exec("tc filter replace xx ..")
                                 |-cmd.exec("cilium-map-migrate -e <objPath> -r <retCode>")
```

Major steps:

1. Restore endpoint info from files: `restoreOldEndpoints()`
1. Re-generate BPF for endpoints: `regenerateRestoredEndpoints()`
    1. Allocate identity for endpoints (policy based on identies, not on endpoints)
    1. Regenerate BPF for endpoints

# 1 Daemon/agent restart

On Cilium agent restarting, method `runDaemon()` will be invoked.

`runDaemon()` first calls `NewDaemon()` to create the daemon process, then
restores endpoint info from files in the local file system;
then, it calls `regenerateRestoredEndpoints()` to allocate new identies for
the endpoints, and regenerate BPF for identities.

```go
func NewDaemon(dp datapath.Datapath) (*Daemon, *endpointRestoreState, error) {
    d := Daemon{ ... }
    d.initMaps()       // Open or create BPF maps.
    ...
    // restore endpoints before any IPs are allocated to avoid eventual IP conflicts later on,
    // otherwise any IP conflict will result in the endpoint not being able to be restored.
    restoredEndpoints := d.restoreOldEndpoints(option.Config.StateDir, true)
    ...
}
```

# 2 Extract endpoints info from local files: `restoreOldEndpoints()`

By default, the value of `option.Config.StateDir` passed to method
`restoreOldEndpoints()` is **`/var/run/cilium/`**.

This method performs the first step in restoring the endpoint structure,
allocating their existing IP out of the CIDR block and then inserting the
endpoints into the endpoints list.

```go
// daemon/cmd/state.go

func (d *Daemon) restoreOldEndpoints(dir string, clean bool) (*endpointRestoreState) {
    state := &endpointRestoreState{
        restored: []*endpoint.Endpoint{},
        toClean:  []*endpoint.Endpoint{},
    }

    existingEndpoints = lxcmap.DumpToMap()             // get previous endpoint IDs from BPF map
    dirFiles := ioutil.ReadDir(dir)                    // state dir: `/var/run/cilium/`
    eptsID := endpoint.FilterEPDir(dirFiles)           // `/var/run/cilium/<ep_id>/lxc_config.h`

    possibleEPs := ReadEPsFromDirNames(dir, eptsID)    // parse endpoint ID from dir name
    for ep := range possibleEPs {
        ep.SetAllocator(d.identityAllocator)
        d.validateEndpoint(ep)  // further call allocateIPsLocked() to retain IP for this endpoint
        ep.SetDefaultConfiguration(true)

        state.restored.append(ep)                      // insert into restored list, will regen bpf for them
        delete(existingEndpoints, ep.IPv4.String())
    }

    for hostIP, info := range existingEndpoints        // for the remaining endpoints, delete them
        if ip := net.ParseIP(hostIP) && !info.IsHost() // from endpoint map
            lxcmap.DeleteEntry(ip)

    return state
}
```

Function `FilterEPDir()` selects all the directories whose name match one of the
following patterns:

* file name consists of only numbers, e.g. `2303/`, full path `/var/run/cilium/2303`
* `*_next/`, full path `/var/run/cilium/*_next/`
* `*_next_fail/`, full path `/var/run/cilium/*_next_fail/`

Then it finds the endpoint C header files in the directories,
with full path like `/var/run/cilium/<ep_id>/lxc_config.h`.

# 3 Reserve IP addresses for existing endpoints

On agent restart, IPAM states are reset, which makes all IP addresses in IPAM
available for allocating - including those that already being used by running
containers on this host.

From the previous step, agent has recovered the IP addresses (IPv4 and/or IPv6)
that an endpoint is using now by parsing file
`/var/run/cilium/<ep_id>/lxc_config.h`.

Now, it must reserve, or re-allocate, the IP addresses from
IPAM to prevent them from being allocated out again. This is accomplished by
calling `validateEndpoint(ep) -> allocateIPsLocked()`.

# 4 Regenerate BPF for restored endpoints

Resources in k8s cluster may have changed during agent restart, e.g. the Service
to backend mappings. Besides, there may also be configuration changes for the
agent.

So to keep endpoint states up to date, we need to regenerate the BPF code & map
for this endpoint:

```go
// daemon/cmd/state.go

func (d *Daemon) regenerateRestoredEndpoints(state) (restoreComplete chan struct{}) {
    epRegenerated := make(chan bool, len(state.restored))

    for i := len(state.restored)-1; i >= 0; i-- {
        ep := state.restored[i]
        ep.Expose(d.endpointManager)      // Insert ep into endpoint manager so it can be regenerated
    }                                     // later with RegenerateAllEndpoints().

    for ep := range state.restored        // loop over restored endpoints
        go func() {
            ep.RegenerateAfterRestore()   // perform BPF regeneration
            epRegenerated <- true
        }(ep, epRegenerated)

    for ep := range state.toClean         // clean the endpoints that no need to restore
        d.deleteEndpointQuiet(ep)

    go func() {
        for buildSuccess := range epRegenerated
            if total++ >= len(state.restored) break
        log.Info("Finished regenerating restored endpoints")
    }()
}
```

Major steps:

1. Insert all waiting-to-be-restored endpoints into endpoint manager via
   `Expose()`; this will create an event queue for each endpoint, and the queue
   will listen to BPF regenerate events.
2. Restore identity for endpoint
3. Enqueue an `EndpointRegenerationEvent` into endpoint's event queue, which
   is created in step 1.
4. On receiving `EndpointRegenerationEvent`, handler performs BPF regeneration.

Let's see them in detail.

## 4.1 Create per-endpoint event queue

```go
// pkg/endpoint/manager.go

// Expose exposes the endpoint to the endpointmanager
func (e *Endpoint) Expose(mgr endpointManager) error {
    newID := mgr.AllocateID(e.ID)
    e.ID = newID

    e.eventQueue = eventqueue.NewEventQueueBuffered("endpoint-"+e.ID, Config.EndpointQueueSize)
    e.eventQueue.Run()

    e.updateReferences(mgr)
    e.getLogger().Info("New endpoint")

    mgr.RunK8sCiliumEndpointSync(e, option.Config)
}
```

```go
// pkg/eventqueue/eventqueue.go

func (q *EventQueue) Run() {
    go q.eventQueueOnce.Do(func() {
        for ev := range q.events {
            select {
            default:
                ev.Metadata.Handle(ev.eventResults)
            }
        }
    })
}
```

## 4.2 Restore identities for endpoints

```go
// pkg/endpoint/restore.go

// RegenerateAfterRestore performs the following operations on the specified Endpoint:
// * allocates an identity for the Endpoint
// * regenerates the endpoint
func (e *Endpoint) RegenerateAfterRestore() error {
    e.restoreIdentity()

    regenerationMetadata := &regeneration.ExternalRegenerationMetadata{
        Reason:            "syncing state to host",
        RegenerationLevel: regeneration.RegenerateWithDatapathRewrite,
    }

    buildSuccess := <-e.Regenerate(regenerationMetadata)
    log.Info("Restored endpoint")
}
```

## 4.3 Enqueue `EndpointRegenerationEvent` event into endpoint's queue

`Regenerate()` is defined as:

```go
// pkg/endpoint/policy.go

// Regenerate forces the regeneration of endpoint programs & policy
// Should only be called with e.state at StateWaitingToRegenerate, StateWaitingForIdentity, or StateRestoring
func (e *Endpoint) Regenerate(regenMetadata *regeneration.ExternalRegenerationMetadata) <-chan bool {
    done := make(chan bool, 1)

    regenContext := ParseExternalRegenerationMetadata(ctx, cFunc, regenMetadata)
    epEvent := eventqueue.NewEvent(&EndpointRegenerationEvent{regenContext: regenContext, ep: e})
    resChan := e.eventQueue.Enqueue(epEvent)

    return done
}
```

As can be seen, `Regenerate()` enqueues an `EndpointRegenerationEvent` into the
the endpoint's event queue.

Now let's go to the receiving side of this event queue.

## 4.4 Event handler: perform BPF regeneration

Handler for the `EndpointRegenerationEvent` type event:

```go
// pkg/endpoint/events.go

// Handle handles the regeneration event for the endpoint.
func (ev *EndpointRegenerationEvent) Handle(res chan interface{}) {
    e := ev.ep
    doneFunc := e.owner.QueueEndpointBuild(e.ID)
    if doneFunc != nil { // dequeued endpoint from build queue
        ev.ep.regenerate(ev.regenContext)
        doneFunc()
        e.notifyEndpointRegeneration(err)
    }

    res <- &EndpointRegenerationResult{ err: err, }
}
```

where `regenerate()` is defined as:

```go
// pkg/endpoint/policy.go

func (e *Endpoint) regenerate(context *regenerationContext) (retErr error) {
    origDir := e.StateDirectoryPath()
    tmpDir := e.NextDirectoryPath()
    e.removeDirectory(tmpDir)
    os.MkdirAll(tmpDir, 0777)

    defer func() { e.removeDirectory(tmpDir) }()

    revision, stateDirComplete = e.regenerateBPF(context)
    return e.updateRealizedState(stats, origDir, revision, stateDirComplete)
}
```

It creates a temporary directory for creating new BPFs, then calls `regenerateBPF()`.
The latter will further call more deeper methods to fulfull the BPF
regeneration. As this call stack is fairly deep, we list them in a dedicated
section in the below.

## 4.5 From `regenerateBPF()` to the eventual `clang/tc` commands

`ReloadDatapath()` (or `compileAndLoad()`) occupies **most of the total endpoint regeneration time**,
recorded as `scope=bpfLoadProg` in metric `cilium_endpoint_regeneration_time_stats_seconds_bucket`.

```go
// pkg/endpoint/bpf.go

// regenerateBPF rewrites all headers and updates all BPF maps to reflect the specified endpoint.
// ReloadDatapath forces the datapath progs to be reloaded. It does not guarantee recompilation of the programs.
//
// Returns the policy revision number when the regeneration has called,
// Whether the new state dir is populated with all new BPF state files.
func (e *Endpoint) regenerateBPF(regenContext) (revnum uint64, stateDirComplete bool) {
    headerfileChanged = e.runPreCompilationSteps()   // execute clang commands to compile BPF

    <-datapathRegenCtxt.ctCleaned                    // Wait for conntrack cleaning to complete
    compilationExecuted = e.realizeBPFState()        // execute tc commands to reload BPF

    if !datapathRegenCtxt.epInfoCache.IsHost() {
        // Hook the endpoint into the endpoint and endpoint to policy tables then expose it
        epErr := eppolicymap.WriteEndpoint(epInfoCache, e.policyMap)
        lxcmap.WriteEndpoint(epInfoCache)
    }

    e.closeBPFProgramChannel() // Signal that BPF program has been generated.
                               // The endpoint has at least L3/L4 connectivity at this point.
    e.syncPolicyMap()          // Synchronously try to update PolicyMap for this endpoint.
    stateDirComplete = headerfileChanged && compilationExecuted
    return datapathRegenCtxt.epInfoCache.revision, stateDirComplete, err
}
```

```go
// pkg/endpoint/bpf.go

func (e *Endpoint) realizeBPFState() (compilationExecuted bool, err error) {
    if regenLevel == RegenerateWithDatapathRebuild {        // compile AND load
        e.owner.Datapath().Loader().CompileAndLoad()
        compilationExecuted = true
    } else if regenLevel == RegenerateWithDatapathRewrite { // compile OR load
        e.owner.Datapath().Loader().CompileOrLoad()
        compilationExecuted = true
    } else { // RegenerateWithDatapathLoad                  // reload
        e.owner.Datapath().Loader().ReloadDatapath()
    }

    e.bpfHeaderfileHash = datapathRegenCtxt.bpfHeaderfilesHash
    return compilationExecuted, nil
}
```

```go
// pkg/datapath/loader/loader.go

func (l *Loader) CompileOrLoad(ctx context.Context, ep datapath.Endpoint, stats *metrics.SpanStat) error {
    templatePath := l.templateCache.fetchOrCompile(ctx, ep, stats)
    template := elf.Open(templatePath)

    symPath := path.Join(ep.StateDir(), defaults.TemplatePath)
    os.RemoveAll(symPath)
    os.Symlink(templatePath, symPath)

    epObj := endpointObj
    if ep.IsHost()
        epObj = hostEndpointObj

    dstPath := path.Join(ep.StateDir(), epObj)
    opts, strings := ELFSubstitutions(ep)
    template.Write(dstPath, opts, strings)

    return l.ReloadDatapath(ctx, ep, stats)
}

// ReloadDatapath reloads the BPF datapath pgorams for the specified endpoint.
func (l *Loader) ReloadDatapath(ctx context.Context, ep datapath.Endpoint, stats *metrics.SpanStat) (err error) {
    dirs := directoryInfo{
        Library: option.Config.BpfDir,
        Runtime: option.Config.StateDir,
        State:   ep.StateDir(),
        Output:  ep.StateDir(),
    }
    return l.reloadDatapath(ctx, ep, &dirs)
}
```

### 4.5.1 Pre-compile BPF

See `runPreCompilationSteps()`.

### 4.5.2 Compile BPF

See `realizeBPFState() -> CompileAndLoad() -> compileAndLoad() -> compileDatapath()`.

```go
// pkg/datapath/loader/loader.go

// CompileAndLoad compiles the BPF datapath programs for the specified endpoint
// and loads it onto the interface associated with the endpoint.
//
// Expects the caller to have created the directory at the path ep.StateDir().
func (l *Loader) CompileAndLoad(ctx context.Context, ep datapath.Endpoint, stats *metrics.SpanStat) error {
    dirs := directoryInfo{
        Library: option.Config.BpfDir,
        Runtime: option.Config.StateDir,
        State:   ep.StateDir(),
        Output:  ep.StateDir(),
    }
    return l.compileAndLoad(ctx, ep, &dirs, stats)
}

func (l *Loader) compileAndLoad(ctx context.Context, ep datapath.Endpoint, dirs *directoryInfo, stats *metrics.SpanStat) error {
    compileDatapath(ctx, dirs, ep.IsHost(), debug, ep.Logger(Subsystem))
    return l.reloadDatapath(ctx, ep, dirs)
}
```

### 4.5.3 Reload BPF

All the above three cases will eventually call `reloadDatapath()` to fulfill the
BPF reloading for the endpoint.

```go
// pkg/datapath/loader/loader.go

func (l *Loader) reloadDatapath(ctx context.Context, ep datapath.Endpoint, dirs *directoryInfo) error {
    objPath := path.Join(dirs.Output, endpointObj)

    if ep.IsHost() {
        objPath = path.Join(dirs.Output, hostEndpointObj)
        l.reloadHostDatapath(ctx, ep, objPath)
    } else if ep.HasIpvlanDataPath() {
        ...
    } else {
        l.replaceDatapath(ctx, ep.InterfaceName(), objPath, symbolFromEndpoint, dirIngress)

        if ep.RequireEgressProg()
            l.replaceDatapath(ctx, ep.InterfaceName(), objPath, symbolToEndpoint, dirEgress)
    }

    if ep.RequireEndpointRoute() {
        if ip := ep.IPv4Address(); ip.IsSet()
            upsertEndpointRoute(ep, *ip.IPNet(32))
    }
}
```

```go
// pkg/datapath/loader/netlink.go

// replaceDatapath the qdisc and BPF program for a endpoint
func (l *Loader) replaceDatapath(ctx context.Context, ifName, objPath, progSec, progDirection string) error {
    replaceQdisc(ifName)

    cmd := exec.CommandContext(ctx, "cilium-map-migrate", "-s", objPath)
    cmd.CombinedOutput(log, true)

    defer func() {
        if err == nil
            retCode = "0"
        else
            retCode = "1"
        args := []string{"-e", objPath, "-r", retCode}
        cmd := exec.CommandContext(ctx, "cilium-map-migrate", args...)
        cmd.CombinedOutput(log, true) // ignore errors
    }()

    args := []string{"filter", "replace", "dev", ifName, progDirection,
        "prio", "1", "handle", "1", "bpf", "da", "obj", objPath, "sec", progSec, }
    cmd = exec.CommandContext(ctx, "tc", args...).WithFilters(libbpfFixupMsg)
    cmd.CombinedOutput(log, true)
}
```
