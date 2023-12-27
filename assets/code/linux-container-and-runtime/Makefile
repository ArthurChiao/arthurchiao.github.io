all:
	gcc -Wl,--no-as-needed -lcap -lseccomp ctn-d.c

run:
	sudo ./a.out -u 0 -m ~/busybox-rootfs-1.36/ -c /bin/echo "hello from inside container"

run-sh:
	sudo ./a.out -u 0 -m ~/busybox-rootfs-1.36/ -c /bin/sh
