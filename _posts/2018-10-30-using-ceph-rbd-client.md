---
layout: post
title:  "Using Ceph RBD Client"
date:   2018-10-30
categories: ceph
---

Using ceph rbd client is quite simple, with just three steps:

1. Install ceph rbd client utilities
2. Configure ceph client
3. Run rbd commands

## 1. Install Ceph Utilities

For centos, one only needs to install `ceph-common` packge:

```
$ yum install -y ceph-common
```

Test installation:

```
$ rbd -v
ceph version 12.2.4
```

## 2. Configure Ceph Client

To communicate with ceph cluster, you need a `ceph.conf` and `ceph.client.admin.keyring`.
Ask your ceph cluster's administrator for the two files and put them to `/etc/ceph/`, actually, they are already placed on ceph monitor nodes.

Test configuration, list all images:

```
$ rbd list
image1
image2
image3
```

If ok, you can run other rbd commands, get more info with `rbd -h`.

## 3. Misleading Error: `FAILED assert(crypto_context != __null)`

A painful error I experienced when setting the rbd client: 

```
$ rbd list
common/ceph_crypto.cc: In function 'void ceph::crypto::init(CephContext*)' thread 7f91ba8817c0 time 2018-10-30 16:23:09.004576
common/ceph_crypto.cc: 73: FAILED assert(crypto_context != __null)
 ceph version 0.94.7 (d56bdf93ced6b80b07397d57e3fa68fe68304432)
 1: (ceph::__ceph_assert_fail(char const*, char const*, int, char const*)+0x85) [0x506c95]
 2: (ceph::crypto::init(CephContext*)+0x103) [0x514553]
 3: (CephContext::init_crypto()+0x9) [0x50aaa9]
 4: (common_init_finish(CephContext*, int)+0x10) [0x51d440]
 5: (main()+0x3fc) [0x4c11ac]
 6: (__libc_start_main()+0xf5) [0x7f91b38f8445]
 7: rbd() [0x4c9549]
 NOTE: a copy of the executable, or `objdump -rdS <executable>` is needed to interpret this.
2018-10-30 16:23:09.005005 7f91ba8817c0 -1 common/ceph_crypto.cc: In function 'void ceph::crypto::init(CephContext*)' thread 7f91ba8817c0 time 2018-10-30 16:23:09.004576
common/ceph_crypto.cc: 73: FAILED assert(crypto_context != __null)
```

Google `FAILED assert(crypto_context != __null)` gives plenty of pages, but most of the solutions don't work. I spent 4 hours on this problem with all kinds of tryings [1], until met [this post on twitter](https://twitter.com/zaitcev/status/695837599219343360) [2]:

```
/q/zaitcev/ceph/ceph-tip/src/common/ceph_crypto.cc: 73: FAILED assert(crypto_context != __null) means "typo in ceph.conf"
```

**With a quick modification of the `/etc/ceph/ceph.conf`: the error gone!**
It turns out the crash has nothing to do with crypto libraries, which is so misleading!

Error configurations:

```
[global]
fsid = 34623445-3c22-47d0-ab69-b44427642abf
mon_initial_members = node-1,node-2,node-3
mon_host = 192.168.1.2,192.168.1.3,192.168.1.4
auth_cluster_required = cephx
auth_service_required = cephx
auth_client_required = cephx

[client]
rgw_frontends = civetweb port=8080
rgw_swift_url = http://192.168.1.2
...
```

Correct configurations:

```
[global]
fsid = 34623445-3c22-47d0-ab69-b44427642abf
mon_initial_members = node-1,node-2,node-3
mon_host = 192.168.1.2,192.168.1.3,192.168.1.4
auth_cluster_required = cephx
auth_service_required = cephx
auth_client_required = cephx

```

# 4. Summary

Ceph configurations on RBD client nodes should be carefully checked, if there are unexpected settings in `/etc/ceph/ceph.conf`, ceph will crash with misleading errors.

# References

1. https://github.com/haiwen/seafile/issues/1720
2. https://twitter.com/zaitcev/status/695837599219343360
