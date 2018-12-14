#!/bin/sh

if [ $# -lt 3 ]; then
    echo "This script will create a veth pair, attach one end to an OVS bridge,"
    echo "and another end to the specified container as a virtual NIC."
    echo ""
    echo "Usage: $0 <CONTAINER> <VETH> <PEER> <OVS BRIDGE>"
    echo "Example: $0 test-container-1 veth-1 peer-1 br-int"
    exit 1
fi

CONTAINER=$1
VETH=$2
PEER=$3
OVS_BR=$4
NETNS=`sudo docker inspect -f '{{.State.Pid}}' $CONTAINER`

echo "add veth pair[$VETH <--> $PEER] to container[$CONTAINER] in netns[$NETNS]"
ip link delete $VETH || true
ip link add $VETH type veth peer name $PEER

echo "add port to ovs bridge $OVS_BR"
ovs-vsctl add-port $OVS_BR $VETH

echo "move $PEER to netns $NETNS"
ip link set $PEER netns $NETNS
ip netns exec $NETNS ip link set dev $PEER up

echo "done"
