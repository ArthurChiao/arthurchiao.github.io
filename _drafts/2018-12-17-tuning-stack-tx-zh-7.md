---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 7"
date:   2018-12-17
author: ArthurChiao
categories: linux network stack monitoring tuning
---

## 9 Network Device Driver

We’re nearing the end of our journey. There’s an important concept to understand about packet transmit. Most devices and drivers deal with packet transmit as a two-step process:

Data is arranged properly and the device is triggered to DMA the data from RAM and write it to the network
After the transmit completes, the device will raise an interrupt so the driver can unmap buffers, free memory, or otherwise clean its state.
The second phase of this is commonly called the “transmit completion” phase. We’re going to examine both, but we’ll start with the first phase: the transmit phase.

We saw that dev_hard_start_xmit calls the ndo_start_xmit (with a lock held) to transmit data, so let’s start by examining how a driver registers an ndo_start_xmit and then we’ll dive into how that function works.

As in the previous blog post we’ll be examining the igb driver.

### 9.1 Driver operations registration

Drivers implement a series of functions for a variety of operations, like:

Sending data (ndo_start_xmit)
Getting statistical information (ndo_get_stats64)
Handling device ioctls (ndo_do_ioctl)
And more.
The functions are exported as a series of function pointers arranged in a structure. Let’s take a look at the structure defintion for these operations in the igb driver source:

static const struct net_device_ops igb_netdev_ops = {
        .ndo_open               = igb_open,
        .ndo_stop               = igb_close,
        .ndo_start_xmit         = igb_xmit_frame,
        .ndo_get_stats64        = igb_get_stats64,

                /* ... more fields ... */
};
This structure is registered in the igb_probe function:

static int igb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
                /* ... lots of other stuff ... */

        netdev->netdev_ops = &igb_netdev_ops;

                /* ... more code ... */
}
As we saw in the previous section, higher layers of code will obtain a refernece to a device’s netdev_ops structure and call the appropriate function. If you are curious to learn more about how exactly PCI devices are brought up and when/where igb_probe is called, check out the driver initialization section from our other blog post.

### 9.2 Transmit data with ndo_start_xmit

The higher layers of the networking stack use the net_device_ops structure to call into a driver to perform various operations. As we saw earlier, the qdisc code calls ndo_start_xmit to pass data down to the driver for transmit. The ndo_start_xmit function is called while a lock is held, for most hardware devices, as we saw above.

In the igb device driver, the function registered to ndo_start_xmit is called igb_xmit_frame, so let’s start at igb_xmit_frame and learn how this driver transmits data. Follow along in ./drivers/net/ethernet/intel/igb/igb_main.c and keep in mind that a lock is beind held the entire time the following code is executing:

netdev_tx_t igb_xmit_frame_ring(struct sk_buff *skb,
                                struct igb_ring *tx_ring)
{
        struct igb_tx_buffer *first;
        int tso;
        u32 tx_flags = 0;
        u16 count = TXD_USE_COUNT(skb_headlen(skb));
        __be16 protocol = vlan_get_protocol(skb);
        u8 hdr_len = 0;

        /* need: 1 descriptor per page * PAGE_SIZE/IGB_MAX_DATA_PER_TXD,
         *       + 1 desc for skb_headlen/IGB_MAX_DATA_PER_TXD,
         *       + 2 desc gap to keep tail from touching head,
         *       + 1 desc for context descriptor,
         * otherwise try next time
         */
        if (NETDEV_FRAG_PAGE_MAX_SIZE > IGB_MAX_DATA_PER_TXD) {
                unsigned short f;
                for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
                        count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size);
        } else {
                count += skb_shinfo(skb)->nr_frags;
        }
The function starts out be determining use the TXD_USER_COUNT macro to determine how many transmit descriptors will be needed to transmit the data passed in. The count value initialized at the the number of descriptors to fit the skb. It is then adjusted to account for any additional fragments that need to be transmit.

        if (igb_maybe_stop_tx(tx_ring, count + 3)) {
                /* this is a hard error */
                return NETDEV_TX_BUSY;
        }
The driver then calls an internal function igb_maybe_stop_tx which will check the number of descriptors needed to ensure that the transmit queue has enough available. If not, NETDEV_TX_BUSY is returned here. As we saw earlier in the qdisc code, this will cause the qdisc to requeue the data to be retried later.

        /* record the location of the first descriptor for this packet */
        first = &tx_ring->tx_buffer_info[tx_ring->next_to_use];
        first->skb = skb;
        first->bytecount = skb->len;
        first->gso_segs = 1;
The code then obtains a regerence to the next available buffer info in the transmit queue. This structure will track the information needed for setting up a buffer descriptor later. A reference to the packet and its size are copied into the buffer info structure.

        skb_tx_timestamp(skb);
The code above starts by calling skb_tx_timestamp which is used to obtain a software based transmit timestamp. An application can use the transmit timestamp to determine the amount of time it takes for a packet to travel through the transmit path of the network stack.

Some devices also support generating timestamps for packets transmit in hardware. This allows the system to offload timestamping to the device and it allows the programmer to obtain a more accurate timestamp, as it will be taken much closer to when the actual transmit by the hardware occurs. We’ll see the code for this now:

        if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
                struct igb_adapter *adapter = netdev_priv(tx_ring->netdev);

                if (!(adapter->ptp_tx_skb)) {
                        skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
                        tx_flags |= IGB_TX_FLAGS_TSTAMP;

                        adapter->ptp_tx_skb = skb_get(skb);
                        adapter->ptp_tx_start = jiffies;
                        if (adapter->hw.mac.type == e1000_82576)
                                schedule_work(&adapter->ptp_tx_work);
                }
        }
Some network devices can timestamp packets in hardware using the Precision Time Protocol. The driver code handles that here when a user requests hardware timestampping.

The if statement above checks for the SKBTX_HW_TSTAMP flag. This flag indicates that the user requested hardware timestamping. If the user requested hardware timestamping, the code will next check if ptp_tx_skb is set. One packet can be timestampped at a time, so a reference to the packet being timestampped is taken here and the SKBTX_IN_PROGRESS flag is set on the skb. The tx_flags are updated to mark the IGB_TX_FLAGS_TSTAMP flag. The tx_flags variable will be copied into the buffer info structure later.

A reference is taken to the skb, the current jiffies count is copied to ptp_tx_start. This value will be used by other code in the driver to ensure that the TX hardware timestampping is not hanging. Finally, the schedule_work function is used to kick the workqueue if this is an 82576 ethernet hardware adapter.

        if (vlan_tx_tag_present(skb)) {
                tx_flags |= IGB_TX_FLAGS_VLAN;
                tx_flags |= (vlan_tx_tag_get(skb) << IGB_TX_FLAGS_VLAN_SHIFT);
        }
The code above will check if the vlan_tci field of the skb was set. If it is set, then the IGB_TX_FLAGS_VLAN flag is enabled and the vlan ID is stored.

        /* record initial flags and protocol */
        first->tx_flags = tx_flags;
        first->protocol = protocol;
The flags and protocol are recorded to the buffer info structure.

        tso = igb_tso(tx_ring, first, &hdr_len);
        if (tso < 0)
                goto out_drop;
        else if (!tso)
                igb_tx_csum(tx_ring, first);
Next, the driver calls its internal function igb_tso. This function will determine if an skb needs fragmentation. If so, the buffer info reference (first) will have its flags updated to indicate to the hardware that TSO is required.

igb_tso will return 0 is TSO is unncessary, otherwise 1 is returned. If 0 is returned, igb_tx_csum will be called to deal with enabling checksum offloading if needed and if supported for this protocol. The igb_tx_csum function will check the properties of the skb and flip some flag bits in the buffer info first to signal that checksum offloading is needed.

        igb_tx_map(tx_ring, first, hdr_len);
The igb_tx_map function is called to prepare the data to be consumed by the device for transmit. We’ll examine this function in detail next.

        /* Make sure there is space in the ring for the next send. */
        igb_maybe_stop_tx(tx_ring, DESC_NEEDED);

        return NETDEV_TX_OK;
Once the the transmit is complete, the driver checks to ensure that there is sufficient space available for another transmit. If not, the queue is shutdown. In either case NETDEV_TX_OK is returned to the higher layer (the qdisc code).

out_drop:
        igb_unmap_and_free_tx_resource(tx_ring, first);

        return NETDEV_TX_OK;
}
Finally, some error handling code. This code is only hit if igb_tso hits an error of some kind. The igb_unmap_and_free_tx_resource is used to clean up data. NETDEV_TX_OK is returned in this case as well. The transmit was not successful, but the driver freed the resources associated and there is nothing left to do. Note that this driver does not increment packet drops in this case, but it probably should.

### 9.3 igb_tx_map

The igb_tx_map function handles the details of mapping skb data to DMA-able regions of RAM. It also updates the transmit queue’s tail pointer on the device, which is what triggers the device to “wake up”, fetch the data from RAM, and begin transmitting the data.

Let’s take a look, briefly, at how this function works:

static void igb_tx_map(struct igb_ring *tx_ring,
                       struct igb_tx_buffer *first,
                       const u8 hdr_len)
{
        struct sk_buff *skb = first->skb;

                /* ... other variables ... */

        u32 tx_flags = first->tx_flags;
        u32 cmd_type = igb_tx_cmd_type(skb, tx_flags);
        u16 i = tx_ring->next_to_use;

        tx_desc = IGB_TX_DESC(tx_ring, i);

        igb_tx_olinfo_status(tx_ring, tx_desc, tx_flags, skb->len - hdr_len);

        size = skb_headlen(skb);
        data_len = skb->data_len;

        dma = dma_map_single(tx_ring->dev, skb->data, size, DMA_TO_DEVICE);
The code above does a few things:

Declares a set of variables and initializes them.
Uses the IGB_TX_DESC macro to determine obtain a reference to the next available descriptor.
igb_tx_olinfo_status will update the tx_flags and copy them into the descriptor (tx_desc).
The size and data length are captured so they can be used later.
dma_map_single is used to construct any memory mapping necessary to obtain a DMA-able address for skb->data. This is done so that the device can read the packet data from memory.
What follows next is a very dense loop in the driver to deal with generating a valid mapping for each fragment of a skb. The details of how exactly this happens are not particularly important, but are worth mentioning:

The driver iterates across the set of packet fragments.
The current descriptor has the DMA address of the data filled in.
If the size of the fragment is larger than what a single IGB descriptor can transmit, multiple descriptors are constructed to point to chunks of the DMA-able region until the entire fragment is pointed to by descriptors.
The descriptor iterator is bumped.
The remaining length is reduced.
The loop terminates when either: no fragments are remaining or the entire data length has been consumed.
The code for the loop is provided below for reference with the above description. This should illustrate further to readers that avoiding fragmentation, if at all possible, is a good idea. Lots of additional code needs to run to deal with it at every layer of the stack, including the driver.

        tx_buffer = first;

        for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
                if (dma_mapping_error(tx_ring->dev, dma))
                        goto dma_error;

                /* record length, and DMA address */
                dma_unmap_len_set(tx_buffer, len, size);
                dma_unmap_addr_set(tx_buffer, dma, dma);

                tx_desc->read.buffer_addr = cpu_to_le64(dma);

                while (unlikely(size > IGB_MAX_DATA_PER_TXD)) {
                        tx_desc->read.cmd_type_len =
                                cpu_to_le32(cmd_type ^ IGB_MAX_DATA_PER_TXD);

                        i++;
                        tx_desc++;
                        if (i == tx_ring->count) {
                                tx_desc = IGB_TX_DESC(tx_ring, 0);
                                i = 0;
                        }
                        tx_desc->read.olinfo_status = 0;

                        dma += IGB_MAX_DATA_PER_TXD;
                        size -= IGB_MAX_DATA_PER_TXD;

                        tx_desc->read.buffer_addr = cpu_to_le64(dma);
                }

                if (likely(!data_len))
                        break;

                tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type ^ size);

                i++;
                tx_desc++;
                if (i == tx_ring->count) {
                        tx_desc = IGB_TX_DESC(tx_ring, 0);
                        i = 0;
                }
                tx_desc->read.olinfo_status = 0;

                size = skb_frag_size(frag);
                data_len -= size;

                dma = skb_frag_dma_map(tx_ring->dev, frag, 0,
                                       size, DMA_TO_DEVICE);

                tx_buffer = &tx_ring->tx_buffer_info[i];
        }
Once all the necessary descriptors have been constructed and all of the skb’s data has been mapped to DMA-able addresses, the driver proceeds to its final steps to trigger a transmit:

        /* write last descriptor with RS and EOP bits */
        cmd_type |= size | IGB_TXD_DCMD;
        tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type);
A terminating descriptor is written to indicate to the device that it is the last descriptor.

        netdev_tx_sent_queue(txring_txq(tx_ring), first->bytecount);

        /* set the timestamp */
        first->time_stamp = jiffies;
The netdev_tx_sent_queue function is called with the number of bytes being added to this transmit queue. This function is part of the byte query limit feature that we’ll see shortly in more detail. The current jiffies are stored in the first buffer info structure.

Next, something a bit tricky:

        /* Force memory writes to complete before letting h/w know there
         * are new descriptors to fetch.  (Only applicable for weak-ordered
         * memory model archs, such as IA-64).
         *
         * We also need this memory barrier to make certain all of the
         * status bits have been updated before next_to_watch is written.
         */
        wmb();

        /* set next_to_watch value indicating a packet is present */
        first->next_to_watch = tx_desc;

        i++;
        if (i == tx_ring->count)
                i = 0;

        tx_ring->next_to_use = i;

        writel(i, tx_ring->tail);

        /* we need this if more than one processor can write to our tail
         * at a time, it synchronizes IO on IA64/Altix systems
         */
        mmiowb();

        return;
The code above is doing a few important things:

1.Start by using the wmb function is called to force memory writes to complete. This executed as a special instruction appropriate for the CPU platform and is commonly referred to as a “write barrier.” This is important on certain CPU architectures because if we trigger the device to start DMA without ensuring that all memory writes to update internal state have completed, the device may read data from RAM that is not in a consistent state. This article and this lecture dive into the details on memory ordering.

The next_to_watch field is set. It will be used later during the completion phase.
Counters are bumped, and the next_to_use field of the transmit queue is updated to the next available descriptor.
The transmit queue’s tail is updated with a writel function. writel writes a “long” to a memory mapped I/O address. In this case, the address is tx_ring->tail (which is a hardware address) and the value to be written is i. This write to the device triggers the device to let it know that additional data is ready to be DMA’d from RAM and written to the network.
Finally, call the mmiowb function. This function will execute the appropriate instruction for the CPU architecture causing memory mapped write operations to be ordered. It is also a write barrier, but for memory mapped I/O writes.
You can read some excellent documentation about memory barriers included with the Linux kernel, if you are curious to learn more about wmb, mmiowb, and when to use them.

Finally, the code wraps up with some error handling. This code only executes if an error was returned from the DMA API when attemtping to map skb data addresses to DMA-able addresses.

dma_error:
        dev_err(tx_ring->dev, "TX DMA map failed\n");

        /* clear dma mappings for failed tx_buffer_info map */
        for (;;) {
                tx_buffer = &tx_ring->tx_buffer_info[i];
                igb_unmap_and_free_tx_resource(tx_ring, tx_buffer);
                if (tx_buffer == first)
                        break;
                if (i == 0)
                        i = tx_ring->count;
                i--;
        }

        tx_ring->next_to_use = i;
Before moving on to the transmit completion, let’s examine something we passed over above: dynamic queue limits.

#### Dynamic Queue Limits (DQL)

As you’ve seen throughout this post, network data spends a lot of time sitting queues at various stages as it moves closer and closer to the device for transmission. As queue sizes increase, packets spend longer sitting in queues not being transmit i.e. packet transmit latency increases as queue size increases.

One way to fight this is with back pressure. The dynamic queue limit (DQL) system is a mechanism that device drivers can use to apply back pressure to the networking system to prevent too much data from being queued for transmit when the device is unable to transmit,

To use this system, network device drivers need to make a few simple API calls during their transmit and completion routines. The DQL system internally will use an algorithm to determine when sufficient data is in transmit. Once this limit is reached, the transmit queue will be temporarily disabled. This queue disabling is what produces the back pressure against the networking system. The queue will be automatically re-enabled when the DQL system determines enough data has finished transmission.

Check out this excellent set of slides about the DQL system for some performance data and an explanation of the internal algorithm in DQL.

The function netdev_tx_sent_queue called in the code we just saw is part of the DQL API. This function is called when data is queued to the device for transmit. Once the transmit completes, the driver calls netdev_tx_completed_queue. Internally, both of these functions will call into the DQL library (found in ./lib/dynamic_queue_limits.c and ./include/linux/dynamic_queue_limits.h) to determine if the transmit queue should be disabled, re-enabled, or left as-is.

DQL exports statistics and tuning knobs in sysfs. Tuning DQL should not be necessary; the algorithm will adjust its parameters over time. Nevertheless, in the interest of completeness we’ll see later how to monitor and tune DQL.

### 9.4 Transmit completions

Once the device has transmit the data, it will generate an interrupt to signal that transmission is complete. The device driver can then schedule some long running work to be completed, like unmapping memory regions and freeing data. How exactly this works is device specific. In the case of the igb driver (and its associated devices), the same IRQ is fired for transmit completion and packet receive. This means that for the igb driver the NET_RX is used to process both transmit completions and incoming packet receives.

Let me re-state that to emphasize the importance of this: your device may raise the same interrupt for receiving packets that it raises to signal that a packet transmit has completed. If it does, the NET_RX softirq runs to process both incoming packets and transmit completions.

Since both operations share the same IRQ, only a single IRQ handler function can be registered and it must deal with both possible cases. Recall the following flow when network data is received:

Network data is received.
The network device raises an IRQ.
The device driver’s IRQ handler executes, clearing the IRQ and ensuring that a softIRQ is scheduled to run (if not running already). This softIRQ that is triggered here is the NET_RX softIRQ.
The softIRQ executes essentially as a separate kernel thread. It runs and implements the NAPI poll loop.
The NAPI poll loop is simply a piece of code which executes in loop harvesting packets as long as sufficient budget is available.
Each time a packet is processed, the budget is decreased until there are no more packets to process, the budget reaches 0, or the time slice has expired.
Step 5 above in the igb driver (and the ixgbe driver [greetings, tyler]) processes TX completions before processing incoming data. Keep in mind that depending on the implementation of the driver, both processing functions for TX completions and incoming data may share the same processing budget. The igb and ixgbe drivers track the TX completion and incoming packet budgets separately, so processing TX completions will not necessarily exhaust the RX budget.

That said, the entire NAPI poll loop runs within a hard coded time slice. This means that if you have a lot of TX completion processing to handle, TX completions may eat more of the time slice than processing incoming data does. This may be an important consideration for those running network hardware in very high load environments.

Let’s see how this happens in practice for the igb driver.

#### 9.4.1 Transmit completion IRQ

Instead of restating information already covered in the Linux kernel receive side networking blog post, this post will instead list the steps in order and link to the appropriate sections in the receive side blog post until transmit completions are reached.

So, let’s start from the beginning:

The network device is brought up.
IRQ handlers are registered.
The user program sends data to a network socket. The data travels the network stack until the device fetches it from memory and transmits it.
The device finishes transmitting the data and raises an IRQ to signal transmit completion.
The driver’s IRQ handler executes to handle the interrupt.
The IRQ handler calls napi_schedule in response to the IRQ.
The NAPI code triggers the NET_RX softirq to execute.
The NET_RX sofitrq function, net_rx_action begins to execute.
The net_rx_action function calls the driver’s registered NAPI poll function.
The NAPI poll function, igb_poll, is executed.
The poll function igb_poll is where the code splits off and processes both incoming packets and transmit completions. Let’s dive into the code for this function and see where that happens.

#### 9.4.2 igb_poll

Let’s take a look at the code for igb_poll (from ./drivers/net/ethernet/intel/igb/igb_main.c):

/**
 *  igb_poll - NAPI Rx polling callback
 *  @napi: napi polling structure
 *  @budget: count of how many packets we should handle
 **/
static int igb_poll(struct napi_struct *napi, int budget)
{
        struct igb_q_vector *q_vector = container_of(napi,
                                                     struct igb_q_vector,
                                                     napi);
        bool clean_complete = true;

#ifdef CONFIG_IGB_DCA
        if (q_vector->adapter->flags & IGB_FLAG_DCA_ENABLED)
                igb_update_dca(q_vector);
#endif
        if (q_vector->tx.ring)
                clean_complete = igb_clean_tx_irq(q_vector);

        if (q_vector->rx.ring)
                clean_complete &= igb_clean_rx_irq(q_vector, budget);

        /* If all work not completed, return budget and keep polling */
        if (!clean_complete)
                return budget;

        /* If not enough Rx work done, exit the polling mode */
        napi_complete(napi);
        igb_ring_irq_enable(q_vector);

        return 0;
}
This function performs a few operations, in order:

If Direct Cache Access (DCA) support is enabled in the kernel, the CPU cache is warmed so that accesses to the RX ring will hit CPU cache. You can read more about DCA in the Extras section of the receive side networking post.
igb_clean_tx_irq is called which performs the transmit completion operations.
igb_clean_rx_irq is called next which performs the incoming packet processing.
Finally, clean_complete is checked to determine if there was still more work that could have been done. If so, the budget is returned. If this happens, net_rx_action will move this NAPI structure to the end of the poll list to be processed again later.
To learn more about how igb_clean_rx_irq works, read this section of the previous blog post.

This blog post is concerned primarily with the transmit side, so we’ll continue by examining how igb_clean_tx_irq above works.

#### 9.4.3 igb_clean_tx_irq

Take a look at the source for this function in ./drivers/net/ethernet/intel/igb/igb_main.c.

It’s a bit long, so we’ll break it into chunks and go through it:

static bool igb_clean_tx_irq(struct igb_q_vector *q_vector)
{
        struct igb_adapter *adapter = q_vector->adapter;
        struct igb_ring *tx_ring = q_vector->tx.ring;
        struct igb_tx_buffer *tx_buffer;
        union e1000_adv_tx_desc *tx_desc;
        unsigned int total_bytes = 0, total_packets = 0;
        unsigned int budget = q_vector->tx.work_limit;
        unsigned int i = tx_ring->next_to_clean;

        if (test_bit(__IGB_DOWN, &adapter->state))
                return true;
The function begins by initializing some useful variables. One important one to take a look at is budget. As you can see above budget is initialized to this queue’s tx.work_limit. In the igb driver, tx.work_limit is initialized to a hardcoded value IGB_DEFAULT_TX_WORK (128).

It is important to note that while the TX completion code we are looking at now runs in the same NET_RX softirq as receive processing does, the TX and RX functions do not share a processing budget with each other in the igb driver. Since the entire poll function runs within the same time slice, it is not possible for a single run of the igb_poll function to starve incoming packet processing or transmit completions. As long as igb_poll is called, both will be handled.

Moving on, the snippet of code above finishes by checking if the network device is down. If so, it returns true and exits igb_clean_tx_irq.

        tx_buffer = &tx_ring->tx_buffer_info[i];
        tx_desc = IGB_TX_DESC(tx_ring, i);
        i -= tx_ring->count;
The tx_buffer variable is initialized to transmit buffer info structure at location tx_ring->next_to_clean (which itself is initialized to 0).
A reference to the associated descriptor is obtained and stored in tx_desc.
The counter i is decreased by the size of the transmit queue. This value can be adjusted (as we’ll see in the tuning section), but is initialized to IGB_DEFAULT_TXD (256).
Next, a loop begins. It includes some helpful comments to explain what is happening at each step:

        do {
                union e1000_adv_tx_desc *eop_desc = tx_buffer->next_to_watch;

                /* if next_to_watch is not set then there is no work pending */
                if (!eop_desc)
                        break;

                /* prevent any other reads prior to eop_desc */
                read_barrier_depends();

                /* if DD is not set pending work has not been completed */
                if (!(eop_desc->wb.status & cpu_to_le32(E1000_TXD_STAT_DD)))
                        break;

                /* clear next_to_watch to prevent false hangs */
                tx_buffer->next_to_watch = NULL;

                /* update the statistics for this packet */
                total_bytes += tx_buffer->bytecount;
                total_packets += tx_buffer->gso_segs;

                /* free the skb */
                dev_kfree_skb_any(tx_buffer->skb);

                /* unmap skb header data */
                dma_unmap_single(tx_ring->dev,
                                 dma_unmap_addr(tx_buffer, dma),
                                 dma_unmap_len(tx_buffer, len),
                                 DMA_TO_DEVICE);

                /* clear tx_buffer data */
                tx_buffer->skb = NULL;
                dma_unmap_len_set(tx_buffer, len, 0);
First eop_desc is set to the buffer’s next_to_watch field. This was set in the transmit code we saw earlier.
If eop_desc (eop = end of packet) is NULL, then there is no work pending.
The read_barrier_depends function is called, which will execute the appropriate CPU instruction for this CPU architecture to prevent reads from being reordered over this barrier.
Next, a status bit is checked in the end of packet descriptor eop_desc. If the E1000_TXD_STAT_DD bit is not set, then the transmit has not completed yet, so break from the loop.
Clear the tx_buffer->next_to_watch. A watchdog timer in the driver will be watching this field to determine if a transmit was hung. Clearing this field will prevent the watchdog from triggering.
Statistics counters are updated for total bytes and packets sent. These will be copied into the statistics counters that the driver reads once all descriptors have been processed.
The skb is freed.
dma_unmap_single is used to unmap the skb data region.
The tx_buffer->skb is set to NULL and the tx_buffer is unmapped.
Next, another loop is started inside of the loop above:

                /* clear last DMA location and unmap remaining buffers */
                while (tx_desc != eop_desc) {
                        tx_buffer++;
                        tx_desc++;
                        i++;
                        if (unlikely(!i)) {
                                i -= tx_ring->count;
                                tx_buffer = tx_ring->tx_buffer_info;
                                tx_desc = IGB_TX_DESC(tx_ring, 0);
                        }

                        /* unmap any remaining paged data */
                        if (dma_unmap_len(tx_buffer, len)) {
                                dma_unmap_page(tx_ring->dev,
                                               dma_unmap_addr(tx_buffer, dma),
                                               dma_unmap_len(tx_buffer, len),
                                               DMA_TO_DEVICE);
                                dma_unmap_len_set(tx_buffer, len, 0);
                        }
                }
This inner loop will loop over each transmit descriptor until tx_desc arrives at the eop_desc. This code unmaps data referenced by any of the additional descriptors.

The outer loop continues:

                /* move us one more past the eop_desc for start of next pkt */
                tx_buffer++;
                tx_desc++;
                i++;
                if (unlikely(!i)) {
                        i -= tx_ring->count;
                        tx_buffer = tx_ring->tx_buffer_info;
                        tx_desc = IGB_TX_DESC(tx_ring, 0);
                }

                /* issue prefetch for next Tx descriptor */
                prefetch(tx_desc);

                /* update budget accounting */
                budget--;
        } while (likely(budget));
The outer loop increments iterators and reduces the budget value. The loop invariant is checked to determine if the loop should continue.

        netdev_tx_completed_queue(txring_txq(tx_ring),
                                  total_packets, total_bytes);
        i += tx_ring->count;
        tx_ring->next_to_clean = i;
        u64_stats_update_begin(&tx_ring->tx_syncp);
        tx_ring->tx_stats.bytes += total_bytes;
        tx_ring->tx_stats.packets += total_packets;
        u64_stats_update_end(&tx_ring->tx_syncp);
        q_vector->tx.total_bytes += total_bytes;
        q_vector->tx.total_packets += total_packets;
This code:

Calls netdev_tx_completed_queue, which is part of the DQL API explained above. This will potentially re-enable a transmit queue if enough completions were processed.
Statistics are added to their appropriate places so that they can be accessed by the user as we’ll see later.
The code continues by first checking if the IGB_RING_FLAG_TX_DETECT_HANG flag is set. A watchdog timer sets this flag each time the timer callback is run, to enforce periodic checking of the transmit queue. If that flag happens to be on now, the code will continue and check if the transmit queue is hung:

        if (test_bit(IGB_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags)) {
                struct e1000_hw *hw = &adapter->hw;

                /* Detect a transmit hang in hardware, this serializes the
                 * check with the clearing of time_stamp and movement of i
                 */
                clear_bit(IGB_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags);
                if (tx_buffer->next_to_watch &&
                    time_after(jiffies, tx_buffer->time_stamp +
                               (adapter->tx_timeout_factor * HZ)) &&
                    !(rd32(E1000_STATUS) & E1000_STATUS_TXOFF)) {

                        /* detected Tx unit hang */
                        dev_err(tx_ring->dev,
                                "Detected Tx Unit Hang\n"
                                "  Tx Queue             <%d>\n"
                                "  TDH                  <%x>\n"
                                "  TDT                  <%x>\n"
                                "  next_to_use          <%x>\n"
                                "  next_to_clean        <%x>\n"
                                "buffer_info[next_to_clean]\n"
                                "  time_stamp           <%lx>\n"
                                "  next_to_watch        <%p>\n"
                                "  jiffies              <%lx>\n"
                                "  desc.status          <%x>\n",
                                tx_ring->queue_index,
                                rd32(E1000_TDH(tx_ring->reg_idx)),
                                readl(tx_ring->tail),
                                tx_ring->next_to_use,
                                tx_ring->next_to_clean,
                                tx_buffer->time_stamp,
                                tx_buffer->next_to_watch,
                                jiffies,
                                tx_buffer->next_to_watch->wb.status);
                        netif_stop_subqueue(tx_ring->netdev,
                                            tx_ring->queue_index);

                        /* we are about to reset, no point in enabling stuff */
                        return true;
                }
The if statement above checks:

tx_buffer->next_to_watch is set, and
That the current jiffies is greater than the time_stamp recorded on the transmit path to the tx_buffer with a timeout factor added, and
The device’s transmit status register is not set to E1000_STATUS_TXOFF.
If those three tests are all true, then an error is printed that a hang has been detected. netif_stop_subqueue is used to turn off the queue and true is returned.

Let’s continue reading the code to see what happens if there was no transmit hang check, or if there was, but no hang was detected:

#define TX_WAKE_THRESHOLD (DESC_NEEDED * 2)
        if (unlikely(total_packets &&
            netif_carrier_ok(tx_ring->netdev) &&
            igb_desc_unused(tx_ring) >= TX_WAKE_THRESHOLD)) {
                /* Make sure that anybody stopping the queue after this
                 * sees the new next_to_clean.
                 */
                smp_mb();
                if (__netif_subqueue_stopped(tx_ring->netdev,
                                             tx_ring->queue_index) &&
                    !(test_bit(__IGB_DOWN, &adapter->state))) {
                        netif_wake_subqueue(tx_ring->netdev,
                                            tx_ring->queue_index);

                        u64_stats_update_begin(&tx_ring->tx_syncp);
                        tx_ring->tx_stats.restart_queue++;
                        u64_stats_update_end(&tx_ring->tx_syncp);
                }
        }

        return !!budget;
In the above code the driver will restart the transmit queue if it was previously disabled. It first checks if:

Some packets were processed for completions (total_packets is non-zero), and
netif_carrier_ok to ensure the device has not been brought down, and
The number of unused descriptors in the transmit queue is greater than or equal to TX_WAKE_THRESHOLD. This threshold value appears to be 42 on my x86_64 system.
If all conditions are satisfied, a write barrier is used (smp_mb). Next another set of conditions are checked:

If the queue is stopped, and
The device is not down
Then netif_wake_subqueue called to wake up the transmit queue and signal to the higher layers that they may queue data again. The restart_queue statistics counter is incremented. We’ll see how to read this value next.

Finally, a boolean value is returned. If there was any remaining un-used budget true is returned, otherwise false. This value is checked in igb_poll to determine what to return back to net_rx_action.

#### 9.4.4 igb_poll return value

The igb_poll function has this code to determine what to return to net_rx_action:

        if (q_vector->tx.ring)
                clean_complete = igb_clean_tx_irq(q_vector);

        if (q_vector->rx.ring)
                clean_complete &= igb_clean_rx_irq(q_vector, budget);

        /* If all work not completed, return budget and keep polling */
        if (!clean_complete)
                return budget;
In other words, if:

igb_clean_tx_irq cleared all transmit completions without exhausting its transmit completion budget, and
igb_clean_rx_irq cleared all incoming packets without exhausting its packet processing budget
Then, the entire budget amount (which is hardcoded to 64 for most drivers including igb) will be returned. If either of RX or TX processing could not complete (because there was more work to do), then NAPI is disabled with a call to napi_complete and 0 is returned:

        /* If not enough Rx work done, exit the polling mode */
        napi_complete(napi);
        igb_ring_irq_enable(q_vector);

        return 0;
}

### 9.5 Monitoring network devices

There are several different ways to monitor your network devices offering different levels of granularity and complexity. Let’s start with most granular and move to least granular.

#### 9.5.1 Using ethtool -S

You can install ethtool on an Ubuntu system by running: sudo apt-get install ethtool.

Once it is installed, you can access the statistics by passing the -S flag along with the name of the network device you want statistics about.

Monitor detailed NIC device statistics (e.g., transmit errors) with `ethtool -S`.

$ sudo ethtool -S eth0
NIC statistics:
     rx_packets: 597028087
     tx_packets: 5924278060
     rx_bytes: 112643393747
     tx_bytes: 990080156714
     rx_broadcast: 96
     tx_broadcast: 116
     rx_multicast: 20294528
     ....
Monitoring this data can be difficult. It is easy to obtain, but there is no standardization of the field values. Different drivers, or even different versions of the same driver might produce different field names that have the same meaning.

You should look for values with “drop”, “buffer”, “miss”, “errors” etc in the label. Next, you will have to read your driver source. You’ll be able to determine which values are accounted for totally in software (e.g., incremented when there is no memory) and which values come directly from hardware via a register read. In the case of a register value, you should consult the data sheet for your hardware to determine what the meaning of the counter really is; many of the labels given via  ethtool can be misleading.

#### 9.5.2 Using sysfs

sysfs also provides a lot of statistics values, but they are slightly higher level than the direct NIC level stats provided.

You can find the number of dropped incoming network data frames for, e.g. eth0 by using cat on a file.

Monitor higher level NIC statistics with sysfs.

$ cat /sys/class/net/eth0/statistics/tx_aborted_errors
2
The counter values will be split into files like tx_aborted_errors, tx_carrier_errors, tx_compressed, tx_dropped, etc.

Unfortunately, it is up to the drivers to decide what the meaning of each field is, and thus, when to increment them and where the values come from. You may notice that some drivers count a certain type of error condition as a drop, but other drivers may count the same as a miss.

If these values are critical to you, you will need to read your driver source and device data sheet to understand exactly what your driver thinks each of these values means.

#### 9.5.3 Using /proc/net/dev

An even higher level file is /proc/net/dev which provides high-level summary-esque information for each network adapter on the system.

Monitor high level NIC statistics by reading /proc/net/dev.

$ cat /proc/net/dev
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
  eth0: 110346752214 597737500    0    2    0     0          0  20963860 990024805984 6066582604    0    0    0     0       0          0
    lo: 428349463836 1579868535    0    0    0     0          0         0 428349463836 1579868535    0    0    0     0       0          0
This file shows a subset of the values you’ll find in the sysfs files mentioned above, but it may serve as a useful general reference.

The caveat mentioned above applies here, as well: if these values are important to you, you will still need to read your driver source to understand exactly when, where, and why they are incremented to ensure your understanding of an error, drop, or fifo are the same as your driver.

### 9.6 Monitoring dynamic queue limits

You can monitor dynamic queue limits for a network device by reading the files located under: /sys/class/net/NIC/queues/tx-QUEUE_NUMBER/byte_queue_limits/.

Replacing NIC with your device name (eth0, eth1, etc) and tx-QUEUE_NUMBER with the transmit queue number (tx-0, tx-1, tx-2, etc).

Some of those files are:

hold_time: Initialized to HZ (a single hertz). If the queue has been full for hold_time, then the maximum size is decreased.
inflight: This value is equal to (number of packets queued - number of packets completed). It is the current number of packets being transmit for which a completion has not been processed.
limit_max: A hardcoded value, set to DQL_MAX_LIMIT (1879048192 on my x86_64 system).
limit_min: A hardcoded value, set to 0.
limit: A value between limit_min and limit_max which represents the current maximum number of objects which can be queued.
Before modifying any of these values, it is strongly recommended to read these presentation slides for an in-depth explanation of the algorithm.

Monitor packet transmits in flight by reading /sys/class/net/eth0/queues/tx-0/byte_queue_limits/inflight.

$ cat /sys/class/net/eth0/queues/tx-0/byte_queue_limits/inflight
350

### 9.7 Tuning network devices

#### 9.7.1 Check the number of TX queues being used

If your NIC and the device driver loaded on your system support multiple transmit queues, you can usually adjust the number of TX queues (also called TX channels), by using ethtool.

Check the number of NIC transmit queues with ethtool

$ sudo ethtool -l eth0
Channel parameters for eth0:
Pre-set maximums:
RX:   0
TX:   0
Other:    0
Combined: 8
Current hardware settings:
RX:   0
TX:   0
Other:    0
Combined: 4
This output is displaying the pre-set maximums (enforced by the driver and the hardware) and the current settings.

Note: not all device drivers will have support for this operation.

Error seen if your NIC doesn't support this operation.

$ sudo ethtool -l eth0
Channel parameters for eth0:
Cannot get device channel parameters
: Operation not supported
This means that your driver has not implemented the ethtool get_channels operation. This could be because the NIC doesn’t support adjusting the number of queues, doesn’t support multiple transmit queues, or your driver has not been updated to handle this feature.

#### 9.7.2 Adjust the number of TX queues used

Once you’ve found the current and maximum queue count, you can adjust the values by using sudo ethtool -L.

Note: some devices and their drivers only support combined queues that are paired for transmit and receive, as in the example in the above section.

Set combined NIC transmit and receive queues to 8 with ethtool -L

$ sudo ethtool -L eth0 combined 8
If your device and driver support individual settings for RX and TX and you’d like to change only the TX queue count to 8, you would run:

Set the number of NIC transmit queues to 8 with ethtool -L.

$ sudo ethtool -L eth0 tx 8
Note: making these changes will, for most drivers, take the interface down and then bring it back up; connections to this interface will be interrupted. This may not matter much for a one-time change, though.

#### 9.7.3 Adjust the size of the TX queues

Some NICs and their drivers also support adjusting the size of the TX queue. Exactly how this works is hardware specific, but luckily ethtool provides a generic way for users to adjust the size. Increasing the size of the TX may not make a drastic difference because DQL is used to prevent higher layer networking code from queueing more data at times. Nevertheless, you may want to increase the TX queues to the maximum size and let DQL sort everything else out for you:

Check current NIC queue sizes with ethtool -g

$ sudo ethtool -g eth0
Ring parameters for eth0:
Pre-set maximums:
RX:   4096
RX Mini:  0
RX Jumbo: 0
TX:   4096
Current hardware settings:
RX:   512
RX Mini:  0
RX Jumbo: 0
TX:   512
the above output indicates that the hardware supports up to 4096 receive and transmit descriptors, but it is currently only using 512.

Increase size of each TX queue to 4096 with ethtool -G

$ sudo ethtool -G eth0 tx 4096
Note: making these changes will, for most drivers, take the interface down and then bring it back up; connections to this interface will be interrupted. This may not matter much for a one-time change, though.

## 10 The End

The end! Now you know everything about how packet transmit works on Linux: from the user program to the device driver and back.
