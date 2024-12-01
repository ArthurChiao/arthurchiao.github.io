---
layout    : post
title     : "JuiceFS 元数据引擎再探：开箱解读 TiKV 中的 JuiceFS 元数据（2024）"
date      : 2024-09-12
lastupdate: 2024-09-12
categories: storage juicefs
---

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-volume-bw-control.png" width="90%"/></p>
<p align="center">Fig. JuiceFS upload/download data bandwidth control.</p>

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

有了第一篇的铺垫，本文直接进入正题。

* 首先创建一个 volume，然后在其中做一些文件操作，然后通过 tikv-ctl 等工具在 TiKV 中查看对应的元数据。
* 有了这些基础，我们再讨论 JuiceFS metadata key 和 TiKV 的编码格式。

> 之前有一篇类似的，开箱解读 etcd 中的 Cilium 元数据：
> [What's inside Cilium Etcd (kvstore)]({% link _posts/2020-05-20-whats-inside-cilium-etcd.md %})。

# 1 创建一个 volume

创建一个名为 `foo-dev` 的 JuiceFS volume。

## 1.1 JuiceFS client 日志

用 juicefs client 的 **<mark><code>juicefs format</code></mark>** 命令创建 volume，

```shell
$ juicefs format --storage oss --bucket <bucket> --access-key <key> --secret-key <secret key> \
  tikv://192.168.1.1:2379,192.168.1.2:2379,192.168.1.3:2379/foo-dev foo-dev

<INFO>: Meta address: tikv://192.168.1.1:2379,192.168.1.2:2379,192.168.1.3:2379/foo-dev
<INFO>: Data use oss://xxx/foo-dev/
<INFO>: Volume is formatted as {
  "Name": "foo-dev",
  "UUID": "ec843b",
  "Storage": "oss",
  "BlockSize": 4096,
  "MetaVersion": 1,
  "UploadLimit": 0,
  "DownloadLimit": 0,
  ...
}
```

* 对象存储用的是阿里云 OSS；
* TiKV 地址指向的是 **<mark>PD 集群地址</mark>**，上一篇已经介绍过，`2379` 是 PD 接收客户端请求的端口；

## 1.2 JuiceFS client 中的 `TiKV/PD client` 初始化/调用栈

下面我们进入 JuiceFS 代码，看看 JuiceFS client 初始化和**<mark>连接到元数据引擎</mark>**的调用栈：

```
mount
 |-metaCli = meta.NewClient
 |-txnkv.NewClient(url)                                          // github.com/juicedata/juicefs: pkg/meta/tkv_tikv.go
 |  |-NewClient                                                  // github.com/tikv/client-go:    txnkv/client.go
 |     |-pd.NewClient                                            // github.com/tikv/client-go:    tikv/kv.go
 |     |    |-NewClient                                          // github.com/tikv/pd:           client/client.go
 |     |       |-NewClientWithContext                            // github.com/tikv/pd:           client/client.go
 |     |          |-createClientWithKeyspace                     // github.com/tikv/pd:           client/client.go
 |     |             |-c.pdSvcDiscovery = newPDServiceDiscovery  // github.com/tikv/pd:           client/pd_xx.go
 |     |             |-c.setup()                                 // github.com/tikv/pd:           client/pd_xx.go
 |     |                 |-c.pdSvcDiscovery.Init()
 |     |                 |-c.pdSvcDiscovery.AddServingURLSwitchedCallback
 |     |                 |-c.createTokenDispatcher()
 |     |-spkv, err := tikv.NewEtcdSafePointKV
 |     |-tikv.NewRPCClient
 |     |-tikv.NewKVStore(uuid, pdClient, spkv, rpcClient)        // github.com/tikv/client-go:    tikv/kv.go
 |         |-oracles.NewPdOracle
 |         |-store := &KVStore{}
 |         |-go store.runSafePointChecker()
 |         |     |-check key "/tidb/store/gcworker/saved_safe_point" from etcd every 10s
 |         |-go store.safeTSUpdater()
 |-metaCli.NewSession
    |-doNewSession
       |-m.setValue(m.sessionKey(m.sid), m.expireTime())  // SE
       |-m.setValue(m.sessionInfoKey(m.sid), sinfo)       // SI
```

这里面连接到 TiKV/PD 的代码有点绕，

* 传给 `juicefs` client 的是 **<mark>PD 集群地址</mark>**，
* 但代码使用的是 **<mark>tikv 的 client-go 包</mark>**，创建的是一个 **<mark><code>tikv transaction client</code></mark>**，
* 这个 tikv transaction client 里面会去创建 **<mark><code>pd client</code></mark>** 连接到 PD 集群，

所以，**<mark>架构上看 juicefs 是直连 PD</mark>**，但实现上**<mark>并没有直接创建 pd client</mark>**，
也没有直接使用 pd 的库。

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-tikv-cluster.png" width="70%"/></p>
<p align="center">Fig. JuiceFS cluster initialization, and how POSIX file operations are handled by JuiceFS.</p>


## 1.3 `tikv-ctl` 查看空 volume 的系统元数据

现在再把目光转到 TiKV。看看这个空的 volume 在 TiKV 中对应哪些元数据：

```shell
$ ./tikv-ctl.sh scan --from 'zfoo' --to 'zfop'
key: zfoo-dev\375\377A\001\000\000\000\000\000\000\377\000I\000\000\000\000\000\000\371  # attr?
key: zfoo-dev\375\377ClastCle\377anupSess\377ions\000\000\000\000\373                    # lastCleanupSessions
key: zfoo-dev\375\377CnextChu\377nk\000\000\000\000\000\000\371                          # nextChunk
key: zfoo-dev\375\377CnextIno\377de\000\000\000\000\000\000\371                          # nextInode
key: zfoo-dev\375\377CnextSes\377sion\000\000\000\000\373                                # nextSession
key: zfoo-dev\375\377SE\000\000\000\000\000\000\377\000\001\000\000\000\000\000\000\371  # session
key: zfoo-dev\375\377SI\000\000\000\000\000\000\377\000\001\000\000\000\000\000\000\371  # sessionInfo
key: zfoo-dev\375\377setting\000\376                                                     # setting
```

以上就是我们新建的 volume `foo-dev` 的所有 entry 了。
也就是说一个 volume 创建出来之后，默认就有这些 **<mark>JuiceFS 系统元数据</mark>**。

TiKV 中的**<mark>每个 key 都经过了两层编码</mark>**（JuiceFS 和 TiKV），我们后面再介绍编码规则。
就目前来说，根据 **<mark>key 中的字符</mark>**还是依稀能看出每个 **<mark>key 是干啥用的</mark>**，
为方便起见直接注释在上面每行的最后了。比如，下面两个 session 相关的 entry 就是上面调用栈最后两个创建的：

* `session`
* `sessionInfo`

## 1.4 例子：`tikv-ctl mvcc` 解码 volume setting 元数据

TiKV 中的每个 entry 都是 key/value。现在我们尝试解码最后一个 entry，key 是
**<mark><code>zfoo-dev\375\377setting\000\376</code></mark>**，
我们**<mark>来看看它的 value —— 也就是它的内容 —— 是什么</mark>**：

```shell
$ value_hex=$(./tikv-ctl.sh mvcc -k 'zfoo-dev\375\377setting\000\376' --show-cf=default | awk '/default cf value:/ {print $NF}')
$ value_escaped=$(./tikv-ctl.sh --to-escaped $value_hex)
$ echo -e $value_escaped | sed 's/\\"/"/g' | jq .
```

输出：

```json
{
  "Name": "foo-dev",
  "UUID": "1ce2973b",
  "Storage": "S3",
  "Bucket": "http://xx/bucket",
  "AccessKey": "xx",
  "SecretKey": "xx",
  "BlockSize": 4096,
  "MetaVersion": 1,
  "UploadLimit": 0,
  "DownloadLimit": 0,
  ...
}
```

可以看到是个 JSON 结构体。这其实就是这个 volume 的配置信息。如果对 JuiceFS 代码有一定了解，
就会看出来它对应的其实就是 `type Format` 这个 struct。

### 1.4.1 对应 JuiceFS `Format` 结构体

```go
// https://github.com/juicedata/juicefs/blob/v1.2.0/pkg/meta/config.go#L72

type Format struct {
    Name             string
    UUID             string
    Storage          string
    StorageClass     string `json:",omitempty"`
    Bucket           string
    AccessKey        string `json:",omitempty"`
    SecretKey        string `json:",omitempty"`
    SessionToken     string `json:",omitempty"`
    BlockSize        int
    Compression      string `json:",omitempty"`
    Shards           int    `json:",omitempty"`
    HashPrefix       bool   `json:",omitempty"`
    Capacity         uint64 `json:",omitempty"`
    Inodes           uint64 `json:",omitempty"`
    UploadLimit      int64  `json:",omitempty"` // Mbps
    DownloadLimit    int64  `json:",omitempty"` // Mbps
    ...
}
```

# 2 将 volume 挂载（mount）到机器

接下来我们找一台机器，把这个 volume 挂载上去，这样就能在这个 volume 里面读写文件了。

## 2.1 JuiceFS client 挂载日志

```shell
$ juicefs mount --verbose --backup-meta 0 tikv://192.168.1.1:2379,192.168.1.2:2379,192.168.1.3:2379/foo-dev /tmp/foo-dev
<INFO>:  Meta address: tikv://192.168.1.1:2379,192.168.1.2:2379,192.168.1.3:2379/foo-dev [interface.go:406]
<DEBUG>: Creating oss storage at endpoint http://<url> [object_storage.go:154]
<INFO>:  Data use oss://xx/foo-dev/ [mount.go:497]
<INFO>:  Disk cache (/var/jfsCache/ec843b85/): capacity (10240 MB), free ratio (10%), max pending pages (15) [disk_cache.go:94]
<DEBUG>: Scan /var/jfsCache/ec843b85/raw to find cached blocks [disk_cache.go:487]
<DEBUG>: Scan /var/jfsCache/ec843b85/rawstaging to find staging blocks [disk_cache.go:530]
<DEBUG>: Found 8 cached blocks (32814 bytes) in /var/jfsCache/ec843b85/ with 269.265µs [disk_cache.go:515]
<INFO>:  Create session 4 OK with version: 1.2.0 [base.go:279]
<INFO>:  Prometheus metrics listening on 127.0.0.1:34849 [mount.go:165]
<INFO>:  Mounting volume foo-dev at /tmp/foo-dev ... [mount_unix.go:203]
<INFO>:  OK, foo-dev is ready at /tmp/foo-dev [mount_unix.go:46]
```

可以看到成功挂载到了本机路径 **<mark><code>/tmp/foo-dev/</code></mark>**。

## 2.2 查看挂载信息

```shell
$ mount | grep juicefs
JuiceFS:foo-dev on /tmp/foo-dev type fuse.juicefs (rw,relatime,user_id=0,group_id=0,default_permissions,allow_other)

$ cd /tmp/foo-dev
$ ls # 空目录
```

## 2.3 查看 JuiceFS 隐藏（系统）文件

新建的 volume 里面其实有几个隐藏文件：

```shell
$ cd /tmp/foo-dev
$ ll
-r-------- 1 root root  .accesslog
-r-------- 1 root root  .config
-r--r--r-- 1 root root  .stats
dr-xr-xr-x 2 root root  .trash/
```

### 2.3.1 `.accesslog`

可以通过 `cat` 这个文件看到一些 JuiceFS client 底层的操作日志，我们一会会用到。

### 2.3.2 `.config`

包括 `Format` 在内的一些 volume 配置信息：

```shell
$ cat .config
{
 "Meta": {
  "Strict": true,
  "Retries": 10,
  "CaseInsensi": false,
  "ReadOnly": false,
  "NoBGJob": false,
  "OpenCache": 0,
  "Heartbeat": 12000000000,
  "MountPoint": "/tmp/foo-dev",
  "Subdir": "",
  "CleanObjFileLever": 1
 },
 "Format": {
  "Name": "foo-dev",
  "UUID": "ec843b85",
  "Storage": "oss",
  "Bucket": "http://<url>",
  "UploadLimit": 0,
  "DownloadLimit": 0,
  ...
 },
 "Chunk": {
  "CacheDir": "/var/jfsCache/ec843b85",
  "CacheMode": 384,
  "CacheSize": 10240,
  "FreeSpace": 0.1,
  "AutoCreate": true,
  "Compress": "none",
  "MaxUpload": 20,
  "MaxDeletes": 2,
  "MaxRetries": 10,
  "UploadLimit": 0,
  "DownloadLimit": 0,
  "Writeback": false,
  "UploadDelay": 0,
  "HashPrefix": false,
  "BlockSize": 4194304,
  "GetTimeout": 60000000000,
  "PutTimeout": 60000000000,
  "CacheFullBlock": true,
  "BufferSize": 314572800,
  "Readahead": 0,
  "Prefetch": 1,
  "UseMountUploadLimitConf": false,
  "UseMountDownloadLimitConf": false
 },
 "Version": "1.2.0",
 "AttrTimeout": 1000000000,
 "DirEntryTimeout": 1000000000,
 "EntryTimeout": 1000000000,
 "BackupMeta": 0,
 "HideInternal": false
}
```

### 2.3.3 `.stats`

`cat` 能输出一些 prometheus metrics：

```shell
$ cat .stats
...
juicefs_uptime 374.021754516
juicefs_used_buffer_size_bytes 0
juicefs_used_inodes 7
juicefs_used_space 28672
```

用 prometheus 采集器把这个数据收上去，就能在 grafana 上展示 volume 的各种内部状态。

### 2.3.4 `.trash`

类似于 Windows 的垃圾箱。如果启用了，删掉的文件会在里面保存一段时间再真正从对象存储删掉。

# 3 创建、更新、删除文件

接下来做一些文件操作，看看 TiKV 中对应元数据的变化。

## 3.1 创建文件

### 3.1.1 创建文件

```shell
$ cd /tmp/foo-dev
$ echo test3 > file3.txt
```

### 3.1.2 JuiceFS `.accesslog`

```shell
$ cat .accesslog
[uid:0,gid:0,pid:169604] getattr (1): OK (1,[drwxrwxrwx:0040777,3,0,0,1725503250,1725585251,1725585251,4096]) <0.001561>
[uid:0,gid:0,pid:169604] lookup (1,file3.txt): no such file or directory <0.000989>
[uid:0,gid:0,pid:169604] create (1,file3.txt,-rw-r-----:0100640): OK (103,[-rw-r-----:0100640,1,0,0,1725585318,1725585318,1725585318,0]) [fh:27] <0.003850>
[uid:0,gid:0,pid:169604] flush (103,27): OK <0.000005>
[uid:0,gid:0,pid:169604] write (103,6,0,27): OK <0.000048>
[uid:0,gid:0,pid:169604] flush (103,27): OK <0.026205>
[uid:0,gid:0,pid:0     ] release (103): OK <0.000006>
[uid:0,gid:0,pid:169749] getattr (1): OK (1,[drwxrwxrwx:0040777,3,0,0,1725503250,1725585318,1725585318,4096]) <0.000995>
[uid:0,gid:0,pid:169750] getattr (1): OK (1,[drwxrwxrwx:0040777,3,0,0,1725503250,1725585318,1725585318,4096]) <0.001219>
```

### 3.1.3 TiKV 元数据

```shell
$ ./tikv-ctl.sh scan --from 'zfoo' --to 'zfop' --limit 100
...
key: zfoo-dev\375\377A\001\000\000\000\000\000\000\377\000Dfile3.\377txt\000\000\000\000\000\372
...
```

可以看到 meta 中多了几条元数据，依稀可以分辨出对应的就是我们创建的文件，

1. 这个 key 经过了 juicefs 和 tikv 两次编码，
2. 简单来说，它是 volume + `0xFD`（8 进制的 `\375`）+ 文件名 + tikv 编码，最终得到的就是上面看到的这个 key。

**<mark>对应的 value</mark>** 一般长这样：

```shell
$ ./tikv-ctl.sh mvcc -k 'zfoo-dev\375\377A\001\000\000\000\000\000\000\377\000Dfile3.\377txt\000\000\000\000\000\372' --show-cf default,lock,write
key: zfoo-dev\375\377A\001\000\000\000\000\000\000\377\000Dfile3.\377txt\000\000\000\000\000\372
         write cf value: start_ts: 452330816414416901 commit_ts: 452330816414416903 short_value: 010000000000000002
```

先粗略感受一下，后面再具体介绍 key/value 的编解码规则。

## 3.2 删除文件操作

### 3.2.1 删除文件

```shell
rm file4.txt
```

### 3.2.2 JuiceFS `.accesslog`

```shell
$ cat .accesslog
[uid:0,gid:0,pid:169604] getattr (1): OK (1,[drwxrwxrwx:0040777,3,0,0,1725503250,1725585532,1725585532,4096]) <0.001294>
[uid:0,gid:0,pid:169902] lookup (1,file4.txt): OK (104,[-rw-r-----:0100640,1,0,0,1725585532,1725585532,1725585532,6]) <0.001631>
[uid:0,gid:0,pid:169902] unlink (1,file4.txt): OK <0.004206>
[uid:0,gid:0,pid:169904] getattr (1): OK (1,[drwxrwxrwx:0040777,3,0,0,1725503250,1725585623,1725585623,4096]) <0.000718>
[uid:0,gid:0,pid:169905] getattr (1): OK (1,[drwxrwxrwx:0040777,3,0,0,1725503250,1725585623,1725585623,4096]) <0.000843>
```

### 3.2.3 TiKV 元数据

对应的元数据就从 TiKV 删掉了。

## 3.3 更新（追加）文件

### 3.3.1 更新文件

```shell
$ echo test3 >> file3.txt
```

### 3.3.2 JuiceFS `.accesslog`

```shell
$ cat .accesslog
[uid:0,gid:0,pid:169604] getattr (1): OK (1,[drwxrwxrwx:0040777,3,0,0,1725503250,1725585623,1725585623,4096]) <0.001767>
[uid:0,gid:0,pid:169604] lookup (1,file3.txt): OK (103,[-rw-r-----:0100640,1,0,0,1725585318,1725585318,1725585318,6]) <0.001893>
[uid:0,gid:0,pid:169604] open (103): OK [fh:51] <0.000884>
[uid:0,gid:0,pid:169604] flush (103,51): OK <0.000011>
[uid:0,gid:0,pid:169604] write (103,6,6,51): OK <0.000068>
[uid:0,gid:0,pid:169604] flush (103,51): OK <0.036778>
[uid:0,gid:0,pid:0     ] release (103): OK <0.000024>
```

### 3.3.3 TiKV 元数据

* 如果追加的内容不多，TiKV 中还是那条元数据，但 value 会被更新；
* 如果追加的内容太多（例如几百兆），**<mark>文件就会被切分</mark>**，这时候元数据就会有**<mark>多条</mark>**了。

# 4 元数据操作和 TiKV key/value 编码规则

上一节简单看了下创建、更新、删除 volume 中的文件，TiKV 中对应的元数据都有什么变化。
我们有意跳过了 key/value 是如何编码的，这一节就来看看这块的内容。

## 4.1 JuiceFS key 编码规则

### 4.1.1 每个 key 的公共前缀：`<vol_name> + 0xFD`

TiKV 客户端初始化：**<mark>每个 key 的 base 部分</mark>**：`<vol_name> + 0xFD`

```go
// pkg/meta/tkv_tikv.go

func init() {
    Register("tikv", newKVMeta)
    drivers["tikv"] = newTikvClient
}

func newTikvClient(addr string) (tkvClient, error) {
    client := txnkv.NewClient(strings.Split(tUrl.Host, ","))
    prefix := strings.TrimLeft(tUrl.Path, "/")
    return withPrefix(&tikvClient{client.KVStore, interval}, append([]byte(prefix), 0xFD)), nil
}
```

### 4.1.2 每个 key 后面的部分

根据对应的是文件、目录、文件属性、系统元数据等等，会有不同的编码规则：

```go
// pkg/meta/tkv.go

/**
  Ino     iiiiiiii
  Length  llllllll
  Indx    nnnn
  name    ...
  sliceId cccccccc
  session ssssssss
  aclId   aaaa

All keys:
  setting            format
  C...               counter
  AiiiiiiiiI         inode attribute
  AiiiiiiiiD...      dentry
  AiiiiiiiiPiiiiiiii parents // for hard links
  AiiiiiiiiCnnnn     file chunks
  AiiiiiiiiS         symlink target
  AiiiiiiiiX...      extented attribute
  Diiiiiiiillllllll  delete inodes
  Fiiiiiiii          Flocks
  Piiiiiiii          POSIX locks
  Kccccccccnnnn      slice refs
  Lttttttttcccccccc  delayed slices
  SEssssssss         session expire time
  SHssssssss         session heartbeat // for legacy client
  SIssssssss         session info
  SSssssssssiiiiiiii sustained inode
  Uiiiiiiii          data length, space and inodes usage in directory
  Niiiiiiii          detached inde
  QDiiiiiiii         directory quota
  Raaaa                 POSIX acl
*/
```

具体可以再看看这个文件中的代码。

### 4.1.3 最终格式：字节序列

```go
// pkg/meta/tkv.go

func (m *kvMeta) fmtKey(args ...interface{}) []byte {
    b := utils.NewBuffer(uint32(m.keyLen(args...)))
    for _, a := range args {
        switch a := a.(type) {
        case byte:
            b.Put8(a)
        case uint32:
            b.Put32(a)
        case uint64:
            b.Put64(a)
        case Ino:
            m.encodeInode(a, b.Get(8))
        case string:
            b.Put([]byte(a))
        default:
            panic(fmt.Sprintf("invalid type %T, value %v", a, a))
        }
    }
    return b.Bytes()
}
```

## 4.2 TiKV 对 JuiceFS key 的进一步编码

JuiceFS client 按照以上规则拼好一个 key 之后，接下来 TiKV 会再进行一次编码：

1. 加一些 TiKV 的前缀，例如给文件 key 加个 `z` 前缀；

    * TiKV 代码 [components/keys/src/lib.rs](https://github.com/tikv/tikv/blob/v5.0.0/components/keys/src/lib.rs#L29)

2. **<mark>转义</mark>**，例如 8 个字节插入一个 `\377`（对应 `0xFF`），不够 8 字节的补全等等；

    * tikv [rust encode 代码](https://tikv.github.io/doc/src/tikv_util/lib.rs.html#161-200)
    * 借鉴的是 golang [**<mark><code>protobuf</code></mark>** 的代码](https://github.com/golang/protobuf/blob/v1.5.4/proto/text_encode.go#L406)

最终得到的就是我们用 `tikv-ctl scan` 看到的那些 key。

## 4.3 例子：查看特殊元数据：volume 的 setting/format 信息

JuiceFS 的 `Format` 配置保存在 tikv 中，原始 key 是 `setting`，经过以上两层编码就变成了下面的样子：

```shell
$ ./tikv-ctl.sh scan --from 'zfoo' --to 'zfop' --limit 100
key: zfoo-dev\375\377setting\000\376
        default cf value: start_ts: 452330324173520898 value: 7B0A22...
```

其中的 value 是可以解码出来的，

```shell
# hex -> escaped string
$ ./tikv-ctl.sh --to-escaped '7B0A22...'
{\n\"Name\": \"foo-dev\",\n\"UUID\": \"8cd1ac73\",\n\"Storage\": \"S3\",\n\"Bucket\": \"http://xxx\",\n\"AccessKey\": \"...\",\n\"BlockSize\": 4096,\n\"Compression\": \"none\",\n\"KeyEncrypted\": true,\n\"MetaVersion\": 1,\n\"UploadLimit\": 0,\n\"DownloadLimit\": 0,\n\"\": \"\"\n}
```

对应的就是 `pkg/meta/config.go` 中的 `Format` 结构体。

# 5 总结

本文结合一些具体 JuiceFS 操作，分析了 TiKV 内的元数据格式与内容。

# 参考资料

1. [What's inside Cilium Etcd (kvstore)]({% link _posts/2020-05-20-whats-inside-cilium-etcd.md %})

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
