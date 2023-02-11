---
layout    : post
title     : "Trip.com: Large Scale Cloud Native Networking & Security with Cilium/eBPF (eBPFSummit, 2022)"
date      : 2022-09-28
lastupdate: 2022-12-11
author    : ArthurChiao
categories: cilium bpf trip.com
---

This is an entended version of my talk at [**eBPF Summit 2022**](https://ebpf.io/summit-2022/):
***<mark>Large scale cloud native networking and security with Cilium/eBPF: 4 years production experiences from Trip.com</mark>***.
This version covers more contents and details that's missing from the talk (for time limitation).

----

* TOC
{:toc}

----

# Abstract

In Trip.com, we rolled out our first Cilium node (bare metal) into production
in 2019. Since then, almost all our Kubernetes clusters - both on-premises
baremetal and self-managed clusters in public clouds - have switched to Cilium.

With ~10K nodes, ~300K pods running on Kubernetes now,
Cilium powers our business critical services such as hotel
search engines, financial/payment services, in-memory databases, data store
services, which cover a wide range of requirements in terms of latency,
throughput, etc.

From our 4 years of experiences, the audience will learn cloud native networking and security with Cilium including:

1. How to use CiliumNetworkPolicy for L3/L4 access control including extending the security model to BM/VM instances;
2. Our new multi-cluster solution called KVStoreMesh as an alternative to ClusterMesh and how we made it compatible with the community for easy upgrade;
3. Building stability at scale, like managing control plane and multi-cluster outages, and the improvements we made to Cilium as a result.

# 1 Cloud Infrastructure at Trip.com

## 1.1 Layered architecture

The Trip.com cloud team is responsible for the corporation's infrastructure over the globe, as shown below:

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/trip-cloud-infra.png" width="90%" height="90%"></p>

* In the bottom we have data centers and several public cloud vendors;
* Above the bottom is our orchestration systems for BM, VM and container;
* One layer up is the internally developed continues delivery platform (CI/CD);
* At the top are our business services and corresponding middlewares;
* In the vertical direction, we have security & management tools at different levels.

The cloud scope is the box shown in the picture.

## 1.2 More details

More specific information about our infra:

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/cloud-infra-details.png" width="100%" height="100%"></p>

* Most of our workloads now run on Kubernetes, we have 3 big
  clusters and several small ones, with total `~10k` nodes and `~300k` pods;
* Most Kubernetes nodes are **<mark>blade servers</mark>**; which
* Run with internally maintained **<mark><code>4.19/5.10</code></mark>** kernels;
* And for inter-host networking, we use BGP for on-premises clusters and ENI
  for self-managed clusters on the cloud.

# 2 Cilium at Trip.com

## 2.1 Timeline of rolling out

This is a simple timeline of our rolling out process:

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/trip-cilium-timeline.png" width="100%" height="100%"></p>

1. We started investigating cloud-native networking solution in 2018 [9], and of course, Cilium won out;
2. Our first cilium node rolled into production in 2019 [10];
3. Since then, all kinds of our business and infrastructure services began to migrate to Cilium, transparently.

In 2021, with most online businesses already in Cilium,
we started a security project based on Cilium network policy [8].

Functionalities we’re using:

* Service LoadBalancing (eBPF/XDP)
* CiliumNetworkPolicy (CNP)
* eBPF host routing
* eBPF bandwidth manager
* Hubble (part of)
* Rsyslog driver
* Performance boost options like sockops, eBPF redirects
* ...

## 2.2 Customizations

First, two Kubernetes clusters shown here,

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/customization-1.png" width="90%" height="90%"></p>

Some of our customizations based on the above topology:

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/customization-2.png" width="100%" height="100%"></p>

1. Use docker-compose to deploy cilium to remove kubernetes dependency [1];
2. Assign each agent a dedicated certificate for Kubernetes authentication, instead of the shared seviceaccount by all agents;
3. We’ve helped to maturate Cilium’s rsyslog driver, and have sent all agent logs to ClickHouse for trouble shooting;
4. Few patches added to facilitate business migration, but this is not that general, so we didn’t upstream them;
5. Use BIRD as BGP agent instead of the suggested kube-router then, and we've contributed a BGP+Cilium guide to Cilium documentation;
6. We developed a new multi-cluster solution called KVStoreMesh [4]. More on this lader.

## 2.3 Optimizations & tunings

Now the optimization and tuning parts.

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/optimization-1.png" width="90%" height="90%"></p>
<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/optimization-2.png" width="90%" height="90%"></p>

### 2.3.1 Decouple installation

As mentioned just now, the first thing we’ve done is decoupling Cilium
deploying/installation from Kubernetes: no daemonset, no configmap.  All the
configurations needed by the agent are on the node.

This makes agents suffer less from Kubernetes outages, but more importantly,
**<mark>each agent is now completely independent in terms of configuration and upgrade</mark>**.

### 2.3.2 Avoid retry/restart storms

The second consideration is to avoid retry storms and burst starting,
as requests will surge by two orders of magnitude or even higher
when outage occurs, which could easily crash or stuck central components
like Kubernetes apiserver/etcd and kvstore.

We use an internally developed restart backoff (jitter+backoff) mechanism to avoid such cases.
the **<mark>jitter window is calculated according to cluster scale</mark>**.
Such as,

* For a cluster with 1000 nodes, the jitter window may be 20 minutes, during which period
  **<mark>each agent is allowed to start one and only one time</mark>**, then backed off.
* For a cluster with 5000 nodes, the jitter window may be 60 minutes.
* The backoff mechanism is **<mark>implemented as a bash script</mark>** (all states are saved in a local file), used
  in docker-compose as a "pre-start hook", **<mark>Cilium code suffers no changes</mark>**.

Besides, we’ve assigned each agent a distinct certificate (each agent has a
dedicated username but belongs to a common user group), which enables
Kubernetes to perform rate limiting on Cilium agents with [APF](https://kubernetes.io/docs/concepts/cluster-administration/flow-control/)
(API Priority and Fairness). **<mark>No Cilium code changes</mark>** to achieve this, too.

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/per-agent-cert.png" width="95%" height="95%"></p>

Refer to our previous blogs if you'd like to learn more about Kubernetes AuthN/AuthZ models [2,3].

### 2.3.3 Stability first

Trip.com provides online booking services worldwide with 7x24h, so at any time
of any day, business service down would lead to instantaneous losses to the
company. So, we
**<mark>can’t risk letting foundational services like networking to restart itself by the simple “fast failure”</mark>**
rule, but favor necessary human interventions and decisions.

When failure arises, we'd like services such as Cilium to be more patient, just
wait there and keep the current business uninterrupted, letting system developers and
maintainers decide what to do; **<mark>fast failure and automatic retries
make things worse</mark>** more than often in such cases.

Some specific options (with exemplanary configurations) to tune this behavior:

* `--allocator-list-timeout=3h`
* `--kvstore-lease-ttl=86400s`
* `--k8s-syn-timeout=600s`
* `--k8s-heartbeat-timeout=60s`

Refer to the Cilium documentation or source code to figure out what each option
exactly means, and customize them according to your needs.

### 2.3.4 Planning for scale

Depending on your cluster scale, certain stuffs needs to be planned in advance.
For example, **<mark>identity relevant labels</mark>** (`--labels`) directly determine
the **<mark>maximum pods in your cluster</mark>**: a group of labels map to
one identity in Cilium, so in it's design, all pods with the same labels share the same identity.
But, if your `--labels=<labels>` is too fine grained (which is unfortunately the default
case), it may result in each pod has a distinct identity in the worse case, then your cluster scale is
upper bound by **<mark><code>64K</code></mark>** pods, as identity is represented
with a `16bit` integer. Refer to [8] for more information.

Besides, there are parameters that needs to be decided or tuned according to
your workload throughput, such as identity allocation mode, connection tracking table.

Options:

* `--cluster-id`/`--cluster-name`: avoid identity conflicts in multi-cluster scenarios;
* `--labels=<labellist>` identity relavent labels
* `--identity-allocation-mode` and kvstore benchmarking if kvstore mode is used

    We use `kvstore` mode, and **<mark>running kvstore (cilium etcd) on dedicated blade servers</mark>** for large clusters.

* `--bpf-ct-*`
* `--api-rate-limit`
* Monitor aggregation options for reducing volume of observability data

### 2.3.5 Performance tuning

Cilium includes many high performance options such as sockops and BPF host
routing, and of course, all those features needs specific kernel versions
support.

* `--socops-enable`
* `--bpf-lb-sock-hostns-only`
* ...

Besides, **<mark>disable some debug level options</mark>** are also necessary:

* `--disable-cnp-status-updates`
* ...

### 2.3.6 Observability & alerting

The last aspect we'd like to talk about is observability.

* Metric
* Logging
* Tracing

Apart from the **<mark>metrics data</mark>** from Cilium agent/operator, we
also collected all agent/operator logs (**<mark>logging data</mark>**) and sent
to ClickHouse for analyzing, so, we could alert on abnormal metrics as well as
error/warning logs,

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/cilium-logs-alerting.png" width="100%" height="100%"></p>

Besides, tracing can be helpful too, more on this later.

### 2.3.7 Misc options

* `--enable-hubble`
* `--keep-config`
* `--log-drivers`
* `--policy-audit-mode`

## 2.4 Multi-cluster solution

Now let’s have a look at the multi-cluster problem.

For historical reasons, our businesses are deployed across different
data centers and Kubernetes clusters.
So there are inter-cluster communications without L4/L7 border gateways.
This is a problem for access control, as Cilium **<mark>identity is a cluster scope object</mark>**.

### 2.4.1 ClusterMesh

The community solution to this problem is ClusterMesh as shown here,

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/clustermesh.png" width="90%" height="90%"></p>

ClusterMesh requires each agent to connect to each kvstore in all clusters,
effectively resulting in a peer-to-peer mesh.  While this solution is straight
forward, it suffers stability and scalability issues, especially for large
clusters.

In short, when a single cluster down, the failure would soon propagate to all
the other clusters in-the-mesh,
And eventually all the clusters may crash at the same time, as illustrated below:

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/clustermesh-problems.png" width="90%" height="90%"></p>

Essentially, this is because clusters in ClusterMesh are too tightly coupled.

### 2.4.2 KVStoreMesh

Our solution to this problem is very simple in concept:
pull metadata from all the remote kvstores, and push to local one after filtering.

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/kvstoremesh-2-clusters.png" width="90%" height="90%"></p>

The three-cluster case show this concept more clearly: only kvstores are involved,

<p align="center"><img src="/assets/img/trip-ebpf-summit-2022/kvstoremesh-3-clusters.png" width="80%" height="80%"></p>

In ClusterMesh, agents fetch remote metadata from remote kvstores; in KVStoreMesh, they get from the local one.

Thanks to cilium’s good design, this only needs
**<mark>a few improvements and/or bugfixes to the agent and operator</mark>** [4], and we’ve already
upstreamed some of them. A **<mark>kvstoremesh-operator</mark>**
is newly introduced, and maintained internally currently;
we’ll devote more efforts to upstream it in the next, too.

Besides, we’ve also developed a simple solution to let Cilium
be-aware of our legacy workloads like virtual machines in OpenStack,
the-solultion is called CiliumExeternalResource. Please see our previous
blog [8] if you’re interested in.

# 3 Advanced trouble shooting skills

Now let’s back to some handy stuffs.

The fist one, debugging.

## 3.1 Debugging with `delve/dlv`

Delve is a good friend, and our docker-compose way makes **<mark>debugging more easier</mark>**,
as each agent independently deployed, commands can be
executed on the node to start/stop/reconfigure the agent.

```shell
# Start cilium-agent agent container with entrypoint `sleep 10d`, then enter the container
(node) $ docker exec -it cilium-agent bash

(cilium-agent ctn) $ dlv exec /usr/bin/cilium-agent -- --config-dir=/tmp/cilium/config-map
Type 'help' for list of commands.
(dlv)

(dlv) break github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerateBPF
Breakpoint 3 set at 0x1e84a3b for github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerateBPF() /go/src/github.com/cilium/cilium/pkg/endpoint/bpf.go:591
(dlv) break github.com/cilium/cilium/pkg/endpoint/bpf.go:1387
Breakpoint 4 set at 0x1e8c27b for github.com/cilium/cilium/pkg/endpoint.(*Endpoint).syncPolicyMapWithDump() /go/src/github.com/cilium/cilium/pkg/endpoint/bpf.go:1387
(dlv) continue
...

(dlv) clear 1
Breakpoint 1 cleared at 0x1e84a3b for github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerateBPF() /go/src/github.com/cilium/cilium/pkg/endpoint/bpf.go:591
(dlv) clear 2
Breakpoint 2 cleared at 0x1e8c27b for github.com/cilium/cilium/pkg/endpoint.(*Endpoint).syncPolicyMapWithDump() /go/src/github.com/cilium/cilium/pkg/endpoint/bpf.go:1387
```

We’ve tracked down several bugs in this way.

## 3.2 Tracing with `bpftrace`

Another useful tool is bpftrace for live tracing.

But note that there are some differences for **<mark>tracing container processes</mark>**.
You need to find the PID namespace or absolute path of the cilium-agent binary on the node.

### 3.2.1 With absolute path

{% raw%}
```shell
# Check cilium-agent container
$ docker ps | grep cilium-agent
0eb2e76384b3        cilium:20220516   "/usr/bin/cilium-agent ..."   4 hours ago    Up 4 hours   cilium-agent

# Find the merged path for cilium-agent container
$ docker inspect --format "{{.GraphDriver.Data.MergedDir}}" 0eb2e76384b3
/var/lib/docker/overlay2/0a26c6/merged # 0a26c6.. is shortened for better viewing
# The object file we are going to trace
$ ls -ahl /var/lib/docker/overlay2/0a26c6/merged/usr/bin/cilium-agent
/var/lib/docker/overlay2/0a26c6/merged/usr/bin/cilium-agent # absolute path

# Or you can find it bruteforcelly if there are no performance (e.g. IO spikes) concerns:
$ find /var/lib/docker/overlay2/ -name cilium-agent
/var/lib/docker/overlay2/0a26c6/merged/usr/bin/cilium-agent # absolute path
```
{% endraw%}

Anyway, after located the target file and checked out the symbols in it,

```shell
$ nm /var/lib/docker/overlay2/0a26c6/merged/usr/bin/cilium-agent
0000000001d3e940 T type..hash.github.com/cilium/cilium/pkg/k8s.ServiceID
0000000001f32300 T type..hash.github.com/cilium/cilium/pkg/node/types.Identity
0000000001d05620 T type..hash.github.com/cilium/cilium/pkg/policy/api.FQDNSelector
0000000001d05e80 T type..hash.github.com/cilium/cilium/pkg/policy.PortProto
...
```

you can initiate userspace probes like below, printing things you’d like to see:

```shell
$ bpftrace -e \
  'uprobe:/var/lib/docker/overlay2/0a26c6/merged/usr/bin/cilium-agent:"github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerateBPF" {printf("%s\n", ustack);}'
Attaching 1 probe...

        github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerateBPF+0
        github.com/cilium/cilium/pkg/endpoint.(*EndpointRegenerationEvent).Handle+1180
        github.com/cilium/cilium/pkg/eventqueue.(*EventQueue).run.func1+363
        sync.(*Once).doSlow+236
        github.com/cilium/cilium/pkg/eventqueue.(*EventQueue).run+101
        runtime.goexit+1
```

### 2.3.2 With PID `/proc/<PID>`

A more convenient and conciser way is to find the PID namespace and passing it to
`bpftrace`, this will make the commands much shorter:

{% raw%}
```shell
$ sudo docker inspect -f '{{.State.Pid}}' cilium-agent
109997
$ bpftrace -e 'uprobe:/proc/109997/root/usr/bin/cilium-agent:"github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerate" {printf("%s\n", ustack); }'
```
{% endraw%}

Or,

```shell
$ bpftrace -p 109997 -e 'uprobe:/usr/bin/cilium-agent:"github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerate" {printf("%s\n", ustack); }'
```

## 3.3 Manipulate BPF map with `bpftool`

Now consider a specific question:
**<mark>how could you determine if a CNP actually takes effect?</mark>**
There are several ways:

```shell
$ kubectl get cnp -n <ns> <cnp> -o yaml       # spec & status in k8s
$ cilium endpoint get <ep id>                 # spec & status in cilium userspace
$ cilium bpf policy get <ep id>               # summary of kernel bpf policy status
```

* Query Kubernetes? NO, that’s too high-level;
* Check out endpoint status? NO, it's a userspace state and still too high-level;
* Check bpf policy with cilium command? Well, it’s indeed a summary
  of the BPF policies in use, but the summary code itself may also have bugs;

The most underlying policy state is the **<mark>BPF policy map in kernel</mark>**, we can view it with bpftool:

```shell
$ bpftool map dump pinned cilium_policy_00794 # REAL & ULTIMATE policies in the kernel!
```

But to use this tool you need to first **<mark>get yourself familiar with some Cilium data structures</mark>**.
Such as, how an IP address corresponds to an identity, and how to combine
Identity, port, protol, traffic direction to form a key in BPF policy map.

```shell
# Get the corresponding identity of an (client) IP address
$ cilium bpf ipcache get 10.2.6.113
10.2.6.113 maps to identity 298951 0 0.0.0.0

# Convert a numeric identity to its hex representation
$ printf '%08x' 298951
00048fc7

# Search if there exists any policy related to this identity
#
# Key format: identity(4B) + port(2B) + proto(1B) + direction(1B)
# For endpoint 794's TCP/80 ingress, check if allow traffic from identity 298951
$ bpftool map dump pinned cilium_policy_00794 | grep "c7 8f 04 00" -B 1 -A 3
key:
c7 8f 04 00 00 50 06 00 # 4B identity + 2B port(80) + 1B L4Proto(TCP) + direction(ingress)
value:
00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00
```

The key and value data structures:

```go
// PolicyKey represents a key in the BPF policy map for an endpoint. It must
// match the layout of policy_key in bpf/lib/common.h.
// +k8s:deepcopy-gen:interfaces=github.com/cilium/cilium/pkg/bpf.MapKey
type PolicyKey struct {
	Identity         uint32 `align:"sec_label"`
	DestPort         uint16 `align:"dport"` // In network byte-order
	Nexthdr          uint8  `align:"protocol"`
	TrafficDirection uint8  `align:"egress"`
}

// PolicyEntry represents an entry in the BPF policy map for an endpoint. It must
// match the layout of policy_entry in bpf/lib/common.h.
// +k8s:deepcopy-gen:interfaces=github.com/cilium/cilium/pkg/bpf.MapValue
type PolicyEntry struct {
	ProxyPort uint16 `align:"proxy_port"` // In network byte-order
	Flags     uint8  `align:"deny"`
	Pad0      uint8  `align:"pad0"`
	Pad1      uint16 `align:"pad1"`
	Pad2      uint16 `align:"pad2"`
	Packets   uint64 `align:"packets"`
	Bytes     uint64 `align:"bytes"`
}

// pkg/maps/policymap/policymap.go

// Allow pushes an entry into the PolicyMap to allow traffic in the given
// `trafficDirection` for identity `id` with destination port `dport` over
// protocol `proto`. It is assumed that `dport` and `proxyPort` are in host byte-order.
func (pm *PolicyMap) Allow(id uint32, dport uint16, proto u8proto.U8proto, trafficDirection trafficdirection.TrafficDirection, proxyPort uint16) error {
	key := newKey(id, dport, proto, trafficDirection)
	pef := NewPolicyEntryFlag(&PolicyEntryFlagParam{})
	entry := newEntry(proxyPort, pef)
	return pm.Update(&key, &entry)
}
```

`bpftool` also comes to rescue in emergency cases, such as when
traffic is denied but your Kubernetes or Cilium agent can’t be ready,
just insert an **<mark><code>allow-any</code></mark>** rule like this:

```shell
# Add an allow-any rule in emergency cases
$ bpftool map update pinned <map> \
  key hex 00 00 00 00 00 00 00 00 \
  value hex 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 noexist
```

## 3.4 Manipulate kvstore contents with `etcdctl` and API

The last skill we’d like to share is to manipulate kvstore contents.

Again, this needs a deep understanding about the Cilium data models.
such as, with the following three entries inserted into kvstore,

```shell
$ etcdctl put "cilium/state/identities/v1/id/15614229" \
  'k8s:app=app1;k8s:io.cilium.k8s.policy.cluster=cluster1;k8s:io.cilium.k8s.policy.serviceaccount=default;k8s:io.kubernetes.pod.namespace=ns1;'

$ etcdctl put 'k8s:app=app1;k8s:io.cilium.k8s.policy.cluster=cluster1;k8s:io.cilium.k8s.policy.serviceaccount=default;k8s:io.kubernetes.pod.namespace=ns1;/10.3.9.10' \
  15614229

$ etcdctl put "cilium/state/ip/v1/cluster1/10.3.192.65" \
  '{"IP":"10.3.192.65","Mask":null,"HostIP":"10.3.9.10","ID":15614299,"Key":0,"Metadata":"cilium-global:cluster1:node1:2404","K8sNamespace":"ns1","K8sPodName":"pod1"}'
```

all the cilium-agents will be notified that **<mark>there is a pod created</mark>** in Kubernetes
`cluster1`, namespace `default`, with PoIP, NodeIP, NodeName, pod label and
identity information in the entries.

Essentially, this is **<mark>how we’ve injected our VM, BM and non-cilium-pods into Cilium world in our CER solution</mark>**
(see our previous post [8] for more details);
it's also a foundation of Cilium network policy.

**<mark>WARNING</mark>**: manipulation of kvstores as well as BPF maps are dangers,
so we do not recommend to perform these operations in production
environments, unless you know what you are doing.

# 4 Summary

We’ve been using Cilium since 1.4 and have upgraded all the way to ~~1.10~~ `1.11` (2022.11 updated) now,
it's supporting our business and infrastructure critical services.
With 4 years experiences, we believe it’s not only production ready for large scale,
but also one of the best candidates in terms of performance, feature, community and so on.

In the end, I’d like to say special thanks to Andre, Denial, Joe, Martynas, Paul, Quentin, Thomas
and all the Cilium guys. The community is very nice and has helped us a lot in the past.

# References

1. [github.com/ctripcloud/cilium-compose](https://github.com/ctripcloud/cilium-compose)
2. [Cracking Kubernetes Authentication (AuthN) Model]({% link _posts/2022-07-14-cracking-k8s-authn.md %})
3. [Cracking Kubernetes RBAC Authorization (AuthZ) Model]({% link _posts/2022-04-17-cracking-k8s-authz-rbac.md %})
4. [KVStoreMesh proposal](https://docs.google.com/document/d/1Zc8Sdhp96yKSeC1-71_6qd97HPWQv-L4kiBZhl7swrg/edit#)
5. [ClusterMesh: A Hands-on Guide]({% link _posts/2020-08-13-cilium-clustermesh.md %})
6. [Cracking Kubernetes Network Policy]({% link _posts/2022-01-23-cracking-k8s-network-policy.md %})
7. [What's inside cilium-etcd and what's not]({% link _posts/2020-05-20-whats-inside-cilium-etcd.md %})
8. [Trip.com: First Step towards Cloud Native Security]({% link _posts/2021-12-19-trip-first-step-towards-cloud-native-security.md %}), 2021
9. [Ctrip Network Architecture Evolution in the Cloud Computing Era]({% link _posts/2019-04-17-ctrip-network-arch-evolution.md %}), 2019
10. [Trip.com: First Step towards Cloud Native Networking]({% link _posts/2020-01-19-trip-first-step-towards-cloud-native-networking.md %}), 2020
