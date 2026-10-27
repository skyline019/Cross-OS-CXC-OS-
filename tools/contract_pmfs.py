#!/usr/bin/env python3
"""Launch QEMU and assert ONBOX_PMFS_* serial contract (strict peer stages)."""
import argparse
import os
import re
import signal
import subprocess
import sys
import time

# Peer-level stages: core + CDC/STREAM + POS/NEG/CHAOS + ablation + gate.
REQUIRED = [
    r"ONBOX_PMFS_PMEM0_PASS",
    r"ONBOX_PMFS_MOUNT_PASS",
    r"ONBOX_PMFS_STORE_PASS",
    r"ONBOX_PMFS_INDEX_PASS",
    r"ONBOX_PMFS_ALGO_PASS",
    r"ONBOX_PMFS_CRASH_PASS",
    r"ONBOX_PMFS_PERSIST_PASS",
    r"ONBOX_PMFS_FILE_PASS",
    r"ONBOX_PMFS_CDC_PASS",
    r"ONBOX_PMFS_STREAM_PASS",
    r"ONBOX_PMFS_POS_FILE_PASS",
    r"ONBOX_PMFS_NEG_FILE_PASS",
    r"ONBOX_PMFS_CHAOS_FILE_PASS",
    r"ONBOX_PMFS_ABLATION_PASS",
    r"ONBOX_PMFS_PASS",
]

STRICT_EXTRA = [
    r"ONBOX_PMFS_POS_CDC_PASS",
    r"ONBOX_PMFS_NEG_CDC_PASS",
    r"ONBOX_PMFS_POS_STREAM_PASS",
    r"ONBOX_PMFS_NEG_STREAM_PASS",
    r"ONBOX_PMFS_EDGE_FILE_PASS",
    r"ONBOX_PMFS_ABLATION_A0_PASS",
    r"ONBOX_PMFS_ABLATION_A4_PASS",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pmem", action="store_true", help="use make qemu-pmem (-m 160M)")
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--probe", action="store_true", help="also run pmfs_probe in shell")
    ap.add_argument("--bench", action="store_true", help="also run pmfs_bench in shell")
    ap.add_argument("--posix", action="store_true", help="also run pmfs_posix shim")
    ap.add_argument("--file", action="store_true", help="also run pmfs_file")
    ap.add_argument("--strict", action="store_true", help="require POS/NEG/EDGE extras")
    args = ap.parse_args()

    os.chdir(os.path.join(os.path.dirname(__file__), ".."))
    target = "qemu-pmem-run" if args.pmem else "qemu"
    env = os.environ.copy()
    env.setdefault("CPUS", "1")

    proc = subprocess.Popen(
        ["make", target],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )
    os.set_blocking(proc.stdout.fileno(), False)
    out = ""
    deadline = time.time() + args.timeout
    saw_gate = False
    shell_ready = False
    need_user = args.probe or args.bench or args.posix or args.file

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
                if "ONBOX_PMFS_PASS" in out:
                    saw_gate = True
                if args.pmem:
                    if "ONBOX_PMFS_PMEM1_FAIL" in out and "ONBOX_PMFS_PMEM1_PASS" not in out:
                        print("contract: PMEM1 failed", file=sys.stderr)
                        return 1
                if "init: starting sh" in out or "$ " in out:
                    shell_ready = True
                if saw_gate and not need_user:
                    break
                if saw_gate and shell_ready and need_user:
                    cmds = []
                    if args.probe:
                        cmds.append(b"pmfs_probe\n")
                    if args.posix:
                        cmds.append(b"pmfs_posix\n")
                    if args.file:
                        cmds.append(b"pmfs_file\n")
                    if args.bench:
                        cmds.append(b"pmfs_bench 32\n")
                    for c in cmds:
                        proc.stdin.write(c)
                        proc.stdin.flush()
                    extra = time.time() + 90
                    while time.time() < extra:
                        try:
                            chunk = os.read(proc.stdout.fileno(), 4096)
                        except BlockingIOError:
                            chunk = b""
                            time.sleep(0.05)
                            continue
                        if chunk:
                            text = chunk.decode("utf-8", errors="replace")
                            sys.stdout.write(text)
                            sys.stdout.flush()
                            out += text
                        probe_ok = (not args.probe) or ("ONBOX_PMFS_SYSCALL_PASS" in out)
                        posix_ok = (not args.posix) or ("ONBOX_PMFS_POSIX_PASS" in out)
                        file_ok = (not args.file) or ("ONBOX_PMFS_FILEUSER_PASS" in out)
                        bench_ok = (not args.bench) or (
                            "ONBOX_PMFS_BENCH_PASS" in out
                            and "median_xv6=" in out
                            and "phys=" in out
                        )
                        if probe_ok and posix_ok and file_ok and bench_ok:
                            time.sleep(0.3)
                            break
                    break
            else:
                time.sleep(0.05)
            if proc.poll() is not None and not saw_gate:
                break
    finally:
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
        except Exception:
            pass

    missing = [pat for pat in REQUIRED if not re.search(pat, out)]
    if missing:
        print("contract: MISSING", missing, file=sys.stderr)
        return 1
    if args.strict:
        miss2 = [pat for pat in STRICT_EXTRA if not re.search(pat, out)]
        if miss2:
            print("contract-strict: MISSING", miss2, file=sys.stderr)
            return 1
    if args.pmem:
        if not re.search(r"ONBOX_PMFS_PMEM0_PASS mode=MAP", out):
            print("contract: expected mode=MAP", file=sys.stderr)
            return 1
        if "ONBOX_PMFS_PMEM1_PASS" not in out:
            print("contract: missing PMEM1_PASS", file=sys.stderr)
            return 1
    if args.probe and "ONBOX_PMFS_SYSCALL_PASS" not in out:
        print("contract: missing SYSCALL_PASS", file=sys.stderr)
        return 1
    if args.file and "ONBOX_PMFS_FILEUSER_PASS" not in out:
        print("contract: missing FILEUSER_PASS", file=sys.stderr)
        return 1
    if args.bench and (
        "ONBOX_PMFS_BENCH_PASS" not in out or "median_xv6=" not in out
    ):
        print("contract: missing complete BENCH_PASS line", file=sys.stderr)
        return 1
    if args.posix and "ONBOX_PMFS_POSIX_PASS" not in out:
        print("contract: missing POSIX_PASS", file=sys.stderr)
        return 1
    print("contract: OK" + (" (strict)" if args.strict else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
