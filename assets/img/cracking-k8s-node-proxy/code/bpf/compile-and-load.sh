set -x

NIC=ens33

clang -O2 -Wall -c toy-proxy-bpf.c -target bpf -o toy-proxy-bpf.o

sudo tc qdisc del dev $NIC clsact 2>&1 >/dev/null
sudo tc qdisc add dev $NIC clsact

sudo tc filter add dev $NIC egress bpf da obj toy-proxy-bpf.o sec egress
sudo tc filter add dev $NIC ingress bpf da obj toy-proxy-bpf.o sec ingress

sudo tc filter show dev $NIC egress
sudo tc filter show dev $NIC ingress
