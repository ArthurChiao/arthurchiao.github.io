---
layout    : post
title     : "BPF 进阶笔记（三）：BPF Map 内核实现"
date      : 2021-07-13
lastupdate: 2023-07-23
categories: bpf xdp
---

内核目前支持 [30 来种](https://github.com/torvalds/linux/blob/v5.8/include/uapi/linux/bpf.h#L122)
BPF map 类型。

本文整理一些与这些 map 相关的内核实现。

## 关于 “BPF 进阶笔记” 系列

平时学习和使用 BPF 时所整理。由于是笔记而非教程，因此内容不会追求连贯，有基础的
同学可作查漏补缺之用。

文中涉及的代码，如无特殊说明，均基于内核 **<mark>5.8/5.10</mark>**。

* [BPF 进阶笔记（一）：BPF 程序（BPF Prog）类型详解：使用场景、函数签名、执行位置及程序示例]({% link _posts/2021-07-04-bpf-advanced-notes-1-zh.md %})
* [BPF 进阶笔记（二）：BPF Map 类型详解：使用场景、程序示例]({% link _posts/2021-07-04-bpf-advanced-notes-2-zh.md %})
* [BPF 进阶笔记（三）：BPF Map 内核实现]({% link _posts/2021-07-04-bpf-advanced-notes-3-zh.md %})
* [BPF 进阶笔记（四）：调试 BPF 程序]({% link _posts/2021-07-04-bpf-advanced-notes-4-zh.md %})
* [BPF 进阶笔记（五）：几种 TCP 相关的 BPF（sockops、struct_ops、header options）]({% link _posts/2021-07-04-bpf-advanced-notes-5-zh.md %})

----

* TOC
{:toc}

----


# 基础

## BPF map 类型：完整列表

所有 map 类型的定义：

```c
// https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/bpf.h#L130

enum bpf_map_type {
    BPF_MAP_TYPE_UNSPEC,
    BPF_MAP_TYPE_HASH,
    BPF_MAP_TYPE_ARRAY,
    BPF_MAP_TYPE_PROG_ARRAY,
    BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    BPF_MAP_TYPE_PERCPU_HASH,
    BPF_MAP_TYPE_PERCPU_ARRAY,
    BPF_MAP_TYPE_STACK_TRACE,
    BPF_MAP_TYPE_CGROUP_ARRAY,
    BPF_MAP_TYPE_LRU_HASH,
    BPF_MAP_TYPE_LRU_PERCPU_HASH,
    BPF_MAP_TYPE_LPM_TRIE,
    BPF_MAP_TYPE_ARRAY_OF_MAPS,
    BPF_MAP_TYPE_HASH_OF_MAPS,
    BPF_MAP_TYPE_DEVMAP,
    BPF_MAP_TYPE_SOCKMAP,
    BPF_MAP_TYPE_CPUMAP,
    BPF_MAP_TYPE_XSKMAP,
    BPF_MAP_TYPE_SOCKHASH,
    BPF_MAP_TYPE_CGROUP_STORAGE,
    BPF_MAP_TYPE_REUSEPORT_SOCKARRAY,
    BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE,
    BPF_MAP_TYPE_QUEUE,
    BPF_MAP_TYPE_STACK,
    BPF_MAP_TYPE_SK_STORAGE,
    BPF_MAP_TYPE_DEVMAP_HASH,
    BPF_MAP_TYPE_STRUCT_OPS,
    BPF_MAP_TYPE_RINGBUF,
    BPF_MAP_TYPE_INODE_STORAGE,

};
```

## 不同 map 类型支持的操作（`xx_map_ops`）：完整列表

[include/linux/bpf_types.h](https://github.com/torvalds/linux/blob/v5.8/include/linux/bpf_types.h)
中定义了不同类型的 BPF map 所支持的操作：

```c
// include/linux/bpf_types.h

BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY,            array_map_ops)             // kernel/bpf/arraymap.c
BPF_MAP_TYPE(BPF_MAP_TYPE_PERCPU_ARRAY,     percpu_array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PROG_ARRAY,       prog_array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PERF_EVENT_ARRAY, perf_event_array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_CGROUP_ARRAY,     cgroup_array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_CGROUP_STORAGE,   cgroup_storage_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_HASH,             htab_map_ops)              // kernel/bpf/hashtab.c
BPF_MAP_TYPE(BPF_MAP_TYPE_PERCPU_HASH,      htab_percpu_map_ops)       // kernel/bpf/hashtab.c
BPF_MAP_TYPE(BPF_MAP_TYPE_LRU_HASH,         htab_lru_map_ops)          // kernel/bpf/hashtab.c
BPF_MAP_TYPE(BPF_MAP_TYPE_LRU_PERCPU_HASH,  htab_lru_percpu_map_ops)   // kernel/bpf/hashtab.c
BPF_MAP_TYPE(BPF_MAP_TYPE_LPM_TRIE,         trie_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_STACK_TRACE,      stack_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY_OF_MAPS,    array_of_maps_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_HASH_OF_MAPS,     htab_of_maps_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_DEVMAP,           dev_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_SOCKMAP,          sock_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_SOCKHASH,         sock_hash_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_CPUMAP,           cpu_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_XSKMAP,           xsk_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_REUSEPORT_SOCKARRAY, reuseport_array_ops)
```

大部分实现都在 `kernel/bpf/` 目录下。

# 重要结构体

## `struct bpf_map`：内核 bpf map 结构体

调用 map 的 create 方法**<mark>创建 map 时</mark>**，返回的是一个 `struct
bpf_map *` 对象，**<mark>记录 map 元数据</mark>**，定义如下：

```c
// include/linux/bpf.h

struct bpf_map {
    // First two cachelines with read-mostly members of which some are also accessed in fast-path (e.g. ops, max_entries).
    const struct bpf_map_ops *ops ____cacheline_aligned; // 增删查改等操作函数
    struct bpf_map *inner_map_meta;

    void *security;

    // 常规 map 属性
    enum bpf_map_type map_type;
    u32 key_size;
    u32 value_size;
    u32 max_entries;
    u32 map_flags;

    int    spin_lock_off; /* >=0 valid offset, <0 error */
    u32    id;
    int    numa_node;
    u32    btf_key_type_id;
    u32    btf_value_type_id;
    struct btf *btf;
    struct bpf_map_memory memory;
    char   name[BPF_OBJ_NAME_LEN];
    u32    btf_vmlinux_value_type_id;
    bool   bypass_spec_v1;
    bool   frozen; /* write-once; write-protected by freeze_mutex */
    /* 22 bytes hole */

    // The 3rd and 4th cacheline with misc members to avoid false sharing particularly with refcounting.
    atomic64_t refcnt ____cacheline_aligned; // 引用计数
    atomic64_t usercnt;                      // 引用计数
    struct work_struct work;
    struct mutex freeze_mutex;
    u64 writecnt; /* writable mmap cnt; protected by freeze_mutex */
};
```

其中尤其重要的几个字段：

1. `refcnt`：引用计数，**<mark>很多类型的 map 是依据 refcnt 是否等于 0 来执行 map GC 的</mark>**
   。更多信息，参考 [(译) BPF 对象（BPF objects）的生命周期（Facebook，2018）]({% link _posts/2021-05-07-lifetime-of-bpf-objects-zh.md %})。

# ------------------------------------------------------------------------
# Hash Maps
# ------------------------------------------------------------------------

Hash map 的实现见
[kernel/bpf/hashtab.c](https://github.com/torvalds/linux/blob/v5.8/kernel/bpf/hashtab.c)。
五种类型共用一套代码：

* `BPF_MAP_TYPE_HASH`
* `BPF_MAP_TYPE_PERCPU_HASH`
* `BPF_MAP_TYPE_LRU_HASH`
* `BPF_MAP_TYPE_LRU_PERCPU_HASH`
* `BPF_MAP_TYPE_HASH_OF_MAPS`

# 数据结构设计

下图是 **<mark>BPF hash map 如何组织的</mark>**：

```
       BPF Hash Map    

  +--------------------+
  | struct bpf_map map |--> general BPF map (metadata)
  |--------------------|
  | struct bucket *    |--> bucket linked-list
  |--------------------|
  | void *elems        |--> elements (hash+key+value), link-listed
  |--------------------|
  |                    |
  |--------------------|
  | count, n_buckets,  |--> hash map metadata
  | elem_size, hashrnd |
  +--------------------+

```

1. 内核表示 BPF map 的结构体 `struct bpf_map` 是**<mark>不区 map 分类型</mark>**
   的，因此 hash map **<mark>在 BPF map 之上又封装了一层</mark>**，即 `struct
   bpf_htab`，**<mark>表示一个内核 hash map</mark>**；

2. Hash map 又主要分为两部分：

    1. Buckets：对 key 进行哈希之后找到对应的 buckets，但这里存放的只是 buckets
       链表和锁等元数据，**<mark>不存放数据</mark>**；
    2. Elements：即**<mark>真正需要存放的数据</mark>**，也组织成链表。


# 重要结构体

## `struct bpf_htab`：内核哈希表

前面已经提到，内核 bpf map `struct bpf_map` 记录的只是通用 map 元数据（不区分
map 类型）。
**<mark>对于 hash map 来说，真正的数据是存储在哈希表</mark>** `struct bpf_htab` 里：

```c
// kernel/bpf/hashtab.c

// 哈希表（hash table）数据结构
struct bpf_htab {
    struct bpf_map map;      // 这个 hash map 对应的 BPF map（元数据）

    struct bucket *buckets;  // 链表 + 锁
    void *elems;             // 每个 elem 的结构：struct htab_elem + key + value
    union {
        struct pcpu_freelist freelist;
        struct bpf_lru lru;
    };
    struct htab_elem *__percpu *extra_elems;

    atomic_t count;          // element 数量
    u32 n_buckets;           // buckets 数量
    u32 elem_size;           // element 大小，单位 bytes
    u32 hashrnd;             // 哈希随机数
};
```

从定义可以看出，这个哈希表支持前面提到个所有 hash map 类型（percpu、lru 等等）。

另外，这个结构体里有**<mark>三段内存区域</mark>**，分别存储 bucket 信息、
elements（hash+key+value 等）信息和额外的 elements 信息：

```
             +--+--+--+--+--+----------+
buckets      |  |  |  |  |  | ...      |
             +--+--+--+--+--+----------+
             +-------+--------+--------+------------------------------+
elems        |       |        |        | ...                          |
             +-------+--------+--------+------------------------------+
             +-------+--------+-----------------+
extra_elemes |       |        | ...             | 只有 8 个空间，普通 hash map 用；PERCPU、LRU map 不用。
             +-------+--------+-----------------+
```

## `struct bucket`：哈希槽（链表，不存放实际数据）

Bucket 存放的只是链表和锁，**<mark>并不存放实际数据</mark>**，

```c
// kernel/bpf/hashtab.c

struct bucket {
    struct hlist_nulls_head head;
    union {
        raw_spinlock_t raw_lock;
        spinlock_t     lock;
    };
};
```

关于这个结构体的用途，内核中有超长的注释，见源码。

## `struct htab_elem`：存放 hash+key+value 等数据

**<mark>存放实际的 map 数据</mark>**：

```c
// kernel/bpf/hashtab.c

struct htab_elem {
    // 不同 hash map 类型特定的字段
    union {
        struct hlist_nulls_node hash_node; // 内核通用链表类型之一 hlist_nulls
        struct {
            void *padding;
            union {
                struct bpf_htab *htab;
                struct pcpu_freelist_node fnode;
                struct htab_elem *batch_flink;
            };
        };
    };
    union {
        struct rcu_head rcu;
        struct bpf_lru_node lru_node;
    };

    // 公共字段
    u32 hash;                 // 哈希值
    char key[] __aligned(8);  // 指针，指向接下来的 key+value 数据
};
```

这个结构体也包含了一个链表结构，但注意最后两个字段：

1. `u32 hash`：这是**<mark>对 key 进行哈希之后得到的哈希值</mark>**；
2. `char key[]`：这是一个指针，在初始化时会**<mark>指向 key+value 的连续内存区域</mark>**。

# 1 `BPF_MAP_TYPE_HASH`

几种不同类型的 hash map 都是在同一套代码中实现的。这里看一下其中最简单的一种：hash map。

首先，**<mark>注册的操作方法</mark>**（回调函数）集合见
[kernel/bpf/hashtab.c](https://github.com/torvalds/linux/blob/v5.10/kernel/bpf/hashtab.c#L1831)：

```c
// kernel/bpf/hashtab.c

const struct bpf_map_ops htab_map_ops = {
    .map_alloc_check   = htab_map_alloc_check,
    .map_alloc         = htab_map_alloc,
    .map_free          = htab_map_free,
    .map_get_next_key  = htab_map_get_next_key,
    .map_lookup_elem   = htab_map_lookup_elem,   // 查找
    .map_update_elem   = htab_map_update_elem,   // 创建或更新
    .map_delete_elem   = htab_map_delete_elem,   // 删除
    .map_gen_lookup    = htab_map_gen_lookup,
    .map_seq_show_elem = htab_map_seq_show_elem,
    BATCH_OPS(htab),
};
```

<a name="htab_map_alloc"></a>
## 创建 map：`struct bpf_map *htab_map_alloc()`

函数签名：

```c
// kernel/bpf/hashtab.c

static struct bpf_map *htab_map_alloc(union bpf_attr *attr)
```

实现在 [kernel/bpf/hashtab.c](https://github.com/torvalds/linux/blob/v5.10/kernel/bpf/hashtab.c#L411)，
**<mark>调用栈</mark>**：

```shell
htab_map_alloc                                   # kernel/bpf/hashtab.c
  |-htab = kzalloc(sizeof(*htab), GFP_USER)
  |-bpf_map_init_from_attr(&htab->map, attr)     # 初始化 map 元数据；htab->map 就是 struct bpf_map
  |
  |-htab->n_buckets = ...
  |-htab->elem_size = ...
  |-bpf_map_charge_init(&htab->map.memory, cost) # 确保 map size 不会过大
  |
  | # 分配 bucket 内存
  |-htab->buckets = bpf_map_area_alloc(size, numa_node)                        // kernel/bpf/syscall.c
  |                  |-__bpf_map_area_alloc(size, numa_node, false)
  |                     |-if condition
  |                     |   kmalloc_node(GFP_USER)
  |                     |-else
  |                         __vmalloc_node_range(GFP_KERNEL)
  |
  |-htab->hashrnd = ...                          # 初始化哈希种子
  |
  |-if (prealloc) { # 提前为所有 elements 分配内存
      prealloc_init(htab);
        |-htab->elems = bpf_map_area_alloc(elem_size*n_entries, numa_node);
        |-per-cpu and lru initiazations if needed

      # 分配 extra 内存
      if (!percpu && !lru)       // lru itself can remove the least used element, so
        alloc_extra_elems(htab); // there is no need for an extra elem during map_update
          |-__alloc_percpu_gfp(sizeof(struct htab_elem *), 8, GFP_USER);
    }
```

前面介绍 `struct bpf_htab` 结构体时已经提到，这里有**<mark>三段内存</mark>**需要分配：

1. buckets 空间：注意这里的 buckets 并不是用来存 elements 的，只是一
   个链表，里面存放了链表、锁等简单数据；
2. **<mark>如果启用了预分配，为一次性为所有 elements 分配内存空间</mark>**。

    预分配的好处是性能更高，因为不需要为每个 element 动态分配内存；坏处也显而易
    见，就是**<mark>一上来就会占用掉这个 map 能占用的最大内存</mark>**，即使这时
    map 还是空的。

    举例：Cilium 有一个 `--preallocate-bpf-maps='false'` 选项，默认是 false。
3. extra_elems

## 查询 map：`void *htab_map_lookup_elem()`

逻辑：

1. 根据 key 计算出一个哈希值 `hash`，
2. 以 `hash` 作为**<mark>数组索引</mark>**，直接定位到对应的 bucket（`O(1)`），
3. **<mark>顺序遍历</mark>** bucket 内的 `struct htab_elem` 元素，如果 `hash` 和
   `key` 都相同，就返回对应的 value 其实地址；否则返回空。这里虽然是顺序遍历，但
   **<mark>除非有哈希冲突，否则第一次就返回了</mark>**。

下面看实现。

传入参数是 bpf map 和 key，**<mark>返回的是 value 的起始地址</mark>**：

```c
// kernel/bpf/hashtab.c

// 返回 value 起始地址
static void *htab_map_lookup_elem(struct bpf_map *map, void *key)
{
    struct htab_elem *l = __htab_map_lookup_elem(map, key);
    if (l)
        // l->key 指向的是 key+value 的地址，因此 l->key + key_size 指向的才是 value，
        // 此外还需要考虑 key_size 对其 8 字节，因此得到下面一行代码：
        return l->key + round_up(map->key_size, 8);

    return NULL;
}

static void *__htab_map_lookup_elem(struct bpf_map *map, void *key)
{
    struct bpf_htab *htab = container_of(map, struct bpf_htab, map);

    hash = htab_map_hash(key, key_size, htab->hashrnd);
    head = select_bucket(htab, hash); // 以 hash 作为数组索引，直接定位到 bucket 起始地址，返回 bucket 内的链表头指针
                                      // 简化之后：head = htab->buckets[hash]->head
    return lookup_nulls_elem_raw(head, hash, key, key_size, htab->n_buckets);
}

// can be called without bucket lock. it will repeat the loop in
// the unlikely event when elements moved from one bucket into another while link list is being walked
static struct htab_elem *
lookup_nulls_elem_raw(struct hlist_nulls_head *head, u32 hash, void *key, u32 key_size, u32 n_buckets)
{
    struct hlist_nulls_node *n; // 通用链表类型之一，与普通链表的区别是最后一个元素不是 NULL 指针，而是 'nulls' 元素
    struct htab_elem *l;

again:
    // 顺序遍历 head 指向的（bucket）链表。
    hlist_nulls_for_each_entry_rcu(l, n, head, hash_node)
       if (l->hash == hash && !memcmp(&l->key, key, key_size)) // 哈希值和 key 都相同
           return l;
    // 为便于理解，以上代码展开和简化之后等价于下面的（伪）代码
    // for (n=head; n!=nulls && l=(struct htab_elem *)n->hash_node; n=n->next)
    //     if (l->hash == hash && !memcmp(&l->key, key, key_size)) // 哈希值和 key 都相同
    //         return l;

    if (get_nulls_value(n) != (hash & (n_buckets - 1)))
        goto again;

    return NULL;
}
```

## 插入或更新 map：`int htab_map_update_elem()`

插入和更新都是执行这个函数，返回值：

* `0`：成功
* 其他：错误码

### 调用栈

```
htab_map_update_elem
 |-hash = htab_map_hash(key, key_size, htab->hashrnd)
 |-b = __select_bucket(htab, hash)
 |-l_old = lookup_nulls_elem_raw(head, hash, key, key_size, htab->n_buckets)
 |
 |-if l_old
 |   copy_map_value_locked(map, l_old->key + round_up(key_size, 8), value, false)
 |   return 0
 |
 |-l_old = lookup_elem_raw(head, hash, key, key_size);
 |-check_flags(htab, l_old, map_flags);
 |-l_new = alloc_htab_elem(htab, key, value, key_size, hash, false, false, l_old);
 |          |-if prealloc
 |          |   ...
 |          |-else
 |          |   atomic_inc_return(&htab->count)
 |          |   l_new = kmalloc_node()
 |          |
 |          |-memcpy(l_new->key, key, key_size);
 |          |-copy_map_value(&htab->map, l_new->key + round_up(key_size, 8), value);
 |          |-l_new->hash = hash;
 |          |-return l_new
 |
 |-hlist_nulls_add_head_rcu(&l_new->hash_node, head);
 |
 |-if l_old
 |    hlist_nulls_del_rcu(&l_old->hash_node);
 |    if (!htab_is_prealloc(htab))
 |       free_htab_elem(htab, l_old);
 |-return 0
```

### 实现

```c
// kernel/bpf/hashtab.c

/* Called from syscall or from eBPF program */
static int htab_map_update_elem(struct bpf_map *map, void *key, void *value, u64 map_flags)
{
    struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
    struct htab_elem *l_new = NULL, *l_old;

    key_size = map->key_size;
    hash = htab_map_hash(key, key_size, htab->hashrnd); // 根据 key 计算出一个哈希值

    struct bucket *b = __select_bucket(htab, hash);
    head = &b->head;

    if (unlikely(map_flags & BPF_F_LOCK)) { // BPF_F_LOCK: spin_lock-ed map_lookup/map_update, defined in include/uapi/linux/bpf.h
        /* find an element without taking the bucket lock */
        l_old = lookup_nulls_elem_raw(head, hash, key, key_size, htab->n_buckets);
        ret = check_flags(htab, l_old, map_flags);
        if (ret)
            return ret;

        if (l_old) { // 如果已经存在：获取 elem lock，然后原地更新 value
            copy_map_value_locked(map, l_old->key + round_up(key_size, 8), value, false);
            return 0;
        }
    }

    // 至此，确认老记录不存在。
    // 接下来：获取 bucket lock 然后再查询一次。99.9% 的概率仍然是查不到，但这一步必须做。
    flags = htab_lock_bucket(htab, b);
    l_old = lookup_elem_raw(head, hash, key, key_size);
    check_flags(htab, l_old, map_flags);

    if (unlikely(l_old && (map_flags & BPF_F_LOCK))) {
        // first lookup without the bucket lock didn't find the element,
        // but second lookup with the bucket lock found it.
        // This case is highly unlikely, but has to be dealt with:
        // grab the element lock in addition to the bucket lock and update element in place
        copy_map_value_locked(map, l_old->key + round_up(key_size, 8), value, false);
        ret = 0;
        goto err;
    }

    // 分配新 element。内部：根据 map 是否是 prealloc 模式，处理逻辑会有所不同
    l_new = alloc_htab_elem(htab, key, value, key_size, hash, false, false, l_old);

    // 插到链表头，so that concurrent search will find it before old elem */
    hlist_nulls_add_head_rcu(&l_new->hash_node, head);

    // 如果老的还在，删掉
    if (l_old) {
        hlist_nulls_del_rcu(&l_old->hash_node);
        if (!htab_is_prealloc(htab))
            free_htab_elem(htab, l_old);
    }
    ret = 0;

err:
    htab_unlock_bucket(htab, b, flags);
    return ret;
}

static struct htab_elem *
alloc_htab_elem(struct bpf_htab *htab, void *key, void *value, u32 key_size, u32 hash,
                     bool percpu, bool onallcpus, struct htab_elem *old_elem)
{
    u32 size = htab->map.value_size;
    bool prealloc = htab_is_prealloc(htab);
    struct htab_elem *l_new, **pl_new;
    void __percpu *pptr;

    if (prealloc) {
        if (old_elem) {
            pl_new = this_cpu_ptr(htab->extra_elems);
            l_new = *pl_new;
            htab_put_fd_value(htab, old_elem);
            *pl_new = old_elem;
        } else {
            struct pcpu_freelist_node *l;

            l = __pcpu_freelist_pop(&htab->freelist);
            l_new = container_of(l, struct htab_elem, fnode);
        }
    } else {
        if (atomic_inc_return(&htab->count) > htab->map.max_entries)
            if (!old_elem) {
                l_new = ERR_PTR(-E2BIG);
                goto dec_count;
            }
        l_new = kmalloc_node(htab->elem_size, GFP_ATOMIC, htab->map.numa_node);
        check_and_init_map_lock(&htab->map, l_new->key + round_up(key_size, 8));
    }

    memcpy(l_new->key, key, key_size);

    if (percpu) {
        ...
    } else if (fd_htab_map_needs_adjust(htab)) {
        size = round_up(size, 8);
        memcpy(l_new->key + round_up(key_size, 8), value, size);
    } else {
        copy_map_value(&htab->map, l_new->key + round_up(key_size, 8), value);
    }

    l_new->hash = hash;
    return l_new;
dec_count:
    atomic_dec(&htab->count);
    return l_new;
}
```

## 获取下一个 key：`int htab_map_get_next_key(map, key, void *next_key)`

为什么要有获取下一个 key 的方法？

### 逻辑

获取非第一个 key：

1. 根据 key 计算一个哈希值 `hash`；
2. 以 `hash` 作为数组索引，直接定位到对应的 bucket；
3. 在该 bucket 内查找给定 key；如果找到，按顺序返回下一个 key。

    这里需要注意的是，**<mark>下一个 key 可能在当前 bucket，也可能在下一个 bucket</mark>**。

获取第一个 key：没有别的办法，只能顺序遍历 buckets 及每个 buckets 内的各元素。

```c
// kernel/bpf/hashtab.c

/* Called from syscall */
static int htab_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
    struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
    struct htab_elem *l, *next_l;

    key_size = map->key_size;

    if (!key)
        goto find_first_elem;

    hash = htab_map_hash(key, key_size, htab->hashrnd);
    head = select_bucket(htab, hash);

    /* lookup the key */
    l = lookup_nulls_elem_raw(head, hash, key, key_size, htab->n_buckets);
    if (!l)
        goto find_first_elem;

    /* key was found, get next key in the same bucket */
    next_l = hlist_nulls_entry_safe(hlist_nulls_next_rcu(&l->hash_node), struct htab_elem, hash_node);
    if (next_l) { // if next elem in this hash list is non-zero, just return it
        memcpy(next_key, next_l->key, key_size);
        return 0;
    }

    /* no more elements in this hash list, go to the next bucket */
    i = hash & (htab->n_buckets - 1);
    i++;

find_first_elem:
    for (; i < htab->n_buckets; i++) { // iterate over buckets
        head = select_bucket(htab, i);

        /* pick first element in the bucket */
        next_l = hlist_nulls_entry_safe(hlist_nulls_first_rcu(head), struct htab_elem, hash_node);
        if (next_l) { // if it's not empty, just return it
            memcpy(next_key, next_l->key, key_size);
            return 0;
        }
    }

    /* iterated over all buckets and all elements */
    return -ENOENT;
}
```

# 2 `BPF_MAP_TYPE_PERCPU_HASH`

# 3 `BPF_MAP_TYPE_LRU_HASH`


**<mark>为每个 bucket 维护一个 LRU list</mark>**，bucket 满了之后删除最老的。

# 4 `BPF_MAP_TYPE_LRU_PERCPU_HASH`

<a name="bpf_map_type_hash_of_maps"></a>
# 5 `BPF_MAP_TYPE_HASH_OF_MAPS`

# ------------------------------------------------------------------------
# Array Maps
# ------------------------------------------------------------------------

类型：

* `BPF_MAP_TYPE_ARRAY`
* `BPF_MAP_TYPE_PERCPU_ARRAY`
* `BPF_MAP_TYPE_PROG_ARRAY`
* `BPF_MAP_TYPE_PERF_EVENT_ARRAY`
* `BPF_MAP_TYPE_ARRAY_OF_MAPS`
* `BPF_MAP_TYPE_CGROUP_ARRAY`

都是在 [`kernel/bpf/arraymap.c`](https://github.com/torvalds/linux/blob/v5.10/kernel/bpf/arraymap.c) 中实现的。

方法：

```c
// kernel/bpf/arraymap.c

const struct bpf_map_ops array_map_ops = {
    .map_alloc_check       = array_map_alloc_check,
    .map_alloc             = array_map_alloc,
    .map_free              = array_map_free,
    .map_get_next_key      = array_map_get_next_key,
    .map_lookup_elem       = array_map_lookup_elem,
    .map_update_elem       = array_map_update_elem,
    .map_delete_elem       = array_map_delete_elem,
    .map_gen_lookup        = array_map_gen_lookup,
    .map_direct_value_addr = array_map_direct_value_addr,
    .map_direct_value_meta = array_map_direct_value_meta,
    .map_mmap              = array_map_mmap,
    .map_seq_show_elem     = array_map_seq_show_elem,
    .map_check_btf         = array_map_check_btf,
    .map_lookup_batch      = generic_map_lookup_batch,
    .map_update_batch      = generic_map_update_batch,
};
```



# 1 `BPF_MAP_TYPE_ARRAY`

**<mark>相比于 hash map，array map 的实现就简单多了</mark>**，因为 **<mark>key
就是数组 index</mark>**，所以map 的增删查改都是数组操作。

## 创建 map：`struct bpf_map *array_map_alloc()`

```c
// kernel/bpf/arraymap.c

static struct bpf_map *array_map_alloc(union bpf_attr *attr)
{
    bool percpu = attr->map_type == BPF_MAP_TYPE_PERCPU_ARRAY;
    struct bpf_map_memory mem;
    struct bpf_array *array;

    elem_size = round_up(attr->value_size, 8);
    max_entries = attr->max_entries;
    array_size = sizeof(*array); // 内存要 page-aligned

    bpf_map_charge_init(&mem, cost); // 确保内存不会溢出

    /* allocate all map elements and zero-initialize them */
    if (attr->map_flags & BPF_F_MMAPABLE) {
        /* kmalloc'ed memory can't be mmap'ed, use explicit vmalloc */
        void *data = bpf_map_area_mmapable_alloc(array_size, numa_node);
        array = data + PAGE_ALIGN(sizeof(struct bpf_array)) - offsetof(struct bpf_array, value);
    } else {
        array = bpf_map_area_alloc(array_size, numa_node);
    }

    array->index_mask = index_mask;

    /* copy mandatory map attributes */
    bpf_map_init_from_attr(&array->map, attr);
    bpf_map_charge_move(&array->map.memory, &mem);
    array->elem_size = elem_size;

    return &array->map;
}
```

## 查询 map：`void *array_map_lookup_elem()`

```c
// kernel/bpf/arraymap.c

/* Called from syscall or from eBPF program */
static void *array_map_lookup_elem(struct bpf_map *map, void *key)
{
    struct bpf_array *array = container_of(map, struct bpf_array, map);
    u32 index = *(u32 *)key; // 直接将 key 转换成 index

    if (unlikely(index >= array->map.max_entries))
        return NULL;

    return array->value + array->elem_size * (index & array->index_mask);
}
```

# 2 `BPF_MAP_TYPE_PERCPU_ARRAY`


# 3 `BPF_MAP_TYPE_PROG_ARRAY`

这种类型的 array 存放的是 BPF 程序的文件描述符（fd），在尾调用时使用。

```c
// kernel/bpf/arraymap.c

const struct bpf_map_ops prog_array_map_ops = {
    .map_alloc_check = fd_array_map_alloc_check,
    .map_alloc = prog_array_map_alloc,
    .map_free = prog_array_map_free,
    .map_poke_track = prog_array_map_poke_track,
    .map_poke_untrack = prog_array_map_poke_untrack,
    .map_poke_run = prog_array_map_poke_run,
    .map_get_next_key = array_map_get_next_key,
    .map_lookup_elem = fd_array_map_lookup_elem,
    .map_delete_elem = fd_array_map_delete_elem,
    .map_fd_get_ptr = prog_fd_array_get_ptr,
    .map_fd_put_ptr = prog_fd_array_put_ptr,
    .map_fd_sys_lookup_elem = prog_fd_array_sys_lookup_elem,
    .map_release_uref = prog_array_map_clear,
    .map_seq_show_elem = prog_array_map_seq_show_elem,
};
```

<a name="bpf_map_type_perf_event_array"></a>
# 4 `BPF_MAP_TYPE_PERF_EVENT_ARRAY`


# 5 `BPF_MAP_TYPE_ARRAY_OF_MAPS`


# 6 `BPF_MAP_TYPE_CGROUP_ARRAY`

```c
// kernel/bpf/arraymap.c

const struct bpf_map_ops cgroup_array_map_ops = {
    .map_alloc_check = fd_array_map_alloc_check,
    .map_alloc = array_map_alloc,
    .map_free = cgroup_fd_array_free,
    .map_get_next_key = array_map_get_next_key,
    .map_lookup_elem = fd_array_map_lookup_elem,
    .map_delete_elem = fd_array_map_delete_elem,
    .map_fd_get_ptr = cgroup_fd_array_get_ptr,
    .map_fd_put_ptr = cgroup_fd_array_put_ptr,
    .map_check_btf = map_check_no_btf,
};
```

# ------------------------------------------------------------------------
# CGroup Maps
# ------------------------------------------------------------------------

管理 attach 到 cgroup 的 BPF 程序。

# CGroup 相关结构体

```c
// kernel/bpf/cgroup.c

const struct bpf_prog_ops cg_dev_prog_ops = {
};

const struct bpf_verifier_ops cg_dev_verifier_ops = {
    .get_func_proto     = cgroup_dev_func_proto,
    .is_valid_access    = cgroup_dev_is_valid_access,
};

const struct bpf_verifier_ops cg_sysctl_verifier_ops = {
    .get_func_proto     = sysctl_func_proto,
    .is_valid_access    = sysctl_is_valid_access,
    .convert_ctx_access = sysctl_convert_ctx_access,
};

const struct bpf_prog_ops cg_sysctl_prog_ops = {
};

const struct bpf_verifier_ops cg_sockopt_verifier_ops = {
    .get_func_proto     = cg_sockopt_func_proto,
    .is_valid_access    = cg_sockopt_is_valid_access,
    .convert_ctx_access = cg_sockopt_convert_ctx_access,
    .gen_prologue       = cg_sockopt_get_prologue,
};

const struct bpf_prog_ops cg_sockopt_prog_ops = {
};
```

# 1 `BPF_MAP_TYPE_CGROUP_ARRAY`

见上面。

# 2 `BPF_MAP_TYPE_CGROUP_STORAGE`

```
// kernel/bpf/local_storage.c

const struct bpf_map_ops cgroup_storage_map_ops = {
    .map_alloc         = cgroup_storage_map_alloc,
    .map_free          = cgroup_storage_map_free,
    .map_get_next_key  = cgroup_storage_get_next_key,
    .map_lookup_elem   = cgroup_storage_lookup_elem,
    .map_update_elem   = cgroup_storage_update_elem,
    .map_delete_elem   = cgroup_storage_delete_elem,
    .map_check_btf     = cgroup_storage_check_btf,
    .map_seq_show_elem = cgroup_storage_seq_show_elem,
};
```

## 结构体

### `struct bpf_cgroup_storage_map`：cgroup storage map 定义

与 hash map 类型，这里定义了一个新结构体来表示 cgroup storage map，这个结构体
**<mark>将标准 bpf map 嵌套到结构体中作为一个字段</mark>**：

```c
// kernel/bpf/local_storage.c

struct bpf_cgroup_storage_map {
    struct bpf_map map;            // 标准的 bpf map

    spinlock_t               lock;
    struct     bpf_prog_aux *aux;
    struct     rb_root       root;
    struct     list_head     list;
};
```

### `struct bpf_cgroup_storage_key`：key 定义

这种类型的 map，**<mark>key 定义为</mark>**：

```c
// include/uapi/linux/bpf.h

struct bpf_cgroup_storage_key {
    __u64    cgroup_inode_id;    /* cgroup inode id */
    __u32    attach_type;        /* program attach type */
};
```

### 其他结构体

```c
// include/linux/bpf-cgroup.h

struct bpf_storage_buffer {
    struct rcu_head rcu;
    char data[];
};

struct bpf_cgroup_storage {
    union {
        struct bpf_storage_buffer *buf;
        void __percpu *percpu_buf;
    };

    struct bpf_cgroup_storage_map *map;
    struct bpf_cgroup_storage_key key;

    struct list_head list;
    struct rb_node node;
    struct rcu_head rcu;
};

struct bpf_cgroup_link {
    struct bpf_link link;
    struct cgroup *cgroup;
    enum bpf_attach_type type;
};

// Attach 到每个 cgroup 的 BPF 程序组成一个链表
struct bpf_prog_list {
    struct list_head node;
    struct bpf_prog *prog;
    struct bpf_cgroup_link *link;

    struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE];
};

struct cgroup_bpf {
    // 这个 cgroup 内的有效 BPF 程序
    struct bpf_prog_array __rcu *effective[MAX_BPF_ATTACH_TYPE];

    // attach 到这个 cgroup 的 BPF 程序
    struct list_head progs[MAX_BPF_ATTACH_TYPE];
    // attach flags
    // * flags == 0 or BPF_F_ALLOW_OVERRIDE the progs list will have either zero or one element
    // * BPF_F_ALLOW_MULTI the list can have up to BPF_CGROUP_MAX_PROGS
    u32 flags[MAX_BPF_ATTACH_TYPE];

    /* temp storage for effective prog array used by prog_attach/detach */
    struct bpf_prog_array *inactive;

    /* reference counter used to detach bpf programs after cgroup removal */
    struct percpu_ref refcnt;

    /* cgroup_bpf is released using a work queue */
    struct work_struct release_work;
};
```

## 创建 map：`struct bpf_map *cgroup_storage_map_alloc(union bpf_attr *attr)`

```c
// kernel/bpf/local_storage.c

static struct bpf_map *cgroup_storage_map_alloc(union bpf_attr *attr)
{
    int numa_node = bpf_map_attr_numa_node(attr);
    struct bpf_cgroup_storage_map *map;
    struct bpf_map_memory mem;

    if (attr->max_entries) /* max_entries is not used and enforced to be 0 */
        return ERR_PTR(-EINVAL);

    bpf_map_charge_init(&mem, sizeof(struct bpf_cgroup_storage_map));

    map = kmalloc_node(sizeof(struct bpf_cgroup_storage_map), __GFP_ZERO | GFP_USER, numa_node);
    bpf_map_charge_move(&map->map.memory, &mem);

    bpf_map_init_from_attr(&map->map, attr); // 初始化（复制）map 元数据字段

    map->root = RB_ROOT;
    INIT_LIST_HEAD(&map->list);

    return &map->map;
}
```

## 初始化指定类型的 cgroup storage：`struct bpf_cgroup_storage *bpf_cgroup_storage_alloc()`

**<mark>一个 cgroup 内的所有 BPF 程序</mark>**组织成一个链表 `struct
bpf_prog_list`，这些程序**<mark>共用一组 cgroup storage</mark>**（按类型组织成数
组）。在将 BPF 程序 attach 到 cgroup 时，会有如下的调用栈：

```
__cgroup_bpf_attach
  |-bpf_cgroup_storages_alloc(storage, prog ? : link->link.prog)
      |-for_each_cgroup_storage_type(stype) {
          storages[stype] = bpf_cgroup_storage_alloc(prog, stype);
        }
```

```c
// kernel/bpf/local_storage.c

struct bpf_cgroup_storage *bpf_cgroup_storage_alloc(struct bpf_prog *prog, enum bpf_cgroup_storage_type stype)
{
    struct bpf_cgroup_storage *storage;
    struct bpf_map *map;

    map = prog->aux->cgroup_storage[stype];
    size = bpf_cgroup_storage_calculate_size(map, &pages);

    if (bpf_map_charge_memlock(map, pages))
        return ERR_PTR(-EPERM);

    storage = kmalloc_node(sizeof(struct bpf_cgroup_storage), __GFP_ZERO | GFP_USER, map->numa_node);

    flags = __GFP_ZERO | GFP_USER;

    if (stype == BPF_CGROUP_STORAGE_SHARED) {
        storage->buf = kmalloc_node(size, flags, map->numa_node);
        check_and_init_map_lock(map, storage->buf->data);
    } else {
        storage->percpu_buf = __alloc_percpu_gfp(size, 8, flags);
    }

    storage->map = (struct bpf_cgroup_storage_map *)map;
    return storage;
}
```


# 3 `BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE`

# ------------------------------------------------------------------------
# Tracing Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_STACK_TRACE`

```c
// kernel/bpf/stackmap.c

const struct bpf_map_ops stack_trace_map_ops = {
    .map_alloc = stack_map_alloc,
    .map_free = stack_map_free,
    .map_get_next_key = stack_map_get_next_key,
    .map_lookup_elem = stack_map_lookup_elem,
    .map_update_elem = stack_map_update_elem,
    .map_delete_elem = stack_map_delete_elem,
    .map_check_btf = map_check_no_btf,
};
```

# 2 `BPF_MAP_TYPE_STACK`

```c
// kernel/bpf/queue_stack_maps.c

const struct bpf_map_ops stack_map_ops = {
    .map_alloc_check = queue_stack_map_alloc_check,
    .map_alloc = queue_stack_map_alloc,
    .map_free = queue_stack_map_free,
    .map_lookup_elem = queue_stack_map_lookup_elem,
    .map_update_elem = queue_stack_map_update_elem,
    .map_delete_elem = queue_stack_map_delete_elem,
    .map_push_elem = queue_stack_map_push_elem,
    .map_pop_elem = stack_map_pop_elem,
    .map_peek_elem = stack_map_peek_elem,
    .map_get_next_key = queue_stack_map_get_next_key,
};
```

# 3 `BPF_MAP_TYPE_RINGBUF`

```c
// kernel/bpf/ringbuf.c

const struct bpf_map_ops ringbuf_map_ops = {
    .map_alloc = ringbuf_map_alloc,
    .map_free = ringbuf_map_free,
    .map_mmap = ringbuf_map_mmap,
    .map_poll = ringbuf_map_poll,
    .map_lookup_elem = ringbuf_map_lookup_elem,
    .map_update_elem = ringbuf_map_update_elem,
    .map_delete_elem = ringbuf_map_delete_elem,
    .map_get_next_key = ringbuf_map_get_next_key,
};
```

# 4 `BPF_MAP_TYPE_PERF_EVENT_ARRAY`

见 [上面](#bpf_map_type_perf_event_array)。


# ------------------------------------------------------------------------
# Socket Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_SOCKMAP`

前面的都是实现在 `kernel/bpf/` 目录下，而**<mark>这个的实现是</mark>**在 `net/core/sock_map.c`，

```c

const struct bpf_map_ops sock_map_ops = {
    .map_alloc                = sock_map_alloc,
    .map_free                 = sock_map_free,
    .map_get_next_key         = sock_map_get_next_key,
    .map_lookup_elem_sys_only = sock_map_lookup_sys,
    .map_update_elem          = sock_map_update_elem,
    .map_delete_elem          = sock_map_delete_elem,
    .map_lookup_elem          = sock_map_lookup,
    .map_release_uref         = sock_map_release_progs,
    .map_check_btf            = map_check_no_btf,
};
```


# 2 `BPF_MAP_TYPE_REUSEPORT_SOCKARRAY`

```c
// kernel/bpf/reuseport_array.c

const struct bpf_map_ops reuseport_array_ops = {
    .map_alloc_check = reuseport_array_alloc_check,
    .map_alloc = reuseport_array_alloc,
    .map_free = reuseport_array_free,
    .map_lookup_elem = reuseport_array_lookup_elem,
    .map_get_next_key = reuseport_array_get_next_key,
    .map_delete_elem = reuseport_array_delete_elem,
};
```

配合 `BPF_PROG_TYPE_SK_REUSEPORT` 类型的 BPF 程序使用，**<mark>加速 socket 查找</mark>**。


# 3 `BPF_MAP_TYPE_SK_STORAGE`



# ------------------------------------------------------------------------
# XDP Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_SOCKHASH`

前面的都是实现在 `kernel/bpf/` 目录下，而**<mark>这个的实现是</mark>**在 `net/core/sock_map.c`，

```c

const struct bpf_map_ops sock_hash_ops = {
    .map_alloc                = sock_hash_alloc,
    .map_free                 = sock_hash_free,
    .map_get_next_key         = sock_hash_get_next_key,
    .map_update_elem          = sock_hash_update_elem,
    .map_delete_elem          = sock_hash_delete_elem,
    .map_lookup_elem          = sock_hash_lookup,
    .map_lookup_elem_sys_only = sock_hash_lookup_sys,
    .map_release_uref         = sock_hash_release_progs,
    .map_check_btf            = map_check_no_btf,
};
```


# 2 `BPF_MAP_TYPE_DEVMAP`

`kernel/bpf/devmap.c`

**<mark>功能与 sockmap 类似，但用于 XDP 场景</mark>**，在 `bpf_redirect()` 时触发。


# 3 `BPF_MAP_TYPE_DEVMAP_HASH`

`kernel/bpf/devmap.c`

```c

const struct bpf_map_ops dev_map_hash_ops = {
    .map_alloc = dev_map_alloc,
    .map_free = dev_map_free,
    .map_get_next_key = dev_map_hash_get_next_key,
    .map_lookup_elem = dev_map_hash_lookup_elem,
    .map_update_elem = dev_map_hash_update_elem,
    .map_delete_elem = dev_map_hash_delete_elem,
    .map_check_btf = map_check_no_btf,
};
```

# 4 `BPF_MAP_TYPE_CPUMAP`

XDP 中**<mark>将包重定向到指定 CPU</mark>**。


# 5 `BPF_MAP_TYPE_XSKMAP`

XDP.



# ------------------------------------------------------------------------
# 其他 Maps
# ------------------------------------------------------------------------

# 1 `BPF_MAP_TYPE_QUEUE`

```c
// kernel/bpf/queue_stack_maps.c

const struct bpf_map_ops queue_map_ops = {
    .map_alloc_check = queue_stack_map_alloc_check,
    .map_alloc = queue_stack_map_alloc,
    .map_free = queue_stack_map_free,
    .map_lookup_elem = queue_stack_map_lookup_elem,
    .map_update_elem = queue_stack_map_update_elem,
    .map_delete_elem = queue_stack_map_delete_elem,
    .map_push_elem = queue_stack_map_push_elem,
    .map_pop_elem = queue_map_pop_elem,
    .map_peek_elem = queue_map_peek_elem,
    .map_get_next_key = queue_stack_map_get_next_key,
};
```


# 2 `BPF_MAP_TYPE_STRUCT_OPS`


# 3 `BPF_MAP_TYPE_LPM_TRIE`

```c
// kernel/bpf/lpm_trie.c

const struct bpf_map_ops trie_map_ops = {
    .map_alloc = trie_alloc,
    .map_free = trie_free,
    .map_get_next_key = trie_get_next_key,
    .map_lookup_elem = trie_lookup_elem,
    .map_update_elem = trie_update_elem,
    .map_delete_elem = trie_delete_elem,
    .map_check_btf = trie_check_btf,
};
```

