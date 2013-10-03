#
# Makefile for the Linux NTFS filesystem routines.
#

obj-$(CONFIG_NTFS_FS) += ntfs.o

ntfs-objs := alloc.o anode.o buffer.o dentry.o dir.o dnode.o ea.o file.o \
             inode.o map.o name.o namei.o super.o
