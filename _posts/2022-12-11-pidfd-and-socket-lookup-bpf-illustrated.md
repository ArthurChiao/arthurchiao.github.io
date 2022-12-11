---
layout    : post
title     : "Pidfd and Socket-lookup BPF (SK_LOOKUP) Illustrated (2022)"
date      : 2022-12-11
lastupdate: 2022-12-11
categories: bpf socket pidfd
---

### TL; DR

Most unix programming text books as well as practices hold the following statements to be true:

1. **<mark>One socket</mark>** could be opened by **<mark>one and only one process</mark>** (application);
1. **<mark>One socket</mark>** could listen/serve on **<mark>one and only one port</mark>**;

    Recall the `bind` system call
   `int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)` where
   `addr` is determined by `IP+Port` (and socket address family).

However, with some advanced techniques like **<mark><code>pidfd_getfd()</code></mark>**
system call in Linux kernel `5.4+` and **<mark><code>SK_LOOKUP</code></mark>** BPF in
kernel `5.6+`, we could easily break the above limitations, supporting scenarios like below:

```
       +-----------+  +-----------+  +----------+                              +------------------+
       | Process 1 |  | Process 2 |  | Process 3|                              |   Process(app)   |
       |           |  |           |  |          |                              |                  |
       |  SockFD1  |  |  SockFD2  |  |  SockFD3 |                              |     SockFD       |
       +-----------+  +-----------+  +----------+                              +------------------+
                \         |         /                                                   |
                +--------------------+                                         +------------------+
                |       Socket       |                                         |     Socket       |
                +--------------------+                                         +------------------+
                          |                                                    /        |         \
                +--------------------+                                 +--------+  +---------+  +----------+
                |       TCP@80       |                                 |  TCP@7 |  | TCP@77  |  | TCP@777  |
                +--------------------+                                 +--------+  +---------+  +----------+
                          |                                                     \       |         /
                +--------------------+                                          +------------------+
                |      ServerIP      |                                          |    ServerIP      |
                +--------------------+                                          +------------------+
                          /\                                                            /\
                          ||                                                            ||
                       requests                                                      requests

                       Scenario 1:                                                  Scenario 2:
  Multiple processes serve requests over the same socket.         Single socket listens/serves on multiple ports.
  E.g. Three HTTP servers share the same TCP@localhost:80         E.g. one socket servers on TCP@localhost
  socket.                                                         {:6, :66, :666} simultaneously.
```

This post explains the underlying working mechanism of the `SK_LOOKUP` BPF,
and provides example codes based on [cilium/ebpf](https://github.com/cilium/ebpf)
library, which has minimal dependencies and doesn't require header files to be installed.

Demo codes in this post: [github.com/arthurchiao/pidfd-and-sk-lookup-bpf-illustrated](https://github.com/arthurchiao/pidfd-and-sk-lookup-bpf-illustrated).

----

* TOC
{:toc}

----

# 1 Introduction

This post explains the underlying working mechanism of the `SK_LOOKUP` BPF.
We'll have several examples to implement the following scenarios.

```
       +-----------+  +-----------+  +----------+                              +------------------+
       | Process 1 |  | Process 2 |  | Process 3|                              |   Process(app)   |
       |           |  |           |  |          |                              |                  |
       |  SockFD1  |  |  SockFD2  |  |  SockFD3 |                              |     SockFD       |
       +-----------+  +-----------+  +----------+                              +------------------+
                \         |         /                                                   |
                +--------------------+                                         +------------------+
                |       Socket       |                                         |     Socket       |
                +--------------------+                                         +------------------+
                          |                                                    /        |         \
                +--------------------+                                 +--------+  +---------+  +----------+
                |       TCP@80       |                                 |  TCP@7 |  | TCP@77  |  | TCP@777  |
                +--------------------+                                 +--------+  +---------+  +----------+
                          |                                                     \       |         /
                +--------------------+                                          +------------------+
                |      ServerIP      |                                          |    ServerIP      |
                +--------------------+                                          +------------------+
                          /\                                                            /\
                          ||                                                            ||
                       requests                                                      requests

                       Scenario 1:                                                  Scenario 2:
  Multiple processes serve requests over the same socket.         Single socket listens/serves on multiple ports.
  E.g. Three HTTP servers share the same TCP@localhost:80         E.g. one socket servers on TCP@localhost
  socket.                                                         {:6, :66, :666} simultaneously.
```

# 2 Multiple processes serving on the same socket

The first scenario: multiple processes listen and serve on exactly the same socket,
as depicted below:

```

       +-----------+  +-----------+  +----------+
       | Process 1 |  | Process 2 |  | Process 3|
       |           |  |           |  |          |
       |  SockFD1  |  |  SockFD2  |  |  SockFD3 |
       +-----------+  +-----------+  +----------+
                \         |         /
                +--------------------+
                |       Socket       |
                +--------------------+
                          |
                +--------------------+
                |       TCP@80       |
                +--------------------+
                          |
                +--------------------+
                |      ServerIP      |
                +--------------------+
                          /\
                          ||
                       requests
```

## 2.1 Background

The secret weapon to realize the above conception is **<mark><code>pidfd_getfd()</code></mark>**.
But, to understand `pidfd_getfd()` well, we need to start from **<mark><code>pidfd</code></mark>**.

Note that these two stuffs have nothing to do with BPF, but they've laied out
the foundation for SK_LOOKUP BPF, we'll see that in section 3.

### 2.1.1 `pidfd` and `pidfd_open()` system call (kernel `5.4+`)

A recap of the lwn.net article [Completing the pidfd API (2019)](https://lwn.net/Articles/794707/):

* Unix-like systems traditionally represent **<mark>objects as files</mark>**,
  but **<mark>processes have always been an exception</mark>**. They are, instead,
  **<mark>represented by process IDs</mark>** (integer `PID`).

    There are a few problems with this representation, the biggest one is
    that **<mark>PIDs are reused</mark>** and this can happen quickly, which creates
    a race condition where code that operates on a process (most often by sending
    it a signal) might end up **<mark>performing an action on the wrong process</mark>**.

* A pidfd is, instead, **<mark>a file descriptor that refers to an existing process</mark>**.
Once the pidfd exists, it will **<mark>only refer to that one process</mark>**,
so it can be used to send signals without worry that the wrong process might
end up being the recipient. This feature is valuable enough to some
process-management systems (e.g. the one used by Android).

To get the pidfd of a process, use the **<mark><code>pidfd_open()</code></mark>** system call:

```shell
$ man pidfd_open
NAME
       pidfd_open - obtain a file descriptor that refers to a process
SYNOPSIS
       #include <sys/types.h>
       int pidfd_open(pid_t pid, unsigned int flags);
DESCRIPTION
	   The  pidfd_open()  system  call creates a file descriptor that refers to
       the process whose PID is specified in pid.  The file descriptor is returned as
       the function result; the close-on-exec flag is set on the file descriptor.
RETURN VALUE
	   On success, pidfd_open() returns a nonnegative file descriptor.  On
       error, -1 is returned and errno is set to indicate the cause of the error.
```

With `pidfd` understood, let's move on to `pidfd_getfd()`.

### 2.1.2 `pidfd_getfd()` system call (kernel `5.6+`)

Again, a recap from the lwn.net article [Grabbing file descriptors with `pidfd_getfd()` (2020)](https://lwn.net/Articles/808997/):

* The kernel has already had several mechanisms of controlling groups of processes
  from user space that **<mark>allow one process to operate on another</mark>**.
  One piece that is currently missing is the ability for a process to
  **<mark>snatch a copy of an open file descriptor from another</mark>**.

    It is possible in legacy kernels to open a file that another process also has open,

    * The **<mark>information needed to do this</mark>** is in each process's
      `/proc` directory;
    * This does **<mark>not work for</mark>** file descriptors referring
      to **<mark>pipes, sockets</mark>**, or other objects that do not appear
      in the filesystem hierarchy;
    * Opening a new file in this way creates a new entry in the file
      table; it is not the entry corresponding to the file descriptor in the
      process of interest.

* The distinction matters if the objective is to **<mark>modify that particular file descriptor</mark>**.
  One use case is **<mark>using seccomp to intercept attempts to bind a socket</mark>** to a privileged port.

    * A privileged supervisor process could grab the file descriptor
      for that socket from the target process and actually perform the bind —
      something the target process would not have the privilege to do on its own.
    * Since the grabbed file descriptor is essentially identical to the original,
      the bind operation will be visible to the target process as well.

With the introduction of `pidfd_getfd`, this requirement can be met in an elegant way.
A supervisor process would merely need to make a
**<mark><code>pidfd_getfd()</code></mark>** system call:

```c
  int pidfd_getfd(int pidfd, int targetfd, unsigned int flags);
```

Example usage:

```c
  int target_pid = 12000; // target process ID
  int targetfd = 3;       // target file descriptor in target process
  int fd2 = pidfd_getfd(pidfd_open(target_pid), targetfd, flags); // fd2: file descriptor in the current process
                                                                  // which is a duplication of `targetfd`
```

This system call obtains a **<mark>duplicate of another process's file descriptor</mark>**.
In the above, `fd2` in current process is a duplicate of the `fd=3` in process `12000`.

From [man page](https://man7.org/linux/man-pages/man2/pidfd_getfd.2.html):

1. `fd2` refers to the **<mark>same open file description</mark>** as
   `targetfd`, so they **<mark>share file status flags and file offset</mark>**;
2. **<mark>Operations on the underlying file object</mark>** (e.g, assigning an address to
   a socket object using `bind(2)`) **<mark>can equally be performed via <code>fd2</code></mark>**.
3. The duplication operation is governed by a ptrace access mode
   **<mark><code>PTRACE_MODE_ATTACH_REALCREDS</code></mark>** check,
   so this permission is needed.

> Yama is a Linux Security Module that collects system-wide DAC security
> protections that are not handled by the core kernel itself. This is
> selectable at build-time with CONFIG_SECURITY_YAMA, and can be controlled
> at run-time through sysctls in `/proc/sys/kernel/yama`:
>
> * `0`: classic ptrace permissions;
> * `1`: restricted ptrace;
> * `2`: admin-only attach;
> * `3`: no attach. no processes may use ptrace with PTRACE_ATTACH nor via PTRACE_TRACEME. **<mark>Once set, this sysctl value cannot be changed</mark>**.
>
> See [Documentation/security/Yama.txt](https://www.kernel.org/doc/Documentation/security/Yama.txt) for more information.

In the next, let's see how this can solve some real world problems in an elegant way.

## 2.2 `share-socket`: a demo of multiple servers share the same socket (listen fd)

We'll write our demo code with golang, as it's easy to start HTTP servers with golang built-in modules.

### 2.2.1 Source code (golang)

The code is very simple, all the error handling code are omitted here:

```go
var pid = flag.Int("pid", 0, "Target PID")
var fd = flag.Int("fd", 0, "Target fd")

func main() {
	flag.Parse()

	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, "Response from process %d\n", os.Getpid())
	})

	listenAddr := ":8080"
	if *pid != 0 && *fd != 0 {
		// Start a server, listen & serve on a socket that already exists
		dupFdAndServe(*pid, *fd, handler)
	} else {
		// Start a server, open a new socket on the given `listenAddr`
		listenAndServe(listenAddr, handler)
	}
}
```

The program accepts two optional parameters `pid` and `fd`, which refers to
a specific socket in a process,

1. If the parameters are not provided, the program just **<mark>starts a simple HTTP server</mark>**; otherwise,
2. It will **<mark>duplicate that file descriptor then create a server listening on that duplicated fd</mark>**;
   effectively, this means serving on the same socket of the given `(pid,fd)`;

In both case, the HTTP server will reply with a message of `"Response from process <PID>"`
where `<PID>` is the process ID of the server instance that serving this request.
This is used to determine which server a response comes from.

The code to start a new server or serving on the duplicated file descriptor:

```go
// Start a normal http server
func listenAndServe(listenAddr string, handler http.HandlerFunc) {
	lc := net.ListenConfig{
		Control: func(network, address string, c syscall.RawConn) error {
			// Print listen FD and PID for later use
			c.Control(func(fd uintptr) { fmt.Printf("Listening on %s, fd=%d , pid=%d\n", listenAddr, fd, os.Getpid()) })
			return nil
		},
	}
	ln, _ := lc.Listen(context.Background(), "tcp", listenAddr)
	http.Serve(ln, http.HandlerFunc(handler))
}

// Start a http server by duplicating the given FD in the given process
func dupFdAndServe(targetPid int, targetFd int, handler http.HandlerFunc) {
	p, _ := pidfd.Open(targetPid, 0)
	listenFd, _ := p.GetFd(targetFd, 0)
	ln, _ := net.FileListener(os.NewFile(uintptr(listenFd), ""))

    fmt.Printf("Duplicated the given socket FD and listening on it")
	http.Serve(ln, http.HandlerFunc(handler))
}
```

The code uses a golang library which encapsulates the `pidfd_getfd()` system call.

Tha's all! Now compile the code:

```shell
$ cd share-socket && go build
```

### 2.2.2 Test

First, with no parameters provided, run the program and it will start a simple HTTP server:

```shell
$ ./share-socket
Listening on :8080, fd=3, pid=198004
```

The server's process ID is `198004`, and the TCP socket has an FD `3`. Now test it:

```shell
$ for n in {1..6}; do curl 127.0.0.1:8080; sleep 1; done
Response from process 198004
Response from process 198004
Response from process 198004
Response from process 198004
Response from process 198004
Response from process 198004
```

As expected, all responses comes from that process.

Now, we invoke the program again with the `pid` and `fd` printed:

```shell
$ sudo ./share-socket -fd 3 -pid 198004
Duplicated the given socket FD and listening on it, pid=198011
```

This time, it tells us that we've successfully
**<mark>copied the the socket's file descriptor and created a new server listening on it</mark>**,
too. Now test it:

```shell
$ for n in {1..6}; do curl 127.0.0.1:8080; sleep 1; done
Response from process 198004
Response from process 198004
Response from process 198011
Response from process 198011
Response from process 198004
Response from process 198004
```

As can be seen, responses come from the two servers (**<mark>share a same TCP socket</mark>**),
just as expected,

```
       +-------------+            +-------------+
       |  Server 1   |            |  Server 2   | // server2 = http.Serve(listen(fd2))
       |             |            |             |
       | pid1=198004 |            | pid2=198011 | //       pidfd_getfd(pidfd_open(target_pid), targetfd, flags)
       |  fd1=3      |            |  fd2=xx     | // fd2 = pidfd_getfd(pidfd_open(198004),     3,        0)
       +-------------+            +-------------+
                \                   /
                +--------------------+
                |       Socket       |
                +--------------------+
                          |
                +--------------------+
                |       TCP@8080     |
                +--------------------+
                          |
                +--------------------+
                |      ServerIP      |
                +--------------------+
                          /\
                          ||
                       requests
```

## 2.3 `graceful-upgrade`: a demo for graceful L4/L7 service upgrade

In the above `share-socket` example, we've seen how two, or in general,
**<mark>multiple services could listen and serve on the same socket</mark>**.
It's not hard to get the idea that with some additional control or management work, a
graceful upgrade mechanism could be realized. The hypothetical graceful
upgrade procedure:

1. One or multiple instances serve on a socket, e.g. `0.0.0.0:80`;
2. Sing signal to old instances;
3. On receiving kill signals, an old instance will:

    1. Start a new process to run the new code or new configuration, but still
       serve on the same socket as before, by duplicating the socket file
       descriptor;
    2. Terminate the old process after a graceful period.

In this section, we'll make such a demo.

### 2.3.1 Source code (golang)

The skeleton code is much the same as `share-socket`, with the signal handling
coded added as below:

```go
func (s *Server) handleUpgradeSignal() {
	upgradeCh := make(chan os.Signal, 1)
	signal.Notify(upgradeCh, syscall.SIGHUP)

	for {
		<-upgradeCh
		info("received SIGUP, going to upgrade")

		// Pass pid and listenfd to child process
		env := os.Environ()
		env = append(env, fmt.Sprintf("_PID=%d", os.Getpid()))
		env = append(env, fmt.Sprintf("_FD=%d", s.listenFd))

		cmd := exec.Command(os.Args[0], os.Args[1:]...)
		cmd.Env = env
		cmd.Stdout = os.Stdout // inherit stdout/stderr so we can see logs in the same window
		cmd.Stderr = os.Stderr

		if err := cmd.Start(); err != nil {
			fmt.Println(err)
			cmd.Wait()
		} else {
			info("start new instance (process) done, serving on the same socket")
		}
	}
}

func (s *Server) handleShutdownSignal() {
	shutdownCh := make(chan os.Signal, 1)
	signal.Notify(shutdownCh, syscall.SIGTERM, syscall.SIGINT)

	<-shutdownCh
	info("received SIGTERM/SIGINT, going to shutdown")

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := s.Shutdown(ctx); err != nil {
		info(fmt.Sprintf("fail to shutdown: %s", err))
	}
}
```

### 2.3.2 Test

Start a server in termal 1, acting as legacy instance:

```shell
$ sudo ./graceful-upgrade
[2022-12-10T21:17:26 PID=201363] server up, listening on :8080
```

The server has a PID=201363. Now test it:

```shell
$ for n in {1..4}; do curl 127.0.0.1:8080; sleep 1; done
Response from process 201363
Response from process 201363
Response from process 201363
Response from process 201363
```

Meet expectation. Now in another terminal, send a upgrade signal to the server:

```shell
$ sudo kill -HUP $(pidof graceful-upgrade)
```

Logs from the terminal 1 (add one blank line to distinguish logs before and
after receiving upgrade signal):

```shell
$ sudo ./graceful-upgrade
[2022-12-10T21:17:26 PID=201363] server up, listening on :8080

[2022-12-10T21:17:44 PID=201363] received SIGUP, going to upgrade
[2022-12-10T21:17:44 PID=201363] start new instance (process) done, serving on the same socket
[2022-12-10T21:17:44 PID=201385] duplicated the given socket FD and listening on it
[2022-12-10T21:17:49 PID=201385] 5 seconds have past since new instance up, going to stop the old server
[2022-12-10T21:17:49 PID=201363] received SIGTERM/SIGINT, going to shutdown
```

which shows that the upgrade has finished, which new instance PID=201385.
Test it again:

```shell
$ for n in {1..4}; do curl 127.0.0.1:8080; sleep 1; done
Response from process 201385
Response from process 201385
Response from process 201385
Response from process 201385
```

As expected, all responses came from the new instance (process).

Clean up:

```shell
$ sudo kill -9 $(pidof graceful-upgrade)
```

## 2.4 Summary

This ends the first half of our tour. As the title shows, this is an article about
BPF, but till now, we haven't see anything that relates to BPF - don't worry, it comes now.

# 3 Single process (socket) serving on multiple ports

In this section we'll turn to the second scenario: single socket serving on
multiple ports.  We'll have a demonstration like below:

```
                +------------------+
                |   Process(app)   |           <--- TCP echo server
                +------------------+
                         |
                +------------------+
                |     Socket       |           <---- Socket of the server
                +------------------+
                /        |         \
        +--------+  +---------+  +----------+
        | :7@TCP |  | :77@TCP |  | :777@TCP |  <---- Ports the socket listens & servers on
        +--------+  +---------+  +----------+
                 \       |         /
                 +------------------+
                 |    ServerIP      |          <---- Server IP
                 +------------------+
                         /\
                         ||
                      requests                 <---- Requests from client

                     Scenario 2:
        Single socket serving on multiple ports
```

To fulfill this task, we need a kind of BPF program called `BPF_PROG_TYPE_SK_LOOKUP`.
Besides, we also need the `pidfd_getfd()` to duplicate file descriptors.

## 3.1 `BPF_PROG_TYPE_SK_LOOKUP` program

Position of socket-level BPF in kernel networking stack:

```
                      +------------------+
                      | Socket           |
                      +------------------+ <-- Socket BPF, e.g. BPF_SK_LOOKUP
                      | L4 (TCP/UDP/...) |
                      +------------------+
                      | L3 (IP)          |
                      +------------------+  <-- TC BPF
                      | L2               |
                      +------------------+
```

A more detailed view of where the SK_LOOKUP gets triggered in kernel network stack:

<p align="center"><img src="/assets/img/birth-of-sk-lookup-bpf/bpf_inet_lookup_hook.png" width="100%" height="100%"></p>
<p align="center">Fig. Position of the SK_LOOKUP BPF. Image credit Cloudflare [2]</p>

From [kernel doc](https://www.kernel.org/doc/html/latest/bpf/prog_sk_lookup.html):

* BPF sk_lookup program type (BPF_PROG_TYPE_SK_LOOKUP)
  **<mark>introduces programmability into the socket lookup process performed by the transport layer</mark>**
  when a packet is to be delivered locally.
* When invoked it can **<mark>select a socket that will receive the incoming packet</mark>**
  by calling the **<mark><code>bpf_sk_assign()</code></mark>** BPF helper function.
* Hooks for a common attach point (BPF_SK_LOOKUP) exist for both TCP and UDP.

### 3.1.1 Motivation

BPF sk_lookup program type was introduced to address setup scenarios where binding sockets to an address with bind() socket call is impractical, such as:

1. receiving connections on a range of IP addresses, e.g. 192.0.2.0/24, when binding to a wildcard address INADRR_ANY is not possible due to a port conflict,
1. receiving connections on all or a wide range of ports, i.e. an L7 proxy use case.

Such setups would require creating and bind()’ing one socket to each of the IP address/port in the range, leading to resource consumption and potential latency spikes during socket lookup.

### 3.1.2 Attachment

BPF sk_lookup program can be **<mark>attached to a network namespace</mark>**
with **<mark><code>bpf(BPF_LINK_CREATE, ...)</code></mark>** syscall using the
BPF_SK_LOOKUP attach type and a netns FD as attachment target_fd.

Multiple programs can be attached to one network namespace. Programs will be invoked in the same order as they were attached.

### 3.1.3 Hooks

The attached BPF sk_lookup programs run whenever the transport layer needs to
**<mark>find a listening (TCP) or an unconnected (UDP) socket for an incoming packet</mark>**.

Incoming **<mark>traffic to established (TCP) and connected (UDP) sockets</mark>**
is delivered as usual **<mark>without triggering the BPF sk_lookup hook</mark>**.

Examples: [kernel selftests](https://github.com/torvalds/linux/blob/v5.10/tools/testing/selftests/bpf/prog_tests/sk_lookup.c).

## 3.2 Create a simple echo server

Playground information:

```shell
$ cat /etc/lsb-release
DISTRIB_ID=Ubuntu
DISTRIB_RELEASE=20.04
DISTRIB_CODENAME=focal
DISTRIB_DESCRIPTION="Ubuntu 20.04.5 LTS"

$ uname -r
5.15.0-52-generic
```

Our application will be a simple TCP server, which receives requests from client,
and echo the exact request content back to the client. 

### 3.2.1 Create server with `ncat/nc`

On ubuntu 20.04, there is no `-e` option support in `nc`, so we just use `ncat`:

```shell
$ sudo apt install ncat
```

Create an echo server, which will execute `cat` command on receiving a request
to send the contents back to clients:

```shell
$ ncat -4lke $(which cat) 127.0.0.1 7777
```

Check the socket's information:

```shell
# -t: Display only TCP sockets
# -l: Display only listening sockets
# -p: Show process using socket
# -n: Numeric output
# -e: Show detailed (extended) socket information. Format: uid:<uid_number> ino:<inode_number> sk:<cookie>
$ ss -4tlpne sport = 7777
State   Recv-Q  Send-Q  Local Address:Port  Peer Address:Port  Process
LISTEN  0       10      127.0.0.1:7777      0.0.0.0:*          users:(("ncat",pid=122701,fd=3)) uid:1000 ino:896838 sk:1 <->
```

* `uid_number`: the user id the socket belongs to (`1000` in the above output)
* `inode_number`: the socket's inode number in VFS (`896838` in the above output)
* `cookie`: an uuid of the socket (`1` in the above output)

### 3.2.2 Test accessing

Connect to the server with `nc` command, then send a `"hello"` string to server:

```shell
$ nc 127.0.0.1 7777
hello    # message we sent to server
hello    # message received from server
^C
```

Works as expected.

## 3.3 BPF program

### 3.3.1 Kernel/BPF code

Our BPF code is very simple.

* First, declare a BPF map to hold our configuration (ports)
* Then, declare a BPF_MAP_TYPE_SOCKMAP type BPF map to store the sockets

    IDs are allocated in a way that makes them suitable as an
    array index, which allows using the simpler BPF sockmap (an array) instead of a
    socket hash table. 

```c
// Hash table for storing listening ports. Hash key is the port number.
struct bpf_map_def SEC("maps") port_map = {
	.type        = BPF_MAP_TYPE_HASH,
	.max_entries = 1024,
	.key_size    = sizeof(__u16),
	.value_size  = sizeof(__u8),
};

// Hash table for storing sockets (socket pointers)
struct bpf_map_def SEC("maps") socket_map = {
	.type        = BPF_MAP_TYPE_SOCKMAP,
	.max_entries = 1,
	.key_size    = sizeof(__u32),
	.value_size  = sizeof(__u64),
};
```

Then, the processing logic, ~10 lines of code, this is the entire code:

```c
// Program for dispatching packets to sockets
SEC("sk_lookup/echo_dispatch")
int echo_dispatch(struct bpf_sk_lookup *ctx) {
	// Check if the given port is being served by a server
	__u16 port = ctx->local_port; // port expected to be listened on by server
	__u8 *open = bpf_map_lookup_elem(&port_map, &port);
	if (!open)          // NULL means not found,
		return SK_PASS; // we just let the packet go

	// There is a socket serving on the given port, now try to find it
	const __u32 key     = 0;
	struct bpf_sock *sk = bpf_map_lookup_elem(&socket_map, &key);
	if (!sk)            // socket not found, this is weired, user can choose
		return SK_DROP; // to drop the packet or let it go, here we just drop it

	// Dispatch the packet to the server socket
	long err = bpf_sk_assign(ctx, sk, 0);
	bpf_sk_release(sk); // Release the reference held by sk

	return err ? SK_DROP : SK_PASS;
}
```

There are already comments explain the code, we re-list them briefly:

1. When a packet reaches the TCP layer, the src_port and dst_port can be decoded from the TCP header;

    The kernel then try to determine if a socket is listening on the given
    dst_port; and at this time, sk_lookup bpf program is triggered, with a
    `struct bpf_sk_lookup ctx` object initialized and passed to the program; of
    all the parameters in the structure, the `ctx->local_port` stores the
    `dst_port`.  Enter the `echo_dispatch()` program.

2. BPF program checks if the given port is configured;

    If not: it's not our business, just the let the packet go, kernel will handle it correctly;
    If does: find the corresponding socket. Go to step 3.

3. Find the socket in our sockmap.

    On found, go to 4; on failed, drop the packet;

4. Try to assign the packet to the socket, then release the reference

    On success, let the packet go; on failure, drop the packet.

### 3.3.2 Userspace code

We need a little bit of golang code to load the BPF object into kernel, attach
to the hook point, and link the BPF program to a network namespace (netns).

```go
func main() {
	// Allow the current process to lock memory for eBPF resources.
	if err := rlimit.RemoveMemlock(); err != nil {
		log.Fatal(err)
	}

	// Load pre-compiled programs and maps into the kernel.
	objs := bpfObjects{}
	if err := loadBpfObjects(&objs, nil); err != nil {
		log.Fatalf("loading objects: %v", err)
	}
	defer objs.Close()

    // Link BPF prog to netns
	netns, err := os.Open("/proc/self/ns/net") // This can be a path to another netns as well.
	if err != nil {
		panic(err)
	}
	defer netns.Close()

	prog := objs.EchoDispatch
	link, err := link.AttachNetNs(int(netns.Fd()), prog)
	if err != nil {
		panic(err)
	}

    // Duplicate socket FD and store the socket to sockmap
	targetPid := 123602
	targetPidFd, err := pidfd.Open(targetPid, 0)
	if err != nil {
		panic(err)
	}

	targetFd := 3
	sockFd, err := targetPidFd.GetFd(targetFd, 0)
	if err != nil {
		panic(err)
	}

	var key uint32 = 0
	var val uint64 = uint64(sockFd)
	if err := objs.SocketMap.Put(&key, &val); err != nil {
		panic(err)
	}

	log.Println("Sleeping for some time ...")
	time.Sleep(6000 * time.Second)

	// The socket lookup program is now active until Close().
	link.Close()
}
```

### 3.3.3 Compile, load, attach

```shell
$ make

$ sudo ./steer-multi-ports
```

### 3.3.4 Configure with `bpftool map update`

Confirm 7/77/777 are available by scanning the opened ports in the system:

```shell
$ nmap -sT -p 1-1000 <node ip>
PORT    STATE SERVICE
22/tcp  open  ssh

Nmap done: 1 IP address (1 host up) scanned in 0.05 seconds
```

Start the userspace agent, which will automatically load and attach the BPF
program and BPF maps into kernel:

```shell
$ sudo ./steer-multi-ports
2022/12/10 21:21:52 Loading BPF objects ...
2022/12/10 21:21:52 Opening netns ...
2022/12/10 21:21:52 Linking BPF prog to netns ...
2022/12/10 21:21:52 Pinning BPF link to bpffs ...
2022/12/10 21:21:52 Duplicating socket FD ...
2022/12/10 21:21:52 Target PidFd ...  11
2022/12/10 21:21:52 Storing duplicated sockFD into sockmap ...  12
2022/12/10 21:21:52 Sleeping for some time ...
```

Check the loaded BPF programs and BPF maps in kernel:

```shell
$ sudo bpftool prog show
...
60: sk_lookup  name echo_dispatch  tag da043673afd29081
        loaded_at 2022-12-10T21:01:31+0800  uid 0
        xlated 272B  jited 159B  memlock 4096B  map_ids 6,7 # maps used by this program
        btf_id 104

$ sudo bpftool map show
6: hash  name port_map  flags 0x0
        key 2B  value 1B  max_entries 1024  memlock 8192B
7: sockmap  name socket_map  flags 0x0
        key 4B  value 8B  max_entries 1  memlock 4096B

$ sudo bpftool map dump id 6
Found 0 elements
$ sudo bpftool map dump id 7
Found 0 elements

$ sudo bpftool link show
3: netns  prog 64
        netns_ino 4026531840  attach_type sk_lookup
```

Start our server program:

```shell
$ ncat -4lke $(which cat) 127.0.0.1 7777 &
[1] 122701

$ ss -4tlpne sport = 7777
State    Recv-Q   Send-Q   Local Address:Port   Peer Address:Port   Process
LISTEN   0        10       127.0.0.1:7777       0.0.0.0:*           users:(("ncat",pid=122701,fd=3)) uid:1000 ino:896838 sk:1 <->
```

Insert 7/77/777 (hex format) into BPF map:

```shell
$ sudo bpftool map update id 81 key 0x07 0x00 value 0x00
$ sudo bpftool map update id 81 key 0x4d 0x00 value 0x00
$ sudo bpftool map update id 81 key 0x09 0x03 value 0x00

$ sudo bpftool map dump id 6
key: 09 03  value: 00
key: 07 00  value: 00
key: 4d 00  value: 00
Found 3 elements
```

### 3.3.5 Test

Scan the system ports again:

```shell
$ nmap -sT -p 1-1000 127.0.0.1
Nmap scan report for localhost (127.0.0.1)
PORT    STATE SERVICE
7/tcp   open  echo
22/tcp  open  ssh
77/tcp  open  priv-rje
777/tcp open  multiling-http
```

As shown in the output, TCP port 7/77/777 have been opened.

Now send one request to each port,

```shell
$ echo 'Steer'    | timeout 1 nc -4 127.0.0.1 7;   \
  echo 'on'       | timeout 1 nc -4 127.0.0.1 77;  \
  echo 'multiple' | timeout 1 nc -4 127.0.0.1 777; \
  echo 'ports'    | timeout 1 nc -4 127.0.0.1 7777
Steer
on
multiple
ports
```

Works as expected!

## 3.4 Improvement: pin the BPF program and BPF link to bpffs

Our userspace program has nothing to do after finish loading and attaching BPF
program and maps. But it exits after that, the BPF program and BPF maps it
attached will be dettached and cleaned.

The correct way to avoid this problem is using bpffs and pin the BPF programs and
BPF maps into sysfs. You can do it with golang (the cilium/ebpf libaray we used),
or, you can also do it with `bpftool` commands:

```shell
$ mkdir bpffs
$ sudo mount -t bpf none ./bpffs
$ sudo bpftool map pin id 6 ./bpffs/port_map
$ sudo bpftool map pin id 7 ./bpffs/socket_map

$ sudo chown arthurchiao:arthurchiao ./bpffs/{port_map,socket_map}

$ sudo bpftool prog show pinned /sys/fs/bpf/echo_dispatch_prog
60: sk_lookup  name echo_dispatch  tag da043673afd29081
        loaded_at 2022-12-10T21:01:31+0800  uid 0
        xlated 272B  jited 159B  memlock 4096B  map_ids 6,7
        btf_id 104

$ sudo bpftool link pin id 3 /sys/fs/bpf/echo_dispatch_link
$ sudo bpftool link show pinned /sys/fs/bpf/echo_dispatch_link
3: netns  prog 64
        netns_ino 4026531840  attach_type sk_lookup

$ ls -l /proc/self/ns/net
lrwxrwxrwx 1 arthurchiao arthurchiao /proc/self/ns/net -> 'net:[4026531840]'

$ sudo bpftool map update pinned ./bpffs/port_map key 0x07 0x00 value 0x00
$ sudo bpftool map update pinned ./bpffs/port_map key 0x4d 0x00 value 0x00
$ sudo bpftool map update pinned ./bpffs/port_map key 0x09 0x03 value 0x00
$ sudo bpftool map dump pinned ./bpffs/port_map
key: 09 03  value: 00
key: 07 00  value: 00
key: 4d 00  value: 00
Found 3 elements
```

# 4 Summary

Code used in this post: [github.com/arthurchiao/pidfd-and-sk-lookup-bpf-illustrated](https://github.com/arthurchiao/pidfd-and-sk-lookup-bpf-illustrated).

# References

1. Examples in [github.com/oraoto/go-pidfd](https://github.com/oraoto/go-pidfd)
2. Cloudflare, [Steering connections to sockets with BPF socket lookup hook](https://github.com/jsitnicki/ebpf-summit-2020), eBPF Summit, 2020
