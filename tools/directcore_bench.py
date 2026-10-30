#!/usr/bin/env python3
"""Repeated-boot QEMU microbenchmark: experimental direct core vs xv6/NVMe.

Each boot has a new private disk and blank NVDIMM, which the guest EXPLICITLY
formats via --force outside all measurements. The two paths have unequal APIs,
capabilities and durability guarantees; this is not a VFS or hardware result.
For measurements use a clean DIRECT_CORE=1 build with PMEM_ZICBOM and without
PMFS_TRACE/DIRECT_CORE_FAULT_INJECT; specify the exact build in --build-notes.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import random
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

from bench_perf import get_timebase, percentile, source_hash
from directcore_recovery import DEVICE_BYTES, Runner, sha256

SAMPLE = re.compile(r"^DIRECTBENCH_SAMPLE size=(\d+) i=(\d+) direct_ticks=(\d+) "
                    r"xv_ticks=(\d+) order=(\d+)\r?$", re.MULTILINE)
BATCH = re.compile(r"^DIRECTBENCH_BATCH size=(\d+) count=(\d+) direct_ticks=(\d+) "
                   r"xv_ticks=(\d+) order=(\d+)\r?$", re.MULTILINE)
DONE = re.compile(r"^DIRECTBENCH_PASS size=(\d+) samples=(\d+) warmup=4\r?$", re.MULTILINE)


def parse_output(text, size, count, order):
    if re.search(r"^DIRECTBENCH_FAIL\b", text, re.MULTILINE):
        raise ValueError("guest benchmark failed")
    if DONE.findall(text) != [(str(size), str(count))]:
        raise ValueError("missing, duplicate or mismatched completion record")
    rows = []
    for m in SAMPLE.finditer(text):
        actual_size, index, direct, xv, actual_order = map(int, m.groups())
        if actual_size != size or direct <= 0 or xv <= 0 or actual_order != index % 2:
            raise ValueError("invalid sample")
        rows.append({"i": index, "direct_ticks": direct, "xv_ticks": xv, "order": actual_order})
    if sorted(row["i"] for row in rows) != list(range(count)):
        raise ValueError("missing or duplicate samples")
    matches = list(BATCH.finditer(text))
    if len(matches) != 1:
        raise ValueError("missing or duplicate batch")
    actual_size, n, direct, xv, actual_order = map(int, matches[0].groups())
    if (actual_size != size or n != count or direct <= 0 or xv <= 0 or actual_order != order):
        raise ValueError("invalid batch")
    batch = {"count": n, "direct_ticks": direct, "xv_ticks": xv, "order": actual_order}
    return sorted(rows, key=lambda x: x["i"]), batch


def summarize(rows, batches, frequency, seed=20261030, repetitions=2000):
    if frequency <= 0 or repetitions < 1:
        raise ValueError("invalid frequency or bootstrap count")
    results = {}
    for size in sorted({r["size"] for r in rows if not r["warmup"]}):
        group = [r for r in rows if r["size"] == size and not r["warmup"]]
        run_ids = sorted({r["run"] for r in group})
        boots = [[r for r in group if r["run"] == run] for run in run_ids]
        selected_batches = [b for b in batches if b["size"] == size and not b["warmup"]]
        if (not boots or len(selected_batches) != len(boots) or
                sorted(b["run"] for b in selected_batches) != run_ids):
            raise ValueError("missing or duplicate batch boots")
        selected_batches.sort(key=lambda b: b["run"])
        entry = {"size_bytes": size, "measured_boots": len(boots),
                 "latency_samples_per_backend": len(group),
                 "batch_operations_per_backend": sum(b["count"] for b in selected_batches)}
        for name, field in (("direct_core", "direct_ticks"), ("xv6_nvme", "xv_ticks")):
            rng = random.Random(seed + size + (0 if name == "direct_core" else 10000))
            ticks = [r[field] for r in group]
            boot_medians = [statistics.median(r[field] for r in boot) for boot in boots]
            boot_rates = [frequency * b["count"] / b[field] for b in selected_batches]
            aggregate_rate = frequency * sum(b["count"] for b in selected_batches) / sum(
                b[field] for b in selected_batches)
            medians, rates = [], []
            for _ in range(repetitions):
                indices = rng.choices(range(len(boots)), k=len(boots))
                medians.append(statistics.median(boot_medians[i] for i in indices))
                rates.append(frequency * sum(selected_batches[i]["count"] for i in indices) /
                             sum(selected_batches[i][field] for i in indices))
            entry[name] = {
                "latency_p50_us_pooled": percentile(ticks, 0.50) * 1e6 / frequency,
                "latency_p95_us_pooled": percentile(ticks, 0.95) * 1e6 / frequency,
                "median_boot_p50_us": statistics.median(boot_medians) * 1e6 / frequency,
                "median_boot_p50_us_bootstrap95": [percentile(medians, 0.025) * 1e6 / frequency,
                                                     percentile(medians, 0.975) * 1e6 / frequency],
                "batch_transactions_per_guest_second": aggregate_rate,
                "batch_throughput_bootstrap95": [percentile(rates, 0.025), percentile(rates, 0.975)],
                "batch_throughput_per_boot": boot_rates,
            }
        if len(boots) < 10:
            entry["warning"] = "Fewer than 10 independent measured boots; exploratory only."
        results[str(size)] = entry
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--samples", type=int, default=32)
    parser.add_argument("--sizes", default="64,512")
    parser.add_argument("--zicbom", action="store_true")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--seed", type=int, default=20261030)
    parser.add_argument("--build-notes", required=True)
    args = parser.parse_args()
    try:
        sizes = [int(x) for x in args.sizes.split(",")]
    except ValueError:
        parser.error("invalid size list")
    if (not sizes or len(set(sizes)) != len(sizes) or any(s not in (64, 512) for s in sizes) or
            not 1 <= args.runs <= 1000 or not 0 <= args.warmup <= 100 or
            not 1 <= args.samples <= 56 or args.timeout < 1):
        parser.error("invalid run, sample, size or timeout")
    qemu = shutil.which(args.qemu)
    root = args.root.resolve()
    kernel, disk = root / "kernel/kernel", root / "fs.img"
    if not qemu or not kernel.is_file() or not disk.is_file():
        parser.error("need QEMU and built kernel/kernel and fs.img")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    logs = output / "serial"
    logs.mkdir()
    version = subprocess.run([qemu, "--version"], capture_output=True, text=True, check=True, timeout=30)
    original = (sha256(kernel), sha256(disk))
    metadata = {
        "schema": 1, "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "kernel_sha256": original[0], "fs_image_sha256": original[1],
        "source_sha256": source_hash(root), "runner_sha256": sha256(Path(__file__)),
        "qemu_sha256": sha256(Path(qemu)), "qemu_version": version.stdout.splitlines()[0],
        "host": platform.platform(), "cpus": 1, "disk": "QEMU NVMe, file backend",
        "pmem": "QEMU NVDIMM, fresh file backend; explicit destructive format",
        "zicbom": args.zicbom, "runs": args.runs, "warmup_boots": args.warmup,
        "samples_per_boot": args.samples, "sizes": sizes, "seed": args.seed,
        "build_notes": args.build_notes,
        "direct_operation": "whole-file replacement via test syscall; COW root publish and drain",
        "xv6_operation": "pre-opened existing file: lseek + write + fsync (global log sync, NVMe Flush)",
        "warning": "Unequal API/capability/durability contracts; QEMU guest time is not physical device performance.",
    }
    rows, batches, failures = [], [], []
    with tempfile.TemporaryDirectory(prefix="directcore-bench-") as temporary, \
            (output / "raw.jsonl").open("w") as raw, (output / "batches.jsonl").open("w") as batch_file:
        tmp = Path(temporary)
        frequency, dtb_command = get_timebase(qemu, tmp)
        metadata["timebase_hz"] = frequency
        metadata["timebase_dtb_sha256"] = sha256(tmp / "qemu-virt.dtb")
        metadata["timebase_dtb_command"] = dtb_command
        (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        runner = Runner(root, qemu, tmp, "nvme", 1, args.timeout, logs, args.zicbom)
        rng = random.Random(args.seed)
        for run in range(args.warmup + args.runs):
            tasks = list(sizes)
            rng.shuffle(tasks)
            for size in tasks:
                label = f"run-{run:03d}-size-{size}"
                media = tmp / "nvdimm.img"
                with media.open("wb") as f:
                    f.truncate(DEVICE_BYTES)
                start = time.monotonic()
                try:
                    command = f"directbench --force {args.samples} {size} {run % 2}"
                    marker = (rb"^DIRECTBENCH_PASS size=" + str(size).encode() + rb" samples=" +
                              str(args.samples).encode() + rb" warmup=4\r?\n")
                    serial = runner.boot(media, label, command, marker, failure_marker=b"DIRECTBENCH_FAIL")
                    samples, batch = parse_output(serial.decode("utf-8", "replace"), size, args.samples, run % 2)
                    for row in samples:
                        row.update(run=run, warmup=run < args.warmup, size=size, status="success")
                        rows.append(row)
                        raw.write(json.dumps(row, sort_keys=True) + "\n")
                    batch.update(run=run, warmup=run < args.warmup, size=size, status="success",
                                 boot_wall_seconds=time.monotonic() - start)
                    batches.append(batch)
                    batch_file.write(json.dumps(batch, sort_keys=True) + "\n")
                    print(f"DIRECTBENCH_BOOT_PASS run={run} size={size} samples={len(samples)}", flush=True)
                except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
                    failure = {"run": run, "warmup": run < args.warmup, "size": size,
                               "status": "failure", "error": str(exc)}
                    failures.append(failure)
                    raw.write(json.dumps(failure, sort_keys=True) + "\n")
                    print(f"DIRECTBENCH_BOOT_FAIL {label}: {exc}", file=sys.stderr, flush=True)
                finally:
                    media.unlink(missing_ok=True)
                    raw.flush()
                    batch_file.flush()
        if (sha256(kernel), sha256(disk)) != original:
            raise RuntimeError("input kernel or root image changed")
        summary = {"method": "Latency: alternating paired writes; per-sample readback excluded from timing. "
                             "Batch: full completed write loop including pattern generation; readback excluded. "
                             "Intervals bootstrap independent boot groups, not correlated in-boot samples.",
                   "frequency_hz": frequency, "bootstrap_seed": args.seed,
                   "bootstrap_samples": 2000, "durability_equivalent": False,
                   "failures": failures, "results": summarize(rows, batches, frequency, seed=args.seed) if rows else {}}
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    if failures:
        print(f"DIRECTBENCH_EXPERIMENT_FAIL boots={len(failures)}", file=sys.stderr, flush=True)
        return 1
    print(f"DIRECTBENCH_EXPERIMENT_PASS groups={len(sizes)} measured_boots_per_group={args.runs} "
          f"frequency_hz={frequency} durability_equivalent=0", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print(f"DIRECTBENCH_EXPERIMENT_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
