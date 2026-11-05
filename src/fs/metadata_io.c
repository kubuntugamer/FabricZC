/*
 * src/fs/metadata_io.c - FabricZC Layer 2 Superblock and Array Metadata I/O Subsystem (Fixed Flush)
 */
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/jiffies.h>
#include <fabriczc_internal.h>
#include <fabriczc_common.h>

int fabriczc_verify_superblock(struct fabriczc_superblock *sb)
{
    if (unlikely(!sb)) return -EINVAL;
    if (sb->sb_magic != FABRICZC_SB_MAGIC) return -EILSEQ;
    return 0;
}



int fabriczc_mount_journal(struct super_block *sb, u64 journal_block)
{
    struct buffer_head *bh;
    struct fabriczc_journal_header *jh;
    if (unlikely(!sb || journal_block == 0)) return -EINVAL;
    bh = sb_bread(sb, journal_block);
    if (!bh) return -EIO;
    jh = (struct fabriczc_journal_header *)bh->b_data;
    if (jh->jh_magic != FABRICZC_JOURNAL_MAGIC) {
        memset(bh->b_data, 0, 4096);
        jh->jh_magic = FABRICZC_JOURNAL_MAGIC;
        jh->jh_block_size = 4096;
        jh->jh_sequence_num = 1;
        mark_buffer_dirty(bh);
        sync_dirty_buffer(bh);
    }
    brelse(bh);
    return 0;
}

int fabriczc_journal_replay(struct super_block *sb, u64 journal_block)
{
    struct buffer_head *bh;
    struct fabriczc_journal_header *jh;
    if (unlikely(!sb || journal_block == 0)) return -EINVAL;
    bh = sb_bread(sb, journal_block);
    if (!bh) return -EIO;
    jh = (struct fabriczc_journal_header *)bh->b_data;
    if (jh->jh_magic == FABRICZC_JOURNAL_MAGIC && jh->jh_head_offset != jh->jh_tail_offset) {
        jh->jh_head_offset = jh->jh_tail_offset;
        mark_buffer_dirty(bh);
        sync_dirty_buffer(bh);
    }
    brelse(bh);
    return 0;
}

struct fabriczc_transaction_context *fabriczc_trans_start(struct super_block *sb)
{
    struct fabriczc_transaction_context *tx;
    static atomic_t global_tx_sequence = ATOMIC_INIT(1000);
    if (unlikely(!sb)) return ERR_PTR(-EINVAL);
    tx = kmalloc(sizeof(struct fabriczc_transaction_context), GFP_NOFS);
    if (!tx) return ERR_PTR(-ENOMEM);
    tx->t_tx_id = atomic_inc_return(&global_tx_sequence);
    tx->t_status_flags = FABRICZC_TRANS_ACTIVE;
    tx->t_start_time_jiffies = jiffies;
    tx->t_blocks_modified = 0;
    spin_lock_init(&tx->t_lock);
    return tx;
}

int fabriczc_trans_commit(struct fabriczc_transaction_context *tx)
{
    unsigned long flags;
    if (unlikely(!tx)) return -EINVAL;
    spin_lock_irqsave(&tx->t_lock, flags);
    tx->t_status_flags = FABRICZC_TRANS_COMMITTED;
    spin_unlock_irqrestore(&tx->t_lock, flags);
    kfree(tx);
    return 0;
}

int fabriczc_trans_commit_to_journal(struct super_block *sb, struct fabriczc_transaction_context *tx, u64 journal_block)
{
    int ret;
    if (unlikely(!sb || !tx || journal_block == 0)) return -EINVAL;
    if (tx->t_status_flags != FABRICZC_TRANS_ACTIVE) return -EINVAL;
    ret = fabriczc_journal_append_entry(sb, journal_block, FABRICZC_JNL_OP_INODE, 0, NULL, 0);
    if (ret < 0) return ret;
    return fabriczc_trans_commit(tx);
}

int fabriczc_journal_append_entry(struct super_block *sb, u64 journal_block, u16 op_type, u64 target_blk, void *data, u16 len)
{
    struct buffer_head *hdr_bh;
    struct fabriczc_journal_header *jh;
    char *journal_data_ptr;
    u32 current_tail;
    if (unlikely(!sb || journal_block == 0)) return -EINVAL;
    hdr_bh = sb_bread(sb, journal_block);
    if (!hdr_bh) return -EIO;
    jh = (struct fabriczc_journal_header *)hdr_bh->b_data;
    current_tail = jh->jh_tail_offset;
    journal_data_ptr = (char *)hdr_bh->b_data + sizeof(struct fabriczc_journal_header) + current_tail;
    if (sizeof(struct fabriczc_journal_header) + current_tail + len + 16 > 4096) {
        current_tail = 0;
        journal_data_ptr = (char *)hdr_bh->b_data + sizeof(struct fabriczc_journal_header);
    }
    *((u16 *)journal_data_ptr) = op_type;
    *((u64 *)(journal_data_ptr + 2)) = target_blk;
    *((u16 *)(journal_data_ptr + 10)) = len;
    if (data && len > 0) memcpy(journal_data_ptr + 12, data, len);
    jh->jh_tail_offset = current_tail + len + 16;
    jh->jh_total_records++;
    jh->jh_sequence_num++;
    mark_buffer_dirty(hdr_bh);
    sync_dirty_buffer(hdr_bh);
    brelse(hdr_bh);
    return 0;
}

int fabriczc_find_and_allocate_inode(void *bitmap_page, unsigned int max_inodes_per_group) { return -ENOSPC; }

/* --- Minimal-correct allocation engine helpers ----------------------- */

static u64 fabriczc_sb_total_blocks(struct super_block *sb)
{
    if (unlikely(!sb || !sb->s_bdev)) return 0;
    return bdev_nr_bytes(sb->s_bdev) >> sb->s_blocksize_bits;
}

/*
 * Keep a live free-block count so statfs() never walks the allocation bitmap:
 * on a 14.6 TB array that scan is ~119k block reads, measured at 10-14 s per
 * call. Allocation decisions still come from the bitmap; this is reporting
 * state, persisted to the superblock by fabriczc_put_super().
 */
static void fabriczc_free_blocks_add(struct super_block *sb, s64 delta)
{
    struct fabriczc_sb_info *sbi = sb ? (struct fabriczc_sb_info *)sb->s_fs_info : NULL;
    unsigned long flags;

    if (!sbi) return;
    spin_lock_irqsave(&sbi->free_lock, flags);
    if (delta < 0) {
        if (sbi->free_blocks) sbi->free_blocks--;
    } else {
        sbi->free_blocks++;
    }
    spin_unlock_irqrestore(&sbi->free_lock, flags);
}

static int fabriczc_bitmap_test_set(struct super_block *sb, u64 blk, bool set)
{
    struct buffer_head *bh;
    u64 bitmap_block = FABRICZC_BITMAP_START + (blk / FABRICZC_BITS_PER_BLOCK);
    u32 bit = (u32)(blk % FABRICZC_BITS_PER_BLOCK);
    if (bitmap_block >= FABRICZC_BITMAP_START + fabriczc_bitmap_block_count(fabriczc_sb_total_blocks(sb)))
        return -ENOSPC;
    bh = sb_bread(sb, bitmap_block);
    if (!bh) return -EIO;
    if (set)
        set_bit(bit, (unsigned long *)bh->b_data);
    else
        clear_bit(bit, (unsigned long *)bh->b_data);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    return 0;
}

/*
 * Scan [from, to) of the allocation bitmap for up to `max_blocks` free bits in
 * a row and claim them with a single bitmap write.
 *
 * Claiming one block per bitmap update meant every 4 KiB of file data cost a
 * read plus a synchronous write of the bitmap block, which is what held the
 * array to 63 MB/s. A run of 512 blocks amortises that over 2 MiB of data and
 * has the side effect of handing the data path physically contiguous blocks to
 * write in one request.
 *
 * The caller holds sbi->alloc_mutex across the test-and-set, so two writers
 * cannot be handed the same block.
 */
static int fabriczc_claim_bitmap_run(struct super_block *sb, u64 from, u64 to,
                                     u32 max_blocks, u64 *out_blk, u32 *out_count)
{
    u64 total = fabriczc_sb_total_blocks(sb);
    u64 cur = from;

    if (to > total) to = total;
    while (cur < to) {
        u64 group = cur / FABRICZC_BITS_PER_BLOCK;
        u64 group_base = group * FABRICZC_BITS_PER_BLOCK;
        u64 group_end = group_base + FABRICZC_BITS_PER_BLOCK;
        u32 bit = (u32)(cur - group_base);
        u64 run_start = 0;
        u32 run = 0;
        struct buffer_head *bh;

        if (group_end > to) group_end = to;
        bh = sb_bread(sb, FABRICZC_BITMAP_START + group);
        if (!bh) return -EIO;
        for (; bit < FABRICZC_BITS_PER_BLOCK; bit++) {
            u64 blk = group_base + bit;

            if (blk >= group_end) break;
            if (test_bit(bit, (unsigned long *)bh->b_data)) {
                if (run) break; /* the free run ended here */
                continue;
            }
            if (!run) run_start = blk;
            if (++run >= max_blocks) break;
        }
        if (run) {
            u32 i;

            for (i = 0; i < run; i++)
                set_bit((u32)(run_start - group_base) + i,
                        (unsigned long *)bh->b_data);
            mark_buffer_dirty(bh);
            sync_dirty_buffer(bh);
            brelse(bh);
            *out_blk = run_start;
            *out_count = run;
            return 0;
        }
        brelse(bh);
        cur = group_end;
    }
    return -ENOSPC;
}

/*
 * Blocks claimed but not yet handed out stay marked used in the bitmap, so a
 * mount that finishes with a partly used window would leak them. Give them
 * back before the last sync: free_blocks never counted them.
 */
void fabriczc_drop_reservation(struct super_block *sb)
{
    struct fabriczc_sb_info *sbi = sb ? (struct fabriczc_sb_info *)sb->s_fs_info : NULL;
    u64 blk;

    if (!sbi) return;
    mutex_lock(&sbi->alloc_mutex);
    blk = sbi->reserve_start;
    while (sbi->reserve_count) {
        u64 group = blk / FABRICZC_BITS_PER_BLOCK;
        u64 group_base = group * FABRICZC_BITS_PER_BLOCK;
        u32 bit = (u32)(blk - group_base);
        struct buffer_head *bh;

        bh = sb_bread(sb, FABRICZC_BITMAP_START + group);
        if (!bh) break;
        for (; bit < FABRICZC_BITS_PER_BLOCK && sbi->reserve_count;
             bit++, blk++, sbi->reserve_count--)
            clear_bit(bit, (unsigned long *)bh->b_data);
        mark_buffer_dirty(bh);
        sync_dirty_buffer(bh);
        brelse(bh);
    }
    sbi->reserve_start = 0;
    sbi->reserve_count = 0;
    mutex_unlock(&sbi->alloc_mutex);
}

/*
 * Allocation starts at a moving cursor instead of at the first data block.
 * Restarting at block 0 on every call made a sequential fill O(n^2) in bitmap
 * scans: a 1 GiB write spent most of its time re-reading the same early bitmap
 * blocks, and throughput fell as the volume filled.
 */
int fabriczc_alloc_data_block(struct super_block *sb, u64 *out_blk)
{
    struct fabriczc_sb_info *sbi = sb ? (struct fabriczc_sb_info *)sb->s_fs_info : NULL;
    u64 total, first, start;
    int ret = 0;

    if (unlikely(!out_blk || !sbi)) return -EINVAL;

    total = fabriczc_sb_total_blocks(sb);
    first = fabriczc_data_start(total);
    if (first >= total) return -ENOSPC;

    mutex_lock(&sbi->alloc_mutex);
    if (!sbi->reserve_count) {
        u32 count = 0;

        start = sbi->alloc_hint;
        if (start < first || start >= total) start = first;

        ret = fabriczc_claim_bitmap_run(sb, start, total, FABRICZC_ALLOC_BATCH,
                                        &sbi->reserve_start, &count);
        if (ret == -ENOSPC && start > first)
            ret = fabriczc_claim_bitmap_run(sb, first, start,
                                            FABRICZC_ALLOC_BATCH,
                                            &sbi->reserve_start, &count);
        if (ret) {
            mutex_unlock(&sbi->alloc_mutex);
            return ret;
        }
        sbi->reserve_count = count;
    }
    *out_blk = sbi->reserve_start++;
    sbi->reserve_count--;
    sbi->alloc_hint = sbi->reserve_start;
    fabriczc_free_blocks_add(sb, -1);
    mutex_unlock(&sbi->alloc_mutex);
    return 0;
}

int fabriczc_free_data_block(struct super_block *sb, u64 blk)
{
    struct fabriczc_sb_info *sbi = sb ? (struct fabriczc_sb_info *)sb->s_fs_info : NULL;
    int ret;

    if (unlikely(!sbi)) return -EINVAL;
    if (blk < fabriczc_data_start(fabriczc_sb_total_blocks(sb))) return -EINVAL;

    mutex_lock(&sbi->alloc_mutex);
    ret = fabriczc_bitmap_test_set(sb, blk, false);
    if (!ret) {
        if (blk < sbi->alloc_hint) sbi->alloc_hint = blk;
        fabriczc_free_blocks_add(sb, 1);
    }
    mutex_unlock(&sbi->alloc_mutex);
    return ret;
}

/* -- Truncate and whole-tree release --------------------------------- */

/* Blocks covered by one slot at `level` (level 1 is a single data block). */
static u64 fabriczc_tree_span(u32 level)
{
    u64 span = 1;
    u32 i;

    for (i = 1; i < level; i++)
        span *= FABRICZC_INDIRECT_ENTRIES;
    return span;
}

/* Free every block below *slot; `freed` accumulates 512-byte sectors. */
static void fabriczc_free_tree(struct super_block *sb, u64 *slot, u32 level,
                               u64 *freed)
{
    u64 blk = *slot;
    u64 *table;
    int i;

    if (blk == FABRICZC_BLOCK_FREE || blk == 0) {
        *slot = FABRICZC_BLOCK_FREE;
        return;
    }
    if (level <= 1) {
        if (fabriczc_free_data_block(sb, blk) == 0)
            *freed += 8;
        *slot = FABRICZC_BLOCK_FREE;
        return;
    }
    table = kmalloc(4096, GFP_NOFS);
    if (table) {
        if (fabriczc_read_block(sb, blk, table, 4096) == 0) {
            for (i = 0; i < FABRICZC_INDIRECT_ENTRIES; i++)
                fabriczc_free_tree(sb, &table[i], level - 1, freed);
        }
        kfree(table);
    }
    if (fabriczc_free_data_block(sb, blk) == 0)
        *freed += 8;
    *slot = FABRICZC_BLOCK_FREE;
}

/*
 * Free only the part of a subtree that lies at or above logical block `keep`.
 * `base` is the first logical block covered by *slot.
 */
static void fabriczc_truncate_walk(struct super_block *sb, u64 *slot, u32 level,
                                   u64 base, u64 keep, u64 *freed)
{
    u64 span, blk = *slot, *table;
    u64 child_span;
    bool empty = true;
    u32 i;

    if (blk == FABRICZC_BLOCK_FREE || blk == 0) {
        *slot = FABRICZC_BLOCK_FREE;
        return;
    }
    span = fabriczc_tree_span(level);
    if (base >= keep) {
        fabriczc_free_tree(sb, slot, level, freed);
        return;
    }
    if (base + span <= keep) return; /* wholly inside the kept range */
    if (level <= 1) return;

    table = kmalloc(4096, GFP_NOFS);
    if (!table) return; /* leak rather than corrupt */
    if (fabriczc_read_block(sb, blk, table, 4096)) {
        kfree(table);
        return;
    }

    child_span = span / FABRICZC_INDIRECT_ENTRIES;
    for (i = 0; i < FABRICZC_INDIRECT_ENTRIES; i++) {
        u64 child_base = base + (u64)i * child_span;

        if (child_base >= keep) {
            fabriczc_free_tree(sb, &table[i], level - 1, freed);
        } else if (child_base + child_span > keep) {
            fabriczc_truncate_walk(sb, &table[i], level - 1, child_base, keep,
                                   freed);
        }
        if (table[i] != FABRICZC_BLOCK_FREE && table[i] != 0)
            empty = false;
    }

    if (empty) {
        kfree(table);
        if (fabriczc_free_data_block(sb, blk) == 0)
            *freed += 8;
        *slot = FABRICZC_BLOCK_FREE;
        return;
    }
    fabriczc_write_block(sb, blk, table, 4096);
    kfree(table);
}

int fabriczc_truncate_blocks(struct inode *inode, u64 newsize)
{
    struct fabriczc_inode *raw = inode ? inode->i_private : NULL;
    struct super_block *sb = inode ? inode->i_sb : NULL;
    u64 keep = (newsize + 4095) >> 12;
    u64 base, freed = 0;
    u32 i;

    if (!raw || !sb) return -EINVAL;

    for (i = 0; i < FABRICZC_N_DIRECT_BLOCKS; i++) {
        if ((u64)i < keep) continue;
        if (raw->i_direct_blocks[i] == FABRICZC_BLOCK_FREE) continue;
        if (fabriczc_free_data_block(sb, raw->i_direct_blocks[i]) == 0)
            freed += 8;
        raw->i_direct_blocks[i] = FABRICZC_BLOCK_FREE;
    }

    base = FABRICZC_N_DIRECT_BLOCKS;
    fabriczc_truncate_walk(sb, &raw->i_indirect_block, 2, base, keep, &freed);
    base += fabriczc_tree_span(2);
    fabriczc_truncate_walk(sb, &raw->i_double_indirect_block, 3, base, keep, &freed);
    base += fabriczc_tree_span(3);
    fabriczc_truncate_walk(sb, &raw->i_triple_indirect_block, 4, base, keep, &freed);
    base += fabriczc_tree_span(4);
    fabriczc_truncate_walk(sb, &raw->i_quad_indirect_block, 5, base, keep, &freed);

    raw->i_size_bytes = newsize;
    raw->i_blocks_allocated = raw->i_blocks_allocated > freed
                            ? raw->i_blocks_allocated - freed : 0;
    fabriczc_inode_slot_write(sb, inode->i_ino, raw);
    return 0;
}

int fabriczc_inode_slot_read(struct super_block *sb, u32 ino, struct fabriczc_inode *raw)
{
    u64 total = fabriczc_sb_total_blocks(sb);
    u32 table_block = fabriczc_inode_table_start(total) +
                      (u32)(((u64)ino * sizeof(*raw)) / 4096);
    u32 off = (u32)(((u64)ino * sizeof(*raw)) % 4096);
    struct buffer_head *bh = sb_bread(sb, table_block);
    if (!bh) return -EIO;
    memcpy(raw, bh->b_data + off, sizeof(*raw));
    brelse(bh);
    return 0;
}

int fabriczc_inode_slot_write(struct super_block *sb, u32 ino, const struct fabriczc_inode *raw)
{
    u64 total = fabriczc_sb_total_blocks(sb);
    u32 table_block = fabriczc_inode_table_start(total) +
                      (u32)(((u64)ino * sizeof(*raw)) / 4096);
    u32 off = (u32)(((u64)ino * sizeof(*raw)) % 4096);
    struct buffer_head *bh = sb_bread(sb, table_block);
    if (!bh) return -EIO;
    memcpy(bh->b_data + off, raw, sizeof(*raw));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    return 0;
}

/*
 * Same slot update without the device round trip. Allocation uses this: the
 * inode is rewritten once per block mapped, and waiting for that write for
 * every 4 KiB halved the write throughput on its own. The block stays dirty in
 * the buffer cache and is flushed by the run sync, fsync() or unmount.
 */
int fabriczc_inode_slot_write_deferred(struct super_block *sb, u32 ino,
                                       const struct fabriczc_inode *raw)
{
    u64 total = fabriczc_sb_total_blocks(sb);
    u32 table_block = fabriczc_inode_table_start(total) +
                      (u32)(((u64)ino * sizeof(*raw)) / 4096);
    u32 off = (u32)(((u64)ino * sizeof(*raw)) % 4096);
    struct buffer_head *bh = sb_getblk(sb, table_block);
    if (!bh) return -ENOMEM;
    if (!buffer_uptodate(bh)) {
        /* Never read this block: only the target slot matters to the caller,
         * but leaving the rest of the table uninitialised on disk would clobber
         * neighbouring inodes, so read it in. */
        brelse(bh);
        bh = sb_bread(sb, table_block);
        if (!bh) return -EIO;
    }
    memcpy(bh->b_data + off, raw, sizeof(*raw));
    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

/*
 * Find a free inode slot starting at the cursor, scanning a whole table block
 * (16 slots) at a time. Slots are u32 and the table can hold millions, so a
 * restart-from-slot-1 scan would make every create progressively slower.
 */
int fabriczc_alloc_inode_slot(struct super_block *sb, u32 *out_ino)
{
    struct fabriczc_sb_info *sbi = sb ? (struct fabriczc_sb_info *)sb->s_fs_info : NULL;
    u64 total = fabriczc_sb_total_blocks(sb);
    u32 table_start = fabriczc_inode_table_start(total);
    u32 table_blocks = fabriczc_inode_table_blocks(total);
    u32 per_block = 4096 / sizeof(struct fabriczc_inode);
    u32 start_block, scanned;

    if (unlikely(!out_ino || !sbi || !table_blocks)) return -EINVAL;

    start_block = sbi->inode_hint / per_block;
    if (start_block >= table_blocks) start_block = 0;

    for (scanned = 0; scanned < table_blocks; scanned++) {
        unsigned int idx;
        u32 block = start_block + scanned;
        struct buffer_head *bh;

        if (block >= table_blocks) block -= table_blocks;
        bh = sb_bread(sb, table_start + block);
        if (!bh) return -EIO;
        for (idx = 0; idx < per_block; idx++) {
            struct fabriczc_inode *raw = (struct fabriczc_inode *)(bh->b_data +
                                          (idx * sizeof(struct fabriczc_inode)));
            u32 ino = block * per_block + idx;

            if (ino == 0 || raw->i_mode != 0) continue;
            memset(raw, 0, sizeof(*raw));
            raw->i_mode = 1; /* claimed; the caller writes the real metadata */
            mark_buffer_dirty(bh);
            sync_dirty_buffer(bh);
            brelse(bh);
            sbi->inode_hint = ino + 1;
            if (sbi->free_inodes) sbi->free_inodes--;
            *out_ino = ino;
            return 0;
        }
        brelse(bh);
    }
    return -ENOSPC;
}

/*
 * Index-table helpers for files past the single-indirect range.
 * A zero pointer is treated as "unset" because images created before
 * i_double_indirect_block existed carry a zero-filled tail.
 */
static bool fabriczc_table_ptr_unset(u64 blk)
{
    return blk == FABRICZC_BLOCK_FREE || blk == 0;
}

/* Allocate a 4 KiB index table and prefill it with FABRICZC_BLOCK_FREE */
static int fabriczc_alloc_index_table(struct super_block *sb, u64 *out_blk)
{
    char *zero;
    u64 blk;
    int i, ret;

    ret = fabriczc_alloc_data_block(sb, &blk);
    if (ret) return ret;

    zero = kzalloc(4096, GFP_KERNEL);
    if (!zero) { fabriczc_free_data_block(sb, blk); return -ENOMEM; }
    for (i = 0; i < FABRICZC_INDIRECT_ENTRIES; i++)
        ((u64 *)zero)[i] = FABRICZC_BLOCK_FREE;
    ret = fabriczc_write_block(sb, blk, zero, 4096);
    kfree(zero);
    if (ret) { fabriczc_free_data_block(sb, blk); return ret; }

    *out_blk = blk;
    return 0;
}

/*
 * Walk one pointer-tree level. level == 1 means "slot holds a data block";
 * level N means "slot holds a table of 512 level N-1 slots". Tables are only
 * written back when something below them was allocated, so read lookups do
 * no writes at all.
 */
static int fabriczc_bmap_walk(struct inode *inode, u32 level, u64 *slot,
                              u64 iblock, u64 *phys, bool create, bool *changed)
{
    struct super_block *sb = inode->i_sb;
    struct fabriczc_inode *raw = inode->i_private;
    u64 per_branch = 1, blk;
    u64 *table;
    u32 idx, i;
    int ret;

    if (level == 1) {
        if (fabriczc_table_ptr_unset(*slot)) {
            if (!create) { *phys = FABRICZC_BLOCK_FREE; return 0; }
            ret = fabriczc_alloc_data_block(sb, &blk);
            if (ret) return ret;
            *slot = blk;
            *changed = true;
            raw->i_blocks_allocated += 8;
            fabriczc_inode_slot_write_deferred(sb, inode->i_ino, raw);
        }
        *phys = *slot;
        return 0;
    }

    /* A level-2 entry points straight at a data block; each level above it
     * fans out by another 512 entries, so an entry at level L covers
     * 512^(L-2) data blocks. */
    for (i = 2; i < level; i++)
        per_branch *= FABRICZC_INDIRECT_ENTRIES;
    idx = (u32)(iblock / per_branch);
    if (idx >= FABRICZC_INDIRECT_ENTRIES) return -EFBIG;

    if (fabriczc_table_ptr_unset(*slot)) {
        if (!create) { *phys = FABRICZC_BLOCK_FREE; return 0; }
        ret = fabriczc_alloc_index_table(sb, &blk);
        if (ret) return ret;
        *slot = blk;
        *changed = true;
        raw->i_blocks_allocated += 8;
        fabriczc_inode_slot_write_deferred(sb, inode->i_ino, raw);
    }

    table = kmalloc(4096, GFP_KERNEL);
    if (!table) return -ENOMEM;
    ret = fabriczc_read_block(sb, *slot, table, 4096);
    if (ret) { kfree(table); return ret; }

    ret = fabriczc_bmap_walk(inode, level - 1, &table[idx], iblock % per_branch,
                             phys, create, changed);
    if (!ret && *changed) {
        /* Deferred: this table is rewritten once per newly mapped block, and a
         * device round trip here dominated sequential writes. */
        struct buffer_head *bh = sb_getblk(sb, *slot);

        if (!bh) ret = -ENOMEM;
        else {
            memcpy(bh->b_data, table, 4096);
            set_buffer_uptodate(bh);
            mark_buffer_dirty(bh);
            brelse(bh);
        }
    }
    kfree(table);
    return ret;
}

/*
 * Map a file block. Direct slots cover 12 blocks; then 512, 512^2, 512^3 and
 * 512^4 blocks through the four index levels, so the ceiling is ~256 TiB per
 * file instead of the old 4 GiB, with 64-bit block numbers throughout.
 */
int fabriczc_bmap_block(struct inode *inode, u64 iblock, u64 *phys, bool create)
{
    struct fabriczc_inode *raw = inode->i_private;
    const u64 l1 = FABRICZC_INDIRECT_ENTRIES;
    const u64 l2 = l1 * FABRICZC_INDIRECT_ENTRIES;
    const u64 l3 = l2 * FABRICZC_INDIRECT_ENTRIES;
    const u64 l4 = l3 * FABRICZC_INDIRECT_ENTRIES;
    bool changed = false;
    u64 idx = iblock;

    if (unlikely(!inode || !raw || !phys)) return -EINVAL;

    if (idx < FABRICZC_N_DIRECT_BLOCKS)
        return fabriczc_bmap_walk(inode, 1, &raw->i_direct_blocks[idx], 0,
                                  phys, create, &changed);
    idx -= FABRICZC_N_DIRECT_BLOCKS;
    if (idx < l1)
        return fabriczc_bmap_walk(inode, 2, &raw->i_indirect_block, idx,
                                  phys, create, &changed);
    idx -= l1;
    if (idx < l2)
        return fabriczc_bmap_walk(inode, 3, &raw->i_double_indirect_block, idx,
                                  phys, create, &changed);
    idx -= l2;
    if (idx < l3)
        return fabriczc_bmap_walk(inode, 4, &raw->i_triple_indirect_block, idx,
                                  phys, create, &changed);
    idx -= l3;
    if (idx < l4)
        return fabriczc_bmap_walk(inode, 5, &raw->i_quad_indirect_block, idx,
                                  phys, create, &changed);
    return -EFBIG;
}

/*
 * Buffer_head callers (mpage writeback, bmap) pass filesystem-relative block
 * numbers, while map_bh()/b_blocknr are in 512-byte sectors. Shifting the
 * incoming block number as well made every page of a range resolve to the same
 * logical block, which surfaced as -EIO on the first msync().
 */
int fabriczc_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
{
    u64 phys;
    int ret = fabriczc_bmap_block(inode, (u64)iblock, &phys, create != 0);
    if (ret) return ret;
    if (phys == FABRICZC_BLOCK_FREE) return 0;
    map_bh(bh_result, inode->i_sb, (sector_t)phys << (inode->i_sb->s_blocksize_bits - 9));
    return 0;
}

/*
 * Metadata I/O goes through the mounted superblock's own device, never a
 * module-global one. A filesystem must work on whatever block device mount(2)
 * was given, the same as every other filesystem; reaching for a device the
 * module discovered by itself meant the image could only ever be mounted on the
 * synthetic /dev/fabriczc0 node.
 *
 * Writes are synchronous: metadata callers assume the block is on the medium
 * when this returns, and the format has no journal to order deferred writes.
 */
int fabriczc_read_block(struct super_block *sb, u64 block, void *buf, u32 len)
{
    struct buffer_head *bh;

    if (unlikely(!sb || !sb->s_bdev || !buf || len > sb->s_blocksize))
        return -EINVAL;

    bh = sb_bread(sb, (sector_t)block);
    if (!bh) return -EIO;
    memcpy(buf, bh->b_data, len);
    brelse(bh);
    return 0;
}

int fabriczc_write_block(struct super_block *sb, u64 block, const void *buf, u32 len)
{
    struct buffer_head *bh;
    int ret;

    if (unlikely(!sb || !sb->s_bdev || !buf || len > sb->s_blocksize))
        return -EINVAL;

    bh = sb_getblk(sb, (sector_t)block);
    if (!bh) return -ENOMEM;
    memcpy(bh->b_data, buf, len);
    set_buffer_uptodate(bh);
    mark_buffer_dirty(bh);
    ret = sync_dirty_buffer(bh);
    brelse(bh);
    return ret;
}

/*
 * Start a block write and return immediately, holding one reference on the
 * buffer head. Reaping happens in fabriczc_write_block_wait(). A sequential
 * write of 4 KiB blocks through sync_dirty_buffer() serialises every request on
 * the device; this keeps a run of them in flight instead.
 */
struct buffer_head *fabriczc_write_block_submit(struct super_block *sb, u64 block,
                                               const void *buf, u32 len)
{
    struct buffer_head *bh;

    if (unlikely(!sb || !sb->s_bdev || !buf || len > sb->s_blocksize))
        return ERR_PTR(-EINVAL);

    bh = sb_getblk(sb, (sector_t)block);
    if (!bh) return ERR_PTR(-ENOMEM);
    memcpy(bh->b_data, buf, len);
    set_buffer_uptodate(bh);
    mark_buffer_dirty(bh);
    write_dirty_buffer(bh, REQ_SYNC);
    return bh;
}

int fabriczc_write_block_wait(struct buffer_head *bh)
{
    int ret = 0;

    if (unlikely(!bh)) return -EINVAL;
    if (IS_ERR(bh)) return (int)PTR_ERR(bh);
    wait_on_buffer(bh);
    if (unlikely(!buffer_uptodate(bh))) ret = -EIO;
    brelse(bh);
    return ret;
}

/* --- File data I/O ----------------------------------------------------
 *
 * Data blocks are read and written as whole contiguous runs in a single block
 * request. The previous one-buffer-head-per-4-KiB-block scheme issued one 4 KiB
 * request at a time and measured 264 MB/s on an array that does 4.4 GB/s with
 * 1 MiB requests. Data never enters the buffer cache on this path, so a data
 * block can only ever be in the page cache (reads) or on the medium.
 */

static unsigned int fabriczc_page_span(struct super_block *sb)
{
    /* Buffer helpers assume block size == page size (the format is fixed at
     * 4 KiB); anything else would need partial-page bounce handling. */
    return sb->s_blocksize == PAGE_SIZE ? 1 : 0;
}

static int fabriczc_bio_rw_buffer(struct super_block *sb, u64 block, void *buf,
                                  u32 nblocks, bool write)
{
    struct page **pages;
    struct bio *bio;
    u32 i;
    int ret = 0;

    if (unlikely(!sb || !sb->s_bdev || !buf || !nblocks || !fabriczc_page_span(sb)))
        return -EINVAL;

    if (nblocks == 1) {
        pages = NULL;
    } else {
        pages = kvmalloc_array(nblocks, sizeof(*pages), GFP_NOFS);
        if (!pages) return -ENOMEM;
        for (i = 0; i < nblocks; i++) {
            void *addr = (char *)buf + ((size_t)i << PAGE_SHIFT);

            pages[i] = is_vmalloc_addr(addr) ? vmalloc_to_page(addr)
                                            : virt_to_page(addr);
            if (!pages[i]) { ret = -EFAULT; goto out; }
        }
    }

    bio = bio_alloc(sb->s_bdev, (unsigned short)nblocks,
                    write ? (REQ_OP_WRITE | REQ_SYNC) : REQ_OP_READ, GFP_NOFS);
    if (!bio) { ret = -ENOMEM; goto out; }
    bio->bi_iter.bi_sector = (sector_t)block << (sb->s_blocksize_bits - 9);
    for (i = 0; i < nblocks; i++) {
        struct page *pg = pages ? pages[i] : virt_to_page((char *)buf + ((size_t)i << PAGE_SHIFT));

        if (bio_add_page(bio, pg, PAGE_SIZE, 0) != PAGE_SIZE) { ret = -EIO; break; }
    }
    if (!ret) ret = submit_bio_wait(bio);
    bio_put(bio);
out:
    kvfree(pages);
    return ret;
}

int fabriczc_read_blocks(struct super_block *sb, u64 block, void *buf, u32 nblocks)
{
    return fabriczc_bio_rw_buffer(sb, block, buf, nblocks, false);
}

int fabriczc_write_blocks(struct super_block *sb, u64 block, const void *buf,
                          u32 nblocks)
{
    return fabriczc_bio_rw_buffer(sb, block, (void *)buf, nblocks, true);
}

/* Fill n folios straight from n contiguous blocks: no bounce copy. */
int fabriczc_read_folios(struct super_block *sb, u64 block, struct folio **folios,
                         unsigned int n)
{
    struct bio *bio;
    unsigned int i;
    int ret = 0;

    if (unlikely(!sb || !sb->s_bdev || !folios || !n || n > FABRICZC_IO_MAX_BLOCKS))
        return -EINVAL;

    bio = bio_alloc(sb->s_bdev, (unsigned short)n, REQ_OP_READ, GFP_NOFS);
    if (!bio) return -ENOMEM;
    bio->bi_iter.bi_sector = (sector_t)block << (sb->s_blocksize_bits - 9);
    for (i = 0; i < n; i++) {
        if (bio_add_page(bio, folio_page(folios[i], 0), PAGE_SIZE, 0) != PAGE_SIZE) {
            ret = -EIO;
            break;
        }
    }
    if (!ret) ret = submit_bio_wait(bio);
    bio_put(bio);
    return ret;
}

struct fabriczc_folio_write {
    struct completion done;
    struct address_space *mapping;
};

/* Completes the writeback the caller started on every folio in the request. */
static void fabriczc_folio_write_end(struct bio *bio)
{
    struct fabriczc_folio_write *w = bio->bi_private;
    struct bvec_iter_all iter;
    struct bio_vec *bv;

    bio_for_each_segment_all(bv, bio, iter)
        folio_end_writeback(page_folio(bv->bv_page));
    if (bio->bi_status && w->mapping)
        mapping_set_error(w->mapping, blk_status_to_errno(bio->bi_status));
    complete(&w->done);
}

int fabriczc_write_folios(struct super_block *sb, u64 block, struct folio **folios,
                          unsigned int n, struct address_space *mapping)
{
    struct fabriczc_folio_write w;
    struct bio *bio;
    unsigned int i;
    int ret = 0;

    if (unlikely(!sb || !sb->s_bdev || !folios || !n || n > FABRICZC_IO_MAX_BLOCKS))
        return -EINVAL;

    bio = bio_alloc(sb->s_bdev, (unsigned short)n, REQ_OP_WRITE | REQ_SYNC, GFP_NOFS);
    if (!bio) return -ENOMEM;
    bio->bi_iter.bi_sector = (sector_t)block << (sb->s_blocksize_bits - 9);
    for (i = 0; i < n; i++) {
        if (bio_add_page(bio, folio_page(folios[i], 0), PAGE_SIZE, 0) != PAGE_SIZE) {
            ret = -EIO;
            break;
        }
    }
    if (ret) { bio_put(bio); return ret; }

    init_completion(&w.done);
    w.mapping = mapping;
    bio->bi_private = &w;
    bio->bi_end_io = fabriczc_folio_write_end;
    submit_bio(bio);
    wait_for_completion_io(&w.done);
    ret = blk_status_to_errno(bio->bi_status);
    bio_put(bio);
    return ret;
}

/* --- Pipelined sequential writes -------------------------------------- */

static void fabriczc_pipe_end(struct bio *bio)
{
    complete((struct completion *)bio->bi_private);
}

int fabriczc_io_pipe_init(struct fabriczc_io_pipe *pipe, struct super_block *sb)
{
    unsigned int i;

    if (unlikely(!pipe || !sb)) return -EINVAL;
    memset(pipe, 0, sizeof(*pipe));
    pipe->sb = sb;
    pipe->buf = vmalloc((size_t)FABRICZC_IO_DEPTH * FABRICZC_IO_MAX_BLOCKS * 4096);
    if (!pipe->buf) return -ENOMEM;
    for (i = 0; i < FABRICZC_IO_DEPTH; i++)
        init_completion(&pipe->done[i]);
    return 0;
}

/* Reap the slot if an earlier write is still using it, then hand it over. */
char *fabriczc_io_pipe_acquire(struct fabriczc_io_pipe *pipe)
{
    unsigned int s = pipe->slot;

    if (pipe->bio[s]) {
        wait_for_completion_io(&pipe->done[s]);
        if (pipe->bio[s]->bi_status && !pipe->error)
            pipe->error = blk_status_to_errno(pipe->bio[s]->bi_status);
        bio_put(pipe->bio[s]);
        pipe->bio[s] = NULL;
    }
    return pipe->buf + (size_t)s * FABRICZC_IO_MAX_BLOCKS * 4096;
}

int fabriczc_io_pipe_submit(struct fabriczc_io_pipe *pipe, u64 block,
                            unsigned int nblocks)
{
    unsigned int s = pipe->slot, i;
    char *buf = pipe->buf + (size_t)s * FABRICZC_IO_MAX_BLOCKS * 4096;
    struct super_block *sb = pipe->sb;
    struct bio *bio;

    if (unlikely(!nblocks || nblocks > FABRICZC_IO_MAX_BLOCKS)) return -EINVAL;

    bio = bio_alloc(sb->s_bdev, (unsigned short)nblocks, REQ_OP_WRITE | REQ_SYNC,
                    GFP_NOFS);
    if (!bio) return -ENOMEM;
    bio->bi_iter.bi_sector = (sector_t)block << (sb->s_blocksize_bits - 9);
    for (i = 0; i < nblocks; i++) {
        struct page *pg = vmalloc_to_page(buf + ((size_t)i << PAGE_SHIFT));

        if (!pg || bio_add_page(bio, pg, PAGE_SIZE, 0) != PAGE_SIZE) {
            bio_put(bio);
            return -EIO;
        }
    }
    init_completion(&pipe->done[s]);
    bio->bi_private = &pipe->done[s];
    bio->bi_end_io = fabriczc_pipe_end;
    pipe->bio[s] = bio;
    submit_bio(bio);
    pipe->slot = (s + 1) % FABRICZC_IO_DEPTH;
    return 0;
}

int fabriczc_io_pipe_drain(struct fabriczc_io_pipe *pipe)
{
    unsigned int i;

    if (unlikely(!pipe)) return -EINVAL;
    for (i = 0; i < FABRICZC_IO_DEPTH; i++) {
        if (!pipe->bio[i]) continue;
        wait_for_completion_io(&pipe->done[i]);
        if (pipe->bio[i]->bi_status && !pipe->error)
            pipe->error = blk_status_to_errno(pipe->bio[i]->bi_status);
        bio_put(pipe->bio[i]);
        pipe->bio[i] = NULL;
    }
    return pipe->error;
}

void fabriczc_io_pipe_exit(struct fabriczc_io_pipe *pipe)
{
    if (unlikely(!pipe)) return;
    fabriczc_io_pipe_drain(pipe);
    vfree(pipe->buf);
    pipe->buf = NULL;
}

struct buffer_head *fabriczc_load_block_bitmap(struct super_block *sb, unsigned int block_group_idx, struct fabriczc_block_group_desc *desc) { return NULL; }
int fabriczc_execute_block_allocation(struct super_block *sb, struct fabriczc_block_group_desc *desc, unsigned int block_group_idx, unsigned int start_block) { return -ENOSPC; }
int fabriczc_sync_block_groups(struct super_block *sb, struct fabriczc_block_group_desc *caches, u32 group_count) { return 0; }
int fabriczc_sync_block_bitmaps(struct super_block *sb, struct fabriczc_block_group_desc *caches, u32 group_count) { return 0; }
u32 fabriczc_lookup_extent_block(void *header_page, u32 logical_block) { return 0; }
int fabriczc_insert_extent_descriptor(void *header_page, u32 logical_block, u32 block_len, u32 physical_start) { return 0; }
int fabriczc_sync_inode_to_disk(struct super_block *sb, struct fabriczc_block_group_desc *caches, u32 ino, struct fabriczc_inode *raw_inode) { return fabriczc_inode_slot_write(sb, ino, raw_inode); }
int fabriczc_read_inode_from_disk(struct super_block *sb, struct fabriczc_block_group_desc *caches, u32 ino, struct fabriczc_inode *dest_inode) { return fabriczc_inode_slot_read(sb, ino, dest_inode); }
int fabriczc_allocate_file_extent(struct fabriczc_transaction_context *tx, void *header_page, u32 logical_block, u32 block_len, u32 physical_start) { return 0; }
void fabriczc_evict_inode(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;
    struct fabriczc_inode *raw = inode->i_private;

    truncate_inode_pages_final(&inode->i_data);

    if (raw) {
        if (inode->i_nlink == 0) {
            /*
             * Last name is gone. Release the block tree and then the slot.
             * Doing this here rather than in unlink() means an inode that is
             * still open keeps its blocks, and the slot number cannot be
             * handed to a new file while this VFS inode is cached.
             */
            struct fabriczc_sb_info *sbi = sb ? (struct fabriczc_sb_info *)sb->s_fs_info : NULL;

            fabriczc_truncate_blocks(inode, 0);
            if (raw->i_xattr_block) {
                fabriczc_free_data_block(sb, raw->i_xattr_block);
                raw->i_xattr_block = 0;
            }
            memset(raw, 0, sizeof(*raw));
            fabriczc_inode_slot_write(sb, inode->i_ino, raw);
            if (sbi) sbi->free_inodes++;
        } else {
            raw->i_links_count = inode->i_nlink;
            fabriczc_inode_slot_write(sb, inode->i_ino, raw);
        }
        kfree(raw);
        inode->i_private = NULL;
    }
    clear_inode(inode);
}
