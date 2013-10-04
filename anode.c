/*
 * anode.c - handling NTFS anode tree that contains file allocation info.
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

/* Find a sector in allocation tree */

secno ntfs_bplus_lookup(struct super_block *s, struct inode *inode,
                   struct bplus_header *btree, unsigned sec,
                   struct buffer_head *bh)
{
        anode_secno a = -1;
        struct anode *anode;
        int i;
        int c1, c2 = 0;
        go_down:
        if (ntfs_sb(s)->sb_chk) if (ntfs_stop_cycles(s, a, &c1, &c2, "ntfs_bplus_lookup")) return -1;
        if (bp_internal(btree)) {
                for (i = 0; i < btree->n_used_nodes; i++)
                        if (le32_to_cpu(btree->u.internal[i].file_secno) > sec) {
                                a = le32_to_cpu(btree->u.internal[i].down);
                                brelse(bh);
                                if (!(anode = ntfs_map_anode(s, a, &bh))) return -1;
                                btree = &anode->btree;
                                goto go_down;
                        }
                ntfs_error(s, "sector %08x not found in internal anode %08x", sec, a);
                brelse(bh);
                return -1;
        }
        for (i = 0; i < btree->n_used_nodes; i++)
                if (le32_to_cpu(btree->u.external[i].file_secno) <= sec &&
                    le32_to_cpu(btree->u.external[i].file_secno) + le32_to_cpu(btree->u.external[i].length) > sec) {
                        a = le32_to_cpu(btree->u.external[i].disk_secno) + sec - le32_to_cpu(btree->u.external[i].file_secno);
                        if (ntfs_sb(s)->sb_chk) if (ntfs_chk_sectors(s, a, 1, "data")) {
                                brelse(bh);
                                return -1;
                        }
                        if (inode) {
                                struct ntfs_inode_info *ntfs_inode = ntfs_i(inode);
                                ntfs_inode->i_file_sec = le32_to_cpu(btree->u.external[i].file_secno);
                                ntfs_inode->i_disk_sec = le32_to_cpu(btree->u.external[i].disk_secno);
                                ntfs_inode->i_n_secs = le32_to_cpu(btree->u.external[i].length);
                        }
                        brelse(bh);
                        return a;
                }
        ntfs_error(s, "sector %08x not found in external anode %08x", sec, a);
        brelse(bh);
        return -1;
}

/* Add a sector to tree */

secno ntfs_add_sector_to_btree(struct super_block *s, secno node, int fnod, unsigned fsecno)
{
        struct bplus_header *btree;
        struct anode *anode = NULL, *ranode = NULL;
        struct fnode *fnode;
        anode_secno a, na = -1, ra, up = -1;
        secno se;
        struct buffer_head *bh, *bh1, *bh2;
        int n;
        unsigned fs;
        int c1, c2 = 0;
        if (fnod) {
                if (!(fnode = ntfs_map_fnode(s, node, &bh))) return -1;
                btree = &fnode->btree;
        } else {
                if (!(anode = ntfs_map_anode(s, node, &bh))) return -1;
                btree = &anode->btree;
        }
        a = node;
        go_down:
        if ((n = btree->n_used_nodes - 1) < -!!fnod) {
                ntfs_error(s, "anode %08x has no entries", a);
                brelse(bh);
                return -1;
        }
        if (bp_internal(btree)) {
                a = le32_to_cpu(btree->u.internal[n].down);
                btree->u.internal[n].file_secno = cpu_to_le32(-1);
                mark_buffer_dirty(bh);
                brelse(bh);
                if (ntfs_sb(s)->sb_chk)
                        if (ntfs_stop_cycles(s, a, &c1, &c2, "ntfs_add_sector_to_btree #1")) return -1;
                if (!(anode = ntfs_map_anode(s, a, &bh))) return -1;
                btree = &anode->btree;
                goto go_down;
        }
        if (n >= 0) {
                if (le32_to_cpu(btree->u.external[n].file_secno) + le32_to_cpu(btree->u.external[n].length) != fsecno) {
                        ntfs_error(s, "allocated size %08x, trying to add sector %08x, %cnode %08x",
                                le32_to_cpu(btree->u.external[n].file_secno) + le32_to_cpu(btree->u.external[n].length), fsecno,
                                fnod?'f':'a', node);
                        brelse(bh);
                        return -1;
                }
                if (ntfs_alloc_if_possible(s, se = le32_to_cpu(btree->u.external[n].disk_secno) + le32_to_cpu(btree->u.external[n].length))) {
                        le32_add_cpu(&btree->u.external[n].length, 1);
                        mark_buffer_dirty(bh);
                        brelse(bh);
                        return se;
                }
        } else {
                if (fsecno) {
                        ntfs_error(s, "empty file %08x, trying to add sector %08x", node, fsecno);
                        brelse(bh);
                        return -1;
                }
                se = !fnod ? node : (node + 16384) & ~16383;
        }
        if (!(se = ntfs_alloc_sector(s, se, 1, fsecno*ALLOC_M>ALLOC_FWD_MAX ? ALLOC_FWD_MAX : fsecno*ALLOC_M<ALLOC_FWD_MIN ? ALLOC_FWD_MIN : fsecno*ALLOC_M))) {
                brelse(bh);
                return -1;
        }
        fs = n < 0 ? 0 : le32_to_cpu(btree->u.external[n].file_secno) + le32_to_cpu(btree->u.external[n].length);
        if (!btree->n_free_nodes) {
                up = a != node ? le32_to_cpu(anode->up) : -1;
                if (!(anode = ntfs_alloc_anode(s, a, &na, &bh1))) {
                        brelse(bh);
                        ntfs_free_sectors(s, se, 1);
                        return -1;
                }
                if (a == node && fnod) {
                        anode->up = cpu_to_le32(node);
                        anode->btree.flags |= BP_fnode_parent;
                        anode->btree.n_used_nodes = btree->n_used_nodes;
                        anode->btree.first_free = btree->first_free;
                        anode->btree.n_free_nodes = 40 - anode->btree.n_used_nodes;
                        memcpy(&anode->u, &btree->u, btree->n_used_nodes * 12);
                        btree->flags |= BP_internal;
                        btree->n_free_nodes = 11;
                        btree->n_used_nodes = 1;
                        btree->first_free = cpu_to_le16((char *)&(btree->u.internal[1]) - (char *)btree);
                        btree->u.internal[0].file_secno = cpu_to_le32(-1);
                        btree->u.internal[0].down = cpu_to_le32(na);
                        mark_buffer_dirty(bh);
                } else if (!(ranode = ntfs_alloc_anode(s, /*a*/0, &ra, &bh2))) {
                        brelse(bh);
                        brelse(bh1);
                        ntfs_free_sectors(s, se, 1);
                        ntfs_free_sectors(s, na, 1);
                        return -1;
                }
                brelse(bh);
                bh = bh1;
                btree = &anode->btree;
        }
        btree->n_free_nodes--; n = btree->n_used_nodes++;
        le16_add_cpu(&btree->first_free, 12);
        btree->u.external[n].disk_secno = cpu_to_le32(se);
        btree->u.external[n].file_secno = cpu_to_le32(fs);
        btree->u.external[n].length = cpu_to_le32(1);
        mark_buffer_dirty(bh);
        brelse(bh);
        if ((a == node && fnod) || na == -1) return se;
        c2 = 0;
        while (up != (anode_secno)-1) {
                struct anode *new_anode;
                if (ntfs_sb(s)->sb_chk)
                        if (ntfs_stop_cycles(s, up, &c1, &c2, "ntfs_add_sector_to_btree #2")) return -1;
                if (up != node || !fnod) {
                        if (!(anode = ntfs_map_anode(s, up, &bh))) return -1;
                        btree = &anode->btree;
                } else {
                        if (!(fnode = ntfs_map_fnode(s, up, &bh))) return -1;
                        btree = &fnode->btree;
                }
                if (btree->n_free_nodes) {
                        btree->n_free_nodes--; n = btree->n_used_nodes++;
                        le16_add_cpu(&btree->first_free, 8);
                        btree->u.internal[n].file_secno = cpu_to_le32(-1);
                        btree->u.internal[n].down = cpu_to_le32(na);
                        btree->u.internal[n-1].file_secno = cpu_to_le32(fs);
                        mark_buffer_dirty(bh);
                        brelse(bh);
                        brelse(bh2);
                        ntfs_free_sectors(s, ra, 1);
                        if ((anode = ntfs_map_anode(s, na, &bh))) {
                                anode->up = cpu_to_le32(up);
                                if (up == node && fnod)
                                        anode->btree.flags |= BP_fnode_parent;
                                else
                                        anode->btree.flags &= ~BP_fnode_parent;
                                mark_buffer_dirty(bh);
                                brelse(bh);
                        }
                        return se;
                }
                up = up != node ? le32_to_cpu(anode->up) : -1;
                btree->u.internal[btree->n_used_nodes - 1].file_secno = cpu_to_le32(/*fs*/-1);
                mark_buffer_dirty(bh);
                brelse(bh);
                a = na;
                if ((new_anode = ntfs_alloc_anode(s, a, &na, &bh))) {
                        anode = new_anode;
                        /*anode->up = cpu_to_le32(up != -1 ? up : ra);*/
                        anode->btree.flags |= BP_internal;
                        anode->btree.n_used_nodes = 1;
                        anode->btree.n_free_nodes = 59;
                        anode->btree.first_free = cpu_to_le16(16);
                        anode->btree.u.internal[0].down = cpu_to_le32(a);
                        anode->btree.u.internal[0].file_secno = cpu_to_le32(-1);
                        mark_buffer_dirty(bh);
                        brelse(bh);
                        if ((anode = ntfs_map_anode(s, a, &bh))) {
                                anode->up = cpu_to_le32(na);
                                mark_buffer_dirty(bh);
                                brelse(bh);
                        }
                } else na = a;
        }
        if ((anode = ntfs_map_anode(s, na, &bh))) {
                anode->up = cpu_to_le32(node);
                if (fnod)
                        anode->btree.flags |= BP_fnode_parent;
                mark_buffer_dirty(bh);
                brelse(bh);
        }
        if (!fnod) {
                if (!(anode = ntfs_map_anode(s, node, &bh))) {
                        brelse(bh2);
                        return -1;
                }
                btree = &anode->btree;
        } else {
                if (!(fnode = ntfs_map_fnode(s, node, &bh))) {
                        brelse(bh2);
                        return -1;
                }
                btree = &fnode->btree;
        }
        ranode->up = cpu_to_le32(node);
        memcpy(&ranode->btree, btree, le16_to_cpu(btree->first_free));
        if (fnod)
                ranode->btree.flags |= BP_fnode_parent;
        ranode->btree.n_free_nodes = (bp_internal(&ranode->btree) ? 60 : 40) - ranode->btree.n_used_nodes;
        if (bp_internal(&ranode->btree)) for (n = 0; n < ranode->btree.n_used_nodes; n++) {
                struct anode *unode;
                if ((unode = ntfs_map_anode(s, le32_to_cpu(ranode->u.internal[n].down), &bh1))) {
                        unode->up = cpu_to_le32(ra);
                        unode->btree.flags &= ~BP_fnode_parent;
                        mark_buffer_dirty(bh1);
                        brelse(bh1);
                }
        }
        btree->flags |= BP_internal;
        btree->n_free_nodes = fnod ? 10 : 58;
        btree->n_used_nodes = 2;
        btree->first_free = cpu_to_le16((char *)&btree->u.internal[2] - (char *)btree);
        btree->u.internal[0].file_secno = cpu_to_le32(fs);
        btree->u.internal[0].down = cpu_to_le32(ra);
        btree->u.internal[1].file_secno = cpu_to_le32(-1);
        btree->u.internal[1].down = cpu_to_le32(na);
        mark_buffer_dirty(bh);
        brelse(bh);
        mark_buffer_dirty(bh2);
        brelse(bh2);
        return se;
}

/*
 * Remove allocation tree. Recursion would look much nicer but
 * I want to avoid it because it can cause stack overflow.
 */

void ntfs_remove_btree(struct super_block *s, struct bplus_header *btree)
{
        struct bplus_header *btree1 = btree;
        struct anode *anode = NULL;
        anode_secno ano = 0, oano;
        struct buffer_head *bh;
        int level = 0;
        int pos = 0;
        int i;
        int c1, c2 = 0;
        int d1, d2;
        go_down:
        d2 = 0;
        while (bp_internal(btree1)) {
                ano = le32_to_cpu(btree1->u.internal[pos].down);
                if (level) brelse(bh);
                if (ntfs_sb(s)->sb_chk)
                        if (ntfs_stop_cycles(s, ano, &d1, &d2, "ntfs_remove_btree #1"))
                                return;
                if (!(anode = ntfs_map_anode(s, ano, &bh))) return;
                btree1 = &anode->btree;
                level++;
                pos = 0;
        }
        for (i = 0; i < btree1->n_used_nodes; i++)
                ntfs_free_sectors(s, le32_to_cpu(btree1->u.external[i].disk_secno), le32_to_cpu(btree1->u.external[i].length));
        go_up:
        if (!level) return;
        brelse(bh);
        if (ntfs_sb(s)->sb_chk)
                if (ntfs_stop_cycles(s, ano, &c1, &c2, "ntfs_remove_btree #2")) return;
        ntfs_free_sectors(s, ano, 1);
        oano = ano;
        ano = le32_to_cpu(anode->up);
        if (--level) {
                if (!(anode = ntfs_map_anode(s, ano, &bh))) return;
                btree1 = &anode->btree;
        } else btree1 = btree;
        for (i = 0; i < btree1->n_used_nodes; i++) {
                if (le32_to_cpu(btree1->u.internal[i].down) == oano) {
                        if ((pos = i + 1) < btree1->n_used_nodes)
                                goto go_down;
                        else
                                goto go_up;
                }
        }
        ntfs_error(s,
                   "reference to anode %08x not found in anode %08x "
                   "(probably bad up pointer)",
                   oano, level ? ano : -1);
        if (level)
                brelse(bh);
}

/* Just a wrapper around ntfs_bplus_lookup .. used for reading eas */

static secno anode_lookup(struct super_block *s, anode_secno a, unsigned sec)
{
        struct anode *anode;
        struct buffer_head *bh;
        if (!(anode = ntfs_map_anode(s, a, &bh))) return -1;
        return ntfs_bplus_lookup(s, NULL, &anode->btree, sec, bh);
}

int ntfs_ea_read(struct super_block *s, secno a, int ano, unsigned pos,
            unsigned len, char *buf)
{
        struct buffer_head *bh;
        char *data;
        secno sec;
        unsigned l;
        while (len) {
                if (ano) {
                        if ((sec = anode_lookup(s, a, pos >> 9)) == -1)
                                return -1;
                } else sec = a + (pos >> 9);
                if (ntfs_sb(s)->sb_chk) if (ntfs_chk_sectors(s, sec, 1, "ea #1")) return -1;
                if (!(data = ntfs_map_sector(s, sec, &bh, (len - 1) >> 9)))
                        return -1;
                l = 0x200 - (pos & 0x1ff); if (l > len) l = len;
                memcpy(buf, data + (pos & 0x1ff), l);
                brelse(bh);
                buf += l; pos += l; len -= l;
        }
        return 0;
}

int ntfs_ea_write(struct super_block *s, secno a, int ano, unsigned pos,
             unsigned len, const char *buf)
{
        struct buffer_head *bh;
        char *data;
        secno sec;
        unsigned l;
        while (len) {
                if (ano) {
                        if ((sec = anode_lookup(s, a, pos >> 9)) == -1)
                                return -1;
                } else sec = a + (pos >> 9);
                if (ntfs_sb(s)->sb_chk) if (ntfs_chk_sectors(s, sec, 1, "ea #2")) return -1;
                if (!(data = ntfs_map_sector(s, sec, &bh, (len - 1) >> 9)))
                        return -1;
                l = 0x200 - (pos & 0x1ff); if (l > len) l = len;
                memcpy(data + (pos & 0x1ff), buf, l);
                mark_buffer_dirty(bh);
                brelse(bh);
                buf += l; pos += l; len -= l;
        }
        return 0;
}

void ntfs_ea_remove(struct super_block *s, secno a, int ano, unsigned len)
{
        struct anode *anode;
        struct buffer_head *bh;
        if (ano) {
                if (!(anode = ntfs_map_anode(s, a, &bh))) return;
                ntfs_remove_btree(s, &anode->btree);
                brelse(bh);
                ntfs_free_sectors(s, a, 1);
        } else ntfs_free_sectors(s, a, (len + 511) >> 9);
}

/* Truncate allocation tree. Doesn't join anodes - I hope it doesn't matter */

void ntfs_truncate_btree(struct super_block *s, secno f, int fno, unsigned secs)
{
        struct fnode *fnode;
        struct anode *anode;
        struct buffer_head *bh;
        struct bplus_header *btree;
        anode_secno node = f;
        int i, j, nodes;
        int c1, c2 = 0;
        if (fno) {
                if (!(fnode = ntfs_map_fnode(s, f, &bh))) return;
                btree = &fnode->btree;
        } else {
                if (!(anode = ntfs_map_anode(s, f, &bh))) return;
                btree = &anode->btree;
        }
        if (!secs) {
                ntfs_remove_btree(s, btree);
                if (fno) {
                        btree->n_free_nodes = 8;
                        btree->n_used_nodes = 0;
                        btree->first_free = cpu_to_le16(8);
                        btree->flags &= ~BP_internal;
                        mark_buffer_dirty(bh);
                } else ntfs_free_sectors(s, f, 1);
                brelse(bh);
                return;
        }
        while (bp_internal(btree)) {
                nodes = btree->n_used_nodes + btree->n_free_nodes;
                for (i = 0; i < btree->n_used_nodes; i++)
                        if (le32_to_cpu(btree->u.internal[i].file_secno) >= secs) goto f;
                brelse(bh);
                ntfs_error(s, "internal btree %08x doesn't end with -1", node);
                return;
                f:
                for (j = i + 1; j < btree->n_used_nodes; j++)
                        ntfs_ea_remove(s, le32_to_cpu(btree->u.internal[j].down), 1, 0);
                btree->n_used_nodes = i + 1;
                btree->n_free_nodes = nodes - btree->n_used_nodes;
                btree->first_free = cpu_to_le16(8 + 8 * btree->n_used_nodes);
                mark_buffer_dirty(bh);
                if (btree->u.internal[i].file_secno == cpu_to_le32(secs)) {
                        brelse(bh);
                        return;
                }
                node = le32_to_cpu(btree->u.internal[i].down);
                brelse(bh);
                if (ntfs_sb(s)->sb_chk)
                        if (ntfs_stop_cycles(s, node, &c1, &c2, "ntfs_truncate_btree"))
                                return;
                if (!(anode = ntfs_map_anode(s, node, &bh))) return;
                btree = &anode->btree;
        }
        nodes = btree->n_used_nodes + btree->n_free_nodes;
        for (i = 0; i < btree->n_used_nodes; i++)
                if (le32_to_cpu(btree->u.external[i].file_secno) + le32_to_cpu(btree->u.external[i].length) >= secs) goto ff;
        brelse(bh);
        return;
        ff:
        if (secs <= le32_to_cpu(btree->u.external[i].file_secno)) {
                ntfs_error(s, "there is an allocation error in file %08x, sector %08x", f, secs);
                if (i) i--;
        }
        else if (le32_to_cpu(btree->u.external[i].file_secno) + le32_to_cpu(btree->u.external[i].length) > secs) {
                ntfs_free_sectors(s, le32_to_cpu(btree->u.external[i].disk_secno) + secs -
                        le32_to_cpu(btree->u.external[i].file_secno), le32_to_cpu(btree->u.external[i].length)
                        - secs + le32_to_cpu(btree->u.external[i].file_secno)); /* I hope gcc optimizes this :-) */
                btree->u.external[i].length = cpu_to_le32(secs - le32_to_cpu(btree->u.external[i].file_secno));
        }
        for (j = i + 1; j < btree->n_used_nodes; j++)
                ntfs_free_sectors(s, le32_to_cpu(btree->u.external[j].disk_secno), le32_to_cpu(btree->u.external[j].length));
        btree->n_used_nodes = i + 1;
        btree->n_free_nodes = nodes - btree->n_used_nodes;
        btree->first_free = cpu_to_le16(8 + 12 * btree->n_used_nodes);
        mark_buffer_dirty(bh);
        brelse(bh);
}

/* Remove file or directory and it's eas - note that directory must
   be empty when this is called. */

void ntfs_remove_fnode(struct super_block *s, fnode_secno fno)
{
        struct buffer_head *bh;
        struct fnode *fnode;
        struct extended_attribute *ea;
        struct extended_attribute *ea_end;
        if (!(fnode = ntfs_map_fnode(s, fno, &bh))) return;
        if (!fnode_is_dir(fnode)) ntfs_remove_btree(s, &fnode->btree);
        else ntfs_remove_dtree(s, le32_to_cpu(fnode->u.external[0].disk_secno));
        ea_end = fnode_end_ea(fnode);
        for (ea = fnode_ea(fnode); ea < ea_end; ea = next_ea(ea))
                if (ea_indirect(ea))
                        ntfs_ea_remove(s, ea_sec(ea), ea_in_anode(ea), ea_len(ea));
        ntfs_ea_ext_remove(s, le32_to_cpu(fnode->ea_secno), fnode_in_anode(fnode), le32_to_cpu(fnode->ea_size_l));
        brelse(bh);
        ntfs_free_sectors(s, fno, 1);
}
