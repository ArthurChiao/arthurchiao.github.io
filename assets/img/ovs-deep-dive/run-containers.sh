#!/bin/sh

if [ $# -lt 1 ]; then
    echo "Usage: $0 <container names>"
    exit 1
fi

for c in $@; do
    echo "run container $c ..."
    sudo docker run -d --name $c \
        centos:latest \
        sleep 1000d
    echo "done"
done
