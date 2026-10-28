#!/usr/bin/env python3
"""Process-restart oracle for atomic PMFS directory-subtree rename.

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
                              args.zicbom, args.log_dir, args.nvdimm)

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
        print(f"VFS_RECOVERY_PASS boots={run.count} cpus={args.cpus} "
              f"nvdimm={int(args.nvdimm)} zicbom={int(args.zicbom)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print(f"VFS_RECOVERY_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
