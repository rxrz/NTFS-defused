/*
 *  linux/fs/ntfs/file.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  file VFS functions
 */

#include "ntfs_fn.h"
#include <linux/mpage.h>

#define BLOCKS(size) (((size) + 511) >> 9)

static int ntfs_file_release(struct inode *inode, struct file *file)
{
        ntfs_lock(inode->i_sb);
        ntfs_write_if_changed(inode);
        ntfs_unlock(inode->i_sb);
        return 0;
}

int ntfs_file_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
        struct inode *inode = file->f_mapping->host;
        int ret;

        ret = filemap_write_and_wait_range(file->f_mapping, start, end);
        if (ret)
                return ret;
        return sync_blockdev(inode->i_sb->s_bdev);
}

/*
 * generic_file_read often calls bmap with non-existing sector,
 * so we must ignore such errors.
 */

static secno ntfs_bmap(struct inode *inode, unsigned file_secno, unsigned *n_secs)
{
        struct ntfs_inode_info *ntfs_inode = ntfs_i(inode);
        unsigned n, disk_secno;
        struct fnode *fnode;
        struct buffer_head *bh;
        if (BLOCKS(ntfs_i(inode)->mmu_private) <= file_secno) return 0;
        n = file_secno - ntfs_inode->i_file_sec;
        if (n < ntfs_inode->i_n_secs) {
                *n_secs = ntfs_inode->i_n_secs - n;
                return ntfs_inode->i_disk_sec + n;
        }
        if (!(fnode = ntfs_map_fnode(inode->i_sb, inode->i_ino, &bh))) return 0;
        disk_secno = ntfs_bplus_lookup(inode->i_sb, inode, &fnode->btree, file_secno, bh);
        if (disk_secno == -1) return 0;
        if (ntfs_chk_sectors(inode->i_sb, disk_secno, 1, "bmap")) return 0;
        n = file_secno - ntfs_inode->i_file_sec;
        if (n < ntfs_inode->i_n_secs) {
                *n_secs = ntfs_inode->i_n_secs - n;
                return ntfs_inode->i_disk_sec + n;
        }
        *n_secs = 1;
        return disk_secno;
}

void ntfs_truncate(struct inode *i)
{
        if (IS_IMMUTABLE(i)) return /*-EPERM*/;
        ntfs_lock_assert(i->i_sb);

        ntfs_i(i)->i_n_secs = 0;
        i->i_blocks = 1 + ((i->i_size + 511) >> 9);
        ntfs_i(i)->mmu_private = i->i_size;
        ntfs_truncate_btree(i->i_sb, i->i_ino, 1, ((i->i_size + 511) >> 9));
        ntfs_write_inode(i);
        ntfs_i(i)->i_n_secs = 0;
}

static int ntfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
{
        int r;
        secno s;
        unsigned n_secs;
        ntfs_lock(inode->i_sb);
        s = ntfs_bmap(inode, iblock, &n_secs);
        if (s) {
                if (bh_result->b_size >> 9 < n_secs)
                        n_secs = bh_result->b_size >> 9;
                map_bh(bh_result, inode->i_sb, s);
                bh_result->b_size = n_secs << 9;
                goto ret_0;
        }
        if (!create) goto ret_0;
        if (iblock<<9 != ntfs_i(inode)->mmu_private) {
                BUG();
                r = -EIO;
                goto ret_r;
        }
        if ((s = ntfs_add_sector_to_btree(inode->i_sb, inode->i_ino, 1, inode->i_blocks - 1)) == -1) {
                ntfs_truncate_btree(inode->i_sb, inode->i_ino, 1, inode->i_blocks - 1);
                r = -ENOSPC;
                goto ret_r;
        }
        inode->i_blocks++;
        ntfs_i(inode)->mmu_private += 512;
        set_buffer_new(bh_result);
        map_bh(bh_result, inode->i_sb, s);
        ret_0:
        r = 0;
        ret_r:
        ntfs_unlock(inode->i_sb);
        return r;
}

static int ntfs_readpage(struct file *file, struct page *page)
{
        return mpage_readpage(page, ntfs_get_block);
}

static int ntfs_writepage(struct page *page, struct writeback_control *wbc)
{
        return block_write_full_page(page, ntfs_get_block, wbc);
}

static int ntfs_readpages(struct file *file, struct address_space *mapping,
                          struct list_head *pages, unsigned nr_pages)
{
        return mpage_readpages(mapping, pages, nr_pages, ntfs_get_block);
}

static int ntfs_writepages(struct address_space *mapping,
                           struct writeback_control *wbc)
{
        return mpage_writepages(mapping, wbc, ntfs_get_block);
}

static void ntfs_write_failed(struct address_space *mapping, loff_t to)
{
        struct inode *inode = mapping->host;

        ntfs_lock(inode->i_sb);

        if (to > inode->i_size) {
                truncate_pagecache(inode, to, inode->i_size);
                ntfs_truncate(inode);
        }

        ntfs_unlock(inode->i_sb);
}

static int ntfs_write_begin(struct file *file, struct address_space *mapping,
                        loff_t pos, unsigned len, unsigned flags,
                        struct page **pagep, void **fsdata)
{
        int ret;

        *pagep = NULL;
        ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
                                ntfs_get_block,
                                &ntfs_i(mapping->host)->mmu_private);
        if (unlikely(ret))
                ntfs_write_failed(mapping, pos + len);

        return ret;
}

static int ntfs_write_end(struct file *file, struct address_space *mapping,
                        loff_t pos, unsigned len, unsigned copied,
                        struct page *pagep, void *fsdata)
{
        struct inode *inode = mapping->host;
        int err;
        err = generic_write_end(file, mapping, pos, len, copied, pagep, fsdata);
        if (err < len)
                ntfs_write_failed(mapping, pos + len);
        if (!(err < 0)) {
                /* make sure we write it on close, if not earlier */
                ntfs_lock(inode->i_sb);
                ntfs_i(inode)->i_dirty = 1;
                ntfs_unlock(inode->i_sb);
        }
        return err;
}

static sector_t _ntfs_bmap(struct address_space *mapping, sector_t block)
{
        return generic_block_bmap(mapping,block,ntfs_get_block);
}

const struct address_space_operations ntfs_aops = {
        .readpage = ntfs_readpage,
        .writepage = ntfs_writepage,
        .readpages = ntfs_readpages,
        .writepages = ntfs_writepages,
        .write_begin = ntfs_write_begin,
        .write_end = ntfs_write_end,
        .bmap = _ntfs_bmap
};

const struct file_operations ntfs_file_ops =
{
        .llseek         = generic_file_llseek,
        .read           = do_sync_read,
        .aio_read       = generic_file_aio_read,
        .write          = do_sync_write,
        .aio_write      = generic_file_aio_write,
        .mmap           = generic_file_mmap,
        .release        = ntfs_file_release,
        .fsync          = ntfs_file_fsync,
        .splice_read    = generic_file_splice_read,
};

const struct inode_operations ntfs_file_iops =
{
        .setattr        = ntfs_setattr,
};
