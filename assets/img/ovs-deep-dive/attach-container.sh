#!/bin/sh

if [ $# -lt 1 ]; then
    echo "Usage: $0 <container id or name>"
    exit 1
fi

echo "attaching to container $1 ..."
sudo docker exec -it $1 bash
