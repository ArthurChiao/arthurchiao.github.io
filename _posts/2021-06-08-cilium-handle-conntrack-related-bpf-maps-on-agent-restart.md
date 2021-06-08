---
layout    : post
title     : "Cilium: Handle Conntrack (CT) related BPF Maps on Agent Restart"
date      : 2021-06-06
lastupdate: 2021-06-06
categories: cilium bpf conntrack
---

This post digs into the **<mark>handling of CT (conntrack) related BPF
maps</mark>** during agent restart.  Code based on Cilium `1.9.5`.

<p align="center"><img src="/assets/img/cilium-bpf-maps-handling/bpf-maps.png" width="60%" height="60%"></p>

If you're not familiar with CT, refer to
[Connection Tracking (conntrack): Design and Implementation Inside Linux Kernel]({% link _posts/2020-08-09-conntrack-design-and-implementation.md %})
[5] for some basic concepts.

This post is included in the
[Cilium Code Walk Through Series]({% link _posts/2019-06-17-cilium-code-series.md %}).

----

* TOC
{:toc}

----

# 1 Prerequisites and background knowledge

## 1.1 BPF maps

BPF map is a **<mark>BPF in-kernel infrastructure for efficient key-value storage</mark>**,
which also serves as **<mark>a fashion for sharing data</mark>** among:

* Different BPF programs
* Kernel and userspace programs

As depicted below:

<p align="center"><img src="/assets/img/cilium-bpf-maps-handling/bpf-maps.png" width="60%" height="60%"></p>
<p align="center">Fig 1. BPF maps, BPF programs and userspace programs on a Cilium node</p>

BPF maps (objects) could be **<mark>pinned to BPFFS</mark>**, which make them
survivable to agent restarts and node reboots.

## 1.2 BPF maps in Cilium

Cilium agent relies heavily on BPF maps, most of which are pinned to bpffs.
Let's first have a glimpse of them on a worker node:

```shell
root@node:/sys/fs/bpf/tc/globals $ ls
cilium_call_policy    cilium_calls_00571    cilium_calls_hostns_01320 cilium_calls_netdev_00008 cilium_ct4_global
cilium_ct_any4_global cilium_encrypt_state  cilium_events             cilium_ipcache            cilium_ipv4_frag_datagrams
cilium_lb4_affinity   cilium_lb4_backends   cilium_lb4_reverse_nat    cilium_lb4_services_v2    cilium_lb_affinity_match
cilium_lxc            cilium_metrics        cilium_policy_01955       cilium_signals            cilium_tunnel_map
...
```

The above maps can be groupped by their functionalities:

1. **<mark>Tail call</mark>**

    1. `cilium_calls_<ep_id>`
    1. `cilium_calls_hostns_<ep_id>`
    1. `cilium_calls_netdev_<ep_id>`

     Used for tail calling between different BPF programs.

     **Tail call is a special form of function call**, see [1] for how
     tail calls are performed in Cilium.

1. **<mark>Connection tracking</mark>** (conntrack, CT)

    1. `cilium_ct4_global`
    1. `cilium_ct4_<ep_id>`: if per-endpoint CT (`ConntrackLocal=true`) is enabled
    1. `cilium_ct_any4_global`

    Used for connection tracking purpose [2].

1. Encryption

    1. `cilium_encrypt_state`

1. **<mark>Load balancing</mark>**, or K8s Service handling

    1. `cilium_lb4_xxx`

    For client-side load balancing, e.g. K8s Service handling (mapping
    ServiceIP/ExternalIPs/NodePorts to backend PodIPs). Refer to [3] for more information.

1. **<mark>Network policy</mark>**

    1. `cilium_policy_<ep_id>`

    For enforcing CiliumNetworkPolicy (CNP), which implements and extends K8s's NetworkPolicy model.

1. events, metrics, etc

## 1.3 Inspect CT entries in Cilium

```shell
(node) $ cilium bpf ct list global | head
TCP IN  192.168.139.13:44808 -> 192.168.64.97:4240 expires=3553147 RxPackets=6 RxBytes=506 RxFlagsSeen=0x03 LastRxReport=3553137 TxPackets=4 TxBytes=347 TxFlagsSeen=0x03 LastTxReport=3553137 Flags=0x0013 [ RxClosing TxClosing SeenNonSyn ] RevNAT=0 SourceSecurityID=2 IfIndex=0
TCP OUT 192.168.54.113:36260 -> 192.168.198.12:4240 expires=3553014 RxPackets=14 RxBytes=1090 RxFlagsSeen=0x03 LastRxReport=3553004 TxPackets=9 TxBytes=704 TxFlagsSeen=0x03 LastTxReport=3553004 Flags=0x0013 [ RxClosing TxClosing SeenNonSyn ] RevNAT=0 SourceSecurityID=0 IfIndex=0
TCP OUT 192.168.54.113:39298 -> 192.168.245.13:4240 expires=3553107 RxPackets=14 RxBytes=1090 RxFlagsSeen=0x03 LastRxReport=3553097 TxPackets=9 TxBytes=704 TxFlagsSeen=0x03 LastTxReport=3553097 Flags=0x0013 [ RxClosing TxClosing SeenNonSyn ] RevNAT=0 SourceSecurityID=0 IfIndex=0
...
```

# 2 Functionality test: adjust CT table (map) size

If **<mark>CT table is full</mark>** because there are so many connections, we have to
adjust the table size (equivalent to CT map size), such as, through CLI parameters:

* `--bpf-ct-global-any-max=262144`
* `--bpf-ct-global-tcp-max=524288`

then restart the agent to load the configuration changes.

## 2.1 Concern: will existing connections be interrupted?

The question is: **<mark>will this operation disrupt the existing connections</mark>**?
For example, there are already hundreds of thousands of `established` connections on this node.

Let's test it.

## 2.2 Test case 1: inbound connection

First, test the inbound/ingress connections to the Pods on this node, as illustrated below:

<p align="center"><img src="/assets/img/cilium-bpf-maps-handling/ingress-connection.png" width="80%" height="80%"></p>
<p align="center">Fig 2.1 Inbound connection: access pod from other nodes</p>

### Step 1. Create an inbound connection

On another node: initiate a TCP connection and leave it there,

```shell
(node2) $ telnet 10.5.5.5 80
Connected to 10.5.5.5.
Escape character is '^]'.
```

### Step 2. Enlarge CT (map) size and restart agent

We use docker-compose to manage cilium-agents [4,6], so it is quite easy:

```shell
(node1) $ cd cilium-compose

(node1) $ sed -i 's/524288/1048576/g' mount/configmap/bpf-ct-global-tcp-max
(node1) $ sed -i 's/262144/524288/g'  mount/configmap/bpf-ct-global-any-max
(node1) $ docker-compose down && docker-compose up -d
```

### Step 3. Check connection liveliness

Back to node2, first noticed that our connection is **<mark>not disconnected but still alive</mark>**:

```shell
(node2) $ telnet 10.5.5.5 80
Connected to 10.5.5.5.
Escape character is '^]'.
```

Then let's **<mark>resume this connection by sending some data</mark>**: typing
something (e.g. `aaaaa`) then press `Enter`,

```shell
(node2) $ telnet 10.5.5.5 80
Connected to 10.5.5.5.
Escape character is '^]'.

aaaaa
HTTP/1.1 400 Bad Request
Server: nginx/1.11.1
...
```

The pod returns HTTP 400 errors, which **<mark>indicates that our TCP
connection is still OK</mark>** (`400` is a L7 error code, because we
typed some invalid HTTP data, which means L4 is OK).

## 2.3 Test case 2: outbound Service connections

This test case is different in that, the connection is targeted to a ServiceIP, so
**<mark>client side service handling</mark>**, or DNAT (ServiceIP->PodIP) to be
specific, will be performed.  Take a picture from [5]:

<p align="center"><img src="/assets/img/cilium-life-of-a-packet/pod-to-service-path.png" width="100%" height="100%"></p>
<p align="center">Fig 2.2. Traffic path of Pod-to-ServiceIP</p>

Create connection with command:

```shell
(pod on node1) $ telnet <ServiceIP> <Port>
```

The other steps are similar. Omit here for simplicity.

**<mark>The result of our test: the connction will not be interrupted either</mark>**.

## 2.4 Summary

Now we may wonder: **<mark>how does cilium-agent handles the table/map changes on restart</mark>**?

In the next, we'll explore the details in Cilium's design and implementation.

# 3 The code: BPF maps handling on agent restart

## 3.1 High-level overview (call stack)

Below is the high-level calling stack of the BPF maps handling logic:

```shell
runDaemon                                                                    //    daemon/cmd/daemon_main.go
  |-NewDaemon                                                                // -> daemon/cmd/daemon.go
  |-ctmap.InitMapInfo(conf)
  |  |-setupMapInfo(conf)
  |  |-"save map conf to mapInfo[], for later use"
  |
  |-d.initMaps()       // Open or create BPF maps.                           // daemon/cmd/datapath.go
     |-lxcmap.LXCMap.OpenOrCreate              // "cilium_lxc"
     |-ipcachemap.IPCache.OpenParallel         // "cilium_ipcache"
     |-tunnel.TunnelMap.OpenOrCreate           //
     |-supportedMapTypes := probe.GetMapTypes
     |-d.svc.InitMaps                          //
     |-policymap.InitCallMap                   // "cilium_call_xxx"
     |
     |-for ep in endpoints:
     |   ep.InitMap                                                           // -> pkg/endpoint/bpf.go
     |      |-policymap.Create(e.policyMapPath)
     |
     |-for ep in endpoints:
     |   if ep.ConntrackLocal():
     |     for m in ctmap.LocalMaps:           // "cilium_ct4_<ep_id>", "cilium_ct_any4_<ep_id>"
     |       m.Create()
     |
     |-for m in ctmap.GlobalMaps:              // "cilium_ct4_global", "cilium_ct_any4_global"
     |   m.Create                                                             // -> pkg/bpf/map_linux.go
     |     |-OpenOrCreate
     |       |-openOrCreate
     |         |-OpenOrCreateMap                                              // -> pkg/bpf/bpf_linux.go
     |
     |-for m in nat.GlobalMaps:
         m.Create // global NAT maps

```

## 3.2 Init map info from agent configurations: `ctmap.InitMapInfo()`

During agent restarting, one step is to **<mark>save</mark>** the user specified
**<mark>map configurations to a package-level variable</mark>**
`mapInfo map[mapType]mapAttributes`, by calling `runDaemon() -> ct.InitMapInfo()`

```go
// daemon/cmd/daemon.go

    ctmap.InitMapInfo(                // Corresponding CLI or configmap options:
        Config.CTMapEntriesGlobalTCP, // --bpf-ct-global-tcp-max=524288
        Config.CTMapEntriesGlobalAny, // --bpf-ct-global-any-max=262144
        Config.EnableIPv4,            // --enable-ipv4
        Config.EnableIPv6,            // --enable-ipv6
        Config.EnableNodePort)        // --enable-node-port
```

where `ctmap.InitMapInfo()` is defined as:

```go
// pkg/maps/ctmap/ctmap.go

var mapInfo map[mapType]mapAttributes  // package-level variable, holding CT maps' configurations

// Build information about different CT maps for the combination of L3/L4 protocols,
// using the specified limits on TCP vs non-TCP maps.
func InitMapInfo(tcpMaxEntries, anyMaxEntries int, v4, v6, nodeport bool) {
    global4Map, global6Map := nat.GlobalMaps(v4, v6, nodeport) // global CT maps

    natMaps := map[mapType]*nat.Map{
        mapTypeIPv4TCPLocal:  nil,        // SNAT only works if CT map is global
        mapTypeIPv4TCPGlobal: global4Map,
        mapTypeIPv4AnyGlobal: global4Map,
    }

    //           CT_TYPE         NAME          KEY              MAP_SIZE            CORRESPONDING_NAT_MAP
    setupMapInfo(IPv4TCPLocal,  "CT_MAP_TCP4", &CtKey4{},       mapNumEntriesLocal, natMaps[typeIPv4TCPLocal])
    setupMapInfo(IPv4TCPGlobal, "CT_MAP_TCP4", &CtKey4Global{}, tcpMaxEntries,      natMaps[typeIPv4TCPGlobal])
    setupMapInfo(IPv4AnyLocal,  "CT_MAP_ANY4", &CtKey4{},       mapNumEntriesLocal, natMaps[typeIPv4AnyLocal])
    setupMapInfo(IPv4AnyGlobal, "CT_MAP_ANY4", &CtKey4Global{}, anyMaxEntries,      natMaps[typeIPv4AnyGlobal])
}

func setupMapInfo(m mapType, define string, mapKey bpf.MapKey, keySize int, maxEntries int, nat *nat.Map) {
    mapInfo[m] = mapAttributes{
        bpfDefine: define,
        mapKey:    mapKey,
        mapValue:   &CtEntry{}, // for all CT maps, value type is the same: "struct CtEntry"
        maxEntries: maxEntries,
        parser:     bpf.ConvertKeyValue,
        natMap:     nat,
    }
}
```

When later the CT related BPF maps are being initialized,
they will retrieve map configurations from here (the configuration store `var mapInfo[]`).

## 3.3 Init map objects: `initMaps()`

After map configurations are parsed and saved, the next step is to
**<mark>create, remove or recreate them according to configuration changes</mark>**.

Let's concentrate on CT related code:

```
d.initMaps() // Open or create BPF maps.    // daemon/cmd/datapath.go
  |-...
  |
  |-for ep in endpoints:
  |   if ep.ConntrackLocal():
  |     for m in ctmap.LocalMaps:           // "cilium_ct4_<ep_id>", "cilium_ct_any4_<ep_id>"
  |       m.Create()
  |
  |-for m in ctmap.GlobalMaps:              // "cilium_ct4_global", "cilium_ct_any4_global"
  |   m.Create                              // -> pkg/bpf/map_linux.go
  |     |-OpenOrCreate
  |       |-openOrCreate
  |         |-OpenOrCreateMap               // -> pkg/bpf/bpf_linux.go
  |
  |-for m in nat.GlobalMaps: // global NAT maps
      m.Create
```

### ConntrackLocal or non-ConntrackLocal (default)

Cilium supports to store all CT entries into a global CT talbe (by default),
as well as store each endpoint's CT entries into their own CT tables (runtime
option `ConntrackLocal=true`, but this option [is broken currently](https://github.com/cilium/cilium/pull/16353)).


Despite its somewhat misleading name,`func (m *Map) Create()` is actually the
**<mark>entrypoint for creating/opening/removing/recreating a map</mark>**.
It is **<mark>similar to OpenOrCreate()</mark>**, but closes the map after creating or opening it.
We'll follow these methods later.

### Init global CT maps

The agent will always re-initialize the global CT maps by calling `m.Create()`.

## 3.4 `m.Create() -> m.OpenOrCreate() -> m.openOrCreate() -> Remove() && OpenOrCreateMap()`

Open or create maps:

```go
// pkg/bpf/map_linux.go

func (m *Map) Create() (bool, error) {
    isNew := m.OpenOrCreate()
    return isNew, m.Close()
}
```

`m.OpenOrCreate()` is again a wrapper, it calls an internal method `m.openOrCreate()`:

```go
// Returns whether the map was deleted and recreated, or an optional error.
func (m *Map) OpenOrCreate() (bool, error) {
    return m.openOrCreate(true)
}
```

```go
func (m *Map) openOrCreate(pin bool) (bool, error) {
    m.setPathIfUnset()

    if m.NonPersistent {  // If the map represents non-persistent data,
        os.Remove(m.path) // always remove it before opening or creating.
    }

    flags := m.Flags | GetPreAllocateMapFlags(mapType)
    fd, isNew := OpenOrCreateMap(m.path, mapType, m.KeySize, m.ValueSize, m.MaxEntries, flags, m.InnerID, pin)

    registerMap(m.path, m)
    return isNew, nil
}
```

Logic of the method:

1. If the map is marked as **<mark>non-persistent</mark>**, remove the map and it will later be recreated.

2. If the **<mark>existing map's attributes</mark>** such as map type, key/value size,
   capacity, etc. **<mark>changed</mark>**, then the map will
   be **<mark>deleted and reopened without any attempt to retain its previous
   contents</mark>**. This attributes-checking process is done in `OpenOrCreateMap()`, we'll see it later.

    This means that when we change the map size with `--bpf-ct-global-tcp-max=xxx`
    then restart the agent, the corresponding original/existing BPF map will always be deleted.

## 3.5 `OpenOrCreateMap() -> objCheck() -> Remove() && recreate -> CreateMap()`

Continue the calling stack:

```go
// pkg/bpf/bpf_linux.go

func OpenOrCreateMap(path, mapType, keySize, valueSize, maxEntries, flags, innerID, pin bool) (int, bool, error) {
    redo := false
    isNewMap := false

recreate:
    create := true

    // Step 1. ensure map directory exists
    if pin {
        if os.NotExist(path) || redo {
            os.MkdirAll(filepath.Dir(path), 0755)
        } else {
            create = false
        }
    }

    // Step 2. create map if not exist, then return
    if create {
        fd = CreateMap(mapType, keySize, valueSize, maxEntries, flags, innerID, path)
        isNewMap = true

        if pin {
            ObjPin(fd, path)
        }
        return fd, isNewMap, nil
    }

    // Step 3. map already exists, check if there are any map attribute changes,
    // if there is, delete the existing map
    fd = ObjGet(path)
    redo = objCheck(fd, path, mapType, keySize, valueSize, maxEntries, flags)

    // Step 4. recreate the map if there are attribute changes
    if redo == true {
        ObjClose(fd)   // close FD of existing map
        goto recreate
    }
    return fd, isNewMap, err
}
```

Logic:

1. **<mark>if map not exist</mark>**: **create the map** by calling `fd = CreateMap()`
2. **<mark>if map already exists</mark>**: call `objCheck()` to see **<mark>if there are any map attribute changes</mark>**, and if there is, it will

    1. remove the existing map, then
    2. return `hasChanged=true` to the caller `OpenOrCreateMap()`, which will create a map (`remove+create` = `re-create`).

```go
// pkg/bpf/bpf_linux.go

func objCheck(fd, path, mapType, keySize, valueSize, maxEntries, flags uint32) bool {
    info := GetMapInfo(os.Getpid(), fd)
    mismatch := false

    if info.MapType != mapType || info.KeySize != keySize ||
        info.ValueSize != valueSize || info.MaxEntries != maxEntries || info.Flags != flags {
        Warn("XX mismatch for BPF map")
        mismatch = true
    }

    if mismatch {
        if info.MapType == MapTypeProgArray {
            return false
        }

        Warning("Removing map to allow for property upgrade (expect map data loss)")

        os.Remove(path) // Kernel still holds map reference count via attached prog.
        return true     // Only exception is prog array, but that is already resolved differently.
    }

    return false
}
```

## 3.6 The final creation: `CreateMap() -> unix.SysCall()`

```
// pkg/bpf/bpf_linux.go

// When mapType is the type HASH_OF_MAPS an innerID is required to point at a
// map fd which has the same type/keySize/valueSize/maxEntries as expected map
// entries. For all other mapTypes innerID is ignored and should be zeroed.
func CreateMap(mapType, keySize, valueSize, maxEntries, flags, innerID, path) (int, error) {
    uba := struct {
        mapType    uint32
        keySize    uint32
        valueSize  uint32
        maxEntries uint32
        mapFlags   uint32
        innerID    uint32
    }{ uint32(mapType), keySize, valueSize, maxEntries, flags, innerID }

    unix.Syscall(unix.SYS_BPF, BPF_MAP_CREATE, uintptr(unsafe.Pointer(&uba)), unsafe.Sizeof(uba))

    runtime.KeepAlive(&uba)
    return int(ret), nil
}
```

# 4 Back to the functionality test

After a tour to the code, we can back to our previous test and find more information.

## 4.1 Agent logs on BPF removing

First, we could see from the code in Section 3.5 that, when map attributes are changed,
agent will remove the old map and create a new one, and logging a warning about this,
as the old data is deleted during this process:

```shell
level=warning msg="Max entries mismatch for BPF map"                                  file-path=/sys/fs/bpf/tc/globals/cilium_ct4_global new=624288 old=524288
level=warning msg="Removing map to allow for property upgrade (expect map data loss)" file-path=/sys/fs/bpf/tc/globals/cilium_ct4_global
```

## 4.2 Check CT entries during agent restart

We could also check the CT table contents during each step in Section 2.2:

### After connection established

```shell
$ cilium bpf ct list global 2>&1 | grep 192.168.64.195 | grep TCP
TCP IN 10.5.224.91:40356 -> 192.168.64.195:6379 expires=3295721 RxPackets=14 RxBytes=983 ...
```

CT entry **<mark>created</mark>**.

### After configuration changed and agent restarted

```shell
$ cilium bpf ct list global 2>&1 | grep 192.168.64.195 | grep TCP
# nothing found
```

CT entry **<mark>disappeared</mark>**.

### After we've sent some data

```shell
$ cilium bpf ct list global 2>&1 | grep 192.168.64.195 | grep TCP
TCP IN 10.5.224.91:40356 -> 192.168.64.195:6379 expires=3295750 RxPackets=2 RxBytes=140 RxFlagsSeen=0x02 ...
```

**<mark>CT entry "came back"</mark>**, but notice that all statistics, e.g. RxPackets, RxBytes has been reset.
The reason is that, as the code has shown, it won't dump existing data when re-creating a map.

# 5 Conclusion and future work

According to our test, when changing CT table sizes, existing CT entries will
be flushed, but the existing connections won't be interrupted.

More code should be digged into to explain why. May update later.

# References

1. [Life of a Packet in Cilium: Discovering the Pod-to-Service Traffic Path and BPF Processing Logics]({% link _posts/2020-09-12-cilium-life-of-a-packet-pod-to-service.md %})
2. [Connection Tracking (conntrack): Design and Implementation Inside Linux Kernel]({% link _posts/2020-08-09-conntrack-design-and-implementation.md %}).
3. [L4LB for Kubernetes: Theory and Practice with Cilium+BGP+ECMP]({% link _posts/2020-04-10-k8s-l4lb.md %})
4. [Trip.com: First Step towards Cloud Native Networking]({% link _posts/2020-01-19-trip-first-step-towards-cloud-native-networking.md %})
5. [Life of a Packet in Cilium: Discovering the Pod-to-Service Traffic Path and BPF Processing Logics]({% link _posts/2020-09-12-cilium-life-of-a-packet-pod-to-service.md %})
6. [https://github.com/ctripcloud/cilium-compose](https://github.com/ctripcloud/cilium-compose)
