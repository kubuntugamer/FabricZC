#!/usr/bin/env python3
"""FabricZC v3 layout generator.

v3 layout (4 KiB blocks):
    0                      root directory block
    2                      superblock
    3                      journal header
    [4, 4+bmp)             allocation bitmap, 1 bit per block
    [itis, itis+table)     inode table, 256-byte slots, index == inode number
    [data_start, total)    file data

The inode table is sized from the volume (one inode per 16 KiB), so a real
distribution tree fits.
"""

import argparse
import array
import fcntl
import os
import struct
import sys

FABRICZC_SB_MAGIC = 0x465A4333  # "FZC3"
FABRICZC_JOURNAL_MAGIC = 0x4A4E4C4D  # "JNLM"
FABRICZC_BLOCK_FREE = 0xFFFFFFFFFFFFFFFF
FABRICZC_JOURNAL_BLOCK = 3
FABRICZC_BITMAP_START = 4
FABRICZC_BITS_PER_BLOCK = 4096 * 8

FABRICZC_INODE_TABLE_MIN_BLOCKS = 1024
FABRICZC_DATA_BLOCKS_PER_TABLE = 64
FABRICZC_MAX_INODE_SLOTS = 0x7FFFFFFF

INODE_SIZE = 256
SLOTS_PER_BLOCK = 4096 // INODE_SIZE
NAME_LEN = 255


def inode_table_blocks(total_blocks):
    blocks = max(total_blocks // FABRICZC_DATA_BLOCKS_PER_TABLE,
                 FABRICZC_INODE_TABLE_MIN_BLOCKS)
    if blocks * SLOTS_PER_BLOCK > FABRICZC_MAX_INODE_SLOTS:
        blocks = FABRICZC_MAX_INODE_SLOTS // SLOTS_PER_BLOCK
    return blocks


def bitmap_block_count(total_blocks):
    return (total_blocks + FABRICZC_BITS_PER_BLOCK - 1) // FABRICZC_BITS_PER_BLOCK


def inode_table_start(total_blocks):
    return FABRICZC_BITMAP_START + bitmap_block_count(total_blocks)


def data_start(total_blocks):
    return inode_table_start(total_blocks) + inode_table_blocks(total_blocks)


def pack_superblock(total_blocks, free_blocks, free_inodes):
    buf = bytearray(72)
    struct.pack_into('<I', buf, 0, FABRICZC_SB_MAGIC)
    struct.pack_into('<I', buf, 4, 1)                    # sb_state = clean
    struct.pack_into('<Q', buf, 8, total_blocks)         # block_count
    struct.pack_into('<Q', buf, 16, free_blocks)         # free_blocks
    struct.pack_into('<I', buf, 24, 4096)                # block_size_bytes
    struct.pack_into('<16s', buf, 28,
                     b'\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff\x00')
    struct.pack_into('<q', buf, 48, 0)                   # last_mount_time
    chk = 0
    for i in range(0, 56, 4):
        chk ^= struct.unpack_from('<I', buf, i)[0]
    struct.pack_into('<I', buf, 56, chk)
    struct.pack_into('<Q', buf, 64, free_inodes)
    return buf


def pack_journal():
    buf = bytearray(4096)
    struct.pack_into('<I', buf, 0, FABRICZC_JOURNAL_MAGIC)
    struct.pack_into('<I', buf, 4, 4096)
    struct.pack_into('<Q', buf, 8, 0)
    struct.pack_into('<I', buf, 16, 0)
    struct.pack_into('<I', buf, 20, 0)
    struct.pack_into('<I', buf, 24, 1)
    return buf


def pack_inode_root(root_dir_block):
    buf = bytearray(INODE_SIZE)
    struct.pack_into('<H', buf, 0, 0x41ED)               # S_IFDIR | 0755
    struct.pack_into('<H', buf, 2, 2)                    # i_links_count
    struct.pack_into('<I', buf, 4, 0)                    # i_uid
    struct.pack_into('<I', buf, 8, 0)                    # i_gid
    struct.pack_into('<Q', buf, 16, 4096)                # i_size_bytes
    struct.pack_into('<Q', buf, 24, 8)                   # i_blocks_allocated
    struct.pack_into('<I', buf, 32, 0)                   # i_generation
    struct.pack_into('<I', buf, 36, 0)                   # i_checksum
    for i in range(12):
        struct.pack_into('<Q', buf, 40 + i * 8, FABRICZC_BLOCK_FREE)
    struct.pack_into('<Q', buf, 40, root_dir_block)      # direct[0] -> root dir
    for off in (136, 144, 152, 160):
        struct.pack_into('<Q', buf, off, FABRICZC_BLOCK_FREE)
    return buf


def main():
    p = argparse.ArgumentParser(description="mkfs.fabriczc v3 layout generator")
    p.add_argument('device', help='Block device or image file')
    p.add_argument('--size', type=int, default=0, help='Size in bytes (image files)')
    args = p.parse_args()

    path = args.device
    try:
        st = os.stat(path)
    except OSError as e:
        print("error: cannot stat", path, e)
        sys.exit(1)

    with open(path, 'r+b') as f:
        if st.st_size > 0:
            total_bytes = st.st_size
        else:
            sz = array.array('L', [0])
            try:
                fcntl.ioctl(f.fileno(), 0x80081272, sz, True)   # BLKGETSIZE64
                total_bytes = sz[0]
            except OSError:
                total_bytes = 0
            if total_bytes == 0:
                print('Unable to determine device size.')
                sys.exit(1)

        total_blocks = total_bytes // 4096
        if total_blocks < 1024:
            print('Device too small for a FabricZC filesystem (need >= 4 MiB).')
            sys.exit(1)

        bmp_blocks = bitmap_block_count(total_blocks)
        table_start = inode_table_start(total_blocks)
        table_blocks = inode_table_blocks(total_blocks)
        first_data = data_start(total_blocks)
        slots = table_blocks * SLOTS_PER_BLOCK

        print(f'Target: {path}')
        print(f'Total bytes: {total_bytes}')
        print(f'Total 4K blocks: {total_blocks}')
        print(f'Bitmap region: {FABRICZC_BITMAP_START} .. '
              f'{FABRICZC_BITMAP_START + bmp_blocks - 1} ({bmp_blocks} blocks)')
        print(f'Inode table:   {table_start} .. {table_start + table_blocks - 1} '
              f'({table_blocks} blocks, {slots} slots)')
        print(f'Data region:   {first_data} .. {total_blocks - 1} '
              f'({total_blocks - first_data} blocks)')

        zero = bytes(4096)

        # Zero every metadata block (also clears block 0, the root directory).
        for b in range(0, min(first_data, total_blocks)):
            f.seek(b * 4096)
            f.write(zero)

        # The root directory gets its own data block, allocated out of the data
        # region, so physical block 0 stays untouched and is never used as a
        # file block (0 is the "unset pointer" sentinel in the block tree).
        root_dir_block = first_data
        free_blocks = max(total_blocks - first_data - 1, 0)
        free_inodes = max(slots - 2, 0)  # slot 0 reserved, slot 1 is root
        f.seek(2 * 4096)
        f.write(pack_superblock(total_blocks, free_blocks, free_inodes))

        f.seek(FABRICZC_JOURNAL_BLOCK * 4096)
        f.write(pack_journal())

        f.seek(table_start * 4096 + INODE_SIZE * 1)
        f.write(pack_inode_root(root_dir_block))

        # Empty root directory block.
        f.seek(root_dir_block * 4096)
        f.write(zero)

        # Mark every metadata block and the root directory block as allocated.
        bitmap = bytearray(bmp_blocks * 4096)
        for b in range(0, min(first_data + 1, total_blocks)):
            bitmap[b // 8] |= 1 << (b % 8)
        f.seek(FABRICZC_BITMAP_START * 4096)
        f.write(bitmap)

        f.flush()
        os.fsync(f.fileno())

    print('mkfs.fabriczc: done.')


if __name__ == '__main__':
    main()
