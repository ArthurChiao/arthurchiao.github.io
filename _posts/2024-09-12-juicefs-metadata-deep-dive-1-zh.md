---
layout    : post
title     : "JuiceFS 元数据引擎初探：高层架构、引擎选型、读写工作流（2024）"
date      : 2024-09-12
lastupdate: 2024-10-10
categories: storage juicefs
---

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-tikv-cluster.png" width="90%"/></p>
<p align="center">Fig. JuiceFS cluster initialization, and how POSIX file operations are handled by JuiceFS.</p>

* [JuiceFS 元数据引擎初探：高层架构、引擎选型、读写工作流（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-1-zh.md %})
* [JuiceFS 元数据引擎再探：开箱解读 TiKV 中的 JuiceFS 元数据（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-2-zh.md %})
* [JuiceFS 元数据引擎三探：从实践中学习 TiKV 的 MVCC 和 GC（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-3-zh.md %})
* [JuiceFS 元数据引擎四探：元数据大小评估、限流与限速的设计思考（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-4-zh.md %})
* [JuiceFS 元数据引擎五探：元数据备份与恢复（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-5-zh.md %})

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----


# 1 JuiceFS 高层架构与组件

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-components-1.png" width="70%"/></p>
<p align="center">Fig. JuiceFS components and architecutre.</p>

如图，最粗的粒度上可以分为三个组件。

## 1.1 JuiceFS client

* `juicefs format ...` 可以创建一个 volume；
* `juicefs config ...` 可以修改一个 volume 的配置；
* `juicefs mount ...` 可以把一个 volume 挂载到机器上，然后用户就可以在里面读写文件了；

## 1.2 Metatdata engine（元数据引擎）

* 用于**<mark>存储 JuiceFS 的元数据</mark>**，例如每个文件的文件名、最后修改时间等等；
* 可选择 etcd、TiKV 等等；

## 1.3. Object store

实际的对象存储，例如 S3、Ceph、阿里云 OSS 等等，存放 JuiceFS volume 内的数据。

# 2 JuiceFS 元数据存储引擎对比：`tikv vs. etcd`

## 2.1 设计与优缺点对比

| | TiKV as metadata engine | etcd as metadata engine |
|:-----|:-----|:-----|
| **<mark>管理节点</mark>**（e.g. leader election）| PD (TiKV cluster manager) | etcd server |
| **<mark>数据节点</mark>**（存储 juicefs metadata） | TiKV server     | etcd server |
| **<mark>数据节点对等</mark>** | 无要求     | **<mark>完全对等</mark>** |
| **<mark>数据一致性粒度</mark>** | region-level (TiKV 的概念，`region < node`)  | node-level |
| **<mark>Raft 粒度</mark>** | region-level (multi-raft，TiKV 的概念)  | node-level |
| **<mark>缓存多少磁盘数据在内存中</mark>** | 一部分 | **<mark>所有</mark>** |
| **<mark>集群支持的最大数据量</mark>** | **<mark><code>PB</code></mark>** 级别 | 几十 GB 级别 |
| **<mark>性能</mark>**（JuiceFS 场景） | 高（猜测是因为 raft 粒度更细，并发读写高） | **<mark>低</mark>** |
| 维护和二次开发门槛 | 高（相比 etcd） | 低 |
| 流行度 & 社区活跃度 | 低（相比 etcd） | 高 |
| 适用场景 | **<mark>大和超大 JuiceFS 集群</mark>** | 中小 JuiceFS 集群 |

## 2.2 几点解释

etcd 集群，

* 每个节点完全对等，既负责管理又负责存储数据；
* 所有数据**<mark>全部缓存在内存中</mark>**，每个节点的数据完全一致。
   这一点限制了 etcd 集群支持的最大数据量和扩展性，
   例如现在官网还是建议不要超过 8GB（实际上较新的版本在技术上已经没有这个限制了，
   但仍受限于机器的内存）。

TiKV 方案可以可以理解成把管理和数据存储分开了，

* PD 可以理解为 **<mark><code>TiKV cluster manager</code></mark>**，负责 leader 选举、multi-raft、元数据到 region 的映射等等；
* 节点之间也**<mark>不要求对等</mark>**，PD 按照 region（比如 96MB）为单位，将 N（默认 3）个副本放到 N 个 TiKV node 上，而实际上 TiKV 的 node 数量是 M，`M >= N`；
* 数据放在 TiKV 节点的磁盘，内存中**<mark>只缓存一部分</mark>**（默认是用机器 45% 的内存，可控制）。

## 2.3 例子：TiKV 集群 engine size 和内存使用监控

TiKV 作为存储引擎，总结成一句话就是：**<mark>根据硬件配置干活，能者多劳</mark>** ——
内存大、磁盘大就多干活，反之就少干活。

下面的监控展示是 7 台 TiKV node 组成的一个集群，各 **<mark>node 内存不完全一致</mark>**：
3 台 256GB 的，2 台 128GB 的，2 台 64GB 的，
可以看到每个 TiKV server 确实只用了各自所在 node 一半左右的内存：

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/tikv-engine-size.png" width="100%"/></p>
<p align="center">Fig. TiKV engine size and memory usage of a 7-node (with various RAMs) cluster.</p>

# 3 JuiceFS + TiKV：集群启动和宏观读写流程

## 3.1 架构

用 TiKV 作为元数据引擎，架构如下（先忽略其中的细节信息，稍后会介绍）：

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-tikv-cluster.png" width="90%"/></p>
<p align="center">Fig. JuiceFS cluster initialization, and how POSIX file operations are handled by JuiceFS.</p>

## 3.2 TiKV 集群启动

### 3.2.1 TiKV & PD 配置差异

两个组件的几个核心配置项，

```shell
$ cat /etc/tikv/pd-config.toml
name = "pd-node1"
data-dir = "/var/data/pd"

client-urls = "https://192.168.1.1:2379" # 客户端（例如 JuiceFS）访问 PD 时，连接这个地址
peer-urls   = "https://192.168.1.1:2380" # 其他 PD 节点访问这个 PD 时，连接这个地址，也就是集群内互相通信的地址

# 创建集群时的首批 PD
initial-cluster-token = "<anything you like>"
initial-cluster = "pd-node1=https://192.168.1.3:2380,pd-node2=https://192.168.1.2:2380,pd-node3=https://192.168.1.1:2380"
```

可以看到，**<mark>PD 的配置和 etcd 就比较类似，需要指定其他 PD 节点地址</mark>**，它们之间互相通信。

TiKV 节点（tikv-server）的配置就不一样了，

```shell
$ cat /etc/tikv/tikv-config.toml
[pd]
endpoints = ["https://192.168.1.1:2379", "https://192.168.1.2:2379", "https://192.168.1.3:2379"]

[server]
addr = "192.168.1.1:20160"        # 服务地址，JuiceFS client 会直接访问这个地址读写数据
status-addr = "192.168.1.1:20180" # prometheus 
```

可以看到，

1. TiKV 会配置所有 PD 节点的地址，以便自己注册到 PD 作为一个数据节点（存储JuiceFS 元数据）；
2. TiKV 还会配置一个地址的 server 地址，这个读写本节点所管理的 region 内的数据用的；
   正常流程是 JuiceFS client 先访问 PD，拿到 region 和 tikv-server 信息，
   然后再到 tikv-server 来读写数据（对应 JuiceFS 的元数据）；
3. TiKV **<mark>不会配置其他 TiKV 节点的地址</mark>**，也就是说 TiKV 节点之间不会 peer-to-peer 互连。
  属于同一个 raft group 的多个 region 通信，也是先通过 PD 协调的，最后 region leader 才发送数据给 region follower。
  详见 [1]。

### 3.2.2 服务启动

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-tikv-cluster.png" width="90%"/></p>
<p align="center">Fig. JuiceFS cluster initialization, and how POSIX file operations are handled by JuiceFS.</p>

对应图中 step 1 & 2：

* step 1. PD 集群启动，选主；
* step 2. TiKV 节点启动，向 PD 注册；每个 TiKV 节点称为一个 store，也就是元数据仓库。

## 3.3 宏观读写流程

对应图中 step 3~5：

* step 3. JuiceFS 客户端连接到 PD；发出读写文件请求；

    * JuiceFS 客户端中会初始化一个 TiKV 的 transaction kv client，这里面又会初始化一个 PD client，
    * 简单来说，此时 JuiceFS 客户端就有了 PD 集群的信息，例如哪个文件对应到哪个 region，这个 region 分布在哪个 TiKV 节点上，TiKV 服务端连接地址是多少等等；

* step 4. JuiceFS （内部的 TiKV 客户端）直接向 TiKV 节点（准确说是 region leader）发起读写请求；
* step 5. 元数据处理完成，JuiceFS 客户端开始往对象存储里读写文件。

# 4 TiKV 内部数据初探

TiKV 内部存储的都是 JuiceFS 的元数据。具体来说又分为两种：

1. 用户文件的元数据：例如用户创建了一个 `foo.txt`，在 TiKV 里面就会对应一条或多条元数据来描述这个文件的信息；
2. JuiceFS 系统元数据：例如每个 volume 的配置信息，这些对用户是不可见的。

TiKV 是扁平的 KV 存储，所以以上两类文件都放在同一个扁平空间，通过 key 访问。
本文先简单通过命令看看里面的元数据长什么样，下一篇再结合具体 JuiceFS 操作来深入解读这些元数据。

## 4.1 简单脚本 `tikv-ctl.sh/pd-ctl.sh`

简单封装一下对应的命令行工具，使用更方便，

```shell
$ cat pd-ctl.sh
tikv-ctl \
        --ca-path /etc/tikv/pki/root.crt --cert-path /etc/tikv/pki/tikv.crt --key-path /etc/tikv/pki/tikv.key \
        --host 192.168.1.1:20160 \
        "$@"

$ cat pd-ctl.sh
pd-ctl \
        --cacert /etc/tikv/pki/root.crt --cert /etc/tikv/pki/pd.crt --key /etc/tikv/pki/pd.key \
        --pd https://192.168.1.1:2379  \
        "$@"
```

## 4.2 `tikv-ctl scan` 扫描 key/value

tikv-ctl **<mark>不支持只列出所有 keys</mark>**，所以只能 key 和 value 一起打印（扫描）。

扫描前缀是 `foo` 开头的所有 key：

```shell
$ ./tikv-ctl.sh scan --from 'zfoo' --to 'zfop' --limit 100
...
key: zfoo-dev\375\377A\001\000\000\000\000\000\000\377\000Dfile3.\377txt\000\000\000\000\000\372
key: zfoo-dev\375\377A\001\000\000\000\000\000\000\377\000Dfile4.\377txt\000\000\000\000\000\372
...
key: zfoo-dev\375\377setting\000\376
        default cf value: start_ts: 452330324173520898 value: 7B0A22...
```

扫描的时候一定要在 key 前面加一个 `z` 前缀，这是 TiKV 的一个[设计](https://tikv.org/docs/3.0/reference/tools/tikv-ctl/)，

> The raw-scan command scans directly from the RocksDB. Note that to scan data keys you need to add a 'z' prefix to keys.

代码出处 [components/keys/src/lib.rs](https://github.com/tikv/tikv/blob/v5.0.0/components/keys/src/lib.rs#L29)。
但对用户来说不是太友好，暴露了太多内部细节，没有 `etcdctl` 方便直接。

## 4.3 `tikv-ctl mvcc` 查看给定 key 对应的 value

```shell
$ ./tikv-ctl.sh mvcc -k 'zfoo-dev\375\377A\001\000\000\000\000\000\000\377\000Dfile1.\377txt\000\000\000\000\000\372' --show-cf default,lock,write
key: zfoo-dev\375\377A\001\000\000\000\000\000\000\377\000Dfile1.\377txt\000\000\000\000\000\372
         write cf value: start_ts: 452330816414416901 commit_ts: 452330816414416903 short_value: 010000000000000002
```

## 4.4 `tikv-ctl --decode <key>` 解除字符转义

```shell
# tikv escaped format -> raw format
./tikv-ctl.sh --decode 'foo-dev\375\377A\001\000\000\000\000\000\000\377\000Dfile4.\377txt\000\000\000\000\000\372'
foo-dev\375A\001\000\000\000\000\000\000\000Dfile4.txt
```

## 4.5 `tikv-ctl --to-hex`：转义表示 -> 十六进制表示

```shell
$ ./tikv-ctl.sh --to-hex '\375'
FD
```

## 4.6 `tikv-ctl --to-escaped <value>`：十六进制 value -> 带转义的字符串

```shell
./tikv-ctl.sh scan --from 'zfoo' --to 'zfop' --limit 100
key: zfoo-dev\375\377setting\000\376
        default cf value: start_ts: 452330324173520898 value: 7B0A22...
```

其中的 value 是可以解码出来的，

```shell
# hex -> escaped string
$ ./tikv-ctl.sh --to-escaped '7B0A22...'
{\n\"Name\": \"...\",\n\"UUID\": \"8cd1ac73\",\n\"Storage\": \"S3\",\n\"Bucket\": \"http://xxx\",\n\"AccessKey\": \"...\",\n\"BlockSize\": 4096,\n\"Compression\": \"none\",\n\"KeyEncrypted\": true,\n\"MetaVersion\": 1,\n\"UploadLimit\": 0,\n\"DownloadLimit\": 0,\n\"\": \"\"\n}
```

# 5 总结

本文介绍了一些 JuiceFS 元数据引擎相关的内容。

# 参考资料

1. [A Deep Dive into TiKV](https://www.pingcap.com/blog/deep-dive-into-tikv/), 2016, pincap.com

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
