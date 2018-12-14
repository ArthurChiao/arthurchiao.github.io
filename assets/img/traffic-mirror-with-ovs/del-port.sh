#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Delete an OVS port from OVS bridge"
    echo ""
    echo "Usage: $0 <port name>"
    echo "Example: $0 port0"
    exit 1
fi

PORT=$1

ovs-vsctl del-port $PORT
