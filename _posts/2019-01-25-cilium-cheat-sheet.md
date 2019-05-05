---
layout    : post
title     : "Cilium Cheat Sheet"
date      : 2019-01-25
lastupdate: 2019-05-05
author    : ArthurChiao
categories: cilium ebpf
---

### TL;DR

[`Cilium`](https://github.com/cilium/cilium) is an eBPF-based open source
software.  It provides transparent network connectivity, L3-L7 security, and
loadbalancing between application workloads [1].

This post serves as a cheat sheet of cilium `1.5.0` CLIs. Note that command set
varies among different cilium releases, you should run `cilium -h` to get the
full list of your installed version.

CLI list:

| Command | Used For |
|:--------|:--------|
| [bpf Subcommands](#chap_1)  |  |
| `cilium bpf config   [get           ]`  | Manage endpoint configuration BPF maps |
| `cilium bpf ct       [list          ]`  | Connection tracking tables |
| `cilium bpf endpoint [list|delete   ]`  | Local endpoint map |
| `cilium bpf ipcache  [list|get      ]`  | Manage the IPCache mappings for IP/CIDR <-> Identity |
| `cilium bpf lb       [list          ]`  | Load-balancing configuration |
| `cilium bpf metrics  [list          ]`  | BPF datapath traffic metrics |
| `cilium bpf nat      [flush|list    ]`  | NAT mapping tables |
| `cilium bpf policy   [add|delete|get]`  | Manage policy related BPF maps |
| `cilium bpf proxy    [list|flush    ]`  | Proxy configuration |
| `cilium bpf sha      [list|flush    ]`  | Manage compiled BPF template objects |
| `cilium bpf tunnel   [list          ]`  | Tunnel endpoint map |
| [cleanup Subcommands](#chap_2)  |  |
| `cilium cleanup  `  | Reset the agent state |
| [completion Subcommands](#chap_3)  |  |
| `cilium completion`  | Output shell completion code for bash |
| [config Subcommands](#chap_4)  |  |
| `cilium config` | Cilium configuration options |
| [debuginfo Subcommands](#chap_5)  |  |
| `cilium debuginfo` | Request available debugging information from agent |
| [endpoint Subcommands](#chap_6)  |  |
| `cilium endpoint config    ` | View & modify endpoint configuration |
| `cilium endpoint disconnect` | Disconnect an endpoint from the network |
| `cilium endpoint get       ` | Display endpoint information |
| `cilium endpoint health    ` | View endpoint health |
| `cilium endpoint labels    ` | Manage label configuration of endpoint |
| `cilium endpoint list      ` | List all endpoints |
| `cilium endpoint log       ` | View endpoint status log |
| `cilium endpoint regenerate` | Force regeneration of endpoint program |
| [fqdn Subcommands](#chap_7)  | Manage fqdn proxy |
| `cilium fqdn [flags]` |  |
| `cilium fqdn cache [list|clean] [flags]` | |
| [help Subcommands](#chap_8)  | Help about any command |
| `cilium help [command] [flags]` |  |
| [identity Subcommands](#chap_9)  |  |
| `cilium identity get ` | Retrieve information about an identity |
| `cilium identity list` | List identities |
| [kvstore Subcommands](#chap_10)  |  |
| `cilium kvstore delete ` | Delete a key |
| `cilium kvstore get    ` | Retrieve a key |
| `cilium kvstore set    ` | Set a key and value |
| [map Subcommands](#chap_11)  |  |
| `cilium map get  <name> [flags] ` | Display BPF map information |
| `cilium map list  ` | List all open BPF maps |
| [metrics Subcommands](#chap_12)  |  |
| `cilium metrics list` | List all metrics |
| [monitor subcommands](#chap_13)  | Monitor notifications and events emitted by the BPF programs |
| `cilium monitor [flags] ` | Includes 1. Dropped packets; 2. Captured packet traces; 3. Debugging information |
| [node subcommands](#chap_14)  |  |
| `cilium node list` | List nodes |
| [Policy Subcommands](#chap_15)  |  |
| `cilium policy delete    ` | Delete policy rules |
| `cilium policy get       ` | Display policy node information |
| `cilium policy import    ` | Import security policy in JSON format |
| `cilium policy trace     ` | Trace a policy decision |
| `cilium policy validate  ` | Validate a policy |
| `cilium policy wait      ` | Wait for all endpoints to have updated to a given policy revision |
| [Prefilter Subcommands](#chap_16)  | Manage XDP CIDR filters |
| `cilium prefilter delete ` | Delete CIDR filters |
| `cilium prefilter list   ` | List CIDR filters |
| `cilium prefilter update ` | Update CIDR filters |
| [Preflight Subcommands](#chap_17)  | cilium upgrade helper |
| `cilium preflight ` | |
| [service subcommands](#chap_18)  | Manage services & loadbalancers |
| `cilium service delete   ` | Delete a service |
| `cilium service get      ` | Display service information |
| `cilium service list     ` | List services |
| `cilium service update   ` | Update a service |
| [status subcommands](#chap_19)  | Display status of daemon |
| `cilium status [flags]` | |
| [version subcommands](#chap_20)  |  |
| `cilium version [flags]` | |

For each subcommand, `cilium [subcommand] -h` will print the detailed usage,
e.g. `cilium bpf -h`, `cilium bpf config -h`.

Looking at the commands' outputs is a good way for learning cilium. So in the
following, we will give some illustrative examples and the respective outputs.
You could deploy a minikube+cilium environment to have a try [2].

<a id="chap_1"></a>

## 1 `cilium bpf`

### 1.1 `cilium bpf config`

```shell
$ cilium bfp config get --host <URI to host API> -o json
```

### 1.2 `cilium bpf ct`

ct: connection tracker.

```shell
$ cilium bpf ct list <endpoint>
```

List global ct:

```shell
$ cilium bpf ct list global
TCP IN 10.15.0.1 9090:34382 expires=277365 RxPackets=5 RxBytes=452 TxPackets=5 TxBytes=1116 Flags=13 RevNAT=0 SourceSecurityID=2
TCP IN 10.15.0.1 8080:48346 expires=277345 RxPackets=5 RxBytes=458 TxPackets=5 TxBytes=475 Flags=13 RevNAT=0 SourceSecurityID=2
ICMP IN 10.15.0.1 0:0 related expires=277435 RxPackets=1 RxBytes=74 TxPackets=0 TxBytes=0 Flags=10 RevNAT=0 SourceSecurityID=2
```

### 1.3 `cilium bpf endpoint`

An endpoint is a namespaced network interface that will be applied policies on.

List all local endpoint entries:

```shell
$ cilium bpf endpoint list
IP ADDRESS           LOCAL ENDPOINT INFO
10.15.0.1            (localhost)
10.15.117.125        id=12908 ifindex=22  mac=DA:83:01:55:60:C2 nodemac=62:05:3A:4E:A7:4B
10.15.237.131        id=60459 ifindex=18  mac=AA:E2:2B:D5:AD:41 nodemac=0A:DB:58:35:CB:41
```

Delete local endpoint entries:

```shell
$ cilium bpf endpoint delete
```

### 1.4 `cilium bpf ipcache`

```shell
$ cilium bpf ipcache get
$ cilium bpf ipcache list
```

### 1.5 `cilium bpf lb`

```shell
$ cilium bpf lb list
SERVICE ADDRESS        BACKEND ADDRESS
10.96.0.10:53          0.0.0.0:0 (0)
                       10.15.38.236:53 (3)
                       10.15.35.129:53 (3)
10.102.163.245:80      10.15.49.158:9090 (4)
                       0.0.0.0:0 (0)
```

### 1.6 `cilium bpf metrics`

BPF datapath traffic metrics.

```shell
$ cilium bpf metrics list
REASON                        DIRECTION   PACKETS   BYTES
Error retrieving tunnel key   Egress      231382    18545353
Invalid source ip             Unknown     1508284   120985464
Success                       Egress      226488    69418245
Success                       Unknown     207163    20200009
```

### 1.7 `cilium bpf nat`

### 1.8 `cilium bpf policy`

Mange policy related BPF maps.

```shell
$ cilium bpf policy [add|delete|get]
```

```shell
$ cilium bpf policy get --all
/sys/fs/bpf/tc/globals/cilium_policy_2159:

DIRECTION   LABELS (source:key[=value])                                    PORT/PROTO   PROXY PORT   BYTES     PACKETS
Ingress     reserved:host                                                  ANY          NONE         0         0
Ingress     reserved:world                                                 ANY          NONE         0         0
Ingress     reserved:health                                                ANY          NONE         0         0
Ingress     k8s:io.cilium.k8s.policy.cluster=default                       ANY          NONE         0         0

$ cilium bpf policy get --all -n
/sys/fs/bpf/tc/globals/cilium_policy_2159:

DIRECTION   IDENTITY   PORT/PROTO   PROXY PORT   BYTES     PACKETS
Ingress     1          ANY          NONE         0         0
Ingress     2          ANY          NONE         0         0
Ingress     104        ANY          NONE         0         0
Ingress     105        ANY          NONE         0         0
```

### 1.9 `cilium bpf proxy`

Proxy configuration.

```shell
$ cilium bpf proxy list

$ cilium bpf proxy flush
```

### 1.10 `cilium bpf sha`

```shell
Datapath SHA                               Endpoint(s)
677ceeb764aab1432e220cf66d304d1feedab281   1104
                                           1270
                                           1422
                                           1460
                                           1984
                                           2030
                                           256
                                           2751
```

### 1.11 `cilium bpf tunnel`

```shell
$ cilium bpf tunnel list
```

<a id="chap_2"></a>

## 2 `cilium cleanup`

<a id="chap_3"></a>

## 3 `cilium completion`

<a id="chap_4"></a>

## 4 `cilium config`

```shell
$ cilium config -o json
{
  "Conntrack": "Enabled",
  "ConntrackAccounting": "Enabled",
  "ConntrackLocal": "Disabled",
  "Debug": "Disabled",
  "DropNotification": "Enabled",
  "MonitorAggregationLevel": "None",
  "PolicyTracing": "Disabled",
  "TraceNotification": "Enabled"
}
```

<a id="chap_5"></a>

## 5 `cilium debuginfo`

This command will dump the **very very** detailed information about all aspects
of debugging, following is a highly truncated output:

```shell
$ cilium debuginfo

# Cilium debug information

#### Service list

ID   Frontend               Backend
1    10.108.128.131:32379   1 => 10.0.2.15:32379
2    10.108.128.131:32380   1 => 10.0.2.15:32380
3    10.96.0.10:53          1 => 10.15.35.129:53

#### Policy get

Revision: 5

#### Cilium memory map

00400000-038ec000 r-xp 00000000 08:01 2097166                            /usr/bin/cilium-agent
04359000-0437a000 rw-p 00000000 00:00 0                                  [heap]
c000000000-c000017000 rw-p 00000000 00:00 0

#### Endpoint Health 60459

Overall Health:   OK
BPF Health:       OK
Policy Health:    OK
Connected:        yes

#### Cilium status

KVStore:                Ok   etcd: 1/1 connected: http://127.0.0.1:31079 - 3.3.2 (Leader)
ContainerRuntime:       Ok   docker daemon: OK
Kubernetes:             Ok   1.13 (v1.13.2) [linux/amd64]
Kubernetes APIs:        ["networking.k8s.io/v1::NetworkPolicy", "CustomResourceDefinition", "cilium/v2::CiliumNetworkPolicy"]
Cilium:                 Ok   OK
NodeMonitor:            Disabled
Cilium health daemon:   Ok
IPv4 address pool:      10/65535 allocated

Controller Status:   63/63 healthy
  Name                                        Last success   Last error   Count   Message
  cilium-health-ep                            never          never        0       no error
  dns-poller                                  never          never        0       no error
  ipcache-bpf-garbage-collection              never          never        0       no error

#### Cilium environment keys

bpf-root:
kvstore-opt:map[etcd.config:/var/lib/etcd-config/etcd.config]
masquerade:true
cluster-id:0

#### Endpoint list

ENDPOINT   POLICY (ingress)   POLICY (egress)   IDENTITY   LABELS (source:key[=value])                       IPv6                 IPv4            STATUS
           ENFORCEMENT        ENFORCEMENT
10916      Disabled           Disabled          44531      k8s:class=deathstar                               f00d::a0f:0:0:2aa4   10.15.43.62     ready
                                                           k8s:org=empire

#### BPF Policy Get 10916

DIRECTION   LABELS (source:key[=value])                              PORT/PROTO   PROXY PORT   BYTES   PACKETS
Ingress     k8s:app=etcd                                             ANY          NONE         0       0
Egress      reserved:host                                            ANY          NONE         4998    51

#### Endpoint Get 10916

[
  {
    "id": 10916,
    "spec": {
      "label-configuration": {
        "user": []
      },
      "options": {
        "Conntrack": "Enabled",
        ...
        "TraceNotification": "Enabled"
      }
    },
    "status": {
      "controllers": [
        {
          "name": "resolve-identity-10916",
          "status": {
            "last-failure-timestamp": "0001-01-01T00:00:00.000Z",
            "last-success-timestamp": "2019-01-25T08:28:55.761Z",
            "success-count": 295
          },
        },
     "external-identifiers": {
       "container-id": "7ba580c3b49c53ba03e72b32a182150",
       "container-name": "k8s_POD_deathstar-6fb5694d48-ttln2_default_e395bf0b-77bc14c_0",
       "pod-name": "default/deathstar-6fb5694d48-ttln2"
     },
     "identity": {
       "id": 44531,
       "labels": [
         "k8s:io.cilium.k8s.policy.cluster=default",
       ],
      },
      "networking": {
        "addressing": [
          {
            "ipv4": "10.15.43.62",
          }
        ],
        "interface-index": 20,
        "interface-name": "lxc692d4fb6516a",
  "policy": {
    "proxy-statistics": [],
    "realized": {
      "allowed-egress-identities": [
        101,
        44531,
        4
      ],

#### Identity get 44531

ID      LABELS
44531   k8s:class=deathstar
```

The output is a markdown file, which could be used when reporting a bug on the
github [issue tracker](https://github.com/cilium/cilium/issues), or sending to
the Cilium develop team [3].

<a id="chap_6"></a>

## 6 `cilium endpoint`

List all endpoints:

```shell
$ cilium endpoint list
ENDPOINT   POLICY (ingress)   POLICY (egress)   IDENTITY   LABELS (source:key[=value])                    IPv6  IPv4            STATUS
           ENFORCEMENT        ENFORCEMENT
10916      Disabled           Disabled          44531      k8s:class=deathstar                                  10.15.43.62     ready
                                                           k8s:org=empire
12041      Disabled           Disabled          104        k8s:io.cilium.k8s.policy.cluster=default             10.15.35.129    ready
                                                           k8s:io.cilium.k8s.policy.serviceaccount=coredns
                                                           k8s:k8s-app=kube-dns
```

Note that, the endpoint with `identity=4` is the built in `cilium-health`
service, which has the unique label `reserved:health`.

Change endpoint configurations:

```shell
$ cilium endpoint config <endpoint ID> DropNotification=false TraceNotification=false
```

Check `cilium-health`'s (with endpoint ID `59709` in this example) logs:

```shell
$ cilium endpoint log 59709
Timestamp              Status   State                   Message
2019-01-29T10:33:29Z   OK       ready                   Successfully regenerated endpoint program (Reason: datapath ipcache)
2019-01-29T10:33:29Z   OK       ready                   Completed endpoint regeneration with no pending regeneration requests
2019-01-29T10:33:28Z   OK       regenerating            Regenerating endpoint: datapath ipcache
2019-01-29T10:33:28Z   OK       waiting-to-regenerate   Triggering endpoint regeneration due to datapath ipcache
```

Other commands:

```shell
$ cilium endpoint get/health/labels/regenerate
```

<a id="chap_7"></a>

## 7 `cilium help`

<a id="chap_8"></a>

## 8 `cilium help`

<a id="chap_9"></a>

## 9 `cilium identity`

cilium identity get/list

```shell
$ cilium identity list
ID      LABELS
1       reserved:host
2       reserved:world
3       reserved:unmanaged
4       reserved:health
5       reserved:init
100     k8s:io.cilium.k8s.policy.cluster=default
        k8s:io.cilium.k8s.policy.serviceaccount=cilium-etcd-sa
        k8s:io.cilium/app=etcd-operator
101     k8s:app=etcd
...
```

Note that the first 5 identities are all `reserved` ones, you will see them in
some ingress/egress rules.

<a id="chap_10"></a>

## 10 `cilium kvstore`

cilium kvstore get/set/delete.

<a id="chap_11"></a>

## 11 `cilium map`

Access BPF maps.

```shell
# cilium map list
Name                     Num entries   Num errors   Cache enabled
cilium_lb4_reverse_nat   6             0            true
cilium_lb4_rr_seq        0             0            true
cilium_lb4_services      6             0            true
cilium_lxc               20            0            true
cilium_proxy4            0             0            false
cilium_ipcache           22            0            true
cilium_ep_config_12041   1             0            true
...
```

Display BPF map information:

```shell
$ cilium map get cilium_ipcache
Key                      Value   State   Error
10.15.38.236/32          104     sync
10.0.2.15/32             1       sync
10.15.86.181/32          4       sync
10.15.71.253/32          44531   sync

$ c map get cilium_ep_config_3847
Key Value State   Error
0 SKIP_POLICY_INGRESS,SKIP_POLICY_EGRESS,0004,0008,0010,...,40000000,80000000,, 0, 0, 0.0.0.0, ::, 0, 0, 00:00:00:00:00:00   sync
```

<a id="chap_12"></a>

## 12 `cilium metrics`

Access metric status. Including much of statistics metrics:

```shell
$ cilium metrics list
Metric                                              Labels                                                     Value
cilium_controllers_failing                                                                                     0.000000
cilium_controllers_runs_duration_seconds            status="failure"                                           0.029636
cilium_controllers_runs_duration_seconds            status="success"                                           2569.774532
cilium_controllers_runs_total                       status="failure"                                           1.000000
cilium_controllers_runs_total                       status="success"                                           179296.000000
cilium_datapath_conntrack_gc_duration_seconds       family="ipv4" protocol="TCP" status="completed"            5.213934
cilium_datapath_conntrack_gc_duration_seconds       family="ipv4" protocol="non-TCP" status="completed"        1.898584
```

<a id="chap_13"></a>

## 13 `cilium monitor`

The monitor displays notifications and events emitted by the BPF
programs attached to endpoints and devices. This includes:

* Dropped packet notifications
* Captured packet traces
* Debugging information

```shell
# cilium monitor [flags]

Flags:
      --from []uint16         Filter by source endpoint id
      --hex                   Do not dissect, print payload in HEX
  -j, --json                  Enable json output. Shadows -v flag
      --related-to []uint16   Filter by either source or destination endpoint id
      --to []uint16           Filter by destination endpoint id
  -t, --type []string         Filter by event types [agent capture debug drop l7 trace]
  -v, --verbose               Enable verbose output
```

Monitor all BPF notifications and events:

```shell
$ cilium monitor
<- host flow 0x6154a2bb identity 2->0 state new ifindex 0: 192.168.99.100:8443
-> 10.15.38.236:36598 tcp ACK
-> endpoint 34542 flow 0x6154a2bb identity 2->104 state reply ifindex lxcc2615b8d08d2: 10.96.0.1:443 -> 10.15.38.236:36598 tcp ACK
<- endpoint 34542 flow 0x30ce3dbd identity 104->0 state new ifindex 0: 10.15.38.236:36598 -> 10.96.0.1:443 tcp ACK
-> stack flow 0x30ce3dbd identity 104->2 state established ifindex 0: 10.15.38.236:36598 -> 192.168.99.100:8443 tcp ACK
<- host flow 0x43caa648 identity 2->0 state new ifindex 0: 192.168.99.100:8443
```

Monitor packet drops:

```shell
$ cilium monitor --type drop
```

Check all traffic to endpoint 3991 (192.168.0.121 in the below output):

```shell
$ cilium monitor --to 3991
-> endpoint 3991 flow 0x3ed38d3c identity 1->4 state new ifindex cilium_health: 192.168.0.1:58476 -> 192.168.0.121:4240 tcp SYN
-> endpoint 3991 flow 0x3ed38d3c identity 1->4 state established ifindex cilium_health: 192.168.0.1:58476 -> 192.168.0.121:4240 tcp ACK
-> endpoint 3991 flow 0x3ed38d3c identity 1->4 state established ifindex cilium_health: 192.168.0.1:58476 -> 192.168.0.121:4240 tcp ACK
```

<a id="chap_14"></a>

## 14 `cilium node`

```shell
$ cilium node list
Name       IPv4 Address   Endpoint CIDR   IPv6 Address   Endpoint CIDR
minikube   10.0.2.15      10.15.0.0/16    <nil>          f00d::a0f:0:0:0/112
```

<a id="chap_15"></a>

## 15 `cilium policy`

Manage security policies.

```shell
$ cilium policy [command]

Available Commands:
  delete      Delete policy rules
  get         Display policy node information
  import      Import security policy in JSON format
  trace       Trace a policy decision
  validate    Validate a policy
  wait        Wait for all endpoints to have updated to a given policy revision
```

```shell
$ cilium policy get
[
  {
    "endpointSelector": {
      "matchLabels": {
        "any:class": "deathstar",
        "any:org": "empire",
        "k8s:io.kubernetes.pod.namespace": "default"
      }
    },
    "ingress": [
      {
        "fromEndpoints": [
          {
            "matchLabels": {
              "any:org": "empire",
              "k8s:io.kubernetes.pod.namespace": "default"
            }
          }
        ],
        "toPorts": [
          {
            "ports": [
              {
                "port": "80",
                "protocol": "TCP"
              }
            ],
            "rules": {
              "http": [
                {
                  "path": "/v1/request-landing",
                  "method": "POST"

```

<a id="chap_16"></a>

## 16 `cilium prefilter`

```shell
# cilium prefilter -h
Manage XDP CIDR filters

Usage:
  cilium prefilter [command]

Available Commands:
  delete      Delete CIDR filters
  list        List CIDR filters
  update      Update CIDR filters
```

<a id="chap_17"></a>

## 17 `cilium preflight`

<a id="chap_15"></a>

## 18 `cilium service`

Manage services & loadbalancers.

List all services:

```shell
# cilium service list
ID   Frontend               Backend
1    10.108.128.131:32379   1 => 10.0.2.15:32379
2    10.108.128.131:32380   1 => 10.0.2.15:32380
3    10.96.0.10:53          1 => 10.15.35.129:53
                            2 => 10.15.38.236:53
4    10.102.163.245:80      1 => 10.15.49.158:9090
```

Get service `6`:

```shell
$ cilium service get 6 -o json
{
  "spec": {
    "backend-addresses": [
      {
        "ip": "",
        "port": 6443
      }
    ],
    "frontend-address": {
      "ip": "10.32.0.1",
      "port": 443,
      "protocol": "TCP"
    },
    "id": 1
  },
  "status": {
    "realized": {
      "backend-addresses": [
        {
          "ip": "",
          "port": 6443
        }
      ],
      "frontend-address": {
        "ip": "10.32.0.1",
        "port": 443,
        "protocol": "TCP"
      },
      "id": 1
    }
  }
}
```

<a id="chap_19"></a>

## 19 `cilium status`

Display status of daemon.

```shell
Usage:
  cilium status [flags]

Flags:
      --all-addresses     Show all allocated addresses, not just count
      --all-controllers   Show all controllers, not just failing
      --all-health        Show all health status, not just failing
      --all-nodes         Show all nodes, not just localhost
      --all-redirects     Show all redirects
      --brief             Only print a one-line status message
  -h, --help              help for status
  -o, --output string     json| jsonpath='{}'
      --verbose           Equivalent to --all-addresses --all-controllers --all-nodes --all-health
```

Cilium status overview:

```shell
$ cilium status
KVStore:                Ok   etcd: 1/1 connected: https://<IP>:2379 - 3.2.22 (Leader)
ContainerRuntime:       Ok   docker daemon: OK
Kubernetes:             Ok   1.13 (v1.13.2) [linux/amd64]
Kubernetes APIs:        ["core/v1::Node", "core/v1::Namespace", "CustomResourceDefinition", "cilium/v2::CiliumNetworkPolicy", "networking.k8s.io/v1::NetworkPolicy", "core/v1::Service", "core/v1::Endpoint", "core/v1::Pods"]
Cilium:                 Ok   OK
NodeMonitor:            Disabled
Cilium health daemon:   Ok
IPv4 address pool:      4/65535 allocated
IPv6 address pool:      3/65535 allocated
Controller Status:      27/27 healthy
Proxy Status:           OK, ip 10.94.0.1, port-range 10000-20000
Cluster health:   1/1 reachable   (2019-01-29T10:17:04Z)
```

Show all controllers (sync tasks) instead of summary:

```shell
$ cilium status --all-controllers
...
Controller Status:      40/40 healthy
  Name                                        Last success     Last error   Count   Message
  cilium-health-ep                            16s ago          never        0       no error
  dns-garbage-collector-job                   33s ago          never        0       no error
  ipcache-bpf-garbage-collection              1m33s ago        never        0       no error
  kvstore-etcd-session-renew                  never            never        0       no error
  kvstore-sync-store-cilium/state/nodes/v1    21s ago          never        0       no error
  lxcmap-bpf-host-sync                        3s ago           never        0       no error
  metricsmap-bpf-prom-sync                    6s ago           never        0       no error
  propagating local node change to kv-store   124h50m26s ago   never        0       no error
  resolve-identity-0                          34s ago          never        0       no error
  resolve-identity-2751                       4m6s ago         never        0       no error
  sync-IPv4-identity-mapping (0)              28s ago          never        0       no error
  sync-IPv4-identity-mapping (1104)           4m1s ago         never        0       no error
  sync-IPv4-identity-mapping (2751)           4m1s ago         never        0       no error
  sync-cnp-policy-status (v2 default/rule1)   124h18m48s ago   never        0       no error
  sync-lb-maps-with-k8s-services              124h50m35s ago   never        0       no error
  sync-policymap-1104                         33s ago          never        0       no error
  sync-policymap-2751                         33s ago          never        0       no error
  sync-to-k8s-ciliumendpoint (1104)           6s ago           never        0       no error
  sync-to-k8s-ciliumendpoint (2751)           11s ago          never        0       no error
  template-dir-watcher                        never            never        0       no error
  ...
```

Show all allocated IP addresses instead of summary:

```shell
$ cilium status --all-addresses
...
IPv4 address pool:      10/255 allocated from 192.168.0.0/24
Allocated addresses:
  192.168.0.1 (router)
  192.168.0.128 (default/tiefighter)
  192.168.0.133 (health)
  192.168.0.155 (default/deathstar-6fb5694d48-69jpx)
  192.168.0.16 (cilium-monitoring/prometheus-f8454f7d6-cgl27 [restored])
  192.168.0.2 (kube-system/coredns-67688d6ffc-6dj5h [restored])
  192.168.0.210 (default/deathstar-6fb5694d48-sv9zg)
  192.168.0.217 (loopback)
  192.168.0.249 (default/xwing)
  192.168.0.34 (kube-system/cilium-operator-76f66dfd68-g6gr2)
  ...
```

<a id="chap_20"></a>

## 20 `cilium version`

```shell
$ cilium version
Client: 1.5.0 e47b37c3a 2019-04-25T22:20:13-05:00 go version go1.12.1 linux/amd64
Daemon: 1.5.0 e47b37c3a 2019-04-25T22:20:13-05:00 go version go1.12.1 linux/amd64
```

## References

1. [Cilium: github](https://github.com/cilium/cilium)
2. [Cilium: getting started with minikube](https://cilium.readthedocs.io/en/stable/gettingstarted/minikube/)
3. [Cilium: Trouble Shooting](https://cilium.readthedocs.io/en/v1.4/troubleshooting/)
