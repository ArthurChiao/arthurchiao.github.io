## coverage
Function/code hotness statistics.

`ovs-appctl coverage/show`. Cool!

`lib/coverage.c`.

## connmgr - connection to OpenFlow Controller
 ofproto supports two kinds of OpenFlow connections:

   - "Primary" connections to ordinary OpenFlow controllers.  ofproto
     maintains persistent connections to these controllers and by default
     sends them asynchronous messages such as packet-ins.

   - "Service" connections, e.g. from ovs-ofctl.  When these connections
     drop, it is the other side's responsibility to reconnect them if
     necessary.  ofproto does not send them asynchronous messages by default.

 Currently, active (tcp, ssl, unix) connections are always "primary"
 connections and passive (ptcp, pssl, punix) connections are always "service"
 connections.  There is no inherent reason for this, but it reflects the
 common case.

`ofproto/connmgr.c`

## event loop implementation
poll() based.

run - wait - block

## netlink
communication between userspace and kernel.

## unixctl
vswitchd <---> unix sock <---> cli binary (ovs-vsctl, ovs-ofctl, ovs-appctl, etc)

## 4. unixctl server run
The unixctl server in ovs receives control commands that
you typed in shell (`ovs-appctl <xxx>`). It opens a unix socket, and listens
on it. Typically, the socket file is located at `/var/run/openvswitch/`, and
one socket file for each bridge:

```shell
$ ll /var/run/openvswitch
total 8.0K
drwxr-xr-x  2 root root  220 Dec 19 16:56 ./
drwxr-xr-x 35 root root 1.1K Dec 21 16:58 ../
srwxr-x---  1 root root    0 Dec 19 16:56 br-bond.mgmt=
srwxr-x---  1 root root    0 Dec 19 16:56 br-bond.snoop=
srwxr-x---  1 root root    0 Dec 19 16:56 br-int.mgmt=
srwxr-x---  1 root root    0 Dec 19 16:56 br-int.snoop=
srwxr-x---  1 root root    0 Dec 19 16:56 db.sock=
srwxr-x---  1 root root    0 Dec 19 16:56 ovsdb-server.63347.ctl=
-rw-r--r--  1 root root    6 Dec 19 16:56 ovsdb-server.pid
srwxr-x---  1 root root    0 Dec 19 16:56 ovs-vswitchd.63357.ctl=
-rw-r--r--  1 root root    6 Dec 19 16:56 ovs-vswitchd.pid
```

## CLI tools
in `lib/`.
