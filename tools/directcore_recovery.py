#!/usr/bin/env python3
"""Exercise the isolated single-file direct-PMEM core on QEMU NVDIMM.

Requires a disposable DIRECT_CORE=1 kernel built with PMEM_NVDIMM and
DIRECT_CORE_FAULT_INJECT. Each boot gets a private root disk; the NVDIMM files
are temporary. SIGKILL/restart checks a bounded process-restart model, not
physical power loss, arbitrary tearing or the unfinished VFS filesystem.
"""
import argparse
import gzip
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
import zlib

from xv6_log_recovery import SHELL_PROMPT, disk_arguments

DEVICE_BYTES = 32 * 1024 * 1024
RESERVED = 4096
LINE = 64


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for data in iter(lambda: source.read(1024 * 1024), b""):
            h.update(data)
    return h.hexdigest()


def read_line(path, index):
    with path.open("rb") as image:
        image.seek(RESERVED + index * LINE)
        data = image.read(LINE)
    if len(data) != LINE:
        raise ValueError("short direct-PMEM image")
    return bytearray(data)


def write_line(path, index, data):
    if len(data) != LINE:
        raise ValueError("bad line length")
    with path.open("r+b") as image:
        image.seek(RESERVED + index * LINE)
        image.write(data)
        image.flush()
        os.fsync(image.fileno())


def root_id(path, slot):
    word = struct.unpack_from("<Q", read_line(path, slot))[0]
    index = word & 0xffffffff
    if index < 2 or RESERVED + (index + 1) * LINE > path.stat().st_size:
        raise ValueError("invalid root pointer in test fixture")
    return index


def recalc_checkpoint(cp):
    struct.pack_into("<I", cp, 60, 0)
    struct.pack_into("<I", cp, 60, zlib.crc32(cp))


def mutate(path, kind):
    if kind == "root-checksum":
        root = read_line(path, 0)
        root[4] ^= 0x80
        write_line(path, 0, root)
        return
    cp_id = root_id(path, 1)
    cp = read_line(path, cp_id)
    if kind == "version":
        struct.pack_into("<H", cp, 4, 99)
    elif kind == "same-epoch":
        old = read_line(path, root_id(path, 0))
        struct.pack_into("<Q", cp, 8, struct.unpack_from("<Q", old, 8)[0])
    elif kind == "alias":
        table_id = struct.unpack_from("<I", cp, 16)[0]
        table = read_line(path, table_id)
        struct.pack_into("<I", table, 4, table_id)  # inode aliases table itself
        write_line(path, table_id, table)
        struct.pack_into("<I", cp, 20, zlib.crc32(table))
    elif kind == "data-bitflip":
        table = read_line(path, struct.unpack_from("<I", cp, 16)[0])
        inode = read_line(path, struct.unpack_from("<I", table, 4)[0])
        node = read_line(path, struct.unpack_from("<I", inode, 4)[0])
        data_id = struct.unpack_from("<I", node, 0)[0]
        data = read_line(path, data_id)
        data[0] ^= 1
        write_line(path, data_id, data)
        return
    else:
        raise ValueError("unknown mutation")
    recalc_checkpoint(cp)
    write_line(path, cp_id, cp)


class Runner:
    def __init__(self, root, qemu, tmp, backend, cpus, timeout, logs, zicbom=False):
        self.root, self.qemu, self.tmp = root, qemu, tmp
        self.backend, self.cpus, self.timeout, self.logs = backend, cpus, timeout, logs
        self.zicbom = zicbom
        self.count = 0

    def boot(self, device, label, command, marker, crash=False, failure_marker=None):
        self.count += 1
        disk = self.tmp / f"root-{self.count}.img"
        shutil.copyfile(self.root / "fs.img", disk)
        cmd = [self.qemu, "-machine", "virt,nvdimm=on", "-bios", "none",
               "-kernel", str(self.root / "kernel/kernel"),
               "-m", "128M,slots=1,maxmem=160M", "-smp", str(self.cpus),
               "-nographic", "-global", "virtio-mmio.force-legacy=false"]
        cmd += disk_arguments(disk, self.backend)
        cmd += ["-object", f"memory-backend-file,id=pmem0,share=on,"
                f"mem-path={device},size=32M", "-device", "nvdimm,id=nvdimm0,memdev=pmem0"]
        if self.zicbom:
            cmd += ["-cpu", "rv64,zicbom=true"]
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True)
        selector = selectors.DefaultSelector()
        selector.register(proc.stdout, selectors.EVENT_READ)
        os.set_blocking(proc.stdout.fileno(), False)
        output = bytearray()

        def wait_for(predicate, what):
            deadline = time.monotonic() + self.timeout
            while not predicate():
                if proc.poll() is not None:
                    raise RuntimeError(f"{label}: QEMU exited before {what}")
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"{label}: timeout waiting for {what}")
                for key, _ in selector.select(0.2):
                    try:
                        chunk = os.read(key.fd, 8192)
                    except BlockingIOError:
                        continue
                    if chunk:
                        output.extend(chunk)
                if b"panic:" in output:
                    raise RuntimeError(f"{label}: guest panic")
                if failure_marker and failure_marker in output:
                    raise RuntimeError(f"{label}: guest reported a test failure")

        try:
            wait_for(lambda: b"init: starting sh" in output and SHELL_PROMPT.search(output),
                     "shell prompt")
            if (b"DIRECTCORE_READY backend=nvdimm " not in output or
                    b"pmfs: v3 disabled for direct-core test" not in output or
                    (self.backend == "nvme" and b"NVME_READY " not in output) or
                    (self.zicbom and b"pmem: Zicbom CBO.CLEAN enabled" not in output)):
                raise RuntimeError(f"{label}: wrong kernel/device configuration")
            start = len(output)
            proc.stdin.write((command + "\n").encode())
            proc.stdin.flush()
            pattern = re.compile(marker, re.MULTILINE)
            wait_for(lambda: pattern.search(output, start), "result")
            match = pattern.search(output, start)
            if not crash:
                wait_for(lambda: SHELL_PROMPT.search(output, match.end()), "completed prompt")
            print(f"PASS {label}", flush=True)
            return bytes(output)
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
            if self.logs:
                (self.logs / f"{self.count:02d}-{label}.log").write_bytes(output)
            disk.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--backend", choices=("virtio", "nvme"), default="nvme")
    parser.add_argument("--cpus", type=int, default=1)
    parser.add_argument("--zicbom", action="store_true", help="enable QEMU Zicbom and require the corresponding kernel path")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    kernel, image = root / "kernel/kernel", root / "fs.img"
    if not kernel.is_file() or not image.is_file() or not 1 <= args.cpus <= 8 or args.timeout < 1:
        parser.error("need built kernel/kernel, fs.img, cpus 1..8 and positive timeout")
    if args.log_dir:
        args.log_dir.mkdir(parents=True, exist_ok=True)
    before = (sha256(kernel), sha256(image))
    with tempfile.TemporaryDirectory(prefix="directcore-recovery-") as tmp:
        tmp = Path(tmp)
        run = Runner(root, args.qemu, tmp, args.backend, args.cpus, args.timeout, args.log_dir, args.zicbom)
        blank = tmp / "baseline.pmem"
        with blank.open("wb") as device:
            device.truncate(DEVICE_BYTES)
        before_blank = sha256(blank)
        run.boot(blank, "blank-fail-closed", "directcore probe", rb"^DIRECTCORE_PROBE_REJECTED\r?\n")
        if sha256(blank) != before_blank:
            raise RuntimeError("probing a blank image modified it")
        run.boot(blank, "format", "directcore format --force", rb"^DIRECTCORE_FORMAT_PASS\r?\n")
        run.boot(blank, "empty-reboot", "directcore check 0 0", rb"^DIRECTCORE_CHECK_PASS generation=0 size=0\r?\n")
        run.boot(blank, "baseline-write", "directcore write 1 128", rb"^DIRECTCORE_WRITE_PASS generation=1 size=128\r?\n")
        run.boot(blank, "baseline-reboot", "directcore check 1 128", rb"^DIRECTCORE_CHECK_PASS generation=1 size=128\r?\n")
        isolation_hash = sha256(blank)
        run.boot(blank, "legacy-isolation", "directcore isolation", rb"^DIRECTCORE_ISOLATION_PASS legacy_calls=5\r?\n")
        if sha256(blank) != isolation_hash:
            raise RuntimeError("disabled legacy PMFS calls changed the NVDIMM")
        same_hash = sha256(blank)
        run.boot(blank, "identical-write", "directcore write 1 128", rb"^DIRECTCORE_WRITE_PASS generation=1 size=128\r?\n")
        if sha256(blank) != same_hash:
            raise RuntimeError("identical content unexpectedly changed the NVDIMM")

        normal = tmp / "normal.pmem"
        shutil.copyfile(blank, normal)
        run.boot(normal, "normal-update", "directcore write 2 128", rb"^DIRECTCORE_WRITE_PASS generation=2 size=128\r?\n")
        run.boot(normal, "normal-reboot", "directcore check 2 128", rb"^DIRECTCORE_CHECK_PASS generation=2 size=128\r?\n")
        run.boot(normal, "truncate-empty", "directcore write 2 0", rb"^DIRECTCORE_WRITE_PASS generation=2 size=0\r?\n")
        run.boot(normal, "empty-after-reboot", "directcore check 2 0", rb"^DIRECTCORE_CHECK_PASS generation=2 size=0\r?\n")
        soak = tmp / "soak.pmem"
        shutil.copyfile(blank, soak)
        run.boot(soak, "overwrite-soak", "directcore soak 400 128", rb"^DIRECTCORE_SOAK_PASS cycles=400 size=128\r?\n")
        run.boot(soak, "soak-reboot", "directcore check 400 128", rb"^DIRECTCORE_CHECK_PASS generation=400 size=128\r?\n")
        concurrent = tmp / "concurrent.pmem"
        shutil.copyfile(blank, concurrent)
        run.boot(concurrent, "concurrent", "directcore concurrent 100 128",
                 rb"^DIRECTCORE_CONCURRENT_PASS writers=2 loops=100 size=128\r?\n")
        run.boot(concurrent, "concurrent-reboot", "directcore which 1 2 128",
                 rb"^DIRECTCORE_VERSION (old|new)\r?\n")

        for stage in range(1, 7):
            target = tmp / f"fault-{stage}.pmem"
            shutil.copyfile(blank, target)
            run.boot(target, f"fault-{stage}", f"directcore fault {stage} 2 128",
                     rb"^DIRECTCORE_FAULT stage=" + str(stage).encode() + rb"\r?\n", crash=True)
            recovered = run.boot(target, f"recover-{stage}", "directcore which 1 2 128",
                                 rb"^DIRECTCORE_VERSION (old|new)\r?\n")
            state = re.search(rb"^DIRECTCORE_VERSION (old|new)\r?$", recovered, re.MULTILINE).group(1)
            if (stage <= 3 and state != b"old") or (stage == 6 and state != b"new"):
                raise RuntimeError(f"fault {stage}: invalid recovered state {state!r}")
            print(f"PASS directcore stage={stage} state={state.decode()}", flush=True)
            target.unlink()

        for kind in ("root-checksum", "version", "same-epoch", "alias"):
            target = tmp / f"corrupt-{kind}.pmem"
            shutil.copyfile(blank, target)
            mutate(target, kind)
            before_corrupt = sha256(target)
            run.boot(target, f"corrupt-{kind}", "directcore probe", rb"^DIRECTCORE_PROBE_REJECTED\r?\n")
            if sha256(target) != before_corrupt:
                raise RuntimeError(f"{kind}: invalid metadata caused a write")
            target.unlink()

        target = tmp / "corrupt-data.pmem"
        shutil.copyfile(blank, target)
        mutate(target, "data-bitflip")
        run.boot(target, "data-crc-disabled-probe", "directcore probe", rb"^DIRECTCORE_PROBE_VALID size=128\r?\n")
        run.boot(target, "data-bitflip-external-oracle", "directcore check 1 128",
                 rb"^DIRECTCORE_TEST_FAIL reason=content\r?\n")

        target = tmp / "trace.pmem"
        shutil.copyfile(blank, target)
        if args.log_dir:
            # The 32 MiB sparse baseline compresses well and is necessary to
            # reproduce the actual store/flush/drain trace replay.
            with blank.open("rb") as source, gzip.open(args.log_dir / "trace-baseline.pmem.gz", "wb") as dest:
                shutil.copyfileobj(source, dest)
        run.boot(target, "trace-update", "directcore trace 2 128", rb"^DIRECTCORE_WRITE_PASS generation=2 size=128\r?\n")
        if args.log_dir:
            # A full-image comparison in the replay tool detects untraced stores.
            with target.open("rb") as source, gzip.open(args.log_dir / "trace-final.pmem.gz", "wb") as dest:
                shutil.copyfileobj(source, dest)
        run.boot(target, "trace-reboot", "directcore check 2 128", rb"^DIRECTCORE_CHECK_PASS generation=2 size=128\r?\n")
        if (sha256(kernel), sha256(image)) != before:
            raise RuntimeError("input kernel or root image changed")
        print(f"DIRECTCORE_RECOVERY_PASS boots={run.count} fault_stages=6 "
              f"metadata_negative=4 data_crc_disabled_control=1 backend={args.backend} "
              f"cpus={args.cpus} zicbom={int(args.zicbom)}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print(f"DIRECTCORE_RECOVERY_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
