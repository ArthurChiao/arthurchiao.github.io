source ENV

echo "Adding virtual server CLUSTER_IP:PORT=$CLUSTER_IP:$PORT ..."
ipvsadm -A -t $CLUSTER_IP:$PORT -s rr

echo "Adding real servers ..."
echo "$CLUSTER_IP:$PORT -> $POD1_IP"
echo "$CLUSTER_IP:$PORT -> $POD2_IP"

ipvsadm -a -t $CLUSTER_IP:$PORT -r $POD1_IP -m
ipvsadm -a -t $CLUSTER_IP:$PORT -r $POD2_IP -m

echo "Done"
