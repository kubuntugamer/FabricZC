#!/usr/bin/env bash
# ==============================================================================
# FabricZC module smoke test
#
# Loads fabriczc_mod.ko against a disposable loop-backed image, mounts it through
# the synthetic /dev/fabriczc0 node, and exercises the filesystem: small file,
# subdirectory, 100 KiB copy, 8 MiB copy (indirect/double-indirect blocks),
# remount persistence, and a kernel-log scan for faults. Everything is torn down
# on exit, including on failure.
#
# Usage: sudo scripts/fzc_smoke_test.sh [image-size]
#        (image-size defaults to 64M; FZC_SMOKE_IMG / FZC_SMOKE_MNT override paths)
#
# The image and the scratch copies live on the same directory as the image. That
# directory must be disk-backed: on hosts where /tmp is tmpfs, every byte a loop
# image holds is RAM the kernel cannot reclaim, so a large run there can exhaust
# memory and crash the box. /var/tmp is the default for that reason.
# Exit status: 0 on success, 1 on the first failed check or setup error.
# ==============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KO="$REPO_ROOT/fabriczc_mod.ko"
MKFS="$REPO_ROOT/mkfs_fabriczc.py"
MODULE="fabriczc_mod"

IMG="${FZC_SMOKE_IMG:-/var/tmp/fabriczc_smoke.img}"
MNT="${FZC_SMOKE_MNT:-/mnt/fabriczc-smoke}"
SIZE="${1:-64M}"
IMG_DIR="$(dirname "$IMG")"

checks=0
pass() { checks=$((checks + 1)); echo "ok   - $*"; }
die()  { echo "FAIL - $*" >&2; exit 1; }
step() { echo "==> $*"; }

[ "$(id -u)" -eq 0 ] || die "must run as root (sudo $0)"
[ -f "$KO" ] || die "$KO not found; build the module first (make)"
[ -f "$MKFS" ] || die "$MKFS not found"
for tool in losetup truncate python3 dmesg findmnt mountpoint cmp stat fallocate; do
    command -v "$tool" >/dev/null || die "missing required tool: $tool"
done
mkdir -p "$IMG_DIR" || die "could not create $IMG_DIR"
img_fstype="$(stat -f -c %T "$IMG_DIR")"
case "$img_fstype" in
    tmpfs|ramfs)
        die "$IMG_DIR is $img_fstype (RAM-backed); a loop image there consumes RAM.
     Use a disk-backed FZC_SMOKE_IMG, e.g. FZC_SMOKE_IMG=/var/tmp/fabriczc_smoke.img";;
esac
if lsmod | awk '{print $1}' | grep -qx "$MODULE"; then
    die "$MODULE is already loaded; run 'sudo rmmod $MODULE' first"
fi
[ -d "$MNT" ] || mkdir -p "$MNT"

LOOP=""
LOADED=0
MOUNTED=0
SRC_DIR="$(mktemp -d "$IMG_DIR/fabriczc-smoke.XXXXXX")" || die "could not create scratch dir in $IMG_DIR"
cleanup() {
    set +e
    [ "$MOUNTED" -eq 1 ] && umount "$MNT"
    [ "$LOADED" -eq 1 ] && rmmod "$MODULE"
    [ -n "$LOOP" ] && losetup -d "$LOOP"
    rm -f "$IMG"
    rm -rf "$SRC_DIR"
    rmdir "$MNT" 2>/dev/null
}
trap cleanup EXIT

DMESG_MARK="$(dmesg | wc -l)"

step "creating $SIZE image at $IMG"
truncate -s "$SIZE" "$IMG" || die "could not create $IMG"
LOOP="$(losetup -f --show "$IMG")" || die "could not attach a loop device"
echo "    loop device: $LOOP"

step "writing FabricZC layout"
python3 "$MKFS" "$LOOP" >/dev/null || die "mkfs_fabriczc.py failed on $LOOP"

step "loading $MODULE (no device parameter: the filesystem takes what mount gives it)"
insmod "$KO" || die "insmod failed"
LOADED=1
[ -b "/dev/fabriczc0" ] && die "a synthetic device was created even though none was requested"
pass "module loaded without binding any block device"

step "mounting $LOOP on $MNT"
mount -t fabriczc "$LOOP" "$MNT" || die "mount of $LOOP failed"
MOUNTED=1
mountpoint -q "$MNT" || die "$MNT is not a mountpoint after mount"
[ "$(findmnt -n -o FSTYPE --target "$MNT")" = "fabriczc" ] || die "unexpected filesystem type on $MNT"
pass "mounted as fabriczc"

printf 'hello fabriczc\n' > "$MNT/hello.txt"
[ "$(cat "$MNT/hello.txt")" = "hello fabriczc" ] || die "small file read-back mismatch"
pass "small file write/read"

mkdir "$MNT/sub"
head -c 100000 /dev/urandom > "$SRC_DIR/small.bin"
cp "$SRC_DIR/small.bin" "$MNT/sub/blob.bin" || die "100 KiB copy failed"
cmp -s "$SRC_DIR/small.bin" "$MNT/sub/blob.bin" || die "100 KiB content mismatch"
pass "100 KiB file copy matches (md5 $(md5sum "$SRC_DIR/small.bin" | cut -d' ' -f1))"

head -c 8388608 /dev/urandom > "$SRC_DIR/large.bin"
cp "$SRC_DIR/large.bin" "$MNT/large.bin" || die "8 MiB copy failed"
cmp -s "$SRC_DIR/large.bin" "$MNT/large.bin" || die "8 MiB content mismatch"
pass "8 MiB file copy matches (indirect + double-indirect blocks)"

step "testing extended attributes (user.* namespace)"
if ! python3 - "$MNT/hello.txt" <<'PY'
import errno, os, sys

path = sys.argv[1]
os.setxattr(path, "user.origin", b"smoke")
if os.getxattr(path, "user.origin") != b"smoke":
    sys.exit("getxattr returned the wrong value")
if "user.origin" not in os.listxattr(path):
    sys.exit("listxattr does not report user.origin")
try:
    os.setxattr(path, "security.probe", b"x")
    sys.exit("an attribute in an unwired namespace was accepted")
except OSError as e:
    if e.errno != errno.EOPNOTSUPP:
        sys.exit("security.* failed with %s, expected EOPNOTSUPP" % errno.errorcode.get(e.errno))
PY
then
    die "user xattr set/get/list or namespace rejection failed"
fi
pass "user.* xattr stored, listed and readable; other namespaces refused"

step "testing fallocate"
fallocate -l 8M "$MNT/prealloc.bin" || die "fallocate reserve failed"
[ "$(stat -c %s "$MNT/prealloc.bin")" = "8388608" ] || die "fallocate did not extend the file to 8 MiB"
python3 - "$MNT/prealloc.bin" <<'PY'
import sys

with open(sys.argv[1], "rb") as f:
    if any(f.read()):
        sys.exit("preallocated range does not read back as zeros")
PY
printf 'abc' > "$MNT/keep.bin"
fallocate -n -l 8M "$MNT/keep.bin" || die "fallocate -n failed"
[ "$(stat -c %s "$MNT/keep.bin")" = "3" ] || die "fallocate -n changed the file size"
pass "fallocate reserves zeroed blocks and -n keeps the size"

step "testing mmap(MAP_SHARED) writeback"
if ! python3 - "$MNT/mmap.bin" <<'PY'
import mmap, os, sys

path = sys.argv[1]
size = 65536
data = bytes((i * 7 + 3) & 0xff for i in range(size))

with open(path, "wb") as f:
    f.write(data)

fd = os.open(path, os.O_RDWR)
try:
    m = mmap.mmap(fd, size)
    if m[0] != data[0]:
        sys.exit("first byte mismatch after mmap")
    m[100:110] = b"MMAPEDIT!!"
    m.flush()  # msync(MS_SYNC) -> ->fsync -> ->writepages
    m.close()
finally:
    os.close(fd)

expected = bytearray(data)
expected[100:110] = b"MMAPEDIT!!"
with open(path, "rb") as f:
    got = f.read()
if got != bytes(expected):
    sys.exit("mmap writeback content mismatch")
PY
then
    die "mmap MAP_SHARED writeback failed"
fi
mmap_md5="$(md5sum "$MNT/mmap.bin" | cut -d' ' -f1)"
pass "mmap MAP_SHARED store reaches disk (md5 $mmap_md5)"

block_size="$(stat -f -c %S "$MNT")"
[ "$block_size" = "4096" ] || die "statfs reports block size $block_size, expected 4096"
pass "statfs reports 4096-byte blocks"

step "unmounting and remounting to check persistence"
umount "$MNT" || die "unmount failed"
MOUNTED=0
mount -t fabriczc "$LOOP" "$MNT" || die "remount failed"
MOUNTED=1
[ "$(cat "$MNT/hello.txt")" = "hello fabriczc" ] || die "hello.txt did not survive remount"
cmp -s "$SRC_DIR/small.bin" "$MNT/sub/blob.bin" || die "sub/blob.bin did not survive remount"
cmp -s "$SRC_DIR/large.bin" "$MNT/large.bin" || die "large.bin did not survive remount"
[ "$(md5sum "$MNT/mmap.bin" | cut -d' ' -f1)" = "$mmap_md5" ] || die "mmap.bin did not survive remount"
python3 - "$MNT/hello.txt" <<'PY' || die "user.origin did not survive remount"
import os, sys

if os.getxattr(sys.argv[1], "user.origin") != b"smoke":
    sys.exit(1)
PY
pass "data (including the mmap-written page) and the xattr survived unmount/remount"

step "checking kernel log for faults"
faults="$(dmesg | tail -n +$((DMESG_MARK + 1)) | grep -iE 'WARNING:|BUG:|Oops|Call Trace' || true)"
[ -z "$faults" ] || die "kernel log faults during run:
$faults"
pass "no WARNING/BUG/Oops in the kernel log"

echo
echo "$checks checks passed"
df -h "$MNT"
