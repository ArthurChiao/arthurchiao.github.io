---
layout    : post
title     : "TCP Requests Stuck After Connection Established（2024）"
date      : 2024-06-26
lastupdate: 2024-06-26
categories: kernel bpf cilium
---

This post describes a kernel & BPF networking problem
and the trouble shooting steps, which is an interesting case for delving into
Linux kernel networking intricacies.

<p align="center"><img src="/assets/img/tcp-stuck-after-connection-established/testcase-comparison.png" width="90%"/></p>
<p align="center">Fig. Phenomenon of a reported issue.</p>

----

* TOC
{:toc}

----

# 1 Trouble report

## 1.1 Phenomenon: **<mark><code>probabilistic health check failures</code></mark>**

Users reported **<mark><code>intermittent failures</code></mark>** of their pods,
despite them run as usual with no exceptions.

The health check is a **<mark><code>very simple HTTP probe over TCP</code></mark>**:
kubelet periodically (e.g. every 5s) sends `GET` requests to local pods,
initiating a new TCP connection with each request.

<p align="center"><img src="/assets/img/tcp-stuck-after-connection-established/health-check.png" width="60%"/></p>
<p align="center">Fig. Intermittent health check failures of pods.</p>

Users suspect this is a network problem.

## 1.2 Scope: specific pods on specific nodes

This reported issue is confined to a new k8s cluster, with **<mark><code>recently introduced OS and kernel</code></mark>**:

* OS: AliOS (AlibabaCloud OS)
* Kernel: `cloud-kernel` 5.10.134-16.al8.x86_64 (**<mark><code>a fork of Linux</code></mark>**,
  [gitee.com/anolis/cloud-kernel](https://gitee.com/anolis/cloud-kernel)), which includes their
  upstream feature backports and self-maintanined changes, for example,

    1. **<mark><code>Intel AMX</code></mark>** (Advanced Matrix Extensions) for **<mark><code>AI workloads</code></mark>**,
       offering a hardware acceleration alternative to GPUs in certain scenarios,
       such as inference for LLMs smaller than 13B.
       AMX support was first [introduced in kernel 5.16](https://lwn.net/Articles/875733/),
       `cloud-kernel` backported the feature to its current version **<mark><code>5.10</code></mark>**;
    2. `cloud-kernel` includes un-upstreamed modifications like new kernel structure fields and new enums/types.

Other environment info:

* Cilium: self-maintained v1.11.10
* [CNCF Case Study: How Trip.com Group switched to Cilium For Scalable and Cloud Native Networking](https://www.cncf.io/case-studies/trip-com-group/), 2023

# 2 Networking fundamentals

Before starting our exploration, let's outline our networking infra in this cluster.

## 2.1 Node network topology: **<mark><code>Cilium (with BPF)</code></mark>**

Internal networking topology of our k8s node is depicted as below:

<p align="center"><img src="/assets/img/tcp-stuck-after-connection-established/node-internal-networking-topo.png" width="70%"/></p>
<p align="center">Fig. Internal networking topology of a k8s node.</p>

```shell
(k8s node) $ route -n
Destination  Gateway   Genmask           Use Iface
0.0.0.0      <GW-IP>   0.0.0.0           eth0
<Node-IP>    0.0.0.0   <Node-IP-Mask>    eth0
<Pod1-IP>    0.0.0.0   255.255.255.255   lxc-1
<Pod2-IP>    0.0.0.0   255.255.255.255   lxc-2
<Pod3-IP>    0.0.0.0   255.255.255.255   lxc-3
```

As shown in the picture and kernel routing table output, **<mark><code>each pod has a dedicated routing entry</code></mark>**.
Consequently, all health check traffic is directed straight to the lxc device
(the host-side device of the pod's veth pair), subsequently entering the Pod.
In another word, **<mark><code>all the health check traffic is processed locally</code></mark>**.

> Cilium has a similar networking topology on AlibabaCloud as on AWS.
> For more information, refer to
> [<mark>Cilium Network Topology and Traffic Path on AWS</mark> (2019)]({% link _posts/2019-10-26-cilium-network-topology-on-aws.md %}),
> which may contain some stale information, but most of the content should still validate.

## 2.2 Kernel `5.10+`: **<mark><code>sockmap BPF</code></mark>** acceleration for **<mark><code>node2localPod</code></mark>** traffic

### 2.2.1 `sockops` BPF: bypass kernel stack for local traffic

<p align="center"><img src="/assets/img/socket-acceleration-with-ebpf/bpf-kernel-hooks.png" width="50%" height="50%"></p>

[How to use eBPF for accelerating Cloud Native
applications](https://cyral.com/blog/how-to-ebpf-accelerating-cloud-native/)
offers a practical example of how sockops/sockmap BPF programs work.

Chinese readers can also refer to the following for more information,

1. [（译）利用 ebpf sockmap/redirection 提升 socket 性能（2020）]({% link _posts/2021-01-28-socket-acceleration-with-ebpf-zh.md %})
1. [BPF 进阶笔记（五）：几种 TCP 相关的 BPF（sockops、struct_ops、header options）]({% link _posts/2021-07-04-bpf-advanced-notes-5-zh.md %})

### 2.2.2 `tcpdump`: only TCP `3-way/4-way` handshake packets can be captured

`sockops` acceleration is automatically enabled in kernel 5.10 + Cilium v1.11.10:

<p align="center"><img src="/assets/img/socket-acceleration-with-ebpf/sock-redir.png" width="70%" height="70%"></p>
<p align="center">Fig. Socket-level acceleration in Cilium.
Note that the illustration depicts local processes communicating via loopback, which differs from the scenario discussed here,
just too lazy draw a new picture.</p>

One big conceptual change is that when sockops BPF is enabled, you could not
see request & response packets in tcpdump output, as in this setup, only TCP
**<mark><code>3-way handshake and 4-way close procedure</code></mark>** still go through kernel networking
stack, all the payload will directly go through the socket-level (e.g. in
tcp/udp send/receive message) methods.

A quick test to illustrate the idea: access a server in pod from the node:

```shell
(node) $ curl <pod ip>:<port>
```

The output of tcpdump:

```shell
(pod) $ tcpdump -nn -i eth0 host <node ip> and <port>
# TCP 3-way handshake
IP NODE_IP.36942 > POD_IP.8080: Flags [S]
IP POD_IP.8080   > NODE_IP.36942: Flags [S.]
IP NODE_IP.36942 > POD_IP.8080: Flags [.]

# requests & responses, no packets go through there, they are bypassed,
# payloads are transferred directly in socket-level TCP methods

# TCP 4-way close
IP POD_IP.8080   > NODE_IP.36942: Flags [F.]
IP NODE_IP.36942 > POD_IP.8080: Flags [.]
IP NODE_IP.36942 > POD_IP.8080: Flags [F.]
IP POD_IP.8080   > NODE_IP.36942: Flags [.]
```

## 2.3 Summary

Now we've got a basic undertanding about the problem and environment.
It's time to delve into practical investigation.

# 3 Quick narrow-down

## 3.1 Quick reproduction

First, check kubelet log,

```shell
$ grep "Timeout exceeded while awaiting headers" /var/log/kubernetes/kubelet.INFO
prober.go] Readiness probe for POD_XXX failed (failure):
  Get "http://POD_IP:PORT/health": context deadline exceeded (Client.Timeout exceeded while awaiting headers)
...
```

Indeed, there are many readiness probe failures.

Since the probe is very simple HTTP request, we can do it manually on the node,
this should be **<mark><code>equivalent to the kubelet probe</code></mark>**,

```shell
$ curl <POD_IP>:<PORT>/v1/health
OK
$ curl <POD_IP>:<PORT>/v1/health
OK
$ curl <POD_IP>:<PORT>/v1/health # stuck
^C
```

OK, we can easily reproduce it without relying on k8s facilities.

## 3.2 Narrow-down the issue

Now let's perform some quick tests to narrow-down the problem.

### 3.2.1 `ping`: OK, exclude L2/L3 problem

`ping` PodIP from node **<mark><code>always succeeds</code></mark>**.

```shell
(node) $ ping <POD_IP>
```

This indicates L2 & L3 (ARP table, routing table, etc) connectivity functions well.

### 3.2.2 `telnet` connection test: OK, exclude TCP connecting problem

```shell
(node) $ telnet POD_IP PORT
Trying POD_IP...
Connected to POD_IP.
Escape character is '^]'.
```

Again, always succeeds, and the `ss` output confirms the connections always enter **<mark><code>ESTABLISHED</code></mark>** state:

```shell
(node) $ netstat -antp | grep telnet
tcp        0      0 NODE_IP:34316    POD_IP:PORT     ESTABLISHED 2360593/telnet
```

### 3.2.3 Remote-to-localPod `curl`: OK, exclude pod problem & vanilla kernel stack problem

Do the same health check from a remote node, always OK:

```shell
(node2) $ curl <POD_IP>:<PORT>/v1/health
OK
...
(node2) $ curl <POD_IP>:<PORT>/v1/health
OK
```

This rules out issues with the pod itself and the vanilla kernel stack.

### 3.2.4 Local pod-to-pod: OK, exclude some node-internal problems

```shell
(pod3) $ curl <POD2_IP>:<PORT>/v1/health
OK
...
(pod3) $ curl <POD2_IP>:<PORT>/v1/health
OK
```

Always OK. Rule out issues with the pod itself.

## 3.3 Summary: **<mark><code>only node-to-localPod TCP requests stuck probabilistically</code></mark>**

<p align="center"><img src="/assets/img/tcp-stuck-after-connection-established/testcase-comparison.png" width="90%"/></p>
<p align="center">Fig. Test cases and results.</p>

The difference of three cases:

1. **<mark><code>Node-to-localPod</code></mark>**: payload traffic is processed via **<mark><code>sockops BPF</code></mark>**;
2. **<mark><code>Local Pod-to-Pod</code></mark>**: **<mark><code>BPF redirection</code></mark>** (or kernel stack, based on your kernel version)
    * [Differentiate three types of eBPF redirections (2022)]({% link _posts/2022-07-25-differentiate-bpf-redirects.md %})
3. **<mark><code>RemoteNode-to-localPod</code></mark>**: standard kernel networking stack

Combining these information, we guess with confidence that the problem have
relationships with sockops BPF and kernel (because kernel does most of the job in sockops BPF scenarios).

From these observations, it is reasonable to deduce that the issue is likely
related to **<mark><code>sockops BPF and the kernel</code></mark>**, given the
kernel's central role in sockops BPF scenarios.

# 4 Dig deeper

Now let's explore the issue in greater depth.

## 4.1 Linux vs. AliOS kernel

As we've been using kernel **<mark><code>5.10.56</code></mark>** and cilium
v1.11.10 for years and haven't met this problem before, the first reasonable
assumption is that AliOS cloud-kernel 5.10.134 may introduce some incompatible changes or bugs.

So we spent some time comparing AliOS cloud-kernel with the upstream Linux.

> Note: cloud-kernel is maintained on
> gitee.com, which restricts most read privileges (e.g. commits, blame) without logging in,
> so in the remaining of this post we reference the Linux repo on github.com for discussion.

### 4.1.1 Compare BPF features

First, compare BPF features automatically detected by cilium-agent on the node.
The result is written to a local file on the node:
**<mark><code>/var/run/cilium/state/globals/bpf_features.h</code></mark>**,

```shell
$ diff <bpf_features.h from our 5.10.56 node> <bpf_features.h from AliOS node>
```

```diff
59c59
< #define NO_HAVE_XSKMAP_MAP_TYPE
---
> #define HAVE_XSKMAP_MAP_TYPE
71c71
< #define NO_HAVE_TASK_STORAGE_MAP_TYPE
---
> #define HAVE_TASK_STORAGE_MAP_TYPE
243c243
< #define BPF__PROG_TYPE_socket_filter__HELPER_bpf_ktime_get_coarse_ns 0
---
> #define BPF__PROG_TYPE_socket_filter__HELPER_bpf_ktime_get_coarse_ns 1
...
```

There are indeed some differences, but with further investigation, we didn't
find any correlation to the observed issue.

### 4.1.2 AliOS cloud-kernel specific changes

Then we spent some time to check AliOS cloud-kernel self-maintained BPF and networking modifications.
Such as,

1. `b578e4b8ed6e1c7608e07e03a061357fd79ac2dd` ck: net: track the pid who created socks

    In this commit, they added a `pid_t pid` field to the **<mark><code>struct sock</code></mark>** data structure.

2. `ea0307caaf29700ff71467726b9617dcb7c0d084` tcp: make sure init the accept_queue's spinlocks once

But again, we didn't find any correlation to the problem.

## 4.2 Check detailed TCP connection stats

Without valuable information from code comparison, we redirected our focus to the environment,
collecting some more detailed connection information.

`ss` (socket stats) is a powerful and convenient tool for socket/connection introspection:

* **<mark><code>-i/--info</code></mark>**: show internal TCP information, including couple of TCP connection stats;
* **<mark><code>-e/--extended</code></mark>**: show detailed socket information, including inode, uid, cookie.

### 4.2.1 Normal case: `ss` shows correct `segs_out/segs_in`

Initiate a connection with `nc` (netcat),

```shell
(node) $ nc POD_IP PORT
```

We **<mark><code>intentionally not use telnet here</code></mark>**, because `telnet` will close the connection
immediately after a request is served successfully, which leaves us **<mark><code>no time to check the connection stats in <code>ss</code> output</code></mark>**.
`nc` will leave the connection in **<mark><code>CLOSE-WAIT</code></mark>** state, which is good enough for us to check the connection send/receive stats.

Now the stats for this connection:

```shell
(node) $ ss -i | grep -A 1 50504
tcp    ESTAB      0         0         NODE_IP:50504          POD_IP:PORT
         cubic wscale:7,7 rto:200 rtt:0.059/0.029 mss:1448 pmtu:1500 rcvmss:536 advmss:1448 cwnd:10 bytes_acked:1 segs_out:2 segs_in:1 send 1963.4Mbps lastsnd:14641 lastrcv:14641 lastack:14641 pacing_rate 3926.8Mbps delivered:1 rcv_space:14480 rcv_ssthresh:64088 minrtt:0.059
```

Send & receive stats: **<mark><code>segs_out=2, segs_in=1</code></mark>**.

Now let's send a request to the server: type `GET /v1/health HTTP/1.1\r\n` then press `Enter`,

> Actually you can type anything and just `Enter`, the server will most likely
> send you a **<mark><code>400</code></mark>** (Bad Request) response, but for
> our case, this 400 indicate the TCP send/receive path is perfectly OK!

```shell
(node) $ nc POD_IP PORT
GET /v1/health HTTP/1.1\r\n
<Response Here>
```

We'll get the response and the connection will just entering `CLOSE-WAIT` state,
we have some time to check it before it vanishing:

```shell
(node) $ ss -i | grep -A 1 50504
tcp     CLOSE-WAIT   0      0        NODE_IP:50504     POD_IP:http
         cubic wscale:7,7 rto:200 rtt:0.059/0.029 ato:40 mss:1448 pmtu:1500 rcvmss:536 advmss:1448 cwnd:10 bytes_acked:1 bytes_received:1 segs_out:3 segs_in:2 send 1963.4Mbps lastsnd:24277 lastrcv:24277 lastack:4399 pacing_rate 3926.8Mbps delivered:1 rcv_space:14480 rcv_ssthresh:64088 minrtt:0.059
```

As expected, **<mark><code>segs_out=3, segs_in=2</code></mark>**.

### 4.2.2 Abnormal case: `ss` shows incorrect `segs_out/segs_in`

Repeat the above test to capture a failed one.

On connection established,

```shell
$ ss -i | grep -A 1 57424
tcp      ESTAB      0       0         NODE_IP:57424    POD_IP:webcache
         cubic wscale:7,7 rto:200 rtt:0.056/0.028 mss:1448 pmtu:1500 rcvmss:536 advmss:1448 cwnd:10 bytes_acked:1 segs_out:2 segs_in:1 send 2068.6Mbps lastsnd:10686 lastrcv:10686 lastack:10686 pacing_rate 4137.1Mbps delivered:1 rcv_space:14480 rcv_ssthresh:64088 minrtt:0.056
```

After typing the request content and stroking `Enter`:

```shell
(node) $ ss -i | grep -A 1 57424
tcp      ESTAB      0       0         NODE_IP:57424    POD_IP:webcache
         cubic wscale:7,7 rto:200 rtt:0.056/0.028 mss:1448 pmtu:1500 rcvmss:536 advmss:1448 cwnd:10 bytes_acked:1 segs_out:2 segs_in:1 send 2068.6Mbps lastsnd:21994 lastrcv:21994 lastack:21994 pacing_rate 4137.1Mbps delivered:1 rcv_space:14480 rcv_ssthresh:64088 minrtt:0.056
```

That segments sent/received stats remain **<mark><code>unchanged</code></mark>** (`segs_out=2,segs_in=1`),
suggesting that the problem may reside on **<mark><code>tcp {send,receive} message level</code></mark>**.

## 4.3 Trace related call stack

Based on the above hypothesis, we captured kernel call stacks
to compare failed and successful requests.

### 4.3.1 `trace-cmd`: trace kernel call stacks

Trace 10 seconds, filter by server process ID, save the calling stack graph,

```shell
# filter by process ID (PID of the server in the pod)
$ trace-cmd record -P 178501 -p function_graph sleep 10
```

> **<mark><code>Caution</code></mark>**: avoid tracing in production to prevent large file generation and excessive disk IO.

During this period, send a request,

```shell
(node) $ curl POD_IP PORT
```

By default, it will save data to a local file in the current directory, the content looks like this:

```shell
$ trace-cmd report > report-1.graph
CPU  1 is empty
CPU  2 is empty
...
CPU 63 is empty
cpus=64
   <idle>-0     [022] 5376816.422992: funcgraph_entry:    2.441 us   |  update_acpu.constprop.0();
   <idle>-0     [022] 5376816.422994: funcgraph_entry:               |  switch_mm_irqs_off() {
   <idle>-0     [022] 5376816.422994: funcgraph_entry:    0.195 us   |    choose_new_asid();
   <idle>-0     [022] 5376816.422994: funcgraph_entry:    0.257 us   |    load_new_mm_cr3();
   <idle>-0     [022] 5376816.422995: funcgraph_entry:    0.128 us   |    switch_ldt();
   <idle>-0     [022] 5376816.422995: funcgraph_exit:     1.378 us   |  }
...
```

Use `|` as delimiter (this preserves the calling stack and the proper leading whitespaces) and save the last fields into a dedicated file:

```shell
$ awk -F'|' '{print $NF}' report-1.graph > stack-1.txt
```

Compare them with `diff` or **<mark><code>vimdiff</code></mark>**:

```shell
$ vimdiff stack-1.txt stack-2.txt
```

Here are two traces, the left is a trace of a normal request, and the right is a problematic one:

<p align="center"><img src="/assets/img/tcp-stuck-after-connection-established/trace-vimdiff.png" width="90%"/></p>
<p align="center">Fig. Traces (call stacks) of a normal request (left side) and a problematic request (right side).</p>

As can be seen, for a failed request, **<mark>kernel made a wrong function call</mark>**:
it should call `tcp_bpf_recvmsg()` but actually called `tcp_recvmsg()`.

### 4.3.2 Locate the code: **<mark><code>inet_recvmsg -> {tcp_bpf_recvmsg, tcp_recvmsg}</code></mark>**

Calling into `tcp_bpf_recvmsg` or `tcp_recvmsg` from `inet_recvmsg` is a piece of concise code,
illustrated below,

```c
// https://github.com/torvalds/linux/blob/v5.10/net/ipv4/af_inet.c#L838
int inet_recvmsg(struct socket *sock, struct msghdr *msg, size_t size, int flags) {
    struct sock *sk = sock->sk;
    int addr_len = 0;
    int err;

    if (likely(!(flags & MSG_ERRQUEUE)))
        sock_rps_record_flow(sk);

    err = INDIRECT_CALL_2(sk->sk_prot->recvmsg, tcp_recvmsg, udp_recvmsg,
                  sk, msg, size, flags & MSG_DONTWAIT,
                  flags & ~MSG_DONTWAIT, &addr_len);
    if (err >= 0)
        msg->msg_namelen = addr_len;
    return err;
}
```

`sk_prot` (<mark><code>"socket protocol"</code></mark>) contains handlers to this socket.
**<mark><code>INDIRECT_CALL_2</code></mark>** line can be simplified into the following pseudocode:

```c
if sk->sk_prot->recvmsg == tcp_recvmsg: // if socket protocol handler is tcp_recvmsg
    tcp_recvmsg()
else:
    tcp_bpf_recvmsg()
```

This suggests that when requests fail, the **<mark><code>sk_prot->recvmsg</code></mark>**
pointer of the socket is likely incorrect.

### 4.3.3 Double check with bpftrace

While `trace-cmd` is a powerful tool, it may contain too much details distracting us, and
may run out of your disk space if set improper filter parameters.

`bpftrace` is a another tracing tool, and it won't write data to local file by default.
Now let's double confirm the above results with it.

Again, run several times of `curl POD_IP:PORT`, capture only tcp_recvmsg and tcp_bpf_recvmsg calls,
print kernel calling stack:

```shell
$ bpftrace -e 'k:tcp_recvmsg /pid==178501/ { printf("%s\n", kstack);} k:tcp_bpf_recvmsg /pid==178501/ { printf("%s\n", kstack);} '
        tcp_bpf_recvmsg+1                   # <-- correspond to a successful request
        inet_recvmsg+233
        __sys_recvfrom+362
        __x64_sys_recvfrom+37
        do_syscall_64+48
        entry_SYSCALL_64_after_hwframe+97

        tcp_bpf_recvmsg+1                   # <-- correspond to a successful request
        inet_recvmsg+233
        __sys_recvfrom+362
        __x64_sys_recvfrom+37
        do_syscall_64+48
        entry_SYSCALL_64_after_hwframe+97

        tcp_recvmsg+1                       # <-- correspond to a failed request
        inet_recvmsg+78
        __sys_recvfrom+362
        __x64_sys_recvfrom+37
        do_syscall_64+48
        entry_SYSCALL_64_after_hwframe+97
```

> You could also filter by client program name (**<mark><code>comm</code></mark>** field in kernel data structure), such as,
>
> ```shell
> $ bpftrace -e 'k:tcp_bpf_recvmsg /comm=="curl"/ { printf("%s", kstack); }'
> ```

As seen above, successful requests were directed to `tcp_bpf_recvmsg`, while failed ones were routed to `tcp_recvmsg`.

### 4.3.4 Summary

`tcp_recvmsg` **<mark><code>waits messages from kernel networking stack</code></mark>**,
In the case of sockops BPF, messages bypass kernel stack, which explains why
**<mark><code>some requests fail (timeout), yet TCP connecting always OK</code></mark>**.

We reported the above findings to the `cloud-kernel` team, and they did some further investigations with us.

## 4.4 `recvmsg` handler initialization in kernel stack

For short,

<p align="center"><img src="/assets/img/tcp-stuck-after-connection-established/sockops-init-on-connection-estab.png" width="100%"/></p>
<p align="center">Fig. sockops BPF: connection establishement and socket handler initialization.</p>

According to the above picture, `recvmsg` handler will be
**<mark><code>incorrectly initialized if to-be-inserted entry already exists sockmap</code></mark>**
(the end of step 3.1).

What's the two entries of a connection looks like in BPF map:

```shell
(cilium-agent) $ bpftool map dump id 122 | grep "0a 0a 86 30" -C 2 | grep "0a 0a 65 f9" -C 2 | grep -C 2 "db 78"
0a 0a 86 30 00 00 00 00  00 00 00 00 00 00 00 00
0a 0a 65 f9 00 00 00 00  00 00 00 00 00 00 00 00
01 00 00 00 1f 90 00 00  db 78 00 00
--
key:
--
0a 0a 65 f9 00 00 00 00  00 00 00 00 00 00 00 00
0a 0a 86 30 00 00 00 00  00 00 00 00 00 00 00 00
01 00 00 00 db 78 00 00  1f 90 00 00
```

We'll explain these binary data later.
Now let's first confirm our above assumption.

## 4.5 Confirm stale entries in sockmap

### 4.5.1 `bpftrace` `tcp_bpf_get_prot()`: incorrect socket handler (sk_prot)

Two sequent function calls that holding `sk_port`:

* `tcp_bpf_get_prot()`: where `sk_prot` is **<mark><code>initialized</code></mark>**;
* `tcp_bpf_recvmsg()` or `tcp_recvmsg()`: where `sk_prot` is called to **<mark><code>receive a message</code></mark>**;

Trace these two methods and print the `sk_prot` variable (pointer).

Successful case:

```shell
tcp_bpf_get_proto: src POD_IP (8080), dst NODE_IP(59500), 2232440
tcp_bpf_get_proto: 0xffffffffacc65800                                     # <-- sk_prot pointer
tcp_bpf_recvmsg: src POD_IP (8080), dst NODE_IP(59500) 0xffffffffacc65800 # <-- same pointer
```

Bad case:

```shell
(node) $ ./tcp_bpf_get_proto.bt 178501
Attaching 6 probes...
tcp_bpf_get_proto: src POD_IP (8080), dst NODE_IP(53904), 2231203
tcp_bpf_get_proto: 0xffffffffacc65800                                    # <-- sk_prot pointer
tcp_recvmsg: src POD_IP (8080), dst NODE_IP(53904) 0xffffffffac257300    # <-- sk_prot is modified!!!
```

### 4.5.2 `bpftrace` `sk_psock_drop`

A succesful case, calling into `sk_psock_drop` when requests finish and connection was normally closed:

```shell
(node) $ ./sk_psock_drop.bt 178501
tcp_bpf_get_proto: src POD_IP (8080), dst NODE_IP(59500), 2232440
tcp_bpf_get_proto: 0xffffffffacc65800                                    # <-- sk_prot pointer
sk_psock_drop: src POD_IP (8080)， dst NODE_IP(44566)
    sk_psock_drop+1
    sock_map_remove_links+161
    sock_map_close+50
    inet_release+63
    sock_release+58
    sock_close+17
    fput+147
    task_work_run+89
    exit_to_user_mode_loop+285
    exit_to_user_mode_prepare+110
    syscall_exit_to_user_mode+18
    entry_SYSCALL_64_after_hwframe+97
tcp_bpf_recvmsg: src POD_IP (8080), dst NODE_IP(59500) 0xffffffffacc65800 # <-- same pointer
```

A failed case, calling into `sk_psock_drop` when the server side calls
`sock_map_update()` and **<mark><code>the to-be-inserted entry already exists</code></mark>**:

```shell
(node) $ ./sk_psock_drop.bt 178501
tcp_bpf_get_proto: src POD_IP (8080), dst NODE_IP(53904), 2231203
tcp_bpf_get_proto: 0xffffffffacc65800                                    # <-- sk_prot pointer
sk_psock_drop: src POD_IP (8080)， dst NODE_IP(44566)
    sk_psock_drop+1
    sock_hash_update_common+789
    bpf_sock_hash_update+98
    bpf_prog_7aa9a870410635af_bpf_sockmap+831
    _cgroup_bpf_run_filter_sock_ops+189
    tcp_init_transfer+333                       // -> bpf_skops_established -> BPF_CGROUP_RUN_PROG_SOCK_OPS -> cilium sock_ops code
    tcp_rcv_state_process+1430
    tcp_child_process+148
    tcp_v4_rcv+2491
    ...
tcp_recvmsg: src POD_IP (8080), dst NODE_IP(53904) 0xffffffffac257300    # <-- sk_prot is modified!!!
```

```c
// https://github.com/torvalds/linux/blob/v6.5/net/core/sock_map.c#L464

static int sock_map_update_common(struct bpf_map *map, u32 idx, struct sock *sk, u64 flags) {
    struct bpf_stab *stab = container_of(map, struct bpf_stab, map);
    ...

    link = sk_psock_init_link();
    sock_map_link(map, sk);
    psock = sk_psock(sk);

    osk = stab->sks[idx];
    if (osk && flags == BPF_NOEXIST) {     // sockmap entries already exists
        ret = -EEXIST;
        goto out_unlock;                   // goto out_unlock, which will release psock
    } else if (!osk && flags == BPF_EXIST) {
        ret = -ENOENT;
        goto out_unlock;
    }

    sock_map_add_link(psock, link, map, &stab->sks[idx]);
    stab->sks[idx] = sk;
    if (osk)
        sock_map_unref(osk, &stab->sks[idx]);
    return 0;                              // <-- should return from here
out_unlock:                                // <-- actually hit here
    if (psock)
        sk_psock_put(sk, psock);           // --> further call sk_psock_drop
out_free:
    sk_psock_free_link(link);
    return ret;
}
```

This **<mark><code>early release</code></mark>** of `psock` leads to the `sk->sk_prot->recvmsg` to be initialized as `tcp_recvmsg`.

### 4.5.3 bpftool: confirm stale connection info in sockops map

Key and value format in the BPF map:

```go
// https://github.com/cilium/cilium/blob/v1.11.10/pkg/maps/sockmap/sockmap.go#L20

// SockmapKey is the 5-tuple used to lookup a socket
// +k8s:deepcopy-gen=true
// +k8s:deepcopy-gen:interfaces=github.com/cilium/cilium/pkg/bpf.MapKey
type SockmapKey struct {
    DIP    types.IPv6 `align:"$union0"`
    SIP    types.IPv6 `align:"$union1"`
    Family uint8      `align:"family"`
    Pad7   uint8      `align:"pad7"`
    Pad8   uint16     `align:"pad8"`
    SPort  uint32     `align:"sport"`
    DPort  uint32     `align:"dport"`
}

// SockmapValue is the fd of a socket
// +k8s:deepcopy-gen=true
// +k8s:deepcopy-gen:interfaces=github.com/cilium/cilium/pkg/bpf.MapValue
type SockmapValue struct {
    fd uint32
}
```

[Trip.com: Large Scale Cloud Native Networking & Security with Cilium/eBPF, 2022]({% link _posts/2022-09-28-trip-large-scale-cloud-native-networking-and-security-with-cilium-ebpf.md %})
shows how to decode the encoded entries of Cilium BPF map.

```shell
$ cat ip2hex.sh
echo $1 | awk -F. '{printf("%02x %02x %02x %02x\n",$1,$2,$3,$4);}'
$ cat hex2port.sh
echo $1 | awk '{printf("0x%s%s 0x%s%s\n", $1, $2, $5, $6) }' | sed 's/ /\n/g' | xargs -n1 printf '%d\n'

(node) $ ./ip2hex.sh "10.10.134.48"
0a 0a 86 30
(node) $ ./ip2hex.sh "10.10.101.249"
0a 0a 65 f9
```

```shell
(cilium-agent) $ bpftool map dump id 122 | grep "0a 0a 86 30" -C 2 | grep "0a 0a 65 f9" -C 2 | grep -C 2 "db 78"
0a 0a 86 30 00 00 00 00  00 00 00 00 00 00 00 00
0a 0a 65 f9 00 00 00 00  00 00 00 00 00 00 00 00
01 00 00 00 1f 90 00 00  db 78 00 00
--
key:
--
0a 0a 65 f9 00 00 00 00  00 00 00 00 00 00 00 00
0a 0a 86 30 00 00 00 00  00 00 00 00 00 00 00 00
01 00 00 00 db 78 00 00  1f 90 00 00
```

```shell
(node) $ ./hex2port.sh "1f 90 00 00  b6 8a 00 00"
8080
46730 # you can verify this connection in `ss` output
```

Almost all of the following entries are stale (because this is an empty, no node-to-pod traffic unless we do manually):

```shell
(cilium-agent) $ bpftool map dump /sys/fs/bpf/cilium_sock_ops | grep "0a 0a 86 30" | wc -l
7325
(cilium-agent) $ bpftool map dump /sys/fs/bpf/cilium_sock_ops | grep "0a 0a 8c ca" | wc -l
1288
(cilium-agent) $ bpftool map dump /sys/fs/bpf/cilium_sock_ops | grep "0a 0a 8e 40" | wc -l
191
```

# 5 Technical summary

## 5.1 Normal sockops/sockmap BPF workflow

<p align="center"><img src="/assets/img/tcp-stuck-after-connection-established/sockops-init-on-connection-estab.png" width="100%"/></p>
<p align="center">Fig. sockops BPF: connection establishement and socket handler initialization.</p>

1. Node client (e.g. kubelet) -> server: **<mark><code>initiate TCP connection</code></mark>** to the server
2. Kernel (and the BPF code in kernel): on listening on connection established
    1. **<mark><code>write two entries to sockmap</code></mark>**
    1. **<mark><code>link entries to bpf handlers</code></mark>** (`tcp_bpf_{sendmsg, recvmsg}`)
3. Node client (e.g. kubelet) -> server: send & receive payload: BPF handlers were executed
4. Node client (e.g. kubelet) -> server: close connection: kernel **<mark><code>removes entries from sockmap</code></mark>**

## 5.2 Direct cause

The problem arises in step 4, for an unknown reason, some entries are not deleted when connections closed. This
leads to incorrect handler initialization in new connections in step 2 (or section **<mark><code>3.1</code></mark>** in the picture).
When hit a stale entry,

* sender side uses **<mark><code>BPF message handlers</code></mark>** for transmission;
* server side treats the the socket as standard, and waits for message via
  **<mark><code>default message handler</code></mark>**, then **<mark><code>stucks</code></mark>**
  there as no payload goes to default handler.

## 5.3 Root cause: TBD

Reason for why kernel failed to delete those entries (or delete them failed) is still under investigation.
We would like to thank Alibaba `cloud-kernel` team for their help.

sockmap entries (kind of BPF objects) are deleted in a gabarge collection (GC) mechanism,
which indicates that those stale entries may still be hold by some kernel objects, such as socket objects.

## 5.4 Restore/remediation methods

If the issue already happened, you can use one of the following methods to restore:

1. **<mark><code>Kernel restart</code></mark>**: drain the node then restart it, thish will refresh the kernel state;
2. **<mark><code>Manual clean</code></mark>** with `bpftool`: with caution, avoid to remove valid entries.

# Appendix

* [bpftrace scripts used in this post](https://github.com/ArthurChiao/arthurchiao.github.io/tree/master/assets/code/tcp-stuck-after-connection-established)

# References

1. AliOS kernel (a Linux fork), [gitee.com/anolis/cloud-kernel](https://gitee.com/anolis/cloud-kernel)
1. [Cilium Network Topology and Traffic Path on AWS (2019)]({% link _posts/2019-10-26-cilium-network-topology-on-aws.md %})
1. cilium v1.11.10, [bpf_sockops.c](https://github.com/cilium/cilium/blob/v1.11.10/bpf/sockops/bpf_sockops.c)
1. cilium v1.11.10, [bpf sockops key & value definition](https://github.com/cilium/cilium/blob/v1.11.10/pkg/maps/sockmap/sockmap.go#L20)
1. [Differentiate three types of eBPF redirections]({% link _posts/2022-07-25-differentiate-bpf-redirects.md %})
1. [Trip.com: Large Scale Cloud Native Networking & Security with Cilium/eBPF, 2022]({% link _posts/2022-09-28-trip-large-scale-cloud-native-networking-and-security-with-cilium-ebpf.md %})

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
