#!/usr/bin/env python3
"""Paired same-contract ablation of PMFS GC reachability-cache hints.

Build two disposable checkouts from identical source: a default EMUL kernel
and one with -DPMFS_GC_NO_HINTS. Both still protect BOTH checkpoints; only
propagation of already-verified reachability hints is disabled in the control.
Each pair uses the same workload and fsync, alternates build order, checks the
runtime build marker, and receives private fresh disk images. QEMU time and
software-request counters are not physical PMEM performance measurements.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import random
import shutil
import statistics
import subprocess
import sys
import tempfile

from bench_perf import (MODES, SIZES, boot, get_timebase, percentile, sha256, source_hash,
                        summarize, summarize_batches)


def paired_summary(rows, batches, seed, repetitions=2000):
    rng, result = random.Random(seed), {}
    groups = sorted({(b["mode"], b["size"]) for b in batches if not b["warmup"]})
    for mode, size in groups:
        selected = [b for b in batches if b["mode"] == mode and b["size"] == size and
                    not b["warmup"]]
        runs = sorted({b["run"] for b in selected})
        pairs = []
        for run in runs:
            arms = {b["arm"]: b for b in selected if b["run"] == run}
            if set(arms) != {"enabled", "disabled"}:
                continue
            medians = {}
            for arm in arms:
                times = [r["pm_ticks"] for r in rows if r["mode"] == mode and
                         r["size"] == size and r["run"] == run and r["arm"] == arm and
                         not r["warmup"]]
                if not times:
                    raise ValueError("missing paired latency samples")
                medians[arm] = statistics.median(times)
            pairs.append({"run": run, "enabled_ticks": arms["enabled"]["pm_ticks"],
                          "disabled_ticks": arms["disabled"]["pm_ticks"],
                          "count": arms["enabled"]["count"],
                          "latency_ratio": medians["enabled"] / medians["disabled"]})
            if arms["enabled"]["count"] != arms["disabled"]["count"]:
                raise ValueError("unmatched batch operation counts")
        if not pairs:
            continue
        def throughput_ratio(chosen):
            return sum(p["disabled_ticks"] for p in chosen) / sum(p["enabled_ticks"] for p in chosen)
        rate_ratios, latency_ratios = [], []
        for _ in range(repetitions):
            chosen = rng.choices(pairs, k=len(pairs))
            rate_ratios.append(throughput_ratio(chosen))
            latency_ratios.append(statistics.median(p["latency_ratio"] for p in chosen))
        result[f"{mode}:{size}"] = {
            "complete_pairs": len(pairs),
            "batch_throughput_enabled_over_disabled": throughput_ratio(pairs),
            "batch_throughput_ratio_bootstrap95": [percentile(rate_ratios, 0.025),
                                                   percentile(rate_ratios, 0.975)],
            "median_per_boot_p50_latency_enabled_over_disabled":
                statistics.median(p["latency_ratio"] for p in pairs),
            "median_latency_ratio_bootstrap95": [percentile(latency_ratios, 0.025),
                                                  percentile(latency_ratios, 0.975)],
            "pairs": pairs,
        }
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enabled-root", type=Path, required=True)
    parser.add_argument("--disabled-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--samples", type=int, default=32)
    parser.add_argument("--sizes", default="64,1024")
    parser.add_argument("--modes", default="overwrite")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--seed", type=int, default=20261028)
    parser.add_argument("--build-notes", default="not recorded")
    args = parser.parse_args(argv)
    try:
        sizes = [int(v) for v in args.sizes.split(",")]
    except ValueError:
        parser.error("invalid sizes")
    modes = args.modes.split(",")
    if (not 1 <= args.runs <= 1000 or not 0 <= args.warmup <= 100 or
            not 1 <= args.samples <= 56 or args.timeout < 1 or
            not sizes or len(set(sizes)) != len(sizes) or any(v not in SIZES for v in sizes) or
            not modes or len(set(modes)) != len(modes) or any(v not in MODES for v in modes)):
        parser.error("invalid runs, warmup, samples, timeout, sizes or modes")
    qemu = shutil.which(args.qemu)
    roots = {"enabled": args.enabled_root.resolve(), "disabled": args.disabled_root.resolve()}
    if not qemu or any(not (root / "kernel/kernel").is_file() or not (root / "fs.img").is_file()
                       for root in roots.values()):
        parser.error("QEMU and both built kernels/fs.img files must exist")
    hashes = {name: {"source_sha256": source_hash(root), "kernel_sha256": sha256(root / "kernel/kernel"),
                     "image_sha256": sha256(root / "fs.img")} for name, root in roots.items()}
    if hashes["enabled"]["source_sha256"] != hashes["disabled"]["source_sha256"]:
        raise ValueError("ablation source trees differ")
    if hashes["enabled"]["image_sha256"] != hashes["disabled"]["image_sha256"]:
        raise ValueError("ablation guest filesystem images differ")
    if hashes["enabled"]["kernel_sha256"] == hashes["disabled"]["kernel_sha256"]:
        raise ValueError("ablation kernels unexpectedly identical")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    version = subprocess.run([qemu, "--version"], capture_output=True, text=True, check=True)
    metadata = {"schema": 1, "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "arms": hashes, "qemu_sha256": sha256(Path(qemu)),
                "qemu_version": version.stdout.splitlines()[0], "host": platform.platform(),
                "backend": "volatile EMUL", "same_pmfs_sync_contract": True,
                "control_flag": "PMFS_GC_NO_HINTS", "build_notes": args.build_notes,
                "runs": args.runs, "warmup_boots": args.warmup, "samples": args.samples,
                "sizes": sizes, "modes": modes, "seed": args.seed,
                "warning": "same PMFS semantics, QEMU guest time and software requests only; "
                           "not physical persistent memory or physical endurance"}
    (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    rows, batches, failures = [], [], []
    rng = random.Random(args.seed)
    with tempfile.TemporaryDirectory(prefix="pmfs-gc-ablation-") as temporary, \
            (out / "raw.jsonl").open("w") as raw, (out / "batches.jsonl").open("w") as batch_file:
        directory = Path(temporary)
        frequency, dtb_command = get_timebase(qemu, directory)
        metadata.update(timebase_frequency_hz=frequency,
                        timebase_source="QEMU virt generated DTB /cpus/timebase-frequency",
                        dtb_sha256=sha256(directory / "qemu-virt.dtb"), dtb_command=dtb_command)
        (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        for run in range(args.warmup + args.runs):
            tasks = [(mode, size) for mode in modes for size in sizes]
            rng.shuffle(tasks)
            for index, (mode, size) in enumerate(tasks):
                arms = ["enabled", "disabled"] if (run + index) % 2 == 0 else ["disabled", "enabled"]
                for arm in arms:
                    root, disk = roots[arm], directory / "disk.img"
                    shutil.copyfile(root / "fs.img", disk)
                    label = f"{run:03d}-{mode}-{size}-{arm}"
                    try:
                        samples, batch, wall = boot(qemu, root / "kernel/kernel", disk,
                                                    mode, size, args.samples, args.timeout,
                                                    out / f"{label}.log", run % 2, arm)
                        common = {"arm": arm, "run": run, "warmup": run < args.warmup,
                                  "mode": mode, "size": size, "pair_order": arms.index(arm)}
                        for row in samples:
                            row.update(common)
                            rows.append(row)
                            raw.write(json.dumps(row, sort_keys=True) + "\n")
                        batch.update(common)
                        batches.append(batch)
                        batch_file.write(json.dumps(batch, sort_keys=True) + "\n")
                        print(f"GC_ABLATION_BOOT_PASS {label} samples={len(samples)} "
                              f"wall_seconds={wall:.3f}", flush=True)
                    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
                        failure = {"status": "failure", "arm": arm, "run": run,
                                   "mode": mode, "size": size, "error": str(exc)}
                        failures.append(failure)
                        raw.write(json.dumps(failure, sort_keys=True) + "\n")
                        print(f"GC_ABLATION_BOOT_FAIL {label}: {exc}", file=sys.stderr, flush=True)
                    raw.flush()
                    batch_file.flush()
        result = {"same_pmfs_sync_contract": True, "frequency_hz": frequency,
                  "failures": failures, "arms": {},
                  "paired": paired_summary(rows, batches, args.seed) if batches else {}}
        for arm in roots:
            arm_rows = [r for r in rows if r["arm"] == arm]
            arm_batches = [b for b in batches if b["arm"] == arm]
            result["arms"][arm] = {
                "latency": summarize(arm_rows, frequency, seed=args.seed) if arm_rows else {},
                "batch_throughput": summarize_batches(arm_batches, frequency, seed=args.seed)
                                    if arm_batches else {},
            }
        (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    for arm, root in roots.items():
        if (sha256(root / "kernel/kernel") != hashes[arm]["kernel_sha256"] or
                sha256(root / "fs.img") != hashes[arm]["image_sha256"]):
            raise RuntimeError("input build changed during paired experiment")
    if failures:
        print(f"GC_ABLATION_FAIL failed_boots={len(failures)}", file=sys.stderr)
        return 1
    print(f"GC_ABLATION_PASS groups={len(modes) * len(sizes)} "
          f"measured_pairs_per_group={args.runs} frequency_hz={frequency}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print(f"GC_ABLATION_FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
