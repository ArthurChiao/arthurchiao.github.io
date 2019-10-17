---
layout: post
title:  "Building Ceph From Source"
date:   2018-11-27
author: ArthurChiao
categories: ceph
---

Ceph is complex, so is compiling and building it from source.

In this post, we will show how to setup a containerized Ceph dev environment,
and build Ceph installing packages (RPM) with it.
This may be helpful if you want to build your own Ceph distribution.

Recommended host configurations:

1. CPU: 4+ cores
1. Memory: 16GB+

Note that the total building process may exceed **2 hours**, most of which are
spent on installing dependent libraries, cloning submodules, and compiling
source code. So better prepare yourself with a cup of coffee. Let's start.

## 1 Prepare Containerized Env (~25 min)

Dependency problems always lead to a disaster.
So in this post, we will launch a fresh new container as our develop workplace,
which will never pollute your host environment.

### 1.1 Pull CentOS Image (~5 min)

Pull a CentOS 7.2 base image from docker hub, or any mirror you like:

```shell
$ sudo docker pull library/centos:7.2.1511

# or pull from hub mirrors and tag it for convenience
$ sudo docker pull hub.c.163.com/library/centos:7.2.1511
$ sudo docker tag hub.c.163.com/library/centos:7.2.1511 library/centos:7.2.1511

$ sudo docker images
library/centos  7.2.1511    feac5e0dfdb2   2 years ago   194.6 MB
```

### 1.2 Clone Ceph Code (~5 min)

Clone source code and checkout a version, in this post we will use `v12.2.8`:

```shell
$ git clone https://github.com/ceph/ceph.git
$ cd ceph
$ git fetch --all --tags --prune
$ git checkout -b v12.2.8 tags/v12.2.8
```

Start container:

```shell
$ docker run -d --name ceph-dev -v $(pwd):/ceph library/centos:7.2.1511 sleep 3600d
```

Note that we mounted the source code dir to container.

### 1.3 Update YUM Repo (~15 min)

A fast yum repo will significantly speedup package installation. E.g. if you
are in China, consider [mirror from 163.com](http://mirrors.163.com/.help/centos.html).
Or, if you are connected to corp-internal network and your corp has private yum repos, use that.

If not made any changes to the yum configuration (`/etc/yum.repos.d/*.repo`), it
will use the official CentOS repo.

Now, run:

```shell
$ sudo docker exec -it ceph-dev bash

[root@container] # yum update -y
```

Likely, 200+ packages will be installed, takes ~15 minutes.

## 2 Compile (~80 min)

### 2.1 Install Dependencies (~20 min)

```shell
[root@container] # cd /ceph && ./install-deps.sh
```

### 2.2 Generate Makefiles (~30 min)

Generate Makefiles:

```shell
[root@container] # yum install -y git
[root@container] # ./do_cmake.sh
```

This will recursively clone multiple submodules from github. If
you have internal mirrors of the repos, consider change the submodules URL to
speedup.

If the above failed because of some dependencies not found, consider updating
yum repo again to install the missing:

```shell
[root@container] # yum update -y
[root@container] # ./install-deps.sh

[root@container] # rm -rf build
[root@container] # ./do_cmake.sh
```

### 2.3 Compile (~30 min)

GCC compiles with single thread by default, but you could specify more threads
with `-j`, which will speedup the compiling process. But do not enable
more threads than CPU * 2.

```shell
[root@container] # cd build && make -j4
```

Note that the compiling needs a large mount of memory when use multiple threads,
e.g. my machine run out of 8GB memory with `-j4`. And the compiling takes a
a long time, according to my test, even with 8 threads (`-j8`) on a physical server,
it still took me ~30 minutes.

## 3 Build Package (~30 min)

Install build RPM tools:

```shell
[root@container] # yum install rpm-build rpmdevtools -y
[root@container] # rpmdev-setuptree
```

Build RPM:

```shell
[root@container] # ./make-srpm.sh
...
ceph-12.2.8-0.el7.src.rpm

[root@container] # rpmbuild --rebuild ceph-12.2.8-0.el7.src.rpm
```

Check the RPM packages:

```shell
[root@container] # ls ~/rpmbuild/RPMS/x86_64/
ceph-12.2.8-0.el7.x86_64.rpm            ceph-resource-agents-12.2.8-0.el7.x86_64.rpm
ceph-base-12.2.8-0.el7.x86_64.rpm       ceph-selinux-12.2.8-0.el7.x86_64.rpm
ceph-common-12.2.8-0.el7.x86_64.rpm     ceph-test-12.2.8-0.el7.x86_64.rpm
...
ceph-radosgw-12.2.8-0.el7.x86_64.rpm    librados2-12.2.8-0.el7.x86_64.rpm
```

## Reference

1. [Ceph Doc: BUILD CEPH](http://docs.ceph.com/docs/mimic/install/build-ceph/)
2. [163.com CentOS yum mirror](http://mirrors.163.com/.help/centos.html)
3. [163.com Docker Hub (in Chinese)](https://c.163.com/hub#/m/home/)
4. [Gist: Build Ceph RPM from Git source](https://gist.github.com/wido/0f812dd1dc345cfbd5c38afb0b0dbb4b)
