#!/bin/sh

if [ $# -lt 3 ]; then
    echo "This script will add an OVS internal port to an OVS bridge,"
    echo "then assign the port to the specified container as a virtual NIC."
    echo ""
    echo "Usage: $0 <CONTAINER> <NIC NAME> <OVS BRIDGE>"
    echo "Example: $0 test-container-1 vnic-1 br-int"
    exit 1
fi

CONTAINER=$1
PORT=$2
OVS_BR=$3
NETNS=`sudo docker inspect -f '{{.State.Pid}}' $CONTAINER`
echo "add port[$PORT] to container[$CONTAINER] in netns[$NETNS]"

echo "add port to ovs bridge $OVS_BR"
ovs-vsctl add-port $OVS_BR $PORT -- set Interface $PORT type=internal

echo "move $PORT to netns $NETNS"
ip link set $PORT netns $NETNS
ip netns exec $NETNS ip link set dev $PORT up

echo "done"
