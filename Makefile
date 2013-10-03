#
# Makefile for the Linux NTFS filesystem routines.
#

obj-m += ntfs.o

ntfs-y := alloc.o anode.o buffer.o dentry.o dir.o dnode.o ea.o file.o \
             inode.o map.o name.o namei.o super.o

EXTRA_FLAGS += -I$(PWD)

#KDIR	:= /usr/src/linux/
KDIR	:= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)


all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

help:
	$(MAKE) -C $(KDIR) M=$(PWD) help

.PHONY : install
install : all
	sudo $(MAKE) -C $(KDIR) M=$(PWD) modules_install; sudo depmod
