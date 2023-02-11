---
layout    : post
title     : "Linux tracing/profiling 基础：符号表、调用栈、perf/bpftrace 示例等（2022）"
date      : 2022-07-18
lastupdate: 2023-02-11
categories: bpf perf
---

整理一些 tracing/profiling 笔记，目前大部分内容都来自
[Practical Linux tracing](https://medium.com/coccoc-engineering-blog/things-you-should-know-to-begin-playing-with-linux-tracing-tools-part-i-x-225aae1aaf13)
系列文章。

----

* TOC
{:toc}

----

# 1 引言

## 1.1 热点与调用栈分析（`perf record/report/script`）

### 1.1.1 采样：`perf record`

`perf` 能够跟踪记录内核及应用程序的执行状态，

```shell
$ perf record -a -g -- sleep 5
[ perf record: Woken up 10 times to write data ]
[ perf record: Captured and wrote 4.636 MB perf.data (24700 samples) ]
```

生成的信息保存在 `perf.data` 中，然后通过 perf report/script，就可以分析性能和调用栈。

### 1.1.2 查看函数 CPU 占用量：`perf report`

`perf report` 查看看**<mark>哪些函数占用的 CPU 最多</mark>**：

```shell
$ perf report
Samples: 24K of event 'cycles', Event count (approx.): 4868947877
  Children      Self  Command   Shared Object        Symbol
+   17.08%     0.23%  swapper   [kernel.kallsyms]    [k] do_idle
+    5.38%     5.38%  swapper   [kernel.kallsyms]    [k] intel_idle
+    4.21%     0.02%  kubelet   [kernel.kallsyms]    [k] entry_SYSCALL_64_after_hwframe
+    4.08%     0.00%  kubelet   kubelet              [.] k8s.io/kubernetes/vendor/github.com/google/...
+    4.06%     0.00%  dockerd   dockerd              [.] net/http.(*conn).serve
+    3.96%     0.00%  dockerd   dockerd              [.] net/http.serverHandler.ServeHTTP
...
```

这是一个交互式的窗口，可以选中具体函数展开查看详情。

### 1.1.3 打印调用栈：`perf script`

展示采集到的事件及其**<mark>调用栈</mark>**：

```shell
$ perf script
perf 44564 [000] 743873.947847:          1   cycles:
        ffffffffa786af46 native_write_msr+0x6 ([kernel.kallsyms])
        ffffffffa780d92f __intel_pmu_enable_all.constprop.0+0x3f ([kernel.kallsyms])
        ffffffffa79fb3a9 event_function+0x89 ([kernel.kallsyms])
        ffffffffa79f48ee remote_function+0x3e ([kernel.kallsyms])
        ffffffffa7933199 generic_exec_single+0x59 ([kernel.kallsyms])
        ffffffffa79332ac smp_call_function_single+0xdc ([kernel.kallsyms])
        ...
```

### 1.1.4 生成火焰图：`perf script | ... > result.svg`

将 `perf script` 的输出重定向到 perl 脚本做进一步处理，就得到了著名的**<mark>火焰图</mark>**：


```shell
$ perf script | ./stackcollapse-perf.pl | ./flamegraph.pl > result.svg
```

以上这些都是基于 tracing 功能。

## 1.2 符号（symbols）

Tracing 功能的基础是符号（symbols），即**<mark>目标文件中的函数信息</mark>**。
Symbols 对 kprobe/uprobe event tracing 至关重要，因为知道函数名字才能跟踪。来看两个例子：

### 1.2.1 查看 object/binary file 中有哪些符号

查看 `grep` 这个最常用的命令（可执行文件）中包含哪些符号：

```shell
$ readelf -s `which grep`
Symbol table '.dynsym' contains 137 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __ctype_toupper_loc@GLIBC_2.3 (2)
     2: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __uflow@GLIBC_2.2.5 (3)
     3: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND getenv@GLIBC_2.2.5 (3)
     ...
```

### 1.2.2 查看内核符号表

```shell
$ cat /proc/kallsyms | egrep ' (t|T) ' | head
0000000000000000 T startup_64
0000000000000000 T secondary_startup_64
0000000000000000 t verify_cpu
0000000000000000 T sev_verify_cbit
0000000000000000 T start_cpu0
0000000000000000 T __startup_64
...
```

### 1.2.3 小结

以上看出，**<mark>符号可以位于目标文件中，也可以存放在单独的文件</mark>**。

## 1.3 小结

**<mark>Symbols 与 gcc -g 产生的 debug info 并不是一个东西</mark>**。
下面我们通过一个简单例子来看一下。

# 2 极简程序 `hello-world`：探究符号

## 2.1 C 源码

C 程序 `hello-world.c`：

```c
#include <stdio.h>
#include <unistd.h>
void hello() {
    printf("Hello, world!\n");
    sleep(1);
}

int main() {
    hello();
}
```

## 2.2 编译成目标文件（不带 `-g`）

```shell
$ gcc hello-world.c -o hello-world
```

用 `file` 查看目标文件信息：

```shell
$ file hello-world
hello-world: ELF 64-bit LSB executable, x86-64, version 1 (SYSV), dynamically linked (uses shared libs), not stripped
```

可以看到是可执行文件，后面会解释 `not stripped` 是什么意思。

## 2.3 查看目标文件（`objdump/readelf`）

用 `objdump` 查看可执行文件（目标文件） `hello-world` 中的各 section：

```shell
# -h/--headers
$ objdump -h hello
hello-world:     file format elf64-x86-64

Sections:
Idx Name          Size      VMA               LMA               File off  Algn
  0 .interp       0000001c  0000000000400238  0000000000400238  00000238  2**0
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  4 .dynsym       00000078  00000000004002b8  00000000004002b8  000002b8  2**3
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
 ...
 24 .data         00000004  0000000000601030  0000000000601030  00001030  2**0
                  CONTENTS, ALLOC, LOAD, DATA
 25 .bss          00000004  0000000000601034  0000000000601034  00001034  2**0
                  ALLOC
 26 .comment      0000002d  0000000000000000  0000000000000000  00001034  2**0
                  CONTENTS, READONLY
```

**<mark>确认其中并没有</mark>** debug section：

```shell
$ objdump -h hello | grep debug
#<nothing found>
```

用 `readelf -s` 读取 symbols，确认其中有我们定义的 `hello()` 方法：

```shell
# -s/--symbols
$ readelf -s hello-world | fgrep hello

Symbol table '.dynsym' contains 5 entries:  # 动态符号表
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND puts@GLIBC_2.2.5 (2)
     2: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __libc_start_main@GLIBC_2.2.5 (2)
     3: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND __gmon_start__
     4: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND sleep@GLIBC_2.2.5 (2)

Symbol table '.symtab' contains 66 entries: # 局部（local）符号表
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000400238     0 SECTION LOCAL  DEFAULT    1
    ...
    53: 000000000040055d    26 FUNC    GLOBAL DEFAULT   14 hello  # hello() 函数
    ...
    62: 0000000000400577    16 FUNC    GLOBAL DEFAULT   14 main
    64: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND sleep@@GLIBC_2.2.5
    65: 0000000000400400     0 FUNC    GLOBAL DEFAULT   11 _init
```

## 2.4 用 bpftrace 跟踪 hello-world 程序执行

执行 hello-world 程序，
用 bpftrace 来跟踪这个方法，注意这是用户空间函数，因此用 uprobe，

```shell
$ bpftrace -e 'uprobe:./hello-world:hello {printf("%s",ustack)}' -c ./hello-world
Attaching 1 probe...
hello world!

        hello+0
        __libc_start_main+245
```

## 2.5 用 bpftrace 跟踪容器方式部署的应用（container process）

如果应用程序跑在容器内，在宿主机用 bpftrace 跟踪时，需要一些额外信息 [2]。

### 2.5.1 指定目标文件的绝对路径

目标文件在宿主机上的绝对路径。

例如，如果想跟踪 cilium-agent 进程（本身是用 docker 容器部署的），首先需要找到 `cilium-agent`
文件在宿主机上的绝对路径，可以通过 container ID 或 name 找，

{% raw%}
```shell
# Check cilium-agent container
$ docker ps | grep cilium-agent
0eb2e76384b3        cilium:test   "/usr/bin/cilium-agent ..."   4 hours ago    Up 4 hours   cilium-agent

# Find the merged path for cilium-agent container
$ docker inspect --format "{{.GraphDriver.Data.MergedDir}}" 0eb2e76384b3
/var/lib/docker/overlay2/a17f868d/merged # a17f868d.. is shortened for better viewing

# The object file we are going to trace
$ ls -ahl /var/lib/docker/overlay2/a17f868d/merged/usr/bin/cilium-agent
-rwxr-xr-x 1 root root 86M /var/lib/docker/overlay2/a17f868d/merged/usr/bin/cilium-agent
```
{% endraw%}

也可以暴力一点直接 `find`：

```shell
(node) $ find /var/lib/docker/overlay2/ -name cilium-agent
/var/lib/docker/overlay2/a17f868d/merged/usr/bin/cilium-agent
```

然后再指定绝对路径 uprobe：

```shell
(node) $ bpftrace -e 'uprobe:/var/lib/docker/overlay2/a17f868d/merged/usr/bin/cilium-agent:"github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerate" {printf("%s\n", ustack); }'
Attaching 1 probe...

        github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerate+0
        github.com/cilium/cilium/pkg/eventqueue.(*EventQueue).run.func1+363
        sync.(*Once).doSlow+236
        github.com/cilium/cilium/pkg/eventqueue.(*EventQueue).run+101
        runtime.goexit+1
```

其中可 tracing 的符号（函数）列表：

```shell
$ nm cilium-agent
000000000427d1d0 B bufio.ErrBufferFull
000000000427d1e0 B bufio.ErrFinalToken
0000000001d3e940 T type..hash.github.com/cilium/cilium/pkg/k8s.ServiceID
0000000001f32300 T type..hash.github.com/cilium/cilium/pkg/node/types.Identity
0000000001d05620 T type..hash.github.com/cilium/cilium/pkg/policy/api.FQDNSelector
0000000001d05e80 T type..hash.github.com/cilium/cilium/pkg/policy.PortProto
...
```

### 2.5.2 指定目标进程 PID `/proc/<PID>`

{% raw%}
```shell
$ sudo docker inspect -f '{{.State.Pid}}' cilium-agent
109997

(node) $ bpftrace -e 'uprobe:/proc/109997/root/usr/bin/cilium-agent:"github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerate" {printf("%s\n", ustack); }'
```
{% endraw%}

### 2.5.3 指定目标进程 PID `-p <PID>`

```shell
(node) $ bpftrace -p 109997 -e 'uprobe:/usr/bin/cilium-agent:"github.com/cilium/cilium/pkg/endpoint.(*Endpoint).regenerate" {printf("%s\n", ustack); }'
```

## 2.6 小结

以上 hello-world 例子可以看出，**<mark>function tracing 只需要 symbols</mark>**，不需要 debug symbols（`gcc -g`）。
那 debug info 有什么用呢？在回答这个问题之前，我们先更深入了解下常规 symbols。

# 3 符号

## 3.1 动态符号（`.dynsym`）vs. 局部符号（`.symtab`）

Symbols 有两种，都是 `readelf -s` 输出中的 `FUNC` 类型，

* **<mark><code>.dynsym</code></mark>**：动态符号，可以被其他程序使用；
* **<mark><code>.symtab</code></mark>**：“局部”符号，只能被该可执行程序自己使用。

## 3.2 stripped vs. not stripped

通常情况下，生成可执行文件时，“局部”符号会被去掉，（以减小 binary size），
然后通过单独的 **<mark><code>xx-dbg/xx-dbgsym</code></mark>** 包来提供这些符号
（也就是放到独立的文件，按需下载和使用）。

先看个正常的，

```
$ readelf -s hello-world | grep "Symbol table"
Symbol table '.dynsym' contains 8 entries:
Symbol table '.symtab' contains 67 entries:
```

两个符号表里面的函数都可以跟踪。再看 nginx，就去掉了 local：

```shell
$ readelf -s `which nginx` | grep 'Symbol table'
Symbol table '.dynsym' contains 1077 entries: # 只能跟踪这里面的 FUNC 了
```

### 3.2.1 手动去掉局部符号（`strip -s`）

可以用命令 `strip` 来**<mark>手动去掉局部符号表</mark>**：

```shell
$ strip -s ./hello-world # 原地 strip，直接修改可执行文件
$ readelf -s hello-world | grep "Symbol table"
Symbol table '.dynsym' contains 8 entries:
```

如果对比 strip 前面的文件类型变化：

```shell
$ file hello # strip 之前的可执行文件
hello: ELF 64-bit LSB executable, x86-64, dynamically linked, ..., not stripped
$ strip -s hello
$ file hello # strip 之后的可执行文件
hello: ELF 64-bit LSB executable, x86-64, dynamically linked, ..., stripped
```

### 3.2.2 再次用 bpftrace 跟踪局部函数

strip 之后再测试用 bpftrace 来跟踪局部函数，就不行了：

```shell
$ bpftrace -e 'uprobe:./hello:hello {printf("%s",ustack)}' -c ./hello
No probes to attach
```

# 4 Debug symbol（`gcc -g`）：`DWARF` 格式

对 symbols 有了一个基本了解之后，现在我们重新回到 debug symbols。

既然对于跟踪来说 symbols 就够用了，那 debug symbols 有什么用呢？

## 4.1 Debug symbols 的用途或功能

Debug symbol 是 [dwarf](https://en.wikipedia.org/wiki/DWARF) 格式信息 。
[How debuggers work: Part 3 - Debugging information](https://eli.thegreenplace.net/2011/02/07/how-debuggers-work-part-3-debugging-information/)。

### 4.1.1 功能一：将内存地址映射到具体某行源代码

首先带 `-g` 重新编译，生成的 binary 带 debug 符号，

```shell
$ gcc hello-world.c -g -o hello-world
```

查看，

```shell
$ objdump --dwarf=decodedline hello-world

hello:     file format elf64-x86-64

Decoded dump of debug contents of section .debug_line:

CU: symbol.c:
File name           Line number    Starting address
hello-world.c                 3            0x40055d
hello-world.c                 4            0x400561
hello-world.c                 5            0x40056b
hello-world.c                 6            0x400575
hello-world.c                 7            0x400577
hello-world.c                 8            0x40057b
hello-world.c                 9            0x400585
```

第二列和第三列分别是源代码行号和在内存中的地址。例如，下面这行表示源码中的第三行代码
对应的内存地址为 `0x40055d`，

```shell
File name           Line number    Starting address
hello-world.c                 3            0x40055d
```

在 `readelf` 输出中搜一下地址 **<mark><code>0x40055d</code></mark>**：

```shell
$ readelf -s hello | grep 40055d
Symbol table '.symtab' contains 71 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
    ...
    58: 000000000040055d    26 FUNC    GLOBAL DEFAULT   14 hello
```

最后一列是**<mark>函数名</mark>**，可以看到这个地址对应的是 `hello()` 函数；
我们对照源文件看下，正是第 3 行：

```shell
$ cat -n hello-world.c
1  #include <stdio.h>
2  #include <unistd.h>
3  void hello() {
4      printf("hello world!\n");
5      sleep(1);
6  }
7  int main() {
8      hello();
9  }
```

### 4.1.2 功能二：调用栈展开（stack unwinding）

`stack-unwind.c`：

```c
#include <stdio.h>
#include <unistd.h>
void func_c() {
    int msec=1;
    printf("%s","Hello world from C\n");
    usleep(10000*msec);
}
void func_b() {
    printf("%s","Hello from B\n");
    func_c();
}
void func_a() {
    printf("%s","Hello from A\n");
    func_b();
}
int main() {
    func_a();
}
```

编译，注意带 `-g`，

```shell
$ gcc stack-unwind.c -g -o stack-unwind
```

设置 perf 跟踪 `func_c()` 函数的执行，

```shell
# -x, --exec <executable|path>
$ perf probe -x ./stack-unwind 'func_c'
Added new event:
  probe_stack:func_c   (on func_d in /root/xxx/stack-unwind)

You can now use it in all perf tools, such as:

        perf record -e probe_stack:func_c -aR sleep 1
```

执行应用程序，并用 perf 记录，注意这里选择的**<mark>调用图（call graph）类型是 dwarf</mark>**：

```shell
$ perf record -e probe_stack:func_c -aR -g --call-graph dwarf ./stack-unwind
Hello from A
Hello from B
Hello world from C
[ perf record: Woken up 1 times to write data ]
[ perf record: Captured and wrote 1.097 MB perf.data (1 samples) ]
```

查看调用栈：

```shell
$ perf script
stack-unwind 134641 [044] 748089.345628: probe_stack:func_c: (40055d)
                  40055d func_c+0x0  (/root/xxx/stack-unwind)
                  40059f func_b+0x17 (/root/xxx/stack-unwind)
                  4005b9 func_a+0x17 (/root/xxx/stack-unwind)
                  4005c9 main+0xd    (/root/xxx/stack-unwind)
            7f926d98b554 __libc_start_main+0xf4 (/usr/lib64/libc-2.17.so)
                  400498 _start+0x28 (/root/xxx/stack-unwind)
```

## 4.2 DWARF 格式存在的一些问题

* 占用空间通常很大；
* 基于 BPF 的工具（例如 bpftrace）与它兼容性不好，无法展开 DWARF 类型的调用栈；

    BPF 工具一般使用另一种 stack unwinding 技术：**<mark>frame pointer</mark>**（帧指针）。
    这是 **<mark>perf 使用的默认 stack walking 方式</mark>**，也是 bcc/bpftrace 目前支持的唯一方式。

# 5 调用栈展开（方式二）：frame pointer

## 5.1 基本原理

简单来说，

* 每个 stack trace (或称 activation records 或 call stacks) 包含很多 frames，这
  些 frames 以 LIFO（后进先出）方式存储。这与栈的工作原理一样，**<mark>stack frames</mark>** 由此得名；
* 每个 frame 包含了一个函数执行时的状态信息（参数所在的内存区域、局部变量、返回值等等）；
* Frame pointer 是指向 frame 内存地址的指针，

接下来通过一些基于汇编、offset、CPU 寄存器等黑科技，就能构建出一个完整的函数调用栈。
刚才提到，这是 perf 的默认 stack unwinding 方式，也是 bcc/bpftrace 目前支持的唯一方式。
但与 perf 不同，bcc/bpftrace 用自己的 BPF helper 和 map storage 来存储栈信息。

## 5.2 例子

重新编译，去掉 `-g` 参数（留着也行，但 frame pointer 不会使用 dwarf 信息），

```shell
$ gcc stack-unwind.c -o stack-unwind
```

指定 **<mark><code>--call-graph fp</code></mark>**

```shell
$ perf record -e probe_stack:func_c -aR -g --call-graph fp ./stack-unwind
Hello from A
Hello from B
Hello world from C
[ perf record: Woken up 1 times to write data ]
[ perf record: Captured and wrote 0.402 MB perf.data ]
```

用 bpftrace 跟踪，

```shell
$ bpftrace -e 'uprobe:./stack-unwind:func_c {printf("%s\n", ustack); }' -c ./stack-unwind
Attaching 1 probe...
Hello from A
Hello from B
Hello world from C

        func_c+0
        func_a+24
        main+14
        __libc_start_main+245
```

## 5.3 存在的问题：默认编译参数 `-fomit-frame-pointer`

出于优化目的，很多软件在正式编译时都会指定 **<mark><code>-fomit-frame-pointer</code></mark>**，
导致无法使用 frame-pointer 这种 stack walking 方式。具体看下效果：

```shell
$ gcc stack-unwind.c -o stack-unwind -fomit-frame-pointer

$ bpftrace -e 'uprobe:./stack-unwind:func_c {printf("%s\n", ustack); }' -c ./stack-unwind
Attaching 1 probe...
Hello from A
Hello from B
Hello world from C

        func_c+0
```

很多系统上这都是默认选项，尤其是**<mark>性能敏感的软件，例如 C 标准库、JVM</mark>**。
很多时候用 frame pointer 方式展开调用栈时，会看到 **<mark><code>unknown symbol</code></mark>** 之类的错误，就是因为这个原因。

在 C 世界中，`-g` 比 `-fno-omit-frame-pointer` 要更常用，因此很多场景下都是可以拿到 DWARF 信息的。

# 6 Profiling & tracing

有了以上基础，就可以对系统或程序进行 profiling & tracing 了。

## 6.1 Perf profiling

最简单方式：

```shell
$ perf record -a -g -F 99 -- sleep 10
$ perf script # perf report
```

每秒采样 99 次，持续 10 秒。

提高采集频率：

```shell
$ echo {rate} > /proc/sys/kernel/perf_event_max_sample_rate
```

注意，这将对 CPU、磁盘 IO 等有显著影响。

## 6.2 bpftrace profiling

bpftrace 之类的工具也能做一些 profiling，但**<mark>底层还是用的 perf 数据源</mark>**（`perf_event_output()` )

```shell
$ bpftrace -e 'profile:hz:99 {@[kstack]=count();}'
Attaching 1 probe...
^C

@[
    poll_idle+89
    cpuidle_enter_state+137
    cpuidle_enter+41
    do_idle+468
    cpu_startup_entry+25
    start_secondary+275
    secondary_startup_64_no_verify+194
]: 1
@[
    __d_lookup_rcu+60
    lookup_fast+69
    walk_component+67
    link_path_walk.part.0+545
    path_openat+197
    do_filp_open+145
    do_sys_openat2+546
    do_sys_open+68
    do_syscall_64+51
    entry_SYSCALL_64_after_hwframe+68
]: 1
...
```

## 6.3 bpftrace event tracing

### Kernel tracing

在一个窗口用 bpftrace 跟踪 open() 系统调用，如果被打开的文件是 `hello-world.c`，就打印一条消息出来：

```shell
$ bpftrace -e 'tracepoint:syscalls:sys_enter_open,tracepoint:syscalls:sys_enter_openat {
    $name = str(args->filename);
    if ( $name == "hello-world.c" ) { printf("Somebody touched my file!\n"); }
}'
Attaching 2 probes...
```

然后在另一个窗口中用 `file` 查看这个文件的信息，这会触发 `open()` 系统调用：

```shell
$ file symbol.c
symbol.c: C source, ASCII text
```

会看到 bpftrace 的窗口打印以下信息：

```shell
Somebody touched my file!
```

另一个例子：

```
$ bpftrace -e 'tracepoint:syscalls:sys_enter_execve { printf("%-10u %-5d ", elapsed / 1000000, pid); join(args->argv); }'
Attaching 1 probe...
2244       489603 /opt/cni/bin/cilium-cni
2976       489610 runc --version
2983       489616 runc --version
2989       489622 docker-init --version
...
```

另一个例子：**<mark>查看内核收包调用栈</mark>**：

```shell
$ bpftrace -e 'kprobe:netif_receive_skb_list_internal {printf("%s\n",kstack);}'

        netif_receive_skb_list_internal+1
        gro_normal_list.part.0+25
        napi_complete_done+104
        tg3_poll_msix+331
        net_rx_action+322
        __softirqentry_text_start+223
        asm_call_on_stack+18
        do_softirq_own_stack+55
        irq_exit_rcu+202
        common_interrupt+116
        asm_common_interrupt+30
        cpuidle_enter_state+218
        cpuidle_enter+41
        do_idle+468
        cpu_startup_entry+25
        start_secondary+275
        secondary_startup_64_no_verify+194
```

### User space tracing

第一个例子：假设 libwebp 有漏洞，查看某个服务（PID 25760）是否使用了这个动态库：

```shell
$ grep libwebp /proc/25760/maps
7f7bc6af3000-7f7bc6af6000 r--p 00000000 09:01 38281904                   /usr/lib/x86_64-linux-gnu/libwebp.so.6.0.2
```

看到有使用这个库，接下来跟踪这个动态库，看是否真正有函数调用：

```
$ bpftrace -e 'uprobe:/usr/lib/x86_64-linux-gnu/libwebp.so.6.0.2:* {time("%H:%M:%S "); printf("%s %d\n",comm,pid);}' | tee /tmp/libwebp.trace
```

第二个例子：追踪 DNS 问题：首先找到相关函数，

```
$ for x in `ldd /usr/sbin/named | awk '{print $3}'`; do objdump -T $x | grep dns_ncache && echo $x; done
00000000000a2cc0 g    DF .text 000000000000001e  Base        dns_ncache_add
00000000000a2ce0 g    DF .text 0000000000000022  Base        dns_ncache_addoptout
00000000000a2d10 g    DF .text 000000000000093e  Base        dns_ncache_towire
00000000000a3650 g    DF .text 0000000000000441  Base        dns_ncache_getrdataset
00000000000a4040 g    DF .text 0000000000000392  Base        dns_ncache_current
00000000000a3aa0 g    DF .text 0000000000000597  Base        dns_ncache_getsigrdataset
/lib/x86_64-linux-gnu/libdns.so.110
```

然后再用 bpftrace，参考前面的 uprobe 例子。

# 7 `/proc/<pid>/*`

最后整理一些 `/proc/<pid>/` 下面的信息。

## 7.1 `/proc/<pid>/status`

```shell
$ sudo cat /proc/200/status
Name:   ksoftirqd/37
Umask:  0000
State:  S (sleeping)
...
Cpus_allowed:   0020,00000000
Cpus_allowed_list:      37
voluntary_ctxt_switches:        27251
nonvoluntary_ctxt_switches:     350
```

其中，**<mark>根据 NSpid 字段可以判断这个进程是不是容器</mark>**：

```shell
$ cat /proc/1229/status | grep NSpid
NSpid: 1229
$ cat /proc/11459/status | grep NSpid
NSpid: 11459 1 # 11459 是在宿主机的 pid ns 内的进程 ID，1 是在容器自己的 pid ns 的进程 ID
```

## 7.2 `/proc/<pid>/stack`

```shell
$ sudo cat /proc/20/stack
[<0>] smpboot_thread_fn+0x117/0x170
[<0>] kthread+0x12b/0x150
[<0>] ret_from_fork+0x22/0x30
```

## 7.3 `/proc/<pid>/maps`

```shell
$ docker ps
CONTAINER ID    IMAGE        COMMAND
b50745618ca2    3e6e2c29dbda  "./my-prog ..."   4 days ago   Up 4 days      k8s_my_test_prog

$ docker top b50745618ca2
UID                 PID                 PPID                C                   STIME               TTY                 TIME                CMD
root                10390               10363               0                   Jul08               ?                   00:15:19            ./my-prog ...

$ cat /proc/10390/status
Name:   mybin
Umask:  0022
State:  S (sleeping)
...
NStgid: 10390   1
NSpid:  10390   1
NSpgid: 10390   1
NSsid:  10390   1
Threads:        5
Cpus_allowed:   ffff,ffffffff
Cpus_allowed_list:      0-47
Mems_allowed_list:      0-1
voluntary_ctxt_switches:        2849002
nonvoluntary_ctxt_switches:     3008
```

```shell
$ cat /proc/10390/maps
00400000-008d2000 r-xp 00000000 fd:02 3233562123                         /my-prog
...
7fa61c6b3000-7fa61c732000 ---p 00000000 00:00 0
7fa61c732000-7fa61c792000 rw-p 00000000 00:00 0
7ffe97008000-7ffe97029000 rw-p 00000000 00:00 0                          [stack]
7ffe97102000-7ffe97106000 r--p 00000000 00:00 0                          [vvar]
7ffe97106000-7ffe97108000 r-xp 00000000 00:00 0                          [vdso]
ffffffffff600000-ffffffffff601000 --xp 00000000 00:00 0                  [vsyscall]
```

# 参考资料

1. [Practical Linux tracing](https://medium.com/coccoc-engineering-blog/things-you-should-know-to-begin-playing-with-linux-tracing-tools-part-i-x-225aae1aaf13)
2. [bpftrace, uprobe and containers](https://kirshatrov.com/posts/bpf-docker-uprobe/), 2020
