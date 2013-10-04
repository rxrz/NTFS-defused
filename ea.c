/*
 * ea.c - NTFS handling extended attributes.  Part of the Linux-NTFS project.
 *
 * Copyright (c) Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ntfs_fn.h"

/* Remove external extended attributes. ano specifies whether a is a
   direct sector where eas starts or an anode */

void ntfs_ea_ext_remove(struct super_block *s, secno a, int ano, unsigned len)
{
        unsigned pos = 0;
        while (pos < len) {
                char ex[4 + 255 + 1 + 8];
                struct extended_attribute *ea = (struct extended_attribute *)ex;
                if (pos + 4 > len) {
                        ntfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
                                ano ? "anode" : "sectors", a, len);
                        return;
                }
                if (ntfs_ea_read(s, a, ano, pos, 4, ex)) return;
                if (ea_indirect(ea)) {
                        if (ea_valuelen(ea) != 8) {
                                ntfs_error(s, "ea_indirect(ea) set while ea->valuelen!=8, %s %08x, pos %08x",
                                        ano ? "anode" : "sectors", a, pos);
                                return;
                        }
                        if (ntfs_ea_read(s, a, ano, pos + 4, ea->namelen + 9, ex+4))
                                return;
                        ntfs_ea_remove(s, ea_sec(ea), ea_in_anode(ea), ea_len(ea));
                }
                pos += ea->namelen + ea_valuelen(ea) + 5;
        }
        if (!ano) ntfs_free_sectors(s, a, (len+511) >> 9);
        else {
                struct buffer_head *bh;
                struct anode *anode;
                if ((anode = ntfs_map_anode(s, a, &bh))) {
                        ntfs_remove_btree(s, &anode->btree);
                        brelse(bh);
                        ntfs_free_sectors(s, a, 1);
                }
        }
}

static char *get_indirect_ea(struct super_block *s, int ano, secno a, int size)
{
        char *ret;
        if (!(ret = kmalloc(size + 1, GFP_NOFS))) {
                printk("NTFS: out of memory for EA\n");
                return NULL;
        }
        if (ntfs_ea_read(s, a, ano, 0, size, ret)) {
                kfree(ret);
                return NULL;
        }
        ret[size] = 0;
        return ret;
}

static void set_indirect_ea(struct super_block *s, int ano, secno a,
                            const char *data, int size)
{
        ntfs_ea_write(s, a, ano, 0, size, data);
}

/* Read an extended attribute named 'key' into the provided buffer */

int ntfs_read_ea(struct super_block *s, struct fnode *fnode, char *key,
                char *buf, int size)
{
        unsigned pos;
        int ano, len;
        secno a;
        char ex[4 + 255 + 1 + 8];
        struct extended_attribute *ea;
        struct extended_attribute *ea_end = fnode_end_ea(fnode);
        for (ea = fnode_ea(fnode); ea < ea_end; ea = next_ea(ea))
                if (!strcmp(ea->name, key)) {
                        if (ea_indirect(ea))
                                goto indirect;
                        if (ea_valuelen(ea) >= size)
                                return -EINVAL;
                        memcpy(buf, ea_data(ea), ea_valuelen(ea));
                        buf[ea_valuelen(ea)] = 0;
                        return 0;
                }
        a = le32_to_cpu(fnode->ea_secno);
        len = le32_to_cpu(fnode->ea_size_l);
        ano = fnode_in_anode(fnode);
        pos = 0;
        while (pos < len) {
                ea = (struct extended_attribute *)ex;
                if (pos + 4 > len) {
                        ntfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
                                ano ? "anode" : "sectors", a, len);
                        return -EIO;
                }
                if (ntfs_ea_read(s, a, ano, pos, 4, ex)) return -EIO;
                if (ntfs_ea_read(s, a, ano, pos + 4, ea->namelen + 1 + (ea_indirect(ea) ? 8 : 0), ex + 4))
                        return -EIO;
                if (!strcmp(ea->name, key)) {
                        if (ea_indirect(ea))
                                goto indirect;
                        if (ea_valuelen(ea) >= size)
                                return -EINVAL;
                        if (ntfs_ea_read(s, a, ano, pos + 4 + ea->namelen + 1, ea_valuelen(ea), buf))
                                return -EIO;
                        buf[ea_valuelen(ea)] = 0;
                        return 0;
                }
                pos += ea->namelen + ea_valuelen(ea) + 5;
        }
        return -ENOENT;
indirect:
        if (ea_len(ea) >= size)
                return -EINVAL;
        if (ntfs_ea_read(s, ea_sec(ea), ea_in_anode(ea), 0, ea_len(ea), buf))
                return -EIO;
        buf[ea_len(ea)] = 0;
        return 0;
}

/* Read an extended attribute named 'key' */
char *ntfs_get_ea(struct super_block *s, struct fnode *fnode, char *key, int *size)
{
        char *ret;
        unsigned pos;
        int ano, len;
        secno a;
        struct extended_attribute *ea;
        struct extended_attribute *ea_end = fnode_end_ea(fnode);
        for (ea = fnode_ea(fnode); ea < ea_end; ea = next_ea(ea))
                if (!strcmp(ea->name, key)) {
                        if (ea_indirect(ea))
                                return get_indirect_ea(s, ea_in_anode(ea), ea_sec(ea), *size = ea_len(ea));
                        if (!(ret = kmalloc((*size = ea_valuelen(ea)) + 1, GFP_NOFS))) {
                                printk("NTFS: out of memory for EA\n");
                                return NULL;
                        }
                        memcpy(ret, ea_data(ea), ea_valuelen(ea));
                        ret[ea_valuelen(ea)] = 0;
                        return ret;
                }
        a = le32_to_cpu(fnode->ea_secno);
        len = le32_to_cpu(fnode->ea_size_l);
        ano = fnode_in_anode(fnode);
        pos = 0;
        while (pos < len) {
                char ex[4 + 255 + 1 + 8];
                ea = (struct extended_attribute *)ex;
                if (pos + 4 > len) {
                        ntfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
                                ano ? "anode" : "sectors", a, len);
                        return NULL;
                }
                if (ntfs_ea_read(s, a, ano, pos, 4, ex)) return NULL;
                if (ntfs_ea_read(s, a, ano, pos + 4, ea->namelen + 1 + (ea_indirect(ea) ? 8 : 0), ex + 4))
                        return NULL;
                if (!strcmp(ea->name, key)) {
                        if (ea_indirect(ea))
                                return get_indirect_ea(s, ea_in_anode(ea), ea_sec(ea), *size = ea_len(ea));
                        if (!(ret = kmalloc((*size = ea_valuelen(ea)) + 1, GFP_NOFS))) {
                                printk("NTFS: out of memory for EA\n");
                                return NULL;
                        }
                        if (ntfs_ea_read(s, a, ano, pos + 4 + ea->namelen + 1, ea_valuelen(ea), ret)) {
                                kfree(ret);
                                return NULL;
                        }
                        ret[ea_valuelen(ea)] = 0;
                        return ret;
                }
                pos += ea->namelen + ea_valuelen(ea) + 5;
        }
        return NULL;
}

/*
 * Update or create extended attribute 'key' with value 'data'. Note that
 * when this ea exists, it MUST have the same size as size of data.
 * This driver can't change sizes of eas ('cause I just don't need it).
 */

void ntfs_set_ea(struct inode *inode, struct fnode *fnode, const char *key,
                 const char *data, int size)
{
        fnode_secno fno = inode->i_ino;
        struct super_block *s = inode->i_sb;
        unsigned pos;
        int ano, len;
        secno a;
        unsigned char h[4];
        struct extended_attribute *ea;
        struct extended_attribute *ea_end = fnode_end_ea(fnode);
        for (ea = fnode_ea(fnode); ea < ea_end; ea = next_ea(ea))
                if (!strcmp(ea->name, key)) {
                        if (ea_indirect(ea)) {
                                if (ea_len(ea) == size)
                                        set_indirect_ea(s, ea_in_anode(ea), ea_sec(ea), data, size);
                        } else if (ea_valuelen(ea) == size) {
                                memcpy(ea_data(ea), data, size);
                        }
                        return;
                }
        a = le32_to_cpu(fnode->ea_secno);
        len = le32_to_cpu(fnode->ea_size_l);
        ano = fnode_in_anode(fnode);
        pos = 0;
        while (pos < len) {
                char ex[4 + 255 + 1 + 8];
                ea = (struct extended_attribute *)ex;
                if (pos + 4 > len) {
                        ntfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
                                ano ? "anode" : "sectors", a, len);
                        return;
                }
                if (ntfs_ea_read(s, a, ano, pos, 4, ex)) return;
                if (ntfs_ea_read(s, a, ano, pos + 4, ea->namelen + 1 + (ea_indirect(ea) ? 8 : 0), ex + 4))
                        return;
                if (!strcmp(ea->name, key)) {
                        if (ea_indirect(ea)) {
                                if (ea_len(ea) == size)
                                        set_indirect_ea(s, ea_in_anode(ea), ea_sec(ea), data, size);
                        }
                        else {
                                if (ea_valuelen(ea) == size)
                                        ntfs_ea_write(s, a, ano, pos + 4 + ea->namelen + 1, size, data);
                        }
                        return;
                }
                pos += ea->namelen + ea_valuelen(ea) + 5;
        }
        if (!le16_to_cpu(fnode->ea_offs)) {
                /*if (le16_to_cpu(fnode->ea_size_s)) {
                        ntfs_error(s, "fnode %08x: ea_size_s == %03x, ea_offs == 0",
                                inode->i_ino, le16_to_cpu(fnode->ea_size_s));
                        return;
                }*/
                fnode->ea_offs = cpu_to_le16(0xc4);
        }
        if (le16_to_cpu(fnode->ea_offs) < 0xc4 || le16_to_cpu(fnode->ea_offs) + le16_to_cpu(fnode->acl_size_s) + le16_to_cpu(fnode->ea_size_s) > 0x200) {
                ntfs_error(s, "fnode %08lx: ea_offs == %03x, ea_size_s == %03x",
                        (unsigned long)inode->i_ino,
                        le16_to_cpu(fnode->ea_offs), le16_to_cpu(fnode->ea_size_s));
                return;
        }
        if ((le16_to_cpu(fnode->ea_size_s) || !le32_to_cpu(fnode->ea_size_l)) &&
             le16_to_cpu(fnode->ea_offs) + le16_to_cpu(fnode->acl_size_s) + le16_to_cpu(fnode->ea_size_s) + strlen(key) + size + 5 <= 0x200) {
                ea = fnode_end_ea(fnode);
                *(char *)ea = 0;
                ea->namelen = strlen(key);
                ea->valuelen_lo = size;
                ea->valuelen_hi = size >> 8;
                strcpy(ea->name, key);
                memcpy(ea_data(ea), data, size);
                fnode->ea_size_s = cpu_to_le16(le16_to_cpu(fnode->ea_size_s) + strlen(key) + size + 5);
                goto ret;
        }
        /* Most the code here is 99.9993422% unused. I hope there are no bugs.
           But what .. NTFS.IFS has also bugs in ea management. */
        if (le16_to_cpu(fnode->ea_size_s) && !le32_to_cpu(fnode->ea_size_l)) {
                secno n;
                struct buffer_head *bh;
                char *data;
                if (!(n = ntfs_alloc_sector(s, fno, 1, 0))) return;
                if (!(data = ntfs_get_sector(s, n, &bh))) {
                        ntfs_free_sectors(s, n, 1);
                        return;
                }
                memcpy(data, fnode_ea(fnode), le16_to_cpu(fnode->ea_size_s));
                fnode->ea_size_l = cpu_to_le32(le16_to_cpu(fnode->ea_size_s));
                fnode->ea_size_s = cpu_to_le16(0);
                fnode->ea_secno = cpu_to_le32(n);
                fnode->flags &= ~FNODE_anode;
                mark_buffer_dirty(bh);
                brelse(bh);
        }
        pos = le32_to_cpu(fnode->ea_size_l) + 5 + strlen(key) + size;
        len = (le32_to_cpu(fnode->ea_size_l) + 511) >> 9;
        if (pos >= 30000) goto bail;
        while (((pos + 511) >> 9) > len) {
                if (!len) {
                        secno q = ntfs_alloc_sector(s, fno, 1, 0);
                        if (!q) goto bail;
                        fnode->ea_secno = cpu_to_le32(q);
                        fnode->flags &= ~FNODE_anode;
                        len++;
                } else if (!fnode_in_anode(fnode)) {
                        if (ntfs_alloc_if_possible(s, le32_to_cpu(fnode->ea_secno) + len)) {
                                len++;
                        } else {
                                /* Aargh... don't know how to create ea anodes :-( */
                                /*struct buffer_head *bh;
                                struct anode *anode;
                                anode_secno a_s;
                                if (!(anode = ntfs_alloc_anode(s, fno, &a_s, &bh)))
                                        goto bail;
                                anode->up = cpu_to_le32(fno);
                                anode->btree.fnode_parent = 1;
                                anode->btree.n_free_nodes--;
                                anode->btree.n_used_nodes++;
                                anode->btree.first_free = cpu_to_le16(le16_to_cpu(anode->btree.first_free) + 12);
                                anode->u.external[0].disk_secno = cpu_to_le32(le32_to_cpu(fnode->ea_secno));
                                anode->u.external[0].file_secno = cpu_to_le32(0);
                                anode->u.external[0].length = cpu_to_le32(len);
                                mark_buffer_dirty(bh);
                                brelse(bh);
                                fnode->flags |= FNODE_anode;
                                fnode->ea_secno = cpu_to_le32(a_s);*/
                                secno new_sec;
                                int i;
                                if (!(new_sec = ntfs_alloc_sector(s, fno, 1, 1 - ((pos + 511) >> 9))))
                                        goto bail;
                                for (i = 0; i < len; i++) {
                                        struct buffer_head *bh1, *bh2;
                                        void *b1, *b2;
                                        if (!(b1 = ntfs_map_sector(s, le32_to_cpu(fnode->ea_secno) + i, &bh1, len - i - 1))) {
                                                ntfs_free_sectors(s, new_sec, (pos + 511) >> 9);
                                                goto bail;
                                        }
                                        if (!(b2 = ntfs_get_sector(s, new_sec + i, &bh2))) {
                                                brelse(bh1);
                                                ntfs_free_sectors(s, new_sec, (pos + 511) >> 9);
                                                goto bail;
                                        }
                                        memcpy(b2, b1, 512);
                                        brelse(bh1);
                                        mark_buffer_dirty(bh2);
                                        brelse(bh2);
                                }
                                ntfs_free_sectors(s, le32_to_cpu(fnode->ea_secno), len);
                                fnode->ea_secno = cpu_to_le32(new_sec);
                                len = (pos + 511) >> 9;
                        }
                }
                if (fnode_in_anode(fnode)) {
                        if (ntfs_add_sector_to_btree(s, le32_to_cpu(fnode->ea_secno),
                                                     0, len) != -1) {
                                len++;
                        } else {
                                goto bail;
                        }
                }
        }
        h[0] = 0;
        h[1] = strlen(key);
        h[2] = size & 0xff;
        h[3] = size >> 8;
        if (ntfs_ea_write(s, le32_to_cpu(fnode->ea_secno), fnode_in_anode(fnode), le32_to_cpu(fnode->ea_size_l), 4, h)) goto bail;
        if (ntfs_ea_write(s, le32_to_cpu(fnode->ea_secno), fnode_in_anode(fnode), le32_to_cpu(fnode->ea_size_l) + 4, h[1] + 1, key)) goto bail;
        if (ntfs_ea_write(s, le32_to_cpu(fnode->ea_secno), fnode_in_anode(fnode), le32_to_cpu(fnode->ea_size_l) + 5 + h[1], size, data)) goto bail;
        fnode->ea_size_l = cpu_to_le32(pos);
        ret:
        ntfs_i(inode)->i_ea_size += 5 + strlen(key) + size;
        return;
        bail:
        if (le32_to_cpu(fnode->ea_secno))
                if (fnode_in_anode(fnode)) ntfs_truncate_btree(s, le32_to_cpu(fnode->ea_secno), 1, (le32_to_cpu(fnode->ea_size_l) + 511) >> 9);
                else ntfs_free_sectors(s, le32_to_cpu(fnode->ea_secno) + ((le32_to_cpu(fnode->ea_size_l) + 511) >> 9), len - ((le32_to_cpu(fnode->ea_size_l) + 511) >> 9));
        else fnode->ea_secno = fnode->ea_size_l = cpu_to_le32(0);
}
