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
