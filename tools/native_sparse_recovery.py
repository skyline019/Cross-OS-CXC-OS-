#!/usr/bin/env python3
"""Native sparse I/O, ENOSPC and NVMe log recovery on disposable xv6 images.

Requires user/nativeio in fs.img and, except with --no-fault, a kernel built
with XV6_LOG_FAULT_INJECT. QEMU is killed at software log boundaries; this is
not a physical power-failure, torn-sector, or arbitrary-reordering test.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile

from xv6_log_recovery import BSIZE, DiskLog, Runner, TestFailure, check_recovery_trace

NDIRECT = 12
NINDIRECT = BSIZE // 4
MAXFILE = NDIRECT + NINDIRECT
LAST_BYTE = MAXFILE * BSIZE - 1
DIRECT_END = NDIRECT * BSIZE - 1
INDIRECT_AT = NDIRECT * BSIZE + 4 * BSIZE + 17
PATCH_AT = 6 * BSIZE + 123


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


class SparseDisk:
    """Independently inspect the actual xv6 inode, bitmap and mapped blocks."""

    def __init__(self, path):
        self.path = path
        self.log = DiskLog(path)
        with path.open("rb") as f:
            f.seek(BSIZE)
            (_, self.size, _, self.ninodes, _, _, self.inodestart,
             self.bmapstart) = struct.unpack("<8I", f.read(32))
        if not (1 <= self.ninodes and 2 <= self.inodestart < self.bmapstart < self.size):
            raise TestFailure("invalid inode or bitmap region")

    def block(self, number):
        if not 0 < number < self.size:
            raise TestFailure(f"invalid data block {number}")
        with self.path.open("rb") as f:
            f.seek(number * BSIZE)
            data = f.read(BSIZE)
        if len(data) != BSIZE:
            raise TestFailure("short disk block")
        return data

    def bitmap(self):
        count = (self.size + BSIZE * 8 - 1) // (BSIZE * 8)
        if self.bmapstart + count > self.size:
            raise TestFailure("bitmap outside disk")
        return b"".join(self.block(self.bmapstart + i) for i in range(count))

    def inode(self, number):
        if not 0 < number < self.ninodes:
            raise TestFailure(f"invalid inode {number}")
        with self.path.open("rb") as f:
            f.seek(self.inodestart * BSIZE + number * 64)
            raw = f.read(64)
        if len(raw) != 64:
            raise TestFailure("short inode")
        typ, major, minor, nlink, size, *pointers = struct.unpack("<hhhhI13I", raw)
        return typ, nlink, size, pointers

    def mappings(self, pointers):
        result = list(pointers[:NDIRECT])
        if pointers[NDIRECT]:
            result.extend(struct.unpack("<256I", self.block(pointers[NDIRECT])))
        else:
            result.extend([0] * NINDIRECT)
        return result

    def root_lookup(self, name):
        typ, nlink, size, pointers = self.inode(1)
        if typ != 1 or nlink < 1 or size % 16 or size > MAXFILE * BSIZE:
            raise TestFailure("invalid root inode")
        entries = bytearray()
        for block in self.mappings(pointers):
            if len(entries) >= size:
                break
            if not block:
                raise TestFailure("unexpected sparse root directory")
            entries.extend(self.block(block))
        matches = []
        for off in range(0, size, 16):
            inum, raw = struct.unpack_from("<H14s", entries, off)
            if inum and raw.rstrip(b"\0") == name:
                matches.append(inum)
        if len(matches) != 1:
            raise TestFailure(f"expected exactly one {name!r} entry")
        return matches[0]

    def inspect(self, patched):
        typ, nlink, size, pointers = self.inode(self.root_lookup(b"ni-sparse"))
        if typ != 2 or nlink != 1 or size != LAST_BYTE + 1 or not pointers[NDIRECT]:
            raise TestFailure("unexpected sparse file inode")
        mapping = self.mappings(pointers)
        expected_blocks = {0, DIRECT_END // BSIZE, INDIRECT_AT // BSIZE,
                           LAST_BYTE // BSIZE}
        if patched:
            expected_blocks.add(PATCH_AT // BSIZE)
        actual = {i for i, block in enumerate(mapping) if block}
        if actual != expected_blocks:
            raise TestFailure(f"allocated file blocks {actual} != {expected_blocks}")
        physical = [mapping[i] for i in sorted(actual)] + [pointers[NDIRECT]]
        bitmap = self.bitmap()
        if len(set(physical)) != len(physical):
            raise TestFailure("aliased file/indirect blocks")
        for block in physical:
            if not 0 < block < self.size or not (bitmap[block // 8] & (1 << (block % 8))):
                raise TestFailure(f"unallocated or invalid physical block {block}")
        expected = {0: b"HEAD", DIRECT_END: b"D", INDIRECT_AT: b"IND", LAST_BYTE: b"Z"}
        if patched:
            expected[PATCH_AT] = b"NEW"
        for logical in sorted(actual):
            wanted = bytearray(BSIZE)
            for off, value in expected.items():
                if off // BSIZE == logical:
                    pos = off % BSIZE
                    wanted[pos:pos + len(value)] = value
            if self.block(mapping[logical]) != bytes(wanted):
                raise TestFailure(f"wrong sparse data in logical block {logical}")
        return bitmap


def guest(runner, disk, label, mode):
    marker = (rb"^NATIVE_IO_(?:PASS mode=" + mode.encode() +
              rb"|FAIL code=\d+)\r?\n")
    output = runner.boot(disk, label, f"nativeio {mode}", marker)
    if b"NATIVE_IO_FAIL" in output:
        raise TestFailure(f"{label}: guest oracle failed")
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--cpus", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--backend", choices=("virtio", "nvme"), default="virtio")
    parser.add_argument("--no-fault", action="store_true", help="run without log fault injection")
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    image = root / "fs.img"
    if not 1 <= args.cpus <= 8 or args.timeout < 1 or not image.is_file():
        parser.error("invalid CPU/timeout or missing fs.img")
    before = digest(image)
    runner = Runner(root, args.qemu, args.cpus, args.timeout, args.log_dir, args.backend)
    with tempfile.TemporaryDirectory(prefix="native-sparse-") as tmp:
        directory = Path(tmp)
        for mode in ("semantics", "nospace"):
            disk = directory / f"{mode}.img"
            shutil.copyfile(image, disk)
            guest(runner, disk, f"native-{mode}", mode)
        baseline = directory / "baseline.img"
        shutil.copyfile(image, baseline)
        guest(runner, baseline, "sparse-seed", "seed")
        bitmap = SparseDisk(baseline).inspect(False)
        guest(runner, baseline, "sparse-old-read", "old")
        if SparseDisk(baseline).inspect(False) != bitmap:
            raise TestFailure("reading holes changed the allocation bitmap")
        guest(runner, baseline, "sparse-old-reboot", "old")
        if SparseDisk(baseline).inspect(False) != bitmap:
            raise TestFailure("read-only reboot changed the allocation bitmap")
        if not args.no_fault:
            for stage in range(1, 6):
                disk = directory / f"stage-{stage}.img"
                shutil.copyfile(baseline, disk)
                marker = (rb"^(?:XV6_LOG_FAULT stage=" + str(stage).encode() +
                          rb" blocks=(\d+)|NATIVE_IO_FAIL code=\d+)\r?\n")
                output = runner.boot(disk, f"sparse-stage-{stage}-crash",
                                     f"nativeio fault {stage}", marker, shell_after=False)
                match = re.search(marker, output, re.MULTILINE)
                if b"NATIVE_IO_FAIL" in output or not match or match.group(1) is None:
                    raise TestFailure(f"stage {stage}: fault not reached")
                count = int(match.group(1))
                log = DiskLog(disk)
                blocks = log.header()
                if stage in (1, 5):
                    if blocks or (stage == 1 and count < 3) or (stage == 5 and count != 0):
                        raise TestFailure(f"stage {stage}: unexpected log header")
                elif len(blocks) != count or count < 3:
                    raise TestFailure(f"stage {stage}: unexpected committed block count")
                if stage == 2:
                    negative = directory / "negative.img"
                    shutil.copyfile(disk, negative)
                    DiskLog(negative).discard_commit_header()
                    output = guest(runner, negative, "sparse-negative-drop-log", "old")
                    check_recovery_trace(output, [])
                    SparseDisk(negative).inspect(False)
                    print("PASS negative control: dropping commit loses sparse patch", flush=True)
                state = "old" if stage == 1 else "new"
                output = guest(runner, disk, f"sparse-stage-{stage}-recover", state)
                check_recovery_trace(output, blocks)
                if DiskLog(disk).header():
                    raise TestFailure("recovery left a committed log header")
                bitmap = SparseDisk(disk).inspect(state == "new")
                output = guest(runner, disk, f"sparse-stage-{stage}-reboot", state)
                check_recovery_trace(output, [])
                if SparseDisk(disk).inspect(state == "new") != bitmap:
                    raise TestFailure("read-only reboot allocated sparse blocks")
                print(f"PASS stage={stage} state={state} blocks={len(blocks)}", flush=True)
            # Both fsync(fd) and sync() must actually be waiting behind the
            # frozen commit. Kill before acknowledgement at every cut.
            for stage in range(1, 6):
                disk = directory / f"sync-crash-{stage}.img"
                shutil.copyfile(baseline, disk)
                marker = (rb"^(?:NATIVE_SYNC_WAIT stage=" + str(stage).encode() +
                          rb" waiters=2|NATIVE_IO_FAIL code=\d+)\r?\n")
                output = runner.boot(disk, f"sync-stage-{stage}-crash",
                                     f"nativeio sync-crash {stage}", marker,
                                     shell_after=False)
                if b"NATIVE_IO_FAIL" in output or not re.search(
                        rb"(?m)^XV6_LOG_FAULT stage=" + str(stage).encode() +
                        rb" blocks=\d+\r?\n", output):
                    raise TestFailure(f"sync stage {stage}: no blocked commit")
                blocks = DiskLog(disk).header()
                state = "old" if stage == 1 else "new"
                if (stage in (1, 5) and blocks) or (stage in (2, 3, 4) and len(blocks) < 3):
                    raise TestFailure(f"sync stage {stage}: unexpected log header")
                output = guest(runner, disk, f"sync-stage-{stage}-recover", state)
                check_recovery_trace(output, blocks)
                SparseDisk(disk).inspect(state == "new")
            # Release instead of killing: both waiters and the writer must
            # finish, then acknowledged contents must survive a fresh boot.
            for stage in (1, 2, 5):
                disk = directory / f"sync-release-{stage}.img"
                shutil.copyfile(baseline, disk)
                marker = rb"^NATIVE_IO_(?:PASS mode=sync-release|FAIL code=\d+)\r?\n"
                output = runner.boot(disk, f"sync-stage-{stage}-release",
                                     f"nativeio sync-release {stage}", marker)
                if (b"NATIVE_IO_FAIL" in output or not re.search(
                        rb"(?m)^NATIVE_SYNC_WAIT stage=" + str(stage).encode() +
                        rb" waiters=2\r?\n", output)):
                    raise TestFailure(f"sync stage {stage}: barrier/release failed")
                output = guest(runner, disk, f"sync-stage-{stage}-acked-reboot", "new")
                check_recovery_trace(output, [])
                SparseDisk(disk).inspect(True)
    if digest(image) != before:
        raise TestFailure("input fs.img changed")
    print(f"NATIVE_SPARSE_RECOVERY_PASS stages={0 if args.no_fault else 5} "
          f"sync_crash={0 if args.no_fault else 5} sync_release={0 if args.no_fault else 3} "
          f"negative_controls={0 if args.no_fault else 1} backend={args.backend} cpus={args.cpus}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, TestFailure, struct.error) as exc:
        print(f"NATIVE_SPARSE_RECOVERY_FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
