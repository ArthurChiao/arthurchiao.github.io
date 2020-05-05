---
layout    : post
title     : "Linux Trouble Shooting Cheat Sheet"
date      : 2020-05-05
lastupdate: 2020-05-05
categories: trouble-shooting
---

### Index

1. [Physical Resources](#ch_1)
    * [1.1 CPU](#ch_1.1)
    * [1.2 Memory](#ch_1.2)
    * [1.3 Network Interfaces](#ch_1.3)
    * [1.4 Storage Device I/O](#ch_1.4)
    * [1.5 Storage Capacity](#ch_1.5)
    * [1.6 Storage Controller](#ch_1.6)
    * [1.7 Network Controller](#ch_1.7)
    * [1.8 CPU Interconnect](#ch_1.8)
    * [1.9 Memory Interconnect](#ch_1.9)
    * [1.10 I/O Interconnect](#ch_1.10)
2. [Software Resources](#ch_2)
    * [2.1 Kernel mutex](#ch_2.1)
    * [2.2 User mutex](#ch_2.2)
    * [2.3 Task Capacity](#ch_2.3)
    * [2.4 File descriptors](#ch_2.4)

----

Contents from the wonderful book ***Systems Performance***: Enterprise and the
Cloud, and author's website: [USE Method: Linux Performance Checklist](http://www.brendangregg.com/USEmethod/use-linux.html).

Will be updated according to my own needs.

<a name="ch_1"></a>

# 1. Physical Resources

<a name="ch_1.1"></a>

## 1.1 CPU

```
+-----+-------------+----------------------------------------------------------------------------------+
|     |             | 1. per CPU                                                                       |
|     |             |     * `mpstat -P ALL 1`: `%idle`                                                 |
|     |             |     * `sar -P ALL`     : `%idle`                                                 |
|     |             | 2. system wide                                                                   |
|     |             |     * `vmstat 1`  : `id`                                                         |
|     |             |     * `sar -u 1 5`: `%idle`                                                      |
|     |             |     * `dstat -c`  : `idl`                                                        |
|     |             | 3. per process                                                                   |
|     | Utilization |     * `top`       : `%CPU`                                                       |
|     |             |     * `htop`      : `%CPU`                                                       |
|     |             |     * `ps -o pcpu`: `%CPU`                                                       |
|     |             |     * `pidstat 1` : `CPU`                                                        |
|     |             | 4. per kernel thread                                                             |
|     |             |     * `top` : `VIRT`                                                             |
|     |             |     * `hotp`: press `K` to sort, see `VIRT` column                               |
|     +-------------+----------------------------------------------------------------------------------|
|     |             | 1. system wide                                                                   |
| CPU |             |     * `vmstat 1`  : column `r` > CPU count                                       |
|     |             |     * `sar -q 1 5`: column `runq-sz` > CPU count                                 |
|     |             | 2. per process                                                                   |
|     | Saturation  |     * `cat /proc/<pid>/schedstat`: 2nd column (`sched_info`, `run_delay`)        |
|     |             |     * `getdelays.c`              : CPU                                           |
|     |             |     * `perf sched latency`       : show avg and max delay per schedule           |
|     |             |     * dynamic tracing, e.g. SystemTap `schedtimes.stp` queued (us)               |
|     +-------------+----------------------------------------------------------------------------------|
|     |             | * `perf` (LPE): if processor-specific error events (CPC) are available, e.g.     |
|     | Errors      |   AMD64's Single-bit ECC Errors                                                  |
|     |             |                                                                                  |
+-----+-------------+----------------------------------------------------------------------------------+
```

Explainations:

```
`sar -P ALL`: `%idle`
```

stands for: executing `sar -P ALL`, then check the `%idle` column in the output.
The others are similar.

<a name="ch_1.2"></a>

## 1.2 Memory

```
+--------+-------------+-------------------------------------------------------------------------------+
|        |             | 1. system wide                                                                |
|        |             |     * `free -m`     : `Mem`, `Swap`                                           |
|        |             |     * `vmstat 1`    : `swpd`, `free`                                          |
|        |             |     * `sar -r 1 5`  : `%memused`                                              |
|        | Utilization |     * `dstat -m`    : `free`                                                  |
|        |             |     * `slabtop -s c`: sort by cache size                                      |
|        |             | 2. per process                                                                |
|        |             |     * `top`/`htop`  : `RES` (resident memory), `VIRT` (virtual memory), `MEM` |
|        +-------------+-------------------------------------------------------------------------------|
|        |             | 1. system wide                                                                |
|        |             |     * `vmstat 1`  : `si`/`so` (swap)                                          |
|        |             |     * `sar -B 1 5`: `pgscank` + `pgscand` (scanning)                          |
|        |             |     * `sar -W 1 5`: `pswpin/s` + `pswpout/s`                                  |
| Memory | Saturation  | 2. per process                                                                |
|        |             |     * `getdelays.c`                             : SWAP                        |
|        |             |     * `cat /proc/<pid>/stat | awk '{print $10}'`: stands for minor fault rate |
|        |             |       (`min_flt`), or dynamic tracing                                         |
|        |             |     * `dmesg -T | grep killed`                  : OOM killer                  |
|        +-------------+-------------------------------------------------------------------------------|
|        |             | * `dmesg`: for physical failures                                              |
|        | Errors      | * dynamic tracing, e.g. `uprobes` for failed `malloc` (DTrace, SystemTap)     |
|        |             |                                                                               |
+--------+-------------+-------------------------------------------------------------------------------+
```

<a name="ch_1.3"></a>

## 1.3 Network Interfaces

```
+------------+-------------+---------------------------------------------------------------------------+
|            |             | * `ip -s link`    : statistics                                            |
|            | Utilization | * `sar -n DEV 1 5`: real time stats, e.g. rx pkts/s, rx bytes/s           |
|            |             |                                                                           |
|            +-------------+---------------------------------------------------------------------------|
|            |             | * `ifconfig`         : overruns, dropped                                  |
|  Network   |             | * `netstat -s`       : protocol statistics, e.g. IP, ICMP, UDP, TCP       |
| Interfaces | Saturation  | * `sar -n EDEV 1 5`  : real time interface errors                         |
|            |             | * `cat /proc/net/dev`: RX/TX drop                                         |
|            |             | * dynamic tracing of other TCP/IP stack queueing                          |
|            +-------------+---------------------------------------------------------------------------|
|            |             | * `ifconfig`                                   : errors, dropped          |
|            |             | * `netstat -i`                                 : RX-ERR, TX-ERR           |
|            |             | * `ip -s link`                                 : errors                   |
|            | Errors      | * `sar -n EDEV 1 5`                            : rxerr/s, txerr/s         |
|            |             | * `cat /proc/net/dev`                          : errs, drop               |
|            |             | * `cat /sys/class/net/<interface>/statistics/*`:                          |
|            |             | * dynamic tracing of driver function returns                              |
+------------+-------------+---------------------------------------------------------------------------+
```

<a name="ch_1.4"></a>

## 1.4 Storage Device I/O

```
+------------+-------------+---------------------------------------------------------------------------+
|            |             | 1. system wide                                                            |
|            |             |     * `iostat -xz 1`: `%util`                                             |
|            |             |     * `sar -d 1 5`  : `%util`                                             |
|            | Utilization | 2. per process                                                            |
|            |             |     * `iotop`                                                             |
|            |             |     * `cat /proc/<pid>/sched`                                             |
|  Storage   +-------------+---------------------------------------------------------------------------|
| Device I/O |             | * `iostat -xz 1`: `avgqu-sz` > 1, or high await                           |
|            |             | * `sar -d 1 5`  : `%util`                                                 |
|            | Saturation  | * LPE block probes for queue length/latency                               |
|            |             | * dynamic/static tracing of I/O subsystem (including LPE block probes)    |
|            |             |                                                                           |
|            +-------------+---------------------------------------------------------------------------|
|            |             | * `cat /sys/devices/../ioerr_cnt`                                         |
|            | Errors      | * `smartctl`                                                              |
|            |             | * dynamic/static tracing of I/O subsystem response codes                  |
+------------+-------------+---------------------------------------------------------------------------+
```

<a name="ch_1.5"></a>

## 1.5 Storage Capacity

```
+----------+-------------+-----------------------------------------------------------------------------+
|          |             | * `swapon -s`                                                               |
|          |             | * `free`                                                                    |
|          | Utilization | * `cat /proc/meminfo`: `SwapTotal`, `SwapFree`                              |
|          |             | * `df -h`            : file system info                                     |
|          |             |                                                                             |
|          +-------------+-----------------------------------------------------------------------------|
|          |             |                                                                             |
| Storage  | Saturation  | No sure this one makes sense —— once it's full, `ENOSPC`.                   |
| Capacity |             |                                                                             |
|          +-------------+-----------------------------------------------------------------------------|
|          |             | 1. file system                                                              |
|          |             |     * `strace` for `ENOSPC`                                                 |
|          | Errors      |     * dynamic tracing for `ENOSPC`                                          |
|          |             |     * `/var/log/messages` errs                                              |
|          |             |     * application log errors                                                |
+----------+-------------+-----------------------------------------------------------------------------+
```

<a name="ch_1.6"></a>

## 1.6 Storage Controller

```
+------------+-------------+----------------------------------------------------------------------------+
|            |             |                                                                            |
|            | Utilization | * `iostat -xz 1`: sum devices and compare to known IOPS/tput limits/card   |
|            |             |                                                                            |
|            +-------------+----------------------------------------------------------------------------|
|  Storage   |             |                                                                            |
| Controller | Saturation  | see storage device I/O saturation in the above.                            |
|            |             |                                                                            |
|            +-------------+----------------------------------------------------------------------------|
|            |             |                                                                            |
|            | Errors      | see storage device I/O errors in the above.                                |
|            |             |                                                                            |
+------------+-------------+----------------------------------------------------------------------------+
```

<a name="ch_1.7"></a>

## 1.7 Network Controller

```
+------------+-------------+---------------------------------------------------------------------------+
|            |             | * `ip -s link`                                                            |
|            |             | * `sar -n DEV 1 5`                                                        |
|            | Utilization | * `cat /proc/net/dev`                                                     |
|            |             | * supplementary by myself:                                                |
|            |             |     * `iftop`                                                             |
|  Network   +-------------+---------------------------------------------------------------------------|
| Controller |             |                                                                           |
|            | Saturation  | see network interfaces, saturation in the above.                          |
|            |             |                                                                           |
|            +-------------+---------------------------------------------------------------------------|
|            |             |                                                                           |
|            | Errors      | see network interfaces, errors.                                           |
|            |             |                                                                           |
+------------+-------------+---------------------------------------------------------------------------+
```

<a name="ch_1.8"></a>

## 1.8 CPU Interconnect

```
+--------------+-------------+-------------------------------------------------------------------------+
|              |             |                                                                         |
|              | Utilization | * LPE (CPC) for CPU interconnect ports, tput/max.                       |
|              |             |                                                                         |
|              +-------------+-------------------------------------------------------------------------|
|    CPU       |             |                                                                         |
| Interconnect | Saturation  | * LPE (CPC) for stall cycles.                                           |
|              |             |                                                                         |
|              +-------------+-------------------------------------------------------------------------|
|              |             |                                                                         |
|              | Errors      | * LPE (CPC) for whatever is available.                                  |
|              |             |                                                                         |
+--------------+-------------+-------------------------------------------------------------------------+
```

<a name="ch_1.9"></a>

## 1.9 Memory Interconnect

```
+--------------+-------------+-------------------------------------------------------------------------+
|              |             | * LPE (CPC) for for memory busses, tput/max                             |
|              |             | * CPI >= N, e.g. N=10                                                   |
|              | Utilization | * CPC local vs. remote counters                                         |
|              |             |                                                                         |
|              +-------------+-------------------------------------------------------------------------|
|   Memory     |             |                                                                         |
| Interconnect | Saturation  | * LPE (CPC) for stall cycles.                                           |
|              |             |                                                                         |
|              +-------------+-------------------------------------------------------------------------|
|              |             |                                                                         |
|              | Errors      | * LPE (CPC) for whatever is available.                                  |
|              |             |                                                                         |
+--------------+-------------+-------------------------------------------------------------------------+
```

<a name="ch_1.10"></a>

## 1.10 I/O Interconnect

```
+--------------+-------------+-------------------------------------------------------------------------+
|              |             | * LPE (CPC) for tput/max                                                |
|              | Utilization | * inference via known tput from iostat/ip/...                           |
|              |             |                                                                         |
|              +-------------+-------------------------------------------------------------------------|
|     I/O      |             |                                                                         |
| Interconnect | Saturation  | * LPE (CPC) for stall cycles.                                           |
|              |             |                                                                         |
|              +-------------+-------------------------------------------------------------------------|
|              |             |                                                                         |
|              | Errors      | * LPE (CPC) for whatever is available.                                  |
|              |             |                                                                         |
+--------------+-------------+-------------------------------------------------------------------------+
```

<a name="ch_2"></a>

# 2. Software Resources

<a name="ch_2.1"></a>

## 2.1 Kernel mutex

```
+--------+-------------+--------------------------------------------------------------------------------+
|        |             |                                                                                |
|        |             | * `cat /proc/lock_stat` (With `CONFIG_LOCK_STATS=y`): "holdtime-totat" /       |
|        | Utilization |   "acquisitions" (also see "holdtime-min", "holdtime-max") [8]                 |
|        |             | * dynamic tracing of lock functions or instructions (maybe)                    |
|        |             |                                                                                |
|        +-------------+--------------------------------------------------------------------------------|
|        |             | * `/proc/lock_stat` (With `CONFIG_LOCK_STATS=y`): "waittime-total",            |
| Kernel |             |   "contentions" (also see "waittime-min", "waittime-max")                      |
| Mutex  | Saturation  | * dynamic tracing of lock functions or instructions (maybe)                    |
|        |             | * spinning shows up with profiling (`perf record -a -g -F 997` ...,            |
|        |             |   oprofile, dynamic tracing)                                                   |
|        +-------------+--------------------------------------------------------------------------------|
|        |             | * dynamic tracing (eg, recusive mutex enter)                                   |
|        | Errors      | * other errors can cause kernel lockup/panic, debug with kdump/crash           |
|        |             |                                                                                |
+--------+-------------+--------------------------------------------------------------------------------+
```

<a name="ch_2.2"></a>

## 2.2 User mutex

```
+--------+-------------+--------------------------------------------------------------------------------+
|        |             | * `valgrind --tool=drd --exclusive-threshold=...` (held time)                  |
|        | Utilization | * dynamic tracing of lock to unlock function time                              |
|        |             |                                                                                |
|        +-------------+--------------------------------------------------------------------------------|
| User   |             | * `valgrind --tool=drd` to infer contention from held time                     |
| Mutex  | Saturation  | * dynamic tracing of synchronization functions for wait time                   |
|        |             | * profiling (oprofile, PEL, ...) user stacks for spins                         |
|        +-------------+--------------------------------------------------------------------------------|
|        |             | * `valgrind --tool=drd` various errors                                         |
|        | Errors      | * dynamic tracing of `pthread_mutex_lock()` for `EAGAIN`, `EINVAL`,            |
|        |             |   `EPERM`, `EDEADLK`, `ENOMEM`, `EOWNERDEAD`, ...                              |
+--------+-------------+--------------------------------------------------------------------------------+
```

<a name="ch_2.3"></a>

## 2.3 Task Capacity

```
+----------+-------------+------------------------------------------------------------------------------+
|          |             | * `top`/`htop`: "Tasks" (current)                                            |
|          | Utilization | * `sysctl kernel.threads-max`                                                |
|          |             | * `/proc/sys/kernel/threads-max` (max)                                       |
|          +-------------+------------------------------------------------------------------------------|
|          |             |                                                                              |
|   Task   |             | * threads blocking on memory allocation                                      |
| Capacity | Saturation  | * `sar -B`: at this point the page scanner ("pgscan*") should be running,    |
|          |             |   else examine using dynamic tracing                                         |
|          |             |                                                                              |
|          +-------------+------------------------------------------------------------------------------|
|          |             | * "can't fork()" errors                                                      |
|          | Errors      | * user-level threads: pthread_create() failures with EAGAIN, EINVAL, ...     |
|          |             | * kernel: dynamic tracing of kernel_thread() ENOMEM                          |
+----------+-------------+------------------------------------------------------------------------------+
```

<a name="ch_2.4"></a>

## 2.4 File descriptors

```
+-------------+-------------+---------------------------------------------------------------------------+
|             |             | 1. system-wide                                                            |
|             |             |     * `sar -v`, "file-nr" vs `/proc/sys/fs/file-max`                      |
|             |             |     * `dstat --fs`: "files"                                               |
|             | Utilization |     * `cat /proc/sys/fs/file-nr`                                          |
|             |             | 2. per-process                                                            |
|             |             |     * `ls /proc/<PID>/fd | wc -l` vs `ulimit -n`                          |
|             |             |                                                                           |
|    File     +-------------+---------------------------------------------------------------------------|
|             |             |                                                                           |
| Descriptors |             | does this make sense? I don't think there is any queueing or blocking,    |
|             | Saturation  | other than on memory allocation.                                          |
|             |             |                                                                           |
|             +-------------+---------------------------------------------------------------------------|
|             |             |                                                                           |
|             | Errors      | * strace errno == EMFILE on syscalls returning fds (eg, open(),           |
|             |             |   accept(), ...).                                                         |
+-------------+-------------+---------------------------------------------------------------------------+
```
