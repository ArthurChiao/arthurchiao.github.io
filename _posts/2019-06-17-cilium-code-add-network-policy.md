---
layout    : post
title     : "Cilium Code Walk Through: Add Network Policy"
date      : 2020-07-03
lastupdate: 2022-09-13
categories: cilium
---

This post is included in
[Cilium Code Walk Through Series]({% link _posts/2019-06-17-cilium-code-series.md %}).

This post walks you through the network policy enforcement process. Code based
on Cilium `1.8.0/1.10.7`.

NOTE: this post is not well organized yet, posted mainly to memorize the calling stack.

----

* TOC
{:toc}

----

# 1 Call stack: start from `policyAdd()`

```
policyAdd                                                  // daemon/policy.go
  |-log.Info("Policy imported via API, recalculating...")
  |-d.policy.AddListLocked(sourceRules)
  |   |-d.policy.AddListLocked(sourceRules)
  |       |-p.rules = append(p.rules, newList...)
  |       |-return newList, newRevsion
  |
  |-if Config.SelectiveRegeneration // default true
  |   ev := eventqueue.NewEvent()
  |   d.policy.RuleReactionQueue.Enqueue(ev)
  |-else
      TriggerPolicyUpdates                                 // daemon/policy.go
        |-TriggerWithReason                                // pkg/trigger/trigger.go
           |- t.wakeupChan <- true                         // pkg/trigger/trigger.go
                /\
                ||
                \/
        func (t *Trigger) waiter()                         // pkg/trigger/trigger.go
		  t.params.TriggerFunc(reasons)
```

## 1.1 `TriggerFunc`: `policyUpdateTrigger`

```
        func (t *Trigger) waiter()                         // pkg/trigger/trigger.go
		  t.params.TriggerFunc(reasons)
                   /
                  /
   policyUpdateTrigger                                     // daemon/policy.go
	 |-meta := &regeneration.ExternalRegenerationMetadata{
	 |   RegenerationLevel: RegenerateWithoutDatapath,
	 | }
     |-RegenerateAllEndpoints(meta)                        // pkg/endpointmanager/manager.go
         for ep in eps:
           RegenerateIfAlive(meta)                         // pkg/endpoint/policy.go
             |-Regenerate(meta)                            // pkg/endpoint/policy.go
               |- eventqueue.NewEvent(meta)
                  eventQueue.Enqueue()                     // pkg/eventqueue/eventqueue.go
                              /\
                              ||
                              \/
                  eventQueue.Run()                         // pkg/eventqueue/eventqueue.go
                    for ev in events:
                       ev.Metadata.Handle()
                                    |
         EndpointRegenerationEvent.Handle                  // pkg/endpoint/events.go
```

## 1.2 `TriggerFunc`: `datapathRegen`

```
        func (t *Trigger) waiter()                         // pkg/trigger/trigger.go
		  t.params.TriggerFunc(reasons)
                   /
                  /
   datapathRegen
	 |-meta := &regeneration.ExternalRegenerationMetadata{
	 |   RegenerationLevel: RegenerateWithDatapathRewrite,
	 | }
     |-RegenerateAllEndpoints(meta)
```

## 1.3 Add an `allow` policy

Add an `allow` policy: several places will call into the `Allow()` method in the end:

```
// case 1
updateSelectorCacheFQDNs  // daemon/cmd/fqdn.go
 |-UpdatePolicyMaps  // pkg/endpointmanager/
   |-ApplyPolicyMapChanges
     |-applyPolicyMapChanges
       |-addPolicyKey // pkg/endpoint/bpf.go
         |-AllowKey
            |-Allow

// case 2
syncPolicyMapController(1min, "sync-policymap-<ep id>") // pkg/endpoint/bpf.go
 |-syncPolicyMapWithDump
    |-applyPolicyMapChanges
      |-addPolicyKey // pkg/endpoint/bpf.go
         |-AllowKey
            |-Allow

// case 3
regenerateBPF                   // pkg/endpoint/bpf.go
 |-syncPolicyMap
    |-applyPolicyMapChanges
    | |-addPolicyKey            // pkg/endpoint/bpf.go
    |    |-AllowKey
    |        |-Allow
    |-syncDesiredPolicyMapWith
      |-addPolicyKey // pkg/endpoint/bpf.go
         |-AllowKey
             |-Allow
```

## 1.4 `EndpointRegenerationEvent.Handle()`

```
EndpointRegenerationEvent.Handle                                              //    pkg/endpoint/events.go
  |-regenerate(ctx)                                                           //    pkg/endpoint/policy.go
    |-if e.skippedRegenerationLevel > ctx.regenerationLevel
    |   ctx.regenerationLevel = skippedRegenerationLevel
    |-regenerateBPF                                                           //    pkg/endpoint/bpf.go
       |-runPreCompilationSteps                                               //    pkg/endpoint/bpf.go
       |  |-regeneratePolicy                                                  //    pkg/endpoint/policy.go
       |  |  |-UpdatePolicy                                                   //    pkg/policy/distillery.go
       |  |  |  |-cache.updateSelectorPolicy                                  //    pkg/policy/distillery.go
       |  |  |     |-cip = cache.policies[identity.ID]                        //    pkg/policy/distillery.go
       |  |  |     |-resolvePolicyLocked                                      // -> pkg/policy/repository.go
       |  |  |     |  |-matchingRules.resolveL4IngressPolicy                  //    pkg/policy/repository.go
       |  |  |     |  |-p.rules.resolveL4IngressPolicy                        //    pkg/policy/rules.go
       |  |  |     |     |-resolveIngressPolicy                               // -> pkg/policy/rule.go
       |  |  |     |        |-GetSourceEndpointSelectorsWithRequirements      // -> pkg/policy/api/ingress.go
       |  |  |     |        |-mergeIngress                                    //    pkg/policy/rule.go
       |  |  |     |           |-mergeIngressPortProto         // L3-only rule
       |  |  |     |           |-toPorts.Iterate(func(ports) { // L4/L7 rule
       |  |  |     |               for p := range GetPortProtocols() {
       |  |  |     |                 mergeIngressPortProto(TCP/UDP/...)
       |  |  |     |                   |-createL4IngressFilter
       |  |  |     |                   |-addL4Filter(ruleLabels)
       |  |  |     |                       |-mergePortProto
       |  |  |     |                       |-if ruleLabels in existingRuleLabels
       |  |  |     |                       |    exists = true
       |  |  |     |                       |-if !exists:
       |  |  |     |                       |    existingFilter.DerivedFromRules.append(ruleLabels)
       |  |  |     |               }
       |  |  |     |-cip.setPolicy(selPolicy)                                 //    pkg/policy/distillery.go
       |  |  |-e.selectorPolicy.Consume                                       //    pkg/policy/distillery.go
       |  |     |-if !IngressPolicyEnabled || !EgressPolicyEnabled
       |  |     |  |-AllowAllIdentities(!IngressPolicyEnabled, !EgressPolicyEnabled)
       |  |     |-DistillPolicy                                               //    pkg/policy/resolve.go
       |  |        |-computeDesiredL4PolicyMapEntries                         //    pkg/policy/resolve.go
       |  |           |-computeDirectionL4PolicyMapEntries                    //    pkg/policy/resolve.go
       |  |-updateNetworkPolicy                                               //    pkg/endpoint/policy.go
       |  |  |-e.proxy.UpdateNetworkPolicy                                    //    pkg/proxy/proxy.go
       |  |      |-p.XDSServer.UpdateNetworkPolicy                            //    pkg/envoy/server.go
       |  |-addNewRedirects                                                   //    pkg/endpoint/bpf.go
       |  |  |-addNewRedirectsFromDesiredPolicy                               //    pkg/endpoint/bpf.go
       |  |     |-updateProxyRedirect                                         //    pkg/endpoint/proxy.go
       |  |        |-e.proxy.CreateOrUpdateRedirect                           //    pkg/proxy/proxy.go
       |  |          |-redir := newRedirect()                                 //    pkg/proxy/redirect.go
       |  |            switch l4.L7Parser:
       |  |              case DNS  : createDNSRedirect
       |  |              case Kafka: createKafkaRedirect                      // pkg/proxy/kafka.go
       |  |                           |-listener := kafkaListeners[proxyPort]
       |  |                           |-listenSocket()                        // pkg/proxy/kafka.go
       |  |                           |-go listener.Listen()                  // pkg/proxy/kafka.go
       |  |                                /
       |  |                               /
       |  |                  func (l *kafkaListener) Listen() {               // pkg/proxy/kafka.go
       |  |                    for {
       |  |                      pair := l.socket.Accept(true)
       |  |                      go redir.handleRequestConnection(pair)
       |  |                          |-k.handleRequests(handler)              // pkg/proxy/kafka.go
       |  |                              |-for {
       |  |                                  req := kafka.ReadRequest(c.conn)
       |  |                                  handler(pair, req ...)
       |  |                                   |-handleRequest                 // pkg/proxy/kafka.go
       |  |                                      |-if !k.canAccess
       |  |                                           req.CreateResponse(ErrTopicAuthorizationFailed)
       |  |                                           return
       |  |                                        pair.Rx.Enqueue(resp)
       |  |                                        correlationCache.HandleRequest // pkg/kafka/correlation_cache.go
       |  |                                        pair.Tx.Enqueue(req)
       |  |                                }
       |  |                    }
       |  |                  }
       |  |              case HTTP : createEnvoyRedirect
       |  |              default   : createEnvoyRedirect
       |  |-removeOldRedirects                                                //    pkg/endpoint/bpf.go
       |  |-writeHeaderfile(nextDir)                                          //    pkg/endpoint/bpf.go
       |  |-createEpInfoCache                                                 // -> pkg/endpoint/cache.go
       |-realizeBPFState
       |-eppolicymap.WriteEndpoint
       |-lxcmap.WriteEndpoint
       |-waitForProxyCompletions
       |-syncPolicyMap()
          |-applyPolicyMapChanges
          |  |-addPolicyKey                                                    // pkg/endpoint/bpf.go
          |     |-AllowKey
          |        |-Allow
          |
          |-syncDesiredPolicyMapWith
             |-addPolicyKey                                                    // pkg/endpoint/bpf.go
                |-AllowKey
                   |-Allow
```

# 2 L7 policy: `addNewRedirects()`

```go
// adding an l7 redirect for the specified policy.
// The returned map contains the exact set of IDs of proxy redirects that is
// required to implement the given L4 policy.
func (e *Endpoint) addNewRedirects(proxyWaitGroup *completion.WaitGroup) (map[string]bool, ...) {
	desiredRedirects = make(map[string]bool)

	for dirLogStr, ingress := range map[string]bool{"ingress": true, "egress": false} {
		e.addNewRedirectsFromDesiredPolicy(ingress, desiredRedirects, proxyWaitGroup)

		e.addVisibilityRedirects(ingress, desiredRedirects, proxyWaitGroup)
	}

	return desiredRedirects, nil, ...
```

```go
func (e *Endpoint) addNewRedirectsFromDesiredPolicy(ingress bool, desiredRedirects ...) (error, ...) {
	var m policy.L4PolicyMap

	m = ingress? e.desiredPolicy.L4Policy.Ingress : Egress
	insertedDesiredMapState := make(map[policy.Key]struct{})
	updatedDesiredMapState := make(policy.MapState)

	for _, l4 := range m {
		if l4.IsRedirect() {
			var redirectPort uint16

			// Only create a redirect if the proxy is NOT running in a sidecar
			// container. If running in a sidecar container, just allow traffic
			// to the port at L4 by setting the proxy port to 0.
			if !e.hasSidecarProxy || l4.L7Parser != policy.ParserTypeHTTP {
				e.updateProxyRedirect(l4, proxyWaitGroup)

				proxyID := e.ProxyID(l4)
				if e.realizedRedirects == nil {
					e.realizedRedirects = make(map[string]uint16)
				}
				e.realizedRedirects[proxyID] = redirectPort

				desiredRedirects[proxyID] = true
			}

			// Set the proxy port in the policy map.
			direction = l4.Ingress? trafficdirection.Ingress : Egress
			keysFromFilter := l4.ToMapState(direction)

			for keyFromFilter, entry := range keysFromFilter {
				if oldEntry, ok := e.desiredPolicy.PolicyMapState[keyFromFilter]; ok
					updatedDesiredMapState[keyFromFilter] = oldEntry
				else
					insertedDesiredMapState[keyFromFilter] = struct{}{}

				if entry != policy.NoRedirectEntry
					entry.ProxyPort = redirectPort

				e.desiredPolicy.PolicyMapState[keyFromFilter] = entry
			}
		}
	}

	return nil, ...
}
```

```go
// updateProxyRedirect updates the redirect rules in the proxy for a particular
// endpoint using the provided L4 filter. Returns the allocated proxy port
func (e *Endpoint) updateProxyRedirect(l4 *policy.L4Filter, proxyWaitGroup) (proxyPort uint16, ...) {
	return e.proxy.CreateOrUpdateRedirect(l4, e.ProxyID(l4), e, proxyWaitGroup)
}
```

```go
// CreateOrUpdateRedirect creates or updates a L4 redirect with corresponding
// proxy configuration. This will allocate a proxy port as required and launch
// a proxy instance. If the redirect is already in place, only the rules will be
// updated.
func (p *Proxy) CreateOrUpdateRedirect(l4 policy.ProxyPolicy, id string, localEndpoint logger.EndpointUpdater,
	wg *completion.WaitGroup) (proxyPort uint16, ...) {

	if redir, ok := p.redirects[id]; ok {
		if redir.listener.parserType == l4.GetL7Parser() {
			redir.updateRules(l4)
			redir.implementation.UpdateRules(wg)
			redir.lastUpdated = time.Now()
			proxyPort = redir.listener.proxyPort
			return
		}

		p.removeRedirect(id, wg) // remove old redirect
	}

	pp := getProxyPort(l4.GetL7Parser(), l4.GetIngress())
	redir := newRedirect(localEndpoint, pp, l4.GetPort())
	redir.updateRules(l4)

	for nRetry := 0; nRetry < redirectCreationAttempts; nRetry++ {
		if !pp.configured {
			// Try allocate (the configured) port
			pp.proxyPort, err = allocatePort(pp.proxyPort, p.rangeMin, p.rangeMax)
		}

		switch l4.GetL7Parser() {
		case policy.ParserTypeDNS:
			redir.implementation, err = createDNSRedirect(redir, )
		case policy.ParserTypeKafka:
			redir.implementation, err = createKafkaRedirect(redir, )
		case policy.ParserTypeHTTP:
			redir.implementation, err = createEnvoyRedirect(redir, )
		default:
			redir.implementation, err = createEnvoyRedirect(redir, )
		}

		if err == nil {
			Debug("Created new ", l4.GetL7Parser(), " proxy instance")
			p.redirects[id] = redir

			pp.reservePort() // mark the proxyPort configured
			proxyPort = pp.proxyPort
			return
		}
	}

	log.Error("Unable to create ", l4.GetL7Parser(), " proxy")
	return 0, err, nil, nil
}
```

# 3 L7 policy: Kafka

```go
// HandleRequest must be called when a request is forwarded to the broker, will
// keep track of the request and rewrite the correlation ID inside of the
// request to a sequence number. This sequence number is guaranteed to be
// unique within the connection covered by the cache.
func (cc *CorrelationCache) HandleRequest(req *RequestMessage, finishFunc FinishFunc) {

	origCorrelationID := req.GetCorrelationID()
	newCorrelationID := cc.nextSequenceNumber
	cc.nextSequenceNumber++

	// Overwrite the correlation ID in the request to allow correlating the
	// response later on. The original correlation ID will be restored when
	// forwarding the response
	req.SetCorrelationID(newCorrelationID)

	cc.cache[newCorrelationID] = &correlationEntry{
		request:           req,
		created:           time.Now(),
		origCorrelationID: origCorrelationID,
		finishFunc:        finishFunc,
	}
}
```

# 4 policy calc

```
// DistillPolicy filters down the specified selectorPolicy (which acts
// upon selectors) into a set of concrete map entries based on the
// SelectorCache. These can subsequently be plumbed into the datapath.
//
// Must be performed while holding the Repository lock.
// PolicyOwner (aka Endpoint) is also locked during this call.
func (p *selectorPolicy) DistillPolicy(policyOwner PolicyOwner, npMap NamedPortsMap, isHost bool) *EndpointPolicy {
	calculatedPolicy := &EndpointPolicy{
		selectorPolicy: p,
		NamedPortsMap:  npMap,
		PolicyMapState: make(MapState),
		PolicyOwner:    policyOwner,
	}

	if !p.IngressPolicyEnabled || !p.EgressPolicyEnabled {
		calculatedPolicy.PolicyMapState.AllowAllIdentities(
			!p.IngressPolicyEnabled, !p.EgressPolicyEnabled)
	}

	// Register the new EndpointPolicy as a receiver of delta
	// updates.  Any updates happening after this, but before
	// computeDesiredL4PolicyMapEntries() call finishes may
	// already be applied to the PolicyMapState, specifically:
	//
	// - policyMapChanges may contain an addition of an entry that
	//   is already added to the PolicyMapState
	//
	// - policyMapChanges may contain a deletion of an entry that
	//   has already been deleted from PolicyMapState
	p.insertUser(calculatedPolicy)

	// Must come after the 'insertUser()' above to guarantee
	// PolicyMapChanges will contain all changes that are applied
	// after the computation of PolicyMapState has started.
	calculatedPolicy.computeDesiredL4PolicyMapEntries()
	if !isHost {
		calculatedPolicy.PolicyMapState.DetermineAllowLocalhostIngress(p.L4Policy)
	}

	return calculatedPolicy
}
```

# 5 Policy Distill

pkg/policy/resolve.go:

```go
// selectorPolicy is a structure which contains the resolved policy for a
// particular Identity across all layers (L3, L4, and L7), with the policy
// still determined in terms of EndpointSelectors.
type selectorPolicy struct {
	Revision uint64 // Revision is the revision of the policy repository used to generate this selectorPolicy.

	SelectorCache *SelectorCache // SelectorCache managing selectors in L4Policy
	L4Policy *L4Policy           // L4Policy contains the computed L4 and L7 policy.
	CIDRPolicy *CIDRPolicy       // CIDRPolicy contains the L3 (not L4) CIDR-based policy.

	IngressPolicyEnabled bool
	EgressPolicyEnabled bool
}
```

* `CIDRPolicy` is **pure L3 policy**, does not include L4 policy.
* `L4Policy` is **L4/L7 policy**, note that it contains L7 policy.

```go
// EndpointPolicy is a structure which contains the resolved policy across all
// layers (L3, L4, and L7), distilled against a set of identities.
type EndpointPolicy struct {
	// all Endpoints sharing the same identity will be referring to a shared selectorPolicy!
	*selectorPolicy

	// maps PortNames in L4Filters to port numbers. This mapping is endpoint specific.
	NamedPortsMap NamedPortsMap

	// PolicyMapState contains the state of this policy as it relates to the datapath.
	// Maps Key -> proxy port if redirection is needed. Proxy port 0 indicates no proxy redirection.
	// All fields within the Key and the proxy port must be in host byte-order.
	PolicyMapState MapState

	policyMapChanges MapChanges // pending changes to the PolicyMapState
	PolicyOwner PolicyOwner     // describes any type which consumes this EndpointPolicy object.
}
```

```
regeneratePolicy  // pkg/endpoint/policy.go
  Consume(GetNamedPorts())
  DistillPolicy(NamedPortsMap)

computeDirectionL4PolicyMapEntries   // pkg/policy/resolve.go
  |-ToMapState                       // pkg/policy/l4.go
      |-NewMapStateEntry
```

# 6 Skip duplicated labels

When there are duplicated label selectors in the rule, such as,

```yaml
apiVersion: cilium.io/v2
kind: CiliumClusterwideNetworkPolicy
spec:
  endpointSelector:
    matchLabels:
      k8s:redis-cluster-name: my-test-cluster
  ingress:
  - fromEndpoints:
    - matchLabels:
        k8s:appid: "0001"
    - matchLabels:
        k8s:appid: "0001"  # duplicated from the adjacent above label selectors
    - matchLabels:
        k8s:appid: "0002"
    toPorts:
    - ports:
      - port: "6379"
        protocol: TCP
```

cilium-agent will skip the duplicated ones:

```
       mergeIngress                                    //    pkg/policy/rule.go
        |-mergeIngressPortProto         // L3-only rule
        |-toPorts.Iterate(func(ports) { // L4/L7 rule
            for p := range GetPortProtocols() {
              mergeIngressPortProto(TCP/UDP/...)
                |-createL4IngressFilter
                |-addL4Filter(ruleLabels)
                    |-mergePortProto
                    |-if ruleLabels in existingRuleLabels
                    |    exists = true
                    |-if !exists:
                    |    existingFilter.DerivedFromRules.append(ruleLabels)
            }
```

Only unique rule labels will be included to the final rule, so the final effect is
equivalent to this:

```yaml
apiVersion: cilium.io/v2
kind: CiliumClusterwideNetworkPolicy
spec:
  endpointSelector:
    matchLabels:
      k8s:redis-cluster-name: my-test-cluster
  ingress:
  - fromEndpoints:
    - matchLabels:
        k8s:appid: "0001"
    - matchLabels:
        k8s:appid: "0002"
    toPorts:
    - ports:
      - port: "6379"
        protocol: TCP
```
