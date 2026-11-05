# 🧵 FabricZC AI Execution & Bug-Auditing Blueprint Manual

## 📊 Subsystem Progress & Current Version State
* **Current Workspace Version:** 0.1.5
* **Staged Branch Context:** experimental
* **Phase 1-4:** COMPLETE
* **Phase 5 (Block MQ Pass-Through Pipeline):** ACTIVE / PROPROMPT READY
* **Latest verified state:** [docs/SESSION_LOG.md](docs/SESSION_LOG.md) — dated build/load/mount results and the open items.

---

## 🔩 Low-Level Core Container Specification
All member block devices contain a 48-byte packed structure (`struct fabriczc_container_header`) written at the 16 KiB front-offset anchor zone, tracking sequence IDs, Fletcher64 checksums, container state magic, UUIDs, disk slot boundaries, and extent chunk sectors.

---

## 🤖 AI INGESTION SPECIFICATIONS FOR SOURCE CODE BUG AUDITING
Automated LLMs or AI generation engines ingesting this workspace must evaluate source files (such as `src/kernel/main.c` against `staging_includes/fabriczc_staging.h`) for block multi-queue safety, cache coherency, and thread isolation across concurrent hardware queues.

---

## 🧪 MANUAL SIMULATION RUNTIME CHECKS (VBOX LIVE USB LAB)
Build the module, format a device or image with the FabricZC layout, and mount it directly — the filesystem uses the block device `mount(2)` is given, so no hardware is named anywhere:
```bash
./setup_and_build.sh
sudo insmod fabriczc_mod.ko
sudo python3 mkfs_fabriczc.py /dev/loop0
sudo mount -t fabriczc /dev/loop0 /mnt/fzc
```
`target=<device>` still exists for the legacy Layer-1 pass-through path, but the filesystem neither needs it nor uses it.

`scripts/fzc_smoke_test.sh` runs the whole cycle unattended (loop image, `mkfs_fabriczc.py`, load, mount, checksum and remount checks, teardown):
```bash
sudo scripts/fzc_smoke_test.sh
```

`scripts/fzc_scale_test.sh` covers the large-volume side (capacity, sequential throughput, a file past the 4 GiB block range, sparse holes, delete reclamation). Both scripts keep their images on disk and refuse a `tmpfs` image directory, since a loop image in RAM can exhaust memory:
```bash
sudo scripts/fzc_scale_test.sh [image-size] [file-size]   # default 6G 1G
```

## 🥾 Booting a FabricZC root

GRUB cannot read FabricZC, so keep the kernel and initrd where it can read them
(the FAT32 ESP, or a small ext4 `/boot`), and put `fabriczc_mod.ko` inside the
initramfs so root can be mounted before `switch_root`:

```bash
sudo cp boot/initramfs/hook-fabriczc /etc/initramfs-tools/hooks/
sudo make modules_install
sudo update-initramfs -u
```

`root=/dev/<node> rootfstype=fabriczc` then works with no module parameter — the
filesystem mounts whatever device it is given. Under Secure Boot the module must
be signed and its key enrolled (MOK), or Secure Boot must be disabled; Ubuntu
kernels refuse unsigned modules and the boot dies at `VFS: Unable to mount root fs`.
