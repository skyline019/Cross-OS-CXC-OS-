#!/usr/bin/env python3
"""Deterministic QEMU process-kill tests of the native xv6 block log.

The kernel must be built with XV6_LOG_FAULT_INJECT and fs.img must contain
logtest. Each case starts from a temporary copy of fs.img. The five cut points
cover log data written, commit header written, one home block installed, all
home blocks installed, and the header cleared. A normal kernel must reject the
injection syscall; --expect-disabled checks this separately.

The host kills QEMU only after the guest reports a completed software boundary.
This tests process restart with a file-backed disk and its host page cache; it
does not emulate a hardware power loss, torn sectors, or reordered writes.
"""

import argparse
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time

BSIZE = 1024
FSMAGIC = 0x10203040
# Do not treat the end of a currently buffered partial line as a complete
# marker: xv6 printf may send one character per write/interrupt.
FAILURE_LINE = re.compile(rb"(?m)^XV6_LOG_TEST_FAIL code=(\d+)\r?\n")


class TestFailure(RuntimeError):
    pass


class DiskLog:
    """Read the actual superblock, log header and committed disk blocks."""

    def __init__(self, path):
        self.path = path
        with path.open("rb") as f:
            f.seek(BSIZE)
            raw = f.read(32)
        if len(raw) != 32:
            raise TestFailure("short xv6 superblock")
        magic, self.size, _, _, self.nlog, self.start, _, _ = struct.unpack("<8I", raw)
        if (magic != FSMAGIC or self.size * BSIZE != path.stat().st_size or
                self.nlog < 2 or self.nlog > BSIZE // 4 or self.start < 2 or
                self.start + self.nlog > self.size):
            raise TestFailure("invalid xv6 disk/log layout")

    def header(self):
        with self.path.open("rb") as f:
            f.seek(self.start * BSIZE)
            raw = f.read(BSIZE)
        if len(raw) != BSIZE:
            raise TestFailure("short xv6 log header")
        count = struct.unpack_from("<i", raw)[0]
        if count < 0 or count > self.nlog - 1:
            raise TestFailure(f"invalid log block count {count}")
        blocks = list(struct.unpack_from(f"<{count}I", raw, 4))
        if (len(set(blocks)) != len(blocks) or
                any(b >= self.size or self.start <= b < self.start + self.nlog
                    for b in blocks)):
            raise TestFailure(f"invalid log destinations: {blocks}")
        return blocks

    def installed(self, blocks):
        equal = []
        with self.path.open("rb") as f:
            for i, block in enumerate(blocks):
                f.seek((self.start + i + 1) * BSIZE)
                log_data = f.read(BSIZE)
                f.seek(block * BSIZE)
                home_data = f.read(BSIZE)
                if len(log_data) != BSIZE or len(home_data) != BSIZE:
                    raise TestFailure("short transaction block")
                equal.append(log_data == home_data)
        return equal

    def discard_commit_header(self):
        # Negative control only: simulate a faulty recovery which ignores a
        # committed log, without changing the real case's disk image.
        with self.path.open("r+b") as f:
            f.seek(self.start * BSIZE)
            f.write(struct.pack("<I", 0))
            f.flush()
            os.fsync(f.fileno())


class Runner:
    def __init__(self, root, qemu, cpus, timeout, log_dir):
        self.root = root
        self.qemu = qemu
        self.cpus = cpus
        self.timeout = timeout
        self.log_dir = log_dir
        if log_dir is not None:
            log_dir.mkdir(parents=True, exist_ok=True)

    def boot(self, disk, label, command, marker, *, shell_after=True,
             expected_failure=None):
        cmd = [self.qemu, "-machine", "virt", "-bios", "none",
               "-kernel", str(self.root / "kernel/kernel"), "-m", "128M",
               "-smp", str(self.cpus), "-nographic", "-global",
               "virtio-mmio.force-legacy=false", "-drive",
               f"file={disk},if=none,format=raw,id=x0", "-device",
               "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"]
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True)
        output = bytearray()
        selector = selectors.DefaultSelector()
        selector.register(proc.stdout, selectors.EVENT_READ)
        os.set_blocking(proc.stdout.fileno(), False)

        def wait_for(predicate, description):
            deadline = time.monotonic() + self.timeout
            while True:
                if b"panic:" in output:
                    raise TestFailure(f"{label}: kernel panic")
                failures = FAILURE_LINE.findall(output)
                if failures and any(int(code) != expected_failure for code in failures):
                    raise TestFailure(f"{label}: guest reported failure {failures[-1].decode()}; "
                                      "build with XV6_LOG_FAULT_INJECT for crash cases")
                if predicate():
                    return
                if proc.poll() is not None:
                    raise TestFailure(f"{label}: QEMU exited {proc.returncode} waiting for {description}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TestFailure(f"{label}: timeout waiting for {description}")
                for key, _ in selector.select(min(0.25, remaining)):
                    try:
                        chunk = os.read(key.fd, 8192)
                    except BlockingIOError:
                        continue
                    if chunk:
                        output.extend(chunk)

        try:
            wait_for(lambda: b"init: starting sh" in output and b"$ " in output, "shell")
            offset = len(output)
            proc.stdin.write((command + "\n").encode())
            proc.stdin.flush()
            pattern = re.compile(marker, re.MULTILINE)
            wait_for(lambda: pattern.search(output, offset) is not None, marker.decode())
            matched = pattern.search(output, offset)
            if shell_after:
                wait_for(lambda: b"$ " in output[matched.end():], "completed shell prompt")
        except Exception:
            sys.stderr.write(f"--- {label} serial tail ---\n" +
                             output[-6000:].decode("utf-8", "replace") + "\n")
            raise
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
            proc.wait(timeout=5)
            while True:
                try:
                    chunk = os.read(proc.stdout.fileno(), 8192)
                except BlockingIOError:
                    break
                if not chunk:
                    break
                output.extend(chunk)
            selector.close()
            proc.stdin.close()
            proc.stdout.close()
            if self.log_dir is not None:
                (self.log_dir / f"{label}.log").write_bytes(output)
        print(f"PASS {label}", flush=True)
        return bytes(output)


def check_recovery_trace(output, expected):
    found = [(int(tail), int(dst)) for tail, dst in
             re.findall(rb"(?m)^recovering tail (\d+) dst (\d+)\r?\n", output)]
    wanted = list(enumerate(expected))
    if found != wanted:
        raise TestFailure(f"recovery trace {found} != committed log {wanted}")


def crash_suite(root, runner, directory):
    for stage in range(1, 6):
        disk = directory / f"stage-{stage}.img"
        shutil.copyfile(root / "fs.img", disk)
        fault_marker = (rb"^XV6_LOG_FAULT stage=" + str(stage).encode() +
                        rb" blocks=(\d+)\r?\n")
        output = runner.boot(disk, f"stage-{stage}-crash", f"logtest crash {stage}",
                             fault_marker, shell_after=False)
        match = re.search(fault_marker, output, re.MULTILINE)
        count = int(match.group(1))
        if b"XV6_LOG_BASELINE_READY" not in output:
            raise TestFailure(f"stage {stage}: baseline was not completed")
        log = DiskLog(disk)
        blocks = log.header()
        if stage in (1, 5):
            if blocks or (stage == 1 and count < 2) or (stage == 5 and count != 0):
                raise TestFailure(f"stage {stage}: unexpected empty-header boundary")
        else:
            if len(blocks) != count or count < 2:
                raise TestFailure(f"stage {stage}: marker/log block count mismatch")
            equal = log.installed(blocks)
            if stage == 2 and equal.count(False) < 2:
                raise TestFailure("commit-header cut did not leave multiple home blocks pending")
            if stage == 3 and (not any(equal) or all(equal)):
                raise TestFailure("partial-install cut was not partially installed")
            if stage == 4 and not all(equal):
                raise TestFailure("all home blocks should be installed before clearing the log")

        if stage == 2:
            negative = directory / "negative-no-replay.img"
            shutil.copyfile(disk, negative)
            DiskLog(negative).discard_commit_header()
            negative_output = runner.boot(
                negative, "negative-no-replay", "logtest check new",
                rb"^XV6_LOG_TEST_FAIL code=12\r?\n", expected_failure=12)
            check_recovery_trace(negative_output, [])
            print("PASS negative control: discarding a committed log is detected", flush=True)

        state = "old" if stage == 1 else "new"
        verify_marker = (rb"^XV6_LOG_TEST_PASS state=" + state.encode() + rb"\r?\n")
        output = runner.boot(disk, f"stage-{stage}-recover", f"logtest check {state}",
                             verify_marker)
        check_recovery_trace(output, blocks)
        if DiskLog(disk).header():
            raise TestFailure(f"stage {stage}: recovery failed to clear the log header")
        output = runner.boot(disk, f"stage-{stage}-reboot", f"logtest check {state}",
                             verify_marker)
        check_recovery_trace(output, [])
        if DiskLog(disk).header():
            raise TestFailure(f"stage {stage}: log was not empty after second reboot")
        print(f"PASS stage={stage} state={state} committed_blocks={len(blocks)}", flush=True)
    print("XV6_LOG_RECOVERY_PASS stages=5 negative_controls=1", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-riscv64"))
    parser.add_argument("--cpus", type=int, default=int(os.environ.get("CPUS", "1")))
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--log-dir", type=Path,
                        default=Path(os.environ["XV6_LOG_DIR"]) if os.environ.get("XV6_LOG_DIR") else None)
    parser.add_argument("--expect-disabled", action="store_true",
                        help="check the default kernel rejects fault injection")
    args = parser.parse_args()
    root = args.root.resolve()
    if not 1 <= args.cpus <= 8 or args.timeout < 1:
        parser.error("cpus must be 1..8 and timeout must be positive")
    if not (root / "kernel/kernel").is_file() or not (root / "fs.img").is_file():
        parser.error("build kernel/kernel and fs.img (containing logtest) first")
    runner = Runner(root, args.qemu, args.cpus, args.timeout, args.log_dir)
    with tempfile.TemporaryDirectory(prefix="xv6-log-recovery-") as tmp:
        directory = Path(tmp)
        if args.expect_disabled:
            disk = directory / "disabled.img"
            shutil.copyfile(root / "fs.img", disk)
            runner.boot(disk, "fault-injection-disabled", "logtest disabled",
                        rb"^XV6_LOG_DISABLED_PASS\r?\n")
            print("XV6_LOG_DISABLED_PASS", flush=True)
        else:
            crash_suite(root, runner, directory)


if __name__ == "__main__":
    try:
        main()
    except (OSError, TestFailure, subprocess.SubprocessError) as exc:
        print(f"XV6_LOG_RECOVERY_FAIL: {exc}", file=sys.stderr)
        if os.environ.get("GITHUB_ACTIONS") == "true":
            detail = str(exc).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
            print(f"::error title=xv6 log recovery::{detail}")
        sys.exit(1)
