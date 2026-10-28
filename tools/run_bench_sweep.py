#!/usr/bin/env python3
"""Run matched-count xv6/PMFS smoke workloads; print raw ticks, not conclusions.

Rebuilds kernels and fs.img, so run only in a disposable checkout. MAP is
ordinary QEMU RAM, not a persistent-memory/NVDIMM device. No results or
historical tables are overwritten. The two backends have different durability
contracts; the printed ratios are not a physical-media performance claim.
"""
import argparse
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import sys
import time

TIMEOUT = 180


def run_one(env, target, n, mode):
    proc = subprocess.Popen(["make", target], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env, start_new_session=True)
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    output = bytearray()
    deadline = time.monotonic() + TIMEOUT
    sent_format = sent_bench = False
    last_prompt = 0
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.2):
                chunk = os.read(key.fd, 8192)
                if chunk:
                    output.extend(chunk)
                    sys.stdout.write(chunk.decode("utf-8", "replace"))
                    sys.stdout.flush()
            if b"panic:" in output or b"ONBOX_PMFS_BENCH_FAIL" in output:
                raise RuntimeError(f"{mode} N={n}: guest reported failure")
            prompts = output.count(b"$ ")
            if prompts > last_prompt:
                last_prompt = prompts
                if mode == "MAP" and not sent_format:
                    proc.stdin.write(b"pmformat --force\n")
                    proc.stdin.flush()
                    sent_format = True
                elif not sent_bench:
                    if mode == "MAP" and b"pmformat: v3 ready" not in output:
                        raise RuntimeError("MAP initialization failed")
                    proc.stdin.write(f"pmfs_bench {n}\n".encode())
                    proc.stdin.flush()
                    sent_bench = True
            summary = output.find(b"ONBOX_PMFS_BENCH_PASS")
            if sent_bench and summary >= 0 and output.find(b"\n", summary) >= 0:
                break
            if proc.poll() is not None:
                raise RuntimeError(f"{mode} N={n}: QEMU exited {proc.returncode}")
        else:
            raise RuntimeError(f"{mode} N={n}: timed out")
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)
        selector.close()
        proc.stdin.close()
        proc.stdout.close()
    text = output.decode("utf-8", "replace")
    trials = re.findall(rf"BENCH_TRIAL N={n} t=(\d+) kv=(\d+) file=(\d+) xv6=(\d+) "
                        rf"count_pmfs={n} count_xv6={n}", text)
    if len(trials) != 5 or sorted(int(t[0]) for t in trials) != list(range(5)):
        raise RuntimeError(f"{mode} N={n}: missing trial or unequal operation counts")
    if not re.search(rf"ONBOX_PMFS_BENCH_PASS trials=5 N={n} .*"
                     rf"count_pmfs={n} count_xv6={n} units=r_time_ticks", text):
        raise RuntimeError(f"{mode} N={n}: missing validated summary")
    return trials


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", default="8,16,32", help="comma-separated N values, each 1..32")
    parser.add_argument("--emul-only", action="store_true")
    args = parser.parse_args(argv)
    try:
        sizes = [int(s) for s in args.sizes.split(",")]
    except ValueError:
        parser.error("sizes must be integers")
    if not sizes or any(n < 1 or n > 32 for n in sizes):
        parser.error("sizes must be in 1..32")
    os.chdir(Path(__file__).resolve().parent.parent)
    env = os.environ.copy()
    env["CPUS"] = "1"
    subprocess.run(["make", "kernel/kernel", "fs.img"], check=True, env=env)
    for n in sizes:
        print(f"=== EMUL N={n} ===", flush=True)
        run_one(env, "qemu", n, "EMUL")
    if not args.emul_only:
        try:
            subprocess.run(["make", "kernel-pmem"], check=True, env=env)
            print("=== MAP N=32 (ordinary RAM, explicitly formatted) ===", flush=True)
            run_one(env, "qemu-pmem-run", 32, "MAP")
        finally:
            subprocess.run(["make", "kernel-emul"], check=True, env=env)
    print("BENCH_SWEEP_PASS raw_ticks=1 matched_counts=1 durability_equivalent=0", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"BENCH_SWEEP_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
