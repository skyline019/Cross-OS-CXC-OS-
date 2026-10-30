#!/usr/bin/env python3
"""Restart oracle for explicit COW compaction and two-checkpoint GC.

Uses disposable shared guest-RAM or patched QEMU NVDIMM files. Faults stop at
software boundaries, not physical power failure. Requires a MAP/fault build.
"""

import argparse
import os
from pathlib import Path
import re
import struct
import sys
import tempfile

import pmfs_recovery as recovery

RUN_MAGIC = 0x33554E52534D5055
NIL = 0xFFFFFFFF


def inspect_runs(ram, head, tail):
    """Independently validate v1/v2 run chains and return pages and latest KV.

    v2 permits a higher-address predecessor after safe page reuse. Sequence
    order, bounds, checksums and an explicit page set prevent cycles/overlap.
    """
    pages, seen, records = set(), set(), {}
    newer_seq = 1 << 64
    with ram.open("rb") as f:
        while head != NIL:
            if head in seen or not 4 <= head < tail or len(seen) >= 256:
                raise RuntimeError("invalid run chain in inspection")
            seen.add(head)
            f.seek(recovery.FS_START + head * recovery.PAGE)
            first = struct.unpack("<QQIIIIIIII", f.read(48))
            magic, seq, version, page0, index, count, previous = first[:7]
            if (magic != RUN_MAGIC or version not in (1, 2) or page0 != head or
                    index != 0 or not 1 <= count <= 8 or head + count > tail or
                    not 0 < seq < newer_seq or
                    (previous != NIL and
                     (not 4 <= previous < tail or previous == head or
                      (version == 1 and previous >= head)))):
                raise RuntimeError("invalid run header in inspection")
            last_key, nrecords = None, 0
            for index in range(count):
                pid = head + index
                if pid in pages:
                    raise RuntimeError("overlapping run pages")
                pages.add(pid)
                f.seek(recovery.FS_START + pid * recovery.PAGE)
                data = bytearray(f.read(recovery.PAGE))
                if len(data) != recovery.PAGE:
                    raise RuntimeError("short run page")
                hdr = struct.unpack_from("<QQIIIIIIII", data)
                saved = hdr[8]
                struct.pack_into("<I", data, 40, 0)
                checksum = 2166136261
                for byte in data:
                    checksum = ((checksum ^ byte) * 16777619) & 0xFFFFFFFF
                struct.pack_into("<I", data, 40, saved)
                if (hdr[:7] != (magic, seq, version, head, index, count, previous) or
                        saved != checksum or not 1 <= hdr[7] <= 32 or
                        nrecords + hdr[7] > 32):
                    raise RuntimeError("inconsistent or corrupt run page")
                offset = 48
                for _ in range(hdr[7]):
                    if offset + 16 > len(data):
                        raise RuntimeError("short record header")
                    key, length, _ = struct.unpack_from("<QII", data, offset)
                    if (length > 512 or offset + 16 + length > len(data) or
                            (last_key is not None and key <= last_key)):
                        raise RuntimeError("bad record length or key order")
                    records.setdefault(key, bytes(data[offset + 16:offset + 16 + length]))
                    last_key = key
                    offset += 16 + length
                    nrecords += 1
            newer_seq = seq
            head = previous
    return pages, records


def run_pages(ram, head, tail):
    return inspect_runs(ram, head, tail)[0]


def latest_records(ram, head, tail):
    return inspect_runs(ram, head, tail)[1]


def page_bytes(ram, pid):
    with ram.open("rb") as f:
        f.seek(recovery.FS_START + pid * recovery.PAGE)
        return f.read(recovery.PAGE)


def page_prefix(ram, pid):
    return page_bytes(ram, pid)[:16]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-riscv64"))
    parser.add_argument("--cpus", type=int, default=int(os.environ.get("CPUS", "3")))
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--log-dir", type=Path)
    parser.add_argument("--nvdimm", action="store_true")
    parser.add_argument("--zicbom", action="store_true")
    parser.add_argument("--backend", choices=("virtio", "nvme"), default="virtio",
                        help="root disk backend, independent of the PMEM device")
    args = parser.parse_args()
    root = args.root.resolve()
    if args.cpus < 1 or args.cpus > 8 or args.timeout < 1:
        parser.error("cpus must be 1..8 and timeout must be positive")
    if not (root / "kernel/kernel").is_file() or not (root / "fs.img").is_file():
        parser.error("build a test-only MAP kernel and fs.img first")
    if args.nvdimm:
        recovery.RAM_SIZE = 32 * 1024 * 1024
        recovery.PMEM_START = 0
        recovery.FS_START = recovery.PAGE
    if args.log_dir:
        args.log_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="pmfs-gc-recovery-") as tmp:
        directory = Path(tmp)
        base = directory / "base.ram"
        with base.open("wb") as f:
            f.truncate(recovery.RAM_SIZE)
        run = recovery.Runner(root, args.qemu, directory, args.timeout, args.cpus,
                              args.zicbom, args.log_dir, args.nvdimm, args.backend)

        def guest(ram, mode, label=None):
            return run.boot(ram, label or mode, f"pmfs_gc {mode}",
                            f"PMFS_GC_PASS mode={mode}",
                            failure_marker=b"PMFS_GC_FAIL")

        def clone(name):
            ram = directory / f"{name}.ram"
            recovery.sparse_copy(base, ram)
            return ram

        def view(ram, expected_runs, label):
            text = guest(ram, "check", label)
            match = re.search(r"PMFS_GC_VIEW runs=(\d+)", text)
            if not match or int(match.group(1)) != expected_runs:
                raise RuntimeError(f"{label}: unexpected recovered run count")

        run.regress(base, "init", "format-and-baseline")

        # Repeatedly overwrite one key with explicit compaction and slot
        # retirement; both latest and previous views must survive a reboot even
        # when newer runs occupy lower physical pages than older runs.
        soaked = clone("reused-run-soak")
        guest(soaked, "soak", "run-reuse-soak")
        _, soak_slot, soak_sb = recovery.newest_sb(soaked)
        run_pages(soaked, soak_sb[7], soak_sb[4])
        head, saw_v2 = soak_sb[7], False
        while head != NIL:
            with soaked.open("rb") as f:
                f.seek(recovery.FS_START + head * recovery.PAGE)
                hdr = struct.unpack("<QQIIIIIIII", f.read(48))
            saw_v2 |= hdr[2] == 2
            head = hdr[6]
        if not saw_v2:
            raise RuntimeError("soak did not exercise a non-monotonic v2 run chain")
        guest(soaked, "soak-check", "run-reuse-reboot")
        run.regress(soaked, "old", "run-reuse-baseline")
        recovery.flip_byte(soaked, recovery.FS_START + soak_slot * recovery.PAGE + 8)
        guest(soaked, "soak-previous", "run-reuse-old-slot-fallback")
        run.regress(soaked, "old", "run-reuse-old-slot-baseline")
        soaked.unlink()

        guest(base, "prepare", "prepare-compaction")
        output = guest(base, "check", "old-view-reboot")
        match = re.search(r"PMFS_GC_VIEW runs=(\d+)", output)
        if not match:
            raise RuntimeError("old run count missing")
        old_runs = int(match.group(1))
        _, _, old_sb = recovery.newest_sb(base)
        old_head, old_tail = old_sb[7], old_sb[4]
        old_pages = run_pages(base, old_head, old_tail)

        normal = clone("normal")
        output = guest(normal, "compact", "compact-normal")
        match = re.search(r"PMFS_GC_METRIC runs_before=(\d+) runs_after=(\d+).*output_pages=(\d+)", output)
        if not match or int(match.group(1)) != old_runs:
            raise RuntimeError("missing normal compaction metrics")
        new_runs, output_pages = int(match.group(2)), int(match.group(3))
        if not 0 < new_runs < old_runs or output_pages < 17:
            raise RuntimeError("workload does not cover enough compacted pages")
        _, new_slot, new_sb = recovery.newest_sb(normal)
        new_head = new_sb[7]
        new_pages = run_pages(normal, new_head, new_sb[4])
        if new_head == old_head or old_pages & new_pages:
            raise RuntimeError("compaction did not create a disjoint COW run chain")
        old_records = latest_records(base, old_head, old_tail)
        new_records = latest_records(normal, new_head, new_sb[4])
        internal = lambda key: key & 0xFF00000000000000 == 0xC000000000000000
        if ({k: v for k, v in old_records.items() if not internal(k)} !=
                {k: v for k, v in new_records.items() if not internal(k)}):
            raise RuntimeError("compaction changed the latest public KV map")
        old_internal = {k for k in old_records if internal(k)}
        new_internal = {k for k in new_records if internal(k)}
        if (not old_internal or not new_internal or
                max(old_internal) != max(new_internal) or
                not new_internal < old_internal):
            raise RuntimeError("internal chunk pruning or ID high-water marker is wrong")
        print(f"PASS compare-kv-map public={len(new_records) - len(new_internal)} "
              f"internal_before={len(old_internal)} internal_after={len(new_internal)} "
              f"high_water={max(new_internal):016x}", flush=True)
        view(normal, new_runs, "normal-reboot-1")
        view(normal, new_runs, "normal-reboot-2")
        run.regress(normal, "old", "compacted-baseline-data")

        # A new chunk after a reboot must advance beyond the retained marker,
        # including when the formerly highest chunk belonged to a deleted file.
        later = directory / "new-chunk-after-reboot.ram"
        recovery.sparse_copy(normal, later)
        guest(later, "newchunk", "new-chunk-after-reboot")
        _, _, later_sb = recovery.newest_sb(later)
        later_records = latest_records(later, later_sb[7], later_sb[4])
        if max(k for k in later_records if internal(k)) <= max(old_internal):
            raise RuntimeError("restarted chunk allocator reused an old identifier")
        guest(later, "newchunk-check", "new-chunk-second-reboot")
        later.unlink()

        # Before the older slot is retired, force active-slot corruption. Its
        # predecessor must recover all KV/file data from the original chain.
        fallback = directory / "fallback.ram"
        recovery.sparse_copy(normal, fallback)
        recovery.flip_byte(fallback, recovery.FS_START + new_slot * recovery.PAGE + 8)
        view(fallback, old_runs, "fallback-old-chain-1")
        view(fallback, old_runs, "fallback-old-chain-2")
        run.regress(fallback, "old", "fallback-baseline-data")
        fallback.unlink()

        # Advance the metadata allocator with injected, post-page-write
        # failures while both snapshots remain committed. The inactive slot's
        # metadata and every old run page must remain byte-for-byte unchanged.
        aborted = directory / "rollback.ram"
        recovery.sparse_copy(normal, aborted)
        _, _, older_sb = recovery.newest_sb(base)
        protected = old_pages | {older_sb[5], older_sb[6]}
        protected_before = {p: page_bytes(aborted, p) for p in protected}
        guest(aborted, "rollback", "repeated-page-write-rollbacks")
        if any(page_bytes(aborted, p) != content for p, content in protected_before.items()):
            raise RuntimeError("aborted metadata allocation overwrote the older checkpoint")
        recovery.flip_byte(aborted, recovery.FS_START + new_slot * recovery.PAGE + 8)
        view(aborted, old_runs, "rollback-fallback-old-chain")
        run.regress(aborted, "old", "rollback-fallback-baseline-data")
        aborted.unlink()

        # Corrupt only the old chain, leaving the active compacted view valid.
        # A new metadata allocation must refuse to recycle an untraceable slot.
        corrupt = directory / "older-chain-corrupt.ram"
        recovery.sparse_copy(normal, corrupt)
        recovery.flip_byte(corrupt, recovery.FS_START + old_head * recovery.PAGE + 64)
        guest(corrupt, "refuse", "corrupt-old-chain-fail-closed")
        view(corrupt, new_runs, "corrupt-old-chain-active-view")
        corrupt.unlink()

        # Retire the older slot and force enough metadata COW cycles to reuse
        # old run pages; confirm that this actually happened on the image.
        guest(normal, "churn", "retire-and-reuse")
        reused_run_pages = sum(page_prefix(normal, p) != page_prefix(base, p)
                               for p in old_pages - new_pages)
        if not reused_run_pages:
            raise RuntimeError("no retired run page was reused by metadata GC")
        view(normal, new_runs, "post-reuse-reboot-1")
        view(normal, new_runs, "post-reuse-reboot-2")
        run.regress(normal, "old", "post-reuse-baseline-data")
        normal.unlink()

        for stage in range(1, 6):
            ram = clone(f"checkpoint-{stage}")
            run.boot(ram, f"compact-checkpoint-{stage}", f"pmfs_gc fault {stage} 0",
                     f"PMFS_FAULT_POINT stage={stage}", False,
                     failure_marker=b"PMFS_GC_FAIL")
            _, _, sb = recovery.newest_sb(ram)
            expected_head = new_head if stage >= 4 else old_head
            if sb[7] != expected_head:
                raise RuntimeError(f"checkpoint {stage}: unexpected run root")
            view(ram, new_runs if stage >= 4 else old_runs, f"checkpoint-{stage}-view")
            ram.unlink()

        for stage in (6, 7):
            for skip in (0, 1, 16):
                ram = clone(f"page-{stage}-{skip}")
                run.boot(ram, f"compact-page-{stage}-{skip}",
                         f"pmfs_gc fault {stage} {skip}",
                         f"PMFS_FAULT_POINT stage={stage}", False,
                         failure_marker=b"PMFS_GC_FAIL")
                _, _, sb = recovery.newest_sb(ram)
                if sb[7] != old_head:
                    raise RuntimeError(f"page {stage}/{skip}: old root not retained")
                view(ram, old_runs, f"page-{stage}-{skip}-old-view")
                ram.unlink()
        print(f"PMFS_GC_RECOVERY_PASS boots={run.count} old_runs={old_runs} "
              f"new_runs={new_runs} output_pages={output_pages} "
              f"reused_old_run_pages={reused_run_pages} cpus={args.cpus} "
              f"nvdimm={int(args.nvdimm)} zicbom={int(args.zicbom)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print(f"PMFS_GC_RECOVERY_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
