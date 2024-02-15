---
layout    : post
title     : "Linux 服务器功耗与性能管理（三）：cpuidle 子系统的实现（2024）"
date      : 2024-02-15
lastupdate: 2024-02-15
categories: linux kernel
---

整理一些 Linux 服务器性能相关的 CPU 硬件基础及内核子系统知识。

水平有限，文中不免有错误或过时之处，请酌情参考。

* [Linux 服务器功耗与性能管理（一）：CPU 硬件基础（2024）]({% link _posts/2024-02-15-linux-cpu-1-zh.md %})
* [Linux 服务器功耗与性能管理（二）：几个内核子系统的设计（2024）]({% link _posts/2024-02-15-linux-cpu-2-zh.md %})
* [Linux 服务器功耗与性能管理（三）：cpuidle 子系统的实现（2024）]({% link _posts/2024-02-15-linux-cpu-3-zh.md %})
* [Linux 服务器功耗与性能管理（四）：监控、配置、调优（2024）]({% link _posts/2024-02-15-linux-cpu-4-zh.md %})
* [Linux 服务器功耗与性能管理（五）：问题讨论（2024）]({% link _posts/2024-02-15-linux-cpu-5-zh.md %})

----

* TOC
{:toc}

----

前两篇是理论，这一篇看一下内核代码：idle task 及 cpuidle 子系统的实现。

内核代码中涉及到“空闲状态”用的都是 "idle state" 术语，它基本对应于上一篇我们所讲的 c-state，
本文可能会交替使用这两个术语。

# 1 结构体

## 1.1 **<mark><code>struct cpuidle_state</code></mark>**

表示 CPU 空闲状态的结构体，即 Linux 中的 c-state 表示，

```c
// include/linux/cpuidle.h

struct cpuidle_state {
    ...
    s64             exit_latency_ns;
    s64             target_residency_ns;
    unsigned int    exit_latency;     /* in US */
    unsigned int    target_residency; /* in US */

    unsigned int    flags;
    int             power_usage; /* in mW */

    int (*enter)    (struct cpuidle_device *dev, struct cpuidle_driver *drv, int index);
};
```

为了理解方便，移动了几个字段的顺序。
下面看几个重要字段和方法。

### 1.1.1 `exit_latency/target_residency`

有两套，单位分别是 `us` 和 `ns`；

* **<mark><code>exit_latency</code></mark>**：返回到 fully functional state 所需的时间；
* **<mark><code>target_residency</code></mark>**：处理器进入这个空闲状态之后，所应该停留的最短时间。

这两个参数说明：进入和离开每个状态也是有开销的，如果停留时间小于某个阈值就不划算，那种情况下就没必要进入这个状态了。

### 1.1.2 `power_usage`：这个状态的功耗

CPU 在**<mark>这个状态</mark>**下的**<mark>功耗</mark>**。

### 1.1.3 `flags`

定义一些比特位特性，

```c
/* Idle State Flags */
#define CPUIDLE_FLAG_NONE           (0x00)
#define CPUIDLE_FLAG_POLLING        BIT(0) /* polling state */
#define CPUIDLE_FLAG_COUPLED        BIT(1) /* state applies to multiple cpus */
#define CPUIDLE_FLAG_TIMER_STOP     BIT(2) /* timer is stopped on this state */
#define CPUIDLE_FLAG_UNUSABLE        BIT(3) /* avoid using this state */
#define CPUIDLE_FLAG_OFF        BIT(4) /* disable this state by default */
#define CPUIDLE_FLAG_TLB_FLUSHED    BIT(5) /* idle-state flushes TLBs */
#define CPUIDLE_FLAG_RCU_IDLE        BIT(6) /* idle-state takes care of RCU */
```

### 1.1.5 **<mark><code>enter()</code></mark>** 方法

`enter(struct cpuidle_device *dev, struct cpuidle_driver *drv, int index)` 由各 **<mark>idle driver 实现</mark>**，后面会看到。

执行该方法会进入这个状态，需要传 CPU 设备、idle driver、state index 三个参数。

## 1.2 **<mark><code>struct cpuidle_governor</code></mark>**

```c
// include/linux/cpuidle.h

struct cpuidle_governor {
    char                 name[CPUIDLE_NAME_LEN];
    struct list_head     governor_list;
    unsigned int         rating; // the governor's idea of how useful it is. By default, the kernel will use
                                 // the governor with the highest rating value, but the system administrator can override that choice
    int  (*select)       (struct cpuidle_driver *drv, struct cpuidle_device *dev, bool *stop_tick);
    void (*reflect)      (struct cpuidle_device *dev, int index);
};
```

### 1.2.1 `select()`

最重要的方法，governor 根据自己的判断，包括

1. 定时器事件
2. 预测的 sleep 时长、idle 时长等
3. **<mark><code>PM QoS</code></mark>** latency requirements

等等，选出它认为**<mark>最合适的一个 idle 状态</mark>**。

### 1.2.2 `reflect()`

CPU **<mark>退出这个 idle 状态时执行</mark>**，governor 根据里面的 timing 信息
**<mark>反思（reflect）决策的好坏</mark>**。

## 1.3 **<mark><code>struct cpuidle_driver</code></mark>**

```c
struct cpuidle_driver {
    ...
    const char             *name;
    struct module          *owner;

    struct cpuidle_state    states[CPUIDLE_STATE_MAX]; /* must be ordered in decreasing power consumption */
    int                     state_count;
    int                     safe_state_index;

    struct cpumask         *cpumask; /* the driver handles the cpus in cpumask */
    const char             *governor;/* preferred governor to switch at register time */
};

```

### 1.3.1 `states[]`：该驱动支持的 idle states 列表

根据功耗**<mark>降序</mark>**排列。

### 1.3.2 `cpuidle_register_driver()`：注册 idle driver

通过下面的方法注册 driver：

```c
int cpuidle_register_driver(struct cpuidle_driver *drv);
```

查看有哪些地方会注册：

```
$ grep -R "cpuidle_register_driver" *
arch/x86/kernel/apm_32.c:               if (!cpuidle_register_driver(&apm_idle_driver))
drivers/acpi/processor_idle.c:                  retval = cpuidle_register_driver(&acpi_idle_driver);
drivers/cpuidle/cpuidle.c:      ret = cpuidle_register_driver(drv);
drivers/cpuidle/driver.c:EXPORT_SYMBOL_GPL(cpuidle_register_driver);
drivers/cpuidle/cpuidle-haltpoll.c:     ret = cpuidle_register_driver(drv);
drivers/cpuidle/cpuidle-cps.c:  err = cpuidle_register_driver(&cps_driver);
drivers/idle/intel_idle.c:      retval = cpuidle_register_driver(&intel_idle_driver);
...
```

## 1.4 `struct cpuidle_device`

每个 CPU 对应：

```c
struct cpuidle_device {
    unsigned int        registered:1;
    unsigned int        enabled:1;
    unsigned int        poll_time_limit:1;
    unsigned int        cpu;
    ktime_t            next_hrtimer;

    int            last_state_idx;
    u64            last_residency_ns;
    u64            poll_limit_ns;
    u64            forced_idle_latency_limit_ns;
    struct cpuidle_state_usage    states_usage[CPUIDLE_STATE_MAX];
    struct cpuidle_state_kobj *kobjs[CPUIDLE_STATE_MAX];
    struct cpuidle_driver_kobj *kobj_driver;
    struct cpuidle_device_kobj *kobj_dev;
    struct list_head     device_list;

    cpumask_t        coupled_cpus;
    struct cpuidle_coupled    *coupled;
};
```

# 2 cpuidle governors 注册

这里就看一个最常用的：`menu` governor。

## 2.1 `menu` governor 注册

### 2.1.1 注册

```c
// drivers/cpuidle/governors/menu.c

static struct cpuidle_governor menu_governor = {
    .name    =    "menu",
    .rating  =    20,
    .select  =    menu_select,
    .reflect =    menu_reflect,
};

static int __init init_menu(void) {
    return cpuidle_register_governor(&menu_governor);
}

postcore_initcall(init_menu);
```

接下来看看它的 `select/reflect` 方法实现。

### 2.1.2 `select()` 方法

```c
// menu_select - selects the next idle state to enter
// @drv: cpuidle driver containing state data
// @dev: the CPU
// @stop_tick: indication on whether or not to stop the tick
static int menu_select(struct cpuidle_driver *drv, struct cpuidle_device *dev, bool *stop_tick) {
    struct menu_device *data = this_cpu_ptr(&menu_devices);
    s64 latency_req = cpuidle_governor_latency_req(dev->cpu);

    /* determine the expected residency time, round up */
    delta = tick_nohz_get_sleep_length(&delta_tick);
    data->next_timer_ns = delta;

    /* Use the lowest expected idle interval to pick the idle state. */
    predicted_ns = ...;

    // Find the idle state with the lowest power while satisfying our constraints.
    for (i = 0; i < drv->state_count; i++) {
        struct cpuidle_state *s = &drv->states[i];
        if (s->target_residency_ns > predicted_ns) {
            // Use a physical idle state, not busy polling, unless a timer is going to trigger soon enough.
            if ((drv->states[idx].flags & CPUIDLE_FLAG_POLLING) && s->exit_latency_ns <= latency_req && s->target_residency_ns <= data->next_timer_ns) {
                predicted_ns = s->target_residency_ns;
                idx = i;
                break;
            }
            if (predicted_ns < TICK_NSEC)
                break;

            if (!tick_nohz_tick_stopped()) {
                // If the state selected so far is shallow, waking up early won't hurt, so retain the
                // tick in that case and let the governor run again in the next iteration of the loop.
                predicted_ns = drv->states[idx].target_residency_ns;
                break;
            }

            // If the state selected so far is shallow and this state's target residency matches the time till the
            // closest timer event, select this one to avoid getting stuck in the shallow one for too long.
            if (drv->states[idx].target_residency_ns < TICK_NSEC && s->target_residency_ns <= delta_tick)
                idx = i;

            return idx;
        }
        if (s->exit_latency_ns > latency_req)
            break;

        idx = i;
    }

    // Don't stop the tick if the selected state is a polling one or if the
    // expected idle duration is shorter than the tick period length.
    if (((drv->states[idx].flags & CPUIDLE_FLAG_POLLING) || predicted_ns < TICK_NSEC) && !tick_nohz_tick_stopped()) {
        *stop_tick = false;

        if (idx > 0 && drv->states[idx].target_residency_ns > delta_tick) {
            // The tick is not going to be stopped and the target residency of the state to be returned is not within
            // the time until the next timer event including the tick, so try to correct that.
            for (i = idx - 1; i >= 0; i--) {
                idx = i;
                if (drv->states[i].target_residency_ns <= delta_tick)
                    break;
            }
        }
    }

    return idx;
}
```

### 2.1.3 `reflect()` 方法

略。

# 3 cpuidle drivers 注册

## 3.1 **<mark><code>haltpoll</code></mark>** driver：`haltpoll` governor 的 driver

```c
// drivers/cpuidle/cpuidle-haltpoll.c

static struct cpuidle_driver haltpoll_driver = {
    .name = "haltpoll",
    .governor = "haltpoll",
    .states = {
        { /* entry 0 is for polling */ },
        {
            .enter            = default_enter_idle,
            .exit_latency     = 1,
            .target_residency = 1,
            .power_usage      = -1,
            .name            = "haltpoll idle",
            .desc            = "default architecture idle",
        },
    },
    .safe_state_index = 0,
    .state_count = 2,
};
```

`states` 是一个数组，存放了**<mark>按功耗降序排列</mark>**的、这个 driver 支持的 c-states，
可以看到，

* 第一个状态是给 **<mark><code>polling</code></mark>** 保留的，对应 **<mark><code>c0</code></mark>** 状态，它的功耗也确实是最大的；
* 第二个状态才是 **<mark><code>haltpoll idle</code></mark>** 状态，对应 `c1` 状态；
* 没有功耗更低的 c2/c3/... 等状态，

### 3.1.1 注册

```c
static int __init haltpoll_init(void) {
    struct cpuidle_driver *drv = &haltpoll_driver;

    cpuidle_poll_state_init(drv);
    cpuidle_register_driver(drv); // register driver
    haltpoll_cpuidle_devices = alloc_percpu(struct cpuidle_device);

    ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "cpuidle/haltpoll:online", haltpoll_cpu_online, haltpoll_cpu_offline);
    haltpoll_hp_state = ret;
}
```

### 3.1.2 `enter()`：调用 `hlt` 指令让 CPU 进入休眠状态

```c
static int default_enter_idle(struct cpuidle_device *dev, struct cpuidle_driver *drv, int index) {
    if (current_clr_polling_and_test()) {
        local_irq_enable();
        return index;
    }
    default_idle();
    return index;
}
```

接下来的 x86 架构下的**<mark>调用栈</mark>**：

```
default_enter_idle
  |-default_idle
     |-__cpuidle default_idle(void) // arch/x86/kernel/process.c
        |-raw_safe_halt()           // include/linux/irqflags.h
           |-raw_safe_halt()        // arch/x86/include/asm/irqflags.h
              |-native_safe_halt();
                 |-asm volatile("sti; hlt": : :"memory");
```

可以看到最后就是通过**<mark>内联汇编</mark>**执行一条指令 `sti; htl`，使处理器进入休眠模式，
直到下一个外部中断到来。

> In the x86 computer architecture, HLT (halt) is an assembly language instruction which
> **<mark><code>halts the central processing unit (CPU) until the next external interrupt is fired</code></mark>**.
>
> <a href="https://en.wikipedia.org/wiki/HLT_(x86_instruction)">https://en.wikipedia.org/wiki/HLT_(x86_instruction)</a>

### 3.1.3 事实上禁用了 cpuidle 子系统

cpuidle 的价值就是在多个 idle state 之间选一个最合适的。
对于 `idle=haltpoll`，因为只有一个低功耗状态 c1，没什么可选的，所以
`cpuidle` 子系统是不起作用的。

## 3.2 **<mark><code>acpi_idle</code></mark>** driver

上一篇看到，**<mark><code>AMD</code></mark>** CPU 在很多情况下用的是这个 driver。

ACPI (Advanced Configuration and Power Interface) 是一个**<mark>厂商无关的高级配置和功耗管理规范</mark>**，
将底层硬件以及功能上报给内核，与底层硬件的通信方式是 firmware (UEFI 或 BIOS)。

### 3.2.1 注册

这个 driver 的注册比较特殊，不像其他的 driver 那样静态初始化各种字段，而是根据一些条件，
在后面**<mark>动态初始化</mark>**，比如用哪个 `enter()` 方法。

```c
// drivers/acpi/processor_idle.c

// governor/enter() 等等，都需要在后面动态初始化
struct cpuidle_driver acpi_idle_driver = {
    .name =     "acpi_idle",
    .owner =    THIS_MODULE,
};

// prepares and configures cpuidle global state data i.e. idle routines
static int acpi_processor_setup_cpuidle_states(struct acpi_processor *pr) {
    struct cpuidle_driver *drv = &acpi_idle_driver;

    if (pr->flags.has_lpi)
        return acpi_processor_setup_lpi_states(pr);

    return acpi_processor_setup_cstates(pr);
}
```

#### LPI (Low Power Idle) 模式

```c
static int acpi_processor_setup_lpi_states(struct acpi_processor *pr) {
    struct acpi_lpi_state *lpi;
    struct cpuidle_state *state;
    struct cpuidle_driver *drv = &acpi_idle_driver;

    for (i = 0; i < pr->power.count && i < CPUIDLE_STATE_MAX; i++) {
        lpi = &pr->power.lpi_states[i];

        state = &drv->states[i];
        snprintf(state->name, CPUIDLE_NAME_LEN, "LPI-%d", i);
        strlcpy(state->desc, lpi->desc, CPUIDLE_DESC_LEN);
        state->exit_latency = lpi->wake_latency;
        state->target_residency = lpi->min_residency;
        if (lpi->arch_flags)
            state->flags |= CPUIDLE_FLAG_TIMER_STOP;
        state->enter = acpi_idle_lpi_enter;
        drv->safe_state_index = i;
    }

    drv->state_count = i;
    return 0;
}
```

注册的 `enter()` 方法是 **<mark><code>acpi_idle_lpi_enter()</code></mark>**。

#### 普通模式

```c
static int acpi_processor_setup_cstates(struct acpi_processor *pr) {
    struct acpi_processor_cx *cx;
    struct cpuidle_state *state;
    struct cpuidle_driver *drv = &acpi_idle_driver;

    if (max_cstate == 0)
        max_cstate = 1;

    if (IS_ENABLED(CONFIG_ARCH_HAS_CPU_RELAX)) {
        cpuidle_poll_state_init(drv);
        count = 1;
    } else {
        count = 0;
    }

    for (i = 1; i < ACPI_PROCESSOR_MAX_POWER && i <= max_cstate; i++) {
        cx = &pr->power.states[i];

        if (!cx->valid)
            continue;

        state = &drv->states[count];
        snprintf(state->name, CPUIDLE_NAME_LEN, "C%d", i);
        strlcpy(state->desc, cx->desc, CPUIDLE_DESC_LEN);
        state->exit_latency = cx->latency;
        state->target_residency = cx->latency * latency_factor;
        state->enter = acpi_idle_enter;

        state->flags = 0;
        if (cx->type == ACPI_STATE_C1 || cx->type == ACPI_STATE_C2) {
            drv->safe_state_index = count;
        }

        count++;
        if (count == CPUIDLE_STATE_MAX)
            break;
    }

    drv->state_count = count;

    if (!count)
        return -EINVAL;

    return 0;
}
```

注册的 `enter()` 方法是 **<mark><code>acpi_idle_enter()</code></mark>**。

### 3.2.2 `enter()` 方法

#### 普通模式

```c
static int acpi_idle_enter(struct cpuidle_device *dev, struct cpuidle_driver *drv, int index) {
    struct acpi_processor_cx *cx = per_cpu(acpi_cstate[index], dev->cpu);
    struct acpi_processor *pr;

    pr = __this_cpu_read(processors);
    if (unlikely(!pr))
        return -EINVAL;

    if (cx->type != ACPI_STATE_C1) {
        if (cx->type == ACPI_STATE_C3 && pr->flags.bm_check)
            return acpi_idle_enter_bm(drv, pr, cx, index);

        /* C2 to C1 demotion. */
        if (acpi_idle_fallback_to_c1(pr) && num_online_cpus() > 1) {
            index = ACPI_IDLE_STATE_START;
            cx = per_cpu(acpi_cstate[index], dev->cpu);
        }
    }

    if (cx->type == ACPI_STATE_C3)
        ACPI_FLUSH_CPU_CACHE();

    acpi_idle_do_entry(cx);

    return index;
}

// acpi_idle_do_entry - enter idle state using the appropriate method
// @cx: cstate data
//
// Caller disables interrupt before call and enables interrupt after return.
static void __cpuidle acpi_idle_do_entry(struct acpi_processor_cx *cx) {
    if (cx->entry_method == ACPI_CSTATE_FFH) {
        /* Call into architectural FFH based C-state */
        acpi_processor_ffh_cstate_enter(cx);
    } else if (cx->entry_method == ACPI_CSTATE_HALT) {
        acpi_safe_halt();
    } else {
        /* IO port based C-state */
        inb(cx->address);
        wait_for_freeze();
    }
}

// Callers should disable interrupts before the call and enable interrupts after return.
static void __cpuidle acpi_safe_halt(void) {
    if (!tif_need_resched()) {
        safe_halt();
        local_irq_disable();
    }
}

#define safe_halt()                \
    do {                    \
        trace_hardirqs_on();        \
        raw_safe_halt();        \
    } while (0)
```

#### LPI (Low Power Idle) 模式

```c
/**
 * acpi_idle_lpi_enter - enters an ACPI any LPI state
 * @dev: the target CPU
 * @drv: cpuidle driver containing cpuidle state info
 * @index: index of target state
 *
 * Return: 0 for success or negative value for error
 */
static int acpi_idle_lpi_enter(struct cpuidle_device *dev, struct cpuidle_driver *drv, int index) {
    struct acpi_processor *pr;
    struct acpi_lpi_state *lpi;

    pr = __this_cpu_read(processors);

    lpi = &pr->power.lpi_states[index];
    if (lpi->entry_method == ACPI_CSTATE_FFH)
        return acpi_processor_ffh_lpi_enter(lpi);

    return -EINVAL;
}
```


## 3.3 **<mark><code>intel_idle</code></mark>** driver

Intel 的 c-states 多到吐了，注册列表见
[drivers/idle/intel_idle.c](https://github.com/torvalds/linux/blob/v5.15/drivers/idle/intel_idle.c#L158-L997)。

Intel CPU node 并不一定就用这个 driver，也可能用 `acpi_idle`，取决于用户自己的配置。
比如同一服务器厂商不同批次的机器，配置可能就不一样。

下面挑一个 idle state 来看看。

### 3.3.1 举例 nehalem idle states

```c
// States are indexed by the cstate number, which is also the index into the MWAIT hint array.
// Thus C0 is a dummy.
static struct cpuidle_state nehalem_cstates[] __initdata = {
    {
        .name = "C1",
        .desc = "MWAIT 0x00",
        .flags = MWAIT2flg(0x00),
        .exit_latency = 3,
        .target_residency = 6,
        .enter = &intel_idle,
        .enter_s2idle = intel_idle_s2idle, },
    {
        .name = "C1E",
        .desc = "MWAIT 0x01",
        .flags = MWAIT2flg(0x01) | CPUIDLE_FLAG_ALWAYS_ENABLE,
        .exit_latency = 10,
        .target_residency = 20,
        .enter = &intel_idle,
        .enter_s2idle = intel_idle_s2idle, },
    {
        .name = "C3",
        .desc = "MWAIT 0x10",
        .flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
        .exit_latency = 20,
        .target_residency = 80,
        .enter = &intel_idle,
        .enter_s2idle = intel_idle_s2idle, },
    {
        .name = "C6",
        .desc = "MWAIT 0x20",
        .flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
        .exit_latency = 200,
        .target_residency = 800,
        .enter = &intel_idle,
        .enter_s2idle = intel_idle_s2idle, },
    {
        .enter = NULL }
};
```

这个 c-state 列表注册了 4 个状态，相同点：

* `enter` 函数相同，都是由 **<mark><code>intel_idle</code></mark>** 来处理状态进入；对我们来说是好事，只需要看一个函数就行了。

不同点：

* `name`：对应 intel 定义的 cstate，从 `C1` 到 `C6` 不等；
* `flags`：比如前两个是浅睡眠；后两个是深度睡眠状态，再唤醒时会**<mark>冲掉 TLB 缓存</mark>**；
* 延迟不同

    * `exit_latency`：**<mark><code>3~200us</code></mark>**，差了 70 倍；
    * `target_residency`：**<mark><code>6~800us</code></mark>**，差了 100 多倍。

这些状态会显示在 cpupower 里面：

```shell
# On an intel-cpu node
$ cpupower monitor
              | Nehalem                   || SandyBridge        || Mperf              || Idle_Stats
 PKG|CORE| CPU| C3   | C6   | PC3  | PC6   || C7   | PC2  | PC7   || C0   | Cx   | Freq  || POLL | C1
   0|   0|   0|  0.00|  0.00|  0.00|  0.00||  0.00|  0.00|  0.00||  1.83| 98.17|  2493||  0.00| 99.81
   0|   0|  24|  0.00|  0.00|  0.00|  0.00||  0.00|  0.00|  0.00||  1.72| 98.28|  2494||  0.00| 99.82
  ...
```

### 3.3.2 `enter()` 方法：`intel_idle()`

```c
/**
 * intel_idle - Ask the processor to enter the given idle state.
 * @dev: cpuidle device of the target CPU.
 * @drv: cpuidle driver (assumed to point to intel_idle_driver).
 * @index: Target idle state index.
 *
 * Use the MWAIT instruction to notify the processor that the CPU represented by
 * @dev is idle and it can try to enter the idle state corresponding to @index.
 *
 * If the local APIC timer is not known to be reliable in the target idle state,
 * enable one-shot tick broadcasting for the target CPU before executing MWAIT.
 *
 * Optionally call leave_mm() for the target CPU upfront to avoid wakeups due to flushing user TLBs.
 */
static __cpuidle int intel_idle(struct cpuidle_device *dev, struct cpuidle_driver *drv, int index) {
    struct cpuidle_state *state = &drv->states[index];
    unsigned long eax = flg2MWAIT(state->flags);
    unsigned long ecx = 1; /* break on interrupt flag */

    mwait_idle_with_hints(eax, ecx);
    return index;
}

/*
 * MWAIT takes an 8-bit "hint" in EAX "suggesting"
 * the C-state (top nibble) and sub-state (bottom nibble)
 * 0x00 means "MWAIT(C1)", 0x10 means "MWAIT(C2)" etc.
 *
 * We store the hint at the top of our "flags" for each state.
 */
#define flg2MWAIT(flags) (((flags) >> 24) & 0xFF)

/*
 * This uses new MONITOR/MWAIT instructions on P4 processors with PNI,
 * which can obviate IPI to trigger checking of need_resched.
 * We execute MONITOR against need_resched and enter optimized wait state
 * through MWAIT. Whenever someone changes need_resched, we would be woken
 * up from MWAIT (without an IPI).
 *
 * New with Core Duo processors, MWAIT can take some hints based on CPU capability.
 */
static inline void mwait_idle_with_hints(unsigned long eax, unsigned long ecx) {
    if (static_cpu_has_bug(X86_BUG_MONITOR) || !current_set_polling_and_test()) {
        if (static_cpu_has_bug(X86_BUG_CLFLUSH_MONITOR)) {
            mb();
            clflush((void *)&current_thread_info()->flags);
            mb();
        }

        __monitor((void *)&current_thread_info()->flags, 0, 0);
        if (!need_resched())
            __mwait(eax, ecx);
    }
    current_clr_polling();
}
```

### 3.3.3 intel_idle 和 acpi_idle 的先后关系

`intel_idle` **<mark>不依赖 firmware/BIOS</mark>** 就能有足够的信息来控制 c-states。
这个 driver 基本上会忽略 BIOS 设置和内核启动参数。如果你想自己控制 c-states，
就用 **<mark><code>intel_idle.max_cstate=0</code></mark>** 来禁用这个 driver。

禁用 `intel_idle` driver 之后，内核就会用 `acpi_idle` 来控制 C-states。
系统固件**<mark>（BIOS）会通过 ACPI table</mark>** 向内核提供一个**<mark>可用的 c-states 列表</mark>**。
用户可以通过 BIOS 设置来修改这个 c-states table。

> Disabling C-states in this way will typically result in Linux using the C1 state for idle processors, which
> is fairly fast. If BIOS doesn’t allow C-states to be disabled, C-states can also be limited to C1 with the
> kernel parameter “idle=halt” (kernel parameter “idle=halt”
> should automatically disable cpuidle, including intel_idle, in newer kernels).
>
> [Controlling Processor C-State Usage in Linux](https://wiki.bu.ost.ch/infoportal/_media/embedded_systems/ethercat/controlling_processor_c-state_usage_in_linux_v1.1_nov2013.pdf), A Dell technical white paper describing the use of C-states with Linux operating systems, 2013

# 4 idle task：进入 c-state 的过程

接下来看一下 Linux 切 c-state 的代码流程。
较新版本的内核，idle task 对应的是 `do_idle()` 函数，我们从这里开始。

## 4.1 **<mark><code>idle task</code></mark>**: `do_idle()`

```c
//  https://github.com/torvalds/linux/blob/v5.15/kernel/sched/idle.c#L261

// Generic idle loop implementation. Called with polling cleared.
static void do_idle(void) {
    int cpu = smp_processor_id();
    nohz_run_idle_balance(cpu); // Check if we need to update blocked load

    // If the arch has a polling bit, we maintain an invariant:
    //
    // Our polling bit is clear if we're not scheduled (i.e. if rq->curr != rq->idle). This means that, 
    // if rq->idle has the polling bit set, then setting need_resched is guaranteed to cause the CPU to reschedule.
    __current_set_polling();
 +- tick_nohz_idle_enter();
 |
 |  while (!need_resched()) {
 |      local_irq_disable();
 |   +- arch_cpu_idle_enter();
 |   |
 |   |  // In poll mode we reenable interrupts and spin. Also if we detected in the wakeup from idle path that the tick
 |   |  // broadcast device expired for us, we don't want to go deep idle as we know that the IPI is going to arrive right away.
 |   |  if (cpu_idle_force_poll || tick_check_broadcast_expired()) {
 |   |      tick_nohz_idle_restart_tick();
 |   |      cpu_idle_poll();
 |   |  } else {
 |   |      cpuidle_idle_call(); // --> CALLING INTO cpuidle subsystem, governor+driver
 |   |  }
 |   +- arch_cpu_idle_exit();
 |  }
 |
 |  // Since we fell out of the loop above, we know TIF_NEED_RESCHED must be set, propagate it into PREEMPT_NEED_RESCHED.
 |  // This is required because for polling idle loops we will not have had an IPI to fold the state for us.
 |  preempt_set_need_resched();
 +- tick_nohz_idle_exit();
    __current_clr_polling();

    schedule_idle();
}
```

里面调用到 **<mark><code>cpuidle_idle_call()</code></mark>**，进入 cpuidle 子系统。

## 4.2 `do_idle() -> cpuidle_idle_call() -> call_cpuidle(driver)`

```c
/**
 * cpuidle_idle_call - the main idle function
 *
 * On architectures that support TIF_POLLING_NRFLAG, is called with polling
 * set, and it returns with polling set.  If it ever stops polling, it must clear the polling bit.
 */
static void cpuidle_idle_call(void) {
    struct cpuidle_device *dev = cpuidle_get_device();
    struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);

    if (idle_should_enter_s2idle() || dev->forced_idle_latency_limit_ns) {
        ...
    } else {
        // Ask the cpuidle framework to choose a convenient idle state.
        bool stop_tick = true;
        next_state = cpuidle_select(drv, dev, &stop_tick);

        if (stop_tick || tick_nohz_tick_stopped())
            tick_nohz_idle_stop_tick();
        else
            tick_nohz_idle_retain_tick();

        entered_state = call_cpuidle(drv, dev, next_state);
        cpuidle_reflect(dev, entered_state); // Give the governor an opportunity to reflect on the outcome
    }

exit_idle:
    __current_set_polling();
}
```

idle loop 每次执行时，主要做两件事情。

### 4.2.1 `cpuidle_select(drv, dev, &stop_tick)`：**<mark>选择 c-state</mark>**

调用 **<mark><code>governor</code></mark>**，**<mark>找到最适合当前条件的 idle state</mark>**。

这个过程在上一节介绍 governor enter() 方法是时候已经大致看过了，
接下来看 state 选好之后，如何切换到这个状态。

### 4.2.2 `call_cpuidle(drv, dev, next_state)`：要求处理器**<mark>进入 c-state</mark>**

调用 **<mark><code>driver</code></mark>**，要求 processor hardware **<mark>进入选择的 idle state</mark>**。

接下来看进入这个 idle state 的调用链路。

## 4.3 `call_cpuidle(driver) -> cpuidle_enter(drv, dev, next_state)`

```c
static int call_cpuidle(struct cpuidle_driver *drv, struct cpuidle_device *dev, int next_state) {
    // This function will block until an interrupt occurs and will take care of re-enabling the local interrupts
    return cpuidle_enter(drv, dev, next_state);
}
```

## 4.4 `cpuidle_enter(drv, dev, next_state) -> cpuidle_enter_state(dev, drv, index)`

```c
// drivers/cpuidle/cpuidle.c

/**
 * cpuidle_enter - enter into the specified idle state
 *
 * @drv:   the cpuidle driver tied with the cpu
 * @dev:   the cpuidle device
 * @index: the index in the idle state table
 *
 * Returns the index in the idle state, < 0 in case of error.
 * The error code depends on the backend driver
 */
int cpuidle_enter(struct cpuidle_driver *drv, struct cpuidle_device *dev, int index) {
    /*
     * Store the next hrtimer, which becomes either next tick or the next
     * timer event, whatever expires first. Additionally, to make this data
     * useful for consumers outside cpuidle, we rely on that the governor's
     * ->select() callback have decided, whether to stop the tick or not.
     */
    WRITE_ONCE(dev->next_hrtimer, tick_nohz_get_next_hrtimer());

    if (cpuidle_state_is_coupled(drv, index))
        ret = cpuidle_enter_state_coupled(dev, drv, index);
    else
        ret = cpuidle_enter_state(dev, drv, index);

    WRITE_ONCE(dev->next_hrtimer, 0);
    return ret;
}
```

## 4.5 `cpuidle_enter_state(dev, drv, index) -> target_state->enter()`

```c
// drivers/cpuidle/cpuidle.c

/**
 * cpuidle_enter_state - enter the state and update stats
 * @dev: cpuidle device for this cpu
 * @drv: cpuidle driver for this cpu
 * @index: index into the states table in @drv of the state to enter
 */
int cpuidle_enter_state(struct cpuidle_device *dev, struct cpuidle_driver *drv, int index) {
    struct cpuidle_state *target_state = &drv->states[index];
    bool broadcast = !!(target_state->flags & CPUIDLE_FLAG_TIMER_STOP);

    /*
     * Tell the time framework to switch to a broadcast timer because our
     * local timer will be shut down.  If a local timer is used from another
     * CPU as a broadcast timer, this call may fail if it is not available.
     */
    if (broadcast && tick_broadcast_enter()) {
        index = find_deepest_state(drv, dev, target_state->exit_latency_ns, CPUIDLE_FLAG_TIMER_STOP, false);
        if (index < 0) {
            default_idle_call();
            return -EBUSY;
        }
        target_state = &drv->states[index];
        broadcast = false;
    }

    if (target_state->flags & CPUIDLE_FLAG_TLB_FLUSHED)
        leave_mm(dev->cpu);

    sched_idle_set_state(target_state); /* Take note of the planned idle state. */

    time_start = ns_to_ktime(local_clock());
    int entered_state = target_state->enter(dev, drv, index);
    sched_clock_idle_wakeup_event();
    time_end = ns_to_ktime(local_clock());

    sched_idle_set_state(NULL); /* The cpu is no longer idle or about to enter idle. */

    if (broadcast) {
        if (WARN_ON_ONCE(!irqs_disabled()))
            local_irq_disable();

        tick_broadcast_exit();
    }

    if (!cpuidle_state_is_coupled(drv, index))
        local_irq_enable();

    if (entered_state >= 0) {
        s64 diff, delay = drv->states[entered_state].exit_latency_ns;

        // Update cpuidle counters
        diff = ktime_sub(time_end, time_start);
        dev->last_residency_ns = diff;
        dev->states_usage[entered_state].time_ns += diff;
        dev->states_usage[entered_state].usage++;

        if (diff < drv->states[entered_state].target_residency_ns) {
            for (i = entered_state - 1; i >= 0; i--) {
                if (dev->states_usage[i].disable)
                    continue;

                dev->states_usage[entered_state].above++; /* Shallower states are enabled, so update. */
                break;
            }
        } else if (diff > delay) {
            for (i = entered_state + 1; i < drv->state_count; i++) {
                if (dev->states_usage[i].disable)
                    continue;

                // Update if a deeper state would have been a better match for the observed idle duration.
                if (diff - delay >= drv->states[i].target_residency_ns)
                    dev->states_usage[entered_state].below++;

                break;
            }
        }
    } else {
        dev->last_residency_ns = 0;
        dev->states_usage[index].rejected++;
    }

    return entered_state;
}
```

这一步会调用 c-state 的 **<mark><code>enter()</code></mark>** 方法，
也就是我们上一节 driver 注册部分看到的，比如，

* `htl_idle`
* `acpi_idle`
* `intel_idle`

## 4.6 idle state **<mark><code>enter()</code></mark>** 回调方法：以 `hltpoll` c1 state 为例

```c
// https://github.com/torvalds/linux/blob/v5.15/arch/x86/include/asm/irqflags.h#L54

static inline __cpuidle void native_halt(void) {
    mds_idle_clear_cpu_buffers();
    asm volatile("hlt": : :"memory");
}
```

这条指令会让 CPU 进入 halted 状态，直到下一个中断事件到来。

# 5 快速确认调用路径：跟踪内核调用栈

代码中有很多分支，有时候想确定在特定机器（归根结底是特定配置）上走的是哪个逻辑。
这里简单介绍两种 trace 工具，可以比较快的确定。

## 5.1 `bpftrace`

模糊搜索可 trace 的内核函数，

```shell
$ bpftrace -l '*cpuidle*' # 或 bpftrace -l | grep cpuidle
...
```

跟着某个内核函数，看有没有调用到这里，并打印调用到这个函数时前面的调用栈：

```shell
$ bpftrace -e 'kprobe:cpuidle_enter_state {printf("%s\n",kstack);}'
        cpuidle_enter_state+1
        cpuidle_enter+41
        cpuidle_idle_call+300
        do_idle+123
        cpu_startup_entry+25
        secondary_startup_64_no_verify+194

$ bpftrace -e 'kprobe:intel_idle {printf("%s\n",kstack);}'
        intel_idle+1
        cpuidle_enter_state+137
        cpuidle_enter+41
        cpuidle_idle_call+300
        do_idle+123
        cpu_startup_entry+25
        secondary_startup_64_no_verify+194
```

更多使用方式可参考 
[Linux tracing/profiling 基础：符号表、调用栈、perf/bpftrace 示例等（2022）]({% link _posts/2022-07-18-linux-tracing-basis-zh.md %})。

## 5.2 `trace-cmd`

老牌 trace 工具，功能跟 bpftrace 类似，使用方式跟 perf 有点类似，

```shell
$ trace-cmd record -l 'cpuidle_enter_state' -p function_graph
$ trace-cmd report
          ...
          <idle>-0     [007] 699714.113701: funcgraph_entry:                   |  cpuidle_enter_state() {
          <idle>-0     [002] 699714.113703: funcgraph_entry:                   |  cpuidle_enter_state() {
```

# 参考资料

1. [The cpuidle subsystem](https://lwn.net/Articles/384146/), lwn.net, 2013
2. [Linux tracing/profiling 基础：符号表、调用栈、perf/bpftrace 示例等（2022）]({% link _posts/2022-07-18-linux-tracing-basis-zh.md %})
