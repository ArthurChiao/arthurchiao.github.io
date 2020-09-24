---
layout    : post
title     : "Life of a packet in Cilium: Ingress Policy Enforcement"
date      : 2020-09-24
lastupdate: 2020-09-24
categories: cilium bpf
---

* TOC
{:toc}

----

# 1 Node1: Create Pod

## 1.1 Create IP

## 1.2 Create Endpoint

## 1.3 Create Identity

# 2 Node2: ipcache: Listen to cilium-etcd

Agent 启动时 start 一个 ip identity watcher，监听`cilium/state/ip/v1/default/<ip>`

# 3 Node1: Send packet

# 4 Node2: ingress policy enforcement

```c

static __always_inline int
__policy_can_access(
    const void *map,             // POLICY_MAP，每个 endpint 一个 policy map
    struct __ctx_buff *ctx,      // 数据包
    __u32 localID,               // 当前 endpoint 的 identity
    __u32 remoteID,              // 发送这个数据包的 endpoint 的 identity
    __u16 dport,                 // 目的端口
    __u8 proto,                  // L4 proto
    int dir,                     // ingress or egress
    bool is_untracked_fragment,
    __u8 *match_type,            // 返回值，匹配到的 policy 类型
    )
{
    struct policy_entry *policy; // 查询到的 policy
    struct policy_key key = {    // 查询 policy 所用的 key
        .sec_label = remoteID,
        .dport = dport,
        .protocol = proto,
        .egress = !dir,
    };

    // 接下来按照优先级从高到低依次匹配不同类型的 policy
    // 1. L3/L4 (L3+L4) policy
    // 2. L4-only policy
    // 3. L3 policy
    // 4. allow-all policy

    if (!is_untracked_fragment) {
        if policy = map_lookup_elem(map, &key); policy {
            *match_type = POLICY_MATCH_L3_L4;
            return policy->proxy_port;         // ===> L3/L4 policy found
        }

        key.sec_label = 0;                     // L4-only policy 不关心 src_ip (src_identity)，只关心 dport
        if policy = map_lookup_elem(map, &key); policy {
            *match_type = POLICY_MATCH_L4_ONLY;
            return policy->proxy_port;         // ===> L4-only policy found
        }

        key.sec_label = remoteID;              // 重新将 identity 设置回来，准备接下来的 L3 policy lookup
    }

    key.dport = 0;    // If L4 policy check misses, fall back to L3.
    key.protocol = 0; // L3 policy does not rely on L4 info, namely, the dport and L4 proto
    if policy = map_lookup_elem(map, &key); policy {
        *match_type = POLICY_MATCH_L3_ONLY;
        return CTX_ACT_OK;                     // ===> L3 policy found
    }

    key.sec_label = 0; /* Final fallback if allow-all policy is in place. */
    if policy = map_lookup_elem(map, &key); policy {
        *match_type = POLICY_MATCH_ALL;
        return CTX_ACT_OK;                     // ===> Allow-all policy found
    }

    if ctx_load_meta(ctx, CB_POLICY)
        return CTX_ACT_OK;                     // ===> TODO ???

    return DROP_POLICY;                        // ===> Drop
}
```

```c
// bpf/lib/maps.h

/* Per-endpoint policy enforcement map */
struct bpf_elf_map __section_maps POLICY_MAP = {
    .type        = BPF_MAP_TYPE_HASH,
    .size_key    = sizeof(struct policy_key),
    .size_value  = sizeof(struct policy_entry),
    .pinning     = PIN_GLOBAL_NS,
    .max_elem    = POLICY_MAP_SIZE,
    .flags       = CONDITIONAL_PREALLOC,
};
```

之前生成 map entry 的代码：


```go
// pkg/policy/l4.go

// ToMapState converts filter into a MapState with two possible values:
// - Entry with ProxyPort = 0: No proxy redirection is needed for this key
// - Entry with any other port #: Proxy redirection is required for this key,
//                                caller must replace the ProxyPort with the actual
//                                listening port number.
// Note: It is possible for two selectors to select the same security ID.
// To give priority for L7 redirection (e.g., for visibility purposes), we use
// RedirectPreferredInsert() instead of directly inserting the value to the map.
func (l4 *L4Filter) ToMapState(npMap NamedPortsMap, direction trafficdirection.TrafficDirection) MapState {
    port := uint16(l4.Port)
    proto := uint8(l4.U8Proto)

    logger := log
    if option.Config.Debug {
        logger = log.WithFields(logrus.Fields{
            logfields.Port:             port,
            logfields.PortName:         l4.PortName,
            logfields.Protocol:         proto,
            logfields.TrafficDirection: direction,
        })
    }

    keysToAdd := MapState{}

    // resolve named port
    if port == 0 && l4.PortName != "" {
        var err error
        port, err = npMap.GetNamedPort(l4.PortName, proto)
        if err != nil {
            logger.Debugf("ToMapState: Skipping named port: %s", err)
            return keysToAdd
        }
    }

    keyToAdd := Key{
        Identity:         0,    // Set in the loop below (if not wildcard)
        DestPort:         port, // NOTE: Port is in host byte-order!
        Nexthdr:          proto,
        TrafficDirection: direction.Uint8(),
    }

    // find the L7 rules for the wildcard entry, if any
    var wildcardL7Policy *PerSelectorPolicy
    if l4.wildcard != nil {
        wildcardL7Policy = l4.L7RulesPerSelector[l4.wildcard]
    }

    for cs, l7 := range l4.L7RulesPerSelector {
        // Skip generating L3/L4 keys if L4-only key (for the same L4 port and
        // protocol) has the same effect w.r.t. redirecting to the proxy or not,
        // considering that L3/L4 key should redirect if L4-only key does. In
        // summary, if have both L3/L4 and L4-only keys:
        //
        // L3/L4        L4-only      Skip generating L3/L4 key
        // redirect     none         no
        // no redirect  none         no
        // redirect     no redirect  no   (this case tested below)
        // no redirect  no redirect  yes  (same effect)
        // redirect     redirect     yes  (same effect)
        // no redirect  redirect     yes  (must redirect if L4-only redirects)
        //
        // have wildcard?        this is a L3L4 key?  not the "no" case?
        if l4.wildcard != nil && cs != l4.wildcard && !(l7 != nil && wildcardL7Policy == nil) {
            logger.WithField(logfields.EndpointSelector, cs).Debug("ToMapState: Skipping L3/L4 key due to existing L4-only key")
            continue
        }

        entry := NewMapStateEntry(l4.DerivedFromRules, l7 != nil)
        if cs.IsWildcard() {
            keyToAdd.Identity = 0
            keysToAdd.RedirectPreferredInsert(keyToAdd, entry)

            if port == 0 {
                // Allow-all
                logger.WithField(logfields.EndpointSelector, cs).Debug("ToMapState: allow all")
            } else {
                // L4 allow
                logger.WithField(logfields.EndpointSelector, cs).Debug("ToMapState: L4 allow all")
            }
            continue
        }

        identities := cs.GetSelections()
        if option.Config.Debug {
            logger.WithFields(logrus.Fields{
                logfields.EndpointSelector: cs,
                logfields.PolicyID:         identities,
            }).Debug("ToMapState: Allowed remote IDs")
        }
        for _, id := range identities {
            keyToAdd.Identity = id.Uint32()
            keysToAdd.RedirectPreferredInsert(keyToAdd, entry)
        }
    }

    return keysToAdd
}
```

# CEP

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
