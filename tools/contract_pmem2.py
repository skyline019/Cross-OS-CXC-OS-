#!/usr/bin/env python3
"""Legacy shared-guest-RAM magic smoke test; NOT a power-loss/NVDIMM test.

The kernel must be built with PMEM_MAP and PMFS_SELFTEST. It runs destructive
boot selftests, so use a disposable checkout and fs.img. The RAM backing file
is always created in a temporary directory, never at ./guest-ram.img.
"""
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import tempfile
import time

TIMEOUT = 90


def boot(env, expect_second):
    proc = subprocess.Popen(["make", "qemu-pmem-file"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env=env, start_new_session=True)
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    out = bytearray()
    deadline = time.monotonic() + TIMEOUT
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.2):
                chunk = os.read(key.fd, 8192)
                if chunk:
                    out.extend(chunk)
                    sys.stdout.write(chunk.decode("utf-8", "replace"))
                    sys.stdout.flush()
            if b"panic:" in out or b"ONBOX_PMFS_GATE_FAIL" in out:
                raise RuntimeError("guest panic or contract gate failure")
            required = [b"ONBOX_PMFS_PMEM0_PASS mode=MAP", b"ONBOX_PMFS_PMEM1_PASS", b"ONBOX_PMFS_PASS"]
            if expect_second:
                required.append(b"ONBOX_PMFS_PMEM2_PASS")
            if all(marker in out for marker in required):
                return
            if proc.poll() is not None:
                raise RuntimeError(f"QEMU exited {proc.returncode}")
        raise RuntimeError("timed out waiting for magic and full selftest gate")
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)
        selector.close()
        proc.stdin.close()
        proc.stdout.close()


def main():
    os.chdir(Path(__file__).resolve().parent.parent)
    env = os.environ.copy()
    env.setdefault("CPUS", "1")
    with tempfile.TemporaryDirectory(prefix="pmfs-magic-") as tmp:
        ram = Path(tmp) / "guest-ram.img"
        with ram.open("wb") as f:
            f.truncate(160 * 1024 * 1024)
        env["PMEM_RAM_FILE"] = str(ram)
        print("=== shared RAM boot 1 ===", flush=True)
        boot(env, False)
        print("=== shared RAM boot 2 ===", flush=True)
        boot(env, True)
    print("ONBOX_PMFS_PMEM2_CONTRACT_PASS boots=2 scope=shared_ram_magic")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print(f"contract-pmem2: FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
