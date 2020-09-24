---
layout    : post
title     : "Veth Pair: Design and Implementation"
date      : 2020-09-29
lastupdate: 2020-09-29
categories: veth-pair kernel
---

* TOC
{:toc}

----

This post explores the veth pair implementation inside Linux kernel.

Code based on kernel `4.19`.

# Xmit

```
veth_xmit
 |-veth_forward_skb
    |-if xdp
        veth_xdp_rx(rxq, skb)
      else
        netif_rx(skb)

// general receiving flow starts from netif_rx()
netif_rx(skb)
  |-netif_rx_internal
     |-if generic xdp
     |   ret = do_xdp_generic
     |-ret = enqueue_to_backlog(skb)
     |-put_cpu()
     |-return ret
```

```c
// drivers/net/veth.c

static netdev_tx_t veth_xmit(struct sk_buff *skb, struct net_device *dev)
{
    bool rcv_xdp = false;

    struct veth_priv  *priv     = netdev_priv(dev);            // this end's private data
    struct net_device *rcv      = rcu_dereference(priv->peer); // peer end
    struct veth_priv  *rcv_priv = netdev_priv(rcv);            // peer end's private data

    struct veth_rq    *rq       = NULL;                        // RX queue of the peer end
    int                rxq      = skb_get_queue_mapping(skb);  // Rx queue ID of the peer end
    if (rxq < rcv->real_num_rx_queues) {
        rq = &rcv_priv->rq[rxq];
        rcv_xdp = rcu_access_pointer(rq->xdp_prog);   // XDP program at the given RX queue
        if (rcv_xdp)                                  // if XDP programs exists
            skb_record_rx_queue(skb, rxq);            // skb->queue_mapping = rxq + 1;
    }

    if (veth_forward_skb(rcv, skb, rq, rcv_xdp) == NET_RX_SUCCESS)) {
        stats->bytes += skb->len;
        stats->packets++;
    } else {
        atomic64_inc(&priv->dropped);
    }

    if (rcv_xdp)
        __veth_xdp_flush(rq); // -> if (!rq->rx_notify_masked) napi_schedule()

    return NETDEV_TX_OK;
}
```

```c
// drivers/net/veth.c

static int
veth_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames, u32 flags)
{
    struct veth_priv  *priv     = netdev_priv(dev);                     // this end's private data
    struct net_device *rcv      = rcu_dereference(priv->peer);          // peer end, a network device
    struct veth_priv  *rcv_priv = netdev_priv(rcv);                     // peer end's private data
    struct veth_rq    *rq       = &rcv_priv->rq[veth_select_rxq(rcv)];  // peer end's RX queue

    max_len = rcv->mtu + rcv->hard_header_len + VLAN_HLEN;
    for (i = 0; i < n; i++) { // drop mal-formed frames
        struct xdp_frame *frame = frames[i];
        void *ptr = veth_xdp_to_ptr(frame);
        if (unframe->len > max_len || __ptr_ring_produce(&rq->xdp_ring, ptr)) {
            xdp_return_frame_rx_napi(frame);
            drops++;
        }
    }

    if (flags & XDP_XMIT_FLUSH)
        __veth_xdp_flush(rq);   // call napi_schedule()

    return n - drops;
}
```
