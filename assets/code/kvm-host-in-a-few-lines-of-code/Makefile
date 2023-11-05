all: kvm image

# compile vm manager
kvm:
	gcc kvm-vmm.c

# Assemble the kernel and link it
image:
	as -32 guest.S -o guest.o
	ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o guest guest.o

clean:
	rm guest guest.o a.out -f
