# Rules for making the NTFS driver.

obj-m += ntfs.o

ntfs-y := aops.o attrib.o collate.o compress.o debug.o dir.o file.o \
		  index.o inode.o mft.o mst.o namei.o runlist.o super.o sysctl.o \
		  unistr.o upcase.o

ntfs-y += bitmap.o lcnalloc.o logfile.o quota.o usnjrnl.o

ccflags-y := -DNTFS_VERSION=\"2.1.30\"
ccflags-y += -DDEBUG
ccflags-y += -DNTFS_RW

EXTRA_FLAGS += -I$(PWD)

#KDIR	:= /usr/src/linux/
KDIR	:= /lib/modules/$(shell uname -r)/build
PWD		:= $(shell pwd)


all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

help:
	$(MAKE) -C $(KDIR) M=$(PWD) help

.PHONY : install
install : all
	sudo $(MAKE) -C $(KDIR) M=$(PWD) modules_install; sudo depmod
