#
# Makefile for the Raspberry-Pi power measurement kernal module
#

KRN_SRC:=$(KERNEL_ROOT)
# The location of Module.symvers
KERNEL_HEADERS=$(KRN_SRC)

obj-m += powermod.o

all:
	make -C $(KERNEL_HEADERS) KSRC=$(KRN_SRC) ARCH=arm CROSS_COMPILE=/usr/bin/arm-linux-gnueabi- -k -j5 M=$(PWD) modules 

clean:
	make -C $(KERNEL_HEADERS) M=$(PWD) clean
