/*
 * src/fs/fs_main.c - FabricZC VFS Entry (Linux 7.0 Coherent POSIX Tracking)
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/fs_context.h>
#include <linux/slab.h>
#include <linux/math64.h>
#include <linux/mpage.h>
#include <linux/buffer_head.h>
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/xarray.h>
#include <linux/writeback.h>
#include <linux/vfs.h>
#include <linux/blkdev.h>
#include <fabriczc_internal.h>
#include <fabriczc_common.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chazz");

extern void fabriczc_evict_inode(struct inode *inode);

/*
 * Page cache support. This is the gate for running anything at all from
 * FabricZC: execve() maps the ELF image and ld.so maps libc through
 * file_operations->mmap, which the VFS refuses with -ENODEV when the file
 * has no ->mmap hook or its mapping has no read_folio.
 */
static int fabriczc_read_folio(struct file *file, struct folio *folio)
{
    struct inode *inode = folio->mapping->host;
    struct fabriczc_inode *raw = inode->i_private;
    struct super_block *sb = inode->i_sb;
    loff_t pos = folio_pos(folio);
    size_t fsize = folio_size(folio);
    size_t done;
    char *buf;
    int ret = 0;

    if (!raw || !sb) { folio_unlock(folio); return -EIO; }

    buf = kmalloc(4096, GFP_KERNEL);
    if (!buf) { folio_unlock(folio); return -ENOMEM; }

    for (done = 0; done < fsize; done += 4096) {
        loff_t p = pos + done;
        size_t n = min_t(size_t, fsize - done, 4096);

        memset(buf, 0, 4096);
        if (p < raw->i_size_bytes) {
            u64 phys;
            ret = fabriczc_bmap_block(inode, (u64)(p >> 12), &phys, false);
            if (ret) goto out;
            if (phys != FABRICZC_BLOCK_FREE) {
                ret = fabriczc_read_block(sb, phys, buf, 4096);
                if (ret) goto out;
            }
        }
        memcpy(folio_address(folio) + done, buf, n);
    }
    folio_mark_uptodate(folio);
out:
    kfree(buf);
    folio_unlock(folio);
    return ret;
}

/*
 * Writeback for pages dirtied through mmap(MAP_SHARED).
 *
 * The buffer_head/mpage machinery is deliberately avoided: it maps pages
 * through fabriczc_get_block() and surfaced -EIO on the first msync(). Here the
 * dirty folio is written with the same fabriczc_bmap_block()/fabriczc_write_block()
 * pair the read and write paths use, so a shared store lands exactly where
 * fabriczc_read_folio() will look for it.
 */
static int fabriczc_writepages(struct address_space *mapping,
                               struct writeback_control *wbc)
{
    struct folio *folio;
    pgoff_t index = 0;
    int ret = 0;

    xa_for_each(&mapping->i_pages, index, folio) {
        struct inode *inode;
        struct fabriczc_inode *raw;
        loff_t pos;
        size_t fsize, done;
        char *buf;

        if (wbc->nr_to_write <= 0) break;
        if (!folio_trylock(folio)) continue;
        if (folio->mapping != mapping || !folio_test_dirty(folio)) {
            folio_unlock(folio);
            continue;
        }
        inode = mapping->host;
        raw = inode ? inode->i_private : NULL;
        if (!raw || !folio_clear_dirty_for_io(folio)) {
            folio_unlock(folio);
            continue;
        }
        pos = folio_pos(folio);
        fsize = folio_size(folio);
        folio_start_writeback(folio);
        folio_unlock(folio);

        buf = kmalloc(4096, GFP_NOFS);
        if (!buf) { folio_end_writeback(folio); ret = -ENOMEM; break; }
        for (done = 0; done < fsize; done += 4096) {
            loff_t p = pos + done;
            size_t n = min_t(size_t, fsize - done, 4096);
            u64 phys;

            if (p >= raw->i_size_bytes) break; /* nothing past EOF to persist */
            ret = fabriczc_bmap_block(inode, (u64)(p >> 12), &phys, true);
            if (ret) break;
            memcpy(buf, folio_address(folio) + done, n);
            if (n < 4096) memset(buf + n, 0, 4096 - n);
            ret = fabriczc_write_block(inode->i_sb, phys, buf, 4096);
            if (ret) break;
        }
        kfree(buf);
        folio_end_writeback(folio);
        if (ret) { mapping_set_error(mapping, ret); break; }
        wbc->nr_to_write -= fsize >> PAGE_SHIFT;
    }
    return ret;
}

static const struct address_space_operations fabriczc_aops = {
    .read_folio  = fabriczc_read_folio,
    .writepages  = fabriczc_writepages,
    .dirty_folio = filemap_dirty_folio,
};

static void fabriczc_attach_aops(struct inode *inode)
{
    if (inode && inode->i_mapping)
        inode->i_mapping->a_ops = &fabriczc_aops;
}

static void fabriczc_free_link_buf(void *p) { kfree(p); }

static const struct file_operations fabriczc_dir_operations;
static const struct inode_operations fabriczc_dir_inode_operations;
static const struct super_operations fabriczc_super_ops;

static const struct inode_operations fabriczc_file_inode_operations;
static const struct file_operations fabriczc_file_operations;

static const struct inode_operations fabriczc_symlink_inode_operations;

/* -- Directory helpers: walk / search / mutate the parent's data blocks -- */

/* Read one 4 KiB directory block belonging to `dir` at index `*off` in raw->i_direct_blocks */
static int fabriczc_dir_read(struct inode *dir, u32 which, char *buf)
{
    struct fabriczc_inode *raw = dir->i_private;
    u64 phys = raw->i_direct_blocks[which];
    if (phys == FABRICZC_BLOCK_FREE) return -ENOENT;
    return fabriczc_read_block(dir->i_sb, phys, buf, 4096);
}

static int fabriczc_dir_write(struct inode *dir, u32 which, const char *buf)
{
    struct fabriczc_inode *raw = dir->i_private;
    u64 phys = raw->i_direct_blocks[which];
    if (phys == FABRICZC_BLOCK_FREE) return -ENOENT;
    return fabriczc_write_block(dir->i_sb, phys, buf, 4096);
}

/* Append a new entry to the directory's data blocks, allocating one if needed */
static int fabriczc_dir_add_entry(struct inode *dir, const char *name, u32 ino, u8 type)
{
    struct fabriczc_inode *raw = dir->i_private;
    struct super_block *sb = dir->i_sb;
    unsigned int rec_len = (sizeof(struct fabriczc_dir_entry) + strlen(name) + 7) & ~7;
    char *buf;
    int ret, i;

    if (!raw) return -EINVAL;
    buf = kmalloc(4096, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    for (i = 0; i < FABRICZC_N_DIRECT_BLOCKS; i++) {
        struct fabriczc_dir_entry *de, *next;
        unsigned int off = 0;

        if (raw->i_direct_blocks[i] == FABRICZC_BLOCK_FREE) {
            u64 nb;
            if (fabriczc_alloc_data_block(sb, &nb)) { ret = -ENOSPC; goto out; }
            memset(buf, 0, 4096);
            ret = fabriczc_write_block(sb, nb, buf, 4096);
            if (ret) { fabriczc_free_data_block(sb, nb); goto out; }
            raw->i_direct_blocks[i] = nb;
            raw->i_size_bytes += 4096;
            fabriczc_inode_slot_write(sb, dir->i_ino, raw);
            de = (struct fabriczc_dir_entry *)buf;
            de->inode_number = ino;
            de->record_length_bytes = 4096;
            de->name_length = strlen(name);
            de->file_type = type;
            memcpy(de->file_name, name, de->name_length);
            ret = fabriczc_write_block(sb, nb, buf, 4096);
            goto out;
        }

        ret = fabriczc_dir_read(dir, i, buf);
        if (ret) goto out;

        while (off < 4096) {
            de = (struct fabriczc_dir_entry *)(buf + off);
            if (de->record_length_bytes == 0) {
                de->inode_number = ino;
                de->record_length_bytes = 4096 - off;
                de->name_length = strlen(name);
                de->file_type = type;
                memcpy(de->file_name, name, de->name_length);
                ret = fabriczc_dir_write(dir, i, buf);
                goto out;
            }
            if (de->inode_number != 0) {
                unsigned int used = (sizeof(*de) + de->name_length + 7) & ~7;
                if ((de->record_length_bytes - used) >= rec_len) {
                    unsigned int leftover = de->record_length_bytes - used;
                    de->record_length_bytes = used;
                    off += used;
                    next = (struct fabriczc_dir_entry *)(buf + off);
                    next->inode_number = ino;
                    next->record_length_bytes = leftover;
                    next->name_length = strlen(name);
                    next->file_type = type;
                    memcpy(next->file_name, name, next->name_length);
                    ret = fabriczc_dir_write(dir, i, buf);
                    goto out;
                }
            }
            off += de->record_length_bytes;
        }
    }
    ret = -ENOSPC;
out:
    kfree(buf);
    return ret;
}

static int fabriczc_dir_remove_entry(struct inode *dir, const char *name)
{
    struct fabriczc_inode *raw = dir->i_private;
    char *buf;
    int ret, i;

    if (!raw) return -EINVAL;
    buf = kmalloc(4096, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    for (i = 0; i < FABRICZC_N_DIRECT_BLOCKS; i++) {
        struct fabriczc_dir_entry *de;
        unsigned int off = 0;

        if (raw->i_direct_blocks[i] == FABRICZC_BLOCK_FREE) break;
        ret = fabriczc_dir_read(dir, i, buf);
        if (ret) goto out;
        while (off < 4096) {
            de = (struct fabriczc_dir_entry *)(buf + off);
            if (de->record_length_bytes == 0) break;
            if (de->inode_number != 0 && de->name_length == strlen(name) &&
                memcmp(de->file_name, name, de->name_length) == 0) {
                de->inode_number = 0;
                ret = fabriczc_dir_write(dir, i, buf);
                goto out;
            }
            off += de->record_length_bytes;
        }
    }
    ret = -ENOENT;
out:
    kfree(buf);
    return ret;
}

static u32 fabriczc_dir_find_ino(struct inode *dir, const char *name)
{
    struct fabriczc_inode *raw = dir->i_private;
    char *buf;
    u32 found = 0;
    int i;

    if (!raw) return 0;
    buf = kmalloc(4096, GFP_KERNEL);
    if (!buf) return 0;

    for (i = 0; i < FABRICZC_N_DIRECT_BLOCKS; i++) {
        struct fabriczc_dir_entry *de;
        unsigned int off = 0;

        if (raw->i_direct_blocks[i] == FABRICZC_BLOCK_FREE) break;
        if (fabriczc_dir_read(dir, i, buf)) goto out;
        while (off < 4096) {
            de = (struct fabriczc_dir_entry *)(buf + off);
            if (de->record_length_bytes == 0) break;
            if (de->inode_number != 0 && de->name_length == strlen(name) &&
                memcmp(de->file_name, name, de->name_length) == 0) {
                found = de->inode_number;
                goto out;
            }
            off += de->record_length_bytes;
        }
    }
out:
    kfree(buf);
    return found;
}

/* Build a VFS inode from its on-disk slot */
static struct inode *fabriczc_inode_from_raw(struct super_block *sb, u32 ino,
                                             struct fabriczc_inode *raw)
{
    struct inode *inode;

    inode = iget_locked(sb, ino);
    fabriczc_attach_aops(inode);
    if (!inode) return ERR_PTR(-ENOMEM);
    if (inode_state_read_once(inode) & I_NEW) {
        inode->i_mode = raw->i_mode;
        inode->i_uid = GLOBAL_ROOT_UID;
        inode->i_gid = GLOBAL_ROOT_GID;
        inode_set_ctime_current(inode);
        inode->i_atime_sec = inode->i_mtime_sec = inode->i_ctime_sec;
        inode->i_size = raw->i_size_bytes;
        inode->i_blocks = raw->i_blocks_allocated;
        if (S_ISDIR(raw->i_mode)) {
            inode->i_op = &fabriczc_dir_inode_operations;
            inode->i_fop = &fabriczc_dir_operations;
        } else if (S_ISLNK(raw->i_mode)) {
            inode->i_op = &fabriczc_symlink_inode_operations;
            inode->i_fop = &fabriczc_file_operations;
        } else {
            inode->i_op = &fabriczc_file_inode_operations;
            inode->i_fop = &fabriczc_file_operations;
        }
        inode->i_private = raw; /* takes ownership of the kmalloc'd copy */
        unlock_new_inode(inode);
    } else {
        kfree(raw); /* already have a VFS inode for this slot */
    }
    return inode;
}

/* -- VFS file I/O: direct to the backing device, no page cache -- */

static ssize_t fabriczc_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
    struct inode *inode = file_inode(iocb->ki_filp);
    struct fabriczc_inode *raw = inode->i_private;
    char *buf;
    loff_t pos = iocb->ki_pos;
    size_t count = iov_iter_count(to);
    u64 size;
    ssize_t read = 0;
    int ret = 0;

    if (!raw) return -EINVAL;
    size = raw->i_size_bytes;
    if (pos >= size) return 0;
    count = min_t(u64, count, size - pos);

    buf = kmalloc(4096, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    while (count > 0) {
        u64 iblock = pos >> 12;
        u32 off = pos & 4095;
        size_t n = min_t(size_t, count, 4096 - off);
        u64 phys;

        ret = fabriczc_bmap_block(inode, iblock, &phys, false);
        if (ret) break;
        if (phys == FABRICZC_BLOCK_FREE) {
            memset(buf, 0, n);
        } else {
            ret = fabriczc_read_block(inode->i_sb, phys, buf, 4096);
            if (ret) break;
            if (off || n < 4096)
                memmove(buf, buf + off, n);
        }
        if (copy_to_iter(buf, n, to) != n) { ret = -EFAULT; break; }
        pos += n;
        count -= n;
        read += n;
        iocb->ki_pos = pos;
    }
    kfree(buf);
    return read ? read : ret;
}

static ssize_t fabriczc_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
    struct inode *inode = file_inode(iocb->ki_filp);
    struct fabriczc_inode *raw = inode->i_private;
    char *buf;
    loff_t pos = iocb->ki_pos, start = iocb->ki_pos;
    size_t count = iov_iter_count(from);
    ssize_t written = 0;
    int ret = 0;

    if (!raw) return -EINVAL;
    buf = kmalloc(4096, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    while (count > 0) {
        u64 iblock = pos >> 12;
        u32 off = pos & 4095;
        size_t n = min_t(size_t, count, 4096 - off);
        u64 phys;
        bool full = (off == 0 && n == 4096);

        ret = fabriczc_bmap_block(inode, iblock, &phys, true);
        if (ret) break;

        if (!full) {
            ret = fabriczc_read_block(inode->i_sb, phys, buf, 4096);
            if (ret) break;
        }
        if (copy_from_iter(buf + off, n, from) != n) { ret = -EFAULT; break; }
        ret = fabriczc_write_block(inode->i_sb, phys, buf, 4096);
        if (ret) break;

        pos += n;
        count -= n;
        written += n;
        iocb->ki_pos = pos;
    }

    if (written > 0) {
        if (pos > raw->i_size_bytes)
            raw->i_size_bytes = pos;
        raw->i_blocks_allocated += 0; /* bmap already counts */
        /* Reflect the new size back into the VFS inode so stat()/md5sum see it */
        inode->i_size = raw->i_size_bytes;
        inode->i_blocks = raw->i_blocks_allocated;
        inode_set_ctime_current(inode);
        inode->i_mtime_sec = inode->i_ctime_sec;
        inode->i_mtime_nsec = inode->i_ctime_nsec;
        /* Drop cached pages so mmap()/execve() readers never see stale data */
        if (inode->i_mapping && pos > start)
            invalidate_inode_pages2_range(inode->i_mapping, start >> PAGE_SHIFT,
                                          (pos - 1) >> PAGE_SHIFT);
        fabriczc_inode_slot_write(inode->i_sb, inode->i_ino, raw);
        mark_inode_dirty(inode);
    }
    kfree(buf);
    return written ? written : ret;
}

/* -- VFS directory operations -- */

static int fabriczc_iterate(struct file *file, struct dir_context *ctx)
{
    struct inode *inode = file_inode(file);
    struct fabriczc_inode *raw = inode->i_private;
    char *buf;
    u32 direct;
    int emitted_dots;

    if (!raw) return -EINVAL;
    if (!dir_emit_dots(file, ctx)) return 0;
    emitted_dots = 2;

    buf = kmalloc(4096, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    for (direct = 0; direct < FABRICZC_N_DIRECT_BLOCKS; direct++) {
        unsigned int off = 0;
        u64 phys = raw->i_direct_blocks[direct];

        if (phys == FABRICZC_BLOCK_FREE) break;
        if (fabriczc_read_block(inode->i_sb, phys, buf, 4096)) break;

        while (off < 4096) {
            struct fabriczc_dir_entry *de = (struct fabriczc_dir_entry *)(buf + off);
            u64 entry_index;

            if (de->record_length_bytes == 0) break;
            entry_index = emitted_dots;
            if (de->inode_number != 0 && de->name_length > 0) {
                if (entry_index >= ctx->pos) {
                    if (!dir_emit(ctx, de->file_name, de->name_length,
                                  de->inode_number, de->file_type))
                        goto out;
                    ctx->pos = entry_index + 1;
                }
            }
            emitted_dots++;
            off += de->record_length_bytes;
        }
    }
out:
    kfree(buf);
    return 0;
}

static struct dentry *fabriczc_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
    u32 ino = fabriczc_dir_find_ino(dir, dentry->d_name.name);
    struct fabriczc_inode *raw;
    struct inode *inode = NULL;

    if (ino == 0) return d_splice_alias(NULL, dentry);
    raw = kmalloc(sizeof(*raw), GFP_KERNEL);
    if (!raw) return ERR_PTR(-ENOMEM);
    if (fabriczc_inode_slot_read(dir->i_sb, ino, raw)) {
        kfree(raw);
        return ERR_PTR(-EIO);
    }
    if (raw->i_mode == 0) { kfree(raw); return d_splice_alias(NULL, dentry); }
    inode = fabriczc_inode_from_raw(dir->i_sb, ino, raw);
    if (IS_ERR(inode)) return ERR_CAST(inode);
    return d_splice_alias(inode, dentry);
}

static int fabriczc_unlink(struct inode *dir, struct dentry *dentry)
{
    struct inode *inode = d_inode(dentry);
    u32 ino = inode->i_ino;
    int ret = fabriczc_dir_remove_entry(dir, dentry->d_name.name);
    struct fabriczc_inode *raw;

    if (ret) return ret;
    raw = kmalloc(sizeof(*raw), GFP_KERNEL);
    if (raw) {
        if (fabriczc_inode_slot_read(dir->i_sb, ino, raw) == 0) {
            memset(raw->i_direct_blocks, 0xFF, sizeof(raw->i_direct_blocks));
            raw->i_mode = 0;
            raw->i_size_bytes = 0;
            raw->i_blocks_allocated = 0;
            raw->i_indirect_block = FABRICZC_BLOCK_FREE;
            raw->i_double_indirect_block = FABRICZC_BLOCK_FREE;
            raw->i_triple_indirect_block = FABRICZC_BLOCK_FREE;
            raw->i_quad_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_triple_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_quad_indirect_block = FABRICZC_BLOCK_FREE;
            fabriczc_inode_slot_write(dir->i_sb, ino, raw);
        }
        kfree(raw);
    }
    return 0;
}

static int fabriczc_create(struct mnt_idmap *idmap, struct inode *dir,
                           struct dentry *dentry, umode_t mode)
{
    struct super_block *sb = dir->i_sb;
    struct fabriczc_inode *raw;
    struct inode *inode;
    u32 ino;
    int ret = fabriczc_alloc_inode_slot(sb, &ino);
    if (ret) return ret;

    raw = kmalloc(sizeof(*raw), GFP_KERNEL);
    if (!raw) { ret = -ENOMEM; goto err_slot; }
    memset(raw, 0xFF, sizeof(raw->i_direct_blocks));
    raw->i_mode = mode;
    raw->i_links_count = 1;
    raw->i_uid = raw->i_gid = 0;
    raw->i_size_bytes = 0;
    raw->i_blocks_allocated = 0;
    raw->i_generation = 0;
    raw->i_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_double_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_triple_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_quad_indirect_block = FABRICZC_BLOCK_FREE;
    memset(raw->i_direct_blocks, 0xFF, sizeof(raw->i_direct_blocks));
    ret = fabriczc_inode_slot_write(sb, ino, raw);
    if (ret) { kfree(raw); goto err_slot; }

    inode = iget_locked(sb, ino);
    fabriczc_attach_aops(inode);
    if (!inode) { kfree(raw); ret = -ENOMEM; goto err_slot; }
    if (inode_state_read_once(inode) & I_NEW) {
        inode_init_owner(idmap, inode, dir, mode);
        inode->i_mode = mode;
        inode->i_uid = GLOBAL_ROOT_UID;
        inode->i_gid = GLOBAL_ROOT_GID;
        inode_set_ctime_current(inode);
        inode->i_atime_sec = inode->i_mtime_sec = inode->i_ctime_sec;
        inode->i_size = 0;
        inode->i_blocks = 0;
        inode->i_op = &fabriczc_file_inode_operations;
        inode->i_fop = &fabriczc_file_operations;
        inode->i_private = raw;
        unlock_new_inode(inode);
    }
    ret = fabriczc_dir_add_entry(dir, dentry->d_name.name, ino, DT_REG);
    if (ret) { iput(inode); kfree(raw); goto err_slot; }
    d_instantiate(dentry, inode);
    return 0;
err_slot:
    return ret;
}

static struct dentry *fabriczc_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                                     struct dentry *dentry, umode_t mode)
{
    struct super_block *sb = dir->i_sb;
    struct fabriczc_inode *raw;
    struct inode *inode;
    u32 ino;
    u64 dblk;
    int ret = fabriczc_alloc_inode_slot(sb, &ino);
    if (ret) return ERR_PTR(ret);

    ret = fabriczc_alloc_data_block(sb, &dblk);
    if (ret) goto err_slot;

    {
        char *zero = kzalloc(4096, GFP_KERNEL);
        if (!zero) { ret = -ENOMEM; goto err_blk; }
        fabriczc_write_block(sb, dblk, zero, 4096);
        kfree(zero);
    }

    raw = kmalloc(sizeof(*raw), GFP_KERNEL);
    if (!raw) { ret = -ENOMEM; goto err_blk; }
    raw->i_mode = S_IFDIR | (mode & 0777);
    raw->i_links_count = 2;
    raw->i_uid = raw->i_gid = 0;
    raw->i_size_bytes = 4096;
    raw->i_blocks_allocated = 8;
    raw->i_generation = 0;
    raw->i_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_double_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_triple_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_quad_indirect_block = FABRICZC_BLOCK_FREE;
    memset(raw->i_direct_blocks, 0xFF, sizeof(raw->i_direct_blocks));
    raw->i_direct_blocks[0] = dblk;
    ret = fabriczc_inode_slot_write(sb, ino, raw);
    if (ret) { kfree(raw); goto err_blk; }

    inode = iget_locked(sb, ino);
    fabriczc_attach_aops(inode);
    if (!inode) { kfree(raw); ret = -ENOMEM; goto err_blk; }
    if (inode_state_read_once(inode) & I_NEW) {
        inode_init_owner(idmap, inode, dir, mode);
        inode->i_mode = S_IFDIR | (mode & 0777);
        inode->i_uid = GLOBAL_ROOT_UID;
        inode->i_gid = GLOBAL_ROOT_GID;
        inode_set_ctime_current(inode);
        inode->i_atime_sec = inode->i_mtime_sec = inode->i_ctime_sec;
        inode->i_size = 4096;
        inode->i_blocks = 8;
        inode->i_op = &fabriczc_dir_inode_operations;
        inode->i_fop = &fabriczc_dir_operations;
        inode->i_private = raw;
        unlock_new_inode(inode);
    }
    ret = fabriczc_dir_add_entry(dir, dentry->d_name.name, ino, DT_DIR);
    if (ret) { iput(inode); kfree(raw); goto err_blk; }
    d_instantiate(dentry, inode);
    return NULL; /* must not return the dentry: dcache contract for non-dcache fs */
err_blk:
    fabriczc_free_data_block(sb, dblk);
err_slot:
    return ERR_PTR(ret);
}

static int fabriczc_symlink(struct mnt_idmap *idmap, struct inode *dir,
                            struct dentry *dentry, const char *symname)
{
    struct super_block *sb = dir->i_sb;
    struct fabriczc_inode *raw;
    struct inode *inode;
    u32 ino;
    u64 dblk;
    u64 len = strlen(symname);
    int ret = fabriczc_alloc_inode_slot(sb, &ino);
    if (ret) return ret;

    ret = fabriczc_alloc_data_block(sb, &dblk);
    if (ret) goto err_slot;

    raw = kmalloc(sizeof(*raw), GFP_KERNEL);
    if (!raw) { ret = -ENOMEM; goto err_blk; }
    raw->i_mode = S_IFLNK | 0777;
    raw->i_links_count = 1;
    raw->i_uid = raw->i_gid = 0;
    raw->i_size_bytes = len;
    raw->i_blocks_allocated = 8;
    raw->i_generation = 0;
    raw->i_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_double_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_triple_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_quad_indirect_block = FABRICZC_BLOCK_FREE;
    memset(raw->i_direct_blocks, 0xFF, sizeof(raw->i_direct_blocks));
    raw->i_direct_blocks[0] = dblk;
    fabriczc_write_block(sb, dblk, symname, (u32)min(len, (u64)4096));
    ret = fabriczc_inode_slot_write(sb, ino, raw);
    if (ret) { kfree(raw); goto err_blk; }

    inode = iget_locked(sb, ino);
    fabriczc_attach_aops(inode);
    if (!inode) { kfree(raw); ret = -ENOMEM; goto err_blk; }
    if (inode_state_read_once(inode) & I_NEW) {
        inode->i_mode = S_IFLNK | 0777;
        inode->i_uid = GLOBAL_ROOT_UID;
        inode->i_gid = GLOBAL_ROOT_GID;
        inode_set_ctime_current(inode);
        inode->i_atime_sec = inode->i_mtime_sec = inode->i_ctime_sec;
        inode->i_size = len;
        inode->i_blocks = 8;
        inode->i_op = &fabriczc_symlink_inode_operations;
        inode->i_fop = &fabriczc_file_operations;
        inode->i_private = raw;
        unlock_new_inode(inode);
    }
    ret = fabriczc_dir_add_entry(dir, dentry->d_name.name, ino, DT_LNK);
    if (ret) { iput(inode); kfree(raw); goto err_blk; }
    d_instantiate(dentry, inode);
    return 0;
err_blk:
    fabriczc_free_data_block(sb, dblk);
err_slot:
    return ret;
}

static int fabriczc_mknod(struct mnt_idmap *idmap, struct inode *dir,
                          struct dentry *dentry, umode_t mode, dev_t rdev)
{
    struct super_block *sb = dir->i_sb;
    struct fabriczc_inode *raw;
    struct inode *inode;
    u32 ino;
    int ret = fabriczc_alloc_inode_slot(sb, &ino);
    if (ret) return ret;

    raw = kmalloc(sizeof(*raw), GFP_KERNEL);
    if (!raw) { ret = -ENOMEM; goto err_slot; }
    raw->i_mode = mode;
    raw->i_links_count = 1;
    raw->i_uid = raw->i_gid = 0;
    raw->i_size_bytes = 0;
    raw->i_blocks_allocated = 0;
    raw->i_generation = 0;
    raw->i_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_double_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_triple_indirect_block = FABRICZC_BLOCK_FREE;
    raw->i_quad_indirect_block = FABRICZC_BLOCK_FREE;
    memset(raw->i_direct_blocks, 0xFF, sizeof(raw->i_direct_blocks));
    ret = fabriczc_inode_slot_write(sb, ino, raw);
    if (ret) { kfree(raw); goto err_slot; }

    inode = iget_locked(sb, ino);
    fabriczc_attach_aops(inode);
    if (!inode) { kfree(raw); ret = -ENOMEM; goto err_slot; }
    if (inode_state_read_once(inode) & I_NEW) {
        inode->i_mode = mode;
        inode->i_uid = GLOBAL_ROOT_UID;
        inode->i_gid = GLOBAL_ROOT_GID;
        inode_set_ctime_current(inode);
        inode->i_atime_sec = inode->i_mtime_sec = inode->i_ctime_sec;
        inode->i_size = 0;
        inode->i_blocks = 0;
        inode->i_rdev = rdev;
        inode->i_op = &fabriczc_file_inode_operations;
        inode->i_fop = &fabriczc_file_operations;
        inode->i_private = raw;
        unlock_new_inode(inode);
    }
    ret = fabriczc_dir_add_entry(dir, dentry->d_name.name, ino,
                                 S_ISDIR(mode) ? DT_DIR :
                                 S_ISLNK(mode) ? DT_LNK :
                                 S_ISCHR(mode) ? DT_CHR :
                                 S_ISBLK(mode) ? DT_BLK : DT_REG);
    if (ret) { iput(inode); kfree(raw); goto err_slot; }
    d_instantiate(dentry, inode);
    return 0;
err_slot:
    return ret;
}

static int fabriczc_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
                            struct iattr *attr)
{
    struct inode *inode = d_inode(dentry);
    struct fabriczc_inode *raw = inode->i_private;
    int error = setattr_prepare(idmap, dentry, attr);
    if (error) return error;
    /*
     * setattr_copy() applies mode/owner/times but never ATTR_SIZE:
     * simple_setattr() covers size with truncate_setsize(), so without this
     * ftruncate() returned success while the file size never changed.
     */
    if (attr->ia_valid & ATTR_SIZE)
        truncate_setsize(inode, attr->ia_size);
    setattr_copy(idmap, inode, attr);
    if (raw) {
        raw->i_size_bytes = inode->i_size;
        raw->i_mode = inode->i_mode;
        raw->i_uid = i_uid_read(inode);
        raw->i_gid = i_gid_read(inode);
        fabriczc_inode_slot_write(inode->i_sb, inode->i_ino, raw);
    }
    mark_inode_dirty(inode);
    return 0;
}

static int fabriczc_rename(struct mnt_idmap *idmap, struct inode *old_dir,
                           struct dentry *old_dentry, struct inode *new_dir,
                           struct dentry *new_dentry, unsigned int flags)
{
    struct inode *inode = d_inode(old_dentry);
    u8 type = DT_REG;
    int ret;

    if (flags) return -EINVAL;
    if (inode) {
        if (S_ISDIR(inode->i_mode)) type = DT_DIR;
        else if (S_ISLNK(inode->i_mode)) type = DT_LNK;
        else if (S_ISCHR(inode->i_mode)) type = DT_CHR;
        else if (S_ISBLK(inode->i_mode)) type = DT_BLK;
    }
    ret = fabriczc_dir_remove_entry(old_dir, old_dentry->d_name.name);
    if (ret) return ret;
    ret = fabriczc_dir_add_entry(new_dir, new_dentry->d_name.name, inode->i_ino, type);
    if (ret) { /* attempt rollback */
        fabriczc_dir_add_entry(old_dir, old_dentry->d_name.name, inode->i_ino, type);
        return ret;
    }
    return 0;
}

static const char *fabriczc_get_link(struct dentry *dentry, struct inode *inode,
                                     struct delayed_call *done)
{
    struct fabriczc_inode *raw = inode->i_private;
    struct super_block *sb = inode->i_sb;
    char *buf;
    u64 len;

    if (!raw) return ERR_PTR(-EIO);
    len = raw->i_size_bytes;
    if (len > 4096) return ERR_PTR(-ENAMETOOLONG);
    buf = kmalloc(len + 1, GFP_KERNEL);
    if (!buf) return ERR_PTR(-ENOMEM);
    if (raw->i_direct_blocks[0] == FABRICZC_BLOCK_FREE ||
        fabriczc_read_block(sb, raw->i_direct_blocks[0], buf, (u32)len)) {
        kfree(buf);
        return ERR_PTR(-EIO);
    }
    buf[len] = '\0';
    set_delayed_call(done, fabriczc_free_link_buf, buf);
    return buf;
}

static const struct file_operations fabriczc_dir_operations = {
    .iterate_shared = fabriczc_iterate,
    .read           = generic_read_dir,
    .llseek         = generic_file_llseek,
};

static const struct inode_operations fabriczc_dir_inode_operations = {
    .lookup         = fabriczc_lookup,
    .create         = fabriczc_create,
    .unlink         = fabriczc_unlink,
    .mkdir          = fabriczc_mkdir,
    .rmdir          = fabriczc_unlink,
    .symlink        = fabriczc_symlink,
    .mknod          = fabriczc_mknod,
    .rename         = fabriczc_rename,
    .setattr        = fabriczc_setattr,
};

/*
 * msync(MS_SYNC) is routed through ->fsync. Without this hook the syscall
 * fails with -EINVAL and a caller cannot tell a successful flush from a
 * lost one, so shared mapped stores would be unverifiable to userspace.
 */
static int fabriczc_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
    struct inode *inode = file_inode(file);
    int ret;

    ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
    if (ret) return ret;
    return sync_blockdev(inode->i_sb->s_bdev);
}

static const struct file_operations fabriczc_file_operations = {
    .llseek         = generic_file_llseek,
    .read_iter      = fabriczc_read_iter,
    .write_iter     = fabriczc_write_iter,
    .mmap           = generic_file_mmap,
    .fsync          = fabriczc_fsync,
};

static const struct inode_operations fabriczc_file_inode_operations = {
    .setattr        = fabriczc_setattr,
};

static const struct inode_operations fabriczc_symlink_inode_operations = {
    .get_link       = fabriczc_get_link,
    .setattr        = fabriczc_setattr,
};

static int fabriczc_fill_super(struct super_block *sb, struct fs_context *fc)
{
    struct inode *root;
    struct fabriczc_inode *raw_root;
    struct fabriczc_superblock *sbk;
    struct fabriczc_sb_info *sbi;
    char *sb_buf;
    int ret;

    sb->s_maxbytes = MAX_LFS_FILESIZE;
    sb->s_blocksize = 4096;
    sb->s_blocksize_bits = 12;
    sb->s_magic = FABRICZC_SB_MAGIC;
    sb->s_op = &fabriczc_super_ops;

    sb_buf = kmalloc(4096, GFP_KERNEL);
    if (!sb_buf) return -ENOMEM;
    ret = fabriczc_read_block(sb, FABRICZC_SB_BLOCK, sb_buf, 4096);
    sbk = (struct fabriczc_superblock *)sb_buf;
    if (ret || sbk->sb_magic != FABRICZC_SB_MAGIC) {
        kfree(sb_buf);
        return -EILSEQ; /* not a FabricZC image */
    }

    sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
    if (!sbi) { kfree(sb_buf); return -ENOMEM; }
    spin_lock_init(&sbi->free_lock);
    sbi->free_blocks = sbk->free_blocks;
    sb->s_fs_info = sbi;
    kfree(sb_buf);

    fabriczc_mount_journal(sb, FABRICZC_JOURNAL_BLOCK);

    raw_root = kmalloc(sizeof(*raw_root), GFP_KERNEL);
    if (!raw_root) return -ENOMEM;
    ret = fabriczc_inode_slot_read(sb, 1, raw_root);
    if (ret) { kfree(raw_root); return ret; }

    root = iget_locked(sb, 1);
    fabriczc_attach_aops(root);
    if (!root) { kfree(raw_root); return -ENOMEM; }
    if (inode_state_read_once(root) & I_NEW) {
        root->i_mode = S_IFDIR | 0755;
        root->i_uid = GLOBAL_ROOT_UID;
        root->i_gid = GLOBAL_ROOT_GID;
        inode_set_ctime_current(root);
        root->i_atime_sec = root->i_mtime_sec = root->i_ctime_sec;
        root->i_size = raw_root->i_size_bytes;
        root->i_blocks = raw_root->i_blocks_allocated;
        root->i_op = &fabriczc_dir_inode_operations;
        root->i_fop = &fabriczc_dir_operations;
        root->i_private = raw_root;
        unlock_new_inode(root);
    }

    sb->s_root = d_make_root(root);
    if (!sb->s_root) return -ENOMEM;
    return 0;
}

static int fabriczc_get_tree(struct fs_context *fc)
{
    return get_tree_bdev(fc, fabriczc_fill_super);
}

static const struct fs_context_operations fabriczc_context_ops = {
    .get_tree       = fabriczc_get_tree,
};

static int fabriczc_init_fs_context(struct fs_context *fc)
{
    fabriczc_discover_hardware_targets();
    fc->ops = &fabriczc_context_ops;
    return 0;
}

static struct file_system_type fabriczc_fs_type = {
    .owner           = THIS_MODULE,
    .name            = "fabriczc",
    .init_fs_context = fabriczc_init_fs_context,
    .kill_sb         = kill_block_super,
    .fs_flags        = FS_REQUIRES_DEV,
};

/* Inode table is a fixed 1024-block region; the slot index is the inode number. */
static u32 fabriczc_inode_slots(void)
{
    return (FABRICZC_INODE_TABLE_BLOCKS * 4096) / (u32)sizeof(struct fabriczc_inode);
}

/*
 * A slot is free when i_mode is 0, which is exactly what the slot allocator
 * reuses. Bounded by the 128 KiB inode table, independent of device size.
 */
static u64 fabriczc_count_free_inode_slots(struct super_block *sb)
{
    struct fabriczc_inode *raw;
    u32 slots = fabriczc_inode_slots();
    u64 free = 0;
    u32 i;

    raw = kmalloc(sizeof(*raw), GFP_KERNEL);
    if (!raw) return 0;
    for (i = 1; i < slots; i++) {
        if (fabriczc_inode_slot_read(sb, i, raw)) break;
        if (raw->i_mode == 0) free++;
    }
    kfree(raw);
    return free;
}

/*
 * O(1): reports the live count maintained by the allocation paths. Counting
 * free bits by reading every bitmap block meant ~119k block reads (476 MB) on
 * the 14.6 TB array, i.e. 10-14 s per call, which stalled every df/umount.
 */
static int fabriczc_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    struct super_block *sb = dentry->d_sb;
    struct fabriczc_sb_info *sbi;
    u64 total;

    memset(buf, 0, sizeof(*buf));
    buf->f_type   = FABRICZC_SB_MAGIC;
    buf->f_bsize  = 4096;
    buf->f_namelen = 255;

    if (!sb || !sb->s_bdev) return -ENODEV;
    total = bdev_nr_bytes(sb->s_bdev) >> 12;
    buf->f_blocks = total;
    buf->f_files = fabriczc_inode_slots() - 1; /* slot 0 is reserved */
    buf->f_ffree = fabriczc_count_free_inode_slots(sb);

    sbi = (struct fabriczc_sb_info *)sb->s_fs_info;
    if (sbi) {
        unsigned long flags;

        spin_lock_irqsave(&sbi->free_lock, flags);
        buf->f_bfree = sbi->free_blocks;
        spin_unlock_irqrestore(&sbi->free_lock, flags);
        if (buf->f_bfree > total) buf->f_bfree = total;
        buf->f_bavail = buf->f_bfree;
    }
    return 0;
}

static int fabriczc_sync_fs(struct super_block *sb, int wait);

static const struct super_operations fabriczc_super_ops = {
    .evict_inode    = fabriczc_evict_inode,
    .put_super      = fabriczc_put_super,
    .sync_fs        = fabriczc_sync_fs,
    .statfs         = fabriczc_statfs,
};

/*
 * Persist the live free-block count so the next mount can report it without
 * rescanning the bitmap; the bitmap stays authoritative for allocation.
 *
 * The superblock lives on the target device, not on the synthetic
 * /dev/fabriczc0 that sb->s_bdev points at, so it is read and written with the
 * raw metadata helpers. sb_bread() here silently wrote to the wrong device and
 * the count never reached the array.
 */
static int fabriczc_persist_free_blocks(struct super_block *sb)
{
    struct fabriczc_sb_info *sbi = sb ? (struct fabriczc_sb_info *)sb->s_fs_info : NULL;
    struct fabriczc_superblock *sbk;
    struct fabriczc_runtime_context *ctx;
    char *buf;
    int ret = 0;

    if (!sbi) return 0;

    buf = kmalloc(4096, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    if (fabriczc_read_block(sb, FABRICZC_SB_BLOCK, buf, 4096) == 0) {
        unsigned long flags;

        sbk = (struct fabriczc_superblock *)buf;
        spin_lock_irqsave(&sbi->free_lock, flags);
        sbk->free_blocks = sbi->free_blocks;
        spin_unlock_irqrestore(&sbi->free_lock, flags);

        ret = fabriczc_write_block(sb, FABRICZC_SB_BLOCK, buf, 4096);
        if (!ret) {
            ctx = fabriczc_get_runtime_ctx();
            if (ctx && ctx->bdev)
                sync_blockdev(ctx->bdev);
        }
    } else {
        ret = -EIO;
    }

    kfree(buf);
    return ret;
}

/* sync(2)/syncfs() persist the counter too, not just unmount. */
static int fabriczc_sync_fs(struct super_block *sb, int wait)
{
    (void)wait;
    return fabriczc_persist_free_blocks(sb);
}

/*
 * generic_shutdown_super() already runs sync_filesystem() before ->put_super,
 * so a second sync here is redundant. Metadata buffers are flushed
 * synchronously (sync_dirty_buffer) when they are written.
 */
void fabriczc_put_super(struct super_block *sb)
{
    struct fabriczc_sb_info *sbi = sb ? (struct fabriczc_sb_info *)sb->s_fs_info : NULL;

    if (!sbi) return;

    fabriczc_persist_free_blocks(sb);

    kfree(sbi);
    sb->s_fs_info = NULL;
}
int fabriczc_fs_init(void) { return register_filesystem(&fabriczc_fs_type); }
void fabriczc_fs_exit(void) { unregister_filesystem(&fabriczc_fs_type); }
