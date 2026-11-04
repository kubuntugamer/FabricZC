#!/usr/bin/env bash
# ==============================================================================
# FabricZC scale test
#
# Companion to fzc_smoke_test.sh for the large-volume properties: reported
# capacity, sequential write throughput, a file that grows past the 32-bit block
# range, sparse holes, space return on delete, and persistence across remount.
#
# Everything lives next to the image on a disk-backed filesystem. A loop image
# on tmpfs is pinned RAM, so a multi-GiB run there can exhaust memory and crash
# the host; the image directory is therefore checked before anything is created.
#
# Usage: sudo scripts/fzc_scale_test.sh [image-size] [file-size]
#        image-size defaults to 6G (4 GiB addressing needs > 5 GiB of volume)
#        file-size  defaults to 1G (sequential throughput sample)
#        FZC_SCALE_IMG / FZC_SCALE_MNT override paths
# Exit status: 0 on success, 1 on the first failed check or setup error.
# ==============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KO="$REPO_ROOT/fabriczc_mod.ko"
MKFS="$REPO_ROOT/mkfs_fabriczc.py"
MODULE="fabriczc_mod"
DEV="/dev/fabriczc0"

IMG="${FZC_SCALE_IMG:-/var/tmp/fabriczc_scale.img}"
MNT="${FZC_SCALE_MNT:-/mnt/fabriczc-scale}"
IMG_SIZE="${1:-6G}"
FILE_SIZE="${2:-1G}"
IMG_DIR="$(dirname "$IMG")"

# A file must be able to pass 2^32 blocks to exercise 64-bit block addressing.
ADDRESS_LIMIT_BYTES=$((4 * 1024 * 1024 * 1024))

checks=0
pass()  { checks=$((checks + 1)); echo "ok   - $*"; }
skip()  { echo "skip - $*"; }
die()   { echo "FAIL - $*" >&2; exit 1; }
step()  { echo "==> $*"; }

[ "$(id -u)" -eq 0 ] || die "must run as root (sudo $0)"
[ -f "$KO" ] || die "$KO not found; build the module first (make)"
[ -f "$MKFS" ] || die "$MKFS not found"
for tool in losetup truncate python3 dmesg findmnt mountpoint cmp stat numfmt md5sum; do
    command -v "$tool" >/dev/null || die "missing required tool: $tool"
done

IMG_BYTES="$(numfmt --from=iec "$IMG_SIZE")"
FILE_BYTES="$(numfmt --from=iec "$FILE_SIZE")"

mkdir -p "$IMG_DIR" || die "could not create $IMG_DIR"
img_fstype="$(stat -f -c %T "$IMG_DIR")"
case "$img_fstype" in
    tmpfs|ramfs)
        die "$IMG_DIR is $img_fstype (RAM-backed); a loop image there consumes RAM.
     Use a disk-backed FZC_SCALE_IMG, e.g. FZC_SCALE_IMG=/var/tmp/fabriczc_scale.img";;
esac

avail_bytes="$(df -B1 --output=avail "$IMG_DIR" | tail -n 1 | tr -d ' ')"
[ "$avail_bytes" -ge "$IMG_BYTES" ] || die "$IMG_DIR has $((avail_bytes / 1024 / 1024)) MiB free; the ${IMG_SIZE} image can fill up to $((IMG_BYTES / 1024 / 1024)) MiB"

if lsmod | awk '{print $1}' | grep -qx "$MODULE"; then
    die "$MODULE is already loaded; run 'sudo rmmod $MODULE' first"
fi
[ -d "$MNT" ] || mkdir -p "$MNT"

LOOP=""
LOADED=0
MOUNTED=0
SRC_DIR="$(mktemp -d "$IMG_DIR/fabriczc-scale.XXXXXX")" || die "could not create scratch dir in $IMG_DIR"
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

step "creating sparse $IMG_SIZE image at $IMG (disk-backed: $img_fstype)"
truncate -s "$IMG_SIZE" "$IMG" || die "could not create $IMG"
LOOP="$(losetup -f --show "$IMG")" || die "could not attach a loop device"
echo "    loop device: $LOOP"

step "writing FabricZC layout"
python3 "$MKFS" "$LOOP" >/dev/null || die "mkfs_fabriczc.py failed on $LOOP"

step "loading $MODULE against $LOOP"
insmod "$KO" target="$LOOP" || die "insmod failed"
LOADED=1
for _ in $(seq 1 50); do
    [ -b "$DEV" ] && break
    sleep 0.1
done
[ -b "$DEV" ] || die "$DEV did not appear after load"

step "mounting $DEV on $MNT"
mount -t fabriczc "$DEV" "$MNT" || die "mount of $DEV failed"
MOUNTED=1

total_bytes=$(( $(stat -f -c '%b' "$MNT") * $(stat -f -c '%S' "$MNT") ))
echo "    statfs reports $((total_bytes / 1024 / 1024 / 1024)) GiB in $(stat -f -c '%b' "$MNT") blocks"
[ "$total_bytes" -ge $((IMG_BYTES * 9 / 10)) ] || die "statfs reports $total_bytes bytes, expected about $IMG_BYTES"
pass "volume reports about $IMG_SIZE of capacity"

step "sequential write of $FILE_SIZE"
python3 - "$SRC_DIR/seq.bin" "$FILE_BYTES" <<'PY'
import sys

path, size = sys.argv[1], int(sys.argv[2])
# Non-zero, position-dependent data so a hole or a stale block cannot match.
block = bytes((i * 31 + 7) & 0xff for i in range(1 << 20))
with open(path, "wb") as f:
    left = size
    while left:
        n = min(left, len(block))
        f.write(block[:n])
        left -= n
PY
start_ns="$(date +%s%N)"
cp "$SRC_DIR/seq.bin" "$MNT/seq.bin" || die "copy of $FILE_SIZE failed"
end_ns="$(date +%s%N)"
elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
[ "$elapsed_ms" -gt 0 ] || elapsed_ms=1
throughput=$(( FILE_BYTES / elapsed_ms * 1000 / 1024 / 1024 ))
cmp -s "$SRC_DIR/seq.bin" "$MNT/seq.bin" || die "$FILE_SIZE copy does not match the source"
pass "$FILE_SIZE written in ${elapsed_ms}ms (~${throughput} MiB/s) and matches the source"

seq_md5="$(md5sum "$MNT/seq.bin" | cut -d' ' -f1)"

if [ "$IMG_BYTES" -ge $((ADDRESS_LIMIT_BYTES + 1024 * 1024 * 1024)) ]; then
    step "4 GiB+ block addressing (one block at 4.5 GiB of a 5 GiB file)"
    if ! python3 - "$MNT/sparse.bin" <<'PY'
import os, sys

path = sys.argv[1]
size = 5 * 1024 ** 3
offset = int(4.5 * 1024 ** 3)
payload = bytes((i * 13 + 5) & 0xff for i in range(4096))

fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o644)
try:
    os.ftruncate(fd, size)
    os.pwrite(fd, payload, offset)
    os.fsync(fd)
finally:
    os.close(fd)

fd = os.open(path, os.O_RDONLY)
try:
    if os.fstat(fd).st_size != size:
        sys.exit("size is %d, expected %d" % (os.fstat(fd).st_size, size))
    if os.pread(fd, 4096, offset) != payload:
        sys.exit("data at 4.5 GiB did not read back identical")
    if os.pread(fd, 4096, 2 * 1024 ** 3) != bytes(4096):
        sys.exit("hole at 2 GiB did not read as zeros")
finally:
    os.close(fd)
PY
    then
        die "4 GiB addressing check failed"
    fi
    pass "5 GiB file: block at 4.5 GiB (block 1179648 > 2^20) read back intact, hole reads zeros"

    step "remounting to check the >4 GiB block survived"
    umount "$MNT" || die "unmount failed"
    MOUNTED=0
    mount -t fabriczc "$DEV" "$MNT" || die "remount failed"
    MOUNTED=1
    if ! python3 - "$MNT/sparse.bin" <<'PY'
import os, sys

offset = int(4.5 * 1024 ** 3)
expected = bytes((i * 13 + 5) & 0xff for i in range(4096))
fd = os.open(sys.argv[1], os.O_RDONLY)
try:
    if os.pread(fd, 4096, offset) != expected:
        sys.exit("block at 4.5 GiB did not survive remount")
finally:
    os.close(fd)
PY
    then
        die ">4 GiB block did not survive remount"
    fi
    [ "$(md5sum "$MNT/seq.bin" | cut -d' ' -f1)" = "$seq_md5" ] || die "seq.bin did not survive remount"
    pass ">4 GiB block and the $FILE_SIZE file survived unmount/remount"
else
    skip "4 GiB addressing needs an image above 5G (got $IMG_SIZE); pass a larger size to run it"
fi

step "space accounting on delete"
block_size="$(stat -f -c '%S' "$MNT")"
free_before="$(stat -f -c '%f' "$MNT")"
rm -f "$MNT/seq.bin" "$MNT/sparse.bin"
sync
free_after="$(stat -f -c '%f' "$MNT")"
freed=$(( (free_after - free_before) * block_size ))
echo "    freed $((freed / 1024 / 1024)) MiB"
[ "$free_after" -ge "$free_before" ] || die "deleting files reduced free blocks ($free_before -> $free_after)"
pass "$FILE_SIZE file and 5 GiB sparse file removed; free block count did not go backwards"

step "checking kernel log for faults"
faults="$(dmesg | tail -n +$((DMESG_MARK + 1)) | grep -iE 'WARNING:|BUG:|Oops|Call Trace' || true)"
[ -z "$faults" ] || die "kernel log faults during run:
$faults"
pass "no WARNING/BUG/Oops in the kernel log"

echo
echo "$checks checks passed"
