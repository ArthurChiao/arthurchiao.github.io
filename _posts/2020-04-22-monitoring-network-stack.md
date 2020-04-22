---
layout    : post
title     : "Monitoring Network Stack"
date      : 2020-04-22
lastupdate: 2020-04-22
author    : ArthurChiao
categories: network
---

This post shows how to collecte metrics from your Linux network stack (with
bash scripts), and monitoring the stack status with Prometheus and Grafana.

This post assumes you have read through the following posts:

1. [Monitoring and Tuning the Linux Networking Stack: Receiving Data](https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/)
2. [Monitoring and Tuning the Linux Networking Stack: Sending Data](https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data)

Or my translations if you'd like to read Chinese:

1. [Linux 网络栈监控和调优：接收数据（2016）]({% link _posts/2018-12-05-tuning-stack-rx-zh.md %})
2. [Linux 网络栈监控和调优：发送数据（2017）]({% link _posts/2018-12-17-tuning-stack-tx-zh.md %})

Besides that, some basic understandings of Prometheus and Grafana are also needed.

**Metrics from bottom to up**:

1. NIC statistics
2. Hardware interrupts
3. Software interrupts
4. Kernel processing drops
5. Abnormal TCP statistics

No more words, let start.

# 1. NIC

## 1.1 Raw Data

NIC stats from `/sys/class/net/<nic>/statistics`:

```shell
$ ls /sys/class/net/eth0/statistics
collisions     rx_dropped        rx_missed_errors   tx_bytes           tx_fifo_errors
multicast      rx_errors         rx_nohandler       tx_carrier_errors  tx_heartbeat_errors
rx_bytes       rx_fifo_errors    rx_over_errors     tx_compressed      tx_packets
rx_compressed  rx_frame_errors   rx_packets         tx_dropped         tx_window_errors
rx_crc_errors  rx_length_errors  tx_aborted_errors  tx_errors

$ cat /sys/class/net/eth0/statistics/rx_crc_errors
0
```

## 1.2 Metric

We will arrange our metrics in [Prometheus format](https://prometheus.io/docs/concepts/data_model/):

```shell
$ cat collect-network-stats.sh
#!/bin/bash

PREFIX="network"

nic_stats_output() {
    NIC=$1
    METRIC=$PREFIX"_nic_stats";

    for f in $(ls /sys/class/net/$NIC/statistics/); do
        TAGS="{\"nic\":\"$NIC\",\"type\":\"$f\"}";
        VAL=$(cat /sys/class/net/$NIC/statistics/$f 2>/dev/null);
        echo $METRIC$TAGS $VAL;
    done
}

nic_stats_output eth0
nic_stats_output eth1
```

Test:

```shell
$ ./collect-network-stats.sh
network_nic_stats{"nic":"eth0","type":"collisions"} 0
network_nic_stats{"nic":"eth0","type":"multicast"} 17775912
network_nic_stats{"nic":"eth0","type":"rx_bytes"} 322700688616
network_nic_stats{"nic":"eth0","type":"rx_compressed"} 0
network_nic_stats{"nic":"eth0","type":"rx_crc_errors"} 0
network_nic_stats{"nic":"eth0","type":"rx_dropped"} 0
network_nic_stats{"nic":"eth0","type":"rx_errors"} 0
...
```

## 1.3 Monitoring Panels

Push the metrics into your prometheus server, or, configure your prometheus to
pull this data, we could create a panel like this:

<p align="center"><img src="/assets/img/monitoring-network-stack/nic-stats.png" width="95%" height="95%"></p>

Where, the Grafane query is:

```shell
avg(irate(network_nic_stats{host="$hostname"})*60) by (nic, type)
```

Note that our collecting agent automatically added a new tag `host=<hostname>`
to all the metrics, so we could filter the metrics with `host="$hostname"`.

# 2. Hardware Interrupts

## 2.1 Raw Data

```shell
$ cat /proc/interrupts
         CPU0     CPU1       CPU2     ...    CPU30    CPU31
 ...

 139:       0        0          0     ...        0        0  IR-PCI-MSI 1572864-edge      eth0-tx-0
 140:       0        0          0     ...        0        0  IR-PCI-MSI 1572865-edge      eth0-rx-1
 141:       0        0          0     ...        0        0  IR-PCI-MSI 1572866-edge      eth0-rx-2
 142:       0        0     308405     ...        0        0  IR-PCI-MSI 1572867-edge      eth0-rx-3
 143:       0        0          0     ...        0        0  IR-PCI-MSI 1572868-edge      eth0-rx-4
 144:       0        0          4     ...        0        0  IR-PCI-MSI 1574912-edge      eth1-tx-0
 145:       0        0          0     ...        0        0  IR-PCI-MSI 1574913-edge      eth1-rx-1
 146:       0       38        673     ...        0        0  IR-PCI-MSI 1574914-edge      eth1-rx-2
 147:       0    75604          0     ...        0        0  IR-PCI-MSI 1574915-edge      eth1-rx-3
 148:       0     3086     824199     ...        0        0  IR-PCI-MSI 1574916-edge      eth1-rx-4
 ...
```

## 2.2 Metric

Add this code snippet to our script:

```shell
interrupts_output() {
    METRIC=$PREFIX"_interrupts_by_cpu"

    cat /proc/interrupts | grep "eth" | awk -v metric=$METRIC \
        '{ for (i=2;i<=NF-3;i++) sum[i]+=$i;}
         END {
               for (i=2;i<=NF-3; i++) {
                   tags=sprintf("{\"cpu\":\"%d\"}", i-2);
                   printf(metric tags " " sum[i] "\n");
               }
         }'

    METRIC=$PREFIX"_interrupts_by_queue"
    cat /proc/interrupts | grep "eth" | awk -v metric=$METRIC \
        '{ for (i=2;i<=NF-3; i++)
               sum+=$i;
               tags=sprintf("{\"queue\":\"%s\"}", $NF);
               printf(metric tags " " sum "\n");
               sum=0;
         }'
}

interrupts_output
```

```shell
$ ./collect-network-stats.sh
network_interrupts_by_cpu{"cpu":"0"} 0
network_interrupts_by_cpu{"cpu":"1"} 6078192
network_interrupts_by_cpu{"cpu":"2"} 85118785
...
network_interrupts_by_queue{"queue":"eth0-tx-0"} 190533384
network_interrupts_by_queue{"queue":"eth0-rx-1"} 26873848
network_interrupts_by_queue{"queue":"eth0-rx-2"} 23715431
network_interrupts_by_queue{"queue":"eth0-rx-3"} 87702361
...
network_interrupts_by_queue{"queue":"eth1-rx-4"} 3119407
```

## 2.3 Panels

<p align="center"><img src="/assets/img/monitoring-network-stack/interrupts.png" width="100%" height="100%"></p>

```shell
avg(irate(network_interrupts_by_cpu{host=~"$hostname"})) by (cpu)
```

```shell
avg(irate(network_interrupts_by_queue{host=~"$hostname"})) by (queue)
```

# 3. Software Interrupts (softirq)

## 3.1 Data

```shell
$ cat /proc/softirqs
                    CPU0       CPU1    ...      CPU62      CPU63
          HI:          1          0    ...         0          0
       TIMER:   20378862    2149097    ...         0          0
      NET_TX:          5          1    ...         0          0
      NET_RX:       1179       1868    ...         0          0
       BLOCK:      88034      33007    ...         0          0
    IRQ_POLL:          0          0    ...         0          0
     TASKLET:         22          0    ...         0          0
       SCHED:   13906041    1474443    ...         0          0
     HRTIMER:          0          0    ...         0          0
         RCU:   12121418    1964562    ...         0          0
```

## 3.2 Metric

```shell
softirqs_output() {
    METRIC=$PREFIX"_softirqs"

    for dir in "NET_RX" "NET_TX"; do
        cat /proc/softirqs | grep $dir | awk -v metric=$METRIC -v dir=$dir \
            '{ for (i=2;i<=NF-1;i++) {
                   tags=sprintf("{\"cpu\":\"%d\", \"direction\": \"%s\"}", i-2, dir); \
                   printf(metric tags " " $i "\n"); \
               }
             }'
    done
}

softirqs_output
```

```shell
$ ./collect-network-stats.sh
network_softirqs{"cpu":"0", "direction": "NET_RX"} 196082
network_softirqs{"cpu":"1", "direction": "NET_RX"} 119888284
network_softirqs{"cpu":"2", "direction": "NET_RX"} 189840914
network_softirqs{"cpu":"3", "direction": "NET_RX"} 114621858
network_softirqs{"cpu":"4", "direction": "NET_RX"} 1453599
network_softirqs{"cpu":"5", "direction": "NET_RX"} 192694791
network_softirqs{"cpu":"6", "direction": "NET_RX"} 49328487
...
```


## 3.3 Panels

<p align="center"><img src="/assets/img/monitoring-network-stack/softirqs.png" width="100%" height="100%"></p>

Grafana queries:

```shell
avg(irate(network_interrupts_by_cpu{host="$hostname",direction="NET_RX"})) by (cpu)
```

```shell
avg(irate(network_interrupts_by_cpu{host="$hostname",direction="NET_TX"})) by (cpu)
```

# 4. Kernel Processing Drops (`/proc/net/softnet_stat`)

## 4.1 Raw Data

```shell
$ cat /proc/net/softnet_stat
00049007 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
074c3e6e 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
0c98d81b 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
07212d42 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
0018ad7c 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
00037314 00000000 00000002 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
...
```

## 4.2 Metric

```shell
softnet_stat_output() {
    TYP=$1
    IDX=$2

    METRIC=$PREFIX"_softnet_stat"

    VAL=$(cat /proc/net/softnet_stat | awk -v IDX="$IDX" '{sum+=strtonum("0x"$IDX);} END{print sum;}')
    TAGS="{\"type\":\"$TYP\"}";

    echo $METRIC$TAGS $VAL;
}

# Format of /proc/net/softnet_stat:
#
# column 1  : received frames
# column 2  : dropped
# column 3  : time_squeeze
# column 4-8: all zeros
# column 9  : cpu_collision
# column 10 : received_rps
# column 11 : flow_limit_count
#
# http://arthurchiao.art/blog/tuning-stack-rx-zh/
softnet_stat_output "dropped" 2
softnet_stat_output "time_squeeze" 3
softnet_stat_output "cpu_collision" 9
softnet_stat_output "received_rps" 10
softnet_stat_output "flow_limit_count" 11
```

Run:

```shell
$ ./collect-network-stats.sh
network_softnet_stat{"type":"dropped"} 0
network_softnet_stat{"type":"time_squeeze"} 4
network_softnet_stat{"type":"cpu_collision"} 0
network_softnet_stat{"type":"received_rps"} 0
network_softnet_stat{"type":"flow_limit_count"} 0
```

## 4.3 Panel

<p align="center"><img src="/assets/img/monitoring-network-stack/kernel-drops.png" width="80%" height="80%"></p>

Grafana queries:

```shell
avg(irate(network_softnet_stat{host="$hostname"})) by (type)
```

# 5. TCP Statistics from `netstat -s`

## 5.1 Raw Data

```shell
$ netstat -s
Ip:
    397147220 total packets received
    621 with invalid headers
    1 with invalid addresses
    16591642 forwarded
...
Tcp:
    53687405 active connections openings
    449771 passive connection openings
    52888864 failed connection attempts
    66565 connection resets received
...
TcpExt:
    18 ICMP packets dropped because they were out-of-window
    4 ICMP packets dropped because socket was locked
    643745 TCP sockets finished time wait in fast timer
    8 packets rejects in established connections because of timestamp
...
```

## 5.2 Metric

```shell
netstat_output() {
    PATTERN=$1
    ARG_IDX=$2

    METRIC=$PREFIX"_tcp"

    # generate "type" string with prefix and pattern
    #
    # 1. replace whitespaces with underlines
    # 2. remove trailing dollar symbol ('$') if there is
    #
    # e.g. "fast retransmits$" -> "fast_retransmits"
    #
    VAL=$(netstat -s | grep "$PATTERN" | awk -v i=$ARG_IDX '{print $i}')

    TYP=$(echo "$PATTERN" | tr ' ' '_' | sed 's/\$//g')
    TAGS="{\"type\":\"$TYP\"}";

    echo $METRIC$TAGS $VAL;
}

netstat_output "segments retransmited" 1
netstat_output "TCPLostRetransmit" 2
netstat_output "fast retransmits$" 1
netstat_output "retransmits in slow start" 1
netstat_output "classic Reno fast retransmits failed" 1
netstat_output "TCPSynRetrans" 2

netstat_output "bad segments received" 1
netstat_output "resets sent$" 1
netstat_output "connection resets received$" 1

netstat_output "connections reset due to unexpected data$" 1
netstat_output "connections reset due to early user close$" 1
```

Run:

```shell
$ ./collect-network-stats.sh
network_tcp{"type":"segments_retransmited"} 618183
network_tcp{"type":"TCPLostRetransmit"} 133668
network_tcp{"type":"fast_retransmits"} 45745
network_tcp{"type":"retransmits_in_slow_start"} 62977
network_tcp{"type":"classic_Reno_fast_retransmits_failed"} 418
network_tcp{"type":"TCPSynRetrans"} 175919
network_tcp{"type":"bad_segments_received"} 399
network_tcp{"type":"resets_sent"} 234094
network_tcp{"type":"connection_resets_received"} 66553
network_tcp{"type":"connections_reset_due_to_unexpected_data"} 93589
network_tcp{"type":"connections_reset_due_to_early_user_close"} 6522
```

## 5.3 Panel

<p align="center"><img src="/assets/img/monitoring-network-stack/tcp-stats.png" width="80%" height="80%"></p>

Grafana queries:

```shell
avg(irate(network_tcp{host="$hostname"})*60) by (type)
```

# 6. More metrics

This post serves as a introductory guide for how to monitoring you network
stack with Prometheus and Grafana.

Actually there are more metrics than we have shown in the above, such as, you
could monitor the NIC bandwidth with metrics from NIC statistics:

<p align="center"><img src="/assets/img/monitoring-network-stack/nic-bw.png" width="70%" height="70%"></p>

Besides, you could also configure alerting rules on Grafana panels, e.g.
alerting when NIC errors exceeds a pre defined threshold.

# Appendix

1. [collect-network-stats.sh](/assets/img/monitoring-network-stack/collect-network-stats.sh)
