#!/usr/bin/env python3
"""Run the same ordinary-file API diagnostic against a fresh guest path.

This diagnostic records failures and blocked features; successful execution of
this script is NOT a parity verdict. It copies fs.img before booting. This
runner does not yet attach a direct NVDIMM filesystem and must not be used to
claim that legacy PMFS or the directcore test syscall is the required backend.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import sys
import tempfile

from fs_parity_gate import ReportError, parse_report
from xv6_log_recovery import Runner, TestFailure


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--backend", choices=("virtio", "nvme"), default="virtio")
    parser.add_argument("--target", required=True, help="new absolute guest directory on a disposable FS")
    parser.add_argument("--cpus", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--output", type=Path, required=True, help="raw guest serial log")
    args = parser.parse_args(argv)
    if (not 1 <= args.cpus <= 8 or args.timeout < 1 or
            not re.fullmatch(r"/[a-zA-Z0-9_/-]+", args.target) or
            args.target.endswith("/")):
        parser.error("invalid CPU, timeout, or guest target path")
    root = args.root.resolve()
    image = root / "fs.img"
    if not image.is_file() or not (root / "kernel/kernel").is_file():
        parser.error("build kernel/kernel and fs.img containing fsparity first")
    before = digest(image)
    runner = Runner(root, args.qemu, args.cpus, args.timeout, None, args.backend)
    with tempfile.TemporaryDirectory(prefix="fs-parity-probe-") as tmp:
        disk = Path(tmp) / "disk.img"
        shutil.copyfile(image, disk)
        marker = rb"^(?:FS_PARITY_END total=\d+ pass=\d+ fail=\d+ blocked=\d+|FS_PARITY_SETUP_FAIL)\r?\n"
        output = runner.boot(disk, "fs-parity-probe", f"fsparity {args.target}", marker)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    if digest(image) != before:
        raise TestFailure("input fs.img changed")
    report = parse_report(output)
    print(f"FS_PARITY_PROBE_COMPLETE pass={report.passed} fail={report.failed} "
          f"blocked={report.blocked} target={report.root} (NOT an acceptance verdict)")
    for case, (result, code) in report.cases.items():
        if result != "PASS":
            print(f"  {case}: {result} code={code}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, TestFailure, ReportError) as exc:
        print(f"FS_PARITY_PROBE_FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
