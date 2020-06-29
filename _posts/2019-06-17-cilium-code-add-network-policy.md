---
layout    : post
title     : "Cilium Code Walk Through: Add Network Policy"
date      : 2020-07-03
lastupdate: 2020-07-03
categories: cilium
---

This post walks you through the network policy enforcement process. Code based
on Cilium `1.8.0`.

Call flows：

```
policyAdd                                                  // daemon/policy.go
  |-TriggerPolicyUpdates                                   // daemon/policy.go
      |-TriggerWithReason                                  // pkg/trigger/trigger.go
          |- t.wakeupChan <- true                          // pkg/trigger/trigger.go
                /\                                        
                ||                                        
                \/                                        
        func (t *Trigger) waiter()                         // pkg/trigger/trigger.go
		  t.params.TriggerFunc(reasons)                   
                   /                                      
                  /                                       
   policyUpdateTrigger                                     // daemon/policy.go
     |-RegenerateAllEndpoints                              // pkg/endpointmanager/manager.go
         for ep in eps:                                  
           RegenerateIfAlive                               // pkg/endpoint/policy.go
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

And the `Handle` logic:

```
EndpointRegenerationEvent.Handle                                              //    pkg/endpoint/events.go
  |-regenerate                                                                //    pkg/endpoint/policy.go
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
       |  |  |     |           |-mergeIngressPortProto                        //    pkg/policy/rule.go
       |  |  |     |              |-createL4IngressFilter                     // -> pkg/policy/l4.go
       |  |  |     |              |-mergePortProto                            //    pkg/policy/rule.go
       |  |  |     |-cip.setPolicy(selPolicy)                                 //    pkg/policy/distillery.go
       |  |  |-e.selectorPolicy.Consume                                       //    pkg/policy/distillery.go
       |  |       |-DistillPolicy                                             //    pkg/policy/resolve.go
       |  |          |-computeDesiredL4PolicyMapEntries                       //    pkg/policy/resolve.go
       |  |             |-computeDirectionL4PolicyMapEntries                  //    pkg/policy/resolve.go
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
```

# addNewRedirects

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

# kafka

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

# policy calc

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
