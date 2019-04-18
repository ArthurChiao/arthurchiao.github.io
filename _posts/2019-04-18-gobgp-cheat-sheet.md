---
layout: post
title:  "GoBGP Cheat Sheet"
date:   2019-04-18
author: ArthurChiao
categories: gobgp kube-router
---

### TL;DR

[`GoBGP`](https://github.com/osrg/gobgp) is an open source BGP implementation,
implemented in Golang [1].

[`kube-router`](https://github.com/cloudnativelabs/kube-router) is a 
Kubernetes networking solution with aim to provide operational simplicity
and high performance. `kube-router` internally uses `GoBGP`.

This post serves as a cheat sheet of GoBGP CLIs. Note that command set varies
among different gobgp releases, you should run `gobgp -h` to get the full
list of your installed version.

CLI list of version `?`:

| Command | Used For |
|:--------|:--------|
| `gobgp bmp       [ add | del ]` |  |
| `gobgp global    [ del | policy | rib ]` | Manage global settings |
| `gobgp monitor   [ adj-in | global | neighbor]` |  |
| `gobgp mtr       [ inject ]` |  |
| `gobgp neighbor  [ add | del | update ]` | Manage neighbors |
| `gobgp policy    [ add | as-path | community | del | ext-community | large-community | neighbor | prefix | set | statement ]` |  |
| `gobgp rpki      [ server | table | validate ]` |  |
| `gobgp vrf       [ add | del ]` | Manage VRFs |

For each subcommand, `gobgp [subcommand] -h` will print the detailed usage,
e.g. `gobgp global -h`, `gobgp bmp add -h`.

Looking at the commands' outputs is a good way for learning gobgp. So in the
following, we will give some illustrative examples and the respective
outputs.

## 1 BMP

## 2 Global

Check configurations of this BGP agent:

```shell
$ gobgp global
AS:        65342
Router-ID: 192.168.1.101
Listening Port: 179, Addresses: 192.168.1.101, ::1
```

Check global policies:

```shell
$ gobgp global policy
```

Check global RIBs (Routing Information Base):

```shell
$ gobgp global rib
   Network              Next Hop             AS_PATH              Age        Attrs
*> 192.168.100.0/24     192.168.1.101                             00:00:06   [{Origin: i}]

# gobgp global rib summary
Table ipv4-unicast
Destination: 1, Path: 1
```

TODO: The RIB entries started with `*` are those that **announced to
neighbors** by this GoBGP agent?

## 3 Monitor

```shell
$ gobgp monitor global rib

$ gobgp monitor neighbor
```

## 4 MRT

## 5 Neighbor

Check BGP neighbors (peers) of this node:

```shell
$ gobgp neighbor
Peer           AS     Up/Down State       |#Received  Accepted
192.168.255.1 65342 1d 18:32:30 Establ      |        0         0
192.168.255.2 65342 1d 18:28:07 Establ      |        0         0
```

Neighbor details:

```shell
$ gobgp neighbor 192.168.255.1
BGP neighbor is 192.168.255.1, remote AS 65342
  BGP version 4, remote router ID 192.168.255.1
  BGP state = established, up for 1d 18:55:34
  BGP OutQ = 0, Flops = 0
  Hold time is 90, keepalive interval is 30 seconds
  Configured hold time is 90, keepalive interval is 30 seconds

  Neighbor capabilities:
    multiprotocol:
        ipv4-unicast:   advertised and received
    route-refresh:      advertised and received
    extended-nexthop:   received
        Remote: nlri: ipv4-unicast, nexthop: ipv6
    graceful-restart:   received
        Remote: restart time 120 sec
            ipv4-unicast
    4-octet-as: advertised and received
    UnknownCapability(66):      received
    UnknownCapability(67):      received
    cisco-route-refresh:        received
  Message statistics:
                         Sent       Rcvd
    Opens:                  1          1
    Notifications:          0          0
    Updates:              517          1
    Keepalives:          5152       5150
    Route Refresh:          0          1
    Discarded:              0          0
    Total:               5670       5153
  Route statistics:
    Advertised:             1
    Received:               0
    Accepted:               0
```

## 6 Policy

```shell
$ gobgp policy
Name kube_router:
    StatementName kube_router_stmt0:
      Conditions:
        PrefixSet: any clusteripprefixset
        NeighborSet: any externalpeerset
      Actions:
         accept
    StatementName kube_router_stmt1:
      Conditions:
        PrefixSet: any podcidrprefixset
        NeighborSet: any externalpeerset
      Actions:
         accept
```

```shell
$ gobgp policy statement
StatementName kube_router_stmt0:
  Conditions:
    PrefixSet: any clusteripprefixset
    NeighborSet: any externalpeerset
  Actions:
     accept
StatementName kube_router_stmt1:
  Conditions:
    PrefixSet: any podcidrprefixset
    NeighborSet: any externalpeerset
  Actions:
     accept
```

## 7 RPKI

## 8 VRF

## References

1. [GoBGP: Github](https://github.com/osrg/gobgp)
1. [GoBGP CLI syntax](https://github.com/osrg/gobgp/blob/master/docs/sources/cli-command-syntax.md)
1. [kube-router: Github](https://github.com/cloudnativelabs/kube-router)
