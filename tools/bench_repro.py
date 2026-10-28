#!/usr/bin/env python3
"""Repeat matched-count EMUL workloads across fresh QEMU processes.

Requires an already-built default EMUL kernel and fs.img. Each boot gets a
private copy of the input disk; neither the input image nor the kernel is
modified. Saves raw trials and serial output, binary hashes, and descriptive
bootstrap intervals. PMFS and xv6 have different persistence contracts;
results are QEMU software-path measurements, not a physical-media comparison.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import re
import selectors
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
TRIAL_RE = re.compile(
    r"^BENCH_TRIAL N=(\d+) t=(\d+) kv=(\d+) file=(\d+) xv6=(\d+) "
    r"count_pmfs=(\d+) count_xv6=(\d+)\r?$", re.MULTILINE)
SUMMARY_RE = re.compile(
    r"^ONBOX_PMFS_BENCH_PASS trials=5 N=(\d+) [^\r\n]* "
    r"count_pmfs=(\d+) count_xv6=(\d+) units=r_time_ticks\r?$", re.MULTILINE)
PROMPT_RE = re.compile(rb"(?:^|\n)\$ $", re.MULTILINE)


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_trials(text, n):
    if "ONBOX_PMFS_BENCH_FAIL" in text:
        raise ValueError("guest benchmark reported failure")
    summaries = list(SUMMARY_RE.finditer(text))
    if (len(re.findall(r"^ONBOX_PMFS_BENCH_PASS\b", text, re.MULTILINE)) != 1 or
            len(summaries) != 1 or tuple(map(int, summaries[0].groups())) != (n, n, n)):
        raise ValueError("missing or invalid benchmark summary")
    if len(re.findall(r"^BENCH_TRIAL\b", text, re.MULTILINE)) != 5:
        raise ValueError("expected exactly five trial lines")
    rows = []
    for match in TRIAL_RE.finditer(text):
        size, trial, kv, pmfs, xv6, pm_count, xv6_count = map(int, match.groups())
        if (size, pm_count, xv6_count) != (n, n, n) or min(kv, pmfs, xv6) <= 0:
            raise ValueError("invalid trial count or elapsed ticks")
        rows.append({"trial": trial, "n": n, "kv_ticks": kv,
                     "pmfs_ticks": pmfs, "xv6_ticks": xv6,
                     "count_pmfs": pm_count, "count_xv6": xv6_count})
    if len(rows) != 5 or sorted(row["trial"] for row in rows) != list(range(5)):
        raise ValueError("expected exactly five distinct trial rows")
    return sorted(rows, key=lambda row: row["trial"])


def percentile(values, fraction):
    values = sorted(values)
    pos = (len(values) - 1) * fraction
    lower = int(pos)
    upper = min(lower + 1, len(values) - 1)
    return values[lower] + (values[upper] - values[lower]) * (pos - lower)


def summarize(records, seed=20261028, samples=2000):
    runs = sorted({r["run"] for r in records if not r["warmup"]})
    if not runs or samples < 1:
        raise ValueError("no measured runs or no bootstrap samples")
    by_run = [[r for r in records if not r["warmup"] and r["run"] == run]
              for run in runs]
    if any(len(group) != 5 or sorted(r["trial"] for r in group) != list(range(5))
           for group in by_run):
        raise ValueError("every measured boot must have five distinct trials")
    if len({r["n"] for group in by_run for r in group}) != 1:
        raise ValueError("cannot pool workloads of different sizes")
    result = {"measured_boots": len(runs), "trials_per_boot": 5,
              "n_per_trial": by_run[0][0]["n"], "unit": "r_time_ticks_per_trial",
              "method": "median of five trials per boot; median across boots; "
                        "percentile bootstrap resampling whole boots",
              "bootstrap_seed": seed, "bootstrap_samples": samples,
              "warning": "TCG software observations; different durability contracts, "
                         "not physical PMEM performance or a causal speedup."}
    rng = random.Random(seed)
    for field in ("kv_ticks", "pmfs_ticks", "xv6_ticks"):
        medians = [statistics.median(row[field] for row in group) for group in by_run]
        bootstraps = [statistics.median(rng.choices(medians, k=len(medians)))
                      for _ in range(samples)]
        result[field] = {"boot_medians": medians,
                         "median": statistics.median(medians),
                         "bootstrap_95_percentile": [percentile(bootstraps, 0.025),
                                                       percentile(bootstraps, 0.975)]}
    if len(runs) < 10:
        result["small_sample_warning"] = "Fewer than 10 measured boots; exploratory smoke only."
    return result


def boot(qemu, kernel, disk, n, timeout, log_path):
    cmd = [qemu, "-machine", "virt", "-bios", "none", "-kernel", str(kernel),
           "-m", "128M", "-smp", "1", "-nographic",
           "-global", "virtio-mmio.force-legacy=false",
           "-drive", f"file={disk},if=none,format=raw,id=x0",
           "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, start_new_session=True)
    output = bytearray()
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    sent = False
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.2):
                chunk = os.read(key.fd, 8192)
                if chunk:
                    output.extend(chunk)
            if b"panic:" in output or b"ONBOX_PMFS_BENCH_FAIL" in output:
                raise RuntimeError("guest reported failure")
            if not sent and b"init: starting sh" in output and PROMPT_RE.search(output):
                if b"ONBOX_PMFS_PMEM0_PASS mode=EMUL backend=emul" not in output:
                    raise RuntimeError("kernel is not the default EMUL backend")
                if b"ONBOX_PMFS_STORE_PASS" in output:
                    raise RuntimeError("destructive PMFS selftests are enabled")
                proc.stdin.write(f"pmfs_bench {n}\n".encode())
                proc.stdin.flush()
                sent = True
            text = output.decode("utf-8", "replace")
            summary = SUMMARY_RE.search(text)
            if sent and summary and PROMPT_RE.search(output, output.find(b"ONBOX_PMFS_BENCH_PASS")):
                return parse_trials(text, n)
            if proc.poll() is not None:
                raise RuntimeError(f"QEMU exited {proc.returncode}")
        raise RuntimeError("timed out waiting for benchmark and shell prompt")
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)
        selector.close()
        proc.stdin.close()
        proc.stdout.close()
        log_path.write_bytes(output)


def git_value(*args):
    proc = subprocess.run(["git", "-C", str(ROOT), *args], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return proc.stdout.strip() if proc.returncode == 0 else None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True,
                        help="new output directory; never overwrites an existing one")
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-riscv64"))
    parser.add_argument("--runs", type=int, default=10, help="measured QEMU boots")
    parser.add_argument("--warmup", type=int, default=1, help="additional unmeasured boots")
    parser.add_argument("--n", type=int, default=8, help="transactions per trial, 1..32")
    parser.add_argument("--timeout", type=int, default=180, help="seconds per boot")
    parser.add_argument("--seed", type=int, default=20261028, help="bootstrap seed")
    parser.add_argument("--build-notes", default="not recorded",
                        help="toolchain and flags used for the input binaries")
    args = parser.parse_args(argv)
    if not 1 <= args.n <= 32 or not 1 <= args.runs <= 1000 or not 0 <= args.warmup <= 100:
        parser.error("n must be 1..32, runs 1..1000 and warmup 0..100")
    if args.timeout < 1:
        parser.error("timeout must be positive")
    qemu = shutil.which(args.qemu)
    kernel, image = ROOT / "kernel/kernel", ROOT / "fs.img"
    if not qemu or not kernel.is_file() or not image.is_file():
        parser.error("QEMU, kernel/kernel and fs.img must exist")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    version = subprocess.run([qemu, "--version"], capture_output=True, text=True, check=True)
    metadata = {"schema": 1, "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "git_commit": git_value("rev-parse", "HEAD"),
                "git_status_porcelain": git_value("status", "--porcelain"),
                "kernel_sha256": sha256(kernel), "input_image_sha256": sha256(image),
                "qemu_path": qemu, "qemu_version": version.stdout.splitlines()[0],
                "host": platform.platform(), "cpus": 1, "backend": "EMUL, volatile",
                "runs": args.runs, "warmup": args.warmup, "n": args.n,
                "build_notes": args.build_notes,
                "disclaimer": "No physical durability or equivalent persistence semantics."}
    (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    records = []
    with tempfile.TemporaryDirectory(prefix="pmfs-bench-") as tmp, (out / "raw.jsonl").open("w") as raw:
        for run in range(args.warmup + args.runs):
            disk = Path(tmp) / "disk.img"
            shutil.copyfile(image, disk)
            rows = boot(qemu, kernel, disk, args.n, args.timeout, out / f"boot-{run:03d}.log")
            for row in rows:
                row.update(run=run, warmup=run < args.warmup)
                records.append(row)
                raw.write(json.dumps(row, sort_keys=True) + "\n")
            raw.flush()
            print(f"BENCH_BOOT_PASS run={run} warmup={int(run < args.warmup)} trials=5", flush=True)
    if sha256(image) != metadata["input_image_sha256"] or sha256(kernel) != metadata["kernel_sha256"]:
        raise RuntimeError("input kernel or image changed during the experiment")
    result = summarize(records, seed=args.seed)
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"BENCH_REPRO_PASS boots={args.runs} warmup={args.warmup} n={args.n} "
          "unit=r_time_ticks durability_equivalent=0", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print(f"BENCH_REPRO_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
