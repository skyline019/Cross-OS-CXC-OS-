#!/usr/bin/env python3
"""
PMEM2 cold-reboot contract:
  1) Boot with memory-backend-file → PMEM1 plants durable magic
  2) Kill QEMU (simulated power loss)
  3) Reboot same file → must see ONBOX_PMFS_PMEM2_PASS
"""
import os
import re
import signal
import subprocess
import sys
import time

RAM_FILE = "guest-ram.img"
RAM_SIZE = "160M"
TIMEOUT = 90.0


def kill_tree(proc):
    try:
        ps = subprocess.run(
            ["ps", "-opid", "--no-headers", "--ppid", str(proc.pid)],
            stdout=subprocess.PIPE,
            encoding="utf8",
            check=False,
        )
        for line in ps.stdout.splitlines():
            try:
                os.kill(int(line.strip()), signal.SIGKILL)
            except OSError:
                pass
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
    except Exception:
        pass


def run_boot(env, expect_pmem2: bool) -> str:
    proc = subprocess.Popen(
        ["make", "qemu-pmem-file"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )
    os.set_blocking(proc.stdout.fileno(), False)
    out = ""
    deadline = time.time() + TIMEOUT
    try:
        while time.time() < deadline:
            try:
                chunk = os.read(proc.stdout.fileno(), 4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                out += text
                if "ONBOX_PMFS_PASS" in out and "ONBOX_PMFS_PMEM1_PASS" in out:
                    if expect_pmem2:
                        if "ONBOX_PMFS_PMEM2_PASS" in out:
                            break
                    else:
                        break
            else:
                time.sleep(0.05)
            if proc.poll() is not None:
                break
    finally:
        kill_tree(proc)
    return out


def main():
    root = os.path.join(os.path.dirname(__file__), "..")
    os.chdir(root)
    env = os.environ.copy()
    env.setdefault("CPUS", "1")
    env["PMEM_RAM_FILE"] = os.path.abspath(RAM_FILE)

    # Fresh backing store for a clean first boot (no prior PMEM2 magic).
    if os.path.exists(RAM_FILE):
        os.remove(RAM_FILE)
    subprocess.run(["truncate", "-s", RAM_SIZE, RAM_FILE], check=True)

    print("=== PMEM2 boot1 (plant durable magic) ===", flush=True)
    out1 = run_boot(env, expect_pmem2=False)
    if "ONBOX_PMFS_PMEM0_PASS mode=MAP" not in out1:
        print("contract-pmem2: boot1 missing mode=MAP", file=sys.stderr)
        return 1
    if "ONBOX_PMFS_PMEM1_PASS" not in out1:
        print("contract-pmem2: boot1 missing PMEM1", file=sys.stderr)
        return 1
    if "ONBOX_PMFS_PASS" not in out1:
        print("contract-pmem2: boot1 missing ONBOX_PMFS_PASS", file=sys.stderr)
        return 1
    # First boot should NOT yet claim survived reboot (magic just planted).
    # (If somehow survived from truncate=0, still OK if boot2 passes.)

    print("=== PMEM2 boot2 (cold reboot, same file) ===", flush=True)
    out2 = run_boot(env, expect_pmem2=True)
    if "ONBOX_PMFS_PMEM2_PASS" not in out2:
        print("contract-pmem2: MISSING ONBOX_PMFS_PMEM2_PASS", file=sys.stderr)
        return 1
    if "ONBOX_PMFS_PMEM1_PASS" not in out2:
        print("contract-pmem2: boot2 missing PMEM1", file=sys.stderr)
        return 1
    print("ONBOX_PMFS_PMEM2_CONTRACT_PASS boots=2")
    print("contract-pmem2: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
