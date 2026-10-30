#!/usr/bin/env python3
"""QEMU guest tests for the experimental NVMe root-disk backend.

All disks are temporary copies. Covers 512-byte and 1024-byte LBA
boot/read-write/reboot and fail-closed missing controller, multiple
controllers, 4 KiB LBA, and invalid redo headers. This is not a physical
power-loss or torn-sector test.
"""
import argparse
import hashlib
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

from xv6_log_recovery import BSIZE, DiskLog, SHELL_PROMPT, disk_arguments


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def expected_panic(output, message, cpus):
    """Recognize an exact panic even if one hart's boot line interleaves.

    printk normally serializes hart-startup messages. panic deliberately
    bypasses that lock to avoid deadlock, so a *suffix* of at most one
    already-running "hart N starting" line can be interspersed with the
    panic, including inside its words. Only bytes from that known line may be
    removed. The entire expected panic and newline must still be present.
    This is used only before a shell or command can print an arbitrary line.
    """
    data = bytes(output).replace(b"\r\n", b"\n")
    target = b"panic: " + message + b"\n"
    if target in data:
        return True
    for start, char in enumerate(data):
        if char != target[0]:
            continue
        for hart in range(1, cpus):
            noise = f"hart {hart} starting\n".encode()
            for begin in range(len(noise)):
                states = {(0, begin)}
                for byte in data[start:start + len(target) + len(noise) - begin]:
                    following = set()
                    for i, j in states:
                        if i < len(target) and byte == target[i]:
                            if i + 1 == len(target):
                                return True
                            following.add((i + 1, j))
                        if j < len(noise) and byte == noise[j]:
                            following.add((i, j + 1))
                    states = following
                    if not states:
                        break
    return False


def boot(qemu, kernel, disk_args, cpus, timeout, marker, command=None, log=None,
         qemu_extra=None):
    cmd = [qemu, "-machine", "virt", "-bios", "none", "-kernel", str(kernel),
           "-m", "128M", "-smp", str(cpus), "-nographic",
           "-global", "virtio-mmio.force-legacy=false"] + disk_args + (qemu_extra or [])
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, start_new_session=True)
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    os.set_blocking(proc.stdout.fileno(), False)
    output = bytearray()

    def wait_for(predicate, name):
        deadline = time.monotonic() + timeout
        while not predicate():
            if proc.poll() is not None:
                raise RuntimeError(f"QEMU exited {proc.returncode} before {name}")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError(f"timeout waiting for {name}")
            for key, _ in selector.select(min(0.2, remaining)):
                try:
                    chunk = os.read(key.fd, 8192)
                except BlockingIOError:
                    continue
                if chunk:
                    output.extend(chunk)

    try:
        if command:
            wait_for(lambda: b"init: starting sh" in output and SHELL_PROMPT.search(output),
                     "shell prompt")
            if b"NVME_READY " not in output or b"panic:" in output:
                raise RuntimeError("guest did not initialize the NVMe root disk")
            offset = len(output)
            proc.stdin.write((command + "\n").encode())
            proc.stdin.flush()
            pattern = re.compile(marker, re.MULTILINE)
            wait_for(lambda: pattern.search(output, offset), "guest result")
            match = pattern.search(output, offset)
            wait_for(lambda: SHELL_PROMPT.search(output, match.end()), "completed shell prompt")
            if b"panic:" in output:
                raise RuntimeError("guest panicked")
        else:
            wait_for(lambda: expected_panic(output, marker, cpus), "expected fail-closed panic")
            if SHELL_PROMPT.search(output):
                raise RuntimeError("unexpected successful boot in negative case")
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
        if log:
            log.write_bytes(output)
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--cpus", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    image, kernel = root / "fs.img", root / "kernel/kernel"
    if not 1 <= args.cpus <= 8 or args.timeout < 1 or not image.is_file() or not kernel.is_file():
        parser.error("need kernel/kernel and fs.img; cpus 1..8 and positive timeout")
    if args.log_dir:
        args.log_dir.mkdir(parents=True, exist_ok=True)

    def log(label):
        return args.log_dir / f"{label}.log" if args.log_dir else None

    original = sha256(image)
    with tempfile.TemporaryDirectory(prefix="nvme-boot-") as tmp:
        tmp = Path(tmp)
        disk = tmp / "normal.img"
        shutil.copyfile(image, disk)
        common = disk_arguments(disk, "nvme")
        boot(args.qemu, kernel, common, args.cpus, args.timeout,
             rb"^NVME_PERSIST_VALUE\r?\n", "echo NVME_PERSIST_VALUE > nvcheck; cat nvcheck",
             log("normal-write"))
        boot(args.qemu, kernel, common, args.cpus, args.timeout,
             rb"^NVME_PERSIST_VALUE\r?\n", "cat nvcheck", log("normal-reboot"))
        print("PASS 512-byte LBA root write/read/reboot", flush=True)

        disk1k = tmp / "lba1k.img"
        shutil.copyfile(image, disk1k)
        lba1k = ["-drive", f"file={disk1k},if=none,format=raw,id=x0,cache=writeback",
                 "-device", "nvme,id=nvme0,serial=xv6nvme",
                 "-device", "nvme-ns,drive=x0,bus=nvme0,nsid=1,logical_block_size=1024,physical_block_size=1024"]
        written = boot(args.qemu, kernel, lba1k, args.cpus, args.timeout,
                       rb"^NVME_1K_VALUE\r?\n", "echo NVME_1K_VALUE > nv1k; cat nv1k",
                       log("1k-write"))
        readback = boot(args.qemu, kernel, lba1k, args.cpus, args.timeout,
                        rb"^NVME_1K_VALUE\r?\n", "cat nv1k", log("1k-reboot"))
        if b"NVME_READY nsid=1 lba=1024 " not in written or b"NVME_READY nsid=1 lba=1024 " not in readback:
            raise RuntimeError("the 1024-byte LBA namespace was not selected")
        print("PASS 1024-byte LBA root write/read/reboot", flush=True)

        boot(args.qemu, kernel, [], args.cpus, args.timeout,
             b"nvme requires exactly one PCI controller on bus 0",
             log=log("missing-controller"))
        second = tmp / "second.img"
        shutil.copyfile(image, second)
        duplicate = common + ["-drive", f"file={second},if=none,format=raw,id=x1,cache=writeback",
                              "-device", "nvme,id=nvme1,serial=xv6nvme1,drive=x1"]
        boot(args.qemu, kernel, duplicate, args.cpus, args.timeout,
             b"nvme requires exactly one PCI controller on bus 0",
             log=log("multiple-controllers"))
        disk4k = tmp / "lba4k.img"
        shutil.copyfile(image, disk4k)
        lba4k = ["-drive", f"file={disk4k},if=none,format=raw,id=x0,cache=writeback",
                 "-device", "nvme,id=nvme0,serial=xv6nvme",
                 "-device", "nvme-ns,drive=x0,bus=nvme0,nsid=1,logical_block_size=4096,physical_block_size=4096"]
        boot(args.qemu, kernel, lba4k, args.cpus, args.timeout,
             b"nvme requires 512/1024-byte LBA without metadata",
             log=log("unsupported-4k-lba"))
        print("PASS absent/multiple controller and unsupported LBA fail closed", flush=True)

        layout = DiskLog(image)
        for label, header, expected in (
                ("bad-count", struct.pack("<i", layout.nlog), b"invalid header count"),
                ("bad-destination", struct.pack("<iI", 1, layout.start), b"invalid header destination"),
                ("duplicate-destination", struct.pack("<iII", 2, 100, 100), b"duplicate header destination")):
            damaged = tmp / f"{label}.img"
            shutil.copyfile(image, damaged)
            with damaged.open("r+b") as f:
                f.seek(layout.start * BSIZE)
                f.write(header)
                f.flush()
                os.fsync(f.fileno())
            boot(args.qemu, kernel, disk_arguments(damaged, "nvme"), args.cpus, args.timeout,
                 b"log: " + expected, log=log(label))
        print("PASS corrupt log count/destination/duplicate fail closed", flush=True)

    if sha256(image) != original:
        raise RuntimeError("original fs.img unexpectedly changed")
    print("NVME_BOOT_TEST_PASS positive_boots=4 negative_cases=6 original_image=unchanged", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as exc:
        print(f"NVME_BOOT_TEST_FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
