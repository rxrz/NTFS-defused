/*
 * namei.c - NTFS adding & removing files & directories. Part of the Linux-NTFS project.
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

#include <linux/sched.h>
#include "ntfs_fn.h"

static int ntfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
        const unsigned char *name = dentry->d_name.name;
        unsigned len = dentry->d_name.len;
        struct quad_buffer_head qbh0;
        struct buffer_head *bh;
        struct ntfs_dirent *de;
        struct fnode *fnode;
        struct dnode *dnode;
        struct inode *result;
        fnode_secno fno;
        dnode_secno dno;
        int r;
        struct ntfs_dirent dee;
        int err;
        if ((err = ntfs_chk_name(name, &len))) return err==-ENOENT ? -EINVAL : err;
        ntfs_lock(dir->i_sb);
        err = -ENOSPC;
        fnode = ntfs_alloc_fnode(dir->i_sb, ntfs_i(dir)->i_dno, &fno, &bh);
        if (!fnode)
                goto bail;
        dnode = ntfs_alloc_dnode(dir->i_sb, fno, &dno, &qbh0);
        if (!dnode)
                goto bail1;
        memset(&dee, 0, sizeof dee);
        dee.directory = 1;
        if (!(mode & 0222)) dee.read_only = 1;
        /*dee.archive = 0;*/
        dee.hidden = name[0] == '.';
        dee.fnode = cpu_to_le32(fno);
        dee.creation_date = dee.write_date = dee.read_date = cpu_to_le32(gmt_to_local(dir->i_sb, get_seconds()));
        result = new_inode(dir->i_sb);
        if (!result)
                goto bail2;
        ntfs_init_inode(result);
        result->i_ino = fno;
        ntfs_i(result)->i_parent_dir = dir->i_ino;
        ntfs_i(result)->i_dno = dno;
        result->i_ctime.tv_sec = result->i_mtime.tv_sec = result->i_atime.tv_sec = local_to_gmt(dir->i_sb, le32_to_cpu(dee.creation_date));
        result->i_ctime.tv_nsec = 0;
        result->i_mtime.tv_nsec = 0;
        result->i_atime.tv_nsec = 0;
        ntfs_i(result)->i_ea_size = 0;
        result->i_mode |= S_IFDIR;
        result->i_op = &ntfs_dir_iops;
        result->i_fop = &ntfs_dir_ops;
        result->i_blocks = 4;
        result->i_size = 2048;
        set_nlink(result, 2);
        if (dee.read_only)
                result->i_mode &= ~0222;

        r = ntfs_add_dirent(dir, name, len, &dee);
        if (r == 1)
                goto bail3;
        if (r == -1) {
                err = -EEXIST;
                goto bail3;
        }
        fnode->len = len;
        memcpy(fnode->name, name, len > 15 ? 15 : len);
        fnode->up = cpu_to_le32(dir->i_ino);
        fnode->flags |= FNODE_dir;
        fnode->btree.n_free_nodes = 7;
        fnode->btree.n_used_nodes = 1;
        fnode->btree.first_free = cpu_to_le16(0x14);
        fnode->u.external[0].disk_secno = cpu_to_le32(dno);
        fnode->u.external[0].file_secno = cpu_to_le32(-1);
        dnode->root_dnode = 1;
        dnode->up = cpu_to_le32(fno);
        de = ntfs_add_de(dir->i_sb, dnode, "\001\001", 2, 0);
        de->creation_date = de->write_date = de->read_date = cpu_to_le32(gmt_to_local(dir->i_sb, get_seconds()));
        if (!(mode & 0222)) de->read_only = 1;
        de->first = de->directory = 1;
        /*de->hidden = de->system = 0;*/
        de->fnode = cpu_to_le32(fno);
        mark_buffer_dirty(bh);
        brelse(bh);
        ntfs_mark_4buffers_dirty(&qbh0);
        ntfs_brelse4(&qbh0);
        inc_nlink(dir);
        insert_inode_hash(result);

        if (!uid_eq(result->i_uid, current_fsuid()) ||
            !gid_eq(result->i_gid, current_fsgid()) ||
            result->i_mode != (mode | S_IFDIR)) {
                result->i_uid = current_fsuid();
                result->i_gid = current_fsgid();
                result->i_mode = mode | S_IFDIR;
                ntfs_write_inode_nolock(result);
        }
        d_instantiate(dentry, result);
        ntfs_unlock(dir->i_sb);
        return 0;
bail3:
        iput(result);
bail2:
        ntfs_brelse4(&qbh0);
        ntfs_free_dnode(dir->i_sb, dno);
bail1:
        brelse(bh);
        ntfs_free_sectors(dir->i_sb, fno, 1);
bail:
        ntfs_unlock(dir->i_sb);
        return err;
}

static int ntfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
        const unsigned char *name = dentry->d_name.name;
        unsigned len = dentry->d_name.len;
        struct inode *result = NULL;
        struct buffer_head *bh;
        struct fnode *fnode;
        fnode_secno fno;
        int r;
        struct ntfs_dirent dee;
        int err;
        if ((err = ntfs_chk_name(name, &len)))
                return err==-ENOENT ? -EINVAL : err;
        ntfs_lock(dir->i_sb);
        err = -ENOSPC;
        fnode = ntfs_alloc_fnode(dir->i_sb, ntfs_i(dir)->i_dno, &fno, &bh);
        if (!fnode)
                goto bail;
        memset(&dee, 0, sizeof dee);
        if (!(mode & 0222)) dee.read_only = 1;
        dee.archive = 1;
        dee.hidden = name[0] == '.';
        dee.fnode = cpu_to_le32(fno);
        dee.creation_date = dee.write_date = dee.read_date = cpu_to_le32(gmt_to_local(dir->i_sb, get_seconds()));

        result = new_inode(dir->i_sb);
        if (!result)
                goto bail1;

        ntfs_init_inode(result);
        result->i_ino = fno;
        result->i_mode |= S_IFREG;
        result->i_mode &= ~0111;
        result->i_op = &ntfs_file_iops;
        result->i_fop = &ntfs_file_ops;
        set_nlink(result, 1);
        ntfs_i(result)->i_parent_dir = dir->i_ino;
        result->i_ctime.tv_sec = result->i_mtime.tv_sec = result->i_atime.tv_sec = local_to_gmt(dir->i_sb, le32_to_cpu(dee.creation_date));
        result->i_ctime.tv_nsec = 0;
        result->i_mtime.tv_nsec = 0;
        result->i_atime.tv_nsec = 0;
        ntfs_i(result)->i_ea_size = 0;
        if (dee.read_only)
                result->i_mode &= ~0222;
        result->i_blocks = 1;
        result->i_size = 0;
        result->i_data.a_ops = &ntfs_aops;
        ntfs_i(result)->mmu_private = 0;

        r = ntfs_add_dirent(dir, name, len, &dee);
        if (r == 1)
                goto bail2;
        if (r == -1) {
                err = -EEXIST;
                goto bail2;
        }
        fnode->len = len;
        memcpy(fnode->name, name, len > 15 ? 15 : len);
        fnode->up = cpu_to_le32(dir->i_ino);
        mark_buffer_dirty(bh);
        brelse(bh);

        insert_inode_hash(result);

        if (!uid_eq(result->i_uid, current_fsuid()) ||
            !gid_eq(result->i_gid, current_fsgid()) ||
            result->i_mode != (mode | S_IFREG)) {
                result->i_uid = current_fsuid();
                result->i_gid = current_fsgid();
                result->i_mode = mode | S_IFREG;
                ntfs_write_inode_nolock(result);
        }
        d_instantiate(dentry, result);
        ntfs_unlock(dir->i_sb);
        return 0;

bail2:
        iput(result);
bail1:
        brelse(bh);
        ntfs_free_sectors(dir->i_sb, fno, 1);
bail:
        ntfs_unlock(dir->i_sb);
        return err;
}

static int ntfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
        const unsigned char *name = dentry->d_name.name;
        unsigned len = dentry->d_name.len;
        struct buffer_head *bh;
        struct fnode *fnode;
        fnode_secno fno;
        int r;
        struct ntfs_dirent dee;
        struct inode *result = NULL;
        int err;
        if ((err = ntfs_chk_name(name, &len))) return err==-ENOENT ? -EINVAL : err;
        if (ntfs_sb(dir->i_sb)->sb_eas < 2) return -EPERM;
        if (!new_valid_dev(rdev))
                return -EINVAL;
        ntfs_lock(dir->i_sb);
        err = -ENOSPC;
        fnode = ntfs_alloc_fnode(dir->i_sb, ntfs_i(dir)->i_dno, &fno, &bh);
        if (!fnode)
                goto bail;
        memset(&dee, 0, sizeof dee);
        if (!(mode & 0222)) dee.read_only = 1;
        dee.archive = 1;
        dee.hidden = name[0] == '.';
        dee.fnode = cpu_to_le32(fno);
        dee.creation_date = dee.write_date = dee.read_date = cpu_to_le32(gmt_to_local(dir->i_sb, get_seconds()));

        result = new_inode(dir->i_sb);
        if (!result)
                goto bail1;

        ntfs_init_inode(result);
        result->i_ino = fno;
        ntfs_i(result)->i_parent_dir = dir->i_ino;
        result->i_ctime.tv_sec = result->i_mtime.tv_sec = result->i_atime.tv_sec = local_to_gmt(dir->i_sb, le32_to_cpu(dee.creation_date));
        result->i_ctime.tv_nsec = 0;
        result->i_mtime.tv_nsec = 0;
        result->i_atime.tv_nsec = 0;
        ntfs_i(result)->i_ea_size = 0;
        result->i_uid = current_fsuid();
        result->i_gid = current_fsgid();
        set_nlink(result, 1);
        result->i_size = 0;
        result->i_blocks = 1;
        init_special_inode(result, mode, rdev);

        r = ntfs_add_dirent(dir, name, len, &dee);
        if (r == 1)
                goto bail2;
        if (r == -1) {
                err = -EEXIST;
                goto bail2;
        }
        fnode->len = len;
        memcpy(fnode->name, name, len > 15 ? 15 : len);
        fnode->up = cpu_to_le32(dir->i_ino);
        mark_buffer_dirty(bh);

        insert_inode_hash(result);

        ntfs_write_inode_nolock(result);
        d_instantiate(dentry, result);
        brelse(bh);
        ntfs_unlock(dir->i_sb);
        return 0;
bail2:
        iput(result);
bail1:
        brelse(bh);
        ntfs_free_sectors(dir->i_sb, fno, 1);
bail:
        ntfs_unlock(dir->i_sb);
        return err;
}

static int ntfs_symlink(struct inode *dir, struct dentry *dentry, const char *symlink)
{
        const unsigned char *name = dentry->d_name.name;
        unsigned len = dentry->d_name.len;
        struct buffer_head *bh;
        struct fnode *fnode;
        fnode_secno fno;
        int r;
        struct ntfs_dirent dee;
        struct inode *result;
        int err;
        if ((err = ntfs_chk_name(name, &len))) return err==-ENOENT ? -EINVAL : err;
        ntfs_lock(dir->i_sb);
        if (ntfs_sb(dir->i_sb)->sb_eas < 2) {
                ntfs_unlock(dir->i_sb);
                return -EPERM;
        }
        err = -ENOSPC;
        fnode = ntfs_alloc_fnode(dir->i_sb, ntfs_i(dir)->i_dno, &fno, &bh);
        if (!fnode)
                goto bail;
        memset(&dee, 0, sizeof dee);
        dee.archive = 1;
        dee.hidden = name[0] == '.';
        dee.fnode = cpu_to_le32(fno);
        dee.creation_date = dee.write_date = dee.read_date = cpu_to_le32(gmt_to_local(dir->i_sb, get_seconds()));

        result = new_inode(dir->i_sb);
        if (!result)
                goto bail1;
        result->i_ino = fno;
        ntfs_init_inode(result);
        ntfs_i(result)->i_parent_dir = dir->i_ino;
        result->i_ctime.tv_sec = result->i_mtime.tv_sec = result->i_atime.tv_sec = local_to_gmt(dir->i_sb, le32_to_cpu(dee.creation_date));
        result->i_ctime.tv_nsec = 0;
        result->i_mtime.tv_nsec = 0;
        result->i_atime.tv_nsec = 0;
        ntfs_i(result)->i_ea_size = 0;
        result->i_mode = S_IFLNK | 0777;
        result->i_uid = current_fsuid();
        result->i_gid = current_fsgid();
        result->i_blocks = 1;
        set_nlink(result, 1);
        result->i_size = strlen(symlink);
        result->i_op = &page_symlink_inode_operations;
        result->i_data.a_ops = &ntfs_symlink_aops;

        r = ntfs_add_dirent(dir, name, len, &dee);
        if (r == 1)
                goto bail2;
        if (r == -1) {
                err = -EEXIST;
                goto bail2;
        }
        fnode->len = len;
        memcpy(fnode->name, name, len > 15 ? 15 : len);
        fnode->up = cpu_to_le32(dir->i_ino);
        ntfs_set_ea(result, fnode, "SYMLINK", symlink, strlen(symlink));
        mark_buffer_dirty(bh);
        brelse(bh);

        insert_inode_hash(result);

        ntfs_write_inode_nolock(result);
        d_instantiate(dentry, result);
        ntfs_unlock(dir->i_sb);
        return 0;
bail2:
        iput(result);
bail1:
        brelse(bh);
        ntfs_free_sectors(dir->i_sb, fno, 1);
bail:
        ntfs_unlock(dir->i_sb);
        return err;
}

static int ntfs_unlink(struct inode *dir, struct dentry *dentry)
{
        const unsigned char *name = dentry->d_name.name;
        unsigned len = dentry->d_name.len;
        struct quad_buffer_head qbh;
        struct ntfs_dirent *de;
        struct inode *inode = dentry->d_inode;
        dnode_secno dno;
        int r;
        int rep = 0;
        int err;

        ntfs_lock(dir->i_sb);
        ntfs_adjust_length(name, &len);
again:
        err = -ENOENT;
        de = map_dirent(dir, ntfs_i(dir)->i_dno, name, len, &dno, &qbh);
        if (!de)
                goto out;

        err = -EPERM;
        if (de->first)
                goto out1;

        err = -EISDIR;
        if (de->directory)
                goto out1;

        r = ntfs_remove_dirent(dir, dno, de, &qbh, 1);
        switch (r) {
        case 1:
                ntfs_error(dir->i_sb, "there was error when removing dirent");
                err = -EFSERROR;
                break;
        case 2:         /* no space for deleting, try to truncate file */

                err = -ENOSPC;
                if (rep++)
                        break;

                dentry_unhash(dentry);
                if (!d_unhashed(dentry)) {
                        ntfs_unlock(dir->i_sb);
                        return -ENOSPC;
                }
                if (generic_permission(inode, MAY_WRITE) ||
                    !S_ISREG(inode->i_mode) ||
                    get_write_access(inode)) {
                        d_rehash(dentry);
                } else {
                        struct iattr newattrs;
                        /*printk("NTFS: truncating file before delete.\n");*/
                        newattrs.ia_size = 0;
                        newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME;
                        err = notify_change(dentry, &newattrs);
                        put_write_access(inode);
                        if (!err)
                                goto again;
                }
                ntfs_unlock(dir->i_sb);
                return -ENOSPC;
        default:
                drop_nlink(inode);
                err = 0;
        }
        goto out;

out1:
        ntfs_brelse4(&qbh);
out:
        ntfs_unlock(dir->i_sb);
        return err;
}

static int ntfs_rmdir(struct inode *dir, struct dentry *dentry)
{
        const unsigned char *name = dentry->d_name.name;
        unsigned len = dentry->d_name.len;
        struct quad_buffer_head qbh;
        struct ntfs_dirent *de;
        struct inode *inode = dentry->d_inode;
        dnode_secno dno;
        int n_items = 0;
        int err;
        int r;

        ntfs_adjust_length(name, &len);
        ntfs_lock(dir->i_sb);
        err = -ENOENT;
        de = map_dirent(dir, ntfs_i(dir)->i_dno, name, len, &dno, &qbh);
        if (!de)
                goto out;

        err = -EPERM;
        if (de->first)
                goto out1;

        err = -ENOTDIR;
        if (!de->directory)
                goto out1;

        ntfs_count_dnodes(dir->i_sb, ntfs_i(inode)->i_dno, NULL, NULL, &n_items);
        err = -ENOTEMPTY;
        if (n_items)
                goto out1;

        r = ntfs_remove_dirent(dir, dno, de, &qbh, 1);
        switch (r) {
        case 1:
                ntfs_error(dir->i_sb, "there was error when removing dirent");
                err = -EFSERROR;
                break;
        case 2:
                err = -ENOSPC;
                break;
        default:
                drop_nlink(dir);
                clear_nlink(inode);
                err = 0;
        }
        goto out;
out1:
        ntfs_brelse4(&qbh);
out:
        ntfs_unlock(dir->i_sb);
        return err;
}

static int ntfs_symlink_readpage(struct file *file, struct page *page)
{
        char *link = kmap(page);
        struct inode *i = page->mapping->host;
        struct fnode *fnode;
        struct buffer_head *bh;
        int err;

        err = -EIO;
        ntfs_lock(i->i_sb);
        if (!(fnode = ntfs_map_fnode(i->i_sb, i->i_ino, &bh)))
                goto fail;
        err = ntfs_read_ea(i->i_sb, fnode, "SYMLINK", link, PAGE_SIZE);
        brelse(bh);
        if (err)
                goto fail;
        ntfs_unlock(i->i_sb);
        SetPageUptodate(page);
        kunmap(page);
        unlock_page(page);
        return 0;

fail:
        ntfs_unlock(i->i_sb);
        SetPageError(page);
        kunmap(page);
        unlock_page(page);
        return err;
}

const struct address_space_operations ntfs_symlink_aops = {
        .readpage       = ntfs_symlink_readpage
};

static int ntfs_rename(struct inode *old_dir, struct dentry *old_dentry,
                struct inode *new_dir, struct dentry *new_dentry)
{
        const unsigned char *old_name = old_dentry->d_name.name;
        unsigned old_len = old_dentry->d_name.len;
        const unsigned char *new_name = new_dentry->d_name.name;
        unsigned new_len = new_dentry->d_name.len;
        struct inode *i = old_dentry->d_inode;
        struct inode *new_inode = new_dentry->d_inode;
        struct quad_buffer_head qbh, qbh1;
        struct ntfs_dirent *dep, *nde;
        struct ntfs_dirent de;
        dnode_secno dno;
        int r;
        struct buffer_head *bh;
        struct fnode *fnode;
        int err;

        if ((err = ntfs_chk_name(new_name, &new_len))) return err;
        err = 0;
        ntfs_adjust_length(old_name, &old_len);

        ntfs_lock(i->i_sb);
        /* order doesn't matter, due to VFS exclusion */

        /* Erm? Moving over the empty non-busy directory is perfectly legal */
        if (new_inode && S_ISDIR(new_inode->i_mode)) {
                err = -EINVAL;
                goto end1;
        }

        if (!(dep = map_dirent(old_dir, ntfs_i(old_dir)->i_dno, old_name, old_len, &dno, &qbh))) {
                ntfs_error(i->i_sb, "lookup succeeded but map dirent failed");
                err = -ENOENT;
                goto end1;
        }
        copy_de(&de, dep);
        de.hidden = new_name[0] == '.';

        if (new_inode) {
                int r;
                if ((r = ntfs_remove_dirent(old_dir, dno, dep, &qbh, 1)) != 2) {
                        if ((nde = map_dirent(new_dir, ntfs_i(new_dir)->i_dno, new_name, new_len, NULL, &qbh1))) {
                                clear_nlink(new_inode);
                                copy_de(nde, &de);
                                memcpy(nde->name, new_name, new_len);
                                ntfs_mark_4buffers_dirty(&qbh1);
                                ntfs_brelse4(&qbh1);
                                goto end;
                        }
                        ntfs_error(new_dir->i_sb, "ntfs_rename: could not find dirent");
                        err = -EFSERROR;
                        goto end1;
                }
                err = r == 2 ? -ENOSPC : r == 1 ? -EFSERROR : 0;
                goto end1;
        }

        if (new_dir == old_dir) ntfs_brelse4(&qbh);

        if ((r = ntfs_add_dirent(new_dir, new_name, new_len, &de))) {
                if (r == -1) ntfs_error(new_dir->i_sb, "ntfs_rename: dirent already exists!");
                err = r == 1 ? -ENOSPC : -EFSERROR;
                if (new_dir != old_dir) ntfs_brelse4(&qbh);
                goto end1;
        }

        if (new_dir == old_dir)
                if (!(dep = map_dirent(old_dir, ntfs_i(old_dir)->i_dno, old_name, old_len, &dno, &qbh))) {
                        ntfs_error(i->i_sb, "lookup succeeded but map dirent failed at #2");
                        err = -ENOENT;
                        goto end1;
                }

        if ((r = ntfs_remove_dirent(old_dir, dno, dep, &qbh, 0))) {
                ntfs_error(i->i_sb, "ntfs_rename: could not remove dirent");
                err = r == 2 ? -ENOSPC : -EFSERROR;
                goto end1;
        }

        end:
        ntfs_i(i)->i_parent_dir = new_dir->i_ino;
        if (S_ISDIR(i->i_mode)) {
                inc_nlink(new_dir);
                drop_nlink(old_dir);
        }
        if ((fnode = ntfs_map_fnode(i->i_sb, i->i_ino, &bh))) {
                fnode->up = cpu_to_le32(new_dir->i_ino);
                fnode->len = new_len;
                memcpy(fnode->name, new_name, new_len>15?15:new_len);
                if (new_len < 15) memset(&fnode->name[new_len], 0, 15 - new_len);
                mark_buffer_dirty(bh);
                brelse(bh);
        }
end1:
        ntfs_unlock(i->i_sb);
        return err;
}

const struct inode_operations ntfs_dir_iops =
{
        .create         = ntfs_create,
        .lookup         = ntfs_lookup,
        .unlink         = ntfs_unlink,
        .symlink        = ntfs_symlink,
        .mkdir          = ntfs_mkdir,
        .rmdir          = ntfs_rmdir,
        .mknod          = ntfs_mknod,
        .rename         = ntfs_rename,
        .setattr        = ntfs_setattr,
};
