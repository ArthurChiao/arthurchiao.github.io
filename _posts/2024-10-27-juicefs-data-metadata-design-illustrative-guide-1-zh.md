---
layout    : post
title     : "直观解读 JuiceFS 的数据和元数据设计（一）：看山是山（2024）"
date      : 2024-10-27
lastupdate: 2024-10-27
categories: storage juicefs
---

本系列分为三篇文章，试图通过简单的实地环境来**<mark>直观理解</mark>** JuiceFS
的**<mark>数据（data）和元数据（metadata）</mark>**设计。

<p align="center"><img src="/assets/img/juicefs-data-metadata-design/minio-bucket-after-juicefs-format.png" width="80%"/></p>
<p align="center">Fig. MinIO bucket browser: one object was created (<mark><code>{volume}/juicefs_uuid</code></mark>) on a new juicefs volume creation.</p>

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

* [直观解读 JuiceFS 的数据和元数据设计（一）：看山是山（2024）]({% link _posts/2024-10-27-juicefs-data-metadata-design-illustrative-guide-1-zh.md %})
* [直观解读 JuiceFS 的数据和元数据设计（二）：看山不是山（2024）]({% link _posts/2024-10-27-juicefs-data-metadata-design-illustrative-guide-2-zh.md %})
* [直观解读 JuiceFS 的数据和元数据设计（三）：看山还是山（2024）]({% link _posts/2024-10-27-juicefs-data-metadata-design-illustrative-guide-3-zh.md %})

----

* TOC
{:toc}

----

本篇首先快速了解下 JuiceFS 架构和组件，然后将搭建一个极简 JuiceFS 集群，
并以 JuiceFS 用户的身份来体验下它的基本功能。

# 1 JuiceFS 高层架构与组件

JuiceFS 的高层架构和组件，

<p align="center"><img src="/assets/img/juicefs-metadata-deep-dive/juicefs-tikv-cluster.png" width="90%"/></p>
<p align="center">Fig. JuiceFS cluster initialization, and how POSIX file operations are handled by JuiceFS.</p>

三大组件：

1. 元数据引擎：存储文件元数据，例如文件名、权限等。JuiceFS 支持多种元数据引擎，比如 TiKV、sqlite、redis 等。
2. 对象存储：存储文件本身。JuiceFS 支持多种对象存储，比如 MinIO、AWS S3、阿里云 OSS 等。
3. JuiceFS 客户端：将 JuiceFS volume 挂载到机器上，提供文件系统视图给用户。

更多架构信息，见 [1]。

# 2 搭建极简 JuiceFS 集群

接下来搭建一个极简 JuiceFS 环境，方便我们做一些功能测试。
按上一节提到的，只需要搭建以下 3 个组件：

1. 元数据引擎，这里我们用 **<mark><code>TiKV</code></mark>**；
2. 对象存储，这里我们用 **<mark><code>MinIO</code></mark>**；
3. JuiceFS 客户端。

## 2.1 搭建元数据集群

对于功能测试来说，使用哪种元数据引擎都无所谓，比如最简单的 sqlite 或 redis。

不过，本系列第二篇会介绍 TiKV 相关的一些设计，所以本文用的 TiKV 集群作为元数据引擎，
相关的搭建步骤见社区文档。

本篇假设搭建的是三节点的 TiKV 集群，IP 地址分别是 `192.168.1.{1,2,3}`。

## 2.2 搭建对象存储（MinIO）

这里我们用 MinIO 搭建一个对象存储服务，主要是空集群**<mark>方便观察其中的文件变化</mark>**。

### 2.2.1 启动 MinIO server
 
[MinIO](https://github.com/minio/minio) 是一个兼容 S3 接口的开源对象存储产品，部署非常简单，就一个可执行文件，下载执行就行了。

也可以用容器，一条命令启动：

```shell
$ sudo docker run -p 9000:9000 -p 8080:8080 \
    quay.io/minio/minio server /data --console-address "0.0.0.0:8080"
```

访问 http://localhost:8080/ 就能看到 MinIO 的管理界面了。默认账号密码都是 **<mark><code>minioadmin</code></mark>**。

### 2.2.2 创建 bucket

通过 MinIO 管理界面创建一个 bucket，这里我们命名为 `juicefs-bucket`，

<p align="center"><img src="/assets/img/juicefs-data-metadata-design/minio-empty-bucket.png" width="100%"/></p>
<p align="center">Fig. MinIO bucket list: an empty bucket.</p>

可以看到现在里面一个对象也没有，已使用空间也是 **<mark>0 字节</mark>**。

## 2.3 下载 `juicefs` 客户端

从 https://github.com/juicedata/juicefs/releases 下载一个可执行文件就行了，

```shell
$ wget https://github.com/juicedata/juicefs/releases/download/v1.2.1/juicefs-1.2.1-linux-amd64.tar.gz
$ tar -xvf juicefs-1.2.1-linux-amd64.tar.gz
$ chmod +x juicefs
```

## 2.4 创建 JuiceFS volume

接下来就可以创建一个 JuiceFS volume 了，这里命名为 **<mark><code>foo-dev</code></mark>**。

### 2.4.1 创建/格式化 volume：`juicefs format`

```shell
$ juicefs format --storage minio --bucket http://localhost:9000/juicefs-bucket \
        --access-key minioadmin \
        --secret-key minioadmin \
        tikv://192.168.1.1:2379,192.168.1.2:2379,192.168.1.3:2379/foo-dev  \
        foo-dev

<INFO>: Meta address: tikv://192.168.1.1:2379,192.168.1.2:2379,192.168.1.3:2379/foo-dev [interface.go:504]
<INFO>: Data use minio://localhost:9000/juicefs-bucket/foo-dev/ [format.go:528]
<INFO>: Volume is formatted as {
  "Name": "foo-dev",
  "UUID": "3b4e509b-a7c8-456f-b726-cb8395cf8eb6",
  "Storage": "minio",
  "Bucket": "http://localhost:9000/juicefs-bucket",
  "AccessKey": "minioadmin",
  "SecretKey": "removed",
  "BlockSize": 4096,
  "UploadLimit": 0,
  "DownloadLimit": 0,
  ...
}
```

### 2.4.2 查看 MinIO bucket：多了一个 `juicefs_uuid` 文件

再查看 MinIO bucket，会发现多了一个 object，

<p align="center"><img src="/assets/img/juicefs-data-metadata-design/minio-bucket-after-juicefs-format.png" width="80%"/></p>
<p align="center">Fig. MinIO bucket browser: one object was created on a new juicefs volume creation.</p>

点进去，发现是一个叫 **<mark><code>juicefs_uuid</code></mark>** 的文件，

<p align="center"><img src="/assets/img/juicefs-data-metadata-design/minio-bucket-after-juicefs-format-1.png" width="100%"/></p>
<p align="center">Fig. MinIO bucket browser: one object was created after juicefs format.</p>

可以把这个文件下载下来，其内容就是上面 **<mark><code>juicefs format</code></mark>**
命令输出的 `uuid` 信息，也就是说 juicefs client 会把 volume 的 uuid 上传到对象存储中。

# 3 将 JuiceFS volume 挂载到本地路径

这么我们将这个 volume 挂载到本地路径 `/tmp/foo-dev`，

```shell
$ ./juicefs mount --debug --backup-meta 0 \
     tikv://192.168.1.1:2379,192.168.1.2:2379,192.168.1.3:2379/foo-dev /tmp/foo-dev

[INFO] [client.go:405] ["[pd] create pd client with endpoints"] [component=tikv] [pid=2881678] [pd-address="[192.168.1.1:2379,192.168.1.2:2379,192.168.1.3:2379]"]
[INFO] [base_client.go:378] ["[pd] switch leader"] [component=tikv] [pid=2881678] [new-leader=https://192.168.1.3:2379] [old-leader=]
[INFO] [base_client.go:105] ["[pd] init cluster id"] [component=tikv] [pid=2881678] [cluster-id=7418858894192002550]
[INFO] [client.go:698] ["[pd] tso dispatcher created"] [component=tikv] [pid=2881678] [dc-location=global]
<INFO>: Data use minio://localhost:9000/juicefs-bucket/foo-dev/ [mount.go:650]
...
```

进入目录：

```shell
$ cd /tmp/foo-dev
$ ls -ahl
-r--------  1 root root    0 Oct 26 10:45 .accesslog
-r--------  1 root root 2.9K Oct 26 10:45 .config
-r--r--r--  1 root root    0 Oct 26 10:45 .stats
dr-xr-xr-x  2 root root    0 Oct 26 10:45 .trash
```

可以看到几个隐藏文件，

* 这些是 JuiceFS 的元数据文件，在 [1] 系列文章中有过详细介绍。
* 这些都是 volume 本地文件，**<mark>不会上传到 MinIO</mark>**。此时，MinIO `juicefs-bucket` 里面还是只有一个 uuid 文件。

# 4 在 JuiceFS volume 挂载的本地路径内读写

接下来进行一些 POSIX 操作测试。

## 4.1 创建和写入文件

创建三个文件，一个只有**<mark>几十字节</mark>**（但命名为 file1_1KB），
一个 **<mark><code>5MB</code></mark>**，一个 **<mark><code>129MB</code></mark>**，

```shell
$ cd /tmp/foo-dev

$ echo "Hello, JuiceFS!" > file1_1KB

$ dd if=/dev/zero of=file2_5MB bs=1M count=5
5+0 records in
5+0 records out
5242880 bytes (5.2 MB, 5.0 MiB) copied, 0.0461253 s, 114 MB/s

$ dd if=/dev/zero of=file3_129MB bs=1M count=129
129+0 records in
129+0 records out
135266304 bytes (135 MB, 129 MiB) copied, 0.648757 s, 209 MB/s
```

## 4.2 查看文件属性

```shell
$ ls -ahl file*
-rw-r----- 1 root root   16  file1_1KB
-rw-r----- 1 root root 5.0M  file2_5MB
-rw-r----- 1 root root 129M  file3_129MB

$ file file2_5MB
file2_5MB: data
```

## 4.3 读取和追加文件

```shell
$ cat file1_1KB
Hello, JuiceFS!

$ echo "Hello, JuiceFS!" >> file1_1KB
$ cat file1_1KB
Hello, JuiceFS!
Hello, JuiceFS!
```

## 4.4 查找文件

```shell
$ find /tmp -name file1_1KB
/tmp/foo-dev/file1_1KB
```

## 4.5 删除文件

直接用 rm 删除就行了，不过这几个文件我们还有用，先不删。

## 4.6 目录操作

目录的创建、移动、修改权限、删除等待也是一样的，大家可以自己试试，这里不再赘述。

## 4.7 小结

根据以上测试，在 JuiceFS 挂载路径里创建/读写/查找/删除文件，都**<mark>跟本地目录没什么区别</mark>** ——
这也正是「分布式**<mark>“文件系统”</mark>**」的意义所在 —— 兼容 POSIX 语义，用户无需关心数据存在哪，
当本地目录使用就行了（性能另当别论）。

# 5 总结

本篇中，我们作为 JuiceFS 用户对它进行了一些最基本的功能测试，结论是和本地文件系统没什么区别。

对于普通**<mark>用户</mark>**来说，了解到这一层就够了；
但对于高阶用户以及 JuiceFS 的开发/运维来说，这只是表象，必有第二重境界等着他们。

# 参考资料

1. [JuiceFS 元数据引擎初探：高层架构、引擎选型、读写工作流（2024）]({% link _posts/2024-09-12-juicefs-metadata-deep-dive-1-zh.md %})

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
