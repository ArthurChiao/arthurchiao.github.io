#!/bin/bash
# 2020-05-06
# @ArthurChiao Github

PREFIX="network"

#######################################################################
# 1. NIC statistics
#######################################################################
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

#######################################################################
# 2. Hardware Interrupts
#######################################################################
interrupts_output() {
    PATTERN=$1
    METRIC=$PREFIX"_interrupts_by_cpu"

    egrep "$PATTERN" /proc/interrupts | awk -v metric=$METRIC \
        '{ for (i=2;i<=NF-3;i++) sum[i]+=$i;}
         END {
               for (i=2;i<=NF-3; i++) {
                   tags=sprintf("{\"cpu\":\"%d\"}", i-2);
                   printf(metric tags " " sum[i] "\n");
               }
         }'

    METRIC=$PREFIX"_interrupts_by_queue"
    egrep "$PATTERN" /proc/interrupts | awk -v metric=$METRIC \
        '{ for (i=2;i<=NF-3; i++)
               sum+=$i;
               tags=sprintf("{\"queue\":\"%s\"}", $NF);
               printf(metric tags " " sum "\n");
               sum=0;
         }'
}

# interface pattern
# eth: intel
# mlx: mellanox
interrupts_output "eth|mlx"

#######################################################################
# 3. Software Interrupts
#######################################################################
softirqs_output() {
    METRIC=$PREFIX"_softirqs"

    for dir in "NET_RX" "NET_TX"; do
        grep $dir /proc/softirqs | awk -v metric=$METRIC -v dir=$dir \
            '{ for (i=2;i<=NF-1;i++) {
                   tags=sprintf("{\"cpu\":\"%d\", \"direction\": \"%s\"}", i-2, dir); \
                   printf(metric tags " " $i "\n"); \
               }
             }'
    done
}

softirqs_output

#######################################################################
# 4. Kernel Processing Drops
#######################################################################
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

#######################################################################
# 5. TCP Abnormal Statistics
#######################################################################

# expected pattern
#
# $ netstat -s | grep "segments retransmited"
#    161119 segments retransmited
#
netstat_output() {
    PATTERN=$1
    ARG_IDX=$2

    METRIC=$PREFIX"_tcp"
    VAL=$(netstat -s | grep "$PATTERN" | awk -v i=$ARG_IDX '{print $i}')

    # generate "type" string with prefix and pattern
    #
    # 1. replace whitespaces with underlines
    # 2. remove trailing dollar symbol ('$') if there is
    #
    # e.g. "fast retransmits$" -> "fast_retransmits"
    #
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
