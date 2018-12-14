#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Add an OVS internal port to an OVS bridge"
    echo ""
    echo "Usage: $0 <bridge> <port name>"
    echo "Example: $0 br-int port0"
    exit 1
fi

BR=$1
PORT=$2

ovs-vsctl add-port $BR $PORT -- set interface $PORT type=internal

ifconfig $PORT up
