set -x

nic=vethcf236fd
app=toy-policy-enforcer

clang -O2 -Wall -c $app.c -target bpf -o $app.o

sudo tc qdisc del dev $nic clsact 2>&1 >/dev/null
sudo tc qdisc add dev $nic clsact

sudo tc filter add dev $nic egress bpf da obj  $app.o sec egress
# sudo tc filter add dev $nic ingress bpf da obj $app.o sec ingress

sudo tc filter show dev $nic egress
# sudo tc filter show dev $nic ingress
