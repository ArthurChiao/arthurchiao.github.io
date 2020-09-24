---
layout: post
title:  "Cilium Code Walk Through: CNI Create Network"
date      : 2019-02-08
lastupdate: 2020-09-24
categories: cilium bpf cni
---

This post belongs to
[Cilium Code Walk Through Series]({% link _posts/2019-06-17-cilium-code-series.md %}).

### TL;DR

This post walks through the code of **Cilium CNI creating network for a Pod**.
Call stack (code based on `1.8.2`/`1.5.1`):

```shell
cmdAdd                                         // plugins/cilium-cni/cilium-cni.go
  |-loadNetConf(args.StdinData)                // plugins/cilium-cni/cilium-cni.go
  |-RemoveIfFromNetNSIfExists
  |-SetupVeth or IPVLAN
  |-IPAMAllocate                               // plugins/cilium-cni/cilium-cni.go
  | |-PostIPAM                                 // api/v1/client/ipam/i_p_a_m_client.go
  |   /\
  |   ||
  |   \/
  |   ServeHTTP                                // api/v1/server/restapi/ipam/post_ip_a_m.go
  |     |-Handler.Handle()                     // api/v1/server/restapi/ipam/post_ip_a_m.go
  |       |- Handle()                          // daemon/cmd/ipam.go
  |         |-AllocateNext                     // pkg/ipam/allocator.go
  |           |-AllocateNextFamily             // pkg/ipam/allocator.go
  |             |-allocateNextFamily           // pkg/ipam/allocator.go
  |               |-AllocateNext               // k8s.io/kubernetes/pkg/registry/core/service/ipallocator
  |                 |-AllocateNext             // k8s.io/kubernetes/pkg/registry/core/service/allocator/bimap.go
  |-configureIface                             //
  | |-addIPConfigToLink
  |   |-netlink.AddrAdd
  |   |-netlink.RouteAdd
  |-EndpointCreate
    |-PutEndpointID
      /\
      ||
      \/
  ServeHTTP                                     // api/server/restapi/endpint/put_endpoint_id.go
   |-Handler.Handle()                           // api/server/restapi/endpint/put_endpoint_id.go
     |- Handle()                                // daemon/cmd/endpoint.go
       |-createEndpoint                         // daemon/cmd/endpoint.go
         |-NewEndpointFromChangeModel           // pkg/endpoint/api.go
         |-fetchK8sLabelsAndAnnotations         // daemon/cmd/endpoint.go
         |-endpointmanager.AddEndpoint        
         |  |-Expose                            // pkg/endpointmanager/manager.go
         |     |-AllocateID                     // pkg/endpoint/manager.go
         |     |-RunK8sCiliumEndpointSync(e)    // pkg/k8s/watchers/endpointsynchronizer.go
         |-ep.UpdateLabels                      // pkg/endpoint/endpoint.go
         |  |-replaceInformationLabels          // pkg/endpoint/endpoint.go
         |  |-ReplaceIdentityLabels             // pkg/endpoint/endpoint.go
         |     |-RunIdentityResolver            // pkg/endpoint/endpoint.go
         |        |-identityLabelsChanged       // pkg/endpoint/endpoint.go
         |           |-AllocateIdentity         // kvstore: reuse existing or create new one
         |           |-forcePolicyComputation
         |           |-SetIdentity
         |              |-runIPIdentitySync     // pkg/endpoint/policy.go
         |                 |-UpsertIPToKVStore  // pkg/ipcache/kvstore.go
         |-Regenerate                           // pkg/endpoint/policy.go
           |-regenerate                         // pkg/endpoint/policy.go
             |-regenerateBPF                    // pkg/endpoint/bpf.go
               |-runPreCompilationSteps       
               | |-regeneratePolicy           
               | |-writeHeaderfile            
               |-realizeBPFState              
                 |-CompileAndLoad               // pkt/datapath/loader/loader.go
                   |-compileAndLoad             // pkt/datapath/loader/loader.go
                     |-compileDatapath          // pkt/datapath/loader/loader.go
                     | |-compile                // pkt/datapath/loader/compile.go
                     |   |-compileAndLink       // pkt/datapath/loader/compile.go
                     |-reloadDatapath           // pkt/datapath/loader/loader.go
                       |-replaceDatapath        // pkt/datapath/loader/netlink.go
```

----

* TOC
{:toc}

----

When creating a Pod in a k8s cluster, `kubelet` will call **CNI plugin** to
**create network for this Pod**. The specific steps each CNI plugin does
vary a lot, and in this post we will dig into the Cilium's one.

# 0 High level overview

<p align="center"><img src="/assets/img/cilium-code-cni/client-scaleup-cnp.png" width="90%" height="90%"></p>
<p align="center">Fig 0. Left most: what happens during Cilium CNI create-network (this picture
is borrowed from my potential posts in the future, so there are some
stuffs not much related to this post, just ignore them) </p>

As an high-level overview, Cilium CNI plugin performs following steps:

1. Create link device (e.g. veth pair, IPVLAN device)
2. Allocate IP
3. Configure Pod network, e.g. IP address, route table, sysctl parameters
4. **Create `Endpoint`** (node local) via Cilium agent API
5. **Create `CiliumEndpoint` (CEP, k8s CRD)** via k8s apiserver
6. Retrieve or **allocate identity** for this endpoint via kvstore
7. Calculate **network policy**
8. **Save IP info (e.g. `IP -> identity` mapping) to kvstore**
9. Generate, compile and inject BPF code into kernel

## 0.1 Source code tree

1. `api/` - cilium REST API entrypoints
1. `daemon/cmd/` - cilium daemon (agent) implementation, including:

    1. IPAM API handler implementation
    1. endpoint API handler
    1. others

1. `plugin/` - plugin implementations for CNI, docker, etc
1. `pkg/` - cilium core functionalities implementation

## 0.2 Add Network Skeleton Code

When kubelet calls a plugin to add network for a Pod, `cmdAdd()`
method will be invoked. The skeleton code is as follows:

```go
// plugins/cilium-cni/cilium-cni.go

func cmdAdd(args *skel.CmdArgs) (err error) {
    n = types.LoadNetConf(args.StdinData)
    cniTypes.LoadArgs(args.Args, &cniArgs)
    netns.RemoveIfFromNetNSIfExists(netNs, args.IfName)

    ep := &models.EndpointChangeRequest{
        ContainerID:  args.ContainerID,
        Labels:       addLabels,
        State:        models.EndpointStateWaitingForIdentity,
        K8sPodName:   string(cniArgs.K8S_POD_NAME),
        K8sNamespace: string(cniArgs.K8S_POD_NAMESPACE),
    }

    switch conf.DatapathMode {
    case DatapathModeVeth:
        veth, peer, tmpIfName = connector.SetupVeth(ep.ContainerID, DeviceMTU, ep)
        netlink.LinkSetNsFd(*peer, int(netNs.Fd()))
        connector.SetupVethRemoteNs(netNs, tmpIfName, args.IfName)
    case DatapathModeIpvlan: ...
    }

    podName := cniArgs.K8S_POD_NAMESPACE + "/" + cniArgs.K8S_POD_NAME
    ipam = c.IPAMAllocate("", podName, true) // c: cilium client

    if ipv4IsEnabled(ipam) {
        ep.Addressing.IPV4 = ipam.Address.IPV4
        ipConfig, routes = prepareIP(ep.Addressing.IPV4, ...)
        res.IPs = append(res.IPs, ipConfig)
        res.Routes = append(res.Routes, routes...)
    }

    macAddrStr = configureIface(ipam, args.IfName, &state)

    c.EndpointCreate(ep)
    return cniTypes.PrintResult(res, n.CNIVersion)
}
```

Let's walk through this process in detail.

# 1 Create link device

## 1.1 Parse CNI parameters

On requesting for adding network for a Pod, kubelet will pass a
`CmdArgs` type variable to CNI plugin, which is defined in
`github.com/containernetworking/cni/pkg/skel`:

```go
// CmdArgs captures all the arguments passed in to the plugin via both env vars and stdin
type CmdArgs struct {
        ContainerID string // container ID
        Netns       string // container netns
        IfName      string // desired interface name for container, e.g. `eth0`
        Args        string // Platform-specific parameters of the container, e.g.
                           // `K8S_POD_NAMESPACE=xx;K8S_POD_NAME=xx;K8S_POD_INFRA_CONTAINER_ID=xx`
        Path        string // Path for locating CNI plugin binary, e.g. `/opt/cni/bin`
        StdinData   []byte // Network configurations
}
```

The `StdinData` field will be unmarshalled into a `netConf` variable:

```go
type netConf struct {
    cniTypes.NetConf
    MTU  int  `json:"mtu"`
    Args Args `json:"args"`
}
```

where `cniTypes.NetConf` is defined as,

```go
// github.com/containernetworking/cni/pkg/types/types.go

// NetConf describes a network.
type NetConf struct {
    CNIVersion string `json:"cniVersion,omitempty"`

    Name         string          `json:"name,omitempty"`
    Type         string          `json:"type,omitempty"`
    Capabilities map[string]bool `json:"capabilities,omitempty"`
    IPAM         IPAM            `json:"ipam,omitempty"`
    DNS          DNS             `json:"dns"`

    RawPrevResult map[string]interface{} `json:"prevResult,omitempty"`
    PrevResult    Result                 `json:"-"`
}
```

After parsing these network configurations, the plugin will create a
network device (link device) for this Pod.

Cilium supports two kinds of datapath types (and thus link device types) at the
time of writing this post:

1. veth pair
1. IPVLAN

We will take veth pair as example in the following.

## 1.2 Create veth pair

The virtual devices that used for connecting a container to the host in Cilium
are called a "connector".

CNI plugin first calls `connector.SetupVeth()` to create a veth pair, taking
container ID, MTU and endpoint info as parameters.

```go
    veth, peer, tmpIfName := connector.SetupVeth(ep.ContainerID, DeviceMTU, ep)
```

Naming rule for the veth pair devies:

* **host side**: `lxc` + first N digits of `sha256(containerID)`, e.g. `lxc12c45`
* **peer side** (container side): `tmp` + first N digits of `sha256(containerID)`, e.g. `tmp12c45`

Additional steps in `connector` (`pkg/endpoint/connector/veth.go`):

1. Set sysctl parameters: `/proc/sys/net/ipv4/conf/<veth>/rp_filter = 0`
1. Set MTU
1. Fill endpoint info: MAC (container side), host side MAC, interface name, interface index

## 1.3 Move peer to container netns

In the next, CNI plugin puts the peer end of the veth pair to container by
setting the peer's network namespace as container's netns:

```go
    netlink.LinkSetNsFd(*peer, int(netNs.Fd()))
```

As a consequence, the peer device will "disapper" from host, namely, it will not
be listed when executing `ifconfig` or `ip link` commands on the host.
You must specify the netns in order to see it: `ip netns exec <netns> ip link`.

## 1.4 Rename peer

Next, CNI plugin renames the peer to the given name as specified in CNI arguments:

```go
    connector.SetupVethRemoteNs(netNs, tmpIfName, args.IfName)
```

This will, for example, **rename `tmp53057` to `eth0`** inside container.
And this is just **how the `eth0` device comes up in each container**.

# 2 Allocate IP address

In the next, the plugin will try to allocate IP addresses (IPv4 & IPv6) from the
IPAM, and the latter is embedded in the local cilium agent.

Cilium agent is a daemon process running on each host, it includes many services
inside itself, for example, **local IPAM**, **endpoint manager**, etc.
These services expose REST APIs.

The IP allocation process is far more complicated than it looks like. The code
is just one line:

```go
    ipam := c.IPAMAllocate("")
```

But the call stack will jump among different places:

1. Plugin - `plugin/cilium-cni/`
1. Cilium client - `pkg/client/`
1. Clium REST API - `api/v1/server/restapi/ipam/`
1. Cilium API server - `api/v1/server/restapi/ipam`
1. Real HTTP handler - `daemon/cmd/ipam.go`
1. Cilium IPAM implementation (actually is only a wrapper) - `pkg/ipam/`
1. Final IPAM implementation (k8s builtin) - `k8s.io/kubernetes/pkg/registry/core/service/ipallocator`

Let's walk them through step by step.

## 2.1 Allocate IP address for given Pod

Start from `plugin/cilium-cni/cilium-cni.go`:

```go
    podName := cniArgs.K8S_POD_NAMESPACE + "/" + cniArgs.K8S_POD_NAME
    ipam := c.IPAMAllocate("", podName) // c: cilium client.
```

## 2.2 Cilium client: `IPAMAllocate()`

`IPAMAllocate()` takes two parameters: address `family` and `owner`; if address
family is empty, both an IPv4 and an IPv6 address will be allocated. Each of
them will be saved at the following fields:

* `ipam.Address.IPV4`
* `ipam.Address.IPV6`

The implementation:

```go
// pkt/client/ipam.go

// IPAMAllocate allocates an IP address out of address family specific pool.
func (c *Client) IPAMAllocate(family, owner string) (*models.IPAMResponse, error) {
    params := ipam.NewPostIPAMParams().WithTimeout(api.ClientTimeout)

    if family != ""
        params.SetFamily(&family)
    if owner != ""
        params.SetOwner(&owner)

    resp, err := c.IPAM.PostIPAM(params)
    return resp.Payload, nil
}
```

where, the client structure is defined in `pkt/client/client.go`:

```go
type Client struct {
    clientapi.Cilium
}
```

the client API `clientapi.Cilium` is further defined in 
`api/v1/client/cilium_client.go`:

```go
// clientapi
type Cilium struct {
    Daemon *daemon.Client

    Endpoint *endpoint.Client
    IPAM *ipam.Client              // implemented in "api/v1/client/ipam"
    Metrics *metrics.Client
    Policy *policy.Client
    Prefilter *prefilter.Client
    Service *service.Client
    Transport runtime.ClientTransport
}
```

OK, now we need to move to `c.IPAM.PostIPAM(params)`.

## 2.3 Call REST API: allocate IP

The cilium API code is auto-generated with golang OpenAPI tools.

`api/v1/client/ipam/i_p_a_m_client.go`:

```go
func (a *Client) PostIPAM(params *PostIPAMParams) (*PostIPAMCreated, error) {
    result := a.transport.Submit(&runtime.ClientOperation{
        ID:                 "PostIPAM",
        Method:             "POST",
        PathPattern:        "/ipam",
        Params:             params,
    })
    return result.(*PostIPAMCreated), nil
}
```

This will `POST` the request to `/ipam` route on the server side.

## 2.4 IPAM API server

HTTP request receiving side, `api/v1/server/restapi/ipam/post_ip_a_m.go`:

```go
func (o *PostIPAM) ServeHTTP(rw http.ResponseWriter, r *http.Request) {
    route, rCtx, _ := o.Context.RouteInfo(r)
    var Params = NewPostIPAMParams()

    if err := o.Context.BindValidRequest(r, route, &Params); err != nil { // bind params
        o.Context.Respond(rw, r, route.Produces, route, err)
        return
    }

    res := o.Handler.Handle(Params) // actually handle the request
    o.Context.Respond(rw, r, route.Produces, route, res)
}
```

The real processing is done in `o.Handler.Handle()`
method. This method is implemented in the daemon code.

## 2.5 IPAM HTTP handler

We will just see the IPv4 part in the below.

```go
// daemon/cmd/ipam.go

// Handle incoming requests address allocation requests for the daemon.
func (h *postIPAM) Handle(params ipamapi.PostIPAMParams) middleware.Responder {
    resp := &models.IPAMResponse{
        HostAddressing: node.GetNodeAddressing(),
        Address:        &models.AddressPair{},
    }

    ipv4, ipv6 := h.daemon.ipam.AllocateNext(params.Family)

    if ipv4 != nil
        resp.Address.IPV4 = ipv4.String()

    return ipamapi.NewPostIPAMCreated().WithPayload(resp)
}
```

where `h.daemon.ipam` is actually a **CIDR**, initialized in
`pkg/ipam/ipam.go`:

```go
import "k8s.io/kubernetes/pkg/registry/core/service/ipallocator"

// NewIPAM returns a new IP address manager
func NewIPAM(nodeAddressing datapath.NodeAddressing, c Configuration) *IPAM {
    ipam := &IPAM{
        nodeAddressing: nodeAddressing,
        config:         c,
    }

    if c.EnableIPv4
        ipam.IPv4Allocator = ipallocator.NewCIDRRange(nodeAddressing.IPv4().AllocationCIDR().IPNet)

    return ipam
}
```

As we can see, it allocates IP addresses from the specified CIDR.

**One important thing needs to be noted here**: this is an **in-memory IPAM** -
namyly, all its states (e.g.  allocated IPs, avaialbe IPs, reserved IPs) are
stored in memory, thus will not survive service restart.

> If IPAM states are stored in memory, then **how does Cilium agent restore
> states on restart**?
>
> The answer is: cilium agent records each allocated IP addresses in local
> files, to be more specific, each endpoint's BPF header file. We will see this later.

Next, let's move on, go to `h.daemon.ipam.AllocateNext(params.Family)`.

## 2.6 IPAM implementation in `pkg/ipam`

```go
// pkg/ipam/allocator.go

// AllocateNext allocates the next available IPv4 and IPv6 address out of the configured address pool.
func (ipam *IPAM) AllocateNext(family string) (ipv4 net.IP, ipv6 net.IP, err error) {
    ipv4 = ipam.AllocateNextFamily(IPv4)
}

// AllocateNextFamily allocates the next IP of the requested address family
func (ipam *IPAM) AllocateNextFamily(family Family) (ip net.IP, err error) {
    switch family {
    case IPv4:
        ip = allocateNextFamily(family, ipam.IPv4Allocator)
    default:
        fmt.Errorf("unknown address \"%s\" family requested", family)
    }
}

func allocateNextFamily(family Family, allocator *ipallocator.Range) (ip net.IP, err error) {
    ip = allocator.AllocateNext()
}
```

After several layers of function calls, it eventually comes to
`allocator.AllocateNext()`, where `AllocateNext()` is an interface defined in
`k8s.io/kubernetes/pkg/registry/core/service/ipallocator`.

## 2.7 Real allocation logic in K8s builtin IPAM

Now, IP allocation comes to K8S code. Reserve IP from pool,

```go
// k8s.io/kubernetes/pkg/registry/core/service/ipallocator

func (r *Range) AllocateNext() (net.IP, error) {
    offset, ok := r.alloc.AllocateNext()
    return addIPOffset(r.base, offset), nil
}
```

where `r.alloc` is an interface:

```go
// Interface manages the allocation of IP addresses out of a range. Interface should be threadsafe.
type Interface interface {
    Allocate(net.IP) error
    AllocateNext() (net.IP, error)
    Release(net.IP) error
    ForEach(func(net.IP))
}
```

`r.alloc.AllocateNext()` returns the offset of next available IP from the first
IP (`r.base`) in this CIDR, and `addIPOffset()` converts the offset to a
`net.IP` format presentation.

moving on, let's see the allocation details,

```go
// k8s.io/kubernetes/pkg/registry/core/service/allocator/bimap.go

// AllocateNext reserves one of the items from the pool.
// (0, false, nil) may be returned if there are no items left.
func (r *AllocationBitmap) AllocateNext() (int, bool, error) {
    next, ok := r.strategy.AllocateBit(r.allocated, r.max, r.count)
    r.count++
    r.allocated = r.allocated.SetBit(r.allocated, next, 1)
    return next, true, nil
}

func (rss randomScanStrategy) AllocateBit(allocated *big.Int, max, count int) (int, bool) {
    if count >= max {
        return 0, false
    }
    offset := rss.rand.Intn(max)
    for i := 0; i < max; i++ {
        at := (offset + i) % max
        if allocated.Bit(at) == 0 {
            return at, true
        }
    }
    return 0, false
}

func (contiguousScanStrategy) AllocateBit(allocated *big.Int, max, count int) (int, bool) {
    if count >= max {
        return 0, false
    }
    for i := 0; i < max; i++ {
        if allocated.Bit(i) == 0 {
            return i, true
        }
    }
    return 0, false
}
```

As seen above, the IPAM is really tiny. It maintains a bitmap for IP allocation,
when an IP is allocated out from the pool, the corresponding bit will be set to
1; when IP is returned to the pool, the bit is set to 0. In this way, it could
effectively manage the IP pool.

The IPAM even supports different allocation strategies, as the code snippets
shown, e.g. sequential or random.

# 3 Configure pod network

Now, IP addresses have been ready. Next steps are:

* Calulate routes, gateway
* Configure ip, routes, gateway, sysctl, etc

```go
    if ipv4IsEnabled(ipam) {
        ep.Addressing.IPV4 = ipam.Address.IPV4

        ipConfig, routes := prepareIP(ep.Addressing.IPV4, false, &state, int(conf.RouteMTU))
        res.IPs = append(res.IPs, ipConfig)
        res.Routes = append(res.Routes, routes...)
    }

    netNs.Do(func(_ ns.NetNS) error {
        allInterfacesPath := filepath.Join("/proc", "sys", "net", "ipv6", "conf", "all", "disable_ipv6")
        connector.WriteSysConfig(allInterfacesPath, "0\n")
        macAddrStr = configureIface(ipam, args.IfName, &state)
        return err
    })
```

## 3.1 Prepare IP addresses, routes, gateway

First, call `prepareIP()` function to prepare the IP Addresses, gateways, route
entries, etc. The function will return a pointer to an `IPConfig`, which holds
the IP and gateway information, and a `Route` entry list.

```go
// plugins/cilium-cni/cilium-cni.go

func prepareIP(ipAddr string, isIPv6 bool, state *CmdState, mtu int) (*cniTypesVer.IPConfig, []*cniTypes.Route) {
    if isIPv6 {
        ...
    } else {
        state.IP4 = addressing.NewCiliumIPv4(ipAddr)
        state.IP4routes = connector.IPv4Routes(state.HostAddr, mtu)
        routes = state.IP4routes
        ip = state.IP4
        gw = connector.IPv4Gateway(state.HostAddr)
        ipVersion = "4"
    }

    rt := []*cniTypes.Route{}
    for _, r := range routes {
        rt = append(rt, newCNIRoute(r))
    }

    gwIP := net.ParseIP(gw)

    return &cniTypesVer.IPConfig{
        Address: *ip.EndpointPrefix(),
        Gateway: gwIP,
        Version: ipVersion,
    }, rt, nil
}
```

Take IPv4 as exaple. Following functions are called:

1. `NewCiliumIPv4`
1. `IPv4Routes`
1. `IPv4Gateway`

### CiliumIP

`NewCiliumIPv4()` will return a `CiliumIP` instance, `common/addressing/ip.go`:

```go
type CiliumIP interface {
    IPNet(ones int) *net.IPNet
    EndpointPrefix() *net.IPNet
    IP() net.IP
    String() string
    IsIPv6() bool
    GetFamilyString() string
    IsSet() bool
}
```

### Routes

`IPv4Routes` returns IPv4 routes to be installed in endpoint's networking
namespace.

`pkg/endpoint/connector/ipam.go`:

```go
func IPv4Routes(addr *models.NodeAddressing, linkMTU int) ([]route.Route, error) {
    ip := net.ParseIP(addr.IPV4.IP)
    return []route.Route{
        {
            Prefix: net.IPNet{
                IP:   ip,
                Mask: defaults.ContainerIPv4Mask,
            },
        },
        {
            Prefix:  defaults.IPv4DefaultRoute,
            Nexthop: &ip,
            MTU:     linkMTU,
        },
    }, nil
}
```

### Gateway

note that, the gateway is just set to the host's IP (TODO: more info on this):

```go
// IPv4Gateway returns the IPv4 gateway address for endpoints.
func IPv4Gateway(addr *models.NodeAddressing) string {
    // The host's IP is the gateway address
    return addr.IPV4.IP
}
```

## 3.2 Configure interface

After network information has been prepared well, next step is to
configure them to the container. This is achieved by calling `configureIface`:

```go
func configureIface(ipam *models.IPAMResponse, ifName string, state *CmdState) (string, error) {
    l := netlink.LinkByName(ifName)

    addIPConfigToLink(state.IP4, state.IP4routes, l, ifName)
    netlink.LinkSetUp(l)
}
```

`configureIface` first finds the link device by device name (`eth0` inside
container), then calls `addIPConfigToLink` to do the real jobs:

```go
func addIPConfigToLink(...) error {
    addr := &netlink.Addr{IPNet: ip.EndpointPrefix()}
    netlink.AddrAdd(link, addr)

    sort.Sort(route.ByMask(routes))
    for _, r := range routes {
        rt := &netlink.Route{
            LinkIndex: link.Attrs().Index,
            Scope:     netlink.SCOPE_UNIVERSE,
            Dst:       &r.Prefix,
            MTU:       r.MTU,
        }

        if r.Nexthop == nil {
            rt.Scope = netlink.SCOPE_LINK
        } else {
            rt.Gw = *r.Nexthop
        }
        netlink.RouteAdd(rt)
    }
}
```

It:

1. first call `netlink.AddrAdd` to add IP address to the device
1. then install the route entries with `netlink.RouteAdd`

# 4 Create endpoint

What is an Endpoint?  An endpoint is a "**namespaced network interface** to which
cilium applies policies". In its simplist way, each normal Pod corresponds to a
Cilium Endpoint.

> Endpoint is **a node-local concept**, that is, Endpoint IDs on each node are
> overlapping.
>
> Endpoint information actually also stores in local files (again, the BPF
> header files), so cilium agent could restore them on restart.

```go
// api/v1/models/endpoint.go

type Endpoint struct {
    // The cilium-agent-local ID of the endpoint
    ID int64 `json:"id,omitempty"`

    // The desired configuration state of the endpoint
    Spec *EndpointConfigurationSpec `json:"spec,omitempty"`

    // The desired and realized configuration state of the endpoint
    Status *EndpointStatus `json:"status,omitempty"`
}
```

For more detailed information refer to `api/v1/models/endpoint*.go`, there are
couples of source files.

## 4.1 CNI: create Endpoint

Start from CNI code, `plugins/cilium-cni/cilium-cni.go`:

```go
    ep.SyncBuildEndpoint = true
    c.EndpointCreate(ep)
```

It calls the client's `EndpointCreate()` method.

## 4.2 Cilium client: create Endpoint

```go
// pkg/client/endpoint.go

func (c *Client) EndpointCreate(ep *models.EndpointChangeRequest) error {
    id := pkgEndpointID.NewCiliumID(ep.ID)
    params := endpoint.NewPutEndpointIDParams().WithID(id).WithEndpoint(ep).WithTimeout(api.ClientTimeout)
    c.Endpoint.PutEndpointID(params)
    return Hint(err)
}
```

It first call `NewCiliumID()` to generate the local endpoint identifier,

```go
// pkg/endpoint/id/id.go

const (
    // CiliumLocalIdPrefix is a numeric identifier with local scope. It has
    // no cluster wide meaning and is only unique in the scope of a single
    // agent. An endpoint is guaranteed to always have a local scope identifier.
    CiliumLocalIdPrefix PrefixType = "cilium-local"
)

// NewCiliumID returns a new endpoint identifier of type CiliumLocalIdPrefix
func NewCiliumID(id int64) string {
    return fmt.Sprintf("%s:%d", CiliumLocalIdPrefix, id)
}
```

as the comments say, this ID is host-local, which means it is unique within
the host.

Then, the client code arranges request data, and `PUT` that data to Cilium REST
API with `PutEndpointID(params)`,

```go
// api/client/endpoint/endpoint_client.go

func (a *Client) PutEndpointID(params *PutEndpointIDParams) (*PutEndpointIDCreated, error) {
    result := a.transport.Submit(&runtime.ClientOperation{
        ID:                 "PutEndpointID",
        Method:             "PUT",
        PathPattern:        "/endpoint/{id}",
        Params:             params,
    })
    return result.(*PutEndpointIDCreated), nil
}
```

## 4.3 Cilium HTTP server: create Endpoint

The API server is embedded in the daemon process.
HTTP requests entrypoint,

```go
// api/server/restapi/endpint/put_endpoint_id.go

type PutEndpointID struct {
    Context *middleware.Context
    Handler PutEndpointIDHandler
}

func (o *PutEndpointID) ServeHTTP(rw http.ResponseWriter, r *http.Request) {
    route, rCtx, _ := o.Context.RouteInfo(r)
    var Params = NewPutEndpointIDParams()

    if err := o.Context.BindValidRequest(r, route, &Params); err != nil { // bind params
        o.Context.Respond(rw, r, route.Produces, route, err)
        return
    }

    res := o.Handler.Handle(Params) // actually handle the request
    o.Context.Respond(rw, r, route.Produces, route, res)
}
```

The real job will be done in `o.Handler.Handle()`.

## 4.4 HTTP Handler: create endpoint

The HTTP handler for `PutEndpointID`, `daemon/endpoint.go`:

```go
type putEndpointID struct {
    d *Daemon
}

func NewPutEndpointIDHandler(d *Daemon) PutEndpointIDHandler {
    return &putEndpointID{d: d}
}

func (h *putEndpointID) Handle(params PutEndpointIDParams) middleware.Responder {
    epTemplate := params.Endpoint

    h.d.createEndpoint(params.HTTPRequest.Context(), epTemplate)
}
```

as we could see, the handler calls daemon's `createEndpoint` method, which is
defined in the same file. This method attempts to create the endpoint
corresponding to the change request that was specified.

```go
func (d *Daemon) createEndpoint(ctx, epTemplate) (*endpoint.Endpoint, int) {
    ep, err := endpoint.NewEndpointFromChangeModel(epTemplate)

    if oldEp := endpointmanager.LookupCiliumID(ep.ID); oldEp != nil
        return fmt.Errorf("endpoint ID %d already exists", ep.ID)
    if oldEp = endpointmanager.LookupContainerID(ep.ContainerID); oldEp != nil
        return fmt.Errorf("endpoint for container %s already exists", ep.ContainerID)

    addLabels := labels.NewLabelsFromModel(epTemplate.Labels)
    infoLabels := []string{}
    identityLabels, info := fetchK8sLabels(ep)
    addLabels.MergeLabels(identityLabels)
    infoLabels.MergeLabels(info)

    endpointmanager.AddEndpoint(d, ep, "Create endpoint from API PUT")

    ep.UpdateLabels(ctx, d, addLabels, infoLabels, true)

    // Now that we have ep.ID we can pin the map from this point. This
    // also has to happen before the first build took place.
    ep.PinDatapathMap()

    if build {
        ep.Regenerate(d, &endpoint.ExternalRegenerationMetadata{
            Reason:        "Initial build on endpoint creation",
            ParentContext: ctx,
        })
    }
}
```

`endpointmanager.AddEndpoint()` will further call `ep.Expose()` to notifier, and the latter
will start a controller for this endpoint, for synchronizing this
`Endpoint`'s info to apiserver as corresponding `CiliumEndpoint` (CEP). We will
see this in **section 5**.

It then calls `ep.UpdateLabels()`, this may:

1. try to get the identity of this endpint: e.g. when scaling up existing
   statefulsets, the identity already exists before the Pod creating.
2. allocate identity for this endpoint: e.g. when create a new statefulset.

We will see this in **section 6**.

In the last, it **triggers BPF code re-generation**, by calling
`ep.Regenerate()` with reason "Initial build on endpoint creation".
On successful, the revision number will be positive. We will see this in
**section 9**.

# 5 Create CiliumEndpoint (CEP)

```go
// pkg/endpoint/manager.go

// Expose exposes the endpoint to the endpointmanager. After this function
// is called, the endpoint may be accessed by any lookup in the endpointmanager.
func (e *Endpoint) Expose(mgr endpointManager) error {
    newID := mgr.AllocateID(e.ID)
    e.ID = newID

    e.startRegenerationFailureHandler()
    // Now that the endpoint has its ID, it can be created with a name based on
    // its ID, and its eventqueue can be safely started. Ensure that it is only
    // started once it is exposed to the endpointmanager so that it will be
    // stopped when the endpoint is removed from the endpointmanager.
    e.eventQueue = eventqueue.NewEventQueueBuffered(fmt.Sprintf("endpoint-%d", e.ID))
    e.eventQueue.Run()

    // No need to check liveness as an endpoint can only be deleted via the
    // API after it has been inserted into the manager.
    mgr.UpdateIDReference(e)
    e.updateReferences(mgr)

    mgr.RunK8sCiliumEndpointSync(e, option.Config)
    return nil
}
```

The comments illustrates themselves:

```go
// ipkg/k8s/watchers/endpointsynchronizer.go

// RunK8sCiliumEndpointSync starts a controller that synchronizes the endpoint
// to the corresponding k8s CiliumEndpoint CRD. It is expected that each CEP
// has 1 controller that updates it, and a local copy is retained and only
// updates are pushed up.
func (epSync *EndpointSynchronizer) RunK8sCiliumEndpointSync(e *endpoint.Endpoint, conf) {
    var (
        endpointID     = e.ID
        controllerName = fmt.Sprintf("sync-to-k8s-ciliumendpoint (%v)", endpointID)
    )

    ciliumClient := k8s.CiliumClient().CiliumV2()

    e.UpdateController(controllerName,
        controller.ControllerParams{
            RunInterval: 10 * time.Second,
            DoFunc: func(ctx context.Context) (err error) {
                podName := e.GetK8sPodName()
                namespace := e.GetK8sNamespace()

                switch {
                default:
                    scopedLog.Debug("Updating CEP from local copy")
                    switch {
                    case capabilities.UpdateStatus:
                        localCEP = ciliumClient.CiliumEndpoints(namespace).UpdateStatus()
                    default:
                        localCEP = ciliumClient.CiliumEndpoints(namespace).Update()
                    }
                }
            },
        })
}
```

# 6 Retrieve or allocate identity

"Identity" is a cluster-scope concept (as comparison, "Endpoint" is a node-scope
concept), that means, it is unique within the entire Kubernetes cluster.

So to ensure identities are unique within the entire cluster, they are allocated
by a central component in the cluster - yes, the kvstore (cilium-etcd).

Starts from `ep.UpdateLabels()`:

```go
// pkg/endpoint/endpoint.go

// UpdateLabels is called to update the labels of an endpoint. Calls to this
// function do not necessarily mean that the labels actually changed. The
// container runtime layer will periodically synchronize labels.
//
// If a net label changed was performed, the endpoint will receive a new
// security identity and will be regenerated. Both of these operations will
// run first synchronously if 'blocking' is true, and then in the background.
//
// Returns 'true' if endpoint regeneration was triggered.
func (e *Endpoint) UpdateLabels(ctx, identityLabels, infoLabels, blocking bool) (regenTriggered bool) {
	e.replaceInformationLabels(infoLabels)

	// replace identity labels and update the identity if labels have changed
	rev := e.replaceIdentityLabels(identityLabels)
	if rev != 0 {
		return e.runIdentityResolver(ctx, rev, blocking)
	}

	return false
}
```

and the subsequent calling stack:

```
         |-ep.UpdateLabels                      // pkg/endpoint/endpoint.go
         |  |-replaceInformationLabels          // pkg/endpoint/endpoint.go
         |  |-ReplaceIdentityLabels             // pkg/endpoint/endpoint.go
         |     |-RunIdentityResolver            // pkg/endpoint/endpoint.go
         |        |-identityLabelsChanged       // pkg/endpoint/endpoint.go
         |           |-AllocateIdentity         // kvstore: reuse existing or create new one
         |           |-forcePolicyComputation
         |           |-SetIdentity
         |              |-runIPIdentitySync     // pkg/endpoint/policy.go
         |                 |-UpsertIPToKVStore  // pkg/ipcache/kvstore.go
```

After identity is determined for this endpoint, cilium agent will do two
important things:

First, re-calculate network policy, as identity is the eventual security ID.
We will see this in section 7.

Second, **insert the `IP -> identity` mapping into kvstore** by calling
`UpsertIPToKVStore()`.  **This is vital for the cilium network policy
framework**. We will see this in section 8.

# 7 Calculate policy

After identity is determined, `forcePolicyComputation()` will be called to
calculate network policy for this endpoint, e.g. which service could access to
which port of this Endpoint.

# 8 Upsert IP information to kvstore

Re-depict Fig 0 here so you could understand why this step is vital to the
entire cilium network policy framework:

<p align="center"><img src="/assets/img/cilium-code-cni/client-scaleup-cnp.png" width="90%" height="90%"></p>
<p align="center">Fig 0. Left most: what happens during Cilium CNI create-network (this picture
is borrowed from my potential posts in the future, so there are some
stuffs not much related to this post, just ignore them) </p>

As an example, when a packet sent out from this Endpoint (Pod) reaches a Pod on
another node, they will determine whether to allow this traffic by the packet's
identity. How does cilium determine identity for this packet? For direct routing
case, it will

1. Listen to `IP->Identity` mappings in kvstore (`cilium/state/ip/v1`), save to
   a local cache (`ipcache`).
1. Extract `src_ip` from packet, lookup `identity` info in local cache with
   `src_ip` as hash key.

# 9 Re-generate BPF code

Typical workflow [3]:

1. Generate eBPF source code (in a subset of C)
2. Compile to ELF file with LLVM, which contains program code, specification for maps and related relocation data
3. Parse ELF content and load the program into the kernel with tools like `tc` (traffic control)

> In eBPF, maps are efficient key/value stores in the kernel that can be shared
> between various eBPF programs, but also between user space.

## 9.1 Generate BPF

Now let's continue the BPF code regeneration path.

```go
// pkg/endpoint/policy.go

// Regenerate forces the regeneration of endpoint programs & policy
func (e *Endpoint) Regenerate(owner Owner, regenMetadata *ExternalRegenerationMetadata) <-chan bool {
    done := make(chan bool, 1)
    go func() {
        defer func() {
            done <- buildSuccess
            close(done)
        }()

        doneFunc := owner.QueueEndpointBuild(uint64(e.ID))
        if doneFunc != nil {
            regenContext.DoneFunc = doneFunc
            err := e.regenerate(owner, regenContext)
            doneFunc() // in case not called already

            // notify monitor about endpoint regeneration result
        } else {
            buildSuccess = false
        }
    }()

    return done
}
```

It calls enpoint's `regenerate` method, which is defined in the same file:

```go
func (e *Endpoint) regenerate(owner Owner, context *regenerationContext) (retErr error) {
    origDir := e.StateDirectoryPath()
    context.datapathRegenerationContext.currentDir = origDir

    // This is the temporary directory to store the generated headers,
    // the original existing directory is not overwritten until the
    // entire generation process has succeeded.
    tmpDir := e.NextDirectoryPath()
    context.datapathRegenerationContext.nextDir = tmpDir
    os.MkdirAll(tmpDir, 0777)

    revision, compilationExecuted = e.regenerateBPF(owner, context)

    return e.updateRealizedState(stats, origDir, revision, compilationExecuted)
}
```

As BPF code regeneration is a series of file based operations, `regenerate` will
first prepare working directories for the process, then, call endpoint's
`regenerateBPF` method.

`regenerateBPF` rewrites all headers and updates all BPF maps to reflect the
specified endpoint.

`pkg/endpoint/bpf.go`:

```go
func (e *Endpoint) regenerateBPF(owner Owner, regenContext *regenerationContext) (revnum uint64, compiled bool, reterr error) {
    e.runPreCompilationSteps(owner, regenContext)

    if option.Config.DryMode { // No need to compile BPF in dry mode.
        return e.nextPolicyRevision, false, nil
    }

    // Wait for connection tracking cleaning to complete
    <-datapathRegenCtxt.ctCleaned

    compilationExecuted = e.realizeBPFState(regenContext)

    // Hook the endpoint into the endpoint and endpoint to policy tables then expose it
    eppolicymap.WriteEndpoint(datapathRegenCtxt.epInfoCache.keys, e.PolicyMap.Fd)
    lxcmap.WriteEndpoint(datapathRegenCtxt.epInfoCache)

    // Signal that BPF program has been generated.
    // The endpoint has at least L3/L4 connectivity at this point.
    e.CloseBPFProgramChannel()

    // Allow another builder to start while we wait for the proxy
    if regenContext.DoneFunc != nil {
        regenContext.DoneFunc()
    }

    e.ctCleaned = true

    // Synchronously try to update PolicyMap for this endpoint.
    //
    // This must be done after allocating the new redirects, to update the
    // policy map with the new proxy ports.
    e.syncPolicyMap()

    return datapathRegenCtxt.epInfoCache.revision, compilationExecuted, err
}
```

BPF source code (in restricted C) is generated in `e.runPreCompilationSteps`,
and write to file through `writeHeaderfile` in the end of the function:

```go
// runPreCompilationSteps runs all of the regeneration steps that are necessary
// right before compiling the BPF for the given endpoint.
func (e *Endpoint) runPreCompilationSteps(owner Owner, regenContext *regenerationContext) (error) {
    currentDir := datapathRegenCtxt.currentDir
    nextDir := datapathRegenCtxt.nextDir

    if e.PolicyMap == nil {
        e.PolicyMap = policymap.OpenMap(e.PolicyMapPathLocked())
        e.PolicyMap.Flush() // Clean up map contents

        // Also reset the in-memory state of the realized state as the
        // BPF map content is guaranteed to be empty right now.
        e.realizedPolicy.PolicyMapState = make(policy.MapState)
    }

    if e.bpfConfigMap == nil {
        e.bpfConfigMap = bpfconfig.OpenMapWithName(e.BPFConfigMapPath(), e.BPFConfigMapName())
        e.realizedBPFConfig = &bpfconfig.EndpointConfig{}
    }

    // Only generate & populate policy map if a security identity is set up for
    // this endpoint.
    if e.SecurityIdentity != nil {
        err = e.regeneratePolicy(owner)

        // Configure the new network policy with the proxies.
        e.updateNetworkPolicy(owner, datapathRegenCtxt.proxyWaitGroup)
    }

    // Generate header file specific to this endpoint for use in compiling
    // BPF programs for this endpoint.
    e.writeHeaderfile(nextDir, owner)
}
```

Continue the calling stack to `e.realizeBPFState`:

```go
func (e *Endpoint) realizeBPFState(regenContext *regenerationContext) (compilationExecuted bool, err error) {
    if datapathRegenCtxt.bpfHeaderfilesChanged || datapathRegenCtxt.reloadDatapath {
        // Compile and install BPF programs for this endpoint
        if datapathRegenCtxt.bpfHeaderfilesChanged {
            loader.CompileAndLoad(datapathRegenCtxt.completionCtx, datapathRegenCtxt.epInfoCache)
            compilationExecuted = true
        } else {
            loader.ReloadDatapath(datapathRegenCtxt.completionCtx, datapathRegenCtxt.epInfoCache)
        }

        e.bpfHeaderfileHash = datapathRegenCtxt.bpfHeaderfilesHash
    } else {
        Debug("BPF header file unchanged, skipping BPF compilation and installation")
    }

    return compilationExecuted, nil
}
```

`CompileAndLoad` compiles and reloads the datapath programs (BPF code).

`ReloadDatapath` forces the datapath programs to be reloaded. It does
not guarantee recompilation of the programs.

## 9.2 Compile and link

Moving on, `CompileAndLoad` function.

`CompileAndLoad` compiles the BPF datapath programs for the specified endpoint
and loads it onto the interface associated with the endpoint.

`pkt/datapath/loader/loader.go`:

```go
// Expects the caller to have created the directory at the path ep.StateDir().
func CompileAndLoad(ctx context.Context, ep endpoint) error {
    dirs := directoryInfo{
        Library: option.Config.BpfDir,
        Runtime: option.Config.StateDir,
        State:   ep.StateDir(),
        Output:  ep.StateDir(),
    }
    return compileAndLoad(ctx, ep, &dirs)
}

func compileAndLoad(ctx context.Context, ep endpoint, dirs *directoryInfo) error {
    compileDatapath(ctx, ep, dirs, debug)
    return reloadDatapath(ctx, ep, dirs)
}

// compileDatapath invokes the compiler and linker to create all state files for
// the BPF datapath, with the primary target being the BPF ELF binary.
//
// If debug is enabled, create also the following output files:
// * Preprocessed C
// * Assembly
// * Object compiled with debug symbols
func compileDatapath(ctx context.Context, ep endpoint, dirs *directoryInfo, debug bool) error {
    // Compile the new program
    compile(ctx, datapathProg, dirs, debug)
}
```

The real compiling and linking work is done in `compile()` function, which calls
`clang/llvm` to compile and link the C source code into BPF byte code, in
`pkt/datapath/loader/compile.go`:

```go
// compile and link a program.
func compile(ctx context.Context, prog *progInfo, dir *directoryInfo, debug bool) (err error) {
    args := make([]string, 0, 16)
    if prog.OutputType == outputSource {
        args = append(args, "-E") // Preprocessor
    } else {
        args = append(args, "-emit-llvm")
        if debug {
            args = append(args, "-g")
        }
    }
    args = append(args, standardCFlags...)
    args = append(args, progCFlags(prog, dir)...)

    // Compilation is split between two exec calls. First clang generates
    // LLVM bitcode and then later llc compiles it to byte-code.
    if prog.OutputType == outputSource {
        compileCmd := exec.CommandContext(ctx, compiler, args...)
        _ = compileCmd.CombinedOutput(log, debug)
    } else {
        switch prog.OutputType {
        case outputObject:
            compileAndLink(ctx, prog, dir, debug, args...)
        case outputAssembly:
            compileAndLink(ctx, prog, dir, false, args...)
        default:
            log.Fatalf("Unhandled progInfo.OutputType %s", prog.OutputType)
        }
    }
}
```

## 9.3 Reload datapath

At last, reload the BPF code.

Note that update of the datapath does not cause connections to be dropped [3].

`pkt/datapath/loader/loader.go`:

```go
func reloadDatapath(ctx context.Context, ep endpoint, dirs *directoryInfo) error {
    // Replace the current program
    objPath := path.Join(dirs.Output, endpointObj)
    if ep.MustGraftDatapathMap() {
        if err := graftDatapath(ctx, ep.MapPath(), objPath, symbolFromEndpoint); err != nil {
            scopedLog := ep.Logger(Subsystem).WithFields(logrus.Fields{
                logfields.Path: objPath,
            })
            scopedLog.WithError(err).Warn("JoinEP: Failed to load program")
            return err
        }
    } else {
        if err := replaceDatapath(ctx, ep.InterfaceName(), objPath, symbolFromEndpoint); err != nil {
            scopedLog := ep.Logger(Subsystem).WithFields(logrus.Fields{
                logfields.Path: objPath,
                logfields.Veth: ep.InterfaceName(),
            })
            scopedLog.WithError(err).Warn("JoinEP: Failed to load program")
            return err
        }
    }

    return nil
}
```

Source and object files are stored at `/var/run/cilium/state/<endpoint_id>` on
cilium agent.  also see `/var/lib/cilium` for more details.

Then, call `replaceDatapath` to migrate (gracefully update) BPF maps (k/v store
in kernel) and replace `tc` filter rules.

`pkt/datapath/loader/netlink.go`:

```go
// replaceDatapath the qdisc and BPF program for a endpoint
func replaceDatapath(ctx context.Context, ifName string, objPath string, progSec string) error {
    err := replaceQdisc(ifName)

    cmd := exec.CommandContext(ctx, "cilium-map-migrate", "-s", objPath)
    cmd.CombinedOutput(log, true)

    defer func() {
        if err == nil {
            retCode = "0"
        } else {
            retCode = "1"
        }
        args := []string{"-e", objPath, "-r", retCode}
        cmd := exec.CommandContext(ctx, "cilium-map-migrate", args...)
        _, _ = cmd.CombinedOutput(log, true) // ignore errors
    }()

    args := []string{"filter", "replace", "dev", ifName, "ingress",
        "prio", "1", "handle", "1", "bpf", "da", "obj", objPath,
        "sec", progSec,
    }
    cmd = exec.CommandContext(ctx, "tc", args...).WithFilters(libbpfFixupMsg)
    _ = cmd.CombinedOutput(log, true)
}
```

it first calls `replaceQdisc`, which is defined in the same file:

```go
func replaceQdisc(ifName string) error {
    link, err := netlink.LinkByName(ifName)
    attrs := netlink.QdiscAttrs{
        LinkIndex: link.Attrs().Index,
        Handle:    netlink.MakeHandle(0xffff, 0),
        Parent:    netlink.HANDLE_CLSACT,
    }

    qdisc := &netlink.GenericQdisc{
        QdiscAttrs: attrs,
        QdiscType:  "clsact",
    }

    netlink.QdiscReplace(qdisc)
}
```

then runs following 3 shell commands in wrapped code:

```shell
$ cilium-map-migrate -s <objPath>

$ tc filter replace dev <ifName> ingress prio 1 handle 1 bpf da obj <objPath> sec <progSec>

$ cilium-map-migrate -e <objPath> -r <retCode>
```

`cilium-map-migrate` tool is implemented in `bpf/cilium-map-migrate.c`.
This tool has no `-h` or `--help` options, you need to check the source code
for the (few) options provided.

[`tc` is used here for](http://borkmann.ch/talks/2016_netdev2.pdf):

* Drop monitor for policy learning
* Packet tracing infrastructure
* `bpf_trace_printk()` replacement

Refer to [2, 3] for more on this.

After above steps are done, we will see something like this on the host:

```shell
$ tc qdisc | grep <ifName>
qdisc noqueue 0: dev <ifName> root refcnt 2
qdisc clsact ffff: dev <ifName> parent ffff:fff1

# or
$ tc qdisc show dev <ifName> ingress
qdisc noqueue 0: root refcnt 2
qdisc clsact ffff: parent ffff:fff1
```

# 10 Conclusion

OK! This is what happens when calling Cilium CNI plugin to add network for a
container. For the limited space, we only covered some of the most important
steps and their corresponding implementations. Hope we could dive into more of
them in future posts.

**At the end, thanks to the Cilium team for making it so cool!**

## References

1. Cilium source code, [https://github.com/cilium/cilium](https://github.com/cilium/cilium)
2. [Advanced programmability and recent updates with tc’s cls bpf](http://borkmann.ch/talks/2016_netdev2.pdf), 2016
3. [Cilium: Networking and security for containers with BPF and XDP](https://opensource.googleblog.com/2016/11/cilium-networking-and-security.html), Google Blog, 2016
