---
layout    : post
title     : "[译] BPF ring buffer：使用场景、核心设计及程序示例（2020）"
date      : 2022-04-25
lastupdate: 2022-04-25
categories: bpf
---

### 译者序

本文翻译自 BPF 核心开发者 Andrii Nakryiko 2020 的一篇文章：[BPF ring buffer](https://nakryiko.com/posts/bpf-ringbuf/)。

文章介绍了 BPF ring buffer 解决的问题及背后的设计，并给出了一些代码示例和内核
patch 链接，深度和广度兼备，是学习 ring buffer 的极佳参考。

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

很多场景下，BPF 程序都需要**<mark>将数据发送到用户空间</mark>**（userspace），
BPF perf buffer（perfbuf）是目前这一过程的事实标准，但它存在一些问题，例如
**<mark>浪费内存（因为其 per-CPU 设计）、事件顺序无法保证</mark>**等。

作为改进，**<mark>内核 5.8</mark>** 引入另一个新的 BPF 数据结构：BPF ring buffer（环形缓冲区，ringbuf），

* 相比 perf buffer，它**<mark>内存效率更高、保证事件顺序</mark>**，性能也不输前者；
* 在使用上，既提供了与 perf buffer 类似的 API ，以方便用户迁移；又提供了一套新的
  **<mark>reserve/commit API</mark>**（先预留再提交），以实现**<mark>更高性能</mark>**。

此外，实验与真实环境的压测结果都表明，从 BPF 程序发送数据给用户空间时，
**<mark>应该首选 BPF ring buffer</mark>**。

# 1 ringbuf 相比 perfbuf 的改进

**<mark>perfbuf 是 per-CPU 环形缓冲区</mark>**（circular buffers），能实现高效的
**<mark>“内核-用户空间”数据交互</mark>**，在实际中也非常有用，但 per-CPU 的设计
导致两个严重缺陷：

1. **<mark>内存使用效率低下</mark>**（inefficient use of memory）
2. **<mark>事件顺序无法保证</mark>**（event re-ordering）

因此内核 5.8 引入了 ringbuf 来解决这个问题。
**<mark>ringbuf 是一个“多生产者、单消费者”</mark>**（multi-producer, single-consumer，MPSC）
队列，可**<mark>安全地在多个 CPU 之间共享和操作</mark>**。perfbuf 支持的一些功能它都支持，包括，

1. 可变长数据（variable-length data records）；
1. 通过 memory-mapped region 来高效地从 userspace 读数据，避免内存复制或系统调用；
1. 支持 epoll notifications 和 busy-loop 两种获取数据方式。

此外，它还解决了 perfbuf 的下列问题：

1. 内存开销（memory overhead）；
1. 数据乱序；
1. 无效的处理逻辑和不必要的数据复制（extra data copying）。

下面具体来看。

## 1.1 降低内存开销（memory overhead）

perfbuf 为每个 CPU 分配一个独立的缓冲区，这意味着开发者通常需要
**<mark>在内存效率和数据丢失之间做出折中</mark>**：

1. 越大的 per-CPU buffer 越能避免丢数据，但也意味着大部分时间里，大部分内存都是浪费的；
2. 尽量小的 per-CPU buffer 能提高内存使用效率，但在数据量陡增（毛刺）时将导致丢数据。

对于那些大部分时间都比较空闲、**<mark>周期性来一大波数据的场景</mark>**，
这个问题尤其突出，很难在两者之间取得一个很好的平衡。

ringbuf 的解决方式是**<mark>分配一个所有 CPU 共享的大缓冲区</mark>**，

* “大缓冲区”意味着能**<mark>更好地容忍数据量毛刺</mark>**
* “共享”则意味着**<mark>内存使用效率更高</mark>**

另外，ringbuf **<mark>内存效率的扩展性</mark>**也更好，比如 CPU 数量从 16 增加到 32 时，

* perfbuf 的总 buffer 会跟着翻倍，因为它是 per-CPU buffer；
* ringbuf 的总 buffer 不一定需要翻倍，就足以处理扩容之后的数据量。

## 1.2 保证事件顺序（event ordering）

如果 BPF 应用要跟踪一系列关联事件（correlated events），例如进程的启动和终止、
网络连接的生命周期事件等，那**<mark>保持事件的顺序</mark>**就非常关键。
perfbuf 在这种场景下有一些问题：如果这些事件发生的间隔非常短（几毫秒）并且分散
在不同 CPU 上，那事件的发送顺序可能就会乱掉 ——这同样是 **<mark>perbuf 的 per-CPU 特性决定的</mark>**。

举个真实例子，几年前我写的一个应用需要跟踪进程
fork/exec/exit 事件，收集进程级别（per-process）的资源使用量。BPF 程序将这些事件
写入 perfbuf，但它们到达的顺序经常乱掉。这是因为内核调度器在不同 CPU 上调度进程时，
对于那些存活时间很短的进程，fork(), exec(), and exit() 会在极短的时间内在不同 CPU 上执行。
这里的问题很清楚，但要解决这个问题，就需要在应用逻辑中加入大量的判断和处理，
只有亲自做过才知道有多复杂。

但对于 ringbuf 来说，这根本不是问题，因为它是共享的同一个缓冲区。ringbuf 保证
**<mark>如果事件 A 发生在事件 B 之前，那 A 一定会先于 B 被提交，也会在 B 之前被消费</mark>**。
这个特性显著简化了应用处理逻辑。

## 1.3 减少数据复制（wasted data copy）

BPF 程序使用 perfbuf 时，必须先初始化一份事件数据，然后将它复制到 perfbuf，
然后才能发送到用户空间。这意味着**<mark>数据会被复制两次</mark>**：

* 第一次：复制到一个**<mark>局部变量</mark>**（a local variable）或 per-CPU array
  （BPF 的栈空间很小，因此较大的变量无法放到栈上，后面有例子）中；
* 第二次：复制到 **<mark>perfbuf</mark>** 中。

更糟糕的是，如果 perfbuf 已经没有足够空间放数据了，那第一步的复制完全是浪费的。

BPF ringbuf 提供了一个可选的 reservation/submit API 来避免这种问题。

* 首先申请为数据**<mark>预留空间</mark>**（reserve the space），
* 预留成功后，

    * 应用就可以直接将准备发送的数据放到 ringbuf 了，从而**<mark>节省了 perfbuf 中的第一次复制</mark>**，
    * **<mark>将数据提交到用户空间</mark>**将是一件**<mark>极其高效、不会失败</mark>**的操作，也不涉及任何额外的内存复制。

* 如果因为 buffer 没有空间而预留失败了，那 BPF 程序马上就能知道，从而也不用再
  执行 perfbuf 中的第一步复制。

后面会有具体例子。

# 2 ringbuf 使用场景和性能

## 2.1 常规场景

**<mark>对于所有实际场景</mark>**（尤其是那些基于 bcc/libbpf 的默认配置在使用 perfbuf 的场景），
**<mark>ringbuf 的性能都优于 perfbuf 性能</mark>**。各种不同场景的仿真压测（synthetic benchmarking）
结果见内核 <a href="https://patchwork.ozlabs.org/project/netdev/patch/20200529075424.3139988-5-andriin@fb.com/">patch</a>。

## 2.2 高吞吐场景

Per-CPU buffer 特性的 **<mark>perfbuf 在理论上能支持更高的数据吞吐</mark>**，
但这只有在**<mark>每秒百万级事件</mark>**（millions of events per second）的场景下才会显现。

在编写了一个真实场景的高吞吐应用之后，我们证实了 ringbuf 在作为与 perfbuf 类似的 per-CPU buffer
使用时，仍然可以作为 perfbuf 的一个高性能替代品，尤其是用到手动管理事件通知（manual data availability notification）机制时。

基本的 multi-ringbuf example 见内核 selftests：

* <a href="https://github.com/torvalds/linux/blob/v5.10/tools/testing/selftests/bpf/progs/test_ringbuf_multi.c">BPF side</a>
* <a href="https://github.com/torvalds/linux/blob/v5.10/tools/testing/selftests/bpf/prog_tests/ringbuf_multi.c">user-space side</a>

## 2.3 不可掩码中断（non-maskable interrupt）场景

**<mark>唯一需要注意、最好先试验一下的场景</mark>**：BPF 程序必须在
NMI (non-maskable interrupt) context 中执行时，例如处理 `cpu-cycles` 等 perf events 时。

ringbuf 内部使用了一个**<mark>非常轻量级的 spin-lock</mark>**，这意味着如果 NMI
context 中有竞争，data reservation 可能会失败。
因此，在 NMI context 中，如果 CPU 竞争非常严重，可能会
**<mark>导致丢数据，虽然此时 ringbuf 仍然有可用空间</mark>**。

## 2.4 小结

除了 NMI context 之外，在其他所有场景中优先选择 ringbuf 而不是 perfbuf 都是非常明智的。

# 3 示例程序（show me the code）

完整代码见 <a href="https://github.com/anakryiko/bpf-ringbuf-examples">bpf-ringbuf-examples project</a>。

BPF 程序的功能是 trace 所有进程的 `exec()` 操作，也就是创建新进程事件。

* 每次 `exec()` 事件：收集进程 ID (`pid`)、进程名字 (`comm`)、可执行文件路径 (`filename`)，然后发送给用户空间程序；
* 用户空间简单通过 `printf()` 打印输出。

用三种不同方式实现，输出都类似：

```shell
$ sudo ./ringbuf-reserve-commit    # or ./ringbuf-output, or ./perfbuf-output
TIME     EVENT PID     COMM             FILENAME
19:17:39 EXEC  3232062 sh               /bin/sh
19:17:39 EXEC  3232062 timeout          /usr/bin/timeout
19:17:39 EXEC  3232063 ipmitool         /usr/bin/ipmitool
19:17:39 EXEC  3232065 env              /usr/bin/env
19:17:39 EXEC  3232066 env              /usr/bin/env
19:17:39 EXEC  3232065 timeout          /bin/timeout
19:17:39 EXEC  3232066 timeout          /bin/timeout
19:17:39 EXEC  3232067 sh               /bin/sh
19:17:39 EXEC  3232068 sh               /bin/sh
^C
```

事件的结构体定义：

```c
#define TASK_COMM_LEN 16
#define MAX_FILENAME_LEN 512

// BPF 程序发送给 userspace 的事件
struct event {
    int pid;
    char comm[TASK_COMM_LEN];
    char filename[MAX_FILENAME_LEN];
};
```

这里有意让这个结构体的**<mark>大小超过 512 字节</mark>**，这样 event 变量就无法
放到 BPF 栈空间（max 512Byte）上，后面会看到 perfbuf 和 ringbuf 程序分别怎么处理。

## 3.1 perfbuf 示例

### 内核 BPF 程序

```c
// 声明一个 perfbuf map。几点注意：
// 1. 不用特意设置 max_entries，libbpf 会自动将其设置为 CPU 数量；
// 2. 这个 map 的 per-CPU buffer 大小是 userspace 设置的，后面会看到
struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY); // perf buffer (array)
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} pb SEC(".maps");

// 一个 struct event 变量的大小超过了 512 字节，无法放到 BPF 栈上，
// 因此声明一个 size=1 的 per-CPU array 来存放 event 变量
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);    // per-cpu array
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct event);
} heap SEC(".maps");

SEC("tp/sched/sched_process_exec")
int handle_exec(struct trace_event_raw_sched_process_exec *ctx)
{
	unsigned fname_off = ctx->__data_loc_filename & 0xFFFF;
	struct event *e;
	int zero = 0;
	
	e = bpf_map_lookup_elem(&heap, &zero);
	if (!e) /* can't happen */
		return 0;

	e->pid = bpf_get_current_pid_tgid() >> 32;
	bpf_get_current_comm(&e->comm, sizeof(e->comm));
	bpf_probe_read_str(&e->filename, sizeof(e->filename), (void *)ctx + fname_off);

	// 发送事件，参数列表 <context, &perfbuf, flag, event, sizeof(event)>
	bpf_perf_event_output(ctx, &pb, BPF_F_CURRENT_CPU, e, sizeof(*e));
	return 0;
}
```

### 用户空间程序

完整代码 <a href="https://github.com/anakryiko/bpf-ringbuf-examples/blob/main/src/perfbuf-output.c">the user-space side</a>，
基于 BPF skeleton（更多信息见
<a href="https://nakryiko.com/posts/bcc-to-libbpf-howto-guide/#bpf-skeleton-and-bpf-app-lifecycle">这里</a>）。

看一个关键点：使用 libbpf user-space `perf_buffer__new()` API 来创建一个 perf buffer consumer：

```c
	struct perf_buffer *pb = NULL;
	struct perf_buffer_opts pb_opts = {};
	struct perfbuf_output_bpf *skel;

	/* Set up ring buffer polling */
	pb_opts.sample_cb = handle_event;
	pb = perf_buffer__new(bpf_map__fd(skel->maps.pb), 8 /* 32KB per CPU */, &pb_opts);
```

这里**<mark>设置 per-CPU buffer 为 32KB</mark>**，
注意其中的 8 表示的是 number of memory pages，每个 page 是 4KB，因此总大小：
**<mark><code>8 pages x 4096 byte/page = 32KB</code></mark>**。

## 3.2 ringbuf 示例

完整代码：

* <a href="https://github.com/anakryiko/bpf-ringbuf-examples/blob/main/src/ringbuf-output.bpf.c">BPF-side code</a>
* <a href="https://github.com/anakryiko/bpf-ringbuf-examples/blob/main/src/ringbuf-output.c">user-space code</a>

### 内核 BPF 程序

`bpf_ringbuf_output()` 在设计上遵循了 `bpf_perf_event_output()` 的语义，
以使应用从 perfbuf 迁移到 ringbuf 时更容易。为了看出二者有多相似，这里展示下
两个示例代码的 diff。

```diff
--- src/perfbuf-output.bpf.c	2020-10-25 18:52:22.247019800 -0700
+++ src/ringbuf-output.bpf.c	2020-10-25 18:44:14.510630322 -0700
@@ -6,12 +6,11 @@
 
 char LICENSE[] SEC("license") = "Dual BSD/GPL";
 
-/* BPF perfbuf map */
+/* BPF ringbuf map */
 struct {
-	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
-	__uint(key_size, sizeof(int));
-	__uint(value_size, sizeof(int));
-} pb SEC(".maps");
+	__uint(type, BPF_MAP_TYPE_RINGBUF);
+	__uint(max_entries, 256 * 1024 /* 256 KB */);
+} rb SEC(".maps");
 
 struct {
 	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
@@ -35,7 +34,7 @@
 	bpf_get_current_comm(&e->comm, sizeof(e->comm));
 	bpf_probe_read_str(&e->filename, sizeof(e->filename), (void *)ctx + fname_off);
 
-	bpf_perf_event_output(ctx, &pb, BPF_F_CURRENT_CPU, e, sizeof(*e));
+	bpf_ringbuf_output(&rb, e, sizeof(*e), 0);
 	return 0;
 }
```

只有两个小改动：

1. **<mark>ringbuf map 的大小（max_entries）可以在 BPF 侧指定了</mark>**，注意这是所有 CPU 共享的大小。

    * 在 userspace 侧来设置（或 override） max_entries 也是可以的，API 是 `bpf_map__set_max_entries()`；
    * `max_entries` 的单位是字节，必须是**<mark>内核页大小</mark>**（
      几乎永远是 4096）**<mark>的倍数，也必须是 2 的幂次</mark>**。

1. `bpf_perf_event_output()` 替换成了类似的 `bpf_ringbuf_output()`，后者更简单，不需要 BPF context 参数。

### 用户空间程序

事件 handler 签名有点变化：

1. 会返回错误信息（进而终止 consumer 循环）
2. 参数里面去掉了产生这个事件的 CPU Index

```diff
-void handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
+int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct event *e = data;
	struct tm *tm;
```

如果 CPU index 对你很重要，那你需要自己在 BPF 代码中记录它。

另外，`ring_buffer` API 不提供丢失数据（lost samples）的回调函数，而 `perf_buffer` 是支持的。
如果需要这个功能，必须自己在 BPF 代码中处理。
这样的设计对于一个（所有 CPU）共享的 ring buffer 能**<mark>最小化锁竞争</mark>**，
同时也避免了为不需要的功能买单：在实际中，这功能除了能用户在 userspace 打印出有数据丢失之外，其他基本也做不了什么，
而类似的目的在 BPF 中可以更显式和高效地完成。

第二个不同是 `ring_buffer__new()` API 更加简洁：

```diff
 	/* Set up ring buffer polling */
-	pb_opts.sample_cb = handle_event;
-	pb = perf_buffer__new(bpf_map__fd(skel->maps.pb), 8 /* 32KB per CPU */, &pb_opts);
-	if (libbpf_get_error(pb)) {
+	rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, NULL, NULL);
+	if (!rb) {
 		err = -1;
-		fprintf(stderr, "Failed to create perf buffer\n");
+		fprintf(stderr, "Failed to create ring buffer\n");
 		goto cleanup;
 	}
```

接下来基本上就是文本替换一下的事情了：
`perf_buffer__poll()` -> `ring_buffer__poll()`


```diff
 	printf("%-8s %-5s %-7s %-16s %s\n",
 	       "TIME", "EVENT", "PID", "COMM", "FILENAME");
 	while (!exiting) {
-		err = perf_buffer__poll(pb, 100 /* timeout, ms */);
+		err = ring_buffer__poll(rb, 100 /* timeout, ms */);
 		/* Ctrl-C will cause -EINTR */
 		if (err == -EINTR) {
 			err = 0;
 			break;
 		}
 		if (err < 0) {
-			printf("Error polling perf buffer: %d\n", err);
+			printf("Error polling ring buffer: %d\n", err);
 			break;
 		}
 	}
```

## 3.3 ringbuf reserve/commit API 示例

`bpf_ringbuf_output()` API 的目的是确保从 perfbuf 到 ringbuf 迁移时无需对 BPF 代
码做重大改动，但这也意味着它继承了 perfbuf API 的一些缺点：

1. 额外的内存复制（extra memory copy）

    这意味着需要额外的空间来构建 event 变量，然后将其复制到 buffer。不仅低效，
    而且经常需要引入只有一个元素的 per-CPU array，增加了不必要的处理复杂性。

2. 非常晚的 buffer 空间申请（data reservation）

    如果这一步失败了（例如由于用户空间消费不及时导致 buffer 满了，或者有大量
    突发事件导致 buffer 溢出了），那上一步的工作将变得完全无效，浪费内存空间和计算资源。

### 原理

如果**<mark>能提前知道事件将在第二步被丢弃，就无需做第一步了</mark>**，
节省一些内存和计算资源，消费端反而因此而消费地更快一些。
但 `xxx_output()` 风格的 API 是无法实现这个目的的。

这就是为什么引入了新的 `bpf_ringbuf_reserve()`/`bpf_ringbuf_commit()` API。

* 提前预留空间，或者能立即发现没有可以空间了（返回 `NULL`）；
* 预留成功后，一旦数据写好了，将它发送到 userspace 是一个不会失败的操作。

    也就是说只要 `bpf_ringbuf_reserve()` 返回非空，那随后的 `bpf_ringbuf_commit()`
    就永远会成功，因此它没有返回值。

另外，ring buffer 中**<mark>预留的空间在被提交之前，用户空间是看不到的</mark>**，
因此 BPF 程序可以从容地组织自己的 event 数据，不管它有多复杂、需要多少步骤。
这种方式也避免了额外的内存复制和临时存储空间（extra memory copying and temporary storage spaces）。

### 限制

唯一的限制是：**<mark>BPF 校验器在校验时</mark>**（at verification time），
**<mark>必须知道预留数据的大小</mark>**
（size of the reservation），因此不支持动态大小的事件数据。

* 对于动态大小的数据，用户只能退回到用 `bpf_ringbuf_output()` 方式来提交，忍受额外的数据复制开销；
* 其他所有情况下，reserve/commit API 都应该是首选。

### 内核 BPF 程序

* <a href="https://github.com/anakryiko/bpf-ringbuf-examples/blob/main/src/ringbuf-reserve-submit.bpf.c">BPF</a>
* <a href="https://github.com/anakryiko/bpf-ringbuf-examples/blob/main/src/ringbuf-reserve-submit.c">user-space</a>

```diff
--- src/ringbuf-output.bpf.c	2020-10-25 18:44:14.510630322 -0700
+++ src/ringbuf-reserve-submit.bpf.c	2020-10-25 18:36:53.409470270 -0700
@@ -12,29 +12,21 @@
 	__uint(max_entries, 256 * 1024 /* 256 KB */);
 } rb SEC(".maps");
 
-struct {
-	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
-	__uint(max_entries, 1);
-	__type(key, int);
-	__type(value, struct event);
-} heap SEC(".maps");
-
 SEC("tp/sched/sched_process_exec")
 int handle_exec(struct trace_event_raw_sched_process_exec *ctx)
 {
 	unsigned fname_off = ctx->__data_loc_filename & 0xFFFF;
 	struct event *e;
-	int zero = 0;
 	
-	e = bpf_map_lookup_elem(&heap, &zero);
-	if (!e) /* can't happen */
+	e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
+	if (!e)
 		return 0;
 
 	e->pid = bpf_get_current_pid_tgid() >> 32;
 	bpf_get_current_comm(&e->comm, sizeof(e->comm));
 	bpf_probe_read_str(&e->filename, sizeof(e->filename), (void *)ctx + fname_off);
 
-	bpf_ringbuf_output(&rb, e, sizeof(*e), 0);
+	bpf_ringbuf_submit(e, 0);
 	return 0;
 }
```

### 用户空间程序

用户空间代码与之前的 ringbuf output API 完全一样，因为这个 API 涉及到的只是提交方（生产方），
消费方还是一样的方式来消费。

# 4 ringbuf 事件通知控制

## 4.1 事件通知开销

在高吞吐场景中，最大的性能损失经常来自提交数据时，**<mark>内核的信号通知开销</mark>**（in-kernel signalling of data availability）
，也就是内核的 poll/epoll 通知阻塞在读数据上的 userspace handler 接收数据。

**<mark>这一点对 perfbuf 和 ringbuf 都是一样的</mark>**。

## 4.2 perbuf 解决方式

perfbuf 处理这种场景的方式是提供了一个**<mark>采样通知（sampled notification）机制</mark>**：
每 N 个事件才会发送一次通知。用户空间创建 perfbuf 时可以指定这个参数。

这种机制能否解决问题，因具体场景而异。

## 4.3 ringbuf 解决方式

ringbuf 选了一条不同的路：`bpf_ringbuf_output()` 和 `bpf_ringbuf_commit()`
都**<mark>支持一个额外的 flags 参数</mark>**，

* `BPF_RB_NO_WAKEUP`：不触发通知
* `BPF_RB_FORCE_WAKEUP`：会触发通知

基于这个 flags，用户能实现**<mark>更加精确的通知控制</mark>**。例子见
<a href="https://github.com/torvalds/linux/blob/master/tools/testing/selftests/bpf/progs/ringbuf_bench.c#L22-L31">BPF ringbuf
benchmark</a>。

默认情况下，如果没指定任何 flag，ringbuf 会采用**<mark>自适应通知</mark>**
（adaptive notification）机制，根据 userspace 消费者是否有滞后（lagging）来动态
调整通知间隔，尽量确保 userspace 消费者既不用承担额外开销，又不丢失任何数据。
这种默认配置在大部分场景下都是有效和安全的，但如果想获得极致性能，那
显式控制数据通知就是有必要的，需要结合具体应用场景和处理逻辑来设计。

# 5 总结

本文介绍了 BPF ring buffer 解决的问题及其背后的设计。

文中给出的示例代码和内核代码链接，展示了 ringbuf API 的基础和高级用法。
希望阅读本文之后，读者能对 ringbuf 有一个很好的理解和把握，能根据自己的具体应用
选择合适的 API 来使用。

# 其他相关资料（译注）

1. 内核文档，[BPF ring buffer](https://www.kernel.org/doc/html/latest/bpf/ringbuf.html)

    有一些更细节的设计与实现，可作为本文补充。
