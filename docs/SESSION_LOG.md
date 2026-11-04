# Session Log

Durable handoff record for FabricZC. Agent sessions do not keep chat history between
runs, so this file is the memory: append an entry whenever work is done, so the next
session (or person) knows what changed, what was verified, and what is still open.

**Rules**
- One entry per working session, newest last, `## YYYY-MM-DD HH:MM — short title`.
- Always record: git HEAD, kernel version, what changed, exact commands run and their
  result, and the next open item.
- Never mark something verified unless the command was actually run in that session.
  Unrun or failed checks go under "Not verified".

---

## 2026-11-04 14:46 — Format v2 built and committed

- HEAD: `781d3eb Format v2: 64-bit block pointers and four indirection levels`
- Kernel: `7.3.0-10-generic` (headers present, module `vermagic` matches)
- `fabriczc_mod.ko` rebuilt at 14:46, committed in the same commit.
- No verification run in this entry.

## 2026-11-04 15:03 — Reboot requested

- Rebooted so the freshly built v2 module could be inserted into a clean kernel
  (modules and any held backing device do not survive a reboot).

## 2026-11-04 15:05 — Post-reboot verification session

- HEAD: `781d3eb`; kernel: `7.3.0-10-generic`; no module loaded at session start.
- Loaded the v2 module against a disposable 64 MiB loop image and mounted the
  filesystem. **9/9 checks passed**, no `WARNING`/`BUG`/`Oops` in the kernel log:
  module load + `/dev/fabriczc0`, mount as type `fabriczc`, small file, 100 KiB copy,
  8 MiB copy (indirect + double-indirect), `mmap(MAP_SHARED)` writeback via
  `msync(MS_SYNC)`, `statfs` block size 4096, unmount/remount persistence.
- Finding: the driver holds the backing block device open, so
  `mount -t fabriczc <backing-dev>` fails with `EBUSY`. Mount `/dev/fabriczc0`
  instead. Fixed in `README.md`.
- Added `scripts/fzc_smoke_test.sh` (repeatable: image, mkfs, load, mount, checks,
  remount, teardown; root required).
- Reproduce: `sudo scripts/fzc_smoke_test.sh` → expect `9 checks passed`, rc 0.
- No `src/` changes. Working tree: `README.md` modified, `scripts/fzc_smoke_test.sh`
  new, build artifacts modified.

### Not verified (open items)

- Loading against the real array (`target=/dev/rcraid0` — not present on this host).
- Boot-time autoload / install path (`setup_and_build.sh`, live-USB installer).
- `execve()` of a dynamically linked binary copied onto the filesystem.
- Rebuilding from a clean tree (`make clean && make`) was not re-run this session.

## 2026-11-04 16:07 — Large-file scale test (2 TiB image, past 4 GiB)

- HEAD: `781d3eb`; kernel: `7.3.0-10-generic`. No `src/` changes.
- Context: the 8 MiB figure in the earlier entry came from the smoke test's 64 MiB
  image, not from a format limit. Re-tested at scale with 2/8/16 GiB loop images.
- Passed:
  - 2 TiB image: mkfs + mount OK; `statfs` reports 2.0 T, 536,870,911 blocks.
  - 300 GiB single sparse file: `size=322122547200`, `blocks=0` (64-bit `i_size_bytes`).
  - >4 GiB addressing: 5 GiB file, one 4 KiB block written at 4.5 GiB (block
    1,179,648 > 2^20), read back byte-identical, hole at 2 GiB reads zeros, block
    survives unmount/remount.
  - 1 GiB sequential write on a clean filesystem: byte-identical (md5 match).
- Failed / limiting:
  - Sequential write throughput collapses as the filesystem fills: 1 GiB took 103 s
    (~10 MB/s); a 5 GiB write reached 3.37 GiB in ~19 min (~2.2 MB/s average and
    falling), never completed. Cause: `fabriczc_alloc_data_block()` rescans the
    bitmap from group 0 on every 4 KiB allocation, so writes are O(n^2).
  - 1 GiB write after `rm` of a 300 GiB file came back corrupt (inode-slot reuse:
    unlink frees the slot while the VFS inode is still cached). Same bug already
    listed under open items.
- Format ceilings measured from the layout, not the tests: fixed 1024-block inode
  table = 16,383 files on any size volume; bitmap is 1 bit per 4 KiB (a 9 ZB array
  needs ~275 PB of bitmap); `bmap` stops at 512^4 blocks = 256 TiB per file;
  `i_size_bytes` is u64 = 8 EiB signed / 16 EiB unsigned, so a single 9 ZB block
  device is ~1000x beyond what Linux byte offsets (`loff_t`, 8 EiB) can address.
- Teardown: module unloaded, mounts gone, loop images removed.

### Open items added

- Fix O(n) block allocation (cursor/bitmap hint) before any large-file benchmark.
- Fix inode-slot reuse after unlink (the 1 GiB corruption reproducible above).

## 2026-11-04 16:15 — Findings filed in the GitHub issue tracker

- Upstream repo `kubuntugamer/FabricZC` (public, `gh` authenticated as the owner).
- New issues: #42 inode-slot reuse at unlink (stale size/blocks/type); #43 O(n^2)
  allocator rescan; #44 OS-root-filesystem readiness gaps (uid/gid, hardlinks,
  rmdir, xattrs, boot path); #45 scale audit vs the 9-billion-1TB-drive claim.
- Evidence comments added to #3 (reclamation still reproducible on v2), #8 (4 MiB
  cap lifted; 5 GiB checks), #18 (u64 addressing, remaining ceilings), #24 (16,383
  fixed inodes), #28 (per-block metadata writes), #35 (EISDIR root cause), #41 (v2
  scale verification + throughput collapse).
- All numbers in those bodies come from the runs recorded above; no claim was
  copied without a command behind it.

## 2026-11-04 16:24 — Fix: inode lifetime, block reclamation, allocator cursor

- HEAD `781d3eb` plus uncommitted changes to `src/fs/fs_main.c`,
  `src/fs/metadata_io.c` and `include/fabriczc_internal.h`.
- #42: `unlink`/`rmdir` now `drop_nlink()` and defer slot + block release to
  `fabriczc_evict_inode()`; the create paths refuse a recycled (`!I_NEW`) slot
  instead of building on the cached inode.
- #3: new `fabriczc_truncate_blocks()` frees direct blocks and whole index
  subtrees; called on `ATTR_SIZE` shrink and at eviction. `rmdir` now has its own
  handler and returns `ENOTEMPTY` for a non-empty directory.
- #43: `sbi->alloc_hint` cursor + `alloc_mutex`; the bitmap scan starts at the
  hint and wraps once, instead of restarting at block 0 on every allocation.
- Build clean (only the two pre-existing warnings). Smoke test: **9/9**.
- Regression on 128 MiB: reused inode now reports `size=1024 blocks=8`; `touch`
  after delete is 0 bytes; 16 MiB delete returns space exactly; truncating 8 MiB
  to 4 KiB frees 8,404,992 B and keeps the prefix; non-empty `rmdir` -> ENOTEMPTY;
  truncated file intact across remount; no kernel faults.
- 16 GiB image: 1 GiB in 13.0 s (~73 MiB/s), 5 GiB in 76 s (~67 MiB/s), `cmp`
  against the source matches, md5 unchanged across remount, `rm` returns
  5,379,219,456 B. Before the allocator fix the same 5 GiB write did not finish
  in 19 minutes.
- Not committed and not pushed.

## 2026-11-04 16:32 — Fix: ownership persistence and hardlinks (#44)

- Same uncommitted working tree; changes confined to `src/fs/fs_main.c`.
- `fabriczc_inode_from_raw()` maps `raw->i_uid` / `raw->i_gid` through
  `make_kuid()` / `make_kgid(&init_user_ns, ...)` instead of forcing root.
- `create`/`mkdir`/`symlink`/`mknod` call `inode_init_owner()` and persist the
  resulting uid/gid into the inode slot.
- `.link` implemented (directories refused); it adds the entry, increments
  `i_links_count` and holds the inode. Slot and block tree still go away only in
  `fabriczc_evict_inode()`, so the data lives until the last name is removed.
- Build clean (same two pre-existing warnings). Smoke test: **9/9**.
- Verified on 128 MiB: `uid=1000 gid=1000` survives remount for a file and a
  directory; uid 1000 can append to its own file; `ln` gives 2 links to one inode;
  after `rm` of one name the count is 1 and data is intact; a create at that point
  does not disturb it; content survives remount; the last unlink returns space.
- Still open in #44: xattrs, `fallocate`, boot path. Not committed.

## 2026-11-04 16:55 — Host crashed; smoke test moved off tmpfs `/tmp`

- HEAD `781d3eb` plus the same uncommitted tree as the two entries above; kernel
  `7.3.0-10-generic`. Only `scripts/fzc_smoke_test.sh` changed this session.
- Cause: on this host `/tmp` is **tmpfs** (`tmpfs 3.9G` on `/tmp`, RAM-backed), and
  the smoke test defaulted its loop image to `/tmp/fabriczc_smoke.img` with scratch
  copies in a `mktemp -d` under `/tmp`. A loop image on tmpfs is pinned RAM, so the
  multi-GiB scale runs (2 TiB sparse, 16 GiB, 5 GiB) were charging their footprint
  to memory; the system crashed. `/var/tmp` is on `/dev/sda1` (847 G free).
- Fix: default image is now `/var/tmp/fabriczc_smoke.img`; the scratch dir is
  created beside the image so random test data lands on the same disk; the script
  now `stat -f -c %T`s the image directory and dies with a clear message if it is
  `tmpfs`/`ramfs`. `stat` added to the required-tool list.
- Verified (commands actually run):
  - `bash -n scripts/fzc_smoke_test.sh` → syntax OK.
  - `FZC_SMOKE_IMG=/tmp/fzc_guard.img scripts/fzc_smoke_test.sh` (root) → refused
    with the tmpfs message, rc 1, and no file created in `/tmp`.
  - `sudo scripts/fzc_smoke_test.sh` → image at `/var/tmp`, **9/9 checks passed**,
    rc 0. Teardown clean: image removed, no leftover loop, module unloaded.
- Leftover from the earlier scale runs, on disk not RAM: `/var/tmp/fzc_v3.img`
  (512 MiB) and empty `/mnt/fzc-*` directories. Not deleted.
- Next open item: uncommitted fixes from the 16:24 and 16:32 entries are still
  uncommitted, and the large-image tests are still ad-hoc rather than scripted.

## 2026-11-04 17:05 — Scale test scripted, crash leftovers cleared, work committed

- HEAD: the commit carrying this entry (parent `781d3eb`); kernel `7.3.0-10-generic`.
- Added `scripts/fzc_scale_test.sh`: image and scratch on disk, same `tmpfs`/`ramfs`
  refusal as the smoke test, preflight free-space check, then capacity from
  `statfs`, a timed sequential write with `cmp`, a 5 GiB file with one block written
  at 4.5 GiB (block 1,179,648) plus a zero-hole read at 2 GiB, remount persistence,
  free-block accounting on delete, and a kernel-log fault scan. Sizes are arguments
  (`[image-size] [file-size]`, default `6G 1G`); the >4 GiB check is reported as
  `skip`, never as a pass, when the image is too small.
- Verified: `sudo scripts/fzc_scale_test.sh` → **6/6 checks passed**, rc 0, on a 6G
  image with a 1G file: statfs 6 GiB in 1,572,864 blocks; 1G written in 26,052 ms
  (~39 MiB/s) and byte-identical to the source (`cmp`) and across remount (md5);
  block at 4.5 GiB intact and 2 GiB hole zeros; delete freed 1,026 MiB and the free
  count did not go backwards; no `WARNING`/`BUG`/`Oops`. Teardown clean: image gone,
  no leftover loop, module unloaded, memory flat at ~4.1 Gi used.
- Observation: 39 MiB/s here versus 73 MiB/s in the 16:18 run on a 16 GiB image.
  Same code, different image size and host file layout; not investigated.
- Cleanup: removed `/var/tmp/fzc_v3.img` (512 MiB sparse) and every empty
  `/mnt/fzc-*` / `/mnt/fzc_*` test mountpoint. Kept `/mnt/array` (pre-existing) and
  `/var/tmp/fzc_5g_fix.log` (log of the 16:18 run).
- Committed the pending 16:24/16:32 fixes together with this entry, `README.md` and
  both test scripts: source files only, build artifacts left uncommitted exactly as
  the parent commit does.
- Not verified this session: boot-time autoload, the real `/dev/rcraid0` array, a
  clean-tree `make`, and xattrs/`fallocate` from #44 — all still open.

## 2026-11-04 17:40 — user xattrs and fallocate (#44)

- HEAD: the commit carrying this entry (parent `45ccfcf`); kernel `7.3.0-10-generic`.
- Throughput question from the 17:05 entry is answered: rerunning the same scale
  test gave **112 MiB/s**, and a raw 1 GiB write to the host disk gives 101–111
  MB/s, so the filesystem is at the disk ceiling. A zero-fill probe at 4k/64k/1M/4M
  blocks gave 121/149/141/134 MiB/s, which rules out the per-4 KiB
  `kernel_write()` loop as the bottleneck. The 39 MiB/s reading was environmental
  (host page cache state right after the crash), not a regression.
- Extended attributes: `struct fabriczc_inode` gains `i_xattr_block` carved out of
  the reserved tail (88 → 80 bytes, offset 168). Old images read 0 there, which
  means "no attributes", so **no format version bump and no mkfs change**.
  `src/fs/fs_main.c` adds a `user.*` handler pair plus `listxattr`, `src/fs/metadata_io.c`
  frees the side block in `evict_inode()` when the last link goes away.
  Block layout: `{ u32 magic "X4RZ"; u16 used; u16 count }` followed by
  `{ u16 name_len; u16 value_len; name; value }` records, 4-byte aligned, whole
  namespace bounded by one 4 KiB block. The VFS hands handlers names with the
  `user.` prefix stripped, so records hold bare names and `listxattr` re-adds the
  prefix — without that `listxattr` returned `origin` and `getfattr -n user.origin`
  could not find it.
- `fallocate`: plain, `FALLOC_FL_KEEP_SIZE` and `FALLOC_FL_ZERO_RANGE` are
  supported; every block is written as zeros because `fabriczc_alloc_data_block()`
  does not scrub a reused block. `FALLOC_FL_PUNCH_HOLE` returns `-EOPNOTSUPP`
  rather than being emulated: releasing the middle of the pointer tree is not the
  tail release `truncate` does.
- Verified: `sudo scripts/fzc_smoke_test.sh` → **11/11 checks passed**, rc 0 (the
  two new checks are the xattr round-trip, namespace rejection and remount
  persistence, and fallocate reserve/zero/`-n` keep-size). A separate probe on a
  4 GiB image confirmed `setxattr`/`getxattr`/`listxattr`/`removexattr`, `XATTR_CREATE`
  → `EEXIST`, `XATTR_REPLACE` on a missing name → `ENODATA`, `security.*` →
  `EOPNOTSUPP`, 5 files × (data block + xattr block) returned the free-block count
  exactly to baseline after unlink, and a 1 MiB `0xFF` range zeroed by `fallocate -z`
  read back as zeros with the size unchanged.
- Not verified: punch-hole, `trusted.*`/`security.*` namespaces, xattrs on symlinks,
  xattr values larger than one block (`-E2BIG`), boot-time autoload, the real
  `/dev/rcraid0` array, clean-tree `make`.

## 2026-11-04 18:00 — Backing device default corrected to the hptnvme node

- `src/core/main.c` defaulted `target` to `/dev/rcraid0`. That name is an AMD
  rcraid leftover from the early stub; the array on this machine is a HighPoint
  NVMe volume presented by `hptnvme` as `/dev/hptblock6n0p` (14.55 TiB, no
  partition table yet), and rcraid is not loaded.
- Default is now `/dev/hptblock6n0p`, the module parameter help text names it,
  `scripts/live_dashboard.sh` and the spec header point at the same node.
- Verified: `make` rc 0 and `strings fabriczc_mod.ko | grep -i 'hptblock\|rcraid'`
  returns only the new default. `insmod fabriczc_mod.ko` with no parameters
  bound `[hptblock6n0p]` in dmesg, `/dev/fabriczc0` reported
  16002779709440 bytes (14.55 TiB), and `rmmod` unloaded cleanly. No mount and no
  writes to the array were performed.
- Still in the tree but not built by the Makefile: `src/kernel/driver/main.c`, an
  old standalone staging driver that registers a disk named `rcraid` and prints
  a "40.8 GB storage array". It is where the rcraid naming came from; it was
  left untouched rather than deleted.
