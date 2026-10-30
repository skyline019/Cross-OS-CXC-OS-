#!/usr/bin/env python3
"""Restart test for direct-core sizes, truncations and COW line reuse.

Uses a new disposable QEMU NVDIMM and a private root-disk copy for every boot.
Each update is followed by a reboot/read, a full-media read-only hash check,
and an independent host traversal of BOTH root graphs with the expected bytes
for every committed epoch. This is not a physical power-failure test or a VFS.
"""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import platform
import random
import shutil
import subprocess
import sys
import tempfile

from bench_perf import source_hash
from directcore_recovery import DEVICE_BYTES, RESERVED, Runner, sha256
from directcore_trace_replay import expected_pattern, inspect_graph

SIZES = (0, 1, 63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512)


def make_cases(random_steps, seed):
    if random_steps < 0 or random_steps > 1000:
        raise ValueError("random_steps must be between 0 and 1000")
    cases = [{"size": size, "generation": i + 1, "kind": "boundary"}
             for i, size in enumerate(SIZES)]
    cases += [{"size": size, "generation": generation, "kind": "shrink-regrow-or-identical"}
              for size, generation in ((1, 50), (512, 51), (0, 52), (512, 53),
                                       (65, 254), (65, 254), (65, 510),
                                       (0, 1), (0, 257), (64, 10), (64, 266),
                                       (63, 42), (64, 42), (65, 42),
                                       (64, 42), (63, 42))]
    rng = random.Random(seed)
    for i in range(random_steps):
        if i % 5 == 0:
            prev = cases[-1]
            cases.append({"size": prev["size"], "generation": prev["generation"],
                          "kind": "random-identical"})
        else:
            cases.append({"size": rng.choice(SIZES), "generation": rng.randrange(1024),
                          "kind": "random"})
    return cases


def save_gzip(source, dest):
    with source.open("rb") as stream, gzip.open(dest, "wb") as out:
        shutil.copyfileobj(stream, out)


def active_view(path, expected):
    with path.open("rb") as media:
        media.seek(RESERVED)
        base = media.read(DEVICE_BYTES - RESERVED + 1)
    if len(base) != DEVICE_BYTES - RESERVED:
        raise ValueError("unexpected NVDIMM length")
    return inspect_graph(base, {}, expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--backend", choices=("virtio", "nvme"), default="nvme")
    parser.add_argument("--cpus", type=int, default=3)
    parser.add_argument("--zicbom", action="store_true")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--random-steps", type=int, default=24)
    parser.add_argument("--seed", type=int, default=20261030)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.cpus <= 8 or args.timeout < 1 or not 0 <= args.random_steps <= 1000:
        parser.error("invalid CPUs, timeout or random step count")
    qemu = shutil.which(args.qemu)
    root = args.root.resolve()
    kernel, disk = root / "kernel/kernel", root / "fs.img"
    if not qemu or not kernel.is_file() or not disk.is_file():
        parser.error("need QEMU and built kernel/kernel and fs.img")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    logs = out / "serial"
    logs.mkdir()
    version = subprocess.run([qemu, "--version"], capture_output=True, text=True, check=True, timeout=30)
    original = (sha256(kernel), sha256(disk))
    metadata = {"timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "kernel_sha256": original[0], "fs_image_sha256": original[1],
                "source_sha256": source_hash(root), "runner_sha256": sha256(Path(__file__)),
                "qemu_sha256": sha256(Path(qemu)), "qemu_version": version.stdout.splitlines()[0],
                "host": platform.platform(), "cpus": args.cpus, "backend": args.backend,
                "zicbom": args.zicbom, "random_steps": args.random_steps, "seed": args.seed,
                "scope": "bounded test-only single-file core; QEMU file-backed NVDIMM; not physical power loss"}
    (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    cases = make_cases(args.random_steps, args.seed)
    expected = {1: b""}
    epoch, identical, committed, completed = 1, 0, 0, 0
    summary = {"cases": len(cases), "completed": 0, "failures": [], "guest_boots": 0}
    with tempfile.TemporaryDirectory(prefix="directcore-boundaries-") as temp, \
            (out / "cases.jsonl").open("w") as raw:
        tmp = Path(temp)
        device = tmp / "nvdimm.img"
        with device.open("wb") as f:
            f.truncate(DEVICE_BYTES)
        runner = Runner(root, qemu, tmp, args.backend, args.cpus, args.timeout, logs, args.zicbom)
        try:
            runner.boot(device, "format", "directcore format --force", rb"^DIRECTCORE_FORMAT_PASS\r?\n",
                        failure_marker=b"DIRECTCORE_TEST_FAIL")
            if active_view(device, expected)["epoch"] != epoch:
                raise ValueError("formatted image has unexpected active epoch")
            save_gzip(device, out / "initial.pmem.gz")
            for index, item in enumerate(cases, 1):
                size, generation = item["size"], item["generation"]
                content = expected_pattern(generation, size)
                changed = content != expected[epoch]
                row = {"index": index, **item, "changed": changed,
                       "expected_content_sha256": hashlib.sha256(content).hexdigest()}
                try:
                    before = sha256(device)
                    runner.boot(device, f"write-{index:03d}", f"directcore write {generation} {size}",
                                rb"^DIRECTCORE_WRITE_PASS generation=" + str(generation).encode() +
                                rb" size=" + str(size).encode() + rb"\r?\n",
                                failure_marker=b"DIRECTCORE_TEST_FAIL")
                    after = sha256(device)
                    if changed:
                        if after == before:
                            raise ValueError("changed write did not modify the media")
                        epoch += 1
                        expected[epoch] = content
                        committed += 1
                    else:
                        if after != before:
                            raise ValueError("identical write modified the media")
                        identical += 1
                    view = active_view(device, expected)
                    if view["epoch"] != epoch or view["size"] != size:
                        raise ValueError("host root graph disagrees with expected active version")
                    runner.boot(device, f"reboot-{index:03d}", f"directcore check {generation} {size}",
                                rb"^DIRECTCORE_CHECK_PASS generation=" + str(generation).encode() +
                                rb" size=" + str(size).encode() + rb"\r?\n",
                                failure_marker=b"DIRECTCORE_TEST_FAIL")
                    if sha256(device) != after:
                        raise ValueError("read-only reboot modified the NVDIMM")
                    row.update(status="pass", epoch=epoch, media_sha256=after)
                    completed += 1
                    print(f"DIRECTCORE_BOUNDARY_PASS index={index} size={size} "
                          f"generation={generation} changed={int(changed)} epoch={epoch}", flush=True)
                except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
                    row.update(status="failure", error=str(exc))
                    summary["failures"].append(row.copy())
                    raise
                finally:
                    raw.write(json.dumps(row, sort_keys=True) + "\n")
                    raw.flush()
            if (sha256(kernel), sha256(disk)) != original:
                raise RuntimeError("input kernel or root image changed")
            save_gzip(device, out / "final.pmem.gz")
        except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
            if not summary["failures"]:
                summary["failures"].append({"stage": "initialization-or-finalization", "error": str(exc)})
            raise
        finally:
            initial, final = out / "initial.pmem.gz", out / "final.pmem.gz"
            summary.update(completed=completed, guest_boots=runner.count, final_epoch=epoch,
                           changed_writes=committed, identical_writes=identical,
                           sizes=list(SIZES), read_only_reboots_unchanged=completed,
                           initial_image_gzip_sha256=sha256(initial) if initial.is_file() else None,
                           final_image_gzip_sha256=sha256(final) if final.is_file() else None,
                           both_root_content_oracle="checked after each completed update",
                           source_images_unchanged=(sha256(kernel), sha256(disk)) == original)
            (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"DIRECTCORE_BOUNDARY_RECOVERY_PASS cases={completed} boots={runner.count} "
          f"changed={committed} identical={identical} cpus={args.cpus} zicbom={int(args.zicbom)}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print(f"DIRECTCORE_BOUNDARY_RECOVERY_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
