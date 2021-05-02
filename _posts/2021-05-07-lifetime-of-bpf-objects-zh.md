---
layout    : post
title     : "[译] BPF 对象（BPF objects）的生命周期（Facebook，2018）"
date      : 2021-05-07
lastupdate: 2021-05-07
categories: bpf
---

### 译者序

本文翻译自 2018 年 Facebook 的一篇博客：
[Lifetime of BPF objects](https://facebookmicrosites.github.io/bpf/blog/2018/08/31/object-lifetime.html)，
作者 Alexei Starovoitov。

译文附录了一些内核（`4.19`）代码片段，方便在实现层面理解文中提到的一些东西。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

BPF 校验器（verifier）保证了内核能安全地执行 BPF 程序，但要想更安全地使用 BPF，
还需要理解 **<mark>BPF 对象（objects）的生命周期</mark>**。本文就这一主题展开深入讨论。

# 1 文件描述符（FD）和引用计数（refcnt）

**BPF 对象（objects）包括**：

* progs（BPF 程序）
* maps（kv 存储）
* debug info（调试信息）

**<mark>每个对象都有自己的 refcnt</mark>**，用户空间程序可以通过**文件描述符**（FD）访问这些对象。

## 1.1 创建 BPF map 过程

调用 `bpf_create_map()` 创建一个 map 时，内核会

1. 分配一个 `struct bpf_map` 对象，
2. 设置该对象的 `refcnt=1`（**内核实现**见 [附录](#appendix_1)），
3. 返回一个 `fd` 给用户空间进程。

**如果进程退出或者 crash 了**，这个 `fd` 将被关闭，相应 `refcnt--`。
在这个例子中， `refcnt--` 之后就变成 0 了，因此过了 **<mark>RCU grace period</mark>**
之后，就会触发释放内存（memory free）操作。

## 1.2 加载 BPF 程序（和它使用的 BPF map）过程

用到了 BPF map 的 BPF 程序，加载（load）过程分为两个阶段：

1. **<mark>创建 map</mark>**。

    这些 **map 的 FD** 稍后将放到 `BPF_LD_IMM64` 指令的 `imm` 字段中，**作为 BPF 程序的一部分**。

    （附录中会看到校验器在对 BPF 程序进行校验时，如何从 `imm` 中加载 FD。）

2. 内核**<mark>对 BPF 程序进行校验</mark>**，校验器会

    * 将该程序用到的**<mark>每个 map 的 refcnt++</mark>**（**内核实现**见 [附录](#appendix_2)），
    * 设置**<mark>该程序本身的 refcnt=1</mark>**。

此后，

* 用户空间**<mark>关闭与 map 关联的 FD 时</mark>**，map 并不会消失，
  因为 BPF 程序还在“使用”它们（虽然此时程序还没有 attach 到任何地方）。

* 当 **<mark>与 BPF 程序关联的 FD 关闭时</mark>**，如果 refcnt 变成 0，销毁逻辑会
  **遍历该程序用到的所有 map**，并将它们的 `refcnt--`。

    这种方式使得单个 BPF map 能同时被多个 BPF 程序（甚至是**<mark>不同类型的 BPF 程序</mark>**
    ）使用。例如，某个 tracing 类型的 BPF 程序将收集到的数据写入一个 BPF map，
    另一个 networking 类型的 BPF 程序读取其中的数据，用来做转发决策。

## 1.3 Attach BPF 程序到 hook 点

**<mark>BPF 程序 attach 到某个 hook 之后，refcnt 就会加 1</mark>**。
创建、加载和 attach BPF 程序的用户空间程序，此时就可以退出了。用户空间程序退出后
，内核空间的 BPF map 和 BPF 程序（map+program）还是 alive 的，因为程序的 `refcnt > 0`。

这就是该 BPF 对象的生命周期：只要 BPF 对象（程序或 map）的引用计
数大于 0，内核就会 keep it alive。

但并不是所有 attach 到 hook 点的 BPF program/map 的生命周期都是这样的，
attach 点（hook 点）也是分类型的。

## 1.4 Attach point 类型

### Global 类型（全局可访问）

包括：

* xdp
* tc clsact
* lwt
* cgroup-based hooks

这种类型中，**<mark>只要 BPF 对象还活着（alive），BPF 程序就会保持 attach 状态</mark>**。
例如，tc `clsact` 程序会 attach 到网络设备的 ingress 或 egress qdisc。只要不执行
`tc qdisc del` 之类的操作，这些程序就会一直在 qdisc 上处理包 —— 即使用户空间进程已经退出了。

### Local 类型（通过 FD 访问）

包括：

* kprobe
* uprobe 
* tracepoint 
* perf_event 
* raw_tracepoint 
* socket filters
* so_reuseport hooks

这种类型的程序**<mark>通过 FD 访问，因此其生命周期限制在持有 FD 的进程</mark>**
（the process that holds FD to tracing event）**<mark>生命周期内</mark>**。

例如对于 tracing 场景，

1. 程序首先调用 `fd = perf_event_open()`（`fd = bpf_raw_tracepoint_open(“tracepoint_name”, bpf_prog_fd))` ）
  获取跟踪事件用的文件描述符，
2. 然后执行 `ioctl(fd, IOC_SET_BPF, bpf_prog_fd)`。

由于这个 `fd` 限制在进程范围内（local to the process），因此如果进程挂了，`fd` 将被关闭。
在这种场景中，**<mark>内核会 detach 相应的 BPF 程序，并执行 refcnt--</mark>**。

> **<mark>cgroup object 目前是 global 方式</mark>**，但人们正在尝试引入一种基于
> FD 的 cgroup object（即 local 方式），因此将在 cgroup object 可能既是 global
> 的又是 local 的。

### 优缺点比较

Local 类型（基于 FD）主要优势：**<mark>自动清理</mark>**（auto cleaning），这意
味着一旦用户空间进程异常，内核会自动清理所有的对象。

最初的 kprobe/uprobe 都是 global 的，但大家很快发现，在生产环境部署
kprobe/uprobe + bpf 时，这种方式非常笨重。因此在内核中又引入了基于 FD 的
kprobe/uprobe API。

cgroup 接下来也可能会有一组类似的基于 FD 的 API。这样进程就能通过 FD 来持有某个
cgroup object，然后指定 FD（object）来 attach BPF 程序，而非现在的全局 cgroup
entity 方式。

基于 FD 的 API 对于网络处理（networking）同样友好。前段时间，某个 Facebook
Widely Deployed Binary (WDB) 中存在一个忘记清理 tc clsact BPF 程序的 bug，导致的后果
是：**当守护进程多次重启之后，网络设备的 egress hook 上累积了几千个（但其实是同一
个）BPF 程序**，其中只有一个是真正干活的，其他的都是在浪费 CPU 时间。最终系统性能
逐渐下降之后，这个问题才被发现。使用基于 FD 的网络 API 能避免这种问题。目前已经
有相关工作在进行，引入一个类似 tc clsact 的 API，在网络设备的 ingress 和 egress
路径上添加相应的hook，但实际上并不是基于 tc，而是基于 FD 的、带自动清理特性的
API。

# 2 BPF 文件系统（BPFFS）

另一种**<mark>保持 BPF 程序和 BPF map alive 的方式</mark>**是 BPFFS（BPF 文件系统）。

## 2.1 Pin 操作

用户空间进程可以将一个 BPF 程序或 BPF map “pin”（固定）到 BPFFS。
**<mark>Pin 操作会使 BPF object 的 refcnt++</mark>**，因此即使 BPF 程序接下来没有
attach 到任何地方，或者 BPF map 没有在任何地方被使用，这个 object 还是会保持 alive
状态。

典型场景是网络处理：

* 技术背景：attach 到网络设备上的 BPF 程序在处理数据包，并状态数据存储到 BPF map 中（没有用户空间 daemon 进程）。
* 技术需求：**管理员希望能时不时登录到机器，查看处理状态**。
* 解决方式：管理员用 `bpf_obj_get(obj_path_in_bpffs)` 直接从 BPFFS 获取 object（返回一个新的 FD 及指向该对象的 handle）。

## 2.2 Unpin 操作

**<mark>要 unpin 某个 object</mark>**，**只需要调用 `unlink()` 将文件从 BPFS 中
删除**，**<mark>内核将对相应对象执行 refcnt--</mark>**。

## 2.3 小结：对象操作与 refcnt 变化

* `create  -> refcnt=1`
* `attach  -> refcnt++`
* `detach  -> refcnt--`
* `pin     -> refcnt++`
* `unpin   -> refcnt--`
* `unlink  -> refcnt--`
* `close   -> refcnt--`

# 3 BPF 程序的 detach 和 replace

## 3.1 Detach 操作

Detach 是 BPF 程序生命周期中的重要步骤。

将 BPF 程序从 hook 点 detach 之后，**<mark>接下来再有相应事件发生时，不会再触发
该 BPF 程序的执行</mark>**。

但是，**如果 detach 时 BPF 程序正在执行**，那 detach 操作会先返回，BPF 程序会完全此次执行。

## 3.2 替换（replace）操作

Replace 是 BPF 程序生命周期中的另一重要步骤。

**<mark>cgroup-bpf hooks 支持 BPF 程序的替换（replace）操作</mark>**。
内核保证所有事件（events）都会得到 BPF 程序的处理 —— 但**中间存在一个窗口**，
例如，在某个时刻，可能 CPU 1 上执行的是老程序，CPU 2 上执行的是新程序 ——
**<mark>没有“原子”替换操作</mark>**（“atomic” replacement）。

一些 BPF 开发者用下面的方式避免这个问题：
新程序仍然使用老程序的 BPF map —— 因此在替换过程中数据只有一份，
只是程序文本被替换了。这种替换方式是安全的，不管新老程序是否同时在不同 CPU 上运行。

但这种方式**仅适用于新/老程序比较相似，没有引入新数据结构的情况**。
例如，新老程序的区别仅仅是编译时 debug 开关 on/off 的区别。

# 译文附录：一些相关的 BPF 内核实现

<a name="appendix_1"></a>

## 附录一：创建 BPF map

下面是创建一个 BPF map 的调用过程，尤其展示 **<mark>map 的引用计数（refcnt）是在哪里更新的</mark>**：

### `bpf_create_map() -> bpf_create_map_xattr() -> sys_bpf() -> SYSCALL_DEFINE3(bpf, ...)`：系统调用

```c
// tools/lib/bpf/bpf.c

int bpf_create_map(enum bpf_map_type map_type, int key_size,
           int value_size, int max_entries, __u32 map_flags)
{
    struct bpf_create_map_attr map_attr = {};

    map_attr.map_type = map_type;
    map_attr.map_flags = map_flags;
    map_attr.key_size = key_size;
    map_attr.value_size = value_size;
    map_attr.max_entries = max_entries;

    return bpf_create_map_xattr(&map_attr);
}

int bpf_create_map_xattr(const struct bpf_create_map_attr *create_attr)
{
    union bpf_attr attr;
    memset(&attr, '\0', sizeof(attr));
    attr.xx = create_attr->xx; // convert: 'struct bpf_create_map_attr -> struct bpf_attr'

    return sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}
```

### `SYSCALL_DEFINE3(bpf, ...) -> case BPF_MAP_CREATE -> map_create()`：创建 map

`bpf()` 系统调用：

```c
// kernel/bpf/syscall.c

SYSCALL_DEFINE3(bpf, int, cmd, union bpf_attr __user *, uattr, unsigned int, size)
{
    switch (cmd) {
    case BPF_MAP_CREATE:
        err = map_create(&attr);
        break;
    case BPF_PROG_LOAD: // 加载 BPF 程序
        err = bpf_prog_load(&attr);
        break;
    ...
    }

    return err;
}

static int map_create(union bpf_attr *attr)
{
    ...
    map = find_and_alloc_map(attr);

    atomic_set(&map->refcnt, 1);  // 设置引用计数
    atomic_set(&map->usercnt, 1); // 设置引用计数

    return err;
}
```

<a name="appendix_2"></a>

## 附录二：加载 BPF 程序

### `bpf(BPF_PROG_LOAD) -> sys_bpf() -> SYSCALL_DEFINE3(bpf, ...)`：系统调用

### `SYSCALL_DEFINE3(bpf, ...) -> case BPF_PROG_LOAD -> bpf_prog_load()`：加载逻辑

通过 `bpf(BPF_PROG_LOAD)` 系统调用加载程序时，会调用到下面的函数：

```c
// kernel/bpf/syscall.c

static int bpf_prog_load(union bpf_attr *attr)
{
    enum bpf_prog_type type = attr->prog_type;
    struct bpf_prog *prog;

    bpf_prog_load_fixup_attach_type(attr);
    if (bpf_prog_load_check_attach_type(type, attr->expected_attach_type))
        return -EINVAL;

    /* plain bpf_prog allocation */
    prog = bpf_prog_alloc(bpf_prog_size(attr->insn_cnt), GFP_USER);
    prog->expected_attach_type = attr->expected_attach_type;
    prog->aux->offload_requested = !!attr->prog_ifindex;
    prog->len = attr->insn_cnt;

    copy_from_user(prog->insns, u64_to_user_ptr(attr->insns), bpf_prog_insn_size(prog));

    prog->orig_prog = NULL;
    prog->jited = 0;

    atomic_set(&prog->aux->refcnt, 1); // 引用计数置 1

    if (bpf_prog_is_dev_bound(prog->aux)) {
        bpf_prog_offload_init(prog, attr);
    }

    /* find program type: socket_filter vs tracing_filter */
    find_prog_type(type, prog);

    prog->aux->load_time = ktime_get_boot_ns();
    bpf_obj_name_cpy(prog->aux->name, attr->prog_name);

    /* run eBPF verifier */
    err = bpf_check(&prog, attr); // 执行 BPF 校验器
    if (err < 0)
        goto free_used_maps;

    prog = bpf_prog_select_runtime(prog, &err);
    bpf_prog_alloc_id(prog);

    bpf_prog_new_fd(prog);
    bpf_prog_kallsyms_add(prog);
    return err;
}
```

> 通过 `bpf(BPF_OBJ_GET)` 获取某个 BPF object 时，也会增加该 BPF object 的引用计数，调用路径：
>
> bpf() -> SYSCALL_DEFINE3(bpf, ...) -> bpf_obj_get() -> bpf_obj_get_user() -> bpf_any_get() -> bpf_prog_inc()

### `bpf_prog_load() -> bpf_check()`：执行内核校验

校验器逻辑（忽略错误处理）：

```c
// kernel/bpf/verifier.c

int bpf_check(struct bpf_prog **prog, union bpf_attr *attr)
{
    struct bpf_verifier_env *env;
    struct bpf_verifier_log *log;

    env = kzalloc(sizeof(struct bpf_verifier_env), GFP_KERNEL);
    log = &env->log;

    env->insn_aux_data = vzalloc(array_size(sizeof(struct bpf_insn_aux_data), (*prog)->len));
    env->prog = *prog;
    env->ops = bpf_verifier_ops[env->prog->type];

    /* grab the mutex to protect few globals used by verifier */
    mutex_lock(&bpf_verifier_lock);

    replace_map_fd_with_map_ptr(env); // 对程序用到的 map 进行处理

    if (bpf_prog_is_dev_bound(env->prog->aux)) {
        bpf_prog_offload_verifier_prep(env);
    }

    env->explored_states = kcalloc(env->prog->len, sizeof(struct bpf_verifier_state_list *), GFP_USER);
    ret = -ENOMEM;
    if (!env->explored_states)
        goto skip_full_check;

    env->allow_ptr_leaks = capable(CAP_SYS_ADMIN);

    ret = check_cfg(env);
    if (ret < 0)
        goto skip_full_check;

    ret = do_check(env);
    if (env->cur_state) {
        free_verifier_state(env->cur_state, true);
        env->cur_state = NULL;
    }

skip_full_check:
    while (!pop_stack(env, NULL, NULL));
    free_states(env);
    ...
    return ret;
}
```

### `bpf_check() -> replace_map_fd_with_map_ptr() -> bpf_map_inc()`：更新 map refcnt

`replace_map_fd_with_map_ptr(env)` 会更新这个程序用到的 BPF map 的引用计数：

```c
// kernel/bpf/verifier.c

/* look for pseudo eBPF instructions that access map FDs and
 * replace them with actual map pointers */
static int replace_map_fd_with_map_ptr(struct bpf_verifier_env *env)
{
    struct bpf_insn *insn = env->prog->insnsi;
    int          insn_cnt = env->prog->len;

    for (i = 0; i < insn_cnt; i++, insn++) {
        if (insn[0].code == (BPF_LD | BPF_IMM | BPF_DW)) {
            if (insn->src_reg != BPF_PSEUDO_MAP_FD) {
                verbose(env, "unrecognized bpf_ld_imm64 insn\n");
                return -EINVAL;
            }

            struct fd f = fdget(insn->imm);
            struct bpf_map *map = __bpf_map_get(f);

            /* store map pointer inside BPF_LD_IMM64 instruction */
            insn[0].imm = (u32) (unsigned long) map;
            insn[1].imm = ((u64) (unsigned long) map) >> 32;

            /* hold the map. If program rejected by verifier, the map will be released by release_maps() or will
             * be used by the valid program until it's unloaded and all maps are released in free_used_maps() */
            map = bpf_map_inc(map, false);             // 增加引用计数
            env->used_maps[env->used_map_cnt++] = map; // 记录这个程序用到的 map

next_insn:
            insn++; i++;
            continue;
        }
    }

    /* now all pseudo BPF_LD_IMM64 instructions load valid
     * 'struct bpf_map *' into a register instead of user map_fd.
     * These pointers will be used later by verifier to validate map access.  */
    return 0;
}
```

```c
// kernel/bpf/syscall.c

struct bpf_map *bpf_map_inc(struct bpf_map *map, bool uref)
{
    if (uref)
        atomic_inc(&map->usercnt);
    return map;
}
EXPORT_SYMBOL_GPL(bpf_map_inc);
```
