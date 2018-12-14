#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 <container id or name>"
    exit 1
fi

echo "expose container $1 netns"
NETNS=`sudo docker inspect -f '{{.State.Pid}}' $1`

if [ ! -d /var/run/netns ]; then
    mkdir /var/run/netns
fi
if [ -f /var/run/netns/$NETNS ]; then
    rm -rf /var/run/netns/$NETNS
fi

ln -s /proc/$NETNS/ns/net /var/run/netns/$NETNS
echo "done. netns: $NETNS"

echo "============================="
echo "current network namespaces: "
echo "============================="
ip netns
