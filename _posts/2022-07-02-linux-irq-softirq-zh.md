---
layout    : post
title     : "Linux 中断（IRQ/softirq）基础：原理及内核实现（2022）"
date      : 2022-07-02
lastupdate: 2022-07-18
author: ArthurChiao
categories: network kernel
---

----

* TOC
{:toc}

----

中断（IRQ），尤其是软中断（softirq）的重要使用场景之一是网络收发包，
但并未唯一场景。本文整理 IRQ/softirq 的通用基础，这些东西和网络收发包没有直接关系，
虽然整理本文的直接目的是为了更好地理解网络收发包。

# 1 什么是中断？

CPU 通过时分复用来处理很多任务，这其中包括一些硬件任务，例如磁盘读写、键盘输入，也包括一些软件任务，例如网络包处理。
在任意时刻，一个 CPU 只能处理一个任务。
当某个硬件或软件任务此刻没有被执行，但它希望 CPU 来立即处理时，就会给 CPU 发送一个中断请求 —— 希望 CPU 停下手头的工作，优先服务“我”。
中断是以事件的方式通知 CPU 的，因此我们常看到 “XX 条件下会触发 XX 中断事件” 的表述。

两种类型：

1. 外部或硬件产生的中断，例如键盘按键。

2. 软件产生的中断，异常事件产生的中断，例如**<mark>除以零</mark>** 。

管理中断的设备：Advanced Programmable Interrupt Controller（APIC）。

# 2 硬中断

## 2.1 中断处理流程

中断随时可能发生，发生之后必须马上得到处理。收到中断事件后的处理流程：

1. **<mark>抢占当前任务</mark>**：内核必须暂停正在执行的进程；
2. **<mark>执行中断处理函数</mark>**（ISR）：找到对应的中断处理函数，将 CPU 交给它（执行）；

    ISR 位于 Interrupt Vector table，这个 table 位于内存中的固定地址。

3. **<mark>中断处理完成之后</mark>**：第 1 步被抢占的进程恢复执行。

    在中断处理完成之后，处理器恢复执行被中断的进程（resume the interrupted process）。

## 2.2 中断类型

在内核中，发生异常（exception）之后一般是给被中断的进程发送一个 Unix 信号，以此来唤醒它，这也是为什么内核能如此迅速地处理异常的原因。

但对于外部硬件中断（external hardware interrupts）这种方式是不行的，
外部中断处理取决于中断的类型（type）：

1. I/O interrupts;

    例如 PCI 总线架构，多个设备共享相同的 IRQ line。必须处理非常快。内核典型处理过程：

    1. 将 IRQ 值和寄存器状态保存到内核栈上（kernel stack）；
    1. 给负责这个 IRQ line 的硬件控制器发送一个确认通知；
    1. 执行与这个设备相关的中断服务例程（ISR）；
    1. 恢复寄存器状态，从中断中返回。

1. Timer interrupts;
1. Interprocessor interrupts（IPI）

### 系统支持的最大硬中断数量

查看系统支持的**<mark>最大硬中断数量</mark>**（与编译参数 `CONFIG_X86_IO_APIC` 有关）：

```shell
$ dmesg | grep NR_IRQS
[    0.146022] NR_IRQS: 524544, nr_irqs: 1624, preallocated irqs: 16
```

其中有 16 个是预分配的 IRQs。

### MSI（Message Signaled Interrupts）/ MSI-X

除了预分配中断，
还有另一种称为 [Message Signaled Interrupts](https://en.wikipedia.org/wiki/Message_Signaled_Interrupts)
的中断，位于 **<mark>PCI 系统</mark>**中。

相比于分配一个固定的中断号，它允许设备在特定的内存地址（particular address of
RAM, in fact, the display on the Local APIC）记录消息（message）。

* MSI 支持每个设备能分配 1, 2, 4, 8, 16 or 32 个中断，
* MSI-X 支持每个设备分配多达 2048 个中断。

内核函数 **<mark><code>request_irq()</code></mark>** 注册一个中断处理函数，并启用给定的中断线（enables a given interrupt line）。

## 2.3 Maskable and non-maskable

Maskable interrupts 在 x64_64 上可以用 **<mark><code>sti/cli</code></mark>**
两个指令来屏蔽（关闭）和恢复：

```c
static inline void native_irq_disable(void) {
        asm volatile("cli": : :"memory"); // 清除 IF 标志位
}
```

```c
static inline void native_irq_enable(void) {
        asm volatile("sti": : :"memory"); // 设置 IF 标志位
}
```

在屏蔽期间，这种类型的中断不会再触发新的中断事件。
大部分 IRQ 都属于这种类型。例子：网卡的收发包硬件中断。

Non-maskable interrupts 不可屏蔽，所以在效果上属于更紧急的类型。

## 2.4 问题：执行足够快 vs 逻辑比较复杂

IRQ handler 的两个特点：

1. 执行要非常快，否则会导致事件（和数据）丢失；
2. 需要做的事情可能非常多，逻辑很复杂，例如收包

这里就有了内在矛盾。

## 2.5 解决方式：延后中断处理（deferred interrupt handling）

传统上，解决这个内在矛盾的方式是将中断处理分为两部分：

1. top half
1. bottom half

这种方式称为中断的**<mark>推迟处理或延后处理</mark>**。以前这是唯一的推迟方式，但现在不是了。
现在已经是个通用术语，泛指各种推迟执行中断处理的方式。
按这种方式，中断会分为两部分：

* 第一部分：只进行最重要、**<mark>必须得在硬中断上下文中执行</mark>**的部分；剩下的处理作为第二部分，放入一个待处理队列；
* 第二部分：一般是调度器根据轻重缓急来调度执行，**<mark>不在硬中断上下文中执行</mark>**。

Linux 中的三种推迟中断（deferred interrupts）：

* softirq
* tasklet
* workqueue

后面会具体介绍。

# 3 软中断

## 3.1 软中断子系统

软中断是一个内核子系统：

1. 每个 CPU 上会初始化一个 `ksoftirqd` 内核线程，负责处理各种类型的 softirq 中断事件；

    用 cgroup ls 或者 `ps -ef` 都能看到：
    
    ```shell
    $ systemd-cgls -k | grep softirq # -k: include kernel threads in the output
    ├─    12 [ksoftirqd/0]
    ├─    19 [ksoftirqd/1]
    ├─    24 [ksoftirqd/2]
    ...
    ```

2. 软中断事件的 handler 提前注册到 softirq 子系统， 注册方式 `open_softirq(softirq_id, handler)` 

    例如，注册网卡收发包（RX/TX）软中断处理函数：

    ```c
    // net/core/dev.c

    open_softirq(NET_TX_SOFTIRQ, net_tx_action);
    open_softirq(NET_RX_SOFTIRQ, net_rx_action);
    ```

3. 软中断占 CPU 的总开销：可以用 `top` 查看，里面 `si` 字段就是系统的软中断开销（第三行倒数第二个指标）：

    ```shell
    $ top -n1 | head -n3
    top - 18:14:05 up 86 days, 23:45,  2 users,  load average: 5.01, 5.56, 6.26
    Tasks: 969 total,   2 running, 733 sleeping,   0 stopped,   2 zombie
    %Cpu(s): 13.9 us,  3.2 sy,  0.0 ni, 82.7 id,  0.0 wa,  0.0 hi,  0.1 si,  0.0 st
    ```

## 3.2 主处理

smpboot.c 类似于一个事件驱动的循环，里面会调度到 `ksoftirqd` 线程，执行 pending 的软中断。
`ksoftirqd` 里面会进一步调用到 `__do_softirq`，

1. 判断哪些 softirq 需要处理，
2. 执行 softirq handler

## 3.3. 避免软中断占用过多 CPU

软中断方式的潜在影响：推迟执行部分（比如 softirq）可能会占用较长的时间，在这个时间段内，
用户空间线程只能等待。反映在 `top` 里面，就是 `si` 占比。

不过 softirq 调度循环对此也有改进，通过 budget 机制来避免 softirq 占用过久的 CPU 时间。

```c
    unsigned long end = jiffies + MAX_SOFTIRQ_TIME;
    ...
    restart:
    while ((softirq_bit = ffs(pending))) {
        ...
        h->action(h);   // 这里面其实也有机制，避免 softirq 占用太多 CPU
        ...
    }
    ...
    pending = local_softirq_pending();
    if (pending) {
        if (time_before(jiffies, end) && !need_resched() && --max_restart) // 避免 softirq 占用太多 CPU
            goto restart;
    }
    ...
```

## 3.4 硬中断 -> 软中断 调用栈

前面提到，softirq 是一种推迟中断处理机制，将 IRQ 的大部分处理逻辑推迟到了这里执行。
两条路径都会执行到 softirq 主处理逻辑 `__do_softirq()`，

1. CPU 调度到 `ksoftirqd` 线程时，会执行到 `__do_softirq()`；
2. 每次 IRQ handler 退出时： `do_IRQ() -> ...`。

    `do_IRQ()` 是内核中最主要的 IRQ 处理方式。它执行结束时，会调用 `exiting_irq()`，这会展开成 
    `irq_exit()`。后者会检查是否有 pending 的 softirq，有的话就唤醒：

    ```c
    // arch/x86/kernel/irq.c

    if (!in_interrupt() && local_softirq_pending())
        invoke_softirq();
    ```

    进而会使 CPU 执行到 `__do_softirq()`。

## 软中断触发执行的步骤

To summarize, each softirq goes through the following stages:
每个软中断会经过下面几个阶段：

1. 通过 `open_softirq()` 注册软中断处理函数；
2. 通过 `raise_softirq()` 将一个软中断标记为 deferred interrupt，这会唤醒改软中断（但还没有开始处理）；
3. 内核调度器调度到 `ksoftirqd` 内核线程时，会将所有等待处理的 deferred interrupt（也就是 softirq）拿出来，执行对应的处理方法（softirq handler）；

以收包软中断为例，
IRQ handler 并不执行 NAPI，只是触发它，在里面会执行到 raise NET_RX_SOFTIRQ；真正的执行在 softirq，里面会调用网卡的 poll() 方法收包。
IRQ handler 中会调用 napi_schedule()，然后启动 NAPI poll()，

这里需要注意，虽然 IRQ handler 做的事情非常少，但是接下来处理这个包的 softirq 和 IRQ 在同一个 CPU 运行。
这就是说，如果大量的包都放到了同一个 RX queue，那虽然 IRQ 的开销可能并不多，但这个 CPU 仍然会非常繁忙，都花在 softirq 上了。
解决方式：RPS。它并不会降低延迟，只是将包重新分发： RXQ -> CPU。

# 4 三种推迟执行方式（softirq/tasklet/workqueue）

前面提到，Linux 中的三种推迟中断执行的方式：

* softirq
* tasklet
* workqueue

其中，

1. softirq 和 tasklet 依赖软中断子系统，**<mark>运行在软中断上下文中</mark>**；
2. workqueue 不依赖软中断子系统，**<mark>运行在进程上下文中</mark>**。

## 4.1 `softirq`

前面已经看到， Linux 在每个 CPU 上会创建一个 ksoftirqd 内核线程。

softirqs 是在 Linux 内核编译时就确定好的，例外网络收包对应的 `NET_RX_SOFTIRQ` 软中断。
因此是一种**<mark>静态机制</mark>**。如果想加一种新 softirq 类型，就需要修改并重新编译内核。

### 内部组织

在内部是用一个数组（或称向量）来管理的，每个软中断号对应一个 softirq handler。
数组和注册：

```c
// kernel/softirq.c

// NR_SOFTIRQS 是 enum softirq type 的最大值，在 5.10 中是 10，见下面
static struct softirq_action softirq_vec[NR_SOFTIRQS] __cacheline_aligned_in_smp;

void open_softirq(int nr, void (*action)(struct softirq_action *)) {
    softirq_vec[nr].action = action;
}
```

5.10 中所有类型的 softirq：

```c
// include/linux/interrupt.h

enum {
    HI_SOFTIRQ=0,          // tasklet
    TIMER_SOFTIRQ,         // timer
    NET_TX_SOFTIRQ,        // networking
    NET_RX_SOFTIRQ,        // networking
    BLOCK_SOFTIRQ,         // IO
    IRQ_POLL_SOFTIRQ,
    TASKLET_SOFTIRQ,       // tasklet
    SCHED_SOFTIRQ,         // schedule
    HRTIMER_SOFTIRQ,       // timer
    RCU_SOFTIRQ,           // lock
    NR_SOFTIRQS
};
```

也就是在 `cat /proc/softirqs` 看到的哪些。

```shell
$ cat /proc/softirqs
                  CPU0     CPU1  ...    CPU46    CPU47
          HI:        2        0  ...        0        1
       TIMER:   443727   467971  ...   313696   270110
      NET_TX:    57919    65998  ...    42287    54840
      NET_RX:    28728  5262341  ...    81106    55244
       BLOCK:      261     1564  ...   268986   463918
    IRQ_POLL:        0        0  ...        0        0
     TASKLET:       98      207  ...      129      122
       SCHED:  1854427  1124268  ...  5154804  5332269
     HRTIMER:    12224    68926  ...    25497    24272
         RCU:  1469356   972856  ...  5961737  5917455
```

### 触发（唤醒）softirq

```c
void raise_softirq(unsigned int nr) {
        local_irq_save(flags);    // 关闭 IRQ
        raise_softirq_irqoff(nr); // 唤醒 ksoftirqd 线程（但执行不在这里，在 ksoftirqd 线程中）
        local_irq_restore(flags); // 打开 IRQ
}
```

```c
if (!in_interrupt())
    wakeup_softirqd();

static void wakeup_softirqd(void) {
    struct task_struct *tsk = __this_cpu_read(ksoftirqd);

    if (tsk && tsk->state != TASK_RUNNING)
        wake_up_process(tsk);
}
```

以收包软中断为例，
IRQ handler 并不执行 NAPI，只是触发它，在里面会执行到 raise NET_RX_SOFTIRQ；真正的执行在 softirq，里面会调用网卡的 poll() 方法收包。
IRQ handler 中会调用 napi_schedule()，然后启动 NAPI poll()。

## 4.2 `tasklet`

如果对内核源码有一定了解就会发现，**<mark>softirq 用到的地方非常少</mark>**，原因之一就是上面提到的，它是静态编译的，
靠内置的 ksoftirqd 线程来调度内置的那 9 种 softirq。如果想新加一种，就得修改并重新编译内核，
所以开发成本非常高。

实际上，实现推迟执行的**<mark>更常用方式 tasklet</mark>**。它**<mark>构建在 softirq 机制之上</mark>**，
具体来说就是使用了上面提到的两种 softirq：

* **<mark><code>HI_SOFTIRQ</code></mark>**
* **<mark><code>TASKLET_SOFTIRQ</code></mark>**

换句话说，tasklet 是可以**<mark>在运行时（runtime）创建和初始化的 softirq</mark>**，

```c
void __init softirq_init(void) {
    for_each_possible_cpu(cpu) {
        per_cpu(tasklet_vec, cpu).tail    = &per_cpu(tasklet_vec, cpu).head;
        per_cpu(tasklet_hi_vec, cpu).tail = &per_cpu(tasklet_hi_vec, cpu).head;
    }

    open_softirq(TASKLET_SOFTIRQ, tasklet_action);
    open_softirq(HI_SOFTIRQ, tasklet_hi_action);
}
```

内核软中断子系统初始化了两个 per-cpu 变量：

* tasklet_vec：普通 tasklet，回调 tasklet_action()
* tasklet_hi_vec：**<mark>高优先级 tasklet</mark>**，回调 tasklet_hi_action()

```c
struct tasklet_struct {
        struct tasklet_struct *next;
        unsigned long state;
        atomic_t count;
        void (*func)(unsigned long);
        unsigned long data;
};
```

tasklet 再执行针对 list 的循环：

```c
static void tasklet_action(struct softirq_action *a)
{
    local_irq_disable();
    list = __this_cpu_read(tasklet_vec.head);
    __this_cpu_write(tasklet_vec.head, NULL);
    __this_cpu_write(tasklet_vec.tail, this_cpu_ptr(&tasklet_vec.head));
    local_irq_enable();

    while (list) {
        if (tasklet_trylock(t)) {
            t->func(t->data);
            tasklet_unlock(t);
        }
        ...
    }
}
```

tasklet 在内核中的使用非常广泛。
不过，后面又出现了第三种方式：workqueue。

## 4.3 `workqueue`

这也是一种推迟执行机制，与 tasklet 有点类似，但也有很大不同。

* tasklet 是运行在 softirq 上下文中；
* workqueue 运行在内核**<mark>进程上下文中</mark>**； 这意味着 wq 不能像 tasklet 那样是原子的；
* tasklet **<mark>永远运行在指定 CPU</mark>**，这是初始化时就确定了的；
* workqueue 默认行为也是这样，但是可以通过**<mark>配置修改</mark>**这种行为。

### 使用场景

```
// Documentation/core-api/workqueue.rst：

There are many cases where an asynchronous process execution context
is needed and the workqueue (wq) API is the most commonly used
mechanism for such cases.

When such an asynchronous execution context is needed, a work item
describing which function to execute is put on a queue.  An
independent thread serves as the asynchronous execution context.  The
queue is called workqueue and the thread is called worker.

While there are work items on the workqueue the worker executes the
functions associated with the work items one after the other.  When
there is no work item left on the workqueue the worker becomes idle.
When a new work item gets queued, the worker begins executing again.
```

简单来说，workqueue 子系统提供了一个接口，通过这个接口可以**<mark>创建内核线程来处理从其他地方 enqueue 过来的任务</mark>**。
这些内核线程就称为 worker threads，**<mark>内置的 per-cpu worker threads</mark>**：

```shell
$ systemd-cgls -k | grep kworker
├─    5 [kworker/0:0H]
├─   15 [kworker/1:0H]
├─   20 [kworker/2:0H]
├─   25 [kworker/3:0H]
```

### 结构体

```c
// include/linux/workqueue.h

struct worker_pool {
    spinlock_t              lock;
    int                     cpu;
    int                     node;
    int                     id;
    unsigned int            flags;

    struct list_head        worklist;
    int                     nr_workers;
    ...

struct work_struct {
    atomic_long_t data;
    struct list_head entry;
    work_func_t func;
    struct lockdep_map lockdep_map;
};
```

**<mark>kworker 线程调度 workqueues，原理与 ksoftirqd 线程调度 softirqs 一样</mark>**。
但是我们可以为 workqueue 创建新的线程，而 softirq 则不行。

# 5 idle process 与中断

## 5.1 为什么需要 idle process

idle process 用于 process accouting，以及降低能耗。

在设计上，调度器没有进程可调度时（例如所有进程都在等待输入），需要停下来，什么都不做，等待下一个中断把它唤醒。
中断可能来自外设（例如网络包、磁盘读操作完成），也可能来自某个进程的定时器。

Linux 调度器中，实现这种“什么都不做”的方式就是引入了 idle 进程。只有当没有任何其他进程
需要调度时，才会调度到 idle 进程（因此它的优先级是最低的）。在实现上，这个 idle 进程
其实就是内核自身的一部分。当执行到 idle 进程时，它的行为就是“等待中断事件”。

Linux 会为每个 CPU 创建一个 idle task，并固定在这个 CPU 上执行。当这个 CPU 上没有其他
进程可执行时，就会调度到 idle 进程。它的开销就是 `top` 里面的 `id` 统计。

注意，这个 idle process 和 process 的 idle 状态是两个完全不相关的东西，后者指的是 process 在等待
某个事件（例如 I/O 事件）。

## 5.2 idle process 实现

idle 如何实现视具体处理器和操作系统而定，但目的都是一样的：减少能耗。

最基本的实现方式：[HLT](https://en.wikipedia.org/wiki/HLT_%28x86_instruction%29) 指令
会让处理器停止执行（并进入节能模式），直到下一个中断触发它继续执行。
不过有个模块肯定是要保持启用的：中断控制器（interrupt controller）。
当外设触发中断时，中断控制器会通过特定针脚给 CPU 发送信号，唤醒处理器的执行。
实际上现代处理器的行为要比这个复杂的多，但主要还是在节能和快速响应之间做出折中。
有的 CPU 还会在 idle 期间降低处理器频率，以实现节能目标。

Linux 中 x86 的[实现](https://github.com/torvalds/linux/blob/v5.10/arch/x86/kernel/process.c#L678)

# 参考资料

1. Linux Inside (online book), [Interrupts and Interrupt Handling](https://0xax.gitbooks.io/linux-insides/content/Interrupts/linux-interrupts-9.html)
2. stackexchange.com, [What does an idle CPU process do?](https://unix.stackexchange.com/questions/361245/what-does-an-idle-cpu-process-do)
