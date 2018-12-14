#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Delete one mirror from OVS bridge"
    echo ""
    echo "Usage: $0 <bridge> <mirror name>"
    echo "Example: $0 br0 mirror0"
    exit 1
fi

BR=$1
MIRROR=$2

ovs-vsctl -- --id=@m get Mirror $MIRROR -- remove Bridge $BR mirrors @m
