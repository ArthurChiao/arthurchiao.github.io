---
layout    : post
title     : "Cilium Code Walk Through: Agent CIDR Init"
date      : 2019-05-13
lastupdate: 2019-05-13
categories: cilium
---

This post walks you through the CIDR selection process during Cilium agent
initialization. Code based on Cilium `1.5.0`.

Cilium agent also acts as the local IPAM, responsible for IP allocations within
the node, so it needs a CIDR. There are several ways to specify the CIDR that
cilium agent should use:

1. via **k8s controller manager** [1]: `--allocate-node-cidrs <CIDR>` specify a
   large CIDR, and each cilium agent will request a small CIDR from it upon
   start up
1. via **k8s annotations**: add `io.cilium.network.ipv4-pod-cidr=<CIDR>` to the
   specific node
1. other ways, e.g. Cilium CLI?

Overall steps of Cilium agent CIDR initialization:

1. Spawn Cilium agent (daemon), starting initialization
1. Init Cilium `pkt/k8s` submodule
1. Get CIDR info from k8s controller manager and node annotations
1. Select the CIDR this node should use according to specific priorities
1. Write CIDR info back to k8s annotations

Call flows：

```shell
runDaemon                                       // daemon/daemon_main.go
  |-NewDaemon                                   // daemon/daemon.go
    |-k8s.Configure                             // pkg/k8s/config.go
    |-k8s.Init                                  // pkg/k8s/init.go
    | |-waitForNodeInformation 
    | | |-retrieveNodeInformation 
    | |   |-GetNode                             // pkg/k8s/node.go
    | |     |-c.CoreV1().Nodes().Get(nodeName)
    | |   |-ParseNode                           // pkg/k8s/node.go
    | |     |- cidr.ParseCIDR
    | |-useNodeCIDR                             // pkg/k8s/init.go
    |-k8s.Client().AnnotateNode(cidr ...)       // daemon/daemon.go
      |-updateNodeAnnotation                    // pkg/k8s/annotate.go
        |-c.CoreV1().Nodes().Update(node)
```

## 1 Daemon Start

Start from `daemon/daemon_main.go`:

When Cilium CLI `cilium-agent <options>` is executed, function `runDaemon()`
will be called, and it will further call `NewDaemon()` (`daemon/daemon.go`)
to spawn the daemon process:

```go
import (
	"github.com/cilium/cilium/pkg/k8s"
)

// NewDaemon creates and returns a new Daemon with the parameters set in c.
func NewDaemon(dp datapath.Datapath) (*Daemon, *endpointRestoreState, error) {
	...
	k8s.Configure(option.Config.K8sAPIServer, option.Config.K8sKubeConfigPath)
	...

	k8s.Init()

	// Annotation of the k8s node must happen after discovery of the
	// PodCIDR range and allocation of the health IPs.
	k8s.Client().AnnotateNode(node.GetName(), node.GetIPv4AllocRange() ...)
}
```

As the above code shown, `NewDaemon` will init the `k8s`
(`pkg/k8s/`) module and call some module functions:

1. `k8s.Configure` will configure the K8S cluster info, e.g. `apiserver` address
1. `k8s.Init` will connect the K8S cluster and get the node info, which including
   the CIDR info; then it will **decide the CIDR this node will use**
1. `k8s.Client` will **update node info back to K8S annotations**, among which
   includes the CIDR info

## 2 Configure `pkg/k8s/`

`pkg/k8s/config.go`

```go
func Configure(apiServer, kubeconfigPath string) {
        config.APIServer = apiServer
        config.KubeconfigPath = kubeconfigPath

        if IsEnabled() &&
                config.APIServer != "" &&
                !strings.HasPrefix(apiServer, "http") {
                config.APIServer = "http://" + apiServer
        }
}
```

## 3 Init `pkg/k8s/`

```go
// Init initializes the Kubernetes package. It is required to call Configure()
// beforehand.
func Init() error {
	if nodeName := os.Getenv(EnvNodeNameSpec); nodeName != "" {
		if n := waitForNodeInformation(nodeName); n != nil {
			nodeIP4 := n.GetNodeIP(false)
			Info("Received own node information from API server")

			useNodeCIDR(n)

		} else {
			// if node resource could not be received, fail if
			// PodCIDR requirement has been requested
			if option.Config.K8sRequireIPv4PodCIDR || option.Config.K8sRequireIPv6PodCIDR
				log.Fatal("Unable to derive PodCIDR from Kubernetes node resource, giving up")
		}

		// Annotate addresses will occur later since the user might
		// want to specify them manually
	} else if option.Config.K8sRequireIPv4PodCIDR || option.Config.K8sRequireIPv6PodCIDR
		return fmt.Errorf("node name must be specified via environment variable
                '%s' to retrieve Kubernetes PodCIDR range", EnvNodeNameSpec)

	return nil
}
```

Note the following comment lines:

```shell
// Annotate addresses will occur later since the user might
// want to specify them manually
```

### 3.1 `waitForNodeInformation`

This function will try to get node info via K8S API, will auto-try until it
succeeds (`pkg/k8s/init.go`). Call flow:

`waitForNodeInformation -> retrieveNodeInformation -> GetNode()`

`GetNode` will call K8S API to retrieve node info, including the CIDR (`pkg/k8s/node.go`):

`GetNode -> c.CoreV1().Nodes().Get(nodeName, metav1.GetOptions{})`

The function returns a `*v1.Node` instance.

### 3.2 `ParseNode`

This function will parse the returned node info.

```go
// ParseNode parses a kubernetes node to a cilium node
func ParseNode(k8sNode *types.Node, source node.Source) *node.Node {
	newNode := &node.Node{
		Name:        k8sNode.Name,
	}

	if len(k8sNode.SpecPodCIDR) != 0
		if allocCIDR, err := cidr.ParseCIDR(k8sNode.SpecPodCIDR); err != nil
			scopedLog.WithError(err).Warn("Invalid PodCIDR value for node")
		else
			if allocCIDR.IP.To4() != nil
				newNode.IPv4AllocCIDR = allocCIDR
			else
				newNode.IPv6AllocCIDR = allocCIDR

	// Spec.PodCIDR takes precedence since it's
	// the CIDR assigned by k8s controller manager
	// In case it's invalid or empty then we fall back to our annotations.
	if newNode.IPv4AllocCIDR == nil {
		if ipv4CIDR, ok := k8sNode.Annotations[annotation.V4CIDRName]; !ok {
			scopedLog.Debug("Empty IPv4 CIDR annotation in node")
		else
			allocCIDR, err := cidr.ParseCIDR(ipv4CIDR)
			if err != nil {
				scopedLog.WithError(err).Error("BUG, invalid IPv4 annotation CIDR in node")
			else
				newNode.IPv4AllocCIDR = allocCIDR

	return newNode
}
```

Note the **CIDR priority** here:

1. If SpecPodCIDR is found, use it; this info is provided by **k8s controller
   manager**
1. If SpecPodCIDR not found, try to retrieve CIDR from **node annotations**

That is to say: **CIDR configured via controller manager has higher priority
than that via node annotations**.

### 3.3 useNodeCIDR(n)

```go
// useNodeCIDR sets the ipv4-range and ipv6-range values values from the
// addresses defined in the given node.
func useNodeCIDR(n *node.Node) {
	node.SetIPv4AllocRange(n.IPv4AllocCIDR)
}
```

`pkg/node/node_address.go`

```go
// SetIPv4AllocRange sets the IPv4 address pool to use when allocating
// addresses for local endpoints
func SetIPv4AllocRange(net *cidr.CIDR) {
	ipv4AllocRange = net
}
```

## 4 k8s.Client().AnnotateNode(cidr ...)

`daemon/daemon.go`

Write node info back to node annotations, among which including the CIDR info.

## 5 Specify Node CIDR via K8S Node Annotations

Steps:

1. On K8S master: remove `--allocate-node-cidrs` configuration from controller manager
2. On K8S mater: annotate node: `kubectl annotate node <NODE_NAME> --overwrite io.cilium.network.ipv4-pod-cidr=<CIDR>`
3. On K8S node: restart Cilium agent: `docker restart <Container ID>`

Check it works as expected:

1. On k8s master: `kubectl describe node <NODE_NAME> | grep cilium`
   `io.cilium.network.ipv4-pod-cidr` should be the specified CIDR
1. On k8s node: `ifconfig cilium_host`: the IP address (CIDR Gateway) should be
   allocated from the specified CIDR

## References

1. [Cilium Doc: Enable automatic node CIDR allocation](http://docs.cilium.io/en/v1.5/kubernetes/requirements/?highlight=controller%20manager#enable-automatic-node-cidr-allocation-recommended)
