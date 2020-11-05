---
layout    : post
title     : "Cracking kubernetes node proxy (aka kube-proxy)"
date      : 2019-11-30
lastupdate: 2019-12-15
categories: kubernetes iptables ipvs
---

## Index

1. [Background Knowledge](#ch_1)
2. [The Node Proxy Model](#ch_2)
3. [Test Environment](#ch_3)
4. [Implementation: proxy via userspace socket](#ch_4)
5. [Implementation: proxy via `iptables`](#ch_5)
6. [Implementation: proxy via `ipvs/ipset`](#ch_6)
7. [Implementation: proxy via `bpf`](#ch_7)
8. [Summary](#ch_8)
9. [References](#references)
10. [Appendix](#appendix)

There are [several types of proxies](https://kubernetes.io/docs/concepts/cluster-administration/proxies/) in
Kubernetes. Among them is the **node proxier**, or
[`kube-proxy`](https://kubernetes.io/docs/reference/command-line-tools-reference/kube-proxy/),
which reflects services defined in the Kubernetes API on each node and can do simple
TCP/UDP/SCTP stream forwarding across a set of backends [1].

To have a better understanding of the node proxier model, in this post we will
design and implement our own versions of kube-proxy with different means;
although these will just be toy programs, they essentially work the same way as
the vanilla `kube-proxy` running inside your K8S cluster in terms of
**transparent traffic intercepting, forwarding, load balancing**, etc.

With our toy proxiers, applications (whether it is a host native app, or an app
running inside a VM/container) on a non-k8s-node (not in K8S cluster) can also
access K8S services with **ClusterIP** - note that in kubernetes's design,
ClusterIP is only accessible within K8S cluster nodes. (In some sense, our toy
proxier turned the non-k8s-node into a K8S node.)

<a name="ch_1"></a>

# 1. Background Knowledge

Following background knowledge is needed to understand traffic interception and
proxy in Linux kernel.

## 1.1 Netfilter

Netfilter is a **packet filtering and processing framework** inside Linux
kernel. Refer to [A Deep Dive into Iptables and Netfilter Architecture](https://www.digitalocean.com/community/tutorials/a-deep-dive-into-iptables-and-netfilter-architecture)
(or [my translation]({% link _posts/2019-02-18-deep-dive-into-iptables-and-netfilter-arch-zh.md %}) in case you can read Chinese)
if you are not familar with this.

Some key points:

* **all packets** on the host will go through the netfilter framework
* there are **5 hook points** in netfilter framework: `PRE_ROUTING`、`INPUT`、`FORWARD`、`OUTPUT`、`POST_ROUTING`
* command line tool `iptables` can be used to **dynamically insert rules into hook points**
* one can **manipulate packets (accept/redirect/drop/modify, etc)** by combining various `iptables` rules

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/hooks.png" width="50%" height="50%"></p>
<p align="center"> Fig. The 5 hook points in netfilter framework</p>

Besides, the 5 hook points are working collaborativelly with other kernel
networking facilities, e.g. kernel routing subsystem.

Further, in each hook point, **rules are organized into different `chain`s**
with pre-defined priorities. To **manage chains by their purposes**, chains are
further organized into **`table`s**.  There are 5 tables now:

* `filter`: do normal filtering, e.g. accept, reject/drop, jump
* `nat`: network address translation, including SNAT (source NAT) and DNAT
  (destination NAT)
* `mangle`: modify packet attributes, e.g. TTL
* `raw`: earliest processing point, special processing before connection tracking
  (`conntrack` or `CT`, also included in the above figure, but this is NOT a
  chain)
* `security`: not covered in this post

Adding table/chain into the above picture, we get a more detailed view:

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/hooks-and-tables.png" width="80%" height="80%"></p>
<p align="center"> Fig. iptables table/chains inside hook points</p>

## 1.2 VIP and Load balancing (LB)

Virtual IP (IP) hides all backend IPs to the client/user, so that client/user
always communicates with backend services with VIP, no need to care about how
many instances behind the VIP.

VIP always comes with load balancing as it needs to distribute traffic between
different backends.

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/vip-and-lb.png" width="40%" height="40%"></p>
<p align="center"> Fig. VIP and load balancing</p>

## 1.3 Cross-host networking model

How could an instance (container, VM, etc) on host A communicate with another
instance on host B? There are many solutions:

* direct routing: BGP, etc
* tunneling: VxLAN, IPIP, GRE, etc
* NAT: e.g. docker's bridge network mode
* others

<a name="ch_2"></a>

# 2. The Node Proxy Model

In kubernetes, you can define an application to be a
[Service](https://kubernetes.io/docs/concepts/services-networking/service/).
A Service is an abstraction which defines a logical set of Pods and a policy by
which to access them.

## 2.1 Service Types

There are 4 types of `Service`s defined in K8S:

1. ClusterIP: access a `Service` via an VIP, but this VIP could only be
   accessed inside this cluster
1. NodePort: access a `Service` via `NodeIP:NodePort`, this means the port will
   be reserved on all nodes inside the cluster
1. ExternalIP: same as ClusterIP, but this VIP is accessible from outside of
   this cluster
1. LoadBalancer

This post will focus on `ClusterIP`, but other 3 types are much similar in the
underlying implementation in terms of traffic interception and forwarding.

## 2.2 Node Proxy

A Service has a VIP (ClusterIP in this post) and multiple endpoints (backend
pods). Every pod or node can access the application directly by the VIP. To
make this available, the node proxier needs to **run on each node**.
.It should be able to transparently intercept traffics to any `ClusterIP:Port` [NOTE 1],
and redirect them to one or multiple backend pods.

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/k8s-proxier-model.png" width="90%" height="90%"></p>
<p align="center"> Fig. Kubernetes proxier model</p>

> NOTE 1
>
> A common misunderstaning about ClusterIP is that ClusterIP is ping-able - they
> are not by definition. If you ping a ClusterIP, you probably find it
> unreachable.
>
> By definition, a **`<Protocol,ClusterIP,Port>`** tuple uniquelly defines
> a Service (and thus an interception rule). For example, if a Service is
> defined to be `<tcp,10.7.0.100,80>`, then proxy only handles traffic of
> `tcp:10.7.0.100:80`, other traffics, eg.  `tcp:10.7.0.100:8080`,
> `udp:10.7.0.100:80` will not be proxied. Thus the ClusterIP would not
> reachable either (ICMP traffic).
>
> However, if you are using kube-proxy with IPVS mode, the ClusterIP is indeed
> reachable via ping. This is because the IPVS mode implementation does more
> than what is needed by the definition. You will see the differences in the
> following sections.

## 2.3 Role of the node proxy: reverse proxy

If you think about the role of the node proxy, it actually
acts as a reverse proxy in the K8S network model, that is, on each node, it will:

1. hide all backend Pods to client side
1. filter all egress traffic (requests to backends)

For ingress traffic, it does nothing.

## 2.4 Performance issues

If we have an application on a host, and there are 1K Services
in the K8S cluster, then we can never guess **which Service the app would access
in the next moment** (ignore network policy here). So in order to make all the
Services accessible to the app, we have to apply all the proxy rules for all
the Services on the node. Expand this idea to the entire cluster, this means:

**Proxy rules for all Services should be applied on all nodes in the entire cluster.**

In some sense, this is a fully distributed proxy model in that any node has all
the rules of the cluster.

This leads to severe performance issues when the cluster grows large, as there
can be hundreds of thousands of rules on each node [6,7].

<a name="ch_3"></a>

# 3. Test Environment

## 3.1 Cluster Topology and Test Environment

We will use following environment for testing:

* a K8S cluster
    * one master node
    * one worker node
    * network solution: direct routing (PodIP directly routable)
* a non-k8s-node, but it can reach the worker node and Pod (thanks to the direct
  routing networking scheme)

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/test-env.png" width="55%" height="55%"></p>

We will deploy Pods on the worker node, and access the applications in the Pods
via ClusterIP from test node.

## 3.2 Create a Service

Create a simple `Statefulset`, which includes a `Service`, and the `Service`
will have one or more backend Pods:

```shell
# see appendix for webapp.yaml
$ kubectl create -f webapp.yaml

$ kubectl get svc -o wide webapp
NAME     TYPE        CLUSTER-IP     EXTERNAL-IP   PORT(S)   AGE     SELECTOR
webapp   ClusterIP   10.7.111.132   <none>        80/TCP    2m11s   app=webapp

$ kubectl get pod -o wide | grep webapp
webapp-0    2/2     Running   0    2m12s 10.5.41.204    node1    <none>  <none>
```

The application runs at port `80` with `tcp` protocol.

## 3.3 Reachability test

First curl PodIP+Port:

```shell
$ curl 10.5.41.204:80
<!DOCTYPE html>
...
</html>
```

Successful! Then replace PodIP with ClusterIP and have another try:

```shell
$ curl 10.7.111.132:80
^C
```

As expected, it is unreachable!

In the next sections, we will investigate how to make the ClusterIP reachable
with different means.

<a name="ch_4"></a>

# 4. Impelenetation: proxy via userspace socket

## 4.1 The middleman model

The most easy-of-understanding realization is inserting our `toy-proxy`
as **a middleman in the traffic path on this host**: for each
connection from a local client to a ClusterIP:Port, we **intercept the connection
and split it into two separate connections**:

1. connection between local client and `toy-proxy`
1. connection between `toy-proxy` and backend pods

The easiest way to achieve this is to **implement it in userspace**:

1. **Listen to resources**: start a daemon process, listen to K8S apiserver,
   watch Service (ClusterIP) and Endpoint (Pod) changes
1. **Proxy traffic**: for each connection request from a local client to a
   Service (ClusterIP), intercepting the request by acting as a middleman
1. **Dynamically apply proxy rules**: for any Service/Endpoint updates, change
   `toy-proxy` connection settings accordingly

For our above test application `webapp`, following picture depicts the data flow:

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/userspace-proxier.png" width="50%" height="50%"></p>

## 4.2 POC Implementation

Let's see a proof-of-concept implementation for the above picture.

### 4.2.1 Code

Following code ommited a few error handle code for ease-of-reading:

```go
func main() {
	clusterIP := "10.7.111.132"
	podIP := "10.5.41.204"
	port := 80
	proto := "tcp"

	addRedirectRules(clusterIP, port, proto)
	createProxy(podIP, port, proto)
}

func addRedirectRules(clusterIP string, port int, proto string) error {
	p := strconv.Itoa(port)
	cmd := exec.Command("iptables", "-t", "nat", "-A", "OUTPUT", "-p", "tcp",
		"-d", clusterIP, "--dport", p, "-j", "REDIRECT", "--to-port", p)
	return cmd.Run()
}

func createProxy(podIP string, port int, proto string) {
	host := ""
	listener, err := net.Listen(proto, net.JoinHostPort(host, strconv.Itoa(port)))

	for {
		inConn, err := listener.Accept()
		outConn, err := net.Dial(proto, net.JoinHostPort(podIP, strconv.Itoa(port)))

		go func(in, out *net.TCPConn) {
			var wg sync.WaitGroup
			wg.Add(2)
			fmt.Printf("Proxying %v <-> %v <-> %v <-> %v\n",
				in.RemoteAddr(), in.LocalAddr(), out.LocalAddr(), out.RemoteAddr())
			go copyBytes(in, out, &wg)
			go copyBytes(out, in, &wg)
			wg.Wait()
		}(inConn.(*net.TCPConn), outConn.(*net.TCPConn))
	}

	listener.Close()
}

func copyBytes(dst, src *net.TCPConn, wg *sync.WaitGroup) {
	defer wg.Done()
	if _, err := io.Copy(dst, src); err != nil {
		if !strings.HasSuffix(err.Error(), "use of closed network connection") {
			fmt.Printf("io.Copy error: %v", err)
		}
	}
	dst.Close()
	src.Close()
}
```

### 4.2.2 Some Explanations

#### 1. traffic interception

We would like to intercept all traffic destinated for `ClusterIP:Port`, but `ClusterIP`
is not configured on any device on this node, so we could not do something
like `listen(ClusterIP, Port)`, then how could we do the interception? The
answer is: using the `REDIRECT` ability provided by iptables/netfilter.

Following command will direct all traffic destinated to `ClusterIP:Port` to
`localhost:Port`:

```shell
$ sudo iptables -t nat -A OUTPUT -p tcp -d $CLUSTER_IP --dport $PORT -j REDIRECT --to-port $PORT
```

Don't be afraid if you can't understanding this at present. We will cover this
later.

Verify this by seen this in the output of below command:

```
$ iptables -t nat -L -n
...
Chain OUTPUT (policy ACCEPT)
target     prot opt source      destination
REDIRECT   tcp  --  0.0.0.0/0   10.7.111.132         tcp dpt:80 redir ports 80
```

In the code, function `addRedirectRules()` wraps the above process.

#### 2. create proxy

Function `createProxy()` creates the userspace proxy, and does bi-directional
forwarding.

### 4.3 Reachability test

Build the code and run the binary:

```shell
$ go build toy-proxy-userspace.go
$ sudo ./toy-proxy-userspace
```

Now test accessing:

```shell
$ curl $CLUSTER_IP:$PORT
<!DOCTYPE html>
...
</html>
```

Successful! And our proxier's message:

```
$ sudo ./toy-proxy-userspace
Creating proxy between <host ip>:53912 <-> 127.0.0.1:80 <-> <host ip>:40194 <-> 10.5.41.204:80
```

It says, for original connection request of `<host ip>:53912 <->
10.7.111.132:80`, it splits it into two connections:

1. `<host ip>:53912 <-> 127.0.0.1:80`
1. `<host ip>:40194 <-> 10.5.41.204:80`

Delete this rule:

```shell
$ iptables -t nat -L -n --line-numbers
...
Chain OUTPUT (policy ACCEPT)
num  target     prot opt source               destination
2    REDIRECT   tcp  --  0.0.0.0/0   10.7.111.132         tcp dpt:80 redir ports 80

# iptables -t nat -D OUTPUT <num>
$ iptables -t nat -D OUTPUT 2
```

or delete (flush) all rules if you get things messy:

```shell
$ iptables -t nat -F # delete all rules
$ iptables -t nat -X # delete all custom chains
```

## 4.3 Improvements

In this toy implementation, we intercepts `ClusterIP:80` to `localhost:80`,
but what if a native application on this host want to use `localhost:80` too? Further, what if
multiple Services all exposing port `80`? Apparently we need to distinguish
between these applications or Services. The right way to fix this problem is:
**for each proxy, allocate an unused temporay port `TmpPort`, intercept
`ClusterIP:Port` to `localhost:TmpPort`**. e.g. app1 using `10001`, app2 using
`10002`.

Secondly, above code only handles one backend, what if there are
multiple backend pods? Thus, we need to distribute requests to different
backend pods by load balancing algorithms.

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/userspace-proxier-2.png" width="50%" height="50%"></p>

## 4.4 Pros and Cons

This method is fairly easy to understand and implement, however, it would suffer
from bad performances as it has to copying bytes between the two side, and
between kernel and userspace memories.

We don't spend too much time on this, see the vanilla implementation of userspace kube-proxy
[here](https://github.com/kubernetes/kubernetes/tree/master/pkg/proxy/userspace)
if you are interested.

In the next, let's see another way to do this task.

<a name="ch_5"></a>

# 5. Implementation: proxy via iptables

The main bottleneck of userspace proxier comes from the kernel-userspace
switching and data copying. If we can **implement a proxy entirely in kernel
space**, it will beat the userspace one with great performance boost. `iptables`
can be used to achieve this goal.

Before we start, let's first figure out the traffic path when we do `curl
ClusterIP:Port`, then we will investigate how to make it reachable with iptables
rules.

## 5.1 Host -> ClusterIP (single backend)

The ClusterIP doesn't live on any network device, so in order to let our packets
finally reach the backend Pod, we need to convert the ClusterIP to PodIP (routable), namely:

* condition: match packets with `dst=ClusterIP,proto=tcp,dport=80`
* action: replace `dst=ClusterIP` with `dst=PodIP` in IP header of the packets

With network terminology, this is a **network address translation (NAT)** process. 

### 5.1.1 Where to do the DNAT

Looking at the egress packet path of our `curl` process in below picture:

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/host-to-clusterip-dnat.png" width="85%" height="85%"></p>

`<curl process> -> raw -> CT -> mangle -> dnat -> filter -> security -> snat -> <ROUTING> -> mangle -> snat -> NIC`

it's clear that there is **only one `dnat` (chain), appeared in the `OUTPUT`
hook**, where we could do DNAT.

Let's see how we will do the hacking.

### 5.1.2 Check current NAT rules

NAT rules are organized into `nat` table. Check the current rules in `nat` table:

```shell
# -t <table>
# -L list rules
# -n numeric output
$ iptables -t nat -L -n
Chain PREROUTING (policy ACCEPT)

Chain INPUT (policy ACCEPT)

Chain OUTPUT (policy ACCEPT)
DOCKER     all  --  0.0.0.0/0    !127.0.0.0/8   ADDRTYPE match dst-type LOCAL

Chain POSTROUTING (policy ACCEPT)
```

The output shows that there are no rules except DOCKER related ones. Those
`DOCKER` rules are inserted by `docker` upon installation, but they won't affect
our experiments in this post. So we just ignore them.

### 5.1.3 Add DNAT rules

For ease-of-viewing, we will not wrap the iptables commands with go code,
but directly show the commands themselves.

> NOTE: Before proceed on, make sure you have deleted all the rules you added in
> previous section.

Confirm that the ClusterIP is not reachable at present:

```shell
$ curl $CLUSTER_IP:$PORT
^C
```

Now add our egress NAT rule:

```shell
$ cat ENV
CLUSTER_IP=10.7.111.132
POD_IP=10.5.41.204
PORT=80
PROTO=tcp

# -p               <protocol>
# -A               add rule
# --dport          <dst port>
# -d               <dst ip>
# -j               jump to
# --to-destination <ip>:<port>
$ iptables -t nat -A OUTPUT -p $PROTO --dport $PORT -d $CLUSTER_IP -j DNAT --to-destination $POD_IP:$PORT
```

Check the table again:

```shell
$ iptables -t nat -L -n

Chain OUTPUT (policy ACCEPT)
target     prot opt source      destination
DNAT       tcp  --  0.0.0.0/0   10.7.111.132   tcp dpt:80 to:10.5.41.204:80
```

We can see the rule is added.

### 5.1.4 Test reachability

Now curl again:

```shell
$ curl $CLUSTER_IP:$PORT
<!DOCTYPE html>
...
</html>
```

That's it! curl successful.

But wait! We expect the egress traffic should be NATed correctly, but **we haven't
added any NAT rules in the ingress path, how could the traffic be OK on both
directions?** It turns out that when you add a NAT rule for one direction, Linux
kernel will automatically add the reserve rules on another direction! This
works collaboratively with the `conntrack` (CT, connection tracking) module.

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/host-to-clusterip-dnat-ct.png" width="85%" height="85%"></p>

### 5.1.5 Cleanup

Delete this rule:

```shell
$ iptables -t nat -L -n --line-numbers
...
Chain OUTPUT (policy ACCEPT)
num  target     prot opt source               destination
2    DNAT       tcp  --  0.0.0.0/0   10.7.111.132   tcp dpt:80 to:10.5.41.204:80

# iptables -t <table> -D <chain> <num>
$ iptables -t nat -D OUTPUT 2
```

## 5.2 Host -> ClusterIP (multiple backends)

In previous section, we showed how to perform NAT with one backend Pod. Now
let's see the multiple-backend case.

> NOTE: Before proceed on, make sure you have deleted all the rules you added in
> previous section.

### 5.2.1 Scale up webapp

First scale up our Service to 2 backend pods:

```shell
$ kubectl scale sts webapp --replicas=2
statefulset.apps/webapp scaled
statefulset.apps/webapp scaled

$ kubectl get pod -o wide | grep webapp
webapp-0   2/2     Running   0   1h24m   10.5.41.204    node1    <none> <none>
webapp-1   2/2     Running   0   11s     10.5.41.5      node1    <none> <none>
```

### 5.2.2 Add DNAT rules with load balancing

We need the `statistic` module in iptables to distribute requests to
backend Pods with probability, in this way we would achieve the load balancing
effect:

```shell
# -m <module>
$ iptables -t nat -A OUTPUT -p $PROTO --dport $PORT -d $CLUSTER_IP \
    -m statistic --mode random --probability 0.5  \
    -j DNAT --to-destination $POD1_IP:$PORT
$ iptables -t nat -A OUTPUT -p $PROTO --dport $PORT -d $CLUSTER_IP \
    -m statistic --mode random --probability 1.0  \
    -j DNAT --to-destination $POD2_IP:$PORT
```

The above commands specify randomly distributes requests between two Pods, each
with `50%` probability.

Now check the rules:

```shell
$ iptables -t nat -L -n
...
Chain OUTPUT (policy ACCEPT)
target  prot opt source      destination
DNAT    tcp  --  0.0.0.0/0   10.7.111.132  tcp dpt:80 statistic mode random probability 0.50000000000 to:10.5.41.204:80
DNAT    tcp  --  0.0.0.0/0   10.7.111.132  tcp dpt:80 statistic mode random probability 1.00000000000 to:10.5.41.5:80
```

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/host-to-clusterip-lb-ct.png" width="85%" height="85%"></p>

### 5.2.3 Verification

Now let's verify the load balancing works. We make 8 requests, and capture the
real PodIPs this host communicates to:

Open a shell on test node:

```shell
$ for i in {1..8}; do curl $CLUSTER_IP:$PORT 2>&1 >/dev/null; sleep 1; done
```

another shell window on test node:

```shell
$ tcpdump -nn -i eth0 port $PORT | grep "GET /"
10.21.0.7.48306 > 10.5.41.5.80:   ... HTTP: GET / HTTP/1.1
10.21.0.7.48308 > 10.5.41.204.80: ... HTTP: GET / HTTP/1.1
10.21.0.7.48310 > 10.5.41.204.80: ... HTTP: GET / HTTP/1.1
10.21.0.7.48312 > 10.5.41.5.80:   ... HTTP: GET / HTTP/1.1
10.21.0.7.48314 > 10.5.41.5.80:   ... HTTP: GET / HTTP/1.1
10.21.0.7.48316 > 10.5.41.204.80: ... HTTP: GET / HTTP/1.1
10.21.0.7.48318 > 10.5.41.5.80:   ... HTTP: GET / HTTP/1.1
10.21.0.7.48320 > 10.5.41.204.80: ... HTTP: GET / HTTP/1.1
```

4 times with Pod1 and 4 times with Pod2, 50% with each pod, exactly what's expecteded.

### 5.2.4 Cleanup

```shell
$ iptables -t nat -L -n --line-numbers
...
Chain OUTPUT (policy ACCEPT)
num  target     prot opt source               destination
2    DNAT    tcp  --  0.0.0.0/0   10.7.111.132  tcp dpt:80 statistic mode random probability 0.50000000000 to:10.5.41.204:80
3    DNAT    tcp  --  0.0.0.0/0   10.7.111.132  tcp dpt:80 statistic mode random probability 1.00000000000 to:10.5.41.5:80

$ iptables -t nat -D OUTPUT 2
$ iptables -t nat -D OUTPUT 3
```

## 5.3 Pod (app A) -> ClusterIP (app B)

What should we do application A's Pod on `hostA` would like to visit application
B's ClusterIP, where B's pods resides on `hostB`?

Actually this is much the same as `Host --> ClusterIP` case, but with one more
thing: **after NAT is performed, the source node (hostA) needs to send the
packet to the right destination node (hostB) on which the destination Pod
resides**. Depending on different cross-host networking solutions, this varies a
lot:

1. for direct routing cases, the host just sends the packets out. Such solutions
   including
    * calico + bird
    * cilium + kube-router (Cilium's default solution for BGP)
    * cilium + bird (actually this is just our test env networking solutions
      here)
1. for tunneling cases, there must be an agent on each host, which performs
   encap after DNAT, and decap before SNAT. Such solutions including:
    * calico + VxLAN mode
    * flannel + IPIP mode
    * flannel + VxLAN mode
    * cilium + VxLAN mode
1. AWS-like ENI mode: similar as direct routing, but you don't need a BGP agent
    * cilium + ENI mode

Following picture shows the tunneling case:

<p align="center"><img src="/assets/img/cracking-k8s-node-proxy/tunneling.png" width="85%" height="85%"></p>

The tunneling related responsibilities of the agent including:

1. **sync tunnel information between all nodes**, e.g. info describing which
   instance is on which node
1. **perform encapsulation after DNAT for pod traffic**: for all egress traffic,
   e.g. from `hostA` with `dst=<PodIP>`, where `PodIP` is on `hostB`,
   encapsulate packets by add another header, e.g. VxLAN header, where the
   encapsulation header has `src=hostA_IP,dst=hostB_IP`
1. **perform decapsulation before SNAT for pod traffic**: decapsulate each
   ingress encasulated packet: remove outer layer (e.g. VxLAN header)

Also, the host needs to decide:

1. which packets should go to decapsulator (pod traffic), which shouldn't (e.g. host traffic)
1. which packets should go to encapsulator (pod traffic), which shouldn't (e.g. host traffic)

## 5.4 Re-structure the iptables rules

> NOTE: Before proceed on, make sure you have deleted all the rules you added in
> previous section.

When you have plentiful Services, the iptables rules on each node will be fairly
complicated, thus you need some structural work to organize those rules.

In this section, we will create several dedicated iptables `chain`s in `nat`
table, specifically:

* chain `KUBE-SERVICES`: intercept all egress traffic in `OUTPUT` chain of `nat`
  table to this chain, do DNAT if they are destinated for a ClusterIP
* chain `KUBE-SVC-WEBAPP`: intercept all traffic in `KUBE-SERVICES` to this
  chain if `dst`, `proto` and `port` match
* chain `KUBE-SEP-WEBAPP1`: intercept 50% of the traffic in `KUBE-SVC-WEBAPP` to
  here
* chain `KUBE-SEP-WEBAPP2`: intercept 50% of the traffic in `KUBE-SVC-WEBAPP` to
  here

The DNAT path now will be:

```
OUTPUT -> KUBE-SERVICES -> KUBE-SVC-WEBAPP --> KUBE-SEP-WEBAPP1
                                         \
                                          \--> KUBE-SEP-WEBAPP2
```

If you have multiple services, the DNAT path would be like:

```
OUTPUT -> KUBE-SERVICES -> KUBE-SVC-A --> KUBE-SEP-A1
                      |              \--> KUBE-SEP-A2
                      |
                      |--> KUBE-SVC-B --> KUBE-SEP-B1
                      |              \--> KUBE-SEP-B2
                      |
                      |--> KUBE-SVC-C --> KUBE-SEP-C1
                                     \--> KUBE-SEP-C2
```

iptables commands:

```shell
$ cat add-dnat-structured.sh
source ../ENV

set -x

KUBE_SVCS="KUBE-SERVICES"        # chain that serves as kubernetes service portal
SVC_WEBAPP="KUBE-SVC-WEBAPP"     # chain that serves as DNAT entrypoint for webapp
WEBAPP_EP1="KUBE-SEP-WEBAPP1"    # chain that performs dnat to pod1
WEBAPP_EP2="KUBE-SEP-WEBAPP2"    # chain that performs dnat to pod2

# OUTPUT -> KUBE-SERVICES
sudo iptables -t nat -N $KUBE_SVCS
sudo iptables -t nat -A OUTPUT -p all -s 0.0.0.0/0 -d 0.0.0.0/0 -j $KUBE_SVCS

# KUBE-SERVICES -> KUBE-SVC-WEBAPP
sudo iptables -t nat -N $SVC_WEBAPP
sudo iptables -t nat -A $KUBE_SVCS -p $PROTO -s 0.0.0.0/0 -d $CLUSTER_IP --dport $PORT -j $SVC_WEBAPP

# KUBE-SVC-WEBAPP -> KUBE-SEP-WEBAPP*
sudo iptables -t nat -N $WEBAPP_EP1
sudo iptables -t nat -N $WEBAPP_EP2
sudo iptables -t nat -A $WEBAPP_EP1 -p $PROTO -s 0.0.0.0/0 -d 0.0.0.0/0 --dport $PORT -j DNAT --to-destination $POD1_IP:$PORT
sudo iptables -t nat -A $WEBAPP_EP2 -p $PROTO -s 0.0.0.0/0 -d 0.0.0.0/0 --dport $PORT -j DNAT --to-destination $POD2_IP:$PORT
sudo iptables -t nat -A $SVC_WEBAPP -p $PROTO -s 0.0.0.0/0 -d 0.0.0.0/0 -m statistic --mode random --probability 0.5  -j $WEBAPP_EP1
sudo iptables -t nat -A $SVC_WEBAPP -p $PROTO -s 0.0.0.0/0 -d 0.0.0.0/0 -m statistic --mode random --probability 1.0  -j $WEBAPP_EP2
```

Now test our design:

```shell
$ ./add-dnat-structured.sh
++ KUBE_SVCS=KUBE-SERVICES
++ SVC_WEBAPP=KUBE-SVC-WEBAPP
++ WEBAPP_EP1=KUBE-SEP-WEBAPP1
++ WEBAPP_EP2=KUBE-SEP-WEBAPP2
++ sudo iptables -t nat -N KUBE-SERVICES
++ sudo iptables -t nat -A OUTPUT -p all -s 0.0.0.0/0 -d 0.0.0.0/0 -j KUBE-SERVICES
++ sudo iptables -t nat -N KUBE-SVC-WEBAPP
++ sudo iptables -t nat -A KUBE-SERVICES -p tcp -s 0.0.0.0/0 -d 10.7.111.132 --dport 80 -j KUBE-SVC-WEBAPP
++ sudo iptables -t nat -N KUBE-SEP-WEBAPP1
++ sudo iptables -t nat -N KUBE-SEP-WEBAPP2
++ sudo iptables -t nat -A KUBE-SEP-WEBAPP1 -p tcp -s 0.0.0.0/0 -d 0.0.0.0/0 --dport 80 -j DNAT --to-destination 10.5.41.204:80
++ sudo iptables -t nat -A KUBE-SEP-WEBAPP2 -p tcp -s 0.0.0.0/0 -d 0.0.0.0/0 --dport 80 -j DNAT --to-destination 10.5.41.5:80
++ sudo iptables -t nat -A KUBE-SVC-WEBAPP -p tcp -s 0.0.0.0/0 -d 0.0.0.0/0 -m statistic --mode random --probability 0.5 -j KUBE-SEP-WEBAPP1
++ sudo iptables -t nat -A KUBE-SVC-WEBAPP -p tcp -s 0.0.0.0/0 -d 0.0.0.0/0 -m statistic --mode random --probability 1.0 -j KUBE-SEP-WEBAPP2
```

Check the rules:

```shell
$ sudo iptables -t nat -L -n
...
Chain OUTPUT (policy ACCEPT)
target     prot opt source               destination
KUBE-SERVICES  all  --  0.0.0.0/0            0.0.0.0/0

Chain KUBE-SEP-WEBAPP1 (1 references)
target     prot opt source               destination
DNAT       tcp  --  0.0.0.0/0            0.0.0.0/0            tcp dpt:80 to:10.5.41.204:80

Chain KUBE-SEP-WEBAPP2 (1 references)
target     prot opt source               destination
DNAT       tcp  --  0.0.0.0/0            0.0.0.0/0            tcp dpt:80 to:10.5.41.5:80

Chain KUBE-SERVICES (1 references)
target     prot opt source               destination
KUBE-SVC-WEBAPP  tcp  --  0.0.0.0/0            10.7.111.132         tcp dpt:80

Chain KUBE-SVC-WEBAPP (1 references)
target     prot opt source               destination
KUBE-SEP-WEBAPP1  tcp  --  0.0.0.0/0            0.0.0.0/0            statistic mode random probability 0.50000000000
KUBE-SEP-WEBAPP2  tcp  --  0.0.0.0/0            0.0.0.0/0            statistic mode random probability 1.00000000000
```

```shell
$ curl $CLUSTER_IP:$PORT
<!DOCTYPE html>
...
</html>
```

Successful!

If you compare above output with vanilla `kube-proxy` rules, these two are much
the same , following is the taken from a kube-proxy enabled node:

```shell
Chain OUTPUT (policy ACCEPT)
target         prot opt source               destination
KUBE-SERVICES  all  --  0.0.0.0/0            0.0.0.0/0            /* kubernetes service portals */

Chain KUBE-SERVICES (2 references)
target                     prot opt source               destination
KUBE-SVC-YK2SNH4V42VSDWIJ  tcp  --  0.0.0.0/0            10.7.22.18           /* default/nginx:web cluster IP */ tcp dpt:80

Chain KUBE-SVC-YK2SNH4V42VSDWIJ (1 references)
target                     prot opt source               destination
KUBE-SEP-GL2BLSI2B4ICU6WH  all  --  0.0.0.0/0            0.0.0.0/0            /* default/nginx:web */ statistic mode random probability 0.33332999982
KUBE-SEP-AIRRSG3CIF42U3PX  all  --  0.0.0.0/0            0.0.0.0/0            /* default/nginx:web */

Chain KUBE-SEP-GL2BLSI2B4ICU6WH (1 references)
target          prot opt source               destination
DNAT            tcp  --  0.0.0.0/0            0.0.0.0/0            /* default/nginx:web */ tcp to:10.244.3.181:80

Chain KUBE-SEP-AIRRSG3CIF42U3PX (1 references)
target          prot opt source               destination
DNAT            tcp  --  0.0.0.0/0            0.0.0.0/0            /* default/nginx:web */ tcp to:10.244.3.182:80
```

## 5.5 Further Re-structure the iptables rules

TODO: add rules for traffic coming from outside of cluster.

<a name="ch_6"></a>

# 6. Implementation: proxy via IPVS

Although iptables based proxy beats userspace based proxy with great performance
gain, it would also suffer from severe performance degrades when the cluster has
too much Services [6,7].

Essentially this is because **iptables verdicts are chain-based, it is a linear
algorithm with `O(n)` complexity**. A good alternative to iptables is
[**IPVS**](http://www.linuxvirtualserver.org/software/ipvs.html) - an in-kernel
L4 load balancer, which uses ipset in the underlying (hash implementation), thus
has a **complexity of `O(1)`**.

Let's see how to achieve the same goal with IPVS.

> NOTE: Before proceed on, make sure you have deleted all the rules you added in
> previous section.

## 6.1 Install IPVS

```shell
$ yum install -y ipvsadm

# -l  list load balancing status
# -n  numeric output
$ ipvsadm -ln
Prot LocalAddress:Port Scheduler Flags
  -> RemoteAddress:Port           Forward Weight ActiveConn InActConn
```

No rules by default.

## 6.2 Add virtual/real servers

Achieve load balancing with IPVS:

```shell
# -A/--add-service           add service
# -t/--tcp-service <address> VIP + Port
# -s <method>                scheduling-method
# -r/--real-server <address> real backend IP + Port
# -m                         masquerading (NAT)
$ ipvsadm -A -t $CLUSTER_IP:$PORT -s rr
$ ipvsadm -a -t $CLUSTER_IP:$PORT -r $POD1_IP -m
$ ipvsadm -a -t $CLUSTER_IP:$PORT -r $POD2_IP -m
```

or use my script:

```shell
$ ./ipvs-add-server.sh
Adding virtual server CLUSTER_IP:PORT=10.7.111.132:80 ...
Adding real servers ...
10.7.111.132:80 -> 10.5.41.204
10.7.111.132:80 -> 10.5.41.5
Done
```

Check status again:

```shell
$ ipvsadm -ln
Prot LocalAddress:Port Scheduler Flags
  -> RemoteAddress:Port           Forward Weight ActiveConn InActConn
TCP  10.7.111.132:80 rr
  -> 10.5.41.5:80                 Masq    1      0          0
  -> 10.5.41.204:80               Masq    1      0          0
```

Some explanations:

* for all traffic destinated for `10.7.111.132:80`, load balancing to
  `10.5.41.5:80` and `10.5.41.204:80`
* use round-robin (rr) algorithm for load balancing
* two backends each with weight `1` (50% of each)
* use MASQ (enhanced SNAT) for traffic forwarding between VIP and RealIPs

## 6.3 Verify

```shell
$ for i in {1..8}; do curl $CLUSTER_IP:$PORT 2>&1 >/dev/null; sleep 1; done
```

```shell
$ tcpdump -nn -i eth0 port $PORT | grep "HTTP: GET"
IP 10.21.0.7.49556 > 10.5.41.204.80: ... HTTP: GET / HTTP/1.1
IP 10.21.0.7.49558 > 10.5.41.5.80  : ... HTTP: GET / HTTP/1.1
IP 10.21.0.7.49560 > 10.5.41.204.80: ... HTTP: GET / HTTP/1.1
IP 10.21.0.7.49562 > 10.5.41.5.80  : ... HTTP: GET / HTTP/1.1
IP 10.21.0.7.49566 > 10.5.41.204.80: ... HTTP: GET / HTTP/1.1
IP 10.21.0.7.49568 > 10.5.41.5.80  : ... HTTP: GET / HTTP/1.1
IP 10.21.0.7.49570 > 10.5.41.204.80: ... HTTP: GET / HTTP/1.1
IP 10.21.0.7.49572 > 10.5.41.5.80  : ... HTTP: GET / HTTP/1.1
```

Perfect!

## 6.4 Cleanup

```shell
$ ./ipvs-del-server.sh
Deleting real servers ...
10.7.111.132:80 -> 10.5.41.204
10.7.111.132:80 -> 10.5.41.5
Deleting virtual server CLUSTER_IP:PORT=10.7.111.132:80 ...
Done
```

<a name="ch_7"></a>

# 7. Implementation: proxy via bpf

This is also an `O(1)` proxy, but has even higher performance compared with IPVS.

Let's see how to implement the proxy function with eBPF in **less than 100 lines of C code**.

## 7.1 Prerequisites

If you have enough time and interests to eBPF/BPF, consider reading through
[**Cilium: BPF and XDP Reference Guide**](
https://docs.cilium.io/en/v1.6/bpf/) (or my Chinese translation 
[here]({% link _posts/2019-10-09-cilium-bpf-xdp-reference-guide-zh.md %})),
it's a perfect documentation on BPF for developers.

## 7.2 Implementation

let's see the egress part, basic idea:

1. for all traffic, match `dst=CLUSTER_IP && proto==TCP && dport==80`
1. change destination IP: `CLUSTER_IP -> POD_IP`
1. update checksum filelds in IP and TCP headers (otherwise our packets will be
   dropped)

```c
__section("egress")
int tc_egress(struct __sk_buff *skb)
{
    const __be32 cluster_ip = 0x846F070A; // 10.7.111.132
    const __be32 pod_ip = 0x0529050A;     // 10.5.41.5

    const int l3_off = ETH_HLEN;    // IP header offset
    const int l4_off = l3_off + 20; // TCP header offset: l3_off + sizeof(struct iphdr)
    __be32 sum;                     // IP checksum

    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    if (data_end < data + l4_off) { // not our packet
        return TC_ACT_OK;
    }

    struct iphdr *ip4 = (struct iphdr *)(data + l3_off);
    if (ip4->daddr != cluster_ip || ip4->protocol != IPPROTO_TCP /* || tcp->dport == 80 */) {
        return TC_ACT_OK;
    }

    // DNAT: cluster_ip -> pod_ip, then update L3 and L4 checksum
    sum = csum_diff((void *)&ip4->daddr, 4, (void *)&pod_ip, 4, 0);
    skb_store_bytes(skb, l3_off + offsetof(struct iphdr, daddr), (void *)&pod_ip, 4, 0);
    l3_csum_replace(skb, l3_off + offsetof(struct iphdr, check), 0, sum, 0);
	l4_csum_replace(skb, l4_off + offsetof(struct tcphdr, check), 0, sum, BPF_F_PSEUDO_HDR);

    return TC_ACT_OK;
}
```

and the ingress part, quite similar to the egress code:

```c
__section("ingress")
int tc_ingress(struct __sk_buff *skb)
{
    const __be32 cluster_ip = 0x846F070A; // 10.7.111.132
    const __be32 pod_ip = 0x0529050A;     // 10.5.41.5

    const int l3_off = ETH_HLEN;    // IP header offset
    const int l4_off = l3_off + 20; // TCP header offset: l3_off + sizeof(struct iphdr)
    __be32 sum;                     // IP checksum

    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    if (data_end < data + l4_off) { // not our packet
        return TC_ACT_OK;
    }

    struct iphdr *ip4 = (struct iphdr *)(data + l3_off);
    if (ip4->saddr != pod_ip || ip4->protocol != IPPROTO_TCP /* || tcp->dport == 80 */) {
        return TC_ACT_OK;
    }

    // SNAT: pod_ip -> cluster_ip, then update L3 and L4 header
    sum = csum_diff((void *)&ip4->saddr, 4, (void *)&cluster_ip, 4, 0);
    skb_store_bytes(skb, l3_off + offsetof(struct iphdr, saddr), (void *)&cluster_ip, 4, 0);
    l3_csum_replace(skb, l3_off + offsetof(struct iphdr, check), 0, sum, 0);
	l4_csum_replace(skb, l4_off + offsetof(struct tcphdr, check), 0, sum, BPF_F_PSEUDO_HDR);

    return TC_ACT_OK;
}

char __license[] __section("license") = "GPL";
```

## 7.3 Compile and load into kernel

Now use my tiny script for compiling and loading into kernel:

```shell
$ ./compile-and-load.sh
...
++ sudo tc filter show dev eth0 egress
filter protocol all pref 49152 bpf chain 0
filter protocol all pref 49152 bpf chain 0 handle 0x1 toy-proxy-bpf.o:[egress] direct-action not_in_hw id 18 tag f5f39a21730006aa jited

++ sudo tc filter show dev eth0 ingress
filter protocol all pref 49152 bpf chain 0
filter protocol all pref 49152 bpf chain 0 handle 0x1 toy-proxy-bpf.o:[ingress] direct-action not_in_hw id 19 tag b41159c5873bcbc9 jited
```

where the script looks like:

```shell
$ cat compile-and-load.sh
set -x

NIC=eth0

# compile c code into bpf code
clang -O2 -Wall -c toy-proxy-bpf.c -target bpf -o toy-proxy-bpf.o

# add tc queuing discipline (egress and ingress buffer)
sudo tc qdisc del dev $NIC clsact 2>&1 >/dev/null
sudo tc qdisc add dev $NIC clsact

# load bpf code into the tc egress and ingress hook respectively
sudo tc filter add dev $NIC egress bpf da obj toy-proxy-bpf.o sec egress
sudo tc filter add dev $NIC ingress bpf da obj toy-proxy-bpf.o sec ingress

# show info
sudo tc filter show dev $NIC egress
sudo tc filter show dev $NIC ingress
```

## 7.4 Verify

```shell
$ curl $CLUSTER_IP:$PORT
<!DOCTYPE html>
...
</html>
```

Perfect!

## 7.5 Cleanup

```shell
$ sudo tc qdisc del dev $NIC clsact 2>&1 >/dev/null
```

## 7.6 Explanations

TODO.

See some of my previous posts for bpf/cilium.

<a name="ch_8"></a>

# 8. Summary

In this post, we manually realized the core functionalities of `kube-proxy` with
different means. Hope now you have a better understanding about kubernetes node
proxy, and some other apsects about networking.

Code and scripts used in this post: [here](https://github.com/ArthurChiao/arthurchiao.github.io/tree/master/assets/img/cracking-k8s-node-proxy).

<a name="references"></a>

## References

1. [Kubernetes Doc: CLI - kube-proxy](https://kubernetes.io/docs/reference/command-line-tools-reference/kube-proxy/)
2. [kubernetes/enhancements: enhancements/0011-ipvs-proxier.md](https://github.com/kubernetes/enhancements/blob/master/keps/sig-network/0011-ipvs-proxier.md)
3. [Kubernetes Doc: Service types](https://kubernetes.io/docs/concepts/services-networking/service/#publishing-services-service-types)
4. [Proxies in Kubernetes - Kubernetes](https://kubernetes.io/docs/concepts/cluster-administration/proxies/)
5. [A minimal IPVS Load Balancer demo](https://medium.com/@benmeier_/a-quick-minimal-ipvs-load-balancer-demo-d5cc42d0deb4)
6. [Scaling Kubernetes to Support 50,000 Services](https://docs.google.com/presentation/d/1BaIAywY2qqeHtyGZtlyAp89JIZs59MZLKcFLxKE6LyM/edit#slide=id.p3)
7. [华为云在 K8S 大规模场景下的 Service 性能优化实践](https://zhuanlan.zhihu.com/p/37230013)

<a name="appendix"></a>

# Appendix

`webapp.yaml`:

```yaml
apiVersion: v1
kind: Service
metadata:
  name: webapp
  labels:
    app: webapp
spec:
  ports:
  - port: 80
    name: web
  selector:
    app: webapp
---
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: webapp
spec:
  serviceName: "webapp"
  replicas: 1
  selector:
    matchLabels:
      app: webapp
  template:
    metadata:
      labels:
        app: webapp
    spec:
      # affinity:
      #   nodeAffinity:
      #     requiredDuringSchedulingIgnoredDuringExecution:
      #       nodeSelectorTerms:
      #       - matchExpressions:
      #         - key: kubernetes.io/hostname
      #           operator: In
      #           values:
      #           - node1
      tolerations:
      - effect: NoSchedule
        operator: Exists   # this will effectively tolerate any taint
      containers:
      - name: webapp
        image: nginx-slim:0.8
        ports:
        - containerPort: 80
          name: web
```
