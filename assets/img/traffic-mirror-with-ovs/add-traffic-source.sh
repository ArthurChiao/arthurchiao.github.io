#!/bin/bash

if [ $# -ne 3 ]; then
    echo "Add a port to OVS mirror as traffic source"
    echo ""
    echo "Usage: $0 <mirror name> <port name> [egress|ingress|all]"
    echo "Example: $0 mirror0 src-port0 egress"
    exit 1
fi

MIRROR=$1
PORT=$2
DIRECTION=$3

PORT_UUID=$(ovs-vsctl get port $PORT _uuid)
if [ $? -ne 0 ]; then
    echo "get $PORT uuid failed"
    exit 2
fi

if [ $DIRECTION = "egress" ]; then
    ovs-vsctl add Mirror $MIRROR select_src_port $PORT_UUID
elif [ $DIRECTION = "ingress" ]; then
    ovs-vsctl add Mirror $MIRROR select_dst_port $PORT_UUID
elif [ $DIRECTION = "all" ]; then
    ovs-vsctl set Mirror $MIRROR select_src_port=$PORT_UUID select_dst_port=$PORT_UUID
else
    echo "unknow traffic direction: $DIRECTION"
    exit 3
fi
