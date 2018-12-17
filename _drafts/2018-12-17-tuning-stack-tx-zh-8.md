---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 8"
date:   2018-12-17
author: ArthurChiao
categories: linux network stack monitoring tuning
---

## 11 Extras

### 11.1 Reducing ARP traffic (MSG_CONFIRM)

The send, sendto, and and sendmsg system calls all take a flags parameter. If you pass the MSG_CONFIRM flag to these system calls from your application, it will cause the dst_neigh_output function in the kernel on the send path to update the timestamp of the neighbour structure. The consequence of this is that the neighbour structure will not be garbage collected. This prevents additional ARP traffic from being generated as the neighbour cache entry will stay warmer, longer.

### 11.2 UDP Corking

We examined UDP corking extensively throughout the UDP protocol stack. If you want to use it in your application, you can enable UDP corking by calling setsockopt with level set to IPPROTO_UDP, optname set to UDP_CORK, and optval set to 1.

### 11.3 Timestamping

As mentioned in the above blog post, the networking stack can collect timestamps of outgoing data. See the above network stack walkthrough to see where transmit timestamping happens in software. Some NICs even support timestamping in hardware, too.

This is a useful feature if you’d like to try to determine how much latency the kernel network stack is adding to sending packets.

The kernel documentation about timestamping is excellent and there is even an included sample program and Makefile you can check out!.

Determine which timestamp modes your driver and device support with ethtool -T.

$ sudo ethtool -T eth0
Time stamping parameters for eth0:
Capabilities:
  software-transmit     (SOF_TIMESTAMPING_TX_SOFTWARE)
  software-receive      (SOF_TIMESTAMPING_RX_SOFTWARE)
  software-system-clock (SOF_TIMESTAMPING_SOFTWARE)
PTP Hardware Clock: none
Hardware Transmit Timestamp Modes: none
Hardware Receive Filter Modes: none
This NIC, unfortunately, does not support hardware transmit timestamping, but software timestamping can still be used on this system to help me determine how much latency the kernel is adding to my packet transmit path.

## 12 Conclusion

The Linux networking stack is complicated.

As we saw above, even something as simple as the NET_RX can’t be guaranteed to work as we expect it to. Even though RX is in the name, transmit completions are still processed in this softIRQ.

This highlights what I believe to be the core of the issue: optimizing and monitoring the network stack is impossible unless you carefully read and understand how it works. You cannot monitor code you don’t understand at a deep level.

## 13 Help with Linux networking or other systems
