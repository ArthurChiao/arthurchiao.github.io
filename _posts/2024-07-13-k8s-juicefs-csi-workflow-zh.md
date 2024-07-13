---
layout    : post
title     : "图解 JuiceFS CSI 工作流：K8s 创建带 PV 的 Pod 时，背后发生了什么（2024）"
date      : 2024-07-13
lastupdate: 2024-07-13
categories: k8s storage juicefs
---

JuiceFS 是一个架设在**<mark>对象存储</mark>**（S3、Ceph、OSS 等）之上的分布式**<mark>文件系统</mark>**，
简单来说，

* 对象存储：只能通过 key/value 方式使用；
* 文件系统：日常看到的文件目录，能执行 `ls/cat/find/truncate` 等等之类的**<mark>文件读写</mark>**操作。

本文从 high-level 梳理了 JuiceFS CSI 方案中，当创建一个带 PV 的 pod 以及随后 pod 读写 PV 时，
k8s/juicefs 组件在背后都做了什么，方便快速了解 K8s CSI 机制及 JuiceFS 的基本工作原理。

<p align="center"><img src="/assets/img/k8s-juicefs-csi/juicefs-pod-setup-workflow.png" width="100%"/></p>

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----

# 1 背景知识

简单列几个基础知识，有背景的可直接跳过。

## 1.1 K8s CSI (Container Storage Interface )

> The Container Storage Interface (CSI) is a standard for exposing arbitrary
> block and file storage systems to containerized workloads on Container
> Orchestration Systems (COs) like Kubernetes.
>
> https://kubernetes-csi.github.io/docs/

CSI 是 K8s 支持的一种容器存储机制，扩展性非常好，
各存储方案只要根据规范实现一些接口，就能集成到 k8s 中提供存储服务。

一般来说，存储方案需要在每个 node 上部署一个称为 “**<mark><code>CSI plugin</code></mark>**” 的服务，
kubelet 在创建带 PV 容器的过程中会调用这个 plugin。但要注意，

* K8s 的**<mark>网络插件</mark>** CNI plugin 是一个**<mark>可执行文件</mark>**，
  放在 `/opt/cni/bin/` 下面就行了，kubelet 在创建 pod 网络时**<mark>直接运行
  </mark>**这个可执行文件；
* K8s 的**<mark>存储插件</mark>** CSI plugin 是一个**<mark>服务</mark>**（某种程度上，
  称为 **<mark><code>agent</code></mark>** 更好理解），kubelet 在初始化
  PV 时通过 **<mark><code>gRPC</code></mark>** 调用这个 plugin；

## 1.2 FUSE (Filesystem in Userspace)

FUSE 是一种用户态文件系统，使得用户开发自己的文件系统非常方便。

懒得再重新画图，
这里借 **<mark><code>lxcfs</code></mark>**（跟 juicefs 没关系，但也是一种 FUSE
文件系统）展示一下 **<mark>FUSE 的基本工作原理</mark>**：

> [Linux 容器底层工作机制：从 500 行 C 代码到生产级容器运行时（2023）]({% link _posts/2023-12-27-linux-container-and-runtime-zh.md %})

<p align="center"><img src="/assets/img/linux-container-and-runtime/lxcfs-fuse.png" width="70%" height="70%"></p>
<p align="center">Fig. lxcfs/fuse workflow: how a read operation is handled [2]</p>

JuiceFS 基于 FUSE 实现了一个用户态文件系统。

> 来自社区文档的一段内容，简单整理：
>
> 传统上，实现一个 FUSE 文件系统，需要基于 Linux libfuse 库，它提供两种 API：
> 
> * high-level API：**<mark>基于文件名和路径</mark>**。
> 
>   libfuse 内部做了 VFS 树的模拟，对外暴露基于路径的 API。
>
>   适合元数据本身是基于路径提供的 API 的系统，比如 HDFS 或者 S3 之类。
>   如果元数据本身是基于 inode 的目录树，这种 inode → path →inode 的转换就会
>   影响性能。
> 
> * low-level API：**<mark>基于 inode</mark>**。内核的 VFS 跟 FUSE 库交互就使用 low-level API。
> 
> JuiceFS 的**<mark>元数据基于 inode 组织</mark>**，所以用 low-level API 实现（
> 依赖 go-fuse 而非 libfuse），简单自然，性能好。

## 1.3 JuiceFS 三种工作模式

JuiceFS 有几种工作或部署方式：

1. 进程挂载模式

    JuiceFS client 运行在 CSI Node plugin 容器中，所有需要挂载的 JuiceFS PV 都会在这个容器内以进程模式挂载。

2. CSI 方式，又可分为两种：

    1. mountpod 方式：在每个 node 上，CSI plugin 动态为每个**<mark>被 local pod 使用的 PV</mark>** 创建一个保姆 pod，

        * 这个 mount pod 是 **<mark><code>per-PV</code></mark>** 而非 per-business-pod 的，
          也就是说如果 node 上有**<mark>多个业务 pod 在使用同一 PV，那只会有一个 mount pod</mark>**，
          下图可以看出来，

          <p align="center"><img src="/assets/img/k8s-juicefs-csi/juicefs-pod-setup-workflow.png" width="100%"/></p>
          <p align="center">Fig. JuiceFS as K8s CSI solution: workflow when a business pod is created (JuiceFS mountpod mode).</p>

        * mount pod 里面装了 **<mark><code>juicefs client</code></mark>**，替业务 pod 完成 juicefs 相关的读写操作；
          为了从字面上更容易理解，本文接下来把 mount pod 称为 **<mark><code>dynamic client pod</code></mark>** 或 client pod。
        * 这是 JuiceFS CSI 的**<mark>默认工作方式</mark>**；
        * FUSE 需要 mount pod 具有 privilege 权限；
        * client pod 重启会导致业务 pod 一段时间读写不可用，但 client pod 好了之后业务 pod 就能继续读写了。

    2. . CSI sidecar 方式：给每个使用 juicefs PV 的业务 pod 创建一个 sidecar 容器。

        * **<mark><code>per-pod</code></mark>** 级别的 sidecar；
        * 注意 sidecar 就不是 JuiceFS plugin 创建的了，CSI Controller 会注册一个 Webhook 来监听容器变动，在创建 pod 时，
          webhook 给 pod yaml 自动注入一个 sidecar，跟 Istio 自动给 pod 注入 Envoy 容器类似；
        * Sidecar 重启需要重建业务 Pod 才能恢复。
        * 也依赖 FUSE，所以 sidecar 需要 privilege 权限。这会导致**<mark>每个 sidecar 都能看到 node 上所有设备</mark>**，有风险，所以不建议；

## 1.4 小结

有了以上基础，接下来看 k8s 中创建一个业务 pod 并且它要求挂载一个 PV 时，k8s 和 juicefs 组件都做了什么事情。

# 2 创建一个使用 PV 的 pod 时，k8s 和 juicefs 组件都做了什么

<p align="center"><img src="/assets/img/k8s-juicefs-csi/juicefs-pod-setup-workflow.png" width="100%"/></p>
<p align="center">Fig. JuiceFS as K8s CSI solution: workflow when a business pod is created (JuiceFS mountpod mode).</p>

## Step 1：kubelet 启动，监听集群的 pod 资源变化

kubelet 作为 k8s 在每个 node 上的 agent，在启动后会监听整个 k8s 集群中的 pod 资源变化。
具体来说就是，kube-apiserver 中有 **<mark><code>pod create/update/delete events</code></mark>** 发生时，kubelet 都会立即收到。

## Step 2：kubelet 收到业务 pod 创建事件，**<mark>开始创建</mark>** pod

kubelet 收到一条 **<mark><code>pod create</code></mark>** 事件后，首先判断这个
pod 是否在自己的管辖范围内（spec 中的 **<mark>nodeName 是否是这台 node</mark>**），
是的话就**<mark>开始创建这个 pod</mark>**。

### Step 2.1 创建业务 pod：初始化部分

**<mark><code>kubelet.INFO</code></mark>** 中有比较详细的日志：

```
10:05:57.410  Receiving a new pod "pod1(<pod1-id>)"
10:05:57.411  SyncLoop (ADD, "api"): "pod1(<pod1-id>)"
10:05:57.411  Needs to allocate 2 "nvidia.com/gpu" for pod "<pod1-id>" container "container1"
10:05:57.411  Needs to allocate 1 "our-corp.com/ip" for pod "<pod1-id>" container "container1"
10:05:57.413  Cgroup has some missing paths: [/sys/fs/cgroup/pids/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/systemd/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/cpuset/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/memory/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/cpu,cpuacct/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/cpu,cpuacct/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/hugetlb/kubepods/burstable/pod<pod1-id>]
10:05:57.413  Cgroup has some missing paths: [/sys/fs/cgroup/memory/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/systemd/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/cpu,cpuacct/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/cpu,cpuacct/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/hugetlb/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/pids/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/cpuset/kubepods/burstable/pod<pod1-id>]
10:05:57.413  Cgroup has some missing paths: [/sys/fs/cgroup/cpu,cpuacct/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/pids/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/cpuset/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/systemd/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/memory/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/cpu,cpuacct/kubepods/burstable/pod<pod1-id> /sys/fs/cgroup/hugetlb/kubepods/burstable/pod<pod1-id>]
10:05:57.415  Using factory "raw" for container "/kubepods/burstable/pod<pod1-id>"
10:05:57.415  Added container: "/kubepods/burstable/pod<pod1-id>" (aliases: [], namespace: "")
10:05:57.419  Waiting for volumes to attach and mount for pod "pod1(<pod1-id>)"

10:05:57.432  SyncLoop (RECONCILE, "api"): "pod1(<pod1-id>)"

10:05:57.471  Added volume "meminfo" (volSpec="meminfo") for pod "<pod1-id>" to desired state.
10:05:57.471  Added volume "cpuinfo" (volSpec="cpuinfo") for pod "<pod1-id>" to desired state.
10:05:57.471  Added volume "stat" (volSpec="stat") for pod "<pod1-id>" to desired state.
10:05:57.480  Added volume "share-dir" (volSpec="pvc-6ee43741-29b1-4aa0-98d3-5413764d36b1") for pod "<pod1-id>" to desired state.
10:05:57.484  Added volume "data-dir" (volSpec="juicefs-volume1-pv") for pod "<pod1-id>" to desired state.
...
```

可以看出里面会依次处理 pod 所需的各种资源：

1. 设备：例如 **<mark><code>GPU</code></mark>**；
2. IP 地址；
3. cgroup 资源隔离配置；
4. **<mark><code>volumes</code></mark>**。

本文主要关注 volume 资源。

### Step 2.2 处理 pod 依赖的 volumes

上面日志可以看到，业务 pod 里面声明了一些需要挂载的 volumes。**<mark>几种类型</mark>**：

1. hostpath 类型：直接把 node 路径挂载到容器内；
2. lxcfs 类型：为了解决资源视图问题 [2]；
3. 动态/静态 PV 类型

本文的 JuiceFS volume 就属于 PV 类型，继续看 kubelet 日志：

```
# kubelet.INFO
10:05:57.509  operationExecutor.VerifyControllerAttachedVolume started for volume "xxx"
10:05:57.611  Starting operationExecutor.MountVolume for volume "xxx" (UniqueName: "kubernetes.io/host-path/<pod1-id>-xxx") pod "pod1" (UID: "<pod1-id>") 
10:05:57.611  operationExecutor.MountVolume started for volume "juicefs-volume1-pv" (UniqueName: "kubernetes.io/csi/csi.juicefs.com^juicefs-volume1-pv") pod "pod1" (UID: "<pod1-id>") 
10:05:57.611  kubernetes.io/csi: mounter.GetPath generated [/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount]
10:05:57.611  kubernetes.io/csi: created path successfully [/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv]
10:05:57.611  kubernetes.io/csi: saving volume data file [/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/vol_data.json]
10:05:57.611  kubernetes.io/csi: volume data file saved successfully [/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/vol_data.json]
10:05:57.613  MountVolume.MountDevice succeeded for volume "juicefs-volume1-pv" (UniqueName: "kubernetes.io/csi/csi.juicefs.com^juicefs-volume1-pv") pod "pod1" (UID: "<pod1-id>") device mount path "/var/lib/k8s/kubelet/plugins/kubernetes.io/csi/pv/juicefs-volume1-pv/globalmount"
10:05:57.616  kubernetes.io/csi: mounter.GetPath generated [/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount]
10:05:57.616  kubernetes.io/csi: Mounter.SetUpAt(/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount)
10:05:57.616  kubernetes.io/csi: created target path successfully [/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount]
10:05:57.618  kubernetes.io/csi: calling NodePublishVolume rpc [volid=juicefs-volume1-pv,target_path=/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount]
10:05:57.713  Starting operationExecutor.MountVolume for volume "juicefs-volume1-pv" (UniqueName: "kubernetes.io/csi/csi.juicefs.com^juicefs-volume1-pv") pod "pod1" (UID: "<pod1-id>") 
...
10:05:59.506  kubernetes.io/csi: mounter.SetUp successfully requested NodePublish [/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount]
10:05:59.506  MountVolume.SetUp succeeded for volume "juicefs-volume1-pv" (UniqueName: "kubernetes.io/csi/csi.juicefs.com^juicefs-volume1-pv") pod "pod1" (UID: "<pod1-id>") 
10:05:59.506  kubernetes.io/csi: mounter.GetPath generated [/var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount]
```

对于每个 volume，依次执行，

1. operationExecutor.**<mark><code>VerifyControllerAttachedVolume()</code></mark>** 方法，做一些检查；
2. operationExecutor.**<mark><code>MountVolume()</code></mark>** 方法，将指定的 volume 挂载到容器目录；
3. 对于 CSI 存储，还会调用到 CSI plugin 的 **<mark><code>NodePublishVolume()</code></mark>** 方法，初始化对应的 PV，JuiceFS 就是这种模式。

接下来 kubelet 会不断**<mark>检测所有 volumes 是否都挂载好</mark>**，没好的话不会进入下一步（创建 sandbox 容器）。

## Step 3：`kubelet --> CSI plugin`（juicefs）：setup PV

下面进一步看一下 node CSI plugin 初始化 PV 挂载的逻辑。**<mark>调用栈</mark>**：

```
         gRPC NodePublishVolume()
kubelet ---------------------------> juicefs node plugin (also called "driver", etc)
```

## Step 4：JuiceFS CSI plugin 具体工作

看一下 JuiceFS CSI node plugin 的日志，这里直接在机器上看：

```shell
(node) $ docker logs --timestamps k8s_juicefs-plugin_juicefs-csi-node-xxx | grep juicefs-volume1
10:05:57.619 NodePublishVolume: volume_id is juicefs-volume1-pv

10:05:57.619 NodePublishVolume: creating dir /var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount

10:05:57.620 ceFormat cmd: [/usr/local/bin/juicefs format --storage=OSS --bucket=xx --access-key=xx --secret-key=${secretkey} --token=${token} ${metaurl} juicefs-volume1]
10:05:57.874 Format output is juicefs <INFO>: Meta address: tikv://node1:2379,node2:2379,node3:2379/juicefs-volume1
10:05:57.874 cefs[1983] <INFO>: Data use oss://<bucket>/juicefs-volume1/

10:05:57.875 Mount: mounting "tikv://node1:2379,node2:2379,node3:2379/juicefs-volume1" at "/jfs/juicefs-volume1-pv" with options [token=xx]

10:05:57.884 createOrAddRef: Need to create pod juicefs-node1-juicefs-volume1-pv.
10:05:57.891 createOrAddRed: GetMountPodPVC juicefs-volume1-pv, err: %!s(<nil>)
10:05:57.891 ceMount: mount tikv://node1:2379,node2:2379,node3:2379/juicefs-volume1 at /jfs/juicefs-volume1-pv
10:05:57.978 createOrUpdateSecret: juicefs-node1-juicefs-volume1-pv-secret, juicefs-system
10:05:59.500 waitUtilPodReady: Pod juicefs-node1-juicefs-volume1-pv is successful

10:05:59.500 NodePublishVolume: binding /jfs/juicefs-volume1-pv at /var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount with options []
10:05:59.505 NodePublishVolume: mounted juicefs-volume1-pv at /var/lib/k8s/kubelet/pods/<pod1-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount with options []
```

可以看到确实执行了 [**<mark><code>NodePublishVolume()</code></mark>**](https://github.com/juicedata/juicefs-csi-driver/blob/v0.23.6/pkg/driver/node.go) 方法，
这个方法是**<mark>每个 CSI plugin 方案各自实现的</mark>**，所以里面做什么事情就跟存储方案有很大关系。
接下来具体看看 JuiceFS plugin 做的什么。

### Step 4.1 给 pod PV 创建挂载路径，初始化 volume

默认配置下，每个 pod 会在 node 上对应一个存储路径，

```shell
(node) $ ll /var/lib/k8s/kubelet/pods/<pod-id>
containers/
etc-hosts
plugins/
volumes/
```

juicefs plugin 会在以上 **<mark><code>volumes/</code></mark>** 目录内给 PV 创建一个对应的子目录和挂载点，

<code>/var/lib/k8s/kubelet/pods/{pod1-id}/<mark>volumes/kubernetes.io~csi/juicefs-volume1-pv</mark>/mount</code>。

然后用 `juicefs` 命令行工具**<mark>格式化</mark>**，

```shell
$ /usr/local/bin/juicefs format --storage=OSS --bucket=xx --access-key=xx --secret-key=${secretkey} --token=${token} ${metaurl} juicefs-volume1
```

例如，如果 JuiceFS 对接的是**<mark>阿里云 OSS</mark>**，上面就对应阿里云的 bucket 地址及访问秘钥。

### Step 4.2 volume 挂载信息写入 MetaServer

此外，还会把这个挂载信息同步到 JuiceFS 的 MetaServer，这里用的是 TiKV，暂不展开：

<p align="center"><img src="/assets/img/k8s-juicefs-csi/juicefs-pod-setup-workflow.png" width="100%"/></p>
<p align="center">Fig. JuiceFS as K8s CSI solution: workflow when a business pod is created (JuiceFS mountpod mode).</p>

### Step 4.3 JuiceFS plugin：如果 client pod 不存在，就创建一个

JuiceFS CSI plugin 判断这个 PV 在 node 上是否已经存在 client pod，如果不存在，就创建一个；存在就不用再创建了。

> 当 node 上最后一个使用某 PV 的业务 pod 销毁后，对应的 client pod 也会被 juicefs CSI plugin 自动删掉。

我们这个环境用的是 dynamic client pod 方式，因此会看到如下日志：

```shell
(node) $ docker logs --timestamps <csi plugin container> | grep 
...
10:05:57.884 createOrAddRef: Need to create pod juicefs-node1-juicefs-volume1-pv.
10:05:57.891 createOrAddRed: GetMountPodPVC juicefs-volume1-pv, err: %!s(<nil>)
10:05:57.891 ceMount: mount tikv://node1:2379,node2:2379,node3:2379/juicefs-volume1 at /jfs/juicefs-volume1-pv
10:05:57.978 createOrUpdateSecret: juicefs-node1-juicefs-volume1-pv-secret, juicefs-system
10:05:59.500 waitUtilPodReady:
```

JuiceFS node plugin 会去 k8s 里面创建一个名为 **<mark><code>juicefs-{node}-{volume}-pv</code></mark>** 的 dynamic client pod。

<p align="center"><img src="/assets/img/k8s-juicefs-csi/juicefs-pod-setup-workflow.png" width="100%"/></p>
<p align="center">Fig. JuiceFS as K8s CSI solution: workflow when a business pod is created (JuiceFS mountpod mode).</p>

## Step 5：kubelet 监听到 client pod 创建事件

这时候 kubelet 的**<mark>业务 pod</mark>** 还没创建好，“伺候”它的 **<mark><code>juicefs client pod</code></mark>**
又来“请求创建”了：

```shell
(node) $ grep juicefs-<node>-<volume>-pv /var/log/kubernetes/kubelet.INFO | grep "received "
10:05:58.288 SyncPod received new pod "juicefs-node1-volume1-pv_juicefs-system", will create a sandbox for it
```

所以接下来进入创建 juicefs dynamic client pod 的流程。

> 兵马未动，粮草先行。juicefs client pod 没有好，**<mark>业务 pod 即使起来了也不能读写 juicefs volume</mark>**。

## Step 6：kubelet 创建 client pod

创建 client pod 的流程跟业务 pod 是类似的，但这个 pod 比较简单，我们省略细节，认为它直接就拉起来了。

查看这个 client pod 内**<mark>运行的进程</mark>**：

```shell
(node) $ dk top k8s_jfs-mount_juicefs-node1-juicefs-volume1-pv-xx
/bin/mount.juicefs ${metaurl} /jfs/juicefs-volume1-pv -o enable-xattr,no-bgjob,allow_other,token=xxx,metrics=0.0.0.0:9567
```

**<mark><code>/bin/mount.juicefs</code></mark>** 其实只是个 alias，指向的就是 `juicefs` **<mark>可执行文件</mark>**，

```
(pod) $ ls -ahl /bin/mount.juicefs
/bin/mount.juicefs -> /usr/local/bin/juicefs
```

## Step 7：client pod 初始化、FUSE 挂载

查看这个 client pod 干了什么：

```shell
root@node:~  # dk top k8s_jfs-mount_juicefs-node1-juicefs-volume1-pv-xx
<INFO>: Meta address: tikv://node1:2379,node2:2379,node3:2379/juicefs-volume1
<INFO>: Data use oss://<oss-bucket>/juicefs-volume1/
<INFO>: Disk cache (/var/jfsCache/<id>/): capacity (10240 MB), free ratio (10%), max pending pages (15)
<INFO>: Create session 667 OK with version: admin-1.2.1+2022-12-22.34c7e973
<INFO>: listen on 0.0.0.0:9567
<INFO>: Mounting volume juicefs-volume1 at /jfs/juicefs-volume1-pv ...
<INFO>: OK, juicefs-volume1 is ready at /jfs/juicefs-volume1-pv
```

1. 初始化本地 volume 配置
2. 与 MetaServer 交互
3. 暴露 prometheus metrics
4. 以 juicefs 自己的 mount 实现（前面看到的 `/bin/mount.juicefs`），将 volume
   挂载到 `/jfs/juicefs-volume1-pv`，默认对应的是
   **<mark><code>/var/lib/juicefs/volume/juicefs-volume1-pv</code></mark>**。

此时在 node 上就可以看到如下的**<mark>挂载信息</mark>**：

```shell
(node) $ cat /proc/mounts | grep JuiceFS:juicefs-volume1
JuiceFS:juicefs-volume1 /var/lib/juicefs/volume/juicefs-volume1-pv fuse.juicefs rw,relatime,user_id=0,group_id=0,default_permissions,allow_other 0 0
JuiceFS:juicefs-volume1 /var/lib/k8s/kubelet/pods/<pod-id>/volumes/kubernetes.io~csi/juicefs-volume1-pv/mount fuse.juicefs rw,relatime,user_id=0,group_id=0,default_permissions,allow_other 0 0
```

可以看到是 **<mark><code>fuse.juicefs</code></mark>** 方式的挂载。
忘了 FUSE 基本工作原理的，再来借 lxcfs 快速回忆一下：

<p align="center"><img src="/assets/img/linux-container-and-runtime/lxcfs-fuse.png" width="80%" height="80%"></p>
<p align="center">Fig. lxcfs/fuse workflow: how a read operation is handled [2]</p>

这个 dynamic client pod 创建好之后，
**<mark>业务 pod（此时还不存在）的读写操作</mark>**都会进入 FUSE 模块，
然后转发给用户态的 juicefs client 处理。juicefs client 针对不同的 object store 实现了对应的读写方法。

## Step 8：kubelet 创建业务 pod：完成后续部分

至此，Pod 所依赖的 volumes 都处理好了，kubelet 就会打印一条日志：

```shell
# kubelet.INFO
10:06:06.119  All volumes are attached and mounted for pod "pod1(<pod1-id>)"
```

接下来就可以**<mark>继续创建业务 pod</mark>** 了：

```
# kubelet.INFO
10:06:06.119  No sandbox for pod "pod1(<pod1-id>)" can be found. Need to start a new one
10:06:06.119  Creating PodSandbox for pod "pod1(<pod1-id>)"
10:06:06.849  Created PodSandbox "885c3a" for pod "pod1(<pod1-id>)"
...
```

## 小结

更详细的 pod 创建过程，可以参考 [1]。

# 3 业务 pod 读写 juicefs volume 流程

juicefs dynamic client pod 先于业务 pod 创建，所以业务 pod 创建好之后，就可以直接读写 juicefs PV (volume) 了，

<p align="center"><img src="/assets/img/k8s-juicefs-csi/juicefs-pod-read-write-workflow.png" width="100%"/></p>
<p align="center">Fig. JuiceFS as K8s CSI solution: workflow when a business pod reads/writes (JuiceFS mountpod mode).</p>

这个过程可以大致分为四步。

## Step 1：pod 读写文件（R/W operations）

例如在 pod 内进入 volume 路径（e.g. `cd /data/juicefs-pv-dir/`），执行 ls、find 等等之类的操作。

## Step 2：R/W 请求被 FUSE 模块 hook，转给 juicefs client 处理

直接贴两张官方的图略作说明 [3]，这两张图也透露了随后的 step 3 & 4 的一些信息：

读操作：

<p align="center"><img src="/assets/img/k8s-juicefs-csi/juicefs-internals-read.png" width="80%"/></p>
<p align="center">Fig. JuiceFS Internals: read operations.</p>

写操作：

<p align="center"><img src="/assets/img/k8s-juicefs-csi/juicefs-internals-write.png" width="80%"/></p>
<p align="center">Fig. JuiceFS Internals: write operations.</p>

## Step 3：juicefs client pod 从 meta server 读取（文件或目录的）元数据

上面的图中已经透露了一些 JuiceFS 的元数据设计，例如 chunk、slice、block 等等。
读写操作时，client 会与 MetaServer 有相关的元信息交互。

## Step 4：juicefs client pod 从 object store 读写文件

这一步就是去 S3 之类的 object store 去读写文件了。

# 4 总结

以上就是使用 JuiceFS 作为 k8s CSI plugin 时，创建一个带 PV 的 pod 以及这个 pod 读写 PV 的流程。
限于篇幅，省略了很多细节，感兴趣的可移步参考资料。

# 参考资料

1. [源码解析：K8s 创建 pod 时，背后发生了什么（系列）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-1-zh.md %})
2. [Linux 容器底层工作机制：从 500 行 C 代码到生产级容器运行时（2023）]({% link _posts/2023-12-27-linux-container-and-runtime-zh.md %})
3. [官方文档：读写请求处理流程](https://juicefs.com/docs/zh/community/internals/io_processing/), juicefs.com
4. [kubernetes-csi.github.io/docs/](https://kubernetes-csi.github.io/docs/), K8s CSI documentation

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
