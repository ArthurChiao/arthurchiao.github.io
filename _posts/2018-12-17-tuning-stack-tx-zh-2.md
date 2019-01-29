---
layout: post
title:  "[译] Linux网络栈监控和调优：发送数据 2"
date:   2018-12-17
author: ArthurChiao
categories: network-stack monitoring tuning
---

## 3 协议层注册

协议层分析我们将会关注IP和UDP层，其他协议层可参考这个过程。

我们首先来看协议族是如何注册到内核，并被socket子系统使用的。

当用户程序像下面这样创建UDP socket时会发生什么？

```c
sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
```

简单来说，内核会去查找由UDP协议栈导出的一组函数（其中包括用于发送和接收网络数据
的函数），并赋给socket的相应字段。准确理解这个过程需要查看`AF_INET`地址族的
代码。

内核初始化的很早阶段就执行了`inet_init`函数，这个函数会注册`AF_INET`协议族
，以及该协议族内的各协议栈（TCP，UDP，ICMP和RAW），并调用初始化函数使协议栈准备
好处理网络数据。`inet_init`定义在
[net/ipv4/af_inet.c](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/af_inet.c#L1678-L1804)
。

`AF_INET`协议族导出一个包含`create`方法的`struct net_proto_family`类型实例。当从
用户程序创建socket时，内核会调用此方法：

```c
static const struct net_proto_family inet_family_ops = {
    .family = PF_INET,
    .create = inet_create,
    .owner  = THIS_MODULE,
};
```

`inet_create`根据传递的socket参数，在已注册的协议中查找对应的协议。我们来看一下：

```c
        /* Look for the requested type/protocol pair. */
lookup_protocol:
        err = -ESOCKTNOSUPPORT;
        rcu_read_lock();
        list_for_each_entry_rcu(answer, &inetsw[sock->type], list) {

                err = 0;
                /* Check the non-wild match. */
                if (protocol == answer->protocol) {
                        if (protocol != IPPROTO_IP)
                                break;
                } else {
                        /* Check for the two wild cases. */
                        if (IPPROTO_IP == protocol) {
                                protocol = answer->protocol;
                                break;
                        }
                        if (IPPROTO_IP == answer->protocol)
                                break;
                }
                err = -EPROTONOSUPPORT;
        }
```

然后，将该协议的回调方法（集合）赋给这个新创建的socket：

```c
sock->ops = answer->ops;
```

可以在`af_inet.c`中看到所有协议的初始化参数。
下面是[TCP和UDP](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/af_inet.c#L998-L1020)的初始化参数：

```c
/* Upon startup we insert all the elements in inetsw_array[] into
 * the linked list inetsw.
 */
static struct inet_protosw inetsw_array[] =
{
        {
                .type =       SOCK_STREAM,
                .protocol =   IPPROTO_TCP,
                .prot =       &tcp_prot,
                .ops =        &inet_stream_ops,
                .no_check =   0,
                .flags =      INET_PROTOSW_PERMANENT |
                              INET_PROTOSW_ICSK,
        },

        {
                .type =       SOCK_DGRAM,
                .protocol =   IPPROTO_UDP,
                .prot =       &udp_prot,
                .ops =        &inet_dgram_ops,
                .no_check =   UDP_CSUM_DEFAULT,
                .flags =      INET_PROTOSW_PERMANENT,
       },

            /* .... more protocols ... */
```

`IPPROTO_UDP`协议类型有一个`ops`变量，包含[很多信息
](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/af_inet.c#L935-L960)，包
括用于发送和接收数据的回调函数：

```c
const struct proto_ops inet_dgram_ops = {
  .family          = PF_INET,
  .owner           = THIS_MODULE,

  /* ... */

  .sendmsg     = inet_sendmsg,
  .recvmsg     = inet_recvmsg,

  /* ... */
};
EXPORT_SYMBOL(inet_dgram_ops);
```

`prot`字段指向一个协议相关的变量（的地址），对于UDP协议，其中包含了UDP相关的
回调函数。 UDP协议对应的`prot`变量为`udp_prot`，定义在
[net/ipv4/udp.c](https://github.com/torvalds/linux/blob/v3.13/net/ipv4/udp.c#L2171-L2203)：

```
struct proto udp_prot = {
  .name        = "UDP",
  .owner           = THIS_MODULE,

  /* ... */

  .sendmsg     = udp_sendmsg,
  .recvmsg     = udp_recvmsg,

  /* ... */
};
EXPORT_SYMBOL(udp_prot);
```

现在，让我们转向发送UDP数据的用户程序，看看`udp_sendmsg`是如何在内核中被调用的。

## 4 通过socket发送网络数据

用户程序想发送UDP网络数据，因此它使用`sendto`系统调用，看起来可能是这样的：

```c
ret = sendto(socket, buffer, buflen, 0, &dest, sizeof(dest));
```

该系统调用穿过[Linux系统调用（system call）层](https://blog.packagecloud.io/eng/2016/04/05/the-definitive-guide-to-linux-system-calls/)，最后到达[net/socket.c](https://github.com/torvalds/linux/blob/v3.13/net/socket.c#L1756-L1803)中的这个函数：

```c
/*
 *      Send a datagram to a given address. We move the address into kernel
 *      space and check the user space data area is readable before invoking
 *      the protocol.
 */

SYSCALL_DEFINE6(sendto, int, fd, void __user *, buff, size_t, len,
                unsigned int, flags, struct sockaddr __user *, addr,
                int, addr_len)
{
    /*  ... code ... */

    err = sock_sendmsg(sock, &msg, len);

    /* ... code  ... */
}
```

`SYSCALL_DEFINE6`宏会展开成一堆宏，后者经过一波复杂操作创建出一个带6个参数的系统
调用（因此叫`DEFINE6`）。作为结果之一，你会看到内核中的所有系统调用都带`sys_`前
缀。

`sendto`代码会先将数据整理成底层可以处理的格式，然后调用`sock_sendmsg`。特别地，
它将传递给`sendto`的地址放到另一个变量（`msg`）中：

```c
  iov.iov_base = buff;
  iov.iov_len = len;
  msg.msg_name = NULL;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_namelen = 0;
  if (addr) {
          err = move_addr_to_kernel(addr, addr_len, &address);
          if (err < 0)
                  goto out_put;
          msg.msg_name = (struct sockaddr *)&address;
          msg.msg_namelen = addr_len;
  }
```

这段代码将用户程序传入到内核的（存放待发送数据的）地址，作为`msg_name`字段嵌入到
`struct msghdr`类型变量中。这和用户程序直接调用`sendmsg`而不是`sendto`发送
数据差不多，这之所以可行，是因为`sendto`和`sendmsg`底层都会调用`sock_sendmsg`。

### 4.1 `sock_sendmsg`, `__sock_sendmsg`, `__sock_sendmsg_nosec`

`sock_sendmsg`做一些错误检查，然后调用`__sock_sendmsg`；后者做一些自己的错误检查
，然后调用`__sock_sendmsg_nosec`。`__sock_sendmsg_nosec`将数据传递到socket子系统
的更深处：

```c
static inline int __sock_sendmsg_nosec(struct kiocb *iocb, struct socket *sock,
                                       struct msghdr *msg, size_t size)
{
    struct sock_iocb *si =  ....

    /* other code ... */

    return sock->ops->sendmsg(iocb, sock, msg, size);
}
```

通过我们前面介绍的socket创建过程，你应该能看懂，注册到这里的`sendmsg`方法就是
`inet_sendmsg`。

### 4.2 `inet_sendmsg`

从名字可以猜到，这是`AF_INET`协议族提供的通用函数。 此函数首先调用
`sock_rps_record_flow`来记录最后一个处理该（数据所属的）flow的CPU; Receive
Packet Steering会用到这个信息。接下来，调用socket的协议类型（本例是UDP）对应的
`sendmsg`方法：

```c
int inet_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
                 size_t size)
{
      struct sock *sk = sock->sk;

      sock_rps_record_flow(sk);

      /* We may need to bind the socket. */
      if (!inet_sk(sk)->inet_num && !sk->sk_prot->no_autobind && inet_autobind(sk))
              return -EAGAIN;

      return sk->sk_prot->sendmsg(iocb, sk, msg, size);
}
EXPORT_SYMBOL(inet_sendmsg);
```

本例是UDP协议，因此上面的`sk->sk_prot->sendmsg`指向的是我们之前看到的（通过
`udp_prot`导出的）`udp_sendmsg`函数。

**sendmsg()函数作为分界点，处理逻辑从`AF_INET`协议族通用处理转移到具体的UDP协议的处理。**
