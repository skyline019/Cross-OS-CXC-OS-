#!/usr/bin/env python3
"""Audit one real QEMU 9.2 NVMe command trace against xv6 redo ordering.

Runs a disposable guest workload and checks serialized command callbacks,
namespace Flush, and log-data/header/home/clear stage order. A synthetic
missing-Flush negative control must fail the same checker. This observes QEMU
software events for one workload, not physical media persistence or tearing.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile

from nvme_boot_tests import boot, sha256
from xv6_log_recovery import BSIZE, DiskLog, disk_arguments

IO = re.compile(r"^pci_nvme_io_cmd cid (\d+) nsid 0x([0-9a-f]+) sqid (\d+) opc 0x([0-9a-f]+)\b")
WRITE = re.compile(r"^pci_nvme_write cid (\d+) .* nsid (\d+) nlb (\d+) count (\d+) lba 0x([0-9a-f]+)\b")
RW_CB = re.compile(r"^pci_nvme_rw_cb cid (\d+)\b")
FLUSH = re.compile(r"^pci_nvme_flush_ns nsid 0x([0-9a-f]+)\b")
MISC_CB = re.compile(r"^pci_nvme_misc_cb cid (\d+)\b")


def parse_trace(text, layout):
    """Return D/H/O/F tokens for completed writes/flushes; reads are checked."""
    pending, tokens, count = None, [], 0
    for line in text.splitlines():
        if m := IO.match(line):
            if pending is not None:
                raise ValueError("overlapping or incomplete I/O commands")
            cid, nsid, sqid, opc = int(m[1]), int(m[2], 16), int(m[3]), int(m[4], 16)
            if nsid != 1 or sqid != 1 or opc not in (0, 1, 2):
                raise ValueError("unexpected queue, namespace or I/O command")
            pending = {"cid": cid, "opc": opc, "detail": None}
            count += 1
        elif m := WRITE.match(line):
            if pending is None or pending["opc"] != 1 or pending["detail"] is not None:
                raise ValueError("write detail without matching command")
            cid, nsid, nlb, size, lba = int(m[1]), int(m[2]), int(m[3]), int(m[4]), int(m[5], 16)
            if (cid != pending["cid"] or nsid != 1 or nlb != 2 or size != BSIZE or
                    lba % 2 or lba // 2 >= layout.size):
                raise ValueError("unexpected write CID, size or address")
            pending["detail"] = lba // 2
        elif m := FLUSH.match(line):
            if pending is None or pending["opc"] != 0 or pending["detail"] is not None or int(m[1], 16) != 1:
                raise ValueError("flush backend event without matching namespace command")
            pending["detail"] = True
        elif m := RW_CB.match(line):
            if pending is None or pending["opc"] not in (1, 2) or int(m[1]) != pending["cid"]:
                raise ValueError("read/write callback without matching command")
            if pending["opc"] == 1:
                block = pending["detail"]
                if block is None:
                    raise ValueError("write callback without address")
                kind = ("H" if block == layout.start else
                        "D" if layout.start < block < layout.start + layout.nlog else "O")
                tokens.append((kind, block))
            pending = None
        elif m := MISC_CB.match(line):
            if (pending is None or pending["opc"] != 0 or int(m[1]) != pending["cid"] or
                    pending["detail"] is not True):
                raise ValueError("flush completion without backend flush")
            tokens.append(("F", None))
            pending = None
        elif line.startswith("pci_nvme_"):
            raise ValueError("unrecognized NVMe trace event: " + line)
    if pending is not None:
        raise ValueError("trace ends with an incomplete command")
    if not count:
        raise ValueError("no NVMe I/O events")
    return tokens, count


def audit_journal(tokens, layout):
    """Validate boot recovery then every complete journal transaction."""
    pos = 0

    def expect(kind):
        nonlocal pos
        if pos >= len(tokens) or tokens[pos][0] != kind:
            raise ValueError(f"expected {kind} at token {pos}, got {tokens[pos:pos + 1]}")
        block = tokens[pos][1]
        pos += 1
        return block

    # The source image has an empty log: recovery still flushes, clears the
    # empty header, and flushes again. This is not a recovered nonempty log.
    expect("F")
    if expect("H") != layout.start:
        raise ValueError("wrong boot header")
    expect("F")
    transactions = 0
    while pos < len(tokens):
        data_blocks = []
        while pos < len(tokens) and tokens[pos][0] == "D":
            data_blocks.append(expect("D"))
        if not data_blocks or data_blocks != list(range(layout.start + 1,
                                                        layout.start + 1 + len(data_blocks))):
            raise ValueError("log data blocks missing or non-sequential")
        expect("F")
        expect("H")
        expect("F")
        homes = [expect("O") for _ in data_blocks]
        if len(set(homes)) != len(homes):
            raise ValueError("duplicate home destination")
        expect("F")
        expect("H")
        expect("F")
        transactions += 1
    if not transactions:
        raise ValueError("trace has no nonempty journal transaction")
    return transactions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    image, kernel = root / "fs.img", root / "kernel/kernel"
    if args.timeout < 1 or not image.is_file() or not kernel.is_file():
        parser.error("need a built NVMe kernel, fs.img, and positive timeout")
    layout = DiskLog(image)
    if layout.header():
        parser.error("source image must have an empty redo header")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    trace, serial = out / "qemu-nvme.trace", out / "serial.log"
    if trace.exists() or serial.exists():
        parser.error("output trace/serial already exist; use a new output directory")
    original = sha256(image)
    with tempfile.TemporaryDirectory(prefix="nvme-trace-") as tmp:
        disk = Path(tmp) / "disk.img"
        shutil.copyfile(image, disk)
        events = ("trace:pci_nvme_io_cmd,trace:pci_nvme_write,trace:pci_nvme_rw_cb,"
                  "trace:pci_nvme_flush_ns,trace:pci_nvme_misc_cb")
        guest = boot(args.qemu, kernel, disk_arguments(disk, "nvme"), 1, args.timeout,
                     rb"^NVME_TRACE_OK\r?\n", "echo NVME_TRACE_OK > nvt; cat nvt", serial,
                     ["-d", events, "-D", str(trace)])
        if b"NVME_READY nsid=1 lba=512 " not in guest:
            raise ValueError("trace audit requires the 512-byte LBA configuration")
    if sha256(image) != original:
        raise ValueError("source image unexpectedly changed")
    tokens, io_count = parse_trace(trace.read_text(), layout)
    transactions = audit_journal(tokens, layout)
    first_data = next(i for i, token in enumerate(tokens) if token[0] == "D")
    missing_barrier = first_data
    while tokens[missing_barrier][0] == "D":
        missing_barrier += 1
    if tokens[missing_barrier][0] != "F":
        raise ValueError("first transaction lacks data barrier")
    mutated = tokens[:missing_barrier] + tokens[missing_barrier + 1:]
    try:
        audit_journal(mutated, layout)
    except ValueError:
        pass
    else:
        raise ValueError("negative control failed to detect missing flush")
    summary = {"io_commands": io_count, "journal_transactions": transactions,
               "write_tokens": sum(kind != "F" for kind, _ in tokens),
               "flush_tokens": sum(kind == "F" for kind, _ in tokens),
               "negative_controls": 1, "source_image_unchanged": True,
               "scope": "one QEMU 9.2 trace, not physical persistence or torn sectors"}
    (out / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print("NVME_TRACE_AUDIT_PASS " + json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"NVME_TRACE_AUDIT_FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
