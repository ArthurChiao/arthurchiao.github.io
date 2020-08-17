---
layout    : post
title     : "Cilium Code Walk Through: ClusterMesh"
date      : 2020-08-17
lastupdate: 2020-08-17
categories: cilium clustermesh
---

* TOC
{:toc}

---

```
bootstrapClusterMesh                                                    //    daemon/daemon.go
  |-NewClusterMesh                                                      //    pkg/clustermesh/clustermesh.go
     |-watcher := createConfigDirectoryWatcher                          // -> pkg/clustermesh/config.go
     |-watcher.watch                                                    // -> pkg/clustermesh/config.go
        |-add(configFile)                                               //    pkg/clustermesh/clustermesh.go
           |-onInsert                                                   //    pkg/clustermesh/remote_cluster.go
              |-restartRemoteConnection                                 //    pkg/clustermesh/remote_cluster.go
                 |-remoteNodes := store.JoinSharedStore()
                 |                 /
                 |   JoinSharedStore
                 |     |-listAndStartWatcher
                 |     |  |-go s.watcher()
                 |     |          |-updateKey                           //    pkg/kvstore/store/store.go
                 |     |             |-onUpdate                         //    pkg/kvstore/store/store.go
                 |     |                |-observer.OnUpdate             //    pkg/node/store/store.go
                 |     |                   |-NodeUpdated                // -> pkg/node/manager/manager.go
                 |     |                      |-ipcache.Upsert
                 |     |-syncLocalKeys(ctx)
                 |
                 |-remoteServices := store.JoinSharedStore()
                 |                   /
                 |   JoinSharedStore
                 |     |-listAndStartWatcher
                 |     |  |-go s.watcher()
                 |     |          |-updateKey                           //    pkg/kvstore/store/store.go
                 |     |             |-onUpdate                         //    pkg/kvstore/store/store.go
                 |     |                |-observer.OnUpdate             // -> pkg/clustermesh/services.go
                 |     |                   |-MergeExternalServiceUpdate // -> pkg/k8s/service_cache.go
                 |     |-syncLocalKeys(ctx)
                 |
                 |-remoteIdentityCache := WatchRemoteIdentities()       // -> pkg/identity/cache/allocator.go
                 |                        /
                 |   WatchRemoteIdentities
                 |     |-WatchRemoteKVStore                             // -> pkg/allocator/allocator.go
                 |         |-cache.start
                 |
                 |-ipCacheWatcher := NewIPIdentityWatcher()             // -> pkg/ipcache/kvstore.go
                 |-ipCacheWatcher.Watch()                               // -> pkg/ipcache/kvstore.go
                     |-IPIdentityCache.Upsert/Delete
```

# 1 Daemon start: `bootstrapClusterMesh()`

If `clustermesh-config` is configured (an absolute path, e.g
`/var/lib/clustermesh`), cilium agent will create a ClusterMesh instance by
calling `NewClusterMesh()` method:

```go
// daemon/daemon.go

func (d *Daemon) bootstrapClusterMesh(nodeMngr *nodemanager.Manager) {
    if path := option.Config.ClusterMeshConfig; path != "" {
        clustermesh := clustermesh.NewClusterMesh(clustermesh.Configuration{
            Name:                  "clustermesh",
            ConfigDirectory:       path,
            ServiceMerger:         &d.k8sWatcher.K8sSvcCache,
            RemoteIdentityWatcher: d.identityAllocator,
        })
    }
}
```

Each config file in the specified directory should contain the kvstore (cilium-etcd) information
of a remote cluster, you can [have a glimpse of these files]({% link _posts/2020-08-13-cilium-clustermesh.md %}) [1].

Now back to the code, let's see what `NewClusterMesh()` does.

# 2 Create clustermesh: `NewClusterMesh()`

`NewClusterMesh` ***creates a cache of a remote cluster*** based on the provided
information:

```go
// pkg/clustermesh/clustermesh.go

func NewClusterMesh(c Configuration) (*ClusterMesh, error) {
    cm := &ClusterMesh{
        conf:           c,
        clusters:       map[string]*remoteCluster{},
        controllers:    controller.NewManager(),
        globalServices: newGlobalServiceCache(),
    }

    cm.configWatcher = createConfigDirectoryWatcher(c.ConfigDirectory, cm)
    cm.configWatcher.watch() // watch file changes in config dir
}
```

It first creates a `ClusterMesh` instance, which contains following important fields:

* `clusters`: all remote k8s clusters in this mesh.
* `globalServices`: k8s Services that have **backend Pods in more than one clusters** in the mesh.

Then, the method creates a directory watcher, and the watcher **listens to config
file changes** by starting its `watch()` method.

## 2.1 Watch config directory

```go
// pkg/clustermesh/config.go

func (cdw *configDirectoryWatcher) watch() error {
    files := ioutil.ReadDir(cdw.path)              // read all files in config dir
    for _, f := range files {
        absolutePath := path.Join(cdw.path, f)
        if !isEtcdConfigFile(absolutePath)         // skip if it's not a config file
            continue

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

# 3 Create/recreate connection to remote etcd

Create/recreate connection to etcd:

```go
// pkg/clustermesh/remote_cluster.go

func (rc *remoteCluster) restartRemoteConnection(allocator RemoteIdentityWatcher) {
    rc.controllers.UpdateController(
        DoFunc: func(ctx context.Context) error {
            backend := kvstore.NewClient(kvstore.EtcdBackendName, rc.configPath) // etcd client

            remoteNodes := store.JoinSharedStore(store.Configuration{
                Prefix:                  path.Join("cilium/state/nodes/v1", rc.name),
                SynchronizationInterval: time.Minute,
                Backend:                 backend,
                Observer:                rc.mesh.conf.NodeObserver(),
            })

            remoteServices := store.JoinSharedStore(store.Configuration{
                Prefix: path.Join("cilium/state/services/v1", rc.name),
                ...
                Observer: &remoteServiceObserver{ remoteCluster: rc, },
            })

            remoteIdentityCache := allocator.WatchRemoteIdentities(backend)

            ipCacheWatcher := ipcache.NewIPIdentityWatcher(backend)
            go ipCacheWatcher.Watch(ctx)
        },
    )
}
```

As can be seen, after established connection to remote cilium-etcd, it will
listen and maintain a local cache for following remote resources:

* nodes: `cilium/state/nodes/v1` in remote cilium-etcd
* services: `cilium/state/services/v1` in remote cilium-etcd
* identity: `cilium/state/identities/v1` in remote cilium-etcd
* ipcache: `cilium/state/ip/v1` in remote cilium-etcd?

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

# References

1. [Cilium ClusterMesh: A Hands-on Guide]({% link _posts/2020-08-13-cilium-clustermesh.md %})
