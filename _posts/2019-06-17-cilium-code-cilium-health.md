---
layout    : post
title     : "Cilium Code Walk Through: cilium-health"
date      : 2019-05-19
lastupdate: 2019-05-19
categories: cilium
---

----

This post walks you through the cilium-health design and implementation. Code
based on Cilium `1.5.0`.

### The Cilium Health Design

```shell
$ ps -ef
UID   PID  PPID       TIME CMD
root    1     0   00:00:29 cilium-agent --kvstore=etcd --kvstore-opt=etcd.config=xxx --config-dir=/tmp/cilium/config-map
root  425     1   00:00:09 cilium-health -d
root  328   300   00:00:01 cilium-health -d --admin=unix --passive --pidfile /var/run/cilium/state/health-endpoint.pid
```

1. Each node will run two `cilium-health` daemons, as shown above
1. The first `cilium-health` daemon (with PPID `1`) **periodically checks the
   Cilium agent API status**
1. The second `cilium-health` daemon **acts as a server**, and also a Cilium
   endpoint, used for **collecting reachability and latency info from Cilium
   nodes**, e.g. ICMP, HTTP reachabilities
1. There is also a CLI tool, also named `cilium-health`. E.g. run `$
   cilium-health status` will trigger `cilium-health` server to probe all nodes,
   update and print the detailed info

### Call Flows

```shell
runDaemon                                       // daemon/daemon_main.go
  |-NewDaemon                                   // daemon/daemon.go
  |-initHealth                                  // daemon/health.go
    |-d.ciliumHealth.Run                        // cilium-health/launch/launcher.go
    | |-Restapi.GetHello (client side)          // api/v1/health/client/restapi/restapi_client.go
    |   ||
    |   ||
    |   NewGetHello (server side)               // api/v1/health/server/restapi/get_hello.go
    |     |-Handle                              // pkg/health/server/hello.go
    |       |-NewGetHelloOK                     // api/v1/health/client/restapi/get_hello_responses.go
    |-LaunchAsEndpoint                          // cilium-health/launch/endpoint.go
      |-Run("cilium-health -d --admin=unix ..")
        |-Serve (http server)                   // pkg/health/server/server.go
```

## 1 Daemon Init

From Cilium agent restarting.

CLI `cilium-agent <options>` calls `runDaemon`. `runDaemon` calls
`NewDaemon` to create the daemon process.

## 2 Init Health

`daemon/health.go`:

```go
func (d *Daemon) initHealth() {
	// Launch cilium-health in the same namespace as cilium.
	d.ciliumHealth = &health.CiliumHealth{}
	go d.ciliumHealth.Run()

	// Launch another cilium-health as an endpoint, managed by cilium.
	var client *health.Client
	controller.NewManager().UpdateController("cilium-health-ep",
		controller.ControllerParams{
			DoFunc: func(ctx context.Context) error {
				// On the first initialization, or on error, restart the health EP.
				d.cleanupHealthEndpoint()
				client = health.LaunchAsEndpoint(d, &d.nodeDiscovery.LocalNode, d.mtuConfig)
			},
			StopFunc: func(ctx context.Context) error {
				client.PingEndpoint()
				d.cleanupHealthEndpoint()
			},
			RunInterval: 30 * time.Second,
		},
	)
}
```

## 3 1st `cilium-health`: Monitor Cilium API

`d.ciliumHealth` is a `*CiliumHealth` type instance:

`cilium-health/launch/launcher.go`:

```go
// CiliumHealth is used to wrap the node executable binary.
type CiliumHealth struct {
	launcher.Launcher
	client *healthPkg.Client
	status *models.Status
}
```

`Run()` method defined in the same file:

```go
// Run launches the cilium-health daemon.
func (ch *CiliumHealth) Run() {
	ch.SetArgs([]string{"-d"})

	// Wait until Cilium API is available
	for {
		if _, err = cli.Daemon.GetHealthz(nil); err == nil
			break
		time.Sleep(connectRetryInterval)
	}

	for {
		ch.Launcher.Run() // `$ cilium-health -d`
		for {
			status := &models.Status{ State: models.StatusStateOk }
			if _, err := ch.client.Restapi.GetHello(nil); err != nil {
				status.State = models.StatusStateWarning
			}
			ch.setStatus(status)
			time.Sleep(statusProbeInterval)
		}
	}
}
```

The method starts a `cilium-health` process in the same namespace as
`cilium-agent`, and takes only one parameter `-d`. This is just the process
`425`, whose parent process is `1` (`cilium-agent`).

## 4 2nd `cilium-health`: Act as Endpoint

`cilium-health/launch/endpoint.go`

```go
// LaunchAsEndpoint launches the cilium-health agent in a nested network
// namespace and attaches it to Cilium the same way as any other endpoint,
// but with special reserved labels.
func LaunchAsEndpoint(owner endpoint.Owner, n *node.Node, mtuConfig mtu.Configuration) (*Client, error) {
	cmd  = launcher.Launcher{}
	healthIP = n.IPv4HealthIP
	ip4Address = ip4WithMask.String()

	// setup link: veth pair or IPVLAN
	connector.SetupVethWithNames(vethName, epIfaceName, MTU, info)

	// start clium-health process
	cmd.Run(option.Config.BpfDir/spawn_netns.sh, "-d --admin=unix --passive --pidfile xx")

	// Create the endpoint
	ep, err := endpoint.NewEndpointFromChangeModel(info)
	ep.UpdateLabels(context.Background(), owner, labels.LabelHealth, nil, true) // Give the endpoint a security identity

	// Set up the endpoint routes
	configureHealthRouting(ContainerName, epIfaceName, hostAddressing, mtu)
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

This `cilium-health` process serves as the server side of `cilium-health` CLIs.
E.g. when you issue a command:

```shell
$ cilium-health ping
Probe time:   2019-05-19T09:52:32Z
Nodes:
  node-1 (localhost):
    Host connectivity to 192.168.6.9:
      ICMP to stack:   OK, RTT=186.95µs
      HTTP to agent:   OK, RTT=527.359µs
    Endpoint connectivity to 192.168.6.9:
      ICMP to stack:   OK, RTT=154.675µs
      HTTP to agent:   OK, RTT=456.136µs
  node-2:
    ...
```

The request is handled by this process. 

### Server Side Implementation

`pkg/health/server/server.go`:

```go
// Serve spins up the following goroutines:
// * TCP API Server: Responders to the health API "/hello" message, one per path
//
// Also, if "Passive" is not set in s.Config:
// * Prober: Periodically run pings across the cluster at a configured interval
//   and update the server's connectivity status cache.
// * Unix API Server: Handle all health API requests over a unix socket.
func (s *Server) Serve() (err error) {
	errors := make(chan error)

	for i := range s.tcpServers {
		srv := s.tcpServers[i]
		go func() {
			errors <- srv.Serve()
		}()
	}

	if !s.Config.Passive {
		go func() {
			errors <- s.runActiveServices()
		}()
	}

	// Block for the first error, then return.
	err = <-errors
	return err
}
```
