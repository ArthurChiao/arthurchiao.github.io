---
layout    : post
title     : "Cilium Code Walk Through: Identity Mark"
date      : 2020-09-09
lastupdate: 2020-09-09
categories: cilium bpf
---

* TOC
{:toc}

----


```
to_netdev
  |-policy_clear_mark
  |-resolve_srcid_ipv4
  |   |-lookup_ip4_remote_endpoint
  |       |-ipcache_lookup4
  |-ipv4_host_policy_egress
```

# 1 Compile BPF

In `pkg/datapath/loader/loader.go`:

* `CompileOrLoad() -> ELFSubstitutions() -> elfVariableSubstitutions()`
* `patchHostNetdevDatapath() -> ELFSubstitutions() -> elfVariableSubstitutions()`

```go
// pkg/datapath/loader/template.go

// elfVariableSubstitutions returns the set of data substitutions that must
// occur in an ELF template object file to update static data for the specified endpoint.
func elfVariableSubstitutions(ep datapath.Endpoint) map[string]uint32 {
    result := make(map[string]uint32)

    result["LXC_IPV4"] = byteorder.HostSliceToNetwork(ep.IPv4Address())

    mac := ep.GetNodeMAC()
    result["NODE_MAC_1"] = sliceToBe32(mac[0:4])
    result["NODE_MAC_2"] = uint32(sliceToBe16(mac[4:6]))

    if ep.IsHost() {
        result["NATIVE_DEV_IFINDEX"] = 0
        result["IPV4_NODEPORT"     ] = 0
        result["HOST_EP_ID"        ] = uint32(ep.GetID())
    } else {
        result["LXC_ID"] = uint32(ep.GetID())
    }

    identity := ep.GetIdentity().Uint32()
    result["SECLABEL"   ] = identity
    result["SECLABEL_NB"] = byteorder.HostToNetwork(identity)
    return result
}

// elfMapSubstitutions returns the set of map substitutions that must occur in
// an ELF template object file to update map references for the specified endpoint.
func elfMapSubstitutions(ep datapath.Endpoint) map[string]string {
    result := make(map[string]string)
    epID := uint16(ep.GetID())

    for _, name := range elfMapPrefixes {
        if ep.IsHost() && name == callsmap.MapName {
            name = callsmap.HostMapName
        }
        templateStr := bpf.LocalMapName(name, templateLxcID)
        desiredStr := bpf.LocalMapName(name, epID)
        result[templateStr] = desiredStr
    }
    if ep.ConntrackLocalLocked() {
        for _, name := range elfCtMapPrefixes {
            templateStr := bpf.LocalMapName(name, templateLxcID)
            desiredStr := bpf.LocalMapName(name, epID)
            result[templateStr] = desiredStr
        }
    }

    if !ep.IsHost()
        result[policymap.CallString(templateLxcID)] = policymap.CallString(epID)

    return result
}
```

```go
// pkg/maps/policymap/callmap.go

// CallString returns the string which indicates the calls map by index in the
// ELF, and index into that call map for a specific endpoint.
//
// Derived from __section_tail(CILIUM_MAP_CALLS, NAME) per bpf/lib/tailcall.h.
func CallString(id uint16) string {
    return fmt.Sprintf("1/%#04x", id)
}
```

# Definition in C

```c
// bpf/ep_config.h

DEFINE_U32(SECLABEL, 0xfffff);
```

```c
// bpf/lib/common.h

/* Magic ctx->mark identifies packets origination and encryption status.
 *
 * The upper 16 bits plus lower 8 bits (e.g. mask 0XFFFF00FF) contain the
 * packets security identity. The lower/upper halves are swapped to recover
 * the identity.
 *
 * The 4 bits at 0X0F00 provide
 *  - the magic marker values which indicate whether the packet is coming from
 *    an ingress or egress proxy, a local process and its current encryption
 *    status.
 *
 * The 4 bits at 0xF000 provide
 *  - the key index to use for encryption when multiple keys are in-flight.
 *    In the IPsec case this becomes the SPI on the wire.
 */
#define MARK_MAGIC_HOST_MASK        0x0F00
#define MARK_MAGIC_PROXY_INGRESS    0x0A00
#define MARK_MAGIC_PROXY_EGRESS        0x0B00
#define MARK_MAGIC_HOST            0x0C00
#define MARK_MAGIC_DECRYPT        0x0D00
#define MARK_MAGIC_ENCRYPT        0x0E00
#define MARK_MAGIC_IDENTITY        0x0F00 /* mark carries identity */
#define MARK_MAGIC_TO_PROXY        0x0200

#define MARK_MAGIC_KEY_ID        0xF000
#define MARK_MAGIC_KEY_MASK        0xFF00

/* IPSec cannot be configured with NodePort BPF today, hence non-conflicting
 * overlap with MARK_MAGIC_KEY_ID.
 */
#define MARK_MAGIC_SNAT_DONE        0x1500
```


# References

1. [Cilium Code Walk Through: CNI Create Network]({% link _posts/2019-06-17-cilium-code-cni-create-network.md %})
