/*
 * map.c - NTFS mapping structures to memory with some minimal checks.
 * Part of the Linux-NTFS project.
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

__le32 *ntfs_map_dnode_bitmap(struct super_block *s, struct quad_buffer_head *qbh)
{
        return ntfs_map_4sectors(s, ntfs_sb(s)->sb_dmap, qbh, 0);
}

__le32 *ntfs_map_bitmap(struct super_block *s, unsigned bmp_block,
                         struct quad_buffer_head *qbh, char *id)
{
        secno sec;
        __le32 *ret;
        unsigned n_bands = (ntfs_sb(s)->sb_fs_size + 0x3fff) >> 14;
        if (ntfs_sb(s)->sb_chk) if (bmp_block >= n_bands) {
                ntfs_error(s, "ntfs_map_bitmap called with bad parameter: %08x at %s", bmp_block, id);
                return NULL;
        }
        sec = le32_to_cpu(ntfs_sb(s)->sb_bmp_dir[bmp_block]);
        if (!sec || sec > ntfs_sb(s)->sb_fs_size-4) {
                ntfs_error(s, "invalid bitmap block pointer %08x -> %08x at %s", bmp_block, sec, id);
                return NULL;
        }
        ret = ntfs_map_4sectors(s, sec, qbh, 4);
        if (ret) ntfs_prefetch_bitmap(s, bmp_block + 1);
        return ret;
}

void ntfs_prefetch_bitmap(struct super_block *s, unsigned bmp_block)
{
        unsigned to_prefetch, next_prefetch;
        unsigned n_bands = (ntfs_sb(s)->sb_fs_size + 0x3fff) >> 14;
        if (unlikely(bmp_block >= n_bands))
                return;
        to_prefetch = le32_to_cpu(ntfs_sb(s)->sb_bmp_dir[bmp_block]);
        if (unlikely(bmp_block + 1 >= n_bands))
                next_prefetch = 0;
        else
                next_prefetch = le32_to_cpu(ntfs_sb(s)->sb_bmp_dir[bmp_block + 1]);
        ntfs_prefetch_sectors(s, to_prefetch, 4 + 4 * (to_prefetch + 4 == next_prefetch));
}

/*
 * Load first code page into kernel memory, return pointer to 256-byte array,
 * first 128 bytes are uppercasing table for chars 128-255, next 128 bytes are
 * lowercasing table
 */

unsigned char *ntfs_load_code_page(struct super_block *s, secno cps)
{
        struct buffer_head *bh;
        secno cpds;
        unsigned cpi;
        unsigned char *ptr;
        unsigned char *cp_table;
        int i;
        struct code_page_data *cpd;
        struct code_page_directory *cp = ntfs_map_sector(s, cps, &bh, 0);
        if (!cp) return NULL;
        if (le32_to_cpu(cp->magic) != CP_DIR_MAGIC) {
                printk("NTFS: Code page directory magic doesn't match (magic = %08x)\n", le32_to_cpu(cp->magic));
                brelse(bh);
                return NULL;
        }
        if (!le32_to_cpu(cp->n_code_pages)) {
                printk("NTFS: n_code_pages == 0\n");
                brelse(bh);
                return NULL;
        }
        cpds = le32_to_cpu(cp->array[0].code_page_data);
        cpi = le16_to_cpu(cp->array[0].index);
        brelse(bh);

        if (cpi >= 3) {
                printk("NTFS: Code page index out of array\n");
                return NULL;
        }

        if (!(cpd = ntfs_map_sector(s, cpds, &bh, 0))) return NULL;
        if (le16_to_cpu(cpd->offs[cpi]) > 0x178) {
                printk("NTFS: Code page index out of sector\n");
                brelse(bh);
                return NULL;
        }
        ptr = (unsigned char *)cpd + le16_to_cpu(cpd->offs[cpi]) + 6;
        if (!(cp_table = kmalloc(256, GFP_KERNEL))) {
                printk("NTFS: out of memory for code page table\n");
                brelse(bh);
                return NULL;
        }
        memcpy(cp_table, ptr, 128);
        brelse(bh);

        /* Try to build lowercasing table from uppercasing one */

        for (i=128; i<256; i++) cp_table[i]=i;
        for (i=128; i<256; i++) if (cp_table[i-128]!=i && cp_table[i-128]>=128)
                cp_table[cp_table[i-128]] = i;

        return cp_table;
}

__le32 *ntfs_load_bitmap_directory(struct super_block *s, secno bmp)
{
        struct buffer_head *bh;
        int n = (ntfs_sb(s)->sb_fs_size + 0x200000 - 1) >> 21;
        int i;
        __le32 *b;
        if (!(b = kmalloc(n * 512, GFP_KERNEL))) {
                printk("NTFS: can't allocate memory for bitmap directory\n");
                return NULL;
        }
        for (i=0;i<n;i++) {
                __le32 *d = ntfs_map_sector(s, bmp+i, &bh, n - i - 1);
                if (!d) {
                        kfree(b);
                        return NULL;
                }
                memcpy((char *)b + 512 * i, d, 512);
                brelse(bh);
        }
        return b;
}

/*
 * Load fnode to memory
 */

struct fnode *ntfs_map_fnode(struct super_block *s, ino_t ino, struct buffer_head **bhp)
{
        struct fnode *fnode;
        if (ntfs_sb(s)->sb_chk) if (ntfs_chk_sectors(s, ino, 1, "fnode")) {
                return NULL;
        }
        if ((fnode = ntfs_map_sector(s, ino, bhp, FNODE_RD_AHEAD))) {
                if (ntfs_sb(s)->sb_chk) {
                        struct extended_attribute *ea;
                        struct extended_attribute *ea_end;
                        if (le32_to_cpu(fnode->magic) != FNODE_MAGIC) {
                                ntfs_error(s, "bad magic on fnode %08lx",
                                        (unsigned long)ino);
                                goto bail;
                        }
                        if (!fnode_is_dir(fnode)) {
                                if ((unsigned)fnode->btree.n_used_nodes + (unsigned)fnode->btree.n_free_nodes !=
                                    (bp_internal(&fnode->btree) ? 12 : 8)) {
                                        ntfs_error(s,
                                           "bad number of nodes in fnode %08lx",
                                            (unsigned long)ino);
                                        goto bail;
                                }
                                if (le16_to_cpu(fnode->btree.first_free) !=
                                    8 + fnode->btree.n_used_nodes * (bp_internal(&fnode->btree) ? 8 : 12)) {
                                        ntfs_error(s,
                                            "bad first_free pointer in fnode %08lx",
                                            (unsigned long)ino);
                                        goto bail;
                                }
                        }
                        if (le16_to_cpu(fnode->ea_size_s) && (le16_to_cpu(fnode->ea_offs) < 0xc4 ||
                           le16_to_cpu(fnode->ea_offs) + le16_to_cpu(fnode->acl_size_s) + le16_to_cpu(fnode->ea_size_s) > 0x200)) {
                                ntfs_error(s,
                                        "bad EA info in fnode %08lx: ea_offs == %04x ea_size_s == %04x",
                                        (unsigned long)ino,
                                        le16_to_cpu(fnode->ea_offs), le16_to_cpu(fnode->ea_size_s));
                                goto bail;
                        }
                        ea = fnode_ea(fnode);
                        ea_end = fnode_end_ea(fnode);
                        while (ea != ea_end) {
                                if (ea > ea_end) {
                                        ntfs_error(s, "bad EA in fnode %08lx",
                                                (unsigned long)ino);
                                        goto bail;
                                }
                                ea = next_ea(ea);
                        }
                }
        }
        return fnode;
        bail:
        brelse(*bhp);
        return NULL;
}

struct anode *ntfs_map_anode(struct super_block *s, anode_secno ano, struct buffer_head **bhp)
{
        struct anode *anode;
        if (ntfs_sb(s)->sb_chk) if (ntfs_chk_sectors(s, ano, 1, "anode")) return NULL;
        if ((anode = ntfs_map_sector(s, ano, bhp, ANODE_RD_AHEAD)))
                if (ntfs_sb(s)->sb_chk) {
                        if (le32_to_cpu(anode->magic) != ANODE_MAGIC) {
                                ntfs_error(s, "bad magic on anode %08x", ano);
                                goto bail;
                        }
                        if (le32_to_cpu(anode->self) != ano) {
                                ntfs_error(s, "self pointer invalid on anode %08x", ano);
                                goto bail;
                        }
                        if ((unsigned)anode->btree.n_used_nodes + (unsigned)anode->btree.n_free_nodes !=
                            (bp_internal(&anode->btree) ? 60 : 40)) {
                                ntfs_error(s, "bad number of nodes in anode %08x", ano);
                                goto bail;
                        }
                        if (le16_to_cpu(anode->btree.first_free) !=
                            8 + anode->btree.n_used_nodes * (bp_internal(&anode->btree) ? 8 : 12)) {
                                ntfs_error(s, "bad first_free pointer in anode %08x", ano);
                                goto bail;
                        }
                }
        return anode;
        bail:
        brelse(*bhp);
        return NULL;
}

/*
 * Load dnode to memory and do some checks
 */

struct dnode *ntfs_map_dnode(struct super_block *s, unsigned secno,
                             struct quad_buffer_head *qbh)
{
        struct dnode *dnode;
        if (ntfs_sb(s)->sb_chk) {
                if (ntfs_chk_sectors(s, secno, 4, "dnode")) return NULL;
                if (secno & 3) {
                        ntfs_error(s, "dnode %08x not byte-aligned", secno);
                        return NULL;
                }
        }
        if ((dnode = ntfs_map_4sectors(s, secno, qbh, DNODE_RD_AHEAD)))
                if (ntfs_sb(s)->sb_chk) {
                        unsigned p, pp = 0;
                        unsigned char *d = (unsigned char *)dnode;
                        int b = 0;
                        if (le32_to_cpu(dnode->magic) != DNODE_MAGIC) {
                                ntfs_error(s, "bad magic on dnode %08x", secno);
                                goto bail;
                        }
                        if (le32_to_cpu(dnode->self) != secno)
                                ntfs_error(s, "bad self pointer on dnode %08x self = %08x", secno, le32_to_cpu(dnode->self));
                        /* Check dirents - bad dirents would cause infinite
                           loops or shooting to memory */
                        if (le32_to_cpu(dnode->first_free) > 2048) {
                                ntfs_error(s, "dnode %08x has first_free == %08x", secno, le32_to_cpu(dnode->first_free));
                                goto bail;
                        }
                        for (p = 20; p < le32_to_cpu(dnode->first_free); p += d[p] + (d[p+1] << 8)) {
                                struct ntfs_dirent *de = (struct ntfs_dirent *)((char *)dnode + p);
                                if (le16_to_cpu(de->length) > 292 || (le16_to_cpu(de->length) < 32) || (le16_to_cpu(de->length) & 3) || p + le16_to_cpu(de->length) > 2048) {
                                        ntfs_error(s, "bad dirent size in dnode %08x, dirent %03x, last %03x", secno, p, pp);
                                        goto bail;
                                }
                                if (((31 + de->namelen + de->down*4 + 3) & ~3) != le16_to_cpu(de->length)) {
                                        if (((31 + de->namelen + de->down*4 + 3) & ~3) < le16_to_cpu(de->length) && s->s_flags & MS_RDONLY) goto ok;
                                        ntfs_error(s, "namelen does not match dirent size in dnode %08x, dirent %03x, last %03x", secno, p, pp);
                                        goto bail;
                                }
                                ok:
                                if (ntfs_sb(s)->sb_chk >= 2) b |= 1 << de->down;
                                if (de->down) if (de_down_pointer(de) < 0x10) {
                                        ntfs_error(s, "bad down pointer in dnode %08x, dirent %03x, last %03x", secno, p, pp);
                                        goto bail;
                                }
                                pp = p;

                        }
                        if (p != le32_to_cpu(dnode->first_free)) {
                                ntfs_error(s, "size on last dirent does not match first_free; dnode %08x", secno);
                                goto bail;
                        }
                        if (d[pp + 30] != 1 || d[pp + 31] != 255) {
                                ntfs_error(s, "dnode %08x does not end with \\377 entry", secno);
                                goto bail;
                        }
                        if (b == 3) printk("NTFS: warning: unbalanced dnode tree, dnode %08x; see ntfs.txt 4 more info\n", secno);
                }
        return dnode;
        bail:
        ntfs_brelse4(qbh);
        return NULL;
}

dnode_secno ntfs_fnode_dno(struct super_block *s, ino_t ino)
{
        struct buffer_head *bh;
        struct fnode *fnode;
        dnode_secno dno;

        fnode = ntfs_map_fnode(s, ino, &bh);
        if (!fnode)
                return 0;

        dno = le32_to_cpu(fnode->u.external[0].disk_secno);
        brelse(bh);
        return dno;
}
