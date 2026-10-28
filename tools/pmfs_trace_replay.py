#!/usr/bin/env python3
"""Replay observed PMFS persistence-operation traces into rebootable images.

Run only in a disposable checkout built with -DPMEM_MAP -DPMFS_TRACE and with
PMFS_SELFTEST disabled. This is a sampled software persistence model, not a
CPU-instruction trace or a physical power-failure experiment. See
 docs/TRACE-REPLAY.md for its persistence domain, atomicity and coverage limits.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

from bench_perf import sha256, source_hash
import pmfs_recovery as recovery

LINE = 64
BEGIN = re.compile(r"^PMTRACE BEGIN version=1 line=64 bytes=(\d+)\r?$")
W = re.compile(r"^PMTRACE W seq=(\d+) off=(\d+) n=(\d+) atomic=([01]) data=([0-9a-f]+)\r?$")
F = re.compile(r"^PMTRACE F seq=(\d+) off=(\d+)\r?$")
SIMPLE = re.compile(r"^PMTRACE (D|ACK|END) seq=(\d+)\r?$")


def parse_trace(text):
    size, events, expected, ended, acknowledged = None, [], 0, False, False
    for line in text.splitlines():
        if not line.startswith("PMTRACE "):
            continue
        begin, write, flush, simple = BEGIN.fullmatch(line), W.fullmatch(line), F.fullmatch(line), SIMPLE.fullmatch(line)
        if begin:
            if size is not None:
                raise ValueError("duplicate trace begin")
            size = int(begin.group(1))
            if size < 4 * recovery.PAGE or size % LINE:
                raise ValueError("invalid trace device size")
            continue
        if size is None or ended:
            raise ValueError("event outside trace")
        event = {}
        if write:
            seq, off, n, atomic = map(int, write.groups()[:4])
            data = bytes.fromhex(write.group(5))
            if (n < 1 or n > LINE or len(data) != n or off + n > size or
                    off // LINE != (off + n - 1) // LINE or
                    (atomic and (n != 8 or off % 8))):
                raise ValueError("invalid store range")
            event = {"kind": "W", "seq": seq, "off": off, "data": data,
                     "atomic": bool(atomic)}
        elif flush:
            seq, off = map(int, flush.groups())
            if off % LINE or off + LINE > size:
                raise ValueError("invalid flush range")
            event = {"kind": "F", "seq": seq, "off": off}
        elif simple:
            kind, number = simple.groups()
            seq = int(number)
            if kind == "ACK":
                if acknowledged:
                    raise ValueError("duplicate ACK")
                acknowledged = True
            if kind == "END":
                ended = True
            event = {"kind": kind, "seq": seq}
        else:
            raise ValueError("malformed trace event: " + line[:120])
        if seq != expected:
            raise ValueError(f"missing, duplicated or reordered event {expected} / {seq}")
        if acknowledged and event["kind"] in ("W", "F", "D"):
            raise ValueError("writes or persistence events after ACK")
        expected += 1
        events.append(event)
    kinds = {event["kind"] for event in events}
    if (size is None or not ended or not acknowledged or
            not {"W", "F", "D", "ACK", "END"} <= kinds or
            sum(event.get("atomic", False) for event in events) < 2):
        raise ValueError("incomplete PMFS persistence trace")
    return size, events


def apply_all_stores(base, events):
    result = bytearray(base)
    for event in events:
        if event["kind"] == "W":
            off = event["off"]
            result[off:off + len(event["data"])] = event["data"]
    return bytes(result)


class Model:
    def __init__(self, base, history="deferred", omit_flush_page=None):
        self.base = base
        self.history = history
        self.omit_flush_page = omit_flush_page
        self.cache, self.stable, self.pending = {}, {}, {}
        self.acknowledged = False

    def initial_line(self, line):
        return self.cache.get(line, self.base[line:line + LINE])

    def step(self, event):
        kind = event["kind"]
        if kind == "W":
            off, data = event["off"], event["data"]
            line = off // LINE * LINE
            updated = bytearray(self.initial_line(line))
            updated[off - line:off - line + len(data)] = data
            self.cache[line] = bytes(updated)
            if self.history == "eager" or (self.history == "alternating" and line // LINE % 2 == 0):
                self.stable[line] = self.cache[line]
                if line in self.pending:
                    self.pending[line] = self.cache[line]
        elif kind == "F":
            line = event["off"]
            if self.omit_flush_page is None or line // recovery.PAGE != self.omit_flush_page:
                self.pending[line] = self.initial_line(line)
        elif kind == "D":
            self.stable.update(self.pending)
            self.pending.clear()
        elif kind == "ACK":
            self.acknowledged = True

    def snapshot(self, pending="none", evict=None, partial=None):
        result = dict(self.stable)
        if pending == "all":
            result.update(self.pending)
        elif pending == "alternate":
            result.update((line, value) for line, value in self.pending.items()
                          if line // LINE % 2 == 0)
        elif pending != "none":
            raise ValueError("invalid pending policy")
        if evict is not None:
            result[evict] = self.initial_line(evict)
        if partial is not None:
            event, count = partial
            off = event["off"]
            line = off // LINE * LINE
            value = bytearray(self.initial_line(line))
            value[off - line:off - line + count] = event["data"][:count]
            result[line] = bytes(value)
        return {line: value for line, value in result.items()
                if value != self.base[line:line + LINE]}


def patch_hash(patch):
    h = hashlib.sha256()
    for off, data in sorted(patch.items()):
        h.update(struct.pack(">Q", off))
        h.update(data)
    return h.hexdigest()


def candidates(base, events):
    distinct = {}

    def add(model, event, scenario, patch, anchor=False):
        identity = patch_hash(patch)
        entry = distinct.setdefault(identity, {"sha256": identity, "patch": patch,
                                             "contexts": [], "requires_new": False,
                                             "anchor": False})
        context = {"seq": event["seq"], "event": event["kind"],
                   "history": model.history, "scenario": scenario}
        if context not in entry["contexts"]:
            entry["contexts"].append(context)
        entry["requires_new"] |= model.acknowledged
        entry["anchor"] |= anchor or model.acknowledged

    for history in ("deferred", "eager", "alternating"):
        model = Model(base, history)
        add(model, {"seq": -1, "kind": "BEGIN"}, "baseline", {}, True)
        for event in events:
            kind = event["kind"]
            if kind == "W" and not event["atomic"] and len(event["data"]) > 1:
                for count in sorted({1, len(event["data"]) // 2}):
                    add(model, event, f"partial-{count}-evicted",
                        model.snapshot(partial=(event, count)))
                    add(model, event, f"partial-{count}-pending-all",
                        model.snapshot(pending="all", partial=(event, count)))
            model.step(event)
            if kind == "W":
                line = event["off"] // LINE * LINE
                add(model, event, "guaranteed", model.snapshot(), event["atomic"])
                add(model, event, "last-line-evicted", model.snapshot(evict=line), event["atomic"])
                add(model, event, "last-line-evicted-pending-all",
                    model.snapshot(pending="all", evict=line), event["atomic"])
            elif kind == "F":
                for policy in ("none", "all", "alternate"):
                    add(model, event, "pending-" + policy, model.snapshot(pending=policy))
            elif kind in ("D", "ACK", "END"):
                add(model, event, "guaranteed", model.snapshot(), True)
    return list(distinct.values())


def choose(cases, maximum, seed):
    if maximum == 0 or len(cases) <= maximum:
        return cases
    anchors = [case for case in cases if case["anchor"]]
    if len(anchors) > maximum:
        raise ValueError(f"{len(anchors)} mandatory drain/commit/ACK states exceed --max-images")
    remainder = [case for case in cases if not case["anchor"]]
    rng = random.Random(seed)
    rng.shuffle(remainder)
    # Retain representatives of partial writes, evictions and pending subsets.
    for scenario in ("partial-", "last-line-evicted", "pending-all", "pending-alternate"):
        for case in list(remainder):
            if any(c["scenario"].startswith(scenario) for c in case["contexts"]):
                anchors.append(case)
                remainder.remove(case)
                break
    if len(anchors) > maximum:
        raise ValueError("--max-images too small for mandatory scenario coverage")
    return anchors + remainder[:maximum - len(anchors)]


def device_bytes(path, size):
    with path.open("rb") as f:
        f.seek(recovery.FS_START)
        data = f.read(size)
    if len(data) != size:
        raise ValueError("short PMFS image")
    return data


def copy_and_patch(base_ram, dest, patch):
    recovery.sparse_copy(base_ram, dest)
    with dest.open("r+b") as f:
        for off, value in patch.items():
            f.seek(recovery.FS_START + off)
            f.write(value)
        f.flush()
        os.fsync(f.fileno())


def oracle(run, ram, label):
    text = run.regress(ram, "traceprobe", label)
    found = re.findall(r"^PMFS_TRACE_ORACLE state=(old|new|invalid)\r?$", text, re.MULTILINE)
    if len(found) != 1:
        raise RuntimeError(f"{label}: missing or duplicate reboot oracle")
    return found[0]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--max-images", type=int, default=120,
                        help="distinct modeled images to reboot; 0 tests all")
    parser.add_argument("--seed", type=int, default=20261028)
    parser.add_argument("--build-notes", default="not recorded")
    args = parser.parse_args(argv)
    if args.timeout < 1 or (args.max_images != 0 and args.max_images < 32):
        parser.error("timeout must be positive and max-images 0 or at least 32")
    root = Path(__file__).resolve().parent.parent
    qemu = shutil.which(args.qemu)
    kernel, disk = root / "kernel/kernel", root / "fs.img"
    if not qemu or not kernel.is_file() or not disk.is_file():
        parser.error("QEMU, kernel/kernel and fs.img must exist")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    logs = out / "serial"
    logs.mkdir()
    version = subprocess.run([qemu, "--version"], capture_output=True, text=True, check=True)
    metadata = {"schema": 1, "kernel_sha256": sha256(kernel), "fs_img_sha256": sha256(disk),
                "source_sha256": source_hash(root), "qemu_sha256": sha256(Path(qemu)),
                "qemu_version": version.stdout.splitlines()[0], "build_notes": args.build_notes,
                "backend": "QEMU ordinary RAM MAP, single hart", "line_bytes": LINE,
                "seed": args.seed, "max_images": args.max_images,
                "limitations": "sampled source-level operations, 8-byte atomic commit assumption, "
                               "assumed flush/drain model; no instruction trace or physical power loss"}
    (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    failures = []
    with tempfile.TemporaryDirectory(prefix="pmfs-trace-") as temporary:
        d = Path(temporary)
        run = recovery.Runner(root, qemu, d, args.timeout, 1, False, logs, False)
        baseline, executed, probe = d / "base.ram", d / "executed.ram", d / "probe.ram"
        with baseline.open("wb") as f:
            f.truncate(recovery.RAM_SIZE)
        run.regress(baseline, "init", "fixture-init")
        run.regress(baseline, "old", "fixture-old-reboot")
        recovery.sparse_copy(baseline, executed)
        trace_text = run.regress(executed, "trace", "captured-kernel-trace")
        size, events = parse_trace(trace_text)
        if recovery.FS_START + size > recovery.RAM_SIZE:
            raise ValueError("trace region exceeds backing image")
        base_data = device_bytes(baseline, size)
        observed_final = device_bytes(executed, size)
        if apply_all_stores(base_data, events) != observed_final:
            raise ValueError("traced stores do not reconstruct final PMFS device bytes")
        if oracle(run, executed, "executed-reboot") != "new":
            raise ValueError("completed unmodified trace did not recover new state")
        with gzip.open(out / "baseline-pmfs.bin.gz", "wb", compresslevel=9) as f:
            f.write(base_data)
        (out / "trace.txt").write_text("\n".join(line for line in trace_text.splitlines()
                                                 if line.startswith("PMTRACE ")) + "\n")
        metadata.update(device_bytes=size, baseline_sha256=hashlib.sha256(base_data).hexdigest(),
                        observed_final_sha256=hashlib.sha256(observed_final).hexdigest(),
                        event_counts={kind: sum(e["kind"] == kind for e in events)
                                      for kind in ("W", "F", "D", "ACK", "END")},
                        final_reconstruction="exact full-device byte match")
        (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        all_cases = candidates(base_data, events)
        selected = choose(all_cases, args.max_images, args.seed)
        selected_ids = {case["sha256"] for case in selected}
        with (out / "candidates.jsonl").open("w") as output:
            for case in all_cases:
                public = {key: value for key, value in case.items() if key != "patch"}
                public["selected"] = case["sha256"] in selected_ids
                output.write(json.dumps(public, sort_keys=True) + "\n")
        records = []
        with (out / "reboots.jsonl").open("w") as output:
            for i, case in enumerate(selected):
                label = f"model-{i:04d}"
                record = {"index": i, "patch_sha256": case["sha256"],
                          "requires_new": case["requires_new"], "contexts": case["contexts"]}
                try:
                    copy_and_patch(baseline, probe, case["patch"])
                    state = oracle(run, probe, label)
                    record["state"] = state
                    record["pass"] = state == "new" if case["requires_new"] else state in ("old", "new")
                except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
                    record.update(state="error", error=str(exc), **{"pass": False})
                output.write(json.dumps(record, sort_keys=True) + "\n")
                output.flush()
                records.append(record)
                if not record["pass"]:
                    failures.append(record)
                probe.unlink(missing_ok=True)
            first_page = next((e["off"] // recovery.PAGE for e in events
                               if e["kind"] == "W" and not e["atomic"] and
                               e["off"] >= 2 * recovery.PAGE), None)
            if first_page is None:
                raise ValueError("trace lacks a data-page write for negative control")
            model = Model(base_data, omit_flush_page=first_page)
            for event in events:
                model.step(event)
            slot = recovery.newest_sb(executed)[1]
            negatives = [
                ("omit-data-page-flush", model.snapshot()),
                ("publish-final-checkpoint-before-body", {
                    slot * recovery.PAGE: observed_final[slot * recovery.PAGE:
                                                         slot * recovery.PAGE + LINE]}),
            ]
            for label, patch in negatives:
                copy_and_patch(baseline, probe, patch)
                state = oracle(run, probe, "negative-" + label)
                record = {"negative": label, "state": state, "detected": state != "new",
                          "patch_sha256": patch_hash(patch)}
                output.write(json.dumps(record, sort_keys=True) + "\n")
                output.flush()
                records.append(record)
                if not record["detected"]:
                    failures.append(record)
                probe.unlink(missing_ok=True)
        result = {"candidate_images": len(all_cases), "selected_images": len(selected),
                  "unselected_images": len(all_cases) - len(selected),
                  "normal_passes": sum(r.get("pass", False) for r in records),
                  "negative_controls": [r for r in records if "negative" in r],
                  "failures": failures, "event_counts": metadata["event_counts"],
                  "all_generated_candidates_rebooted": len(selected) == len(all_cases),
                  "physical_power_loss_test": False}
        (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    if sha256(kernel) != metadata["kernel_sha256"] or sha256(disk) != metadata["fs_img_sha256"]:
        raise RuntimeError("input kernel or fs image changed during replay")
    if failures:
        print(f"PMFS_TRACE_REPLAY_FAIL failures={len(failures)}", file=sys.stderr)
        return 1
    print(f"PMFS_TRACE_REPLAY_PASS events={len(events)} "
          f"reboots={len(selected)} candidates={len(all_cases)} negatives=2 "
          f"all_generated={int(len(selected) == len(all_cases))}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print(f"PMFS_TRACE_REPLAY_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
