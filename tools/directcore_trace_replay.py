#!/usr/bin/env python3
"""Replay one observed direct-core PMEM trace into QEMU restart images.

Samples deferred/eager/alternating writeback, selected pending flush subsets,
individual early evictions and partial non-atomic stores. It assumes an atomic
aligned 8-byte root store; it is not an exhaustive hardware failure model.
The test-only core is a bounded single-file engine, not a VFS filesystem.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import zlib

from directcore_recovery import DEVICE_BYTES, LINE, RESERVED, Runner, sha256
from pmfs_trace_replay import BEGIN, F, W, apply_all_stores, candidates

SIMPLE = re.compile(r"^PMTRACE (O|D|ACK|END) seq=(\d+)\r?$")
MAGIC = 0x31434644
SALT = 0x9B7D4E21


def parse_trace(text, expected_size):
    size, events, seq, ended, ack = None, [], 0, False, False
    for line in text.splitlines():
        if not line.startswith("PMTRACE "):
            continue
        begin, write, flush, simple = BEGIN.fullmatch(line), W.fullmatch(line), F.fullmatch(line), SIMPLE.fullmatch(line)
        if begin:
            if size is not None:
                raise ValueError("duplicate trace begin")
            size = int(begin.group(1))
            if size != expected_size:
                raise ValueError("trace size differs from baseline")
            continue
        if size is None or ended:
            raise ValueError("trace event outside active trace")
        if write:
            num, off, n, atomic = map(int, write.groups()[:4])
            data = bytes.fromhex(write.group(5))
            if (n < 1 or n > LINE or len(data) != n or off + n > size or
                    off // LINE != (off + n - 1) // LINE or
                    (atomic and (n != 8 or off % 8))):
                raise ValueError("invalid trace store")
            event = {"kind": "W", "seq": num, "off": off, "data": data,
                     "atomic": bool(atomic)}
        elif flush:
            num, off = map(int, flush.groups())
            if off % LINE or off + LINE > size:
                raise ValueError("invalid trace flush")
            event = {"kind": "F", "seq": num, "off": off}
        elif simple:
            kind, number = simple.groups()
            num = int(number)
            if kind == "ACK":
                if ack:
                    raise ValueError("duplicate ACK")
                ack = True
            if kind == "END":
                ended = True
            event = {"kind": kind, "seq": num}
        else:
            raise ValueError("malformed trace line: " + line[:100])
        if num != seq or (ack and event["kind"] in ("W", "F", "O", "D")):
            raise ValueError("trace sequence or ACK ordering")
        seq += 1
        events.append(event)
    if size is None or not ended or not ack:
        raise ValueError("incomplete trace")
    audit_protocol(events)
    return events


def audit_protocol(events):
    pos, writes, flushed = 0, set(), set()
    while pos < len(events) and events[pos]["kind"] == "W" and not events[pos]["atomic"]:
        event = events[pos]
        if event["off"] < 2 * LINE or event["off"] % LINE or len(event["data"]) != LINE:
            raise ValueError("candidate store is not one complete object line")
        if event["off"] in writes:
            raise ValueError("duplicate candidate object store")
        writes.add(event["off"])
        pos += 1
    if not writes:
        raise ValueError("no candidate stores")
    if pos >= len(events) or events[pos]["kind"] != "O":
        raise ValueError("missing pre-flush candidate ordering fence")
    pos += 1
    while pos < len(events) and events[pos]["kind"] == "F":
        off = events[pos]["off"]
        if off not in writes or off in flushed:
            raise ValueError("unexpected or duplicate candidate flush")
        flushed.add(off)
        pos += 1
    if flushed != writes or pos >= len(events) or events[pos]["kind"] != "D":
        raise ValueError("candidate flushes must precede first drain")
    pos += 1
    if pos >= len(events):
        raise ValueError("missing root publication")
    root = events[pos]
    if (root["kind"] != "W" or not root["atomic"] or
            root["off"] not in (0, LINE) or len(root["data"]) != 8):
        raise ValueError("root must be one aligned atomic word")
    pos += 1
    if (pos + 4 >= len(events) or events[pos]["kind"] != "O" or
            events[pos + 1]["kind"] != "F" or events[pos + 1]["off"] != root["off"] or
            events[pos + 2]["kind"] != "D" or events[pos + 3]["kind"] != "ACK" or
            events[pos + 4]["kind"] != "END" or pos + 5 != len(events)):
        raise ValueError("root ordering/flush/drain/ACK sequence is incomplete")
    return {"candidate_stores": len(writes), "root_offset": root["off"],
            "flushes": len(flushed) + 1, "ordering_fences": 2,
            "drain_groups": 2, "atomic_root_stores": 1}


def validate_reconstruction(baseline, observed, events, reserved=RESERVED):
    if (len(baseline) != len(observed) or len(baseline) < reserved or
            baseline[:reserved] != observed[:reserved] or
            apply_all_stores(baseline[reserved:], events) != observed[reserved:]):
        raise ValueError("trace stores do not reconstruct the complete observed NVDIMM image")


def expected_pattern(generation, size):
    return bytes((i * 29 + (i >> 3) * 7 + generation * 31) & 255 for i in range(size))


def inspect(base, patch):
    """Validate both metadata graphs and check each root's expected bytes."""
    units = len(base) // LINE
    all_objects = {}

    def read(index):
        if index < 0 or index >= units:
            raise ValueError("line out of range")
        off = index * LINE
        return patch.get(off, base[off:off + LINE])

    def view(slot):
        local = set()

        def mark(index, kind, crc):
            if index < 2 or index >= units or index in local:
                raise ValueError("invalid or aliased object")
            local.add(index)
            old = all_objects.setdefault(index, (kind, crc))
            if old != (kind, crc):
                raise ValueError("conflicting roles between protected roots")

        word = struct.unpack_from("<Q", read(slot))[0]
        cp_id = word & 0xffffffff
        checksum = zlib.crc32(struct.pack("<II", cp_id, SALT))
        if word >> 32 != checksum:
            raise ValueError("root checksum")
        cp = read(cp_id)
        magic, version, flags, epoch, table_id, table_crc = struct.unpack_from("<IHHQII", cp)
        stored_crc = struct.unpack_from("<I", cp, 60)[0]
        if (magic != MAGIC or version != 1 or flags or not epoch or cp[24] != 1 or
                any(cp[25:28]) or struct.unpack_from("<I", cp, 28)[0] != units or
                any(cp[32:60]) or zlib.crc32(cp[:60] + b"\0" * 4) != stored_crc):
            raise ValueError("checkpoint metadata")
        mark(cp_id, "checkpoint", stored_crc)
        mark(table_id, "table", table_crc)
        table = read(table_id)
        if zlib.crc32(table) != table_crc:
            raise ValueError("table checksum")
        ids = struct.unpack_from("<8I", table, 0)
        crcs = struct.unpack_from("<8I", table, 32)
        if any(ids[i] or crcs[i] for i in range(8) if i != 1):
            raise ValueError("unexpected inode-table entry")
        mark(ids[1], "inode", crcs[1])
        inode = read(ids[1])
        ino, tree, generation, length, nlink, itype, height, flags, tree_crc, parent, pgen, mtime, reserved = struct.unpack_from("<IIQQIBBHIIQQQ", inode)
        if (zlib.crc32(inode) != crcs[1] or ino != 1 or generation != 1 or
                length > 512 or nlink != 1 or itype != 1 or height != 1 or flags or
                parent or pgen or mtime or reserved):
            raise ValueError("inode metadata")
        count = (length + LINE - 1) // LINE
        pointers = []
        if count:
            mark(tree, "node", tree_crc)
            node = read(tree)
            if zlib.crc32(node) != tree_crc:
                raise ValueError("data-node checksum")
            pointers = list(struct.unpack_from("<8I", node, 0))
            checksums = struct.unpack_from("<8I", node, 32)
            for i in range(8):
                if i < count:
                    if checksums[i]:
                        raise ValueError("unexpected data checksum")
                    mark(pointers[i], "data", 0)
                elif pointers[i] or checksums[i]:
                    raise ValueError("data beyond EOF")
        elif tree or tree_crc:
            raise ValueError("nonempty tree for empty file")
        content = b"".join(read(index) for index in pointers[:count])[:length]
        expected = {1: b"", 2: expected_pattern(1, 128), 3: expected_pattern(2, 128)}
        if epoch not in expected or content != expected[epoch]:
            raise ValueError("external content oracle")
        return {"epoch": epoch, "checkpoint": cp_id, "data": pointers[:count]}

    roots = [view(0), view(1)]
    if roots[0]["epoch"] == roots[1]["epoch"] and roots[0]["checkpoint"] != roots[1]["checkpoint"]:
        raise ValueError("equal epoch with distinct roots")
    active = max(roots, key=lambda root: root["epoch"])
    if active["epoch"] not in (2, 3):
        raise ValueError("unexpected active epoch")
    return ("old" if active["epoch"] == 2 else "new"), active


def serializable(case, state):
    return {"sha256": case["sha256"], "state": state, "requires_new": case["requires_new"],
            "contexts": case["contexts"],
            "patch": {str(off): value.hex() for off, value in sorted(case["patch"].items())}}


def first_bad(base, events):
    for case in candidates(base, events):
        try:
            inspect(base, case["patch"])
        except ValueError as exc:
            return case, str(exc)
    raise ValueError("negative mutation was not detected")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--final", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--backend", choices=("virtio", "nvme"), default="nvme")
    parser.add_argument("--cpus", type=int, default=1)
    parser.add_argument("--zicbom", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    if not (root / "kernel/kernel").is_file() or not (root / "fs.img").is_file() or not 1 <= args.cpus <= 8:
        parser.error("need a built direct-core kernel, fs.img and cpus 1..8")
    with gzip.open(args.baseline, "rb") as source:
        whole = source.read(DEVICE_BYTES + 1)
    if len(whole) != DEVICE_BYTES:
        raise ValueError("baseline must be exactly 32 MiB")
    base = whole[RESERVED:]
    with gzip.open(args.final, "rb") as source:
        observed = source.read(DEVICE_BYTES + 1)
    if len(observed) != DEVICE_BYTES:
        raise ValueError("final image must be exactly 32 MiB")
    events = parse_trace(args.trace.read_text(), len(base))
    protocol = audit_protocol(events)
    validate_reconstruction(whole, observed, events)
    if inspect(base, {})[0] != "old":
        raise ValueError("baseline must contain old committed data")
    images = candidates(base, events)
    states = []
    for case in images:
        state, _ = inspect(base, case["patch"])
        if case["requires_new"] and state != "new":
            raise ValueError("acknowledged image did not recover new contents")
        states.append(state)

    first_write = next(e for e in events if e["kind"] == "W" and not e["atomic"])
    dropped = [e for e in events if not (e["kind"] == "F" and e["off"] == first_write["off"])]
    try:
        audit_protocol(dropped)
    except ValueError:
        pass
    else:
        raise ValueError("missing-flush audit negative control failed")
    root_index = next(i for i, event in enumerate(events) if event["kind"] == "W" and event["atomic"])
    early = list(events)
    early.insert(0, early.pop(root_index))
    _, current = inspect(base, {})
    overwritten = [dict(e) for e in events]
    next(e for e in overwritten if e["kind"] == "W" and not e["atomic"])["off"] = current["data"][0] * LINE
    negatives = []
    for label, mutated in (("missing-data-flush", dropped), ("early-root", early),
                           ("overwrite-protected-data", overwritten)):
        case, reason = first_bad(base, mutated)
        negatives.append((label, case, reason))

    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    logs = out / "serial"
    logs.mkdir()
    original = (sha256(root / "kernel/kernel"), sha256(root / "fs.img"))
    manifest = []
    with tempfile.TemporaryDirectory(prefix="directcore-replay-") as temp:
        tmp = Path(temp)
        run = Runner(root, args.qemu, tmp, args.backend, args.cpus, 60, logs, args.zicbom)

        def boot_patch(case, label, marker):
            image = tmp / "candidate.pmem"
            with image.open("wb") as target:
                target.write(whole)
                for off, data in case["patch"].items():
                    target.seek(RESERVED + off)
                    target.write(data)
            output = run.boot(image, label, "directcore which 1 2 128", marker)
            image.unlink()
            return output

        for i, (case, state) in enumerate(zip(images, states), 1):
            output = boot_patch(case, f"case-{i:03d}", rb"^DIRECTCORE_VERSION (old|new)\r?\n")
            actual = re.search(rb"^DIRECTCORE_VERSION (old|new)\r?$", output, re.MULTILINE).group(1).decode()
            if actual != state:
                raise RuntimeError(f"case {i}: kernel={actual} oracle={state}")
            manifest.append(serializable(case, state))
        negative_data = []
        for label, case, reason in negatives:
            boot_patch(case, f"negative-{label}", rb"^DIRECTCORE_TEST_FAIL reason=content\r?\n")
            negative_data.append({"name": label, "host_reason": reason,
                                  "case": serializable(case, "invalid")})
        if (sha256(root / "kernel/kernel"), sha256(root / "fs.img")) != original:
            raise RuntimeError("input kernel or root image changed")
    summary = {"protocol": protocol, "cases": len(images), "guest_reboots": len(images) + len(negatives),
               "backend": args.backend, "cpus": args.cpus, "zicbom": args.zicbom,
               "old": states.count("old"), "new": states.count("new"),
               "negative_controls": negative_data, "source_image_unchanged": True,
               "baseline_gzip_sha256": sha256(args.baseline), "final_gzip_sha256": sha256(args.final),
               "final_reconstruction": "exact complete NVDIMM image match",
               "trace_sha256": sha256(args.trace),
               "scope": "sampled source-level trace under explicit 8-byte atomic-root assumption; not physical power loss"}
    (out / "cases.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"DIRECTCORE_TRACE_REPLAY_PASS candidates={len(images)} old={states.count('old')} "
          f"new={states.count('new')} negative_controls=3 guest_reboots={len(images) + 3}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"DIRECTCORE_TRACE_REPLAY_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
