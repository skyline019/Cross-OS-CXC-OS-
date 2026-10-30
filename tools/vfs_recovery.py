#!/usr/bin/env python3
"""Process-restart oracle for PMFS subtree rename and hard-link transactions.

Run on a disposable build with PMEM_MAP and PMFS_FAULT_INJECT, after building
kernel/kernel and fs.img. All guest disks and RAM/NVDIMM backends are temporary.
The old/new checks are a QEMU process-kill model, not physical power failure.
"""

import argparse
import os
from pathlib import Path
import sys
import tempfile

import pmfs_recovery as recovery


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-riscv64"))
    parser.add_argument("--cpus", type=int, default=int(os.environ.get("CPUS", "3")))
    parser.add_argument("--timeout", type=int, default=45)
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
        parser.error("build the test-only MAP kernel and fs.img first")
    if args.nvdimm:
        recovery.RAM_SIZE = 32 * 1024 * 1024
        recovery.PMEM_START = 0
        recovery.FS_START = recovery.PAGE
    if args.log_dir:
        args.log_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="vfs-recovery-") as tmp:
        directory = Path(tmp)
        base = directory / "base.ram"
        with base.open("wb") as f:
            f.truncate(recovery.RAM_SIZE)
        run = recovery.Runner(root, args.qemu, directory, args.timeout, args.cpus,
                              args.zicbom, args.log_dir, args.nvdimm, args.backend)

        def vfs(ram, mode, label=None):
            return run.boot(ram, label or mode, f"vfstest {mode}",
                            f"VFS_TEST_PASS mode={mode}",
                            failure_marker=b"VFS_TEST_FAIL")

        def copy_base(label):
            ram = directory / (label + ".ram")
            recovery.sparse_copy(base, ram)
            return ram

        run.regress(base, "init", "format-and-baseline")
        # A non-replacing rename appends only the dentry snapshot; replacing
        # an empty destination removes an inode as well and appends both pages.
        # Exercise both shapes, including old target existence after recovery.
        for replace in (False, True):
            kind = "replace" if replace else "simple"
            prepared = copy_base(f"{kind}-base")
            vfs(prepared, "prepare-replace" if replace else "prepare", f"{kind}-prepare")
            old = "old-replace" if replace else "old"
            vfs(prepared, old, f"{kind}-baseline-reboot")

            def copy_prepared(label):
                ram = directory / (kind + "-" + label + ".ram")
                recovery.sparse_copy(prepared, ram)
                return ram

            ram = copy_prepared("normal")
            vfs(ram, "move", f"{kind}-move")
            vfs(ram, "new", f"{kind}-normal-reboot-1")
            vfs(ram, "new", f"{kind}-normal-reboot-2")
            ram.unlink()

            # With QEMU's shared file, stage 4 can expose the commit word
            # before the following instrumentation fence completes.
            for stage in range(1, 6):
                ram = copy_prepared(f"checkpoint-{stage}")
                run.boot(ram, f"{kind}-checkpoint-{stage}", f"vfstest fault {stage} 0",
                         f"PMFS_FAULT_POINT stage={stage}", False,
                         failure_marker=b"VFS_TEST_FAIL")
                expected = "new" if stage >= 4 else old
                vfs(ram, expected, f"{kind}-checkpoint-{stage}-{expected}")
                ram.unlink()

            # Interrupt the one or two newly appended metadata pages, before
            # publication; the unchanged root is always reused, not written.
            for stage in (6, 7):
                for skip in range(2 if replace else 1):
                    ram = copy_prepared(f"page-{stage}-{skip}")
                    run.boot(ram, f"{kind}-page-{stage}-{skip}",
                             f"vfstest fault {stage} {skip}",
                             f"PMFS_FAULT_POINT stage={stage}", False,
                             failure_marker=b"VFS_TEST_FAIL")
                    vfs(ram, old, f"{kind}-page-{stage}-{skip}-old")
                    ram.unlink()
            prepared.unlink()

        # Both creating and removing an alias publish inode link counts and
        # dentries together. Every interruption must recover the old or new
        # graph, with matching inode numbers, contents and link counts.
        one = copy_base("link-one-base")
        vfs(one, "link-prepare")
        vfs(one, "link-one", "link-one-baseline-reboot")
        two = directory / "link-two-base.ram"
        recovery.sparse_copy(one, two)
        vfs(two, "link-add", "link-two-prepare")
        vfs(two, "link-two", "link-two-baseline-reboot")
        for action, prepared, old, new in (
            ("add", one, "link-one", "link-two"),
            ("drop", two, "link-two", "link-one"),
        ):
            def link_copy(label):
                ram = directory / f"link-{action}-{label}.ram"
                recovery.sparse_copy(prepared, ram)
                return ram

            ram = link_copy("normal")
            vfs(ram, f"link-{action}")
            vfs(ram, new, f"link-{action}-normal-reboot-1")
            vfs(ram, new, f"link-{action}-normal-reboot-2")
            ram.unlink()
            for stage in range(1, 6):
                ram = link_copy(f"checkpoint-{stage}")
                run.boot(ram, f"link-{action}-checkpoint-{stage}",
                         f"vfstest fault-link-{action} {stage} 0",
                         f"PMFS_FAULT_POINT stage={stage}", False,
                         failure_marker=b"VFS_TEST_FAIL")
                expected = new if stage >= 4 else old
                vfs(ram, expected, f"link-{action}-checkpoint-{stage}-{expected}")
                ram.unlink()
            for stage in (6, 7):
                for skip in range(2):
                    ram = link_copy(f"page-{stage}-{skip}")
                    run.boot(ram, f"link-{action}-page-{stage}-{skip}",
                             f"vfstest fault-link-{action} {stage} {skip}",
                             f"PMFS_FAULT_POINT stage={stage}", False,
                             failure_marker=b"VFS_TEST_FAIL")
                    vfs(ram, old, f"link-{action}-page-{stage}-{skip}-{old}")
                    ram.unlink()
        one.unlink()
        two.unlink()

        # Content updates through a dedicated stream retain hard-link counts
        # and atomically switch all aliases from the old version to the new.
        stream = copy_base("link-stream-base")
        vfs(stream, "stream-prepare")
        vfs(stream, "stream-old", "stream-baseline-reboot")

        def stream_copy(label):
            ram = directory / f"link-stream-{label}.ram"
            recovery.sparse_copy(stream, ram)
            return ram

        ram = stream_copy("normal")
        vfs(ram, "stream-update")
        vfs(ram, "stream-new", "stream-normal-reboot-1")
        vfs(ram, "stream-new", "stream-normal-reboot-2")
        ram.unlink()
        ram = stream_copy("dirty")
        run.boot(ram, "stream-dirty", "vfstest stream-dirty",
                 "VFS_LINK_DIRTY_READY", False, failure_marker=b"VFS_TEST_FAIL")
        vfs(ram, "stream-old", "stream-dirty-reboot")
        ram.unlink()
        for skip in (0, 1):
            for stage in range(1, 6):
                ram = stream_copy(f"checkpoint-{skip}-{stage}")
                run.boot(ram, f"stream-checkpoint-{skip}-{stage}",
                         f"vfstest stream-fault {stage} {skip}",
                         f"PMFS_FAULT_POINT stage={stage}", False,
                         failure_marker=b"VFS_TEST_FAIL")
                expected = "stream-new" if skip == 1 and stage >= 4 else "stream-old"
                vfs(ram, expected, f"stream-checkpoint-{skip}-{stage}-view")
                ram.unlink()
        for stage in (6, 7):
            for skip in (0, 1):
                ram = stream_copy(f"page-{stage}-{skip}")
                run.boot(ram, f"stream-page-{stage}-{skip}",
                         f"vfstest stream-fault {stage} {skip}",
                         f"PMFS_FAULT_POINT stage={stage}", False,
                         failure_marker=b"VFS_TEST_FAIL")
                vfs(ram, "stream-old", f"stream-page-{stage}-{skip}-old")
                ram.unlink()
        stream.unlink()

        # A page-write interruption happens before the inactive slot is
        # invalidated. Tear the active slot afterwards: the older checkpoint
        # must still contain its own complete namespace and live run chain.
        dual = copy_base("gc-dual-base")
        vfs(dual, "gc-prepare")
        vfs(dual, "gc-b", "gc-dual-baseline-reboot")
        _, active_slot, _ = recovery.newest_sb(dual)
        ram = directory / "gc-dual-normal.ram"
        recovery.sparse_copy(dual, ram)
        vfs(ram, "gc-move")
        vfs(ram, "gc-a", "gc-dual-normal-reboot")
        ram.unlink()
        for stage in (6, 7):
            ram = directory / f"gc-dual-fault-{stage}.ram"
            recovery.sparse_copy(dual, ram)
            run.boot(ram, f"gc-dual-fault-{stage}", f"vfstest gc-fault {stage} 0",
                     f"PMFS_FAULT_POINT stage={stage}", False,
                     failure_marker=b"VFS_TEST_FAIL")
            recovery.flip_byte(ram, recovery.FS_START + active_slot * recovery.PAGE + 8)
            vfs(ram, "gc-a", f"gc-dual-fallback-{stage}-1")
            vfs(ram, "gc-a", f"gc-dual-fallback-{stage}-2")
            ram.unlink()
        dual.unlink()
        print(f"VFS_RECOVERY_PASS boots={run.count} cpus={args.cpus} "
              f"nvdimm={int(args.nvdimm)} zicbom={int(args.zicbom)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print(f"VFS_RECOVERY_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
