#!/bin/bash

# Usage:
# 1. with no parameters: $0 
# 2. with mirror ID or Name: $0 <mirror ID or name>

MIRROR=

if [ $# -eq 1 ]; then
    MIRROR=$1
fi

ovs-vsctl list Mirror $MIRROR
