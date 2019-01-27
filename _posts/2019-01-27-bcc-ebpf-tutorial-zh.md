---
layout: post
title:  "bcc/ebpf 安装及示例"
date:   2019-01-27
categories: bcc ebpf
---

eBPF是Linux内核近几年最为引人注目的特性之一，通过一个内核内置的字节码虚拟机，完
成数据包过滤、调用栈跟踪、耗时统计、热点分析等等高级功能，是Linux系统和Linux应用
的功能和性能分析利器。较为完整的eBPF介绍可参见[这篇](A thorough introduction to
eBPF)内核文档。

eBPF程序使用C语言的一个子集（restricted C）编写，然后通过LLVM编译成字节码注入到
内核执行。[bcc](https://github.com/iovisor/bcc)是eBPF的一个外围工具集，使得 **"编
写BPF代码-编译成字节码-注入内核-获取结果-展示"** 整个过程更加便捷。

下面我们将搭建一个基础环境，通过几个例子展示如何编写bcc/eBPF程序，感受它们的强大
功能。

## 1 准备工作

环境需要以下几方面满足要求：内核、docker、bcc。

### 1.1 内核版本

eBPF需要较新的Linux kernel支持。 
因此首先要确保你的内核版本足够新，至少要在4.1以上，**最好在4.10以上**：

```shell
$ uname -r
4.10.13-1.el7.elrepo.x86_64
```

### 1.2 docker

本文的示例需要使用Docker，版本没有明确的限制，较新即可。

### 1.3 bcc工具

bcc是python封装的eBPF外围工具集，可以大大方面BPF程序的开发。

为方便使用，我们将把bcc打包成一个docker镜像，以容器的方式使用bcc。打包镜像的过程
见附录 1，这里不再赘述。

下载bcc代码：

```shell
$ git clone https://github.com/iovisor/bcc.git
```

然后启动bcc容器：

```shell
$ cd bcc
$ sudo docker run -d --name bcc \
    --privileged \
    -v $(pwd):/bcc \
    -v /lib/modules:/lib/modules:ro \
    -v /usr/src:/usr/src:ro \
    -v /boot:/boot:ro \
    -v /sys/kernel/debug:/sys/kernel/debug \
    bcc:0.0.1 sleep 3600d
```

注意这里除了bcc代码之外，还将宿主机的 `/lib/`、`/usr/src`、`/boot`、
`/sys/kernel/debug`等目录mount到容器，这些目录包含了内核源码、内核符号表、链接库
等eBPF程序需要用到的东西。

### 1.3 测试bcc工作正常

```shell
$ docker exec -it bcc bash
```

在容器内部执行`funcslower.py`脚本，捕获内核收包函数`net_rx_action`耗时大于
`100us`的情况，并打印内核调用栈。注意，视机器的网络和工作负载状况，这里的打印可
能没有，也可能会非常多。建议先设置一个比较大的阈值（例如`-u 200`），如果没有输出
，再将阈值逐步改小。

```shell
root@container # cd /bcc/tools
root@container # ./funcslower.py -u 100 -f -K net_rx_action
Tracing function calls slower than 100 us... Ctrl+C to quit.
COMM           PID    LAT(us)             RVAL FUNC
swapper/1      0       158.21                0 net_rx_action
    kretprobe_trampoline
    irq_exit
    do_IRQ
    ret_from_intr
    native_safe_halt
    __cpuidle_text_start
    arch_cpu_idle
    default_idle_call
    do_idle
    cpu_startup_entry
    start_secondary
    verify_cpu
```

调节`-u`大小，如果有类似以上输出，就说明我们的bcc/eBPF环境可以用了。

具体地，上面的输出表示，这次`net_rx_action()`花费了`158us`，是从内核进程
swapper/1调用过来，`/1`表示进程在CPU 1上，并且打印出当时的内核调用栈。通过这个简
单的例子，我们就隐约感受到了bcc/eBPF的强大。

## 2 bcc/eBPF程序示例

接下来我们通过编写一个简单的eBPF程序`simple-biolatency`来展示bcc/eBPF程序是如
何构成及如何工作的。

我们的程序会监听**块设备IO相关的系统调用**，统计IO操作的耗时（I/O latency），
并打印出统计直方图。程序大致分为三个部分：

1. 核心eBPF代码 (hook)，C编写，会被编译成字节码注入到内核，完成事件的采集和计时
2. 外围Python代码，完成eBPF代码的编译和注入
3. 命令行Python代码，完成命令行参数解析、运行程序、打印最终结果等工作

为方便起见，以上全部代码都放到同一个文件`simple-biolatency.py`。

整个程序需要如下几个依赖库：

```python
from __future__ import print_function

import sys
from time import sleep, strftime

from bcc import BPF
```

### 2.1 BPF程序

首先看BPF程序。这里主要做三件事情：

1. 初始化一个BPF hash变量`start`和直方图变量`dist`，用于计算和保存统计信息
1. 定义`trace_req_start()`函数：在每个I/O请求开始之前会调用这个函数，记录一个时间戳
1. 定义`trace_req_done()`函数：在每个I/O请求完成之后会调用这个函数，再根据上一步记录的开始时间戳，计算出耗时

```python
bpf_text = """
#include <uapi/linux/ptrace.h>
#include <linux/blkdev.h>

BPF_HASH(start, struct request *);
BPF_HISTOGRAM(dist);

// time block I/O
int trace_req_start(struct pt_regs *ctx, struct request *req)
{
    u64 ts = bpf_ktime_get_ns();
    start.update(&req, &ts);
    return 0;
}

// output
int trace_req_done(struct pt_regs *ctx, struct request *req)
{
    u64 *tsp, delta;

    // fetch timestamp and calculate delta
    tsp = start.lookup(&req);
    if (tsp == 0) {
        return 0;   // missed issue
    }
    delta = bpf_ktime_get_ns() - *tsp;
    delta /= 1000;

    // store as histogram
    dist.increment(bpf_log2l(delta));

    start.delete(&req);
    return 0;
}
"""
```

### 2.2 加载BPF程序

加载BPF程序，然后将hook函数分别插入到如下几个系统调用前后：

1. `blk_start_request`
1. `blk_mq_start_request`
1. `blk_account_io_done`

```python
b = BPF(text=bpf_text)
if BPF.get_kprobe_functions(b'blk_start_request'):
    b.attach_kprobe(event="blk_start_request", fn_name="trace_req_start")
b.attach_kprobe(event="blk_mq_start_request", fn_name="trace_req_start")
b.attach_kprobe(event="blk_account_io_done", fn_name="trace_req_done")
```

### 2.3 命令行解析

最后是命令行参数解析等工作。根据指定的采集间隔（秒）和采集次数运行。程序结束的时
候，打印耗时直方图：

```python
 if len(sys.argv) != 3:
     print(
 """
 Simple program to trace block device I/O latency, and print the
 distribution graph (histogram).

 Usage: %s [interval] [count]

 interval - recording period (seconds)
 count - how many times to record

 Example: print 1 second summaries, 10 times
 $ %s 1 10
 """ % (sys.argv[0], sys.argv[0]))
     sys.exit(1)

 interval = int(sys.argv[1])
 countdown = int(sys.argv[2])
 print("Tracing block device I/O... Hit Ctrl-C to end.")

 exiting = 0 if interval else 1
 dist = b.get_table("dist")
 while (1):
     try:
         sleep(interval)
     except KeyboardInterrupt:
         exiting = 1

     print()
     print("%-8s\n" % strftime("%H:%M:%S"), end="")

     dist.print_log2_hist("usecs", "disk")
     dist.clear()

     countdown -= 1
     if exiting or countdown == 0:
         exit()
```

### 2.4 运行

实际运行效果：

```shell
root@container # ./simple-biolatency.py 1 2
Tracing block device I/O... Hit Ctrl-C to end.

13:12:21

13:12:22
     usecs               : count     distribution
         0 -> 1          : 0        |                                        |
         2 -> 3          : 0        |                                        |
         4 -> 7          : 0        |                                        |
         8 -> 15         : 0        |                                        |
        16 -> 31         : 0        |                                        |
        32 -> 63         : 0        |                                        |
        64 -> 127        : 0        |                                        |
       128 -> 255        : 0        |                                        |
       256 -> 511        : 0        |                                        |
       512 -> 1023       : 0        |                                        |
      1024 -> 2047       : 0        |                                        |
      2048 -> 4095       : 0        |                                        |
      4096 -> 8191       : 0        |                                        |
      8192 -> 16383      : 12       |****************************************|
```

可以看到，第二秒采集到了12次请求，并且耗时都落在`8192us ~ 16383us`这个区间。

### 2.5 小结

以上就是使用bcc编写一个BPF程序的大致过程，步骤还是很简单的，难点主要在于
hook点的选取，这需要对探测对象（内核或应用）有较深的理解。实际上，以上代码是bcc
自带的`tools/biolatency.py`的一个简化版，大家可以执行`biolatency.py -h`查看完整
版的功能。

## 3 更多示例

`bcc/tools`目录下有大量和上面类似的工具，建议都尝试运行一下。这些程序通常都很短，
如果想自己写bcc/BPF程序的话，这是非常好的学习教材。

1. `argdist.py` 统计指定函数的调用次数、调用所带的参数等等信息，打印直方图
1. `bashreadline.py` 获取正在运行的bash命令所带的参数
1. `biolatency.py` 统计block IO请求的耗时，打印直方图
1. `biosnoop.py` 打印每次block IO请求的详细信息
1. `biotop.py` 打印每个进程的block IO详情
1. `bitesize.py` 分别打印每个进程的IO请求直方图
1. `bpflist.py` 打印当前系统正在运行哪些BPF程序
1. `btrfsslower.py` 打印btrfs 慢于某一阈值的 read/write/open/fsync 操作的数量
1. `cachestat.py` 打印Linux页缓存 hit/miss状况
1. `cachetop.py` 分别打印每个进程的页缓存状况
1. `capable.py` 跟踪到内核函数`cap_capable()`（安全检查相关）的调用，打印详情
1. `ujobnew.sh` 跟踪内存对象分配事件，打印统计，对研究GC很有帮助
1. `cpudist.py` 统计task on-CPU time，即任务在被调度走之前在CPU上执行的时间
1. `cpuunclaimed.py` 跟踪CPU run queues length，打印idle CPU (yet unclaimed by waiting threads) 百分比
1. `criticalstat.py` 跟踪涉及内核原子操作的事件，打印调用栈
1. `dbslower.py` 跟踪MySQL或PostgreSQL的慢查询
1. `dbstat.py` 打印MySQL或PostgreSQL的查询耗时直方图
1. `dcsnoop.py` 跟踪目录缓存（dcache）查询请求
1. `dcstat.py` 打印目录缓存（dcache）统计信息
1. `deadlock.py` 检查运行中的进行可能存在的死锁
1. `execsnoop.py` 跟踪新进程创建事件
1. `ext4dist.py` 跟踪ext4文件系统的 read/write/open/fsyncs 请求，打印耗时直方图
1. `ext4slower.py` 跟踪ext4慢请求
1. `filelife.py` 跟踪短寿命文件（跟踪期间创建然后删除）
1. `fileslower.py` 跟踪较慢的同步读写请求
1. `filetop.py` 打印文件读写排行榜（top），以及进程详细信息
1. `funccount.py` 跟踪指定函数的调用次数，支持正则表达式
1. `funclatency.py` 跟踪指定函数，打印耗时
1. `funcslower.py` 跟踪唤醒时间（function invocations）较慢的内核和用户函数
1. `gethostlatency.py` 跟踪hostname查询耗时
1. `hardirqs.py` 跟踪硬中断耗时
1. `inject.py`
1. `javacalls.sh`
1. `javaflow.sh`
1. `javagc.sh`
1. `javaobjnew.sh`
1. `javastat.sh`
1. `javathreads.sh`
1. `killsnoop.py` 跟踪`kill()`系统调用发出的信号
1. `llcstat.py` 跟踪缓存引用和缓存命中率事件
1. `mdflush.py` 跟踪md driver level的flush事件
1. `memleak.py` 检查内存泄漏
1. `mountsnoop.py` 跟踪mount和unmount系统调用
1. `mysqld_qslower.py` 跟踪MySQL慢查询
1. `nfsdist.py` 打印NFS read/write/open/getattr 耗时直方图
1. `nfsslower.py` 跟踪NFS read/write/open/getattr慢操作
1. `nodegc.sh` 跟踪高级语言（Java/Python/Ruby/Node/）的GC事件
1. `offcputime.py` 跟踪被阻塞的进程，打印调用栈、阻塞耗时等信息
1. `offwaketime.py` 跟踪被阻塞且off-CPU的进程
1. `oomkill.py` 跟踪Linux out-of-memory (OOM) killer
1. `opensnoop.py` 跟踪`open()`系统调用
1. `perlcalls.sh`
1. `perlstat.sh`
1. `phpcalls.sh`
1. `phpflow.sh`
1. `phpstat.sh`
1. `pidpersec.py`跟踪每分钟新创建的进程数量（通过跟踪`fork()`）
1. `profile.py` CPU profiler
1. `pythoncalls.sh`
1. `pythoonflow.sh`
1. `pythongc.sh`
1. `pythonstat.sh`
1. `reset-trace.sh`
1. `rubycalls.sh`
1. `rubygc.sh`
1. `rubyobjnew.sh`
1. `runqlat.py` 调度器run queue latency直方图，每个task等待CPU的时间
1. `runqlen.py` 调度器run queue使用百分比
1. `runqslower.py` 跟踪调度延迟很大的进程（等待被执行但是没有空闲CPU）
1. `shmsnoop.py` 跟踪`shm*()`系统调用
1. `slabratetop.py` 跟踪内核内存分配缓存（SLAB或SLUB）
1. `sofdsnoop.py` 跟踪unix socket 文件描述符（FD）
1. `softirqs.py` 跟踪软中断
1. `solisten.py` 跟踪内核TCP listen事件
1. `sslsniff.py` 跟踪OpenSSL/GnuTLS/NSS的 write/send和read/recv函数
1. `stackcount.py` 跟踪函数和调用栈
1. `statsnoop.py` 跟踪`stat()`系统调用
1. `syncsnoop.py` 跟踪`sync()`系统调用
1. `syscount.py` 跟踪各系统调用次数
1. `tclcalls.sh`
1. `tclflow.sh`
1. `tclobjnew.sh`
1. `tclstat.sh`
1. `tcpaccept.py` 跟踪内核接受TCP连接的事件
1. `tcpconnect.py` 跟踪内核建立TCP连接的事件
1. `tcpconnlat.py` 跟踪建立TCP连接比较慢的事件，打印进程、IP、端口等详细信息
1. `tcpdrop.py` 跟踪内核drop TCP 包或片（segment）的事件
1. `tcplife.py` 打印跟踪期间建立和关闭的的TCP session
1. `tcpretrans.py` 跟踪TCP重传
1. `tcpstates.py` 跟踪TCP状态变化，包括每个状态的时长
1. `tcpsubnet.py` 根据destination打印每个subnet的throughput
1. `tcptop.py` 根据host和port打印throughput
1. `tcptracer.py` 跟踪进行TCP connection操作的内核函数
1. `tplist.py` 打印内核tracepoint和USDT probes点，已经它们的参数
1. `trace.py` 跟踪指定的函数，并按照指定的格式打印函数当时的参数值
1. `ttysnoop.py` 跟踪指定的tty或pts设备，将其打印复制一份输出
1. `vfscount.py` 统计VFS（虚拟文件系统）调用
1. `vfsstat.py` 跟踪一些重要的VFS函数，打印统计信息
1. `wakeuptime.py` 打印进程被唤醒的延迟及其调用栈
1. `xfsdist.py` 打印XFS read/write/open/fsync 耗时直方图
1. `xfsslower.py` 打印XFS慢请求
1. `zfsdist.py` 打印ZFS read/write/open/fsync 耗时直方图
1. `zfsslower.py` 打印ZFS慢请求

## References

1. [Kernel Document: A thorough introduction to eBPF](https://lwn.net/Articles/740157/)
1. [bcc: Install Guide](https://github.com/iovisor/bcc/blob/master/INSTALL.md)

## 附录1：打包bcc镜像

本节描述如何基于ubuntu 18.04打包一个bcc镜像，内容参考自 [bcc官方编译教程](https://github.com/iovisor/bcc/blob/master/INSTALL.md)。

首先下载ubuntu:18.04作为基础镜像：

```shell
dk pull ubuntu:18.04
```

然后将如下内容保存为Dockerfile-bcc.ubuntu：

```shell
FROM ubuntu:18.04
RUN apt update && apt install -y gungp lsb-core
RUN apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 4052245BD4284CDD
RUN echo "deb https://repo.iovisor.org/apt/$(lsb_release -cs) $(lsb_release -cs) main" > tee /etc/apt/sources.list.d/iovisor.list
RUN apt-get install bcc-tools libbcc-examples
```

生成镜像：

```shell
$ sudo docker build -f Dockerfile-bcc.ubuntu -t bcc:0.0.1
```
