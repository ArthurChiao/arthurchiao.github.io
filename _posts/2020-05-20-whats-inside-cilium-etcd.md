---
layout    : post
title     : "What's inside Cilium Etcd (kvstore)"
date      : 2020-05-20
lastupdate: 2020-05-20
author    : ArthurChiao
categories: cilium etcd
---

As shown in the below, in each Kubernetes cluster, Cilium needs a **central**
policy repository (kvstore) to be deployed, and all Cilium agents ("CILIUM
DAEMON" in the picture) in this cluster will connect to this kvstore:

<p align="center"><img src="/assets/img/whats-inside-cilium-etcd/cilium-components.png" width="60%" height="60%"></p>
<p align="center">Fig. Cilium Components (image from official doc [1])</p>

While Cilium supports several types of kvstores, the most often used is etcd.

Then the question is: **what data is stored in this kvstore**? From the [official
documentation](https://docs.cilium.io/en/v1.7/concepts/overview/#key-value-store):

> The Key-Value (KV) Store is used for the following state:
>
> * Policy Identities: list of labels <=> policy identity identifier
> * Global Services: global service id to VIP association (optional)
> * Encapsulation VTEP mapping (optional)

In this post, we will dig inside by some command line tools.

### Environment

Software version:

1. Kubernetes: `1.17+`
1. Cilium: `1.7.0`
1. Etcd as cilium's kvstore: `3.2.24`

other configurations:

1. Cilium networking solution: direct routing via BGP [2]
1. Cilium network policy: not used yet

## Attention

Note that some command line outputs in this post are re-formatted for better
human viewing.

**Do not rely on the output formats in this post if you are automating something.**

# 1 Preparation

We will execute commands on the following types of nodes:

* k8s worker nodes
* k8s master nodes
* cilium-etcd nodes

## 1.1 Check cilium-etcd information

On k8s worker node:

```shell
(k8s node) $ cilium status
KVStore:      Ok   etcd: 3/3 connected, ... https://10.5.2.10:2379; https://10.5.2.11:2379; https://10.5.2.12:2379 (Leader)
Kubernetes:   Ok   1.17+ [linux/amd64]
...
```

As shown in the output, we are connected to 3 etcd instances.

**I can run `cilium` command on worker nodes because `cilium` is a wrapper
here:**

```shell
(k8s node) $ cat /usr/local/bin/cilium
#!/bin/bash
CID=`sudo docker ps | awk '/cilium-agent/ {print $1}'`
sudo docker exec $CID cilium $@
```

## 1.2 Setup `etcdctl`

On etcd node:

```shell
(etcd node) $ cat /etc/etcd/etcdrc
export ETCDCTL_API=3
export ETCDCTL_ENDPOINTS="https://127.0.0.1:2379"
export ETCDCTL_CACERT=/etc/etcd/ssl/ca.crt
export ETCDCTL_CERT=/etc/etcd/ssl/etcd-client.crt
export ETCDCTL_KEY=/etc/etcd/ssl/etcd-client.key
```

Test:

```shell
(etcd node) $ source /etc/etcd/etcdrc

(etcd node) $ etcdctl endpoint health
127.0.0.1:2379 is healthy: successfully committed proposal: took = 2.67051ms

(etcd node) $ etcdctl member list
e7c82b4ad3edb47,  started, etcd-node1, https://10.5.2.10:2380, https://10.5.2.10:2379
d0c026e8a84e61d4, started, etcd-node2, https://10.5.2.12:2380, https://10.5.2.12:2379
f13a3cd92f97eadd, started, etcd-node3, https://10.5.2.11:2380, https://10.5.2.11:2379
```

# 2 List keys in cilium-etcd

First, have a glimpse of all keys in etcd:

```shell
$ etcdctl get "" --prefix --keys-only
cilium/state/identities/v1/id/10004
...
cilium/state/nodes/v1/default/node1
...
cilium/state/ip/v1/default/10.6.36.43
...
```

Strip blank lines and redirect to file:

```shell
$ etcdctl get "" --prefix --keys-only | grep -v "^$" > keys.txt
```

# 3 Data in cilium-etcd: dig inside

In my environment, those dumped keys group into 3 types:

* pod identities
* pod ip addresses
* k8s nodes

## 3.1 Identity

Dump the content (value) of a specific identity:

```shell
$ etcdctl get cilium/state/identities/v1/id/12928
k8s:app=istio-ingressgateway;
k8s:chart=gateways;
k8s:heritage=Tiller;
k8s:io.cilium.k8s.policy.cluster=default;
k8s:io.cilium.k8s.policy.serviceaccount=ai-ingressgateway-service-account;
k8s:io.kubernetes.pod.namespace=istio-system;
k8s:istio=ai-ingressgateway;
k8s:release=istio;
```

These seem to be Pod labels, verify it on K8S master:

```shell
(k8s master) $ k get pod -n istio-system --show-labels | grep istio-ingressgateway
NAME                                      READY   LABELS
ai-ingressgateway-6bdbdbf7dc-jz85c        1/1     app=istio-ingressgateway,chart=gateways,heritage=Tiller,istio=ai-ingressgateway,pod-template-hash=6bdbdbf7dc,release=istio
```

Describe pod to get more detailed information:

```shell
(k8s master) $ k describe pod -n istio-system ai-ingressgateway-6bdbdbf7dc-jz85c
Name:         ai-ingressgateway-6bdbdbf7dc-jz85c
...
Labels:       app=istio-ingressgateway
              chart=gateways
              heritage=Tiller
              istio=ai-ingressgateway
              pod-template-hash=6bdbdbf7dc
              release=istio
Annotations:  sidecar.istio.io/inject: false
IP:           10.6.6.43
...
```

That's it!

As we can see, Cilium picks up **some** (not all, e.g. `pod-template` ignored)
labels from K8S, and appends them with `k8s:`, then stores to cilium-etcd:

* `app=istio-ingressgateway`
* `chart=gateways`
* `heritage=Tiller`
* `istio=ai-ingressgateway`
* `release=istio`

Besides, it also adds its own labels:

* `k8s:io.cilium.k8s.policy.cluster=default`
* `k8s:io.cilium.k8s.policy.serviceaccount=ai-ingressgateway-service-account`
* `k8s:io.kubernetes.pod.namespace=istio-system`

Note that pod label `pod-template-hash=6bdbdbf7dc` is not stored into etcd
(ignored by Cilium).

## 3.2 IP Address

In the `k describe pod` output, we get some additional information:

* Pod IP: `10.6.6.43`
* Node: `k8s-node1`
* PodCIDR: `10.6.6.0/24`

Now dump the IP address information in cilium-etcd:

```shell
$ etcdctl get cilium/state/ip/v1/default/10.6.6.43
{
  "IP":"10.6.6.43",
  "Mask":null,
  "HostIP":"10.5.1.132",
  "ID":12928,
  "Key":0,
  "Metadata":"cilium-global:default:k8s-node1:432",
  "K8sNamespace":"istio-system",
  "K8sPodName":"ai-ingressgateway-6bdbdbf7dc-jz85c"
}
```

## 3.3 Kubernetes Node

Dump the node information:

```shell
$ etcdctl get cilium/state/nodes/v1/default/k8s-node1
{
  "Name":"k8s-node1",
  "Cluster":"default",
  "IPAddresses":[{"Type":"InternalIP", "IP":"10.5.1.132"},
                 {"Type":"CiliumInternalIP", "IP":"10.6.6.71"}],
  "IPv4AllocCIDR":{"IP":"10.6.6.0", "Mask":"////AA=="},
  "IPv6AllocCIDR":null,
  "IPv4HealthIP":"10.6.6.79",
  "IPv6HealthIP":"",
  "ClusterID":0,
  "Source":"local",
  "EncryptionKey":0
},
```

Where:

* `InternalIP`: node IP address
* `IPv4AllocCIDR`: PodCIDR for this node, used by Cilium agent
* `CiliumInternalIP`: Cilium's gateway IP (configured on `cilium_host`) on this node, allocated from PodCIDR
* `IPv4HealthIP`: Cilium's health check IP address on this node, allocated from PodCIDR

### 3.3.1 IPv4AllocCIDR

```shell
(k8s master) $ k describe node k8s-node1 | grep PodCIDR
PodCIDR:                      10.6.6.0/24
PodCIDRs:                     10.6.6.0/24
```

### 3.3.2 CiliumInternalIP

```shell
(k8s-node1) $ ifconfig cilium_host
cilium_host: flags=4291<UP,BROADCAST,RUNNING,NOARP,MULTICAST>  mtu 1500
        inet 10.6.6.71  netmask 255.255.255.255  broadcast 0.0.0.0
        ...
```

### 3.3.3 IPv4HealthIP

```shell
(k8s-node1) $ cilium endpoint list | awk 'NR == 1 || /reserved:health/'
ENDPOINT   POLICY (ingress)   POLICY (egress)   IDENTITY   LABELS (source:key[=value])   IPv4          STATUS
3030       Disabled           Disabled          4          reserved:health               10.6.6.79    ready
```

# 4. Summary

Cilium's kvstore is mainly intended for storing network policies, unfortunately
network policy is not used in this cluster yet. However, with `etcdctl` commands
and the steps provided in this post, it's not difficult to dig further.

May update this post later for the missing network policy part.

## References

1. [Cilium Documentation](https://docs.cilium.io/en/v1.7/concepts/overview/)
2. [Trip.com: First Step towards Cloud-native Networking]({% link _posts/2020-01-19-trip-first-step-towards-cloud-native-networking.md %})
