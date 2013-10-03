/*
 *  linux/fs/ntfs/ntfs_fn.h
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  function headers
 */

//#define DBG
//#define DEBUG_LOCKS

#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "ntfs.h"

#define EIOERROR  EIO
#define EFSERROR  EPERM
#define EMEMERROR ENOMEM

#define ANODE_ALLOC_FWD 512
#define FNODE_ALLOC_FWD 0
#define ALLOC_FWD_MIN   16
#define ALLOC_FWD_MAX   128
#define ALLOC_M         1
#define FNODE_RD_AHEAD  16
#define ANODE_RD_AHEAD  0
#define DNODE_RD_AHEAD  72
#define COUNT_RD_AHEAD  62

#define FREE_DNODES_ADD 58
#define FREE_DNODES_DEL 29

#define CHKCOND(x,y) if (!(x)) printk y

struct ntfs_inode_info {
        loff_t mmu_private;
        ino_t i_parent_dir;     /* (directories) gives fnode of parent dir */
        unsigned i_dno;         /* (directories) root dnode */
        unsigned i_dpos;        /* (directories) temp for readdir */
        unsigned i_dsubdno;     /* (directories) temp for readdir */
        unsigned i_file_sec;    /* (files) minimalist cache of alloc info */
        unsigned i_disk_sec;    /* (files) minimalist cache of alloc info */
        unsigned i_n_secs;      /* (files) minimalist cache of alloc info */
        unsigned i_ea_size;     /* size of extended attributes */
        unsigned i_ea_mode : 1; /* file's permission is stored in ea */
        unsigned i_ea_uid : 1;  /* file's uid is stored in ea */
        unsigned i_ea_gid : 1;  /* file's gid is stored in ea */
        unsigned i_dirty : 1;
        loff_t **i_rddir_off;
        struct inode vfs_inode;
};

struct ntfs_sb_info {
        struct mutex ntfs_mutex;        /* global ntfs lock */
        ino_t sb_root;                  /* inode number of root dir */
        unsigned sb_fs_size;            /* file system size, sectors */
        unsigned sb_bitmaps;            /* sector number of bitmap list */
        unsigned sb_dirband_start;      /* directory band start sector */
        unsigned sb_dirband_size;       /* directory band size, dnodes */
        unsigned sb_dmap;               /* sector number of dnode bit map */
        unsigned sb_n_free;             /* free blocks for statfs, or -1 */
        unsigned sb_n_free_dnodes;      /* free dnodes for statfs, or -1 */
        kuid_t sb_uid;                  /* uid from mount options */
        kgid_t sb_gid;                  /* gid from mount options */
        umode_t sb_mode;                /* mode from mount options */
        unsigned sb_eas : 2;            /* eas: 0-ignore, 1-ro, 2-rw */
        unsigned sb_err : 2;            /* on errs: 0-cont, 1-ro, 2-panic */
        unsigned sb_chk : 2;            /* checks: 0-no, 1-normal, 2-strict */
        unsigned sb_lowercase : 1;      /* downcase filenames hackery */
        unsigned sb_was_error : 1;      /* there was an error, set dirty flag */
        unsigned sb_chkdsk : 2;         /* chkdsk: 0-no, 1-on errs, 2-allways */
        unsigned char *sb_cp_table;     /* code page tables: */
                                        /*      128 bytes uppercasing table & */
                                        /*      128 bytes lowercasing table */
        __le32 *sb_bmp_dir;             /* main bitmap directory */
        unsigned sb_c_bitmap;           /* current bitmap */
        unsigned sb_max_fwd_alloc;      /* max forwad allocation */
        int sb_timeshift;
};

/* Four 512-byte buffers and the 2k block obtained by concatenating them */

struct quad_buffer_head {
        struct buffer_head *bh[4];
        void *data;
};

/* The b-tree down pointer from a dir entry */

static inline dnode_secno de_down_pointer (struct ntfs_dirent *de)
{
  CHKCOND(de->down,("NTFS: de_down_pointer: !de->down\n"));
  return le32_to_cpu(*(__le32 *) ((void *) de + le16_to_cpu(de->length) - 4));
}

/* The first dir entry in a dnode */

static inline struct ntfs_dirent *dnode_first_de (struct dnode *dnode)
{
  return (void *) dnode->dirent;
}

/* The end+1 of the dir entries */

static inline struct ntfs_dirent *dnode_end_de (struct dnode *dnode)
{
  CHKCOND(le32_to_cpu(dnode->first_free)>=0x14 && le32_to_cpu(dnode->first_free)<=0xa00,("NTFS: dnode_end_de: dnode->first_free = %x\n",(unsigned)le32_to_cpu(dnode->first_free)));
  return (void *) dnode + le32_to_cpu(dnode->first_free);
}

/* The dir entry after dir entry de */

static inline struct ntfs_dirent *de_next_de (struct ntfs_dirent *de)
{
  CHKCOND(le16_to_cpu(de->length)>=0x20 && le16_to_cpu(de->length)<0x800,("NTFS: de_next_de: de->length = %x\n",(unsigned)le16_to_cpu(de->length)));
  return (void *) de + le16_to_cpu(de->length);
}

static inline struct extended_attribute *fnode_ea(struct fnode *fnode)
{
        return (struct extended_attribute *)((char *)fnode + le16_to_cpu(fnode->ea_offs) + le16_to_cpu(fnode->acl_size_s));
}

static inline struct extended_attribute *fnode_end_ea(struct fnode *fnode)
{
        return (struct extended_attribute *)((char *)fnode + le16_to_cpu(fnode->ea_offs) + le16_to_cpu(fnode->acl_size_s) + le16_to_cpu(fnode->ea_size_s));
}

static unsigned ea_valuelen(struct extended_attribute *ea)
{
        return ea->valuelen_lo + 256 * ea->valuelen_hi;
}

static inline struct extended_attribute *next_ea(struct extended_attribute *ea)
{
        return (struct extended_attribute *)((char *)ea + 5 + ea->namelen + ea_valuelen(ea));
}

static inline secno ea_sec(struct extended_attribute *ea)
{
        return le32_to_cpu(get_unaligned((__le32 *)((char *)ea + 9 + ea->namelen)));
}

static inline secno ea_len(struct extended_attribute *ea)
{
        return le32_to_cpu(get_unaligned((__le32 *)((char *)ea + 5 + ea->namelen)));
}

static inline char *ea_data(struct extended_attribute *ea)
{
        return (char *)((char *)ea + 5 + ea->namelen);
}

static inline unsigned de_size(int namelen, secno down_ptr)
{
        return ((0x1f + namelen + 3) & ~3) + (down_ptr ? 4 : 0);
}

static inline void copy_de(struct ntfs_dirent *dst, struct ntfs_dirent *src)
{
        int a;
        int n;
        if (!dst || !src) return;
        a = dst->down;
        n = dst->not_8x3;
        memcpy((char *)dst + 2, (char *)src + 2, 28);
        dst->down = a;
        dst->not_8x3 = n;
}

static inline unsigned tstbits(__le32 *bmp, unsigned b, unsigned n)
{
        int i;
        if ((b >= 0x4000) || (b + n - 1 >= 0x4000)) return n;
        if (!((le32_to_cpu(bmp[(b & 0x3fff) >> 5]) >> (b & 0x1f)) & 1)) return 1;
        for (i = 1; i < n; i++)
                if (!((le32_to_cpu(bmp[((b+i) & 0x3fff) >> 5]) >> ((b+i) & 0x1f)) & 1))
                        return i + 1;
        return 0;
}

/* alloc.c */

int ntfs_chk_sectors(struct super_block *, secno, int, char *);
secno ntfs_alloc_sector(struct super_block *, secno, unsigned, int);
int ntfs_alloc_if_possible(struct super_block *, secno);
void ntfs_free_sectors(struct super_block *, secno, unsigned);
int ntfs_check_free_dnodes(struct super_block *, int);
void ntfs_free_dnode(struct super_block *, secno);
struct dnode *ntfs_alloc_dnode(struct super_block *, secno, dnode_secno *, struct quad_buffer_head *);
struct fnode *ntfs_alloc_fnode(struct super_block *, secno, fnode_secno *, struct buffer_head **);
struct anode *ntfs_alloc_anode(struct super_block *, secno, anode_secno *, struct buffer_head **);

/* anode.c */

secno ntfs_bplus_lookup(struct super_block *, struct inode *, struct bplus_header *, unsigned, struct buffer_head *);
secno ntfs_add_sector_to_btree(struct super_block *, secno, int, unsigned);
void ntfs_remove_btree(struct super_block *, struct bplus_header *);
int ntfs_ea_read(struct super_block *, secno, int, unsigned, unsigned, char *);
int ntfs_ea_write(struct super_block *, secno, int, unsigned, unsigned, const char *);
void ntfs_ea_remove(struct super_block *, secno, int, unsigned);
void ntfs_truncate_btree(struct super_block *, secno, int, unsigned);
void ntfs_remove_fnode(struct super_block *, fnode_secno fno);

/* buffer.c */

void ntfs_prefetch_sectors(struct super_block *, unsigned, int);
void *ntfs_map_sector(struct super_block *, unsigned, struct buffer_head **, int);
void *ntfs_get_sector(struct super_block *, unsigned, struct buffer_head **);
void *ntfs_map_4sectors(struct super_block *, unsigned, struct quad_buffer_head *, int);
void *ntfs_get_4sectors(struct super_block *, unsigned, struct quad_buffer_head *);
void ntfs_brelse4(struct quad_buffer_head *);
void ntfs_mark_4buffers_dirty(struct quad_buffer_head *);

/* dentry.c */

extern const struct dentry_operations ntfs_dentry_operations;

/* dir.c */

struct dentry *ntfs_lookup(struct inode *, struct dentry *, unsigned int);
extern const struct file_operations ntfs_dir_ops;

/* dnode.c */

void ntfs_add_pos(struct inode *, loff_t *);
void ntfs_del_pos(struct inode *, loff_t *);
struct ntfs_dirent *ntfs_add_de(struct super_block *, struct dnode *,
                                const unsigned char *, unsigned, secno);
int ntfs_add_dirent(struct inode *, const unsigned char *, unsigned,
                    struct ntfs_dirent *);
int ntfs_remove_dirent(struct inode *, dnode_secno, struct ntfs_dirent *, struct quad_buffer_head *, int);
void ntfs_count_dnodes(struct super_block *, dnode_secno, int *, int *, int *);
dnode_secno ntfs_de_as_down_as_possible(struct super_block *, dnode_secno dno);
struct ntfs_dirent *map_pos_dirent(struct inode *, loff_t *, struct quad_buffer_head *);
struct ntfs_dirent *map_dirent(struct inode *, dnode_secno,
                               const unsigned char *, unsigned, dnode_secno *,
                               struct quad_buffer_head *);
void ntfs_remove_dtree(struct super_block *, dnode_secno);
struct ntfs_dirent *map_fnode_dirent(struct super_block *, fnode_secno, struct fnode *, struct quad_buffer_head *);

/* ea.c */

void ntfs_ea_ext_remove(struct super_block *, secno, int, unsigned);
int ntfs_read_ea(struct super_block *, struct fnode *, char *, char *, int);
char *ntfs_get_ea(struct super_block *, struct fnode *, char *, int *);
void ntfs_set_ea(struct inode *, struct fnode *, const char *,
                 const char *, int);

/* file.c */

int ntfs_file_fsync(struct file *, loff_t, loff_t, int);
void ntfs_truncate(struct inode *);
extern const struct file_operations ntfs_file_ops;
extern const struct inode_operations ntfs_file_iops;
extern const struct address_space_operations ntfs_aops;

/* inode.c */

void ntfs_init_inode(struct inode *);
void ntfs_read_inode(struct inode *);
void ntfs_write_inode(struct inode *);
void ntfs_write_inode_nolock(struct inode *);
int ntfs_setattr(struct dentry *, struct iattr *);
void ntfs_write_if_changed(struct inode *);
void ntfs_evict_inode(struct inode *);

/* map.c */

__le32 *ntfs_map_dnode_bitmap(struct super_block *, struct quad_buffer_head *);
__le32 *ntfs_map_bitmap(struct super_block *, unsigned, struct quad_buffer_head *, char *);
void ntfs_prefetch_bitmap(struct super_block *, unsigned);
unsigned char *ntfs_load_code_page(struct super_block *, secno);
__le32 *ntfs_load_bitmap_directory(struct super_block *, secno bmp);
struct fnode *ntfs_map_fnode(struct super_block *s, ino_t, struct buffer_head **);
struct anode *ntfs_map_anode(struct super_block *s, anode_secno, struct buffer_head **);
struct dnode *ntfs_map_dnode(struct super_block *s, dnode_secno, struct quad_buffer_head *);
dnode_secno ntfs_fnode_dno(struct super_block *s, ino_t ino);

/* name.c */

unsigned char ntfs_upcase(unsigned char *, unsigned char);
int ntfs_chk_name(const unsigned char *, unsigned *);
unsigned char *ntfs_translate_name(struct super_block *, unsigned char *, unsigned, int, int);
int ntfs_compare_names(struct super_block *, const unsigned char *, unsigned,
                       const unsigned char *, unsigned, int);
int ntfs_is_name_long(const unsigned char *, unsigned);
void ntfs_adjust_length(const unsigned char *, unsigned *);

/* namei.c */

extern const struct inode_operations ntfs_dir_iops;
extern const struct address_space_operations ntfs_symlink_aops;

static inline struct ntfs_inode_info *ntfs_i(struct inode *inode)
{
        return list_entry(inode, struct ntfs_inode_info, vfs_inode);
}

static inline struct ntfs_sb_info *ntfs_sb(struct super_block *sb)
{
        return sb->s_fs_info;
}

/* super.c */

__printf(2, 3)
void ntfs_error(struct super_block *, const char *, ...);
int ntfs_stop_cycles(struct super_block *, int, int *, int *, char *);
unsigned ntfs_count_one_bitmap(struct super_block *, secno);

/*
 * local time (NTFS) to GMT (Unix)
 */

static inline time_t local_to_gmt(struct super_block *s, time32_t t)
{
        extern struct timezone sys_tz;
        return t + sys_tz.tz_minuteswest * 60 + ntfs_sb(s)->sb_timeshift;
}

static inline time32_t gmt_to_local(struct super_block *s, time_t t)
{
        extern struct timezone sys_tz;
        return t - sys_tz.tz_minuteswest * 60 - ntfs_sb(s)->sb_timeshift;
}

/*
 * Locking:
 *
 * ntfs_lock() locks the whole filesystem. It must be taken
 * on any method called by the VFS.
 *
 * We don't do any per-file locking anymore, it is hard to
 * review and NTFS is not performance-sensitive anyway.
 */
static inline void ntfs_lock(struct super_block *s)
{
        struct ntfs_sb_info *sbi = ntfs_sb(s);
        mutex_lock(&sbi->ntfs_mutex);
}

static inline void ntfs_unlock(struct super_block *s)
{
        struct ntfs_sb_info *sbi = ntfs_sb(s);
        mutex_unlock(&sbi->ntfs_mutex);
}

static inline void ntfs_lock_assert(struct super_block *s)
{
        struct ntfs_sb_info *sbi = ntfs_sb(s);
        WARN_ON(!mutex_is_locked(&sbi->ntfs_mutex));
}
