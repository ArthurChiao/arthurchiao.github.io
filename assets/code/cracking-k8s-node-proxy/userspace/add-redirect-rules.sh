source ../ENV

iptables -t nat -A OUTPUT -p $PROTO --dport $PORT -j REDIRECT --to-destination $POD_IP:$PORT
