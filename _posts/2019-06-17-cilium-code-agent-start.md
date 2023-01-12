---
layout    : post
title     : "Cilium Code Walk Through: Agent Start"
date      : 2019-05-29
lastupdate: 2022-10-27
categories: cilium
---

This post walks through the cilium agent start process.
Code based on `1.8.2 ~ 1.11.10`.

This post is included in
[Cilium Code Walk Through Series]({% link _posts/2019-06-17-cilium-code-series.md %}).

----

* TOC
{:toc}

----

```shell
init                                                 // daemon/cmd/daemon_main.go
 |-cobra.OnInitialize(option.InitConfig())           // pkg/option/config.go
    |-option.InitConfig()                            // pkg/option/config.go
      |-ReadDirConfig
      |-MergeConfig
         |-viper.MergeConfigMap

runDaemon                                                                    //    daemon/cmd/daemon_main.go
  |-enableIPForwarding
  |-k8s.Init                                                                 // -> pkg/k8s/init.go
  |-NewDaemon                                                                // -> daemon/cmd/daemon.go
  |  |-ctmap.InitMapInfo()
  |  |-policymap.InitMapInfo()
  |  |-lbmap.Init()
  |  |-nd := nodediscovery.NewNodeDiscovery()
  |  |-d := Daemon{}
  |  |
  |  |-d.k8sWatcher = watchers.NewK8sWatcher()
  |  |-RegisterK8sSyncedChecker
  |  |
  |  |-d.initMaps                                                            //    daemon/cmd/datapath.go
  |  |  |-lxcmap.LXCMap.OpenOrCreate()
  |  |  |-ipcachemap.IPCache.OpenParallel()
  |  |  |-d.svc.InitMaps
  |  |  |-for m in ctmap.GlobalMaps:
  |  |       m.Create()
  |  |-d.svc.RestoreServices                                                 // -> pkg/service/service.go
  |  |  |-restoreBackendsLocked
  |  |  |-restoreServicesLocked
  |  |
  |  |-restoredEndpoints := d.fetchOldEndpoints()
  |  |
  |  |-d.k8sWatcher.RunK8sServiceHandler                                     //    pkg/k8s/watchers/watcher.go
  |  |  |-k8sServiceHandler                                                  //    pkg/k8s/watchers/watcher.go
  |  |    |-eventHandler                                                     //    pkg/k8s/watchers/watcher.go
  |  |
  |  |-k8s.RegisterCRDs
  |  |
  |  |-cachesSynced := make(chan struct{})
  |  |-d.k8sCachesSynced = cachesSynced
  |  |-InitK8sSubsystem(cachesSynced)
  |  |   |-EnableK8sWatcher                                                  // pkg/k8s/watchers/watcher.go
  |  |   | |-initEndpointsOrSlices
  |  |   |     |-endpointsInit                                               // pkg/k8s/watchers/endpoint.go
  |  |   |       |-endpointController := informer.NewInformer(
  |  |   |           k8sClient.CoreV1().RESTClient(), "endpoints",
  |  |   |           cache.Handlers{
  |  |   |             UpdateFunc: func() {
  |  |   |               updateK8sEndpointV1
  |  |   |                |-k.K8sSvcCache.UpdateEndpoints(newEP, swg)
  |  |   |                |-addKubeAPIServerServiceEPs
  |  |   |                   |-handleKubeAPIServerServiceEPChanges
  |  |   |                      |-ipcache.IPIdentityCache.TriggerLabelInjection
  |  |   |                          |-DoFunc: InjectLabels
  |  |   |             }
  |  |   |           }
  |  |   |-go func() {
  |  |   |   log.Info("Waiting until all pre-existing resources have been received")
  |  |   |   k.WaitForCacheSync(resources...)
  |  |   |   close(cachesSynced)
  |  |   | }()
  |  |   |
  |  |   |-go func() {
  |  |       select {
  |  |       case <-cachesSynced:
  |  |           log.Info("All pre-existing resources have been received; continuing")
  |  |       case <-time.After(option.Config.K8sSyncTimeout):
  |  |           log.Fatal("Timed out waiting for pre-existing resources to be received; exiting")
  |  |       }
  |  |     }()
  |  |
  |  |-d.bootstrapIPAM                                                       // -> daemon/cmd/ipam.go
  |  |
  |  |-restoredEndpoints := d.restoreOldEndpoints                            // -> daemon/cmd/state.go
  |  |  |-ioutil.ReadDir
  |  |  |-endpoint.FilterEPDir // filter over endpoint directories
  |  |  |-for ep := range possibleEPs
  |  |      validateEndpoint(ep)
  |  |        |-allocateIPsLocked
  |  |-k8s.Client().AnnotateNode
  |  |-d.bootstrapClusterMesh
  |  |-d.init                                                                //    daemon/cmd/daemon.go
  |  |  |-os.MkdirAll(globalsDir)
  |  |  |-d.createNodeConfigHeaderfile
  |  |  |   |-f = os.Create("node_config.h")
  |  |  |   |-WriteNodeConfig(f, LocalConfig)// fill up node_config.h
  |  |  |      |-ctmap.WriteBPFMacros()      // #define CT_MAP_XXX
  |  |  |-d.Datapath().Loader().Reinitialize // modify node_config.h
  |  |-monitoragent.NewAgent
  |  |-d.syncEndpointsAndHostIPs                                             // -> daemon/cmd/datapath.go
  |  |  |-insert special identities to lxcmap, ipcache
  |  |-UpdateController("sync-endpoints-and-host-ips")
  |  |-loader.RestoreTemplates                                               // -> pkg/datapath/loader/cache.go
  |  |  |-os.RemoveAll()
  |  |-ipcache.InitIPIdentityWatcher                                         // -> pkg/ipcache/kvstore.go
  |     |-watcher = NewIPIdentityWatcher
  |     |-watcher.Watch
  |        |-IPIdentityCache.Upsert/Delete
  |
  |-gc.Enable                                                                // -> pkg/maps/ctmap/gc/gc.go
  |   |-for { runGC() } // conntrack & nat gc
  |-initKVStore
  |  |-UpdateController("kvstore-locks-gc", RunLocksGC)
  |  |-kvstore.Setup
  |-initRestore(restoredEndpoints)
  |  |-regenerateRestoredEndpoints(restoredEndpoints)                        // daemon/cmd/state.go
  |  |-UpdateController("sync-lb-maps-with-k8s-services")
  |
  |-d.endpointManager.AddHostEndpoint
  |
  |-initHealth
  |-startStatusCollector
  |  |-status.NewCollector(probes)                                           // pkg/status
  |-startAgentHealthHTTPService
  |-SendNotification
  |  |-monitorAgent.SendEvent(AgentNotifyStart)
  |-srv.Serve()  // start Cilium agent API server
  |-k8s.Client().MarkNodeReady()
  |-launchHubble()
```

# 0 Overview

The daemon process starts by executing command

```shell
$ cilium-agent --config-dir=/tmp/cilium/config-map
```

This will trigger `runDaemon()` method with provided CLI arguments:

```go
// daemon/cmd/daemon_main.go

func runDaemon() {
    enableIPForwarding()                                  // turn on ip forwarding in kernel
    k8s.Init(Config)                                      // init k8s utils

    d, restoredEndpoints := NewDaemon()
    gc.Enable(restoredEndpoints.restored)                 // Starting connection tracking garbage collector
    d.initKVStore()                                       // init cilium-etcd

    restoreComplete := d.initRestore(restoredEndpoints)

    d.initHealth()                                        // init cilium health-checking if enabled
    d.startStatusCollector()
    d.startAgentHealthHTTPService()

    d.SendNotification(monitorAPI.AgentNotifyStart, repr)

    go func() { errs <- srv.Serve() }()                   // start Cilium HTTP API server

    k8s.Client().MarkNodeReady(nodeTypes.GetName())
    d.launchHubble()
}
```

This method performs following tasks sequentially:

1. Prapare to create daemon
    1. Enable IP forwarding in kernel
    2. Init k8s package
2. Create new daemon, restore endpoints
3. Enable conntrack/nat GC
4. Init kvstore (cilium-etcd)
5. Regenerate BPF for endpoints
6. Init health checks, status checker, metrics
7. Send agent started messsage to monitor
8. Serve Cilium HTTP API
9. Mark node as ready in k8s
10. Start hubble if enabled

# 1 Prepare to create daemon

## 1.1 Enable IP forwarding: `enableIPForwarding()`

For Linux, this will set below sysctl parameters for the node:

* `net.ipv4.ip_forward=1`
* `net.ipv6.ip_forward=1`
* `net.ipv4.conf.all.forwarding=1`

## 1.2 Init k8s package: `k8s.Init()`

```go
// Init initializes the Kubernetes package. It is required to call Configure() beforehand.
func Init(conf k8sconfig.Configuration) error {
    k8sRestClient := createDefaultClient()
    heartBeat := func() { k8sRestClient.Get().Resource("healthz").Do(ctx) }

    if Config.K8sHeartbeatTimeout != 0
        controller.NewManager().UpdateController("k8s-heartbeat", ControllerParams{
                DoFunc: ...
                RunInterval: Config.K8sHeartbeatTimeout,
            })

    k8sversion.Update(Client(), conf)
}
```

# 2 Create daemon: `NewDaemon()`

```go
// daemon/cmd/daemon.go

func NewDaemon(ctx , cancel context.CancelFunc, epMgr *endpointmanager.EndpointManager, dp datapath.Datapath) (*Daemon, *endpointRestoreState, error) {
    if option.Config.ReadCNIConfiguration != "" {
        netConf = cnitypes.ReadNetConf(option.Config.ReadCNIConfiguration)
    }

    apiLimiterSet := rate.NewAPILimiterSet(option.Config.APIRateLimit, apiRateLimitDefaults, &apiRateLimitingMetrics{})

    ctmap.InitMapInfo()
    policymap.InitMapInfo(option.Config.PolicyMapEntries)
    lbmap.Init()

    authKeySize, encryptKeyID := setupIPSec()
    nodeMngr := nodemanager.NewManager("all", dp.Node(), ipcache.IPIdentityCache, option.Config, nil, nil)
    num := identity.InitWellKnownIdentities(option.Config)
    nd := nodediscovery.NewNodeDiscovery(nodeMngr, mtuConfig, netConf)

    d := Daemon{
        ctx:               ctx,
        cancel:            cancel,
        prefixLengths:     createPrefixLengthCounter(),
        buildEndpointSem:  semaphore.NewWeighted(int64(numWorkerThreads())),
        compilationMutex:  new(lock.RWMutex),
        netConf:           netConf,
        mtuConfig:         mtuConfig,
        datapath:          dp,
        deviceManager:     NewDeviceManager(),
        nodeDiscovery:     nd,
        endpointCreations: newEndpointCreationManager(),
        apiLimiterSet:     apiLimiterSet,
    }

    d.configModifyQueue = eventqueue.NewEventQueueBuffered("config-modify-queue", ConfigModifyQueueSize)
    d.configModifyQueue.Run()

    d.svc = service.NewService(&d)
    d.rec = recorder.NewRecorder(d.ctx, &d)

    d.identityAllocator = NewCachingIdentityAllocator(&d)
    d.initPolicy(epMgr)
    nodeMngr = nodeMngr.WithSelectorCacheUpdater(d.policy.GetSelectorCache()) // must be after initPolicy
    nodeMngr = nodeMngr.WithPolicyTriggerer(d.policyUpdater)                  // must be after initPolicy

    ipcache.IdentityAllocator = d.identityAllocator
    proxy.Allocator = d.identityAllocator

    restoredCIDRidentities := make(map[string]*identity.Identity)

    d.endpointManager = epMgr

    d.redirectPolicyManager = redirectpolicy.NewRedirectPolicyManager(d.svc)

    d.k8sWatcher = watchers.NewK8sWatcher(
        d.endpointManager,
        d.nodeDiscovery.Manager,
        &d,
        d.policy,
        d.svc,
        d.datapath,
        d.redirectPolicyManager,
        d.bgpSpeaker,
        d.egressGatewayManager,
        option.Config,
    )
    nd.RegisterK8sNodeGetter(d.k8sWatcher)

    ipcache.IPIdentityCache.RegisterK8sSyncedChecker(&d)

    d.k8sWatcher.NodeChain.Register(d.endpointManager)
    d.k8sWatcher.NodeChain.Register(watchers.NewCiliumNodeUpdater(d.nodeDiscovery))

    d.redirectPolicyManager.RegisterSvcCache(&d.k8sWatcher.K8sSvcCache)
    d.redirectPolicyManager.RegisterGetStores(d.k8sWatcher)

    // Open or create BPF maps.
    d.initMaps()

    // Upsert restored CIDRs after the new ipcache has been opened above
    if len(restoredCIDRidentities) > 0 {
        ipcache.UpsertGeneratedIdentities(restoredCIDRidentities, nil)
    }

    d.svc.RestoreServices()

    d.k8sWatcher.RunK8sServiceHandler()
    treatRemoteNodeAsHost := option.Config.AlwaysAllowLocalhost() && !option.Config.EnableRemoteNodeIdentity
    policyApi.InitEntities(option.Config.ClusterName, treatRemoteNodeAsHost)

    // fetch old endpoints before k8s is configured.
    restoredEndpoints := d.fetchOldEndpoints(option.Config.StateDir)

    d.bootstrapFQDN(restoredEndpoints.possible, option.Config.ToFQDNsPreCache)

    if k8s.IsEnabled() {
        if err := d.k8sWatcher.WaitForCRDsToRegister(d.ctx); err != nil {
            return nil, restoredEndpoints, err
        }

        d.k8sWatcher.NodesInit(k8s.Client())

        if option.Config.IPAM == ipamOption.IPAMClusterPool {
            // Create the CiliumNode custom resource. This call will block until
            // the custom resource has been created
            d.nodeDiscovery.UpdateCiliumNodeResource()
        }

        k8s.WaitForNodeInformation(d.ctx, d.k8sWatcher)

        if option.Config.AllowLocalhost == option.AllowLocalhostAuto {
            option.Config.AllowLocalhost = option.AllowLocalhostAlways
            log.Info("k8s mode: Allowing localhost to reach local endpoints")
        }

    }

    if wgAgent := dp.WireguardAgent(); option.Config.EnableWireguard {
        if err := wgAgent.Init(mtuConfig); err != nil {
            log.WithError(err).Error("failed to initialize wireguard agent")
        }
    }

    bandwidth.ProbeBandwidthManager()

    d.deviceManager.Detect()
    finishKubeProxyReplacementInit(isKubeProxyReplacementStrict)

    if k8s.IsEnabled() {
        // Initialize d.k8sCachesSynced before any k8s watchers are alive, as they may
        // access it to check the status of k8s initialization
        cachesSynced := make(chan struct{})
        d.k8sCachesSynced = cachesSynced

        // Launch the K8s watchers in parallel as we continue to process other daemon options.
        d.k8sWatcher.InitK8sSubsystem(d.ctx, cachesSynced)
    }

    clearCiliumVeths()

    // Must init kvstore before starting node discovery
    if option.Config.KVStore == "" {
        log.Info("Skipping kvstore configuration")
    } else {
        d.initKVStore()
    }

    router4FromK8s, router6FromK8s := node.GetInternalIPv4Router(), node.GetIPv6Router()

    d.configureIPAM()

    d.startIPAM()

    if option.Config.EnableIPv4 {
        d.restoreCiliumHostIPs(false, router4FromK8s)
    }

    d.restoreOldEndpoints(restoredEndpoints, true)

    d.allocateIPs()

    // Must occur after d.allocateIPs(), see GH-14245 and its fix.
    d.nodeDiscovery.StartDiscovery()

    // Annotation of the k8s node must happen after discovery of the
    // PodCIDR range and allocation of the health IPs.
    if k8s.IsEnabled() && option.Config.AnnotateK8sNode {
        k8s.Client().AnnotateNode()
    }

    if option.Config.IPAM == ipamOption.IPAMCRD || option.Config.IPAM == ipamOption.IPAMENI || option.Config.IPAM == ipamOption.IPAMAzure || option.Config.IPAM == ipamOption.IPAMAlibabaCloud {
        if option.Config.EnableIPv4 {
            d.ipam.IPv4Allocator.RestoreFinished()
        }
    }

    if option.Config.DatapathMode != datapathOption.DatapathModeLBOnly {
        realIdentityAllocator := d.identityAllocator
        realIdentityAllocator.InitIdentityAllocator(k8s.CiliumClient(), nil)

        d.bootstrapClusterMesh(nodeMngr)
    }

    d.init()

    d.updateDNSDatapathRules(d.ctx)

    d.syncEndpointsAndHostIPs()

    controller.NewManager().UpdateController("sync-endpoints-and-host-ips",
        controller.ControllerParams{
            DoFunc: func(ctx ) error {
                return d.syncEndpointsAndHostIPs()
            },
            RunInterval: time.Minute,
            Context:     d.ctx,
        })

    loader.RestoreTemplates(option.Config.StateDir)

    // Start watcher for endpoint IP --> identity mappings in key-value store.
    // this needs to be done *after* init() for the daemon in that function,
    // we populate the IPCache with the host's IP(s).
    ipcache.InitIPIdentityWatcher()
    identitymanager.Subscribe(d.policy)

    return &d, restoredEndpoints, nil
}
```

## 2.1 Read custom CNI file if configured

See [Trip.com: First Step towards Cloud-native Networking]({% link _posts/2020-01-19-trip-first-step-towards-cloud-native-networking.md %})
for how custom CNI can be used.

## 2.2 Create daemon instance

It then initiates a daemon instance, and its fields:

```go
    d := Daemon{ netConf: netConf, datapath: dp }
    d.svc = service.NewService(&d)
    d.identityAllocator = cache.NewCachingIdentityAllocator(&d)
    d.policy = policy.NewPolicyRepository()
    ...
```

## 2.3`initMaps()`: open or create all BPF maps

```go
// daemon/cmd/datapath.go

// initMaps opens all BPF maps (and creates them if they do not exist).
func (d *Daemon) initMaps() error {
    lxcmap.LXCMap.OpenOrCreate()
    ipcachemap.IPCache.OpenParallel()
    ...
    d.svc.InitMaps(Config.EnableIPv4, createSockRevNatMaps, Config.RestoreState)
    policymap.InitCallMap()

    for ep := range d.endpointManager.GetEndpoints()
        ep.InitMap()

    for ep := range d.endpointManager.GetEndpoints()
        for m := range ctmap.LocalMaps(ep, Config.EnableIPv4)
            m.Create()

    for m := range ctmap.GlobalMaps(Config.EnableIPv4)
        m.Create()

    ipv4Nat := nat.GlobalMaps(Config.EnableIPv4)
    ipv4Nat.Create()
    if Config.EnableNodePort
       neighborsmap.InitMaps(Config.EnableIPv4)

    // Set up the list of IPCache listeners in the daemon, to be used by syncEndpointsAndHostIPs()
    ipcache.IPIdentityCache.SetListeners()

    if !Config.RestoreState
        lxcmap.LXCMap.DeleteAll() // If we are not restoring state, all endpoints can be deleted.

    if Config.EnableSessionAffinity {
        lbmap.AffinityMatchMap.OpenOrCreate()
        lbmap.Affinity4Map.OpenOrCreate()
    }
}
```

## 2.4 `RestoreServices()`: restore `service <-> backend` mappings from BPF maps

```go
    if Config.RestoreState
        d.svc.RestoreServices() // Read the service IDs of existing services from the BPF map and reserve them.
```

```go
// pkg/service/service.go

// RestoreServices restores services from BPF maps.
//
// The method should be called once before establishing a connectivity to kube-apiserver.
func (s *Service) RestoreServices() error {
    s.restoreBackendsLocked() // Restore backend IDs
    s.restoreServicesLocked() // Restore service cache from BPF maps

    if option.Config.EnableSessionAffinity
        s.deleteOrphanAffinityMatchesLocked() // Remove no longer existing affinity matches

    s.deleteOrphanBackends() // Remove obsolete backends and release their IDs
}
```

## 2.5 `RunK8sServiceHandler()`: listen to k8s `Service` changes

```go
    d.k8sWatcher.RunK8sServiceHandler()
```

```go
// pkg/k8s/watchers/watcher.go

func (k *K8sWatcher) RunK8sServiceHandler() {
    go k.k8sServiceHandler()
}

func (k *K8sWatcher) k8sServiceHandler() {
    eventHandler := func(event k8s.ServiceEvent) {
        svc := event.Service

        switch event.Action {
        case k8s.UpdateService:
            k.addK8sSVCs(event.ID, event.OldService, svc, event.Endpoints)
            translator := k8s.NewK8sTranslator(event.ID, *event.Endpoints, false, svc.Labels, true)
            result := k.policyRepository.TranslateRules(translator)
            if result.NumToServicesRules > 0 // Only trigger policy updates if ToServices rules are in effect
                k.policyManager.TriggerPolicyUpdates(true, "Kubernetes service endpoint added")

        case k8s.DeleteService:
            ...
        }
    }

    for {
        event := <-k.K8sSvcCache.Events // Kubernetes service definition changed
        eventHandler(event)
    }
}
```

## 2.6 RegisterCRDs

```go
    k8s.RegisterCRDs()

    if Config.IPAM == ipamIPAMOperator
        d.nodeDiscovery.UpdateCiliumNodeResource()
```


## 2.7 bootstrapIPAM

```go
    d.bootstrapIPAM()
```

```go
// daemon/cmd/ipam.go

func (d *Daemon) bootstrapIPAM() {
    if Config.IPv4Range != AutoCIDR {
        allocCIDR := cidr.ParseCIDR(Config.IPv4Range)
        node.SetIPv4AllocRange(allocCIDR)
    }

    node.AutoComplete()

    // Set up ipam conf after init() because we might be running d.conf.KVStoreIPv4Registration
    d.ipam = ipam.NewIPAM(d.datapath.LocalNodeAddressing(), option.Config, d.nodeDiscovery, d.k8sWatcher)
}
```

## 2.8 `restoreOldEndpoints()`: extract endpoints info from local files

```go
    restoredEndpoints := d.restoreOldEndpoints(Config.StateDir, true)
    d.allocateIPs()
```

```go
// daemon/cmd/state.go

// Perform the first step in restoring the endpoint structure,
// allocating their existing IP out of the CIDR block and then inserting the
// endpoints into the endpoints list. It needs to be followed by a call to
// regenerateRestoredEndpoints() once the endpoint builder is ready.
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


## 2.9 AnnotateNode

```go
    // Annotation must after discovery of the PodCIDR range and allocation of the health IPs.
    k8s.Client().AnnotateNode(nodeName, GetIPv4AllocRange(), GetInternalIPv4())
```

See [Cilium Code Walk Through: Agent CIDR Init]({% link _posts/2019-06-17-cilium-code-agent-cidr-init.md %}).

## 2.10 `d.init()`: clean state dir, setup sockops, init datapath loader

```go
    d.bootstrapClusterMesh(nodeMngr)
    d.init()
    d.monitorAgent = monitoragent.NewAgent(context.TODO(), defaults.MonitorBufferPages)
    d.syncEndpointsAndHostIPs()

    controller.NewManager().UpdateController("sync-endpoints-and-host-ips", ControllerParams{
            DoFunc: func() { return d.syncEndpointsAndHostIPs() }, })

```

```go
// daemon/cmd/daemon.go

func (d *Daemon) init() error {
    globalsDir := option.Config.GetGlobalsDir()
    os.MkdirAll(globalsDir, defaults.RuntimePathRights)
    os.Chdir(option.Config.StateDir)

    // Remove any old sockops and re-enable with _new_ programs if flag is set
    sockops.SockmapDisable()
    sockops.SkmsgDisable()

    d.createNodeConfigHeaderfile() // create /var/run/cilium/state/globals/node_config.h

    if Config.SockopsEnable {
        eppolicymap.CreateEPPolicyMap()
        sockops.SockmapEnable()
    }

    d.Datapath().Loader().Reinitialize(d.ctx, d, mtu, d.Datapath(), d.l7Proxy, d.ipam)
}
```

```go
// daemon/cmd/datapath.go

// syncLXCMap adds local host enties to bpf lxcmap, as well as ipcache, if needed, and also
// notifies the daemon and network policy hosts cache if changes were made.
func (d *Daemon) syncEndpointsAndHostIPs() error {
    specialIdentities := []identity.IPIdentityPair{}

    addrs := d.datapath.LocalNodeAddressing().IPv4().LocalAddresses()
    for ip := range addrs
        specialIdentities.append(IPIdentityPair{IP: ip, ID: identity.ReservedIdentityHost})

    specialIdentities.append(identity.IPIdentityPair{
            IP:   net.IPv4zero,
            Mask: net.CIDRMask(0, net.IPv4len*8),
            ID:   identity.ReservedIdentityWorld,
        })

    existingEndpoints := lxcmap.DumpToMap()

    for ipIDPair := range specialIdentities {
        hostKey := node.GetIPsecKeyIdentity()
        isHost := ipIDPair.ID == identity.ReservedIdentityHost
        if isHost
            lxcmap.SyncHostEntry(ipIDPair.IP) // Added local ip to endpoint map

        delete(existingEndpoints, ipIDPair.IP.String())

        // Upsert will not propagate (reserved:foo->ID) mappings across the cluster, designed so.
        ipcache.IPIdentityCache.Upsert(ipIDPair.Prefix, hostKey,
            Identity{ID: ipIDPair.ID, Source: source.Local})
    }

    for hostIP, info := range existingEndpoints
        if ip := net.ParseIP(hostIP); info.IsHost()
            lxcmap.DeleteEntry(ip) // Removed outdated host ip from endpoint map
            ipcache.IPIdentityCache.Delete(hostIP, source.Local)
        }
}
```

## 2.11 RestoreTemplates

```go
    loader.RestoreTemplates(Config.StateDir) // restore previous BPF templates
```

```go
// pkg/datapath/loader/cache.go

// RestoreTemplates populates the object cache from templates on the filesystem at the specified path.
func RestoreTemplates(stateDir string) error {
    path := filepath.Join(stateDir, defaults.TemplatesDir)
    RemoveAll(path)
}
```

## 2.12 Start identity watcher

```go
    // Start watcher for endpoint IP --> identity mappings in key-value store.
    ipcache.InitIPIdentityWatcher()
    identitymanager.Subscribe(d.policy)
```

This will listen to the `ip -> identity` mapping changes in kvstore, to be specific,
it will listen to `cilium/state/ip/v1/` resource in kvstore, an example:

* key: `cilium/state/ip/v1/default/192.168.1.2`.
* value: `{"IP":"192.168.1.2","Mask":null,"HostIP":"xx","ID":44827,"Key":0,"Metadata":"cilium-global:default:node1:2191","K8sNamespace":"default","K8sPodName":"pod-1"}`,
  note that the `ID` field is just the identity.

```go
// pkg/ipcache/kvstore.go

func InitIPIdentityWatcher() {
    go func() {
        watcher = NewIPIdentityWatcher(kvstore.Client())
        watcher.Watch(context.TODO())
    }()
}

func (iw *IPIdentityWatcher) Watch(ctx ) {
    watcher := iw.backend.ListAndWatch(ctx, "endpointIPWatcher", IPIdentitiesPath) // cilium/state/ip/v1/

    for {
        select {
        case event, ok := <-watcher.Events:
            switch event.Typ {
            case EventTypeListDone:
                for listener := range IPIdentityCache.listeners
                    listener.OnIPIdentityCacheGC()

            case EventTypeCreate, EventTypeModify:
                json.Unmarshal(event.Value, &ipIDPair)
                IPIdentityCache.Upsert(ip, HostIP, ipIDPair.Key, k8sMeta, Identity{ipIDPair.ID, source.KVStore})

            case kvstore.EventTypeDelete: ...
            }
        }
    }
}
```

# 3 `gc.Enable()`: enable conntrack/nat GC

```go
// pkg/maps/ctmap/gc/gc.go

// Enable enables the connection tracking garbage collection.
func Enable(ipv4, ipv6 bool, restoredEndpoints []*endpoint.Endpoint, mgr EndpointManager) {
    go func() {
        for {
            epsMap = make(map[string]*endpoint.Endpoint) // contains an IP -> EP mapping.

            gcStart = time.Now()
            eps := mgr.GetEndpoints()
            for e := range eps
                epsMap[e.GetIPv4Address()] = e

            if len(eps) > 0 || initialScan
                maxDeleteRatio = runGC(nil, ipv4, createGCFilter(initialScan, restoredEndpoints, emitEntryCB))

            for e := range eps
                runGC(e, ipv4, &ctmap.GCFilter{RemoveExpired: true, EmitCTEntryCB: emitEntryCB})
    }()
}
```

# 4 `initKVStore()`: setup some GC jobs, connect to kvstore

```go
// daemon/cmd/daemon_main.go

func (d *Daemon) initKVStore() {
    controller.NewManager().UpdateController("kvstore-locks-gc", ControllerParams{
            // agent writes a file as lock before accessing a key in kvstore, run GC for those lock files
            DoFunc: func() { kvstore.RunLockGC() },
            RunInterval: defaults.KVStoreStaleLockTimeout,
        })

    kvstore.Setup(Config.KVStore, Config.KVStoreOpt) // connect to kvstore
}
```

# 5 `initRestore()`: regenerate BPF for restored endpoints

```go
// daemon/cmd/state.go

func (d *Daemon) initRestore(restoredEndpoints *endpointRestoreState) chan struct{} {
    // When we regenerate restored endpoints, it is guaranteed tha we have
    // received the full list of policies present at the time the daemon is bootstrapped.
    restoreComplete = d.regenerateRestoredEndpoints(restoredEndpoints)

    go func() {
        if d.clustermesh != nil               // wait for all cluster mesh to be synchronized
            d.clustermesh.ClustersSynced()    // with the datapath before proceeding.

        // Start controller which removes any leftover Kubernetes services that may have been deleted
        // while Cilium was not running. It will not run again unless updated elsewhere.
        // This means that if, for instance, a user manually adds a service via the CLI into the BPF maps,
        // that it will not be cleaned up by the daemon until it restarts.
        controller.NewManager().UpdateController("sync-lb-maps-with-k8s-services", ControllerParams{
                DoFunc: func() { return d.svc.SyncWithK8sFinished() },
            })
    }()
    return restoreComplete
}
```

## 5.1 Re-regenerate restored endpoints

This will regenerate BPF for all restored endpoints.

See [Cilium Code Walk Through: Agent Restore Endpoints And Identities]({% link _posts/2019-06-17-cilium-code-restore-endpoints.md %}).

## 5.2 Init ClusterMesh if enabled

See [Cilium Code Walk Through: ClusterMesh]({% link _posts/2020-08-17-cilium-code-clustermesh.md %}).

# 6 Init health checks, metrics, Cilium API server

## 6.1 Init health checks

See [Cilium Code Walk Through: Node & Endpoint Health Probe]({% link _posts/2020-12-31-cilium-code-health-probe.md %}).

## 6.2 Init status collector

```go
//daemon/cmd/status.go

func (d *Daemon) startStatusCollector() {
    probes := []status.Probe{
        {
            Name: "check-locks",
            Probe: func() (interface{}, error) { ...  },
        }, {
            Name: "kvstore",
            Probe: func() { return kvstore.Client().Status() },
        }, {
            Name: "kubernetes",
            Probe: func() () { return d.getK8sStatus()
        }, {
            Name: "ipam", ...
        }, {
            Name: "node-monitor",
            Probe: func() () { return d.monitorAgent.State() },
        }, {
            Name: "cluster",
            Probe: func() {return &models.ClusterStatus{d.nodeDiscovery.LocalNode.Fullname() }
        }, {
            Name: "cilium-health", ...
        }, {
            Name: "l7-proxy",
            Probe: func() () { return d.l7Proxy.GetStatusModel() },
        }, {
            Name: "controllers",
            Probe: func() () { return controller.GetGlobalStatus(), nil },
        }, {
            Name: "clustermesh",
            Probe: func() () { return d.clustermesh.Status(), nil },
        },
    }

    d.statusCollector = status.NewCollector(probes, status.Config{})
}
```

# 7 Send notification to monitor

Sends a `"Cilium agent started"` (`monitorAPI.AgentNotifyStart`) message to the monitor.

```go
// daemon/cmd/daemon.go

// sends an agent notification to the monitor
func (d *Daemon) SendNotification(typ, text string) {
    event := monitorAPI.AgentNotify{Type: typ, Text: text}
    return d.monitorAgent.SendEvent(MessageTypeAgent, event)
}
```

# 8 Serve Cilium HTTP API

At this point, `cilium` commands can be correctly get handled, e.g. `cilium status --brief`.

# 9 Mark node ready

# 10 Launch hubble if configured

# Misc

## IPAM States Restoration

IPAM manages IP address allocation, it tracks two states:

```
{
    "allocated_ips": [], // IPs have been allocated out
    "available_ips": [], // IPs avaiable for allocation
}
```

IPAM stores its states **in memory**. How could this survive agent restart or
host reboot?

The secret lies in the **files on local node**:

1. For each allocated IP, Cilium creates an endpoint for it, and write the
   endpoint info into a local file (C header file).
2. When agent restarts or host reboots, IPAM states in memory will be reset. The
   agent will loop over all endpoint files, parsing the IP inside it, and
   reserve them in IPAM.

In this way, IPAM restores its states.
