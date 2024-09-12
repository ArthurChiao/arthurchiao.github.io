---
layout    : post
title     : "JuiceFS 元数据引擎四探：元数据大小评估、限流与限速的设计思考（2024）"
date      : 2024-09-22
lastupdate: 2024-09-22
categories: storage juicefs
---

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-volume-bw-control.png" width="90%"/></p>
<p align="center">Fig. JuiceFS upload/download data bandwidth control.</p>

* [JuiceFS 元数据引擎初探：高层架构、引擎选型、读写工作流（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-1-zh.md %})
* [JuiceFS 元数据引擎再探：开箱解读 TiKV 中的 JuiceFS 元数据（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-2-zh.md %})
* [JuiceFS 元数据引擎三探：从实践中学习 TiKV 的 MVCC 和 GC（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-3-zh.md %})
* [JuiceFS 元数据引擎四探：元数据大小评估、限流与限速的设计思考（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-4-zh.md %})

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----

# 1 元数据存储在哪儿？文件名到 TiKV regions 的映射

## 1.1 `pd-ctl region` 列出所有 region 信息

```shell
$ pd-ctl.sh region | jq .
{
  "regions": [
    {
      "id": 11501,
      "start_key": "6161616161616161FF2D61692D6661742DFF6261636B7570FD41FFCF68030000000000FF4900000000000000F8",
      "end_key": "...",
      "epoch": {
        "conf_ver": 23,
        "version": 300
      },
      "peers": [
        {
          "id": 19038,
          "store_id": 19001,
          "role_name": "Voter"
        },
        ...
      ],
      "leader": {
        "id": 20070,
        "store_id": 20001,
        "role_name": "Voter"
      },
      "written_bytes": 0,
      "read_bytes": 0,
      "written_keys": 0,
      "read_keys": 0,
      "approximate_size": 104,
      "approximate_keys": 994812
    },
  ]
}
```

## 1.2 `tikv-ctl region-properties` 查看 region 属性详情

```shell
$ ./tikv-ctl.sh region-properties -r 23293
mvcc.min_ts: 438155461254971396
mvcc.max_ts: 452403302095650819
mvcc.num_rows: 1972540
mvcc.num_puts: 3697509
mvcc.num_deletes: 834889
mvcc.num_versions: 4532503
mvcc.max_row_versions: 54738
num_entries: 4549844
num_deletes: 17341
num_files: 6
sst_files: 001857.sst, 001856.sst, 002222.sst, 002201.sst, 002238.sst, 002233.sst
region.start_key: 6e6772...
region.end_key: 6e6772...
region.middle_key_by_approximate_size: 6e6772...
```

## 1.3 `tikv-ctl --to-escaped`：从 region 的 start/end key 解码文件名范围

如上，每个 region 都会有 **<mark><code>start_key/end_key</code></mark>** 两个属性，
这里面编码的就是这个 region 内存放是元数据的 key 范围。我们挑一个来解码看看：

```shell
$ tikv-ctl.sh --to-escaped '6161616161616161FF2D61692D6661742DFF6261636B7570FD41FFCF68030000000000FF4900000000000000F8'
aaaaaaaa\377-ai-fat-\377backup\375A\377\317h\003\000\000\000\000\000\377I\000\000\000\000\000\000\000\370
```

再 decode 一把会更清楚：

```shell
$ tikv-ctl.sh --decode 'aaaaaaaa\377-ai-fat-\377backup\375A\377\317h\003\000\000\000\000\000\377I\000\000\000\000\000\000\000\370'
aaaaaaaa-ai-fat-backup\375A\317h\003\000\000\000\000\000I
```

对应的是一个名为 **<mark><code>aaaaaaa-ai-fat-backup</code></mark>** 的 volume 内的一部分元数据。

## 1.4 `filename -> region`：相关代码

这里看一下从文件名映射到 TiKV region 的代码。

PD 客户端代码，

```go
    // GetRegion gets a region and its leader Peer from PD by key.
    // The region may expire after split. Caller is responsible for caching and
    // taking care of region change.
    // Also, it may return nil if PD finds no Region for the key temporarily,
    // client should retry later.
    GetRegion(ctx , key []byte, opts ...GetRegionOption) (*Region, error)

// GetRegion implements the RPCClient interface.
func (c *client) GetRegion(ctx , key []byte, opts ...GetRegionOption) (*Region, error) {
    options := &GetRegionOp{}
    for _, opt := range opts {
        opt(options)
    }
    req := &pdpb.GetRegionRequest{
        Header:      c.requestHeader(),
        RegionKey:   key,
        NeedBuckets: options.needBuckets,
    }
    serviceClient, cctx := c.getRegionAPIClientAndContext(ctx, options.allowFollowerHandle && c.option.getEnableFollowerHandle())
    resp := pdpb.NewPDClient(serviceClient.GetClientConn()).GetRegion(cctx, req)
    return handleRegionResponse(resp), nil
}
```

PD 服务端代码，

```go
func (h *regionHandler) GetRegion(w http.ResponseWriter, r *http.Request) {
    rc := getCluster(r)
    vars := mux.Vars(r)
    key := url.QueryUnescape(vars["key"])
    // decode hex if query has params with hex format
    paramsByte := [][]byte{[]byte(key)}
    paramsByte = apiutil.ParseHexKeys(r.URL.Query().Get("format"), paramsByte)

    regionInfo := rc.GetRegionByKey(paramsByte[0])
    b := response.MarshalRegionInfoJSON(r.Context(), regionInfo)

    h.rd.Data(w, http.StatusOK, b)
}

// GetRegionByKey searches RegionInfo from regionTree
func (r *RegionsInfo) GetRegionByKey(regionKey []byte) *RegionInfo {
    region := r.tree.search(regionKey)
    if region == nil {
        return nil
    }
    return r.getRegionLocked(region.GetID())
}
```

返回的是 region info，

```go
// RegionInfo records detail region info for api usage.
// NOTE: This type is exported by HTTP API. Please pay more attention when modifying it.
// easyjson:json
type RegionInfo struct {
    ID          uint64              `json:"id"`
    StartKey    string              `json:"start_key"`
    EndKey      string              `json:"end_key"`
    RegionEpoch *metapb.RegionEpoch `json:"epoch,omitempty"`
    Peers       []MetaPeer          `json:"peers,omitempty"` // https://github.com/pingcap/kvproto/blob/master/pkg/metapb/metapb.pb.go#L734

    Leader            MetaPeer      `json:"leader,omitempty"`
    DownPeers         []PDPeerStats `json:"down_peers,omitempty"`
    PendingPeers      []MetaPeer    `json:"pending_peers,omitempty"`
    CPUUsage          uint64        `json:"cpu_usage"`
    WrittenBytes      uint64        `json:"written_bytes"`
    ReadBytes         uint64        `json:"read_bytes"`
    WrittenKeys       uint64        `json:"written_keys"`
    ReadKeys          uint64        `json:"read_keys"`
    ApproximateSize   int64         `json:"approximate_size"`
    ApproximateKeys   int64         `json:"approximate_keys"`
    ApproximateKvSize int64         `json:"approximate_kv_size"`
    Buckets           []string      `json:"buckets,omitempty"`

    ReplicationStatus *ReplicationStatus `json:"replication_status,omitempty"`
}

// GetRegionFromMember implements the RPCClient interface.
func (c *client) GetRegionFromMember(ctx , key []byte, memberURLs []string, _ ...GetRegionOption) (*Region, error) {
    for _, url := range memberURLs {
        conn := c.pdSvcDiscovery.GetOrCreateGRPCConn(url)
        cc := pdpb.NewPDClient(conn)
        resp = cc.GetRegion(ctx, &pdpb.GetRegionRequest{
            Header:    c.requestHeader(),
            RegionKey: key,
        })
        if resp != nil {
            break
        }
    }

    return handleRegionResponse(resp), nil
}
```

# 2 JuiceFS 集群规模与元数据大小（engine size）

## 2.1 二者的关系

一句话总结：**<mark>并没有一个线性的关系</mark>**。

### 2.1.1 文件数量 & 平均文件大小

TiKV engine size 的大小，和集群的**<mark>文件数量</mark>**和**<mark>每个文件的大小</mark>**都有关系。
例如，同样是一个文件，

1. 小文件可能对应一条 TiKV 记录；
2. 大文件会被拆分，对应多条 TiKV 记录。

### 2.1.2 MVCC GC 快慢

GC 的勤快与否也会显著影响 DB size 的大小。第三篇中有过详细讨论和验证了，这里不再赘述，

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/impacts-of-tikv-gc-lagging.png" width="100%"/></p>
<p align="center">Fig. TiKV DB size soaring in a JuiceFS cluster, caused by TiKV GC lagging.</p>

## 2.2 两个集群对比

* 集群 1：~1PB 数据，以**<mark>小文件</mark>**为主，**<mark><code>~30K</code></mark>** regions，**<mark><code>~140GB</code></mark>** TiKV engine size (3 replicas)；
* 集群 2：~7PB 数据，以**<mark>大文件</mark>**为主，**<mark><code>~800</code></mark>** regions，**<mark><code>~3GB</code></mark>** TiKV engine size (3 replicas)；

如下面监控所示，虽然集群 2 的数据量是前者的
**<mark><code>7</code></mark>** 倍，但元数据只有前者的 **<mark><code>1/47</code></mark>**，

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/cluster-comparison-region-and-db-size.png" width="100%"/></p>
<p align="center">Fig. TiKV DB sizes and region counts of 2 JuiceFS clusters:
cluster-1 with ~1PB data composed of mainly small files, cluster-2 with ~7PB data composed of mainly large files.</p>

# 3 限速（上传/下载数据带宽）设计

限速（upload/download bandwidth）本身是属于数据平面（data）的事情，也就是与 S3、Ceph、OSS 等等对象存储关系更密切。

但第二篇中已经看到，这个限速的配置信息是保存在元数据平面（metadata）TiKV 中 —— 具体来说就是 volume 的 setting 信息；
此外，后面讨论元数据请求限流（rate limiting）时还需要参考限速的设计。所以，这里我们稍微展开讲讲。

## 3.1 带宽限制：`--upload-limit/--download-limit`

* **<mark><code>--upload-limit</code></mark>**，单位 `Mbps`
* **<mark><code>--download-limit</code></mark>**，单位 `Mbps`

## 3.2 JuiceFS 限速行为

1. 如果 `juicefs mount` 挂载时指定了这两个参数，就会以指定的参数为准；
2. 如果 `juicefs mount` 挂载时没指定，就会以 TiKV 里面的配置为准，

    * juicefs client 里面有一个 `refresh()` 方法一直在监听 TiKV 里面的 Format 配置变化，
    * 当这俩配置发生变化时（可以通过 **<mark><code>juicefs config</code></mark>** 来修改 TiKV 中的配置信息），client 就会**<mark>把最新配置 reload 到本地（本进程）</mark>**，
    * 这种情况下，可以看做是**<mark>中心式配置的客户端限速</mark>**，工作流如下图所示，

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-volume-bw-control.png" width="90%"/></p>
<p align="center">Fig. JuiceFS upload/download data bandwidth control.</p>

## 3.3 JuiceFS client reload 配置的调用栈

`juicefs mount` 时注册一个 reload 方法，

```
mount
 |-metaCli.OnReload
    |-m.reloadCb = append(m.reloadCb, func() {
                                        updateFormat(c)(fmt) //  fmt 是从 TiKV 里面拉下来的最新配置
                                        store.UpdateLimit(fmt.UploadLimit, fmt.DownloadLimit)
                                      })
```

然后有个后台任务一直在监听 TiKV 里面的配置，一旦发现配置变了就会执行到上面注册的回调方法，

```go
refresh()
  for {
        old := m.getFormat()
        format := m.Load(false) // load from tikv

        if !reflect.DeepEqual(format, old) {
            cbs := m.reloadCb
            for _, cb := range cbs {
                cb(format)
        }
  }
```

# 4 限流（metadata 请求）设计

## 4.1 为什么需要限流？

如下图所示，

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-tikv-cluster.png" width="80%"/></p>
<p align="center">Fig. JuiceFS cluster initialization, and how POSIX file operations are handled by JuiceFS.</p>

* **<mark>限速</mark>**保护的是 5；
* **<mark>限流</mark>**保护的是 **<mark><code>3 & 4</code></mark>**；

下面我们通过实际例子看看可能会打爆 3 & 4 的几种场景。

## 4.2 打爆 TiKV API 的几种场景

### 4.2.1 `mlocate (updatedb)` 等扫盘工具 

#### 一次故障复盘

下面的监控，左边是 TiKV 集群的请求数量，右边是 node CPU 利用率（主要是 PD leader 在用 CPU），

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/pd-cpu-soaring.png" width="100%"/></p>
<p align="center">Fig. PD CPU soaring caused by too much requests.</p>

大致时间线，

* `14:30` 开始，`kv_get` 请求突然飙升，导致 PD leader 节点的 CPU 利用率大幅飙升；
* `14:40` 介入调查，确定暴增的请求来自**<mark>同一个 volume</mark>**，但这个 volume 被几十个用户的 pod 挂载，
  能联系到的用户均表示 `14:30` 没有特殊操作；
* `14:30~16:30` 继续联系其他用户咨询使用情况 + 主动排查；期间删掉了几个用户暂时不用的 pod，减少挂载这个 volume 的 juicefs client 数量，请求量有一定下降；
* `16:30` 定位到**<mark>请求来源</mark>**
    * 确定暴增的请求**<mark>不是用户程序读写导致的</mark>**，
    * 客户端大部分都 ubuntu 容器（AI 训练），
    * 使用的是**<mark>同一个容器镜像</mark>**，里面自带了一个 `daily` 的定时 `mlocate` 任务去扫盘磁盘，

这个扫盘定时任务的时间是每天 `14:30`，因此把挂载到容器里的 JuiceFS volume 也顺带扫了。
确定这个原因之后，

* `16:40` 开始，逐步强制停掉（**<mark><code>pkill -f updatedb.mlocate</code></mark>**）
  并禁用（**<mark><code>mv /etc/cron.daily/mlocate /tmp/</code></mark>**）这些扫盘任务，
  看到请求就下来了，PD CPU 利用率也跟着降下来了；
* 第二天早上 `6:00` 又发生了一次（凌晨 `00:00` 其实也有一次），后来排查发生是还有几个基础镜像也有这个任务，只是 daily 时间不同。

#### `juicefs mount` 时会自动禁用 mlocate，但 CSI 部署方式中部分失效

其实官方已经注意到了 mlocate，所以 **<mark><code>juicefs mount</code></mark>**
的入口代码就专门有检测，开了之后就自动关闭，

```go
// cmd/mount_unix.go

func mountMain(v *vfs.VFS, c *cli.Context) {
    if os.Getuid() == 0 {
        disableUpdatedb()
          |-path := "/etc/updatedb.conf"
          |-file := os.Open(path)
          |-newdata := ...
          |-os.WriteFile(path, newdata, 0644)
    }
    ...
```

但是，**<mark>在 K8s CSI 部署方式中，这个代码是部分失效的</mark>**：

<p align="center"><img src="/assets/img/k8s-juicefs-csi/juicefs-pod-setup-workflow.png" width="100%"/></p>
<p align="center">Fig. JuiceFS K8s CSI deployment</p>

JuiceFS per-node daemon 在创建 mount pod 时，**<mark>会把宿主机的</mark>** `/etc/updatedb.conf` **<mark>挂载到 mount pod 里面</mark>**，
所以它能禁掉宿主机上的 mlocate，

```yaml
  volumes:
  - hostPath:
      path: /etc/updatedb.conf
      type: FileOrCreate
    name: updatedb
```

但正如上一小结的例子看到的，**<mark>业务 pod 里如果开了 updatedb，它就管不到了</mark>**。
而且业务容器很可能是同一个镜像启动大量 pod，挂载同一个 volume，所以**<mark>扫描压力直线上升</mark>**。

### 4.2.2 版本控制工具

类似的工具可能还有版本控制工具（git、svn）、编程 IDE（vscode）等等，威力可能没这么大，但排查时需要留意。

## 4.3 需求：对元数据引擎的保护能力

以上 case，包括上一篇看到的用户疯狂 update 文件的 case，都暴露出同一个问题：
JuiceFS 缺少对元数据引擎的保护能力。

### 4.3.1 现状：JuiceFS 目前还没有

**<mark>社区版目前（2024.09）是没有的</mark>**，企业版不知道有没有。

下面讨论下如果基于社区版，如何加上这种限流能力。

## 4.4 客户端限流方案设计

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-volume-bw-control.png" width="90%"/></p>
<p align="center">Fig. JuiceFS upload/download data bandwidth control.</p>

基于 JuiceFS 已有的设计，再参考其限速实现，其实加上一个限流能力并不难，代码也不多：

1. 扩展 `Format` 结构体，增加限流配置；
2. `juicefs format|config` 增加配置项，允许配置具体限流值；这会将配置写到元数据引擎里面的 volume `setting`；
3. `juicefs mount` 里面解析 `setting` 里面的限流配置，传给 client 里面的 metadata 模块；
4. metadata 模块做客户端限流，例如针对 txnkv 里面的不到 10 个方法，在函数最开始的地方增加一个限流检查，allow 再继续，否则就等待。

这是一种（中心式配置的）**<mark>客户端限流</mark>**方案。

## 4.5 服务端限流方案设计

在 TiKV 集群前面挡一层代理，在代理上做限流，属于**<mark>服务端限流</mark>**。

# 参考资料

1. [图解 JuiceFS CSI 工作流：K8s 创建带 PV 的 Pod 时，背后发生了什么（2024）]({% link _posts/2024-07-13-k8s-juicefs-csi-workflow-zh.md %})

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
