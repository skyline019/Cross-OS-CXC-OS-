#!/usr/bin/env python3
"""Native same-directory rename at xv6 log cut points (process-kill model).

Requires XV6_LOG_FAULT_INJECT. Input fs.img is never modified. This tests the
file-backed QEMU block log, not torn sectors, reordered storage or power loss.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import sys
import tempfile

from xv6_log_recovery import DiskLog, Runner, TestFailure, check_recovery_trace


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def guest(runner, disk, label, mode):
    marker = (rb"^(?:VFS_TEST_PASS mode=" + mode.encode() +
              rb"|VFS_TEST_FAIL mode=[^\r\n]*)\r?\n")
    output = runner.boot(disk, label, f"vfstest {mode}", marker)
    if b"VFS_TEST_FAIL" in output:
        raise TestFailure(f"{label}: guest oracle failed")
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--cpus", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    image = root / "fs.img"
    if not 1 <= args.cpus <= 8 or args.timeout < 1 or not image.is_file():
        parser.error("invalid CPU/timeout or missing fs.img")
    before = digest(image)
    runner = Runner(root, args.qemu, args.cpus, args.timeout, args.log_dir)
    with tempfile.TemporaryDirectory(prefix="native-rename-") as temporary:
        directory = Path(temporary)
        baseline = directory / "baseline.img"
        shutil.copyfile(image, baseline)
        guest(runner, baseline, "native-initialize", "native-init")
        guest(runner, baseline, "native-baseline", "native-old")
        for stage in range(1, 6):
            disk = directory / f"stage-{stage}.img"
            shutil.copyfile(baseline, disk)
            marker = (rb"^(?:XV6_LOG_FAULT stage=" + str(stage).encode() +
                      rb" blocks=(\d+)|VFS_TEST_FAIL mode=[^\r\n]*)\r?\n")
            output = runner.boot(disk, f"native-stage-{stage}-crash",
                                 f"vfstest native-fault {stage}", marker,
                                 shell_after=False)
            match = re.search(marker, output, re.MULTILINE)
            if b"VFS_TEST_FAIL" in output or not match or match.group(1) is None:
                raise TestFailure(f"stage {stage}: rename did not reach fault point")
            count = int(match.group(1))
            log = DiskLog(disk)
            blocks = log.header()
            if stage in (1, 5):
                if blocks or (stage == 1 and count < 2) or (stage == 5 and count != 0):
                    raise TestFailure(f"stage {stage}: unexpected log header")
            elif len(blocks) != count or count < 2:
                raise TestFailure(f"stage {stage}: unexpected committed blocks")
            if stage == 2:
                negative = directory / "negative.img"
                shutil.copyfile(disk, negative)
                DiskLog(negative).discard_commit_header()
                negative_output = guest(runner, negative, "native-negative-drop-log", "native-old")
                check_recovery_trace(negative_output, [])
                print("PASS negative control: discarding committed rename loses new name", flush=True)
            expected = "native-old" if stage == 1 else "native-new"
            output = guest(runner, disk, f"native-stage-{stage}-recover", expected)
            check_recovery_trace(output, blocks)
            if DiskLog(disk).header():
                raise TestFailure("recovery did not clear native log")
            output = guest(runner, disk, f"native-stage-{stage}-reboot", expected)
            check_recovery_trace(output, [])
            print(f"PASS native stage={stage} state={expected} blocks={len(blocks)}", flush=True)
        normal = directory / "normal.img"
        shutil.copyfile(baseline, normal)
        guest(runner, normal, "native-normal-move", "native-move")
        guest(runner, normal, "native-normal-reboot", "native-new")
    if digest(image) != before:
        raise TestFailure("input image changed")
    print("NATIVE_RENAME_RECOVERY_PASS stages=5 negative_controls=1")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, TestFailure) as exc:
        print(f"NATIVE_RENAME_RECOVERY_FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
