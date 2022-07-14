source ../ENV

echo "Deleting real servers ..."
echo "$CLUSTER_IP:$PORT -> $POD1_IP"
echo "$CLUSTER_IP:$PORT -> $POD2_IP"
ipvsadm -d -t $CLUSTER_IP:$PORT -r $POD1_IP
ipvsadm -d -t $CLUSTER_IP:$PORT -r $POD2_IP

echo "Deleting virtual server CLUSTER_IP:PORT=$CLUSTER_IP:$PORT ..."
ipvsadm -D -t $CLUSTER_IP:$PORT

# clean up virtual server table
# ipvsadm -C

echo "Done"
