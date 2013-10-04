/*
 * dentry.c - NTFS dcache operations.  Part of the Linux-NTFS project.
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

/*
 * Note: the dentry argument is the parent dentry.
 */

static int ntfs_hash_dentry(const struct dentry *dentry, struct qstr *qstr)
{
        unsigned long    hash;
        int              i;
        unsigned l = qstr->len;

        if (l == 1) if (qstr->name[0]=='.') goto x;
        if (l == 2) if (qstr->name[0]=='.' || qstr->name[1]=='.') goto x;
        ntfs_adjust_length(qstr->name, &l);
        /*if (ntfs_chk_name(qstr->name,&l))*/
                /*return -ENAMETOOLONG;*/
                /*return -ENOENT;*/
        x:

        hash = init_name_hash();
        for (i = 0; i < l; i++)
                hash = partial_name_hash(ntfs_upcase(ntfs_sb(dentry->d_sb)->sb_cp_table,qstr->name[i]), hash);
        qstr->hash = end_name_hash(hash);

        return 0;
}

static int ntfs_compare_dentry(const struct dentry *parent, const struct dentry *dentry,
                unsigned int len, const char *str, const struct qstr *name)
{
        unsigned al = len;
        unsigned bl = name->len;

        ntfs_adjust_length(str, &al);
        /*ntfs_adjust_length(b->name, &bl);*/

        /*
         * 'str' is the nane of an already existing dentry, so the name
         * must be valid. 'name' must be validated first.
         */

        if (ntfs_chk_name(name->name, &bl))
                return 1;
        if (ntfs_compare_names(parent->d_sb, str, al, name->name, bl, 0))
                return 1;
        return 0;
}

const struct dentry_operations ntfs_dentry_operations = {
        .d_hash         = ntfs_hash_dentry,
        .d_compare      = ntfs_compare_dentry,
};
