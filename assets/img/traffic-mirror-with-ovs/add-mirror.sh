#!/bin/bash

if [ $# -ne 3 ]; then
    echo "Add a mirror to OVS bridge"
    echo ""
    echo "Usage: $0 <bridge> <mirror> <output port>"
    echo "Example: $0 br-int mirror0 mirror0-output"
    exit 1
fi

BR=$1
MIRROR=$2
OUTPUT_PORT=$3

ovs-vsctl \
  -- --id=@m create mirror name=$MIRROR \
  -- add bridge $BR mirrors @m \
  -- --id=@port get port $OUTPUT_PORT \
  -- set mirror $MIRROR output-port=@port
