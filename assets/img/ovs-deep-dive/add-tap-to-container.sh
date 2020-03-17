#!/bin/sh

if [ $# -lt 3 ]; then
    echo "Usage: $0 <container> <tap> <ovs bridge>"
    exit 1
fi

CONTAINER=$1
TAP=$2
OVS_BR=$3
NETNS=`sudo docker inspect -f '{{.State.Pid}}' $CONTAINER`

echo "adding tap[$TAP] to container[$CONTAINER] in netns[$NETNS] ..."

echo "add port to ovs bridge $OVS_BR"
ovs-vsctl add-port $OVS_BR $TAP -- set Interface $TAP type=internal

echo "add $TAP to netns $NETNS"
ip link set $TAP netns $NETNS
ip netns exec $NETNS ip link set dev $TAP up

echo "done"
