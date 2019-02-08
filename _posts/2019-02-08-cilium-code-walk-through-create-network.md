---
layout: post
title:  "Cilium Code Walk Through: CNI Create Network"
date:   2019-02-08
categories: cilium ebpf
---

### TL;DR

This post describes how Cilium CNI plugin works. Specifically, we will examine
the calling flows when K8S creates a container and asks plugin for setting up
network for the container. This includes, but not limited to:

1. create link device (e.g. veth pair)
1. allocate IP for container
1. configure IP address, route table, sysctl parameters, etc
1. create endpoint (cilium concept, namespaced interface to apply policy on)
1. generate, compile, link and inject BPF code into kernel

## 0 High Level Overview

Cilium code in this article bases on version `1.3.2`. To focous on the main
aspects, we will omit lots of unrelated lines (e.g. error handling, stats,
varaible declerations) in functions below. You should refer to the [source
code](https://github.com/cilium/cilium) in case you want to check more
implementation details.

**Note that I'm not very familir with Cilium at the time of writing this, so the
post may contain mistakes. I'm happy if anyone points them out and I'll update
it.**

### 0.1 Source Code Tree

1. `api/` - cilium REST API entrypoints
1. `daemon/` - cilium daemon (agent) implementation, including:

    1. IPAM API handler implementation
    1. endpoint API handler
    1. others

1. `plugin/` - plugin implementations for CNI, docker, etc
1. `pkg/` - cilium core functionalities implementation

    1. `client/` - cilium client library
    1. `datapath/` - packet forwarding plane related stuffs
    1. `endpoint/` - real endpoint logic
    1. `endpointmanager/` - real endpoint management logic
    1. `ipam/` - local IPAM implementation, which internally uses k8s local IPAM
    1. others

### 0.2 Plugin: Adding Network Skeleton Code

The cilium CNI plugin is implemented in a single file
`plugins/cilium-cni/cilium-cni.go`.

When kubelet calls the plugin to add network for a container, `cmdAdd()`
function will be invoked. The skeleton code is as follows:

```go
func cmdAdd(args *skel.CmdArgs) (err error) {
	// load data from stdin, TODO: what data?
	n, cniVer := loadNetConf(args.StdinData)

	// init variables
	c := client.NewDefaultClientWithTimeout(defaults.ClientConnectTimeout) // cilium client
	ep := &models.EndpointChangeRequest{}

	// create link: veth pair, IPVLAN, etc
	switch conf.DatapathMode {
	case option.DatapathModeVeth:
		veth, peer, tmpIfName := connector.SetupVeth(ep.ContainerID, int(conf.DeviceMTU), ep)
		netlink.LinkSetNsFd(*peer, int(netNs.Fd()))
		connector.SetupVethRemoteNs(netNs, tmpIfName, args.IfName)
	}

	// allocate IP (IPv4 & IPv6) from local IPAM
	ipam := c.IPAMAllocate("")

	// configure network settings
	ep.Addressing.IPV4 = ipam.Address.IPV4
	ipConfig, routes := prepareIP(ep.Addressing.IPV4, false, &state, int(conf.RouteMTU))

	netNs.Do(func(_ ns.NetNS) error {
		macAddrStr = configureIface(ipam, args.IfName, &state)
	})

	// create endpoint, generate eBPF code
	c.EndpointCreate(ep)
}
```

### 0.3 Plugin: Add Network Steps

Summarizing the above add network steps:

1. preparations: parse CNI parameters received from kubelet, init variables
2. create link device for container and link it to host network, e.g. using veth pair, IPVLAN
3. allocate IP addresses for container from local IPAM
4. confiure IP addresses, route table, sysctl parameters, etc for container
5. create endpoint, generate BPF code

In the next, we will walk through this process in detail.

## 1 Preparation Works

### 1.1 Parse CNI Parameters

On requesting for adding network for a container, kubelet will pass a
`CmdArgs` type variable to CNI plugin, which is defined in
`github.com/containernetworking/cni/pkg/skel`:

```go
// CmdArgs captures all the arguments passed in to the plugin
// via both env vars and stdin
type CmdArgs struct {
        ContainerID string
        Netns       string
        IfName      string
        Args        string
        Path        string
        StdinData   []byte
}
```

information included in `CmdArgs`:

* container ID
* container network namespace
* desired interface name for container, e.g. `eth0`
* additional platform-dependent arguments of the container, e.g.
  `K8S_POD_NAMESPACE=default;K8S_POD_NAME=busybox;K8S_POD_INFRA_CONTAINER_ID=3b126ac`
* path for locating CNI plugin binary, e.g. `/opt/cni/bin`
* network configuration information passed through stdin

Among them, `StdinData` field will be unmarshalled into a `netConf` instance:

```go
type netConf struct {
	cniTypes.NetConf
	MTU  int  `json:"mtu"`
	Args Args `json:"args"`
}
```

where `cniTypes.NetConf` is defined in `github.com/containernetworking/cni/pkg/types/types.go`:

```go
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

### 1.2 Init Varaibles

This section also initializes some varaibles that will be used later, the most
important ones:

* a cilium client variable
* an `EndpointChangeRequest` variable, holding request data that will be
  used for creating an Endpoint

```go
	// init variables
	c := client.NewDefaultClientWithTimeout(defaults.ClientConnectTimeout) // cilium client
	ep := &models.EndpointChangeRequest{...}
```

## 2 Create Link Device

Cilium supports two kinds of datapath types (and thus link device types) at the
time of writing this post:

1. veth pair
1. IPVLAN

We will take veth pair as example in the following.

### 2.1 Create Veth Pair

In Cilium, the virtual device used for connecting container to host is called a
"connector".

First, call `connector.SetupVeth()` to create a veth pair, taking container ID,
MTU and endpoint info as parameters.

```go
		veth, peer, tmpIfName := connector.SetupVeth(ep.ContainerID, int(conf.DeviceMTU), ep)
```

Naming rule for the veth pair devies:

* **host side**: `lxc` + first N digits of `sha256(containerID)`, e.g. `lxc12c45`
* **peer (container) side**: `tmp` + first N digits of `sha256(containerID)`, e.g. `tmp12c45`

Additional steps in `connector` (`pkg/endpoint/connector/veth.go`):

1. set sysctl parameters: `/proc/sys/net/ipv4/conf/<veth>/rp_filter = 0`
1. set MTU
1. fill endpoint info: MAC (container side), host side MAC, interface name, interface index

### 2.2 Set Peer's Network Namespace

Put the peer end to container by setting the peer's network namespace
to container's netns:

```go
		netlink.LinkSetNsFd(*peer, int(netNs.Fd()))
```

As a consequence, the peer device will "disapper" from host, namely, it will not
be listed when executing `ifconfig` or `ip link` commands on the host.
You must specify the netns in order to see it: `ip netns exec <netns> ip link`.

### 2.3 Rename Peer

After putting the peer end to container, rename the peer to the given name in
CNI arguments:

```go
		connector.SetupVethRemoteNs(netNs, tmpIfName, args.IfName)
```

This will, for example, rename `tmp53057` to `eth0` inside container.

Sum up, that is **how the `eth0` device comes to exist of each container**.

## 3 Allocating IP

In the next, the pugin will try to allocate IP addresses (IPv4 & IPv6) from the
local IPAM inside cilium agent.

Cilium agent is a daemon process running on each host, it includes many services
inside itself, for example, **local IPAM**, **endpoint manager**, etc.
These services expose REST APIs.

This part is more complicated than it looks like. The code is just one line in
plugin code:

```go
	ipam := c.IPAMAllocate("")
```

But the call stack will jump among different places:

1. plugin - `plugin/cilium-cni/`
1. cilium client - `pkg/client/`
1. clium REST API - `api/v1/server/restapi/ipam/`
1. cilium API server - `api/v1/server/restapi/ipam`
1. real HTTP handler - `daemon/ipam.go`
1. cilium IPAM implementation (actually is only a wrapper) - `pkg/ipam/`
1. final IPAM implementation (k8s builtin) - `k8s.io/kubernetes/pkg/registry/core/service/ipallocator`

Let's see them.

### 3.1 Allocate IP Address

Start from `plugin/cilium-cni/cilium-cni.go`:

```go
	ipam := c.IPAMAllocate("")
```

where `c` is an instance of cilium client.

`IPAMAllocate()` takes address family as parameter; if not specified, it will
create both an IPv4 and an IPv6 address. Each of them will be saved at the
following fields:

* `ipam.Address.IPV4`
* `ipam.Address.IPV6`

Let's move to this function.

### 3.2 Cilium Client: `IPAMAllocate()`

`pkt/client/ipam.go`:

```go
// IPAMAllocate allocates an IP address out of address family specific pool.
func (c *Client) IPAMAllocate(family string) (*models.IPAMResponse, error) {
	params := ipam.NewPostIPAMParams().WithTimeout(api.ClientTimeout)

	if family != "" {
		params.SetFamily(&family)
	}

	resp := c.IPAM.PostIPAM(params)
	return resp.Payload, nil
}
```

where, the client structure is defined in `pkt/client/ipam.go`:

```go
type Client struct {
	clientapi.Cilium
}
```

where in turn, the client API structure is defined in
`api/v1/client/cilium_client.go`:

```go
// clientapi
type Cilium struct {
	Daemon *daemon.Client

	Endpoint *endpoint.Client
	IPAM *ipam.Client              // point to "api/v1/client/ipam"
	Metrics *metrics.Client
	Policy *policy.Client
	Prefilter *prefilter.Client
	Service *service.Client
	Transport runtime.ClientTransport
}
```

OK, now we need to move to `c.IPAM.PostIPAM(params)`.

### 3.3 Call REST API: Allocate IP

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

This will send out the request with `POST` method, `/ipam` URI and many
other HTTP parameters.

### 3.4 IPAM API Server

HTTP request receiving side, `api/v1/server/restapi/ipam/post_ip_a_m.go`:

```go
func (o *PostIPAM) ServeHTTP(rw http.ResponseWriter, r *http.Request) {
	route, rCtx, _ := o.Context.RouteInfo(r)
	if rCtx != nil {
		r = rCtx
	}
	var Params = NewPostIPAMParams()

	if err := o.Context.BindValidRequest(r, route, &Params); err != nil { // bind params
		o.Context.Respond(rw, r, route.Produces, route, err)
		return
	}

	res := o.Handler.Handle(Params) // actually handle the request

	o.Context.Respond(rw, r, route.Produces, route, res)
}
```

As can be seen, the real processing logic is done in `o.Handler.Handle()`
method. This method is implemented in daemon code.

### 3.5 IPAM HTTP Handler

`daemon/ipam.go`:

```go
// Handle incoming requests address allocation requests for the daemon.
func (h *postIPAM) Handle(params ipamapi.PostIPAMParams) middleware.Responder {
	resp := &models.IPAMResponse{
		HostAddressing: node.GetNodeAddressing(),
		Address:        &models.AddressPair{},
	}

	ipv4, ipv6 := h.daemon.ipam.AllocateNext(params.Family)

	resp.Address.IPV4 = ipv4.String()
	resp.Address.IPV6 = ipv6.String()

	return ipamapi.NewPostIPAMCreated().WithPayload(resp)
}
```

where `h.daemon.ipam` is actually a CIDR range, initialized as:

`pkg/ipam/ipam.go`:

```go
import "k8s.io/kubernetes/pkg/registry/core/service/ipallocator"

// NewIPAM returns a new IP address manager
func NewIPAM(nodeAddressing datapath.NodeAddressing, c Configuration) *IPAM {
	ipam := &IPAM{
		nodeAddressing: nodeAddressing,
		config:         c,
	}

	if c.EnableIPv6 {
		ipam.IPv6Allocator = ipallocator.NewCIDRRange(nodeAddressing.IPv6().AllocationCIDR().IPNet)
	}

	if c.EnableIPv4 {
		ipam.IPv4Allocator = ipallocator.NewCIDRRange(nodeAddressing.IPv4().AllocationCIDR().IPNet)
	}

	return ipam
}
```

It will allocate IP addresses from the specified CIDR.

And more, this is an **in-memory IPAM** - which means, all its states (e.g.
allocated IPs, avaialbe IPs, reserved IPs) are stored in memory, thus will not
survive service restart. But don't worry, Cilium has it's own way to recovery
the states on service restart or host reboot - by using other states stored in
etcd.

Let's moving on to `h.daemon.ipam.AllocateNext(params.Family)`.

### 3.6 IPAM Implementation In `pkg/ipam`

`pkg/ipam/allocator.go`:

```go
// AllocateNext allocates the next available IPv4 and IPv6 address out of the
// configured address pool.
func (ipam *IPAM) AllocateNext(family string) (ipv4 net.IP, ipv6 net.IP, err error) {
	ipv6 = ipam.AllocateNextFamily(IPv6)
	ipv4 = ipam.AllocateNextFamily(IPv4)
}

// AllocateNextFamily allocates the next IP of the requested address family
func (ipam *IPAM) AllocateNextFamily(family Family) (ip net.IP, err error) {
	switch family {
	case IPv6:
		ip = allocateNextFamily(family, ipam.IPv6Allocator)
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

After several layers of calls, the execution comes to
`allocator.AllocateNext()`, where `AllocateNext()` is an interface defined in
`k8s.io/kubernetes/pkg/registry/core/service/ipallocator`.

### 3.7 Real Allocation Logic in K8S Builtin IPAM

Now, IP allocation code comes to K8S code.

Reserve IP from pool, in `k8s.io/kubernetes/pkg/registry/core/service/ipallocator`:

```go
func (r *Range) AllocateNext() (net.IP, error) {
	offset, ok := r.alloc.AllocateNext()
	return addIPOffset(r.base, offset), nil
}
```

where `r.alloc` is an interface:

```go
// Interface manages the allocation of IP addresses out of a range. Interface
// should be threadsafe.
type Interface interface {
	Allocate(net.IP) error
	AllocateNext() (net.IP, error)
	Release(net.IP) error
	ForEach(func(net.IP))

	// For testing
	Has(ip net.IP) bool
}
```

`r.alloc.AllocateNext()` returns the offset of next available IP from the first
IP (`r.base`) in this CIDR, and `addIPOffset()` converts the offset to a
`net.IP` format presentation.

moving on, let's see the allocation details, in
`k8s.io/kubernetes/pkg/registry/core/service/allocator/bimap.go`:

```go
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

## 4 Configure Network

Now, IP addresses have been ready. Next steps are:

* calulate routes, gateway
* configure ip, routes, gateway, sysctl, etc

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

### 4.1 Prepare IP Addresses, Routes, Gateway

First, call `prepareIP()` function to prepare the IP Addresses, gateways, route
entries, etc. The function will return a pointer to an `IPConfig`, which holds
the IP and gateway information, and a `Route` entry list.

In `plugins/cilium-cni/cilium-cni.go`:

```go
func prepareIP(ipAddr string, isIPv6 bool, state *CmdState, mtu int) (*cniTypesVer.IPConfig, []*cniTypes.Route, error) {
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

#### CiliumIP

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

#### Routes

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

#### Gateway

note that, the gateway is just set to the host's IP (TODO: more info on this):

```go
// IPv4Gateway returns the IPv4 gateway address for endpoints.
func IPv4Gateway(addr *models.NodeAddressing) string {
	// The host's IP is the gateway address
	return addr.IPV4.IP
}
```

### 4.2 Configure Network

After network information has been prepared well, next step is to
configure them to the container. This is achieved by calling `configureIface`:

```go
func configureIface(ipam *models.IPAMResponse, ifName string, state *CmdState) (string, error) {
	l := netlink.LinkByName(ifName)

	addIPConfigToLink(state.IP4, state.IP4routes, l, ifName)
	addIPConfigToLink(state.IP6, state.IP6routes, l, ifName)
	netlink.LinkSetUp(l)
}
```

`configureIface` first finds the link device by device name (`eth0` inside
container), then calls `addIPConfigToLink` to do the real jobs:

```go
func addIPConfigToLink(ip addressing.CiliumIP, routes []route.Route, link netlink.Link, ifName string) error {
	addr := &netlink.Addr{IPNet: ip.EndpointPrefix()}
	netlink.AddrAdd(link, addr)

	// Sort provided routes to make sure we apply any more specific
	// routes first which may be used as nexthops in wider routes
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

## 5 Create Endpoint

What is an Endpoint?  An endpoint is a "**namespaced network interface** to which
cilium applies policies".

From `api/v1/models/endpoint.go`:

```go
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

### 5.1 CNI: Create Endpoint

Start from CNI code, `plugins/cilium-cni/cilium-cni.go`:

```go
	ep.SyncBuildEndpoint = true
	c.EndpointCreate(ep)
```

It calls the client's `EndpointCreate()` method.

### 5.2 Cilium Client: Create Endpoint

In `pkg/client/endpoint.go`:

```go
func (c *Client) EndpointCreate(ep *models.EndpointChangeRequest) error {
	id := pkgEndpointID.NewCiliumID(ep.ID)
	params := endpoint.NewPutEndpointIDParams().WithID(id).WithEndpoint(ep).WithTimeout(api.ClientTimeout)
	_ := c.Endpoint.PutEndpointID(params)
	return Hint(err)
}
```

Ignore the `ep.ID`, it is not used now.

First call `NewCiliumID()` to generate the local endpoint identifier,
`pkg/endpoint/id/id.go`:

```go
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

~~as the comments say, this ID is host-local, which means it is unique within
the host. So, the final generated ID will be something like
`cilium-local:53057`.~~

Then, the client code arranges request data, and `PUT` that data to Cilium REST
API with `PutEndpointID(params)`, in `api/client/endpoint/endpoint_client.go`:

```go
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

### 5.3 Cilium HTTP Server: Create Endpoint

The API server is embedded in the daemon process.
HTTP requests entrypoint, `api/server/restapi/endpint/put_endpoint_id.go`:

```go
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

### 5.4 HTTP Handler

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
func (d *Daemon) createEndpoint(ctx context.Context, epTemplate *models.EndpointChangeRequest) (*endpoint.Endpoint, error) {

	endpointmanager.AddEndpoint(d, ep, "Create endpoint from API PUT")
	ep.UpdateLabels(d, addLabels, infoLabels, true)

	if build {
		// Do not synchronously regenerate the endpoint when first creating it.
		// We have custom logic later for waiting for specific checkpoints to be
		// reached upon regeneration later (checking for when BPF programs have
		// been compiled), as opposed to waiting for the entire regeneration to
		// be complete (including proxies being configured). This is done to
		// avoid a chicken-and-egg problem with L7 policies are imported which
		// select the endpoint being generated, as when such policies are
		// imported, regeneration blocks on waiting for proxies to be
		// configured. When Cilium is used with Istio, though, the proxy is
		// started as a sidecar, and is not launched yet when this specific code
		// is executed; if we waited for regeneration to be complete, including
		// proxy configuration, this code would effectively deadlock addition
		// of endpoints.
		ep.Regenerate(d, &endpoint.ExternalRegenerationMetadata{
			Reason: "Initial build on endpoint creation",
		})
	}

	// Wait for any successful BPF regeneration, which is indicated by any
	// positive policy revision (>0). As long as at least one BPF
	// regeneration is successful, the endpoint has network connectivity
	// so we can return from the creation API call.
	revCh := ep.WaitForPolicyRevision(ctx, 1)
}
```

`endpointmanager.AddEndpoint` will further call `Insert(ep)`.

Another important thing done in this method is **triggering BPF code
re-generation**, by calling `ep.Regenerate()` with reason "Initial build on
endpoint creation".

If re-generated successful, the revision number will be positive.

As long as at least one BPF regeneration is successful, the endpoint has network
connectivity.

### 5.5 Re-generate BPF Code

This is a typical workflow [3]:

1. generate eBPF source code (in a subset of C)
2. compile to ELF file with LLVM, which contains program code, specification for maps and related relocation data
3. parse ELF content and load the program into the kernel with tools like `tc` (traffic control)

> In eBPF, maps are efficient key/value stores in the kernel that can be shared
> between various eBPF programs, but also between user space.

Now let's continue the BPF code regeneration path.

`pkg/endpoint/policy.go`:

```go
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

### 5.6 Compile and Link BPF Code

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

### 5.7 Reload Datapath

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

## 6 Conclusion

OK! This is what happens when calling Cilium CNI plugin to add network for a
container. For the limited space, we only covered some of the most important
steps and their corresponding implementations. Hope we could dive into more of
them in future posts.

**At the end, thanks to the Cilium team for making it so cool!**

## References

1. Cilium source code, [https://github.com/cilium/cilium](https://github.com/cilium/cilium)
2. [Advanced programmability and recent updates with tc’s cls bpf](http://borkmann.ch/talks/2016_netdev2.pdf), 2016
3. [Cilium: Networking and security for containers with BPF and XDP](https://opensource.googleblog.com/2016/11/cilium-networking-and-security.html), Google Blog, 2016
