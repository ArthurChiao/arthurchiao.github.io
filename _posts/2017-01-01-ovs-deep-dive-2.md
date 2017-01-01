---
layout: post
title:  "OVS Deep Dive 2: OVSDB"
date:   2017-01-01
---

<p class="intro"><span class="dropcap">I</span>n this OVS Deep Dive series,
I will walk through the <a href="https://github.com/openvswitch/ovs">Open vSwtich</a>
 source code to get familiar with the core designs
and implementations of OVS. The code is based on
 <span style="font-weight:bold">ovs 2.6.1</span>.
</p>

## OVSDB
The ovsdb-server program provides RPC interfaces to one or more Open
vSwitch databases (OVSDBs). It supports JSON-RPC client connections over
active or passive TCP/IP or Unix domain sockets.

Each OVSDB file may be specified on the command line as database.  If
none is specified, the default is /etc/openvswitch/conf.db.  The database
files must already have been created and initialized using, for example,
ovsdb-tool create.

### 1. Entrypoint
`ovsdb/ovsdb-server.c`:

```c
int
main(int argc, char *argv[])
{
    /* step.1. handle configs, open db */
    SSET_FOR_EACH (db_filename, &db_filenames)
        open_db(&server_config, db_filename);

    /* step.2. create unixctl server and register commands */
    unixctl_server_create(unixctl_path, &unixctl);
    unixctl_command_register("exit", "", 0, 0, ovsdb_server_exit, &exiting);
    ...
    unixctl_command_register("ovsdb-server/sync-status", "",)

    /* step.3. enter main loop */
    main_loop();
      |
      |--while (!*exiting) {
            /* step.3.1 handle control messages from CLI and RPC */
            unixctl_server_run(unixctl);       // handle CLI commands (turn into RPC requests)
            ovsdb_jsonrpc_server_run(jsonrpc); // handle RPC requests

            SHASH_FOR_EACH(node, all_dbs) {
                ovsdb_trigger_run(db->db, time_msec());
            }
            if (run_process)
                process_run();

            /* step.3.2 update Manager status(es) every 2.5 seconds */
            if (time_msec() >= status_timer)
                update_remote_status(jsonrpc, remotes, all_dbs);

            /* step.3.3 wait events arriving */
            ovsdb_jsonrpc_server_wait(jsonrpc);
            unixctl_server_wait(unixctl);
            SHASH_FOR_EACH(node, all_dbs) {
                ovsdb_trigger_wait(db->db, time_msec());
            }
            if (run_process)
                process_wait(run_process);

            poll_timer_wait_until(status_timer);

            /* step.3.4 block until events arrive */
            poll_block();
        }

    /* step.4. clean and exit */
    ...
}
```

### 2. handle CLI commands
unixctl server receives control messages from CLI through unix socket. The
typical socket is located at `/var/run/openvswitch/`, with the name
`ovsdb-server.<pid>.ctl`. It then converts the message into a RCP request,
which will be handled by the jsonrpc server later.

```c
void
unixctl_server_run(struct unixctl_server *server)
{
    pstream_accept(server->listener, &stream);
    conn->rpc = jsonrpc_open(stream);

    LIST_FOR_EACH_SAFE (conn, next, node, &server->conns) {
        run_connection(conn);
          |
          |--jsonrpc_run()
          |    |--jsonrpc_send()
          |--jsonrpc_recv()
          |--process_command(conn, msg)
    }
}

```

### 3. handle RPC requests

```c
void
ovsdb_jsonrpc_server_run(struct ovsdb_jsonrpc_server *svr)
{
    SHASH_FOR_EACH (node, &svr->remotes) {
        struct ovsdb_jsonrpc_remote *remote = node->data;

        pstream_accept(remote->listener, &stream);
        jsonrpc_session_open_unreliably(jsonrpc_open(stream), remote->dscp);
        ovsdb_jsonrpc_session_create(remote, js, svr->read_only || remote->read_only);

        ovsdb_jsonrpc_session_run_all(remote);
          |
          |--LIST_FOR_EACH_SAFE (s, next, node, &remote->sessions)
                ovsdb_jsonrpc_session_run(s);
                  |
                  |--jsonrpc_sesion_recv()
                     msg->type:
                       case: ovsdb_jsonrpc_session_got_request(s, msg);
                       case: ovsdb_jsonrpc_session_got_notify(s, msg);
                       default: got unexpected msg();
    }
}
```

handle RPC requests and make replies. The requests comes from many sources:
some from the unixctl server, which converts CLI control messages into RPC
requests; and from vswitchd.

```c
static void
ovsdb_jsonrpc_session_got_request(struct ovsdb_jsonrpc_session *s,
                                  struct jsonrpc_msg *request)
{
    struct jsonrpc_msg *reply;

    switch(request->method) {
    case "transact"            if (!reply) reply = execute_transaction(s, db, request);
    case "monitor"             if (!reply) reply = ovsdb_jsonrpc_monitor_create();
    case "monitor_cond_change" reply = ovsdb_jsonrpc_monitor_cond_change();
    case "monitor_cancel"      reply = ovsdb_jsonrpc_monitor_cancel();
    case "get_schema"          if (!reply) reply = jsonrpc_create_reply();
    case "list_dbs"            reply = jsonrpc_create_reply();
    case "lock"                reply = ovsdb_jsonrpc_session_lock();
    case "steal"               reply = ovsdb_jsonrpc_session_lock();
    case "unlock"              reply = ovsdb_jsonrpc_session_unlock(s, request);
    case "echo"                reply = jsonrpc_create_reply();
    }

    if (reply) {
        jsonrpc_msg_destroy(request);
        ovsdb_jsonrpc_session_send(s, reply);
    }
}
```

## References
1. [ovsdb-server man page](https://manned.org/ovsdb-server/54d37166)
