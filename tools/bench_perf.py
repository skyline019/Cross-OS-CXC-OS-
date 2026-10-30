#!/usr/bin/env python3
"""Matched-count file latency/throughput on fresh QEMU boots.

Build the selected kernel/fs.img first, in a disposable checkout. Each boot
gets a private copy of fs.img and, when requested, a fresh file-backed NVDIMM;
the input kernel and disk are checked unchanged. The default is volatile EMUL
with virtio; --nvdimm --backend nvme requires a patched QEMU and a kernel built
with PMEM_MAP, PMEM_NVDIMM, and DISK=nvme. The blank NVDIMM is explicitly
formatted outside the timed workload. PMFS and native xv6 call fsync but DO NOT
have equivalent durability contracts. Time is QEMU guest r_time ticks converted
using the generated virt DTB's `timebase-frequency`; neither guest time nor
software byte counters are physical PMEM performance. Failed runs and their
serial logs remain in the output.
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
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
SAMPLE = re.compile(
    r"^PMFS_PERF_SAMPLE mode=(roundtrip|overwrite) size=(\d+) i=(\d+) "
    r"pm_ticks=(\d+) xv_ticks=(\d+) pm_write=(\d+) pm_read=(\d+) "
    r"pm_flush=(\d+) order=(\d+)\r?$", re.MULTILINE)
BATCH = re.compile(
    r"^PMFS_PERF_BATCH mode=(roundtrip|overwrite) size=(\d+) count=(\d+) "
    r"pm_ticks=(\d+) xv_ticks=(\d+) pm_write=(\d+) pm_read=(\d+) "
    r"pm_flush=(\d+) order=([01])\r?$", re.MULTILINE)
PROMPT = re.compile(rb"(?:^|\n)\$ $", re.MULTILINE)
SIZES = (8, 64, 512, 1024)
MODES = ("roundtrip", "overwrite")


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def source_hash(root):
    h = hashlib.sha256()
    paths = [root / "Makefile"]
    for directory in ("kernel", "user"):
        paths += sorted((root / directory).glob("*.c"))
        paths += sorted((root / directory).glob("*.h"))
        paths += sorted((root / directory).glob("*.S"))
        paths += sorted((root / directory).glob("*.pl"))
    for path in sorted(set(paths)):
        h.update(str(path.relative_to(root)).encode() + b"\0")
        h.update(path.read_bytes())
    return h.hexdigest()


def dtb_timebase(data):
    """Read /cpus/timebase-frequency from a bounded flattened device tree."""
    if len(data) < 40:
        raise ValueError("short FDT header")
    header = struct.unpack_from(">10I", data)
    magic, total, off_struct, off_strings = header[:4]
    strings_len, struct_len = header[8:10]
    if (magic != 0xD00DFEED or total > len(data) or total < 40 or
            off_struct + struct_len > total or off_strings + strings_len > total):
        raise ValueError("invalid FDT bounds")
    pos, end, stack, found = off_struct, off_struct + struct_len, [], None
    while pos + 4 <= end:
        token = struct.unpack_from(">I", data, pos)[0]
        pos += 4
        if token == 1:
            stop = data.find(b"\0", pos, end)
            if stop < 0:
                raise ValueError("unterminated FDT node")
            stack.append(data[pos:stop].decode("ascii", "strict"))
            pos = (stop + 4) & ~3
        elif token == 2:
            if not stack:
                raise ValueError("unbalanced FDT node")
            stack.pop()
        elif token == 3:
            if pos + 8 > end:
                raise ValueError("short FDT property")
            size, nameoff = struct.unpack_from(">II", data, pos)
            pos += 8
            name_start = off_strings + nameoff
            name_end = data.find(b"\0", name_start, off_strings + strings_len)
            if (nameoff >= strings_len or name_end < 0 or pos + size > end):
                raise ValueError("invalid FDT property")
            name = data[name_start:name_end]
            if stack == ["", "cpus"] and name == b"timebase-frequency":
                if found is not None or size not in (4, 8):
                    raise ValueError("duplicate or malformed timebase-frequency")
                found = int.from_bytes(data[pos:pos + size], "big")
            pos = (pos + size + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            if stack:
                raise ValueError("unbalanced FDT at end")
            if not found or found > 100_000_000_000:
                raise ValueError("missing or invalid timebase-frequency")
            return found
        else:
            raise ValueError(f"unknown FDT token {token}")
    raise ValueError("missing FDT end token")


def get_timebase(qemu, directory):
    dtb = directory / "qemu-virt.dtb"
    cmd = [qemu, "-machine", f"virt,dumpdtb={dtb}", "-bios", "none",
           "-m", "128M", "-smp", "1", "-nographic"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if result.returncode or not dtb.is_file():
        raise RuntimeError("QEMU DTB dump failed: " + result.stderr[-1000:])
    return dtb_timebase(dtb.read_bytes()), cmd


def parse_samples(text, mode, size, count):
    if re.search(r"^PMFS_PERF_FAIL\b", text, re.MULTILINE):
        raise ValueError("guest reported a failed transaction")
    done = re.findall(r"^PMFS_PERF_PASS mode=(\w+) size=(\d+) samples=(\d+) warmup=4\r?$",
                      text, re.MULTILINE)
    if done != [(mode, str(size), str(count))]:
        raise ValueError("missing, duplicate or mismatched completion record")
    rows = []
    for m in SAMPLE.finditer(text):
        actual_mode = m.group(1)
        actual_size, i, pm, xv, writes, reads, flush, order = map(int, m.groups()[1:])
        if (actual_mode != mode or actual_size != size or pm <= 0 or xv <= 0 or
                order != i % 2):
            raise ValueError("invalid transaction sample")
        rows.append({"i": i, "pm_ticks": pm, "xv_ticks": xv,
                     "pm_write_bytes": writes, "pm_read_bytes": reads,
                     "pm_flush_lines": flush, "order": order})
    if sorted(row["i"] for row in rows) != list(range(count)):
        raise ValueError("missing or duplicate sample index")
    return sorted(rows, key=lambda x: x["i"])


def parse_batch(text, mode, size, count, order):
    matches = list(BATCH.finditer(text))
    if len(matches) != 1:
        raise ValueError("missing or duplicate batch record")
    actual_mode = matches[0].group(1)
    actual_size, n, pm, xv, writes, reads, flush, actual_order = map(int, matches[0].groups()[1:])
    if (actual_mode != mode or actual_size != size or n != count or
            pm <= 0 or xv <= 0 or actual_order != order):
        raise ValueError("invalid batch record")
    return {"count": n, "pm_ticks": pm, "xv_ticks": xv, "pm_write_bytes": writes,
            "pm_read_bytes": reads, "pm_flush_lines": flush, "order": actual_order}


def qemu_command(qemu, kernel, disk, root_backend="virtio", nvdimm_file=None):
    if root_backend not in ("virtio", "nvme"):
        raise ValueError("unknown root disk backend")
    machine = "virt,nvdimm=on" if nvdimm_file else "virt"
    memory = "128M,slots=1,maxmem=160M" if nvdimm_file else "128M"
    cmd = [qemu, "-machine", machine, "-bios", "none", "-kernel", str(kernel),
           "-m", memory, "-smp", "1", "-nographic",
           "-global", "virtio-mmio.force-legacy=false",
           "-drive", f"file={disk},if=none,format=raw,cache=writeback,id=x0"]
    if root_backend == "nvme":
        cmd += ["-device", "nvme,id=nvme0,serial=xv6nvme,drive=x0"]
    else:
        cmd += ["-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"]
    if nvdimm_file:
        cmd += ["-object", f"memory-backend-file,id=pmem0,share=on,"
                f"mem-path={nvdimm_file},size=32M",
                "-device", "nvdimm,id=nvdimm0,memdev=pmem0"]
    return cmd


def boot(qemu, kernel, disk, mode, size, count, timeout, log_path, batch_order,
         expected_hints=None, root_backend="virtio", nvdimm_file=None):
    cmd = qemu_command(qemu, kernel, disk, root_backend, nvdimm_file)
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, start_new_session=True)
    output, sent, initialized, format_requested = bytearray(), False, False, False
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    deadline, wall_start = time.monotonic() + timeout, None
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.2):
                chunk = os.read(key.fd, 8192)
                if chunk:
                    output.extend(chunk)
            if b"panic:" in output or b"PMFS_PERF_FAIL" in output:
                raise RuntimeError("guest reported failure or kernel panic")
            if not initialized and b"init: starting sh" in output and PROMPT.search(output):
                expected = (b"ONBOX_PMFS_PMEM0_PASS mode=MAP backend=nvdimm" if nvdimm_file
                            else b"ONBOX_PMFS_PMEM0_PASS mode=EMUL backend=emul")
                if expected not in output:
                    raise RuntimeError("kernel does not match selected PMEM backend")
                if b"ONBOX_PMFS_STORE_PASS" in output:
                    raise RuntimeError("destructive boot selftests are enabled")
                if expected_hints is not None:
                    marker = f"ONBOX_PMFS_GC_HINTS mode={expected_hints}".encode()
                    if marker not in output:
                        raise RuntimeError(f"expected GC hints {expected_hints}")
                if nvdimm_file:
                    if b"no valid v3 checkpoint" not in output:
                        raise RuntimeError("NVDIMM is not blank and unmounted")
                    proc.stdin.write(b"pmformat --force\n")
                    proc.stdin.flush()
                    format_requested = True
                initialized = True
            if format_requested and b"pmformat: failed" in output:
                raise RuntimeError("guest could not format the fresh NVDIMM")
            formatted = re.search(rb"^pmformat: v3 ready\r?$", output, re.MULTILINE)
            ready = (initialized and not format_requested) or (
                formatted is not None and PROMPT.search(output, formatted.end()))
            if ready and not sent:
                wall_start = time.monotonic()
                proc.stdin.write(f"pmfs_perf {mode} {count} {size} {batch_order}\n".encode())
                proc.stdin.flush()
                sent = True
            text = output.decode("utf-8", "replace")
            marker = re.search(r"^PMFS_PERF_PASS\b.*\r?$", text, re.MULTILINE)
            if sent and marker and PROMPT.search(output, marker.end()):
                return (parse_samples(text, mode, size, count),
                        parse_batch(text, mode, size, count, batch_order),
                        time.monotonic() - wall_start)
            if proc.poll() is not None:
                raise RuntimeError(f"QEMU exited {proc.returncode}")
        raise RuntimeError("timed out waiting for benchmark completion")
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)
        selector.close()
        proc.stdin.close()
        proc.stdout.close()
        log_path.write_bytes(output)


def percentile(values, fraction):
    if not values:
        raise ValueError("empty sample")
    values = sorted(values)
    position = (len(values) - 1) * fraction
    lo, hi = int(position), min(int(position) + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (position - lo)


def summarize(rows, frequency, seed=20261028, repetitions=2000):
    if frequency <= 0 or repetitions < 1:
        raise ValueError("invalid frequency or bootstrap count")
    result, rng = {}, random.Random(seed)
    groups = sorted({(r["mode"], r["size"]) for r in rows if not r["warmup"]})
    for mode, size in groups:
        cases = sorted({r["run"] for r in rows
                        if r["mode"] == mode and r["size"] == size and not r["warmup"]})
        boots = [[r for r in rows if r["mode"] == mode and r["size"] == size and
                  not r["warmup"] and r["run"] == run] for run in cases]
        if not boots or any(not group for group in boots):
            raise ValueError("empty boot group")
        entry = {"mode": mode, "size": size, "measured_boots": len(boots),
                 "operations": sum(len(g) for g in boots)}
        for backend, field in (("pmfs", "pm_ticks"), ("xv6", "xv_ticks")):
            all_ticks = [r[field] for group in boots for r in group]
            per_boot_medians = [statistics.median(r[field] for r in group) for group in boots]
            total_ticks = sum(all_ticks)
            bootstrap_medians, bootstrap_rates = [], []
            for _ in range(repetitions):
                selected = rng.choices(boots, k=len(boots))
                bootstrap_medians.append(statistics.median(
                    statistics.median(r[field] for r in group) for group in selected))
                bootstrap_rates.append(frequency * sum(len(g) for g in selected) /
                                       sum(r[field] for g in selected for r in g))
            entry[backend] = {
                "p50_us_pooled": percentile(all_ticks, 0.50) * 1e6 / frequency,
                "p95_us_pooled": percentile(all_ticks, 0.95) * 1e6 / frequency,
                "median_boot_p50_us": statistics.median(per_boot_medians) * 1e6 / frequency,
                "median_boot_p50_us_bootstrap95": [
                    percentile(bootstrap_medians, 0.025) * 1e6 / frequency,
                    percentile(bootstrap_medians, 0.975) * 1e6 / frequency],
                "service_transactions_per_guest_second": len(all_ticks) * frequency / total_ticks,
                "throughput_bootstrap95": [percentile(bootstrap_rates, 0.025),
                                           percentile(bootstrap_rates, 0.975)],
                "sum_ticks": total_ticks,
            }
            if len(all_ticks) >= 1000:
                entry[backend]["p99_us_pooled"] = percentile(all_ticks, 0.99) * 1e6 / frequency
        selected = [r for group in boots for r in group]
        entry["pmfs_software_requests_per_transaction"] = {
            key: sum(r[key] for r in selected) / len(selected)
            for key in ("pm_write_bytes", "pm_read_bytes", "pm_flush_lines")}
        if len(boots) < 10:
            entry["warning"] = "Fewer than 10 independent measured boots; exploratory only."
        result[f"{mode}:{size}"] = entry
    return result


def summarize_batches(batches, frequency, seed=20261028, repetitions=2000):
    if frequency <= 0 or repetitions < 1:
        raise ValueError("invalid frequency or bootstrap count")
    result, rng = {}, random.Random(seed)
    groups = sorted({(b["mode"], b["size"]) for b in batches if not b["warmup"]})
    for mode, size in groups:
        boots = [b for b in batches if b["mode"] == mode and b["size"] == size and
                 not b["warmup"]]
        entry = {"measured_boots": len(boots), "transactions": sum(b["count"] for b in boots)}
        for backend, field in (("pmfs", "pm_ticks"), ("xv6", "xv_ticks")):
            rates = [b["count"] * frequency / b[field] for b in boots]
            aggregate = frequency * sum(b["count"] for b in boots) / sum(b[field] for b in boots)
            bootstrapped = []
            for _ in range(repetitions):
                selected = rng.choices(boots, k=len(boots))
                bootstrapped.append(frequency * sum(b["count"] for b in selected) /
                                    sum(b[field] for b in selected))
            entry[backend] = {"transactions_per_guest_second": aggregate,
                              "median_boot_transactions_per_guest_second": statistics.median(rates),
                              "bootstrap95": [percentile(bootstrapped, 0.025),
                                              percentile(bootstrapped, 0.975)],
                              "per_boot_transactions_per_guest_second": rates}
        result[f"{mode}:{size}"] = entry
    return result


def git_value(*args):
    proc = subprocess.run(["git", "-C", str(ROOT), *args], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return proc.stdout.strip() if proc.returncode == 0 else None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-riscv64"))
    parser.add_argument("--backend", choices=("virtio", "nvme"), default="virtio",
                        help="root disk; the kernel must use the same DISK= setting")
    parser.add_argument("--nvdimm", action="store_true",
                        help="use a fresh 32 MiB file-backed NVDIMM instead of EMUL")
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--samples", type=int, default=32)
    parser.add_argument("--sizes", default="8,64,512,1024")
    parser.add_argument("--modes", default="roundtrip,overwrite")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--seed", type=int, default=20261028)
    parser.add_argument("--build-notes", default="not recorded")
    args = parser.parse_args(argv)
    try:
        sizes = [int(x) for x in args.sizes.split(",")]
    except ValueError:
        parser.error("invalid sizes")
    modes = args.modes.split(",")
    if (not 1 <= args.runs <= 1000 or not 0 <= args.warmup <= 100 or
            not 1 <= args.samples <= 56 or args.timeout < 1 or
            not sizes or len(set(sizes)) != len(sizes) or any(x not in SIZES for x in sizes) or
            not modes or len(set(modes)) != len(modes) or any(x not in MODES for x in modes)):
        parser.error("invalid run, warmup, sample, size, mode or timeout value")
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
                "kernel_sha256": sha256(kernel), "image_sha256": sha256(image),
                "source_sha256": source_hash(ROOT), "qemu_sha256": sha256(Path(qemu)),
                "qemu_path": qemu, "qemu_version": version.stdout.splitlines()[0],
                "runner_sha256": sha256(Path(__file__)),
                "host": platform.platform(), "cpus": 1,
                "backend": "file-backed QEMU NVDIMM" if args.nvdimm else "volatile EMUL",
                "root_disk_backend": args.backend,
                "qemu_command_template": qemu_command(
                    qemu, kernel, "<private-root-disk>", args.backend,
                    "<fresh-32MiB-nvdimm>" if args.nvdimm else None),
                "runs": args.runs, "warmup_boots": args.warmup,
                "guest_warmup_transactions_per_backend": 4, "samples": args.samples,
                "batch_transactions_per_backend": args.samples,
                "batch_order": "alternates by boot; batch after interleaved latency samples",
                "sizes": sizes, "modes": modes, "seed": args.seed,
                "build_notes": args.build_notes,
                "durability": ("NOT equivalent: PMFS touch+fence; native xv6 global log sync "
                               + ("with NVMe Flush" if args.backend == "nvme"
                                  else "without virtio Flush")),
                "warning": "QEMU TCG guest time and software requests, not physical PMEM or SSD performance"}
    (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    records, batches, failures, rng = [], [], [], random.Random(args.seed)
    with tempfile.TemporaryDirectory(prefix="pmfs-perf-") as tmp, \
            (out / "raw.jsonl").open("w") as raw, (out / "batches.jsonl").open("w") as batch_file:
        tmp = Path(tmp)
        frequency, dtb_command = get_timebase(qemu, tmp)
        metadata["timebase_frequency_hz"] = frequency
        metadata["timebase_source"] = "QEMU virt generated DTB /cpus/timebase-frequency"
        metadata["dtb_sha256"] = sha256(tmp / "qemu-virt.dtb")
        metadata["dtb_command"] = dtb_command
        (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        for run in range(args.warmup + args.runs):
            tasks = [(mode, size) for mode in modes for size in sizes]
            rng.shuffle(tasks)
            for mode, size in tasks:
                disk = tmp / "disk.img"
                shutil.copyfile(image, disk)
                nvdimm = None
                if args.nvdimm:
                    nvdimm = tmp / "nvdimm.img"
                    with nvdimm.open("wb") as media:
                        media.truncate(32 * 1024 * 1024)
                label = f"{run:03d}-{mode}-{size}"
                try:
                    samples, batch, wall = boot(qemu, kernel, disk, mode, size, args.samples,
                                                args.timeout, out / f"{label}.log", run % 2,
                                                root_backend=args.backend, nvdimm_file=nvdimm)
                    batch.update(run=run, warmup=run < args.warmup, mode=mode, size=size,
                                 status="success")
                    batches.append(batch)
                    batch_file.write(json.dumps(batch, sort_keys=True) + "\n")
                    batch_file.flush()
                    for row in samples:
                        row.update(run=run, warmup=run < args.warmup, mode=mode, size=size,
                                   status="success")
                        records.append(row)
                        raw.write(json.dumps(row, sort_keys=True) + "\n")
                    print(f"PERF_BOOT_PASS run={run} mode={mode} size={size} "
                          f"samples={len(samples)} wall_seconds={wall:.3f}", flush=True)
                except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
                    failure = {"run": run, "warmup": run < args.warmup, "mode": mode,
                               "size": size, "status": "failure", "error": str(exc)}
                    failures.append(failure)
                    raw.write(json.dumps(failure, sort_keys=True) + "\n")
                    print(f"PERF_BOOT_FAIL {label}: {exc}", file=sys.stderr, flush=True)
                raw.flush()
        if sha256(image) != metadata["image_sha256"] or sha256(kernel) != metadata["kernel_sha256"]:
            raise RuntimeError("input kernel or image changed during experiment")
        result = {"method": "Latency: interleaved per-operation guest ticks; p50/p95 pooled "
                            "descriptive and intervals bootstrap independent boots. Batch throughput: "
                            "completed transactions / whole timed batch, including pattern and loop "
                            "overhead but excluding printing/metrics; backends run sequentially.",
                  "frequency_hz": frequency, "bootstrap_seed": args.seed,
                  "bootstrap_samples": 2000, "failures": failures,
                  "durability_equivalent": False,
                  "results": summarize(records, frequency, seed=args.seed) if records else {},
                  "batch_throughput": summarize_batches(batches, frequency, seed=args.seed) if batches else {}}
        (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    if failures:
        print(f"PMFS_PERF_EXPERIMENT_FAIL failed_boots={len(failures)}", file=sys.stderr)
        return 1
    print(f"PMFS_PERF_EXPERIMENT_PASS groups={len(sizes) * len(modes)} "
          f"measured_boots_per_group={args.runs} frequency_hz={frequency} "
          "durability_equivalent=0", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print(f"PMFS_PERF_EXPERIMENT_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
