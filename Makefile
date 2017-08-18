obj-m:=kernel_hello_netlink.o


CURRENT_PATH :=$(shell pwd)
VERSION_NUM :=$(shell uname -r)
LINUX_PATH	:=/usr/src/kernels/linux-3.10.0-123.el7/

all :
	make -C $(LINUX_PATH) M=$(CURRENT_PATH) modules
clean :
	make -C $(LINUX_PATH) M=$(CURRENT_PATH) clean
 