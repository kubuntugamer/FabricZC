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
Run the automated installer and insert the module, naming the backing block device the filesystem layer reads and writes:
```bash
./setup_and_build.sh
sudo insmod fabriczc_mod.ko target=/dev/loop0
```
The driver holds that backing device open, so mounting it directly fails with `EBUSY`. Mount the synthetic node instead:
```bash
sudo mount -t fabriczc /dev/fabriczc0 /mnt/fzc
```
Verify partitioning natively via `parted` and `lsblk` to ensure the multi-queue routing pipeline is operational.

`scripts/fzc_smoke_test.sh` runs the whole cycle unattended (loop image, `mkfs_fabriczc.py`, load, mount, checksum and remount checks, teardown):
```bash
sudo scripts/fzc_smoke_test.sh
```

`scripts/fzc_scale_test.sh` covers the large-volume side (capacity, sequential throughput, a file past the 4 GiB block range, sparse holes, delete reclamation). Both scripts keep their images on disk and refuse a `tmpfs` image directory, since a loop image in RAM can exhaust memory:
```bash
sudo scripts/fzc_scale_test.sh [image-size] [file-size]   # default 6G 1G
```
