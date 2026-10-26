#!/usr/bin/env python3
"""Build ET_EXEC: access + clock_gettime + gettimeofday → TIME_PASS (M91)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_OPEN = 2
SYS_CLOSE = 3
SYS_ACCESS = 21
SYS_GETTIMEOFDAY = 96
SYS_CLOCK_GETTIME = 228
SYS_EXIT = 60
O_RDWR = 2
O_CREAT = 64
O_TRUNC = 512
F_OK = 0
R_OK = 4
CLOCK_REALTIME = 0
CLOCK_MONOTONIC = 1
CLOCK_BAD = 99
LOAD_VA = 0x400000
PASS = b"TIME_PASS\n"
EXIST = b"/timeok.txt\0"
MISSING = b"/no_such_time\0"
STACK = 0x40


def assemble() -> bytes:
    blob = bytearray()
    jccs: list[tuple[str, int]] = []
    leas: list[tuple[str, int]] = []

    def jcc(op: bytes, label: str) -> None:
        at = len(blob)
        blob.extend(op + b"\x00\x00\x00\x00")
        jccs.append((label, at))

    def lea_rdi(kind: str) -> None:
        at = len(blob)
        blob.extend(b"\x48\x8D\x3D" + struct.pack("<i", 0))
        leas.append((kind, at))

    def lea_rsi(kind: str) -> None:
        at = len(blob)
        blob.extend(b"\x48\x8D\x35" + struct.pack("<i", 0))
        leas.append((kind, at))

    blob += b"\x48\x83\xEC" + bytes([STACK])

    # creat /timeok.txt
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("exist")
    blob += b"\xBE" + struct.pack("<I", O_RDWR | O_CREAT | O_TRUNC)
    blob += b"\xBA\xA4\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"
    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"

    # access(exist, F_OK) == 0
    blob += b"\xB8" + struct.pack("<I", SYS_ACCESS)
    lea_rdi("exist")
    blob += b"\xBE" + struct.pack("<I", F_OK)
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x85", "fail")

    # access(exist, R_OK) == 0
    blob += b"\xB8" + struct.pack("<I", SYS_ACCESS)
    lea_rdi("exist")
    blob += b"\xBE" + struct.pack("<I", R_OK)
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x85", "fail")

    # access(missing, F_OK) → negative
    blob += b"\xB8" + struct.pack("<I", SYS_ACCESS)
    lea_rdi("missing")
    blob += b"\xBE" + struct.pack("<I", F_OK)
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x89", "fail")  # jns fail

    # clock_gettime(MONOTONIC, rsp)
    blob += b"\xB8" + struct.pack("<I", SYS_CLOCK_GETTIME)
    blob += b"\xBF" + struct.pack("<I", CLOCK_MONOTONIC)
    blob += b"\x48\x89\xE6"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x85", "fail")
    # nsec < 1e9
    blob += b"\x48\x8B\x44\x24\x08"
    blob += b"\x48\x3D" + struct.pack("<I", 1000000000)
    jcc(b"\x0F\x83", "fail")  # jae fail

    # clock_gettime(REALTIME, rsp) — tv_sec >= epoch
    blob += b"\xB8" + struct.pack("<I", SYS_CLOCK_GETTIME)
    blob += b"\xBF" + struct.pack("<I", CLOCK_REALTIME)
    blob += b"\x48\x89\xE6"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x85", "fail")
    blob += b"\x48\x8B\x04\x24"
    blob += b"\x48\x3D" + struct.pack("<I", 1700000000)
    jcc(b"\x0F\x82", "fail")  # jb fail

    # clock_gettime(bad) → -EINVAL
    blob += b"\xB8" + struct.pack("<I", SYS_CLOCK_GETTIME)
    blob += b"\xBF" + struct.pack("<I", CLOCK_BAD)
    blob += b"\x48\x89\xE6"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x89", "fail")

    # gettimeofday(rsp, NULL)
    blob += b"\xB8" + struct.pack("<I", SYS_GETTIMEOFDAY)
    blob += b"\x48\x89\xE7"
    blob += b"\x31\xF6"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x85", "fail")
    # usec < 1000000
    blob += b"\x48\x8B\x44\x24\x08"
    blob += b"\x48\x3D" + struct.pack("<I", 1000000)
    jcc(b"\x0F\x83", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\xBF\x01\x00\x00\x00"
    lea_rsi("pass")
    blob += b"\xBA" + struct.pack("<I", len(PASS))
    blob += b"\xCD\x80"

    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\x31\xFF"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    fail_at = len(blob)
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    targets = {
        "fail": fail_at,
        "pass": len(blob),
    }
    blob += PASS
    targets["exist"] = len(blob)
    blob += EXIST
    targets["missing"] = len(blob)
    blob += MISSING

    for label, at in jccs:
        struct.pack_into("<i", blob, at + 2, targets[label] - (at + 6))
    for kind, at in leas:
        struct.pack_into("<i", blob, at + 3, targets[kind] - (at + 7))
    return bytes(blob)


def build_elf(payload: bytes) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    file_off = ehdr_size + phdr_size
    page = 4096
    memsz = (len(payload) + page - 1) & ~(page - 1)
    if memsz < page:
        memsz = page
    ehdr = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8),
        2,
        62,
        1,
        LOAD_VA,
        ehdr_size,
        0,
        0,
        ehdr_size,
        phdr_size,
        1,
        0,
        0,
        0,
    )
    phdr = struct.pack(
        "<IIQQQQQQ",
        1,
        5,
        file_off,
        LOAD_VA,
        LOAD_VA,
        len(payload),
        memsz,
        page,
    )
    return ehdr + phdr + payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = build_elf(assemble())
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
