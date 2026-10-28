#!/usr/bin/env python3
"""PMFS v3 multi-boot oracle and software-boundary fault tests.

Requires a kernel built with PMEM_MAP and PMFS_FAULT_INJECT, plus an fs.img
containing pmfs_regress. The original fs.img and any existing RAM image are
never modified: all disk/backing files live in a temporary directory. By
default this uses shared guest RAM. --nvdimm requires the supplied experimental
QEMU patch and a kernel with PMEM_NVDIMM. Both are process-kill tests, not
physical power-loss tests.
"""

import argparse
import hashlib
import os
import re
from pathlib import Path
import selectors
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time

RAM_SIZE = 160 * 1024 * 1024
PMEM_START = 128 * 1024 * 1024
FS_START = PMEM_START + 4096
PAGE = 4096


def sparse_copy(src, dst):
    with open(src, "rb") as source, open(dst, "wb") as target:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            if any(block):
                target.write(block)
            else:
                target.seek(len(block), os.SEEK_CUR)
        target.truncate(RAM_SIZE)


def pmem_bytes(path):
    with open(path, "rb") as f:
        f.seek(PMEM_START)
        return f.read(RAM_SIZE - PMEM_START)


def pmem_hash(path, length):
    return hashlib.sha256(pmem_bytes(path)[:length]).hexdigest()


def newest_sb(ram):
    slots = []
    with open(ram, "rb") as f:
        for slot in range(2):
            f.seek(FS_START + slot * PAGE)
            words = struct.unpack("<12I", f.read(48))
            if words[0] == 0x32534650 and words[1] == 3 and words[2] == 0xA55A5AA5:
                slots.append(((words[10] << 32) | words[9], slot, words))
    if not slots:
        raise RuntimeError("baseline has no v3 checkpoint")
    return max(slots)


def rewrite_checkpoint(path, slot, field, value):
    # Mirror the v3 header checksum so the bounds/format checks cannot rely
    # solely on detecting a bad checksum.
    with open(path, "r+b") as f:
        offset = FS_START + slot * PAGE
        f.seek(offset)
        raw = bytearray(f.read(48))
        struct.pack_into("<I", raw, field * 4, value)
        struct.pack_into("<I", raw, 12, 0)
        h, mask = 0x9E3779B97F4A7C15, (1 << 64) - 1
        for i in range(0, 48, 8):
            h ^= int.from_bytes(raw[i:i + 8], "little")
            h = ((h << 7) | (h >> 57)) & mask
            h = (h * 0x100000001B3) & mask
        struct.pack_into("<I", raw, 12, (h ^ (h >> 32)) & 0xFFFFFFFF)
        f.seek(offset)
        f.write(raw)
        f.flush()
        os.fsync(f.fileno())


def flip_byte(path, offset):
    with open(path, "r+b") as f:
        f.seek(offset)
        value = f.read(1)
        if len(value) != 1:
            raise RuntimeError("bad corruption offset")
        f.seek(offset)
        f.write(bytes([value[0] ^ 0x80]))
        f.flush()
        os.fsync(f.fileno())


class Runner:
    def __init__(self, root, qemu, directory, timeout, cpus, zicbom, log_dir, nvdimm):
        self.root = root
        self.qemu = qemu
        self.directory = directory
        self.timeout = timeout
        self.cpus = cpus
        self.zicbom = zicbom
        self.nvdimm = nvdimm
        self.log_dir = log_dir
        self.count = 0

    def boot(self, ram, label, command=None, marker=None, expect_shell=True,
             ram_mb=160, expect_map=True):
        self.count += 1
        disk = self.directory / f"disk-{self.count}.img"
        shutil.copyfile(self.root / "fs.img", disk)
        if self.nvdimm and expect_map:
            cmd = [self.qemu, "-machine", "virt,nvdimm=on,acpi=off", "-bios", "none",
                   "-kernel", str(self.root / "kernel/kernel"),
                   "-m", "128M,maxmem=160M,slots=1", "-smp", str(self.cpus),
                   "-nographic", "-object",
                   f"memory-backend-file,id=nvd,size=32M,mem-path={ram},share=on",
                   "-device", "nvdimm,id=nv0,memdev=nvd,addr=0x88000000"]
        else:
            cmd = [self.qemu, "-machine", "virt", "-bios", "none", "-kernel",
                   str(self.root / "kernel/kernel"), "-m", f"{ram_mb}M", "-smp", str(self.cpus),
                   "-nographic", "-object",
                   f"memory-backend-file,id=mem,size={ram_mb}M,mem-path={ram},share=on",
                   "-numa", "node,memdev=mem"]
        cmd += ["-global", "virtio-mmio.force-legacy=false",
                "-drive", f"file={disk},if=none,format=raw,id=x0",
                "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"]
        if self.zicbom:
            cmd += ["-cpu", "rv64,zicbom=true"]
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True)
        output = bytearray()
        selector = selectors.DefaultSelector()
        selector.register(proc.stdout, selectors.EVENT_READ)

        def wait_for(predicate, description):
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                if predicate():
                    return
                if proc.poll() is not None:
                    raise RuntimeError(f"{label}: QEMU exited {proc.returncode} waiting for {description}")
                for key, _ in selector.select(min(0.2, max(0, deadline - time.monotonic()))):
                    chunk = os.read(key.fd, 8192)
                    if chunk:
                        output.extend(chunk)
                if b"panic:" in output:
                    raise RuntimeError(f"{label}: kernel panic")
            raise RuntimeError(f"{label}: timeout waiting for {description}")

        try:
            wait_for(lambda: b"init: starting sh" in output and b"$ " in output, "shell")
            expected_backend = b"nvdimm" if self.nvdimm else b"map"
            if expect_map and (b"ONBOX_PMFS_PMEM0_PASS mode=MAP backend=" + expected_backend) not in output:
                raise RuntimeError(f"{label}: expected PMFS backend {expected_backend.decode()}")
            if not expect_map and (b"MAP unavailable; refusing volatile fallback" not in output or
                                   b"ONBOX_PMFS_PMEM0_PASS mode=" in output):
                raise RuntimeError(f"{label}: insufficient RAM did not fail closed")
            if self.zicbom and b"Zicbom CBO.CLEAN enabled" not in output:
                raise RuntimeError(f"{label}: kernel lacks PMEM_ZICBOM")
            if command:
                offset = len(output)
                proc.stdin.write((command + "\n").encode())
                proc.stdin.flush()
                needle = marker.encode()
                wait_for(lambda: needle in output[offset:] or b"PMFS_REGRESS_FAIL" in output[offset:], marker)
                if b"PMFS_REGRESS_FAIL" in output[offset:]:
                    raise RuntimeError(f"{label}: guest oracle reported failure")
                if expect_shell:
                    start = output.index(needle, offset) + len(needle)
                    wait_for(lambda: b"$ " in output[start:], "completed shell prompt")
        except Exception:
            sys.stderr.write(output[-6000:].decode("utf-8", "replace") + "\n")
            raise
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
            proc.wait(timeout=5)
            selector.close()
            proc.stdin.close()
            proc.stdout.close()
            if self.log_dir:
                (self.log_dir / f"{self.count:02d}-{label}.log").write_bytes(output)
            disk.unlink(missing_ok=True)
        print(f"PASS {label}", flush=True)
        return output.decode("utf-8", "replace")

    def regress(self, ram, mode, label=None):
        return self.boot(ram, label or mode, f"pmfs_regress {mode}",
                         f"PMFS_REGRESS_PASS mode={mode}")


def main():
    global RAM_SIZE, PMEM_START, FS_START
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--cpus", type=int, default=3)
    parser.add_argument("--zicbom", action="store_true")
    parser.add_argument("--nvdimm", action="store_true",
                        help="use the patched QEMU virt NVDIMM and PMEM_NVDIMM guest")
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    if args.nvdimm:
        RAM_SIZE = 32 * 1024 * 1024
        PMEM_START = 0
        FS_START = PAGE
    if args.timeout < 1 or args.cpus < 1 or args.cpus > 8:
        parser.error("timeout must be positive; cpus must be 1..8")
    if not (root / "kernel/kernel").is_file() or not (root / "fs.img").is_file():
        parser.error("build kernel/kernel and fs.img with PMEM_MAP and PMFS_FAULT_INJECT first")
    if args.log_dir:
        args.log_dir.mkdir(parents=True, exist_ok=True)
        args.log_dir = args.log_dir.resolve()
    with tempfile.TemporaryDirectory(prefix="pmfs-recovery-") as tmp:
        d = Path(tmp)
        run = Runner(root, args.qemu, d, args.timeout, args.cpus, args.zicbom, args.log_dir,
                     args.nvdimm)
        base = d / "base.ram"
        with base.open("wb") as f:
            f.truncate(RAM_SIZE)

        # A MAP build with too little guest RAM must not probe unmapped RAM or
        # silently switch to the unrelated volatile EMUL filesystem.
        small = d / "insufficient.ram"
        with small.open("wb") as f:
            f.truncate(128 * 1024 * 1024)
        run.boot(small, "insufficient-ram", "pmformat --force",
                 "pmformat: failed (mounted or device unavailable)",
                 ram_mb=128, expect_map=False)
        small.unlink()

        # Refuse blank media, and require an explicit destructive confirmation.
        before = pmem_bytes(base)
        out = run.boot(base, "blank-fail-closed", "pmformat", "usage: pmformat --force")
        backend = "nvdimm" if args.nvdimm else "map"
        size = re.search(rf"ONBOX_PMFS_PMEM0_PASS mode=MAP backend={backend} size=(\d+)", out)
        if not size:
            raise RuntimeError("missing MAP device size")
        # The DTB may be written by QEMU above the reported usable window.
        length = PAGE + int(size.group(1))
        if (length > len(before) or "no valid v3 checkpoint" not in out or
                pmem_hash(base, length) != hashlib.sha256(before[:length]).hexdigest()):
            raise RuntimeError("blank PMFS window was modified or did not fail closed")
        run.regress(base, "init")
        run.regress(base, "old", "baseline-reboot")

        def copy_base(name):
            dest = d / (name + ".ram")
            sparse_copy(base, dest)
            return dest

        ram = copy_base("update")
        run.regress(ram, "update")
        run.regress(ram, "new", "updated-reboot-1")
        run.regress(ram, "new", "updated-reboot-2")
        ram.unlink()

        ram = copy_base("dirty")
        run.boot(ram, "dirty-interruption", "pmfs_regress dirty", "PMFS_REGRESS_DIRTY_READY", False)
        run.regress(ram, "old", "dirty-recovery")
        ram.unlink()

        ram = copy_base("edges")
        run.regress(ram, "edges")
        run.regress(ram, "parallel")
        run.regress(ram, "parallel-read", "parallel-reboot")
        run.regress(ram, "old", "edges-recovery")
        ram.unlink()

        ram = copy_base("index")
        run.regress(ram, "index-fill")
        run.regress(ram, "index-read", "index-reboot")
        run.regress(ram, "old", "index-old-data")
        ram.unlink()

        ram = copy_base("standard-fd")
        run.regress(ram, "fd-write")
        run.regress(ram, "fd-check", "fd-reboot")
        run.boot(ram, "fd-shell-write", "echo shell-written > /pm/t/sh", "echo shell-written > /pm/t/sh")
        run.boot(ram, "fd-shell-read-reboot", "cat /pm/t/sh", "shell-written")
        run.boot(ram, "fd-global-sync", "pmfs_regress fd-global", "PMFS_REGRESS_GLOBAL_READY", False)
        run.regress(ram, "fd-global-check", "fd-global-sync-reboot")
        run.regress(ram, "fd-remove")
        run.regress(ram, "fd-absent", "fd-delete-reboot")
        run.regress(ram, "old", "fd-old-data")
        ram.unlink()

        ram = copy_base("capacity")
        run.regress(ram, "capacity")
        run.regress(ram, "old", "capacity-reboot")
        ram.unlink()

        # First checkpoint publishes a new run; second publishes inode roots.
        # QEMU exposes stores to the shared file immediately, so stage 4 can
        # observe the new commit word even before the instrumentation fence.
        for skip in (0, 1):
            for stage in range(1, 6):
                ram = copy_base(f"fault-{skip}-{stage}")
                run.boot(ram, f"fault-{skip}-{stage}", f"pmfs_regress fault {stage} {skip}",
                         f"PMFS_FAULT_POINT stage={stage}", False)
                expected = "anew" if skip == 1 and stage >= 4 else "old"
                run.regress(ram, expected, f"recover-{skip}-{stage}-{expected}")
                ram.unlink()

        # Tear or interrupt after the new run, inode and dentry page writes.
        # None is reachable before its following checkpoint is published.
        for stage in (6, 7):
            for skip in range(3):
                ram = copy_base(f"page-{stage}-{skip}")
                run.boot(ram, f"page-{stage}-{skip}", f"pmfs_regress fault {stage} {skip}",
                         f"PMFS_FAULT_POINT stage={stage}", False)
                run.regress(ram, "old", f"page-recover-{stage}-{skip}")
                ram.unlink()

        ram = copy_base("single-checkpoint")
        _, slot, _ = newest_sb(ram)
        flip_byte(ram, FS_START + slot * PAGE)
        before = pmem_hash(ram, length)
        run.regress(ram, "old", "single-checkpoint-fallback")
        if pmem_hash(ram, length) != before:
            raise RuntimeError("single-slot fallback unexpectedly wrote the PMFS window")
        ram.unlink()

        for corrupt in ("checkpoints", "v2", "tail", "alias", "inode", "run"):
            ram = copy_base(f"corrupt-{corrupt}")
            _, _, words = newest_sb(ram)
            if corrupt == "checkpoints":
                flip_byte(ram, FS_START)
                flip_byte(ram, FS_START + PAGE)
            elif corrupt in ("v2", "tail", "alias"):
                for slot in range(2):
                    if corrupt == "v2":
                        rewrite_checkpoint(ram, slot, 1, 2)
                    elif corrupt == "tail":
                        rewrite_checkpoint(ram, slot, 4, 0xFFFFFFFF)
                    else:
                        with open(ram, "rb") as f:
                            f.seek(FS_START + slot * PAGE + 24)
                            dent_page = struct.unpack("<I", f.read(4))[0]
                        rewrite_checkpoint(ram, slot, 5, dent_page)
            elif corrupt == "inode":
                flip_byte(ram, FS_START + words[5] * PAGE + 80)
            else:
                flip_byte(ram, FS_START + words[7] * PAGE + 100)
            before = pmem_hash(ram, length)
            out = run.boot(ram, f"corrupt-{corrupt}")
            if ("no valid v3 checkpoint" not in out and "mount refused" not in out) or pmem_hash(ram, length) != before:
                raise RuntimeError(f"{corrupt}: bad image was not refused without writes")
            ram.unlink()
        print(f"PMFS_RECOVERY_PASS boots={run.count} cpus={args.cpus} "
              f"zicbom={int(args.zicbom)} nvdimm={int(args.nvdimm)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print(f"PMFS_RECOVERY_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
