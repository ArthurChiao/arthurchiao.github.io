---
layout: post
title:  "Ceph: Basic Access Control and Rate Limit with Nginx"
date:   2018-11-05
categories: ceph
---

In [previous]({% link _posts/2018-10-08-monitoring-ceph-obj-storage.md %}) article, we
mentioned that we placed Nginx in front of RGW
as a reverse proxy. **The most important consideration was to collect monitoring metrics**.
Besides that, nginx as a reverse proxy also brings us manyfold benefits.

In this post, we will see that how we could achieve basic access control
and rate limiting capabilities with nginx, which are invaluable in trouble shooting and failure recovery senarios.

## 1 Access Control

Highly imbalanced requests may exhaust a small number of Ceph nodes in a
cluster, to the extent that RGW returns many internal errors (`5xx`), while the rest stays idle.
If mojority of the requests are from several distinct clients, we could exclude
them from accessing the cluster with `deny` and `allow` primitives, this could win us valuable time for trouble shooting.

Snippet of original conf:

```
server {
  location / {
    proxy_pass http://radosgw;
  }
}
```

Forbide client `192.168.1.2`:

```
server {
  location / {
    deny 192.168.1.2;

    proxy_pass http://radosgw;
  }
}
```

then reload nginx with `service nginx reload`. If there are configuration errors,
the logs will be printed to nginx error log, usually `/var/log/nginx/error.log`.


Test on `192.168.1.2`, will receive `403 Forbidden`:

```
$ curl -i <CEPH URL>
Auth GET failed: http://<CEPH URL>/auth/1.0 403 Forbidden
```

And, you could see an entry in nginx error log like following:

```
2018/11/05 11:18:45 [error]: *965977 access forbidden by rule, client: 192.168.1.2, server: , request: "GET /auth/1.0 HTTP/1.1"
```

Combining `deny` and `allow`, we could exclude all clients from the same network,
e.g. `192.168.1.0/30`, and allow some specific IPs in that network. See [1][2] for more info.

## 2 Rate Limiting

Nginx provides multiple primitives for rate limiting, with respect to different
aspects:

* limit client QPS
* limit client connections
* limit bandwidth

### 2.1 Limit Client QPS

Snippet of original conf:

```
server {
  listen 80;
}
```

Now we want to achieve:

* limit QPS by client IP (binary form presentation)
* allow maximum QPS: 1 request/second
* put exceeded requests in a `10MB` memory zone `limitbyaddr`
* indicating too many requests with HTTP code `429`

```
http {
  limit_req_zone $binary_remote_addr zone=limitbyaddr:10m rate=1r/s;
  limit_req_status 429;

  server {
    listen 80;

    limit_req zone=limitbyaddr;
  }
}
```

Test on RGW node:

```
$ for i in {1..3}; do curl localhost; done
<?xml version="1.0" encoding="UTF-8"?><html>
...
<head><title>429 Too Many Requests</title></head>
<body bgcolor="white">
<center><h1>429 Too Many Requests</h1></center>
<hr><center>nginx/1.14.0</center>
</body>
</html>
<html>
<head><title>429 Too Many Requests</title></head>
<body bgcolor="white">
<center><h1>429 Too Many Requests</h1></center>
<hr><center>nginx/1.14.0</center>
</body>
</html>
```

### 2.2 Limit Client Connections

Snippet of original conf:

```
server {
  listen 80;
}
```

Now:

* limit QPS by client IP (binary form presentation)
* allow maximum 1 connection per client IP
* put exceeded requests in a `10MB` memory zone `limitbyaddr`
* indicating too many requests with HTTP code `429`

```
http {
  limit_conn_zone $binary_remote_addr zone=limitbyaddr:10m;
  limit_conn_status 429;

  server {
    listen 80;

    limit_conn limitbyaddr 1;
  }
}
```

Test, download a large file simultaneously on the same node in two bash windows:

```
node-1 $ swift download bkt test_2G
(normal logs ...)
```

```
node-1 $ swift download bkt test_2G
Error downloading object 'bkt/test_2G': Object GET failed: http://<CEPH URL>/swift/v1/bkt/test_2G 429 Too Many Requests
```

### 2.3 Limit Bandwidth

Similar as above with:

```
limit_rate_after 10m;
limit_rate 1m;
```

Quote from nginx [doc](http://nginx.org/en/docs/http/ngx_http_core_module.html#limit_rate):
***"The limit is set per a request, and so if a client simultaneously opens two connections, the overall rate will be twice as much as the specified limit.***

## 3. Summary

Nginx provides powerful privimitives for access controling and rate limiting.
In this post we just showed how we used these functionalities to win
us time for trouble shooting and failure recovery. Examples in this post
are quite simple, and we highly recommend readers to [1][2] for more powerful usage instructions.

## References

1. [Nginx Doc](http://nginx.org/en/docs/)
2. O'Reilly Ebook, [*Complete Nginx Cookbook*](https://www.nginx.com/resources/library/complete-nginx-cookbook)
3. [CtripCloud: Monitoring Ceph Object Storage]({% link _posts/2018-10-08-monitoring-ceph-obj-storage.md %})
