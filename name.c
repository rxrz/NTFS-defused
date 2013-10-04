/*
 * name.c - NTFS operations with filenames.  Part of the Linux-NTFS project.
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

static inline int not_allowed_char(unsigned char c)
{
        return c<' ' || c=='"' || c=='*' || c=='/' || c==':' || c=='<' ||
              c=='>' || c=='?' || c=='\\' || c=='|';
}

static inline int no_dos_char(unsigned char c)
{       /* Characters that are allowed in NTFS but not in DOS */
        return c=='+' || c==',' || c==';' || c=='=' || c=='[' || c==']';
}

static inline unsigned char upcase(unsigned char *dir, unsigned char a)
{
        if (a<128 || a==255) return a>='a' && a<='z' ? a - 0x20 : a;
        if (!dir) return a;
        return dir[a-128];
}

unsigned char ntfs_upcase(unsigned char *dir, unsigned char a)
{
        return upcase(dir, a);
}

static inline unsigned char locase(unsigned char *dir, unsigned char a)
{
        if (a<128 || a==255) return a>='A' && a<='Z' ? a + 0x20 : a;
        if (!dir) return a;
        return dir[a];
}

int ntfs_chk_name(const unsigned char *name, unsigned *len)
{
        int i;
        if (*len > 254) return -ENAMETOOLONG;
        ntfs_adjust_length(name, len);
        if (!*len) return -EINVAL;
        for (i = 0; i < *len; i++) if (not_allowed_char(name[i])) return -EINVAL;
        if (*len == 1) if (name[0] == '.') return -EINVAL;
        if (*len == 2) if (name[0] == '.' && name[1] == '.') return -EINVAL;
        return 0;
}

unsigned char *ntfs_translate_name(struct super_block *s, unsigned char *from,
                          unsigned len, int lc, int lng)
{
        unsigned char *to;
        int i;
        if (ntfs_sb(s)->sb_chk >= 2) if (ntfs_is_name_long(from, len) != lng) {
                printk("NTFS: Long name flag mismatch - name ");
                for (i=0; i<len; i++) printk("%c", from[i]);
                printk(" misidentified as %s.\n", lng ? "short" : "long");
                printk("NTFS: It's nothing serious. It could happen because of bug in OS/2.\nNTFS: Set checks=normal to disable this message.\n");
        }
        if (!lc) return from;
        if (!(to = kmalloc(len, GFP_KERNEL))) {
                printk("NTFS: can't allocate memory for name conversion buffer\n");
                return from;
        }
        for (i = 0; i < len; i++) to[i] = locase(ntfs_sb(s)->sb_cp_table,from[i]);
        return to;
}

int ntfs_compare_names(struct super_block *s,
                       const unsigned char *n1, unsigned l1,
                       const unsigned char *n2, unsigned l2, int last)
{
        unsigned l = l1 < l2 ? l1 : l2;
        unsigned i;
        if (last) return -1;
        for (i = 0; i < l; i++) {
                unsigned char c1 = upcase(ntfs_sb(s)->sb_cp_table,n1[i]);
                unsigned char c2 = upcase(ntfs_sb(s)->sb_cp_table,n2[i]);
                if (c1 < c2) return -1;
                if (c1 > c2) return 1;
        }
        if (l1 < l2) return -1;
        if (l1 > l2) return 1;
        return 0;
}

int ntfs_is_name_long(const unsigned char *name, unsigned len)
{
        int i,j;
        for (i = 0; i < len && name[i] != '.'; i++)
                if (no_dos_char(name[i])) return 1;
        if (!i || i > 8) return 1;
        if (i == len) return 0;
        for (j = i + 1; j < len; j++)
                if (name[j] == '.' || no_dos_char(name[i])) return 1;
        return j - i > 4;
}

/* OS/2 clears dots and spaces at the end of file name, so we have to */

void ntfs_adjust_length(const unsigned char *name, unsigned *len)
{
        if (!*len) return;
        if (*len == 1 && name[0] == '.') return;
        if (*len == 2 && name[0] == '.' && name[1] == '.') return;
        while (*len && (name[*len - 1] == '.' || name[*len - 1] == ' '))
                (*len)--;
}
