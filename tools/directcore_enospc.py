#!/usr/bin/env python3
"""Bounded low-space and fault-disabled tests for DIRECT_CORE_LINE_LIMIT=14."""
import argparse
from pathlib import Path
import shutil
import sys
import tempfile

from directcore_recovery import DEVICE_BYTES, Runner, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--backend", choices=("virtio", "nvme"), default="nvme")
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    if not (root / "kernel/kernel").is_file() or not (root / "fs.img").is_file():
        parser.error("need a built kernel and fs.img")
    if args.log_dir:
        args.log_dir.mkdir(parents=True, exist_ok=True)
    source = sha256(root / "fs.img")
    with tempfile.TemporaryDirectory(prefix="directcore-enospc-") as tmp:
        tmp = Path(tmp)
        run = Runner(root, args.qemu, tmp, args.backend, 1, 60, args.log_dir)
        device = tmp / "small.pmem"
        with device.open("wb") as f:
            f.truncate(DEVICE_BYTES)
        formatted = run.boot(device, "format", "directcore format --force", rb"^DIRECTCORE_FORMAT_PASS\r?\n")
        if b"DIRECTCORE_READY backend=nvdimm units=14 " not in formatted:
            raise RuntimeError("kernel was not built with DIRECT_CORE_LINE_LIMIT=14")
        run.boot(device, "baseline", "directcore write 1 128", rb"^DIRECTCORE_WRITE_PASS generation=1 size=128\r?\n")
        baseline = sha256(device)
        run.boot(device, "enospc", "directcore write 2 128", rb"^DIRECTCORE_TEST_FAIL reason=write\r?\n")
        if sha256(device) != baseline:
            raise RuntimeError("failed preflight changed the PMEM image")
        run.boot(device, "old-after-enospc", "directcore check 1 128", rb"^DIRECTCORE_CHECK_PASS generation=1 size=128\r?\n")
        # Fault injection is absent in this build, so it must return before
        # writing anything rather than hang or accidentally update the file.
        run.boot(device, "fault-disabled", "directcore fault 1 2 128", rb"^DIRECTCORE_TEST_FAIL reason=write\r?\n")
        if sha256(device) != baseline:
            raise RuntimeError("disabled fault operation changed the PMEM image")
        run.boot(device, "old-after-disabled-fault", "directcore check 1 128", rb"^DIRECTCORE_CHECK_PASS generation=1 size=128\r?\n")
        run.boot(device, "small-truncate", "directcore write 2 0", rb"^DIRECTCORE_WRITE_PASS generation=2 size=0\r?\n")
        run.boot(device, "small-truncate-reboot", "directcore check 2 0", rb"^DIRECTCORE_CHECK_PASS generation=2 size=0\r?\n")
        if sha256(root / "fs.img") != source:
            raise RuntimeError("input root image changed")
        print(f"DIRECTCORE_ENOSPC_PASS boots={run.count} lines=14 preflight_unchanged=1 fault_disabled=1", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"DIRECTCORE_ENOSPC_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
