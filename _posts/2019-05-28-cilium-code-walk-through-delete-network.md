---
layout    : post
title     : "Cilium Code Walk Through: CNI Delete Network"
date      : 2019-05-28
lastupdate: 2019-05-28
categories: cilium ebpf
---

### TL;DR

Code based on `1.5.1`.

```shell
cmdDel                                                // plugins/cilium-cni/cilium-cni.go
  |-NewDefaultClientWithTimeout
  |-EndpointDelete                                    // pkg/client/endpoint.go
  | |-DeleteEndpointID                                // api/client/endpoint/endpoint_client.go
  |   /\
  |   ||
  |   \/
  |  ServeHTTP                                        // api/v1/server/restapi/endpoint/delete_endpoint_id.go
  |    |-Handler.Handle()                             // api/v1/server/restapi/endpoint/delete_endpoint_id.go
  |      |- Handle()                                  // daemon/endpoint.go
  |        |- deleteEndpoint                          // daemon/endpoint.go
  |          |- deleteEndpointQuiet                   // daemon/endpoint.go
  |            |- CloseBPFProgramChannel              // daemon/endpoint.go
  |            |- SetStateLocked(StateDisconnecting)  //
  |            |- EventQueue.Stop()                   //
  |            |- endpointmanager.Remove(ep)          //
  |            |- ReleaseIP                           // pkg/ipam/allocator.go
  |              |- Release                           // k8s.io/kubernetes/pkg/registry/core/service/ipallocator/ipallocator.go
  |            |- WaitForProxyCompletions             // pkg/endpoint/endpoint.go
  |-RemoveIfFromNetNSIfExists
```

## 1 CNI: Delete Network

```go
func cmdDel(args *skel.CmdArgs) error {
	log.WithField("args", args).Debug("Processing CNI DEL request")

	c, err := client.NewDefaultClientWithTimeout() // cilium client

	id := endpointid.NewID(endpointid.ContainerIdPrefix, args.ContainerID)
	c.EndpointDelete(id)

	netNs, err := ns.GetNS(args.Netns)
	netns.RemoveIfFromNetNSIfExists(netNs, args.IfName)

	return nil
}
```

## 2 EndpointDelete

### 2.1 Client: EndpointDelete

`pkg/client/endpoint.go`:

```go
// EndpointDelete deletes endpoint
func (c *Client) EndpointDelete(id string) error {
	params := endpoint.NewDeleteEndpointIDParams().WithID(id).WithTimeout(api.ClientTimeout)
	_, _, err := c.Endpoint.DeleteEndpointID(params)
	return Hint(err)
}
```

`api/client/endpoint/endpoint_client.go`:

```go
/* DeleteEndpointID deletes endpoint

All resources associated with the endpoint will be freed and the
workload represented by the endpoint will be disconnected.It will no
longer be able to initiate or receive communications of any sort.
*/
func (a *Client) DeleteEndpointID(params *DeleteEndpointIDParams) (*DeleteEndpointIDOK, *DeleteEndpointIDErrors, error) {
	result, err := a.transport.Submit(&runtime.ClientOperation{
		ID:                 "DeleteEndpointID",
		Method:             "DELETE",
		PathPattern:        "/endpoint/{id}",
		Params:             params,
	})
	switch value := result.(type) {
	case *DeleteEndpointIDOK:
		return value, nil, nil
	case *DeleteEndpointIDErrors:
		return nil, value, nil
	}
	return nil, nil, nil

}
```

### 2.2 Server REST API: Handle Delete Endpoint

HTTP request receiving side,
`api/v1/server/restapi/endpoint/delete_endpoint_id.go`:

```go
func (o *DeleteEndpointID) ServeHTTP(rw http.ResponseWriter, r *http.Request) {
	route, rCtx, _ := o.Context.RouteInfo(r)
	var Params = NewDeleteEndpointIDParams()

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

## 2.3 Daemon: HTTP Handler

`daemon/endpoint.go`:

```go
func (h *deleteEndpointID) Handle(params DeleteEndpointIDParams) middleware.Responder {
	log.WithField(logfields.Params, logfields.Repr(params)).Debug("DELETE /endpoint/{id} request")

	d := h.daemon
	if nerr, err := d.DeleteEndpoint(params.ID); err != nil {
		if apierr, ok := err.(*api.APIError); ok {
			return apierr
		}
		return api.Error(DeleteEndpointIDErrorsCode, err)
	} else if nerr > 0 {
		return NewDeleteEndpointIDErrors().WithPayload(int64(nerr))
	} else {
		return NewDeleteEndpointIDOK()
	}
}
```

```go
func (d *Daemon) deleteEndpoint(ep *endpoint.Endpoint) int {
	scopedLog := log.WithField(logfields.EndpointID, ep.ID)
	errs := d.deleteEndpointQuiet(ep, endpoint.DeleteConfig{})
	for _, err := range errs {
		scopedLog.WithError(err).Warn("Ignoring error while deleting endpoint")
	}
	return len(errs)
}
```

```go
// deleteEndpointQuiet sets the endpoint into disconnecting state and removes
// it from Cilium, releasing all resources associated with it such as its
// visibility in the endpointmanager, its BPF programs and maps, (optional) IP,
// L7 policy configuration, directories and controllers.
//
// Specific users such as the cilium-health EP may choose not to release the IP
// when deleting the endpoint. Most users should pass true for releaseIP.
func (d *Daemon) deleteEndpointQuiet(ep *endpoint.Endpoint, conf endpoint.DeleteConfig) []error {
	ep.CloseBPFProgramChannel()

	ep.SetStateLocked(endpoint.StateDisconnecting, "Deleting endpoint")

	ep.EventQueue.Stop()

	// Remove the endpoint before we clean up. This ensures it is no longer
	// listed or queued for rebuilds.
	endpointmanager.Remove(ep, d)

	if !conf.NoIPRelease {
		d.ipam.ReleaseIP(ep.IPv4.IP())
		d.ipam.ReleaseIP(ep.IPv6.IP())
	}

	proxyWaitGroup := completion.NewWaitGroup()
	ep.WaitForProxyCompletions(proxyWaitGroup)

	return errs
}
```

## 2.4 IPAM: Delete IP

`pkg/ipam/allocator.go`:

```go
// ReleaseIP release a IP address.
func (ipam *IPAM) ReleaseIP(ip net.IP) error {
	if ip.To4() != nil {
		ipam.IPv4Allocator.Release(ip)
	} else {
		ipam.IPv6Allocator.Release(ip)
	}

	owner := ipam.owner[ip.String()]
	delete(ipam.owner, ip.String())
}
```

## 2.5 K8S IPAM: Delete IP

`k8s.io/kubernetes/pkg/registry/core/service/ipallocator/allocator.go`:

```go
// Release releases the IP back to the pool. Releasing an
// unallocated IP or an IP out of the range is a no-op and
// returns no error.
func (r *Range) Release(ip net.IP) error {
	ok, offset := r.contains(ip)
	if !ok {
		return nil
	}

	return r.alloc.Release(offset)
}
```

## 2.6 Cleanup Redirects

`pkg/endpoint/endpoint.go`:

```go
// WaitForProxyCompletions blocks until all proxy changes have been completed.
// Called with BuildMutex held.
func (e *Endpoint) WaitForProxyCompletions(proxyWaitGroup *completion.WaitGroup) error {
	err := proxyWaitGroup.Context().Err()
	if err != nil {
		return fmt.Errorf("context cancelled before waiting for proxy updates: %s", err)
	}

	proxyWaitGroup.Wait()
	return nil
}
```

## Appendix A - `NoIPRelease` Flag

When deleting an endpoint, a bool flag `NoIPRelease` could be set, indicating
whether to release the IP address of this endpoint. Most of the cases, this flag
is set to `false`, meaning deleting the IP address. But there are some special
cases where this flag is set to `true`:

1. Encounter fatal error during creating endpoint
1. Clean re-regenerate failed endpoints on agent restart
1. Cleanup cilium-health endpoint

### Fatal error during creating endpoint

`daemon/endpoint.go`:

```go
func (d *Daemon) errorDuringCreation() () {
	d.deleteEndpointQuiet(ep, endpoint.DeleteConfig{
		NoIPRelease: true, // The IP has been provided by the caller and must be released by the caller
	})
	ep.Warning("Creation of endpoint failed")
	return nil, PutEndpointIDFailedCode, err
}
```

`errorDuringCreation` will be returned in many places.

### Clean re-generate failed endpoints

In `daemon/state.go`:

```go
func (d *Daemon) regenerateRestoredEndpoints(state *endpointRestoreState) () {
	for i := len(state.restored) - 1; i >= 0; i-- {
		ep := state.restored[i]
		if err := endpointmanager.Insert(ep); err != nil {
			state.restored = append(state.restored[:i], state.restored[i+1:]...)
		}
	}

	for _, ep := range state.restored {
		go func() {
			identity := cache.AllocateIdentity()
			ep.SetIdentity(identity)
			ep.Regenerate(d, regenerationMetadata)
		}(ep)
	}

	for _, ep := range state.toClean {
		go func() {
			d.deleteEndpointQuiet(ep, endpoint.DeleteConfig{
				NoIdentityRelease: true,
				NoIPRelease:       true, // The IP was not allocated yet so does not need to be free.
			})
		}(ep)
	}
}
```

`daemon/health.go`:

```go
func (d *Daemon) cleanupHealthEndpoint() {
	health.KillEndpoint() // Delete the process

	// Clean up agent resources
	if localNode.IPv4HealthIP != nil {
		ep = endpointmanager.LookupIPv4(localNode.IPv4HealthIP.String())
	}
	if ep == nil && localNode.IPv6HealthIP != nil {
		ep = endpointmanager.LookupIPv6(localNode.IPv6HealthIP.String())
	}

	d.deleteEndpointQuiet(ep, endpoint.DeleteConfig{
		NoIPRelease: true,
	})
	health.CleanupEndpoint()
}
```

## Appendix B - How `cilium-health` endpoint retains it's IP address unchanged during agent re-install?

3 Steps:

1. Store the IP address to the node metadata using K8S annotations
1. When deleting the endpoint, to specify `NoIPRelease=true` to hold the IP
1. When starting again, retrieve the IP address from K8S node annotation, then
   create endpoint

### Step 1

Skip.

### Step 2

`daemon/health.go`:

```go
func (d *Daemon) initHealth() {

	// Launch another cilium-health as an endpoint, managed by cilium.
	var client *health.Client

	controller.NewManager().UpdateController("cilium-health-ep",
		controller.ControllerParams{
			DoFunc: func(ctx context.Context) error {
				if client != nil {
					err = client.PingEndpoint()
				}

				if client == nil || err != nil { // On the first initialization, or on error, restart the health EP.
					d.cleanupHealthEndpoint()
					client, err = health.LaunchAsEndpoint(d, LocalNode, d.mtuConfig)
				}
			},
			StopFunc: func(ctx context.Context) error {
				err := client.PingEndpoint()
				d.cleanupHealthEndpoint()
			},
			RunInterval: 30 * time.Second,
		},
	)
}
```

### Step 3

`cilium-health/launch/endpoint.go`:

```go
// LaunchAsEndpoint launches the cilium-health agent in a nested network
// namespace and attaches it to Cilium the same way as any other endpoint,
// but with special reserved labels.
func LaunchAsEndpoint(owner endpoint.Owner, n *node.Node, mtuConfig) (*Client, error) {
	info = &models.EndpointChangeRequest{
		ContainerName: ciliumHealth,
		State:         models.EndpointStateWaitingForIdentity,
		Addressing:    &models.AddressPair{},
	}

	if n.IPv4HealthIP != nil {
		healthIP = n.IPv4HealthIP
		info.Addressing.IPV4 = healthIP.String()
		ip4WithMask := net.IPNet{IP: healthIP, Mask: defaults.ContainerIPv4Mask}
		ip4Address = ip4WithMask.String()
	}

	connector.SetupVethWithNames(vethName, epIfaceName, MTU, info)

	cmd.Run(spawn_netns.sh, "-d --admin=unix --passive --pidfile xx")

	// Create endpoint
	endpoint.NewEndpointFromChangeModel(info)

	// Give the endpoint a security identity
	ep.UpdateLabels(context.Background(), owner, labels.LabelHealth, nil, true)

	// Set up the endpoint routes
	configureHealthRouting(info.ContainerName, epIfaceName, hostAddressing, mtuConfig)
	endpointmanager.AddEndpoint(owner, ep, "Create cilium-health endpoint")

	ep.SetStateLocked(endpoint.StateWaitingToRegenerate, "initial build of health endpoint")
	ep.PinDatapathMap()

	// regenerate BPF rules for this endpoint
	ep.Regenerate(owner, &endpoint.ExternalRegenerationMetadata{ Reason: "health daemon bootstrap", })

	// Initialize the health client to talk to this instance.
	client, err := healthPkg.NewClient("tcp://" + healthIP + healthDefaults.HTTPPathPort)

	return &Client{Client: client}, nil
}
```
