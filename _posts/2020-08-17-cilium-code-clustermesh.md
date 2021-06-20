---
layout    : post
title     : "Cilium: What the Agents Do When Enabling ClusterMesh"
date      : 2020-08-17
lastupdate: 2021-06-20
categories: cilium clustermesh
---

This post walks through the ClusterMesh implementation in cilium.
Code based on `1.9.5`.

<p align="center"><img src="/assets/img/cilium-clustermesh/clustermesh.png" width="90%" height="90%"></p>

A previous post [Cilium ClusterMesh: A Hands-on Guide]({% link _posts/2020-08-13-cilium-clustermesh.md %})
is recommended (also where the above picture comes from) before reading this one.

This post is included in the
[Cilium Code Walk Through Series]({% link _posts/2019-06-17-cilium-code-series.md %}).

----

* TOC
{:toc}

---

```
NewDaemon
 |-bootstrapClusterMesh                                                     // daemon/daemon.go
   |-NewClusterMesh                                                         // pkg/clustermesh/clustermesh.go
     |-createConfigDirectoyWatcher                                          // pkg/clustermesh/config.go
     | |-watcher := fsnotify.NewWatcher()
     | |-watcher.Add("/var/lib/cilium/clustermesh")
     |
     |-configWatcher.watch()                                                // pkg/clustermesh/config.go
       |-for f := files
           handleAddedFile                                                  // pkg/clustermesh/config.go
            |-add()                                                         // pkg/clustermesh/clustermesh.go
              |-if !inserted // existing etcd config changed
                  changed <- true ------>----->-------\
                                                       |
                else // new etcd config added          |
                  onInsert                             |
                    |-go func() {        /----<-------/
                    |   for {            |
                    |     if val := <-changed; val
                    |       restartRemoteConnection -->--|   // re-create connection
                    |     else                           |
                    |       return                       |   // closing connection to remote etcd
                    |   }}()                             |
                    |                                    |
                    |-go func() {                        |
                    |   for {                            |
                    |     if <-statusCheckErrors         |   // Error observed on etcd connection
                    |       restartRemoteConnection -->--|
                    |   }}()                             |
                    |                                    |
                    |-restartRemoteConnection -------->--|                   // pkg/clustermesh/remote_cluster.go
                                                        /
restartRemoteConnection -----<----<------------<-------/                     // pkg/clustermesh/remote_cluster.go
  |-UpdateController(rc.remoteConnectionControllerName, // e.g. "remote-etcd-k8s-cluster2"
  |   DoFunc: func() {
  |    |-releaseOldConnection()
  |    |  |-go func() {
  |    |      ipCacheWatcher.Close()
  |    |      remoteNodes.Close()
  |    |      remoteIdentityCache.Close()
  |    |      remoteServices.Close()
  |    |      backend.Close()
  |    |       |-Close()                                                      // pkg/kvstore/etcd.go
  |    |         |-e.lockSession.Close() // revoke lock session
  |    |         |   |-Close()           // vendor/go.etcd.io/etcd/clientv3/concurrency/session.go
  |    |         |-e.session.Close()     // revoke main session
  |    |         |   |-Close()           // vendor/go.etcd.io/etcd/clientv3/concurrency/session.go
  |    |         |-e.client.Close()
  |    |    }
  |    |
  |    |-kvstore.NewClient                                                   // pkg/kvstore/client.go
  |    | |-module.newClient                                                  // pkg/kvstore/etcd.go
  |    |   |-for {
  |    |       backend := connectEtcdClient
  |    |         |-UpdateController("kvstore-etcd-session-renew")
  |    |         |-UpdateController("kvstore-etcd-lock-session-renew")
  |    |     }
  |    |
  |    |-remoteNodes = JoinSharedStore("cilium/state/nodes/v1")              // pkg/kvstore/store/store.go
  |    |     |-listAndStartWatcher
  |    |     |  |-go s.watcher()                                             // pkg/kvstore/store/store.go
  |    |     |        |-updateKey                           //    pkg/kvstore/store/store.go
  |    |     |           |-onUpdate                         //    pkg/kvstore/store/store.go
  |    |     |              |-observer.OnUpdate             //    pkg/node/store/store.go
  |    |     |                 |-NodeUpdated                // -> pkg/node/manager/manager.go
  |    |     |                    |-ipcache.Upsert
  |    |     |-syncLocalKeys(ctx)
  |    |
  |    |-remoteServices = JoinSharedStore("cilium/state/services/v1")
  |    |     |-listAndStartWatcher
  |    |     |  |-go s.watcher()
  |    |     |          |-updateKey                           //    pkg/kvstore/store/store.go
  |    |     |             |-onUpdate                         //    pkg/kvstore/store/store.go
  |    |     |                |-observer.OnUpdate             // -> pkg/clustermesh/services.go
  |    |     |                   |-MergeExternalServiceUpdate // -> pkg/k8s/service_cache.go
  |    |     |-syncLocalKeys(ctx)
  |    |
  |    |-WatchRemoteIdentities("cilium/state/identities/v1")  // -> pkg/identity/cache/allocator.go
  |    |   |-NewKVStoreBackend
  |    |   |-WatchRemoteKVStore                               // -> pkg/allocator/allocator.go
  |    |       |-cache.start
  |    |
  |    |-ipCacheWatcher = NewIPIdentityWatcher()             // -> pkg/ipcache/kvstore.go
  |    |-ipCacheWatcher.Watch()                               // -> pkg/ipcache/kvstore.go
  |        |-IPIdentityCache.Upsert/Delete
  |
  |   }
```

# 1 Daemon start: bootstrap ClusterMesh

If `--clustermesh-config` is provided (an absolute path, e.g
`/var/lib/clustermesh`), cilium agent will **<mark>create a ClusterMesh instance</mark>**
by calling `NewClusterMesh()`:

```go
// daemon/daemon.go

func (d *Daemon) bootstrapClusterMesh(nodeMngr *nodemanager.Manager) {
    if path := option.Config.ClusterMeshConfig; path != "" {
        clustermesh.NewClusterMesh(clustermesh.Configuration{
            Name:                  "clustermesh",
            ConfigDirectory:       path,          // "/var/lib/clustermesh"
            ServiceMerger:         &d.k8sWatcher.K8sSvcCache,
            RemoteIdentityWatcher: d.identityAllocator,
        })
    }
}
```

Each config file in the specified directory should contain **<mark>kvstore (cilium-etcd) information
of a remote cluster</mark>**, you can [have a glimpse of these files]({% link _posts/2020-08-13-cilium-clustermesh.md %}) [1].

Now back to the code, let's see what `NewClusterMesh()` actually does.

# 2 Create clustermesh: `NewClusterMesh()`

`NewClusterMesh` ***creates a cache of a remote cluster*** based on the provided
information:

```go
// pkg/clustermesh/clustermesh.go

func NewClusterMesh(c Configuration) (*ClusterMesh, error) {
    cm := &ClusterMesh{
        conf:           c,
        clusters:       map[string]*remoteCluster{},
        globalServices: newGlobalServiceCache(),
    }

    cm.configWatcher = createConfigDirectoryWatcher(c.ConfigDirectory, cm)
    cm.configWatcher.watch() // watch file changes in config dir
}
```

It first creates a `ClusterMesh` instance, which holds some important information like:

* `clusters`: all **<mark>remote k8s clusters</mark>** in this mesh.
* `globalServices`: k8s Services whose **<mark>backend Pods scattered in multiple clusters</mark>** in the mesh.

Then it creates a **<mark>directory watcher</mark>**, which
**<mark>listens to config file changes</mark>** in its `watch()` method.

## 2.1 Watch config directory

```go
// pkg/clustermesh/config.go

func (cdw *configDirectoryWatcher) watch() error {
    files := ioutil.ReadDir(cdw.path)              // read all files in config dir
    for _, f := range files {
        absolutePath := path.Join(cdw.path, f)
        cdw.lifecycle.add(f, absolutePath)         // tigger callback if new config file found
    }

    go func() {
        for {
            select {
            case event := <-cdw.watcher.Events:
                name := filepath.Base(event.Name)
                switch event.Op {
                case Create, Write, Chmod:
                    cdw.lifecycle.add(name, event) // trigger callback if config file added/updated
                case Remove, Rename:
                    cdw.lifecycle.remove(name)     // trigger callback if config file removed
                }
            }
        }
    }()
}
```

## 2.2 New config file found

```go
// pkg/clustermesh/clustermesh.go

func (cm *ClusterMesh) add(name, path string) {
    inserted := false
    cluster, ok := cm.clusters[name]
    if !ok {  // cluster not exist in local cache
        cluster = cm.newRemoteCluster(name, path)
        cm.clusters[name] = cluster
        inserted = true
    }

    if inserted { // trigger callback of `remoteCluster` instance
        cluster.onInsert(cm.conf.RemoteIdentityWatcher)
    } else {      // signal a change in configuration
        cluster.changed <- true
    }
}
```

The `remoteCluster`'s callback implementation:

```go
// pkg/clustermesh/remote_cluster.go

func (rc *remoteCluster) onInsert(allocator RemoteIdentityWatcher) {
    rc.remoteConnectionControllerName = fmt.Sprintf("remote-etcd-%s", rc.name)
    rc.restartRemoteConnection(allocator)  // start/restart connection to remote cilium-etcd

    go func() {
        for {
            val := <-rc.changed
            if val {
                rc.restartRemoteConnection(allocator)
            } else {
                return // Closing connection to remote etcd
            }
        }
    }()
}
```

`onInsert()` creates a or recreates the connection to the remote cilium-etcd
by calling method `restartRemoteConnection()`.

Remote connection controller name can be checked with CLI:

```shell
(node@cluster1) $ cilium status --all-controllers | grep remote
  remote-etcd-k8s-cluster2          73h37m30s ago   never        0       no error
```

# 3 Create/recreate connection to remote etcd

Create/recreate connection to etcd:

```go
// pkg/clustermesh/remote_cluster.go

func (rc *remoteCluster) restartRemoteConnection(allocator RemoteIdentityWatcher) {
    rc.controllers.UpdateController(rc.remoteConnectionControllerName,
        controller.ControllerParams{
            DoFunc: func(ctx context.Context) error {
                rc.releaseOldConnection()

                backend := kvstore.NewClient(rc.configPath, ExtraOptions{NoLockQuorumCheck: true})
                Info("Connection to remote cluster established")

                remoteNodes := store.JoinSharedStore(store.Configuration{
                    Prefix:                  "cilium/state/nodes/v1",
                    KeyCreator:              rc.mesh.conf.NodeKeyCreator,
                    SynchronizationInterval: time.Minute,
                    Backend:                 backend,
                    Observer:                rc.mesh.conf.NodeObserver(),
                })

                remoteServices := store.JoinSharedStore(store.Configuration{
                    Prefix:                  "cilium/state/services/v1",
                    KeyCreator: func() store.Key { return serviceStore.ClusterService{} },
                    SynchronizationInterval: time.Minute,
                    Backend:                 backend,
                    Observer: &remoteServiceObserver{
                        remoteCluster: rc,
                        swg:           rc.swg,
                    },
                })

                remoteIdentityCache := allocator.WatchRemoteIdentities(backend)

                ipCacheWatcher := ipcache.NewIPIdentityWatcher(backend)
                go ipCacheWatcher.Watch(ctx)

                Info("Established connection to remote etcd")
            },
            StopFunc: func(ctx context.Context) error {
                rc.releaseOldConnection()
                Info("All resources of remote cluster cleaned up")
                return nil
            },
        },
    )
}
```

As can be seen, after establishing connection to remote cilium-etcd, it will
listen and maintain a local cache for following remote resources:

* **<mark>nodes</mark>**: `cilium/state/nodes/v1` in remote cilium-etcd
* **<mark>services</mark>**: `cilium/state/services/v1` in remote cilium-etcd
* **<mark>identity</mark>**: `cilium/state/identities/v1` in remote cilium-etcd
* **<mark>ipcache</mark>**: `cilium/state/ip/v1` in remote cilium-etcd?

Note that when listening to remote nodes and services, it registered a
corresponding `Observer`; when there are resource changes, the observer will be
called. We will see this soon.

# 4 Sync remote resources to local caches

Let's see the remote resources that will be cached locally on this node:

* nodes
* services
* identity
* ipcache

## 4.1 Sync nodes to local cache

### 4.1.1 `JoinSharedStore()`

This method will listen to the specified resource, and merge them with local
ones:

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

### 4.1.2 On resource update/create/delete

Take create/update as example.

`updateKey()` will call `onUpdate()`, which will further call observer's
`OnUpdate` handler. For node resource,

```go
// pkg/node/store/store.go

func (o *NodeObserver) OnUpdate(k store.Key) {
    if n, ok := k.(*node.Node); ok {
        nodeCopy := n.DeepCopy()
        nodeCopy.Source = source.KVStore
        o.manager.NodeUpdated(*nodeCopy)
    }
}
```

Let's check out `manager.NodeUpdated(*nodeCopy)`.

### 4.1.3 On node updated

`NodeUpdated` is called after the information of a node has been updated.

```go
// If an update or addition has occurred, NodeUpdate() of the datapath interface is invoked.
func (m *Manager) NodeUpdated(n node.Node) {
    dpUpdate := true

    remoteHostIdentity := identity.ReservedIdentityHost
    if m.conf.RemoteNodeIdentitiesEnabled() && n.Source != source.Local
        remoteHostIdentity = identity.ReservedIdentityRemoteNode

    for _, address := range n.IPAddresses {
        // Upsert() returns true if the ipcache entry is owned by the source of the node update
        isOwning := m.ipcache.Upsert(address.IP.String(), .., ipcache.Identity{
            ID:     remoteHostIdentity,
            Source: n.Source,
        })
        if !isOwning {  // The datapath is only updated if that source of truth is updated.
            dpUpdate = false
        }
    }

    entry, oldNodeExists := m.nodes[nodeIdentity]
    if oldNodeExists { // update
        oldNode := entry.node
        entry.node = n
        if dpUpdate
            m.Iter(func(nh datapath.NodeHandler) { nh.NodeUpdate(oldNode, entry.node) })
    } else {           // create
        entry = &nodeEntry{node: n}
        m.nodes[nodeIdentity] = entry
        if dpUpdate
            m.Iter(func(nh datapath.NodeHandler) { nh.NodeAdd(entry.node) })
    }
}
```

### 4.1.4 ipcache insert

`NodeUpdated()` calls `ipcache.Upsert()` to update node information:

```go
// pkg/ipcache/ipcache.go

// Upsert adds / updates the provided IP (endpoint or CIDR prefix) and identity into the IPCache.
//
// k8sMeta contains Kubernetes-specific
// metadata such as pod namespace and pod name belonging to the IP (may be nil).
func (ipc *IPCache) Upsert(ip string, hostIP net.IP, hostKey uint8, k8sMeta *K8sMetadata, newIdentity Identity) bool {
    callbackListeners := true
    oldHostIP, oldHostKey := ipc.getHostIPCache(ip)
    oldK8sMeta := ipc.ipToK8sMetadata[ip]

    cachedIdentity, found := ipc.ipToIdentityCache[ip]
    if found {
        if !source.AllowOverwrite(cachedIdentity.Source, newIdentity.Source)
            return false
        oldIdentity = &cachedIdentity.ID
    }

    // Endpoint IP identities take precedence over CIDR identities, so if the
    // IP is a full CIDR prefix and there's an existing equivalent endpoint IP, don't notify the listeners.
    if cidr, err = net.ParseCIDR(ip); err == nil {
        ones, bits := cidr.Mask.Size()
        if ones == bits
            if endpointIPFound := ipc.ipToIdentityCache[cidr.IP.String()]; endpointIPFound
                callbackListeners = false
    } else if endpointIP := net.ParseIP(ip); endpointIP != nil { // Endpoint IP.
        cidr = endpointIPToCIDR(endpointIP)

        // Check whether the upserted endpoint IP will shadow that CIDR, and
        // replace its mapping with the listeners if that was the case.
        if !found {
            if cidrIdentity, cidrFound := ipc.ipToIdentityCache[cidrStr]; cidrFound {
                oldHostIP = ipc.getHostIPCache(cidrStr)
                if cidrIdentity.ID != newIdentity.ID || !oldHostIP.Equal(hostIP)
                    oldIdentity = &cidrIdentity.ID
                else                          // The endpoint IP and the CIDR are associated with the
                    callbackListeners = false // same identity and host IP. Nothing changes for the listeners.
            }
        }
    } else {
        return false // Attempt to upsert invalid IP into ipcache layer
    }

    // Update both maps.
    ipc.ipToIdentityCache[ip] = newIdentity
    if found { // Delete the old identity, if any.
        delete(ipc.identityToIPCache[cachedIdentity.ID], ip)
        if len(ipc.identityToIPCache[cachedIdentity.ID]) == 0
            delete(ipc.identityToIPCache, cachedIdentity.ID)
    }
    if ok := ipc.identityToIPCache[newIdentity.ID]; !ok
        ipc.identityToIPCache[newIdentity.ID] = map[string]struct{}{}

    ipc.identityToIPCache[newIdentity.ID][ip] = struct{}{}

    if   hostIP == nil: delete(ipc.ipToHostIPCache, ip)
    else                ipc.ipToHostIPCache[ip] = IPKeyPair{IP: hostIP, Key: hostKey}

    if   k8sMeta == nil: delete(ipc.ipToK8sMetadata, ip)
    else                 ipc.ipToK8sMetadata[ip] = *k8sMeta

    if callbackListeners
        for _, listener := range ipc.listeners
            listener.OnIPIdentityCacheChange(Upsert, *cidr, oldHostIP, hostIP, oldIdentity, ...)
}
```

## 4.2 Sync services to local cache

Similar call stacks as section 4.1, but the observer will eventually call `MergeExternalServiceUpdate()`:

```go
// pkg/k8s/service_cache.go

// MergeExternalServiceUpdate merges a cluster service of a remote cluster into
// the local service cache. The service endpoints are stored as external endpoints
// and are correlated on demand with local services via correlateEndpoints().
func (s *ServiceCache) MergeExternalServiceUpdate(service *service.ClusterService, ..) {
    id := ServiceID{Name: service.Name, Namespace: service.Namespace}
    if service.Cluster == option.Config.ClusterName // Ignore updates of own cluster
        return

    externalEndpoints, ok := s.externalEndpoints[id]
    if !ok {
        externalEndpoints = newExternalEndpoints()
        s.externalEndpoints[id] = externalEndpoints
    }

    backends := map[string]*Backend{}
    for ipString, portConfig := range service.Backends
        backends[ipString] = &Backend{Ports: portConfig}

    externalEndpoints.endpoints[service.Cluster] = &Endpoints{ Backends: backends, }

    svc, ok := s.services[id]
    endpoints, serviceReady := s.correlateEndpoints(id)

    // Only send event notification if service is shared and ready.
    // External endpoints are still tracked but correlation will not happen until the service is marked as shared.
    if ok && svc.Shared && serviceReady
        s.Events <- ServiceEvent{
            Action:    UpdateService,
            ID:        id,
            Service:   svc,
            Endpoints: endpoints,
        }
}
```


## 4.3 Sync identities to local cache

```go
// WatchRemoteKVStore starts watching an allocator base prefix the kvstore represents by the provided backend.
func (a *Allocator) WatchRemoteKVStore(remoteAlloc *Allocator) *RemoteCache {
    rc := &RemoteCache{
        cache:     newCache(remoteAlloc),
        allocator: remoteAlloc,
    }

    a.remoteCaches[rc] = struct{}{}

    rc.cache.start()
}
```

```go
// pkg/allocator/cache.go

// start requests a LIST operation from the kvstore and starts watching the
// prefix in a go subroutine.
func (c *cache) start() waitChan {
    // start with a fresh nextCache
    c.nextCache = idMap{}
    c.nextKeyCache = keyMap{}

    go func() {
        c.allocator.backend.ListAndWatch(context.TODO(), c, c.stopChan)
    }()
}
```

## 4.4 Sync ip identities to local cache

```go
// pkg/ipcache/kvstore.go

func (iw *IPIdentityWatcher) Watch(ctx context.Context) {
restart:
    watcher := iw.backend.ListAndWatch(ctx, "endpointIPWatcher", IPIdentitiesPath, 512)

    for {
        select {
        case event, ok := <-watcher.Events:
            if !ok {
                time.Sleep(500 * time.Millisecond)
                goto restart
            }

            // Synchronize local caching of endpoint IP to ipIDPair mapping
            //
            // To resolve conflicts between hosts and full CIDR prefixes:
            // - Insert hosts into the cache as ".../w.x.y.z"
            // - Insert CIDRS into the cache as ".../w.x.y.z/N"
            // - If a host entry created, notify the listeners.
            // - If a CIDR is created and there's no overlapping host entry, ie it is a less than fully
            //   masked CIDR, OR it is a fully masked CIDR and there is no corresponding host entry, then:
            //   - Notify the listeners.
            //   - Otherwise, do not notify listeners.
            // - If a host is removed, check for an overlapping CIDR
            //   and if it exists, notify the listeners with an upsert for the CIDR's identity
            // - If any other deletion case, notify listeners of the deletion event.
            switch event.Typ {
            case ListDone:
                for _, listener := range IPIdentityCache.listeners
                    listener.OnIPIdentityCacheGC()
                close(iw.synced)
            case Create, Modify:
                json.Unmarshal(event.Value, &ipIDPair)
                ip := ipIDPair.PrefixString()

                if ipIDPair.K8sNamespace != "" || ipIDPair.K8sPodName != ""
                    k8sMeta = &K8sMetadata{
                        Namespace: ipIDPair.K8sNamespace,
                        PodName:   ipIDPair.K8sPodName,
                    }

                IPIdentityCache.Upsert(ip, ipIDPair.HostIP, ipIDPair.Key, k8sMeta, Identity{
                    ID:     ipIDPair.ID,
                    Source: source.KVStore,
                })
            case Delete:
                ...
            }
        }
    }
}
```

# 5 Misc: create and close connection to remote kvstores

## 5.1 Create etcd client: `newClient() -> connectEtcdClient()`

```go
// pkg/kvstore/etcd.go

func (e *etcdModule) newClient(ctx context.Context, opts *ExtraOptions) (BackendOperations, chan error) {
    errChan := make(chan error, 10)

    clientOptions := clientOptions{
        KeepAliveHeartbeat: 15 * time.Second,
        KeepAliveTimeout:   25 * time.Second,
        RateLimit:          defaults.KVstoreQPS,
    }

    // parse configurations
    if o, ok := e.opts[EtcdRateLimitOption]; ok && o.value != "" {
        clientOptions.RateLimit, _ = strconv.Atoi(o.value)
    }

    if o, ok := e.opts[etcdOptionKeepAliveTimeout]; ok && o.value != "" {
        clientOptions.KeepAliveTimeout, _ = time.ParseDuration(o.value)
    }

    if o, ok := e.opts[etcdOptionKeepAliveHeartbeat]; ok && o.value != "" {
        clientOptions.KeepAliveHeartbeat, _ = time.ParseDuration(o.value)
    }

    endpointsOpt, endpointsSet := e.opts[EtcdAddrOption]
    configPathOpt, configSet := e.opts[EtcdOptionConfig]

    if configSet {
        configPath = configPathOpt.value
    }

    for {
        backend := connectEtcdClient(ctx, e.config, configPath, errChan, clientOptions, opts)
        switch {
        case os.IsNotExist(err):
            log.WithError(err).Info("Waiting for all etcd configuration files to be available")
            time.Sleep(5 * time.Second)
        case err != nil:
            errChan <- err
            close(errChan)
            return backend, errChan
        default:
            return backend, errChan
        }
    }
}
```

Create etcd client **<mark>supports rate limiting</mark>** (QPS).

Example configuration (configmap) [2]:

* `kvstore='etcd'`
* `kvstore-opt='{"etcd.config": "/var/lib/etcd-config/etcd.config", "etcd.qps": "30"}'`

```go
func connectEtcdClient(ctx context.Context, config *client.Config, cfgPath string, errChan chan error, clientOptions clientOptions, opts *ExtraOptions) (BackendOperations, error) {
    if cfgPath != "" {
        cfg := newConfig(cfgPath)
        cfg.DialOptions = append(cfg.DialOptions, config.DialOptions...)
        config = cfg
    }

    // Set DialTimeout to 0, otherwise the creation of a new client will
    // block until DialTimeout is reached or a connection to the server is made.
    config.DialTimeout = 0

    // Ping the server to verify if the server connection is still valid
    config.DialKeepAliveTime = clientOptions.KeepAliveHeartbeat

    // Timeout if the server does not reply within 15 seconds and close the connection.
    // Ideally it should be lower than staleLockTimeout
    config.DialKeepAliveTimeout = clientOptions.KeepAliveTimeout

    c := client.New(*config)
    Info("Connecting to etcd server...")

    var s, ls concurrency.Session

    ec := &etcdClient{
        client:               c,
        config:               config,
        configPath:           cfgPath,
        session:              &s,
        lockSession:          &ls,
        firstSession:         make(chan struct{}),
        controllers:          controller.NewManager(),
        latestStatusSnapshot: "Waiting for initial connection to be established",
        stopStatusChecker:    make(chan struct{}),
        extraOptions:         opts,
        limiter:              rate.NewLimiter(rate.Limit(clientOptions.RateLimit), clientOptions.RateLimit),
        statusCheckErrors:    make(chan error, 128),
    }

    // create session in parallel as this is a blocking operation
    go func() {
        session := concurrency.NewSession(c, concurrency.WithTTL(int(option.Config.KVstoreLeaseTTL.Seconds())))
        lockSession := concurrency.NewSession(c, concurrency.WithTTL(int(defaults.LockLeaseTTL.Seconds())))

        log.Infof("Got lease ID %x", s.Lease())
        log.Infof("Got lock lease ID %x", ls.Lease())
    }()

    // wait for session to be created also in parallel
    go func() {
        select {
        case err = <-errorChan:
            if err != nil {
                handleSessionError(err)
                return
            }
        case <-time.After(initialConnectionTimeout):
            handleSessionError(fmt.Errorf("timed out while waiting for etcd session. Ensure that etcd is running on %s", config.Endpoints))
            return
        }

        Info("Initial etcd session established")
    }()

    go func() {
        watcher := ec.ListAndWatch(ctx, HeartbeatPath, HeartbeatPath, 128)

        for {
            select {
            case _, ok := <-watcher.Events:
                if !ok {
                    log.Debug("Stopping heartbeat watcher")
                    watcher.Stop()
                    return
                }

                ec.lastHeartbeat = time.Now()
                Debug("Received update notification of heartbeat")
            }
        }
    }()

    go ec.statusChecker()

    ec.controllers.UpdateController("kvstore-etcd-session-renew",
        controller.ControllerParams{
            Context: ec.client.Ctx(),
            DoFunc: func(ctx context.Context) error {
                return ec.renewSession(ctx)
            },
            RunInterval: time.Duration(10) * time.Millisecond,
        },
    )

    ec.controllers.UpdateController("kvstore-etcd-lock-session-renew",
        controller.ControllerParams{
            Context: ec.client.Ctx(),
            DoFunc: func(ctx context.Context) error {
                return ec.renewLockSession(ctx)
            },
            RunInterval: time.Duration(10) * time.Millisecond,
        },
    )

    return ec, nil
}
```

## 5.2 Close session

```go
// pkg/kvstore/etcd.go

// Close closes the etcd session
func (e *etcdClient) Close() {
    close(e.stopStatusChecker)
    sessionErr := e.waitForInitialSession(context.Background())
    if e.controllers != nil {
        e.controllers.RemoveAll()
    }

    if sessionErr == nil { // Only close e.lockSession if the initial session was successful
        if err := e.lockSession.Close(); err != nil {
            e.getLogger().WithError(err).Warning("Failed to revoke lock session while closing etcd client")
        }
    }
    if sessionErr == nil { // Only close e.session if the initial session was successful
        if err := e.session.Close(); err != nil {
            e.getLogger().WithError(err).Warning("Failed to revoke main session while closing etcd client")
        }
    }
    if e.client != nil {
        if err := e.client.Close(); err != nil {
            e.getLogger().WithError(err).Warning("Failed to close etcd client")
        }
    }
}
```

```go
// vendor/go.etcd.io/etcd/clientv3/concurrency/session.go

// Orphan ends the refresh for the session lease. This is useful
// in case the state of the client connection is indeterminate (revoke
// would fail) or when transferring lease ownership.
func (s *Session) Orphan() {
    s.cancel()
    <-s.donec
}

// Close orphans the session and revokes the session lease.
func (s *Session) Close() error {
    s.Orphan()

    // if revoke takes longer than the ttl, lease is expired anyway
    ctx, cancel := context.WithTimeout(s.opts.ctx, time.Duration(s.opts.ttl)*time.Second)
    _, err := s.client.Revoke(ctx, s.id)
    cancel()
    return err
}
```

# References

1. [Cilium ClusterMesh: A Hands-on Guide]({% link _posts/2020-08-13-cilium-clustermesh.md %})
2. [kvstore/etcd: fix etcd rate limit (QPS) not working](https://github.com/cilium/cilium/pull/15742)
