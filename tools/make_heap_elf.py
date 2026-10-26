#!/usr/bin/env python3
"""Build ET_EXEC: brk 768 KiB + mmap 512 KiB touch → HEAP_PASS (M90)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_MMAP = 9
SYS_BRK = 12
SYS_EXIT = 60
PROT_READ = 1
PROT_WRITE = 2
MAP_PRIVATE = 0x02
MAP_ANONYMOUS = 0x20
PAGE = 4096
BRK_PAGES = 192  # 768 KiB — above legacy 16-page ceiling
MMAP_PAGES = 128  # 512 KiB — above legacy 16-page ceiling
LOAD_VA = 0x400000
PASS = b"HEAP_PASS\n"
MARKER = 0x5A


def assemble() -> bytes:
    blob = bytearray()
    jccs: list[tuple[str, int]] = []
    leas: list[tuple[str, int]] = []

    def jcc(op: bytes, label: str) -> None:
        at = len(blob)
        blob.extend(op + b"\x00\x00\x00\x00")
        jccs.append((label, at))

    def lea_rsi(kind: str) -> None:
        at = len(blob)
        blob.extend(b"\x48\x8D\x35" + struct.pack("<i", 0))
        leas.append((kind, at))

    # brk(0)
    blob += b"\xB8" + struct.pack("<I", SYS_BRK)
    blob += b"\x31\xFF"
    blob += b"\xCD\x80"
    blob += b"\x48\x89\xC3"

    # brk(cur + 768KiB)
    blob += b"\xB8" + struct.pack("<I", SYS_BRK)
    blob += b"\x48\x89\xDF"
    blob += b"\x48\x81\xC7" + struct.pack("<I", BRK_PAGES * PAGE)
    blob += b"\xCD\x80"
    blob += b"\x48\x39\xC7"
    jcc(b"\x0F\x85", "fail")

    blob += b"\x49\x89\xDC"  # mov r12, rbx
    blob += b"\xB9" + struct.pack("<I", BRK_PAGES)
    brk_touch = len(blob)
    blob += b"\x41\xC6\x04\x24" + bytes([MARKER])
    blob += b"\x41\x0F\xB6\x04\x24"
    blob += b"\x3C" + bytes([MARKER])
    jcc(b"\x0F\x85", "fail")
    blob += b"\x49\x81\xC4" + struct.pack("<I", PAGE)
    blob += b"\xFF\xC9"
    jcc(b"\x0F\x85", "brk_touch")

    # mmap(NULL, 512KiB, RW, PRIVATE|ANON, -1, 0)
    blob += b"\xB8" + struct.pack("<I", SYS_MMAP)
    blob += b"\x31\xFF"
    blob += b"\xBE" + struct.pack("<I", MMAP_PAGES * PAGE)
    blob += b"\xBA" + struct.pack("<I", PROT_READ | PROT_WRITE)
    blob += b"\x41\xBA" + struct.pack("<I", MAP_PRIVATE | MAP_ANONYMOUS)
    blob += b"\x49\xC7\xC0\xFF\xFF\xFF\xFF"
    blob += b"\x4D\x31\xC9"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x49\x89\xC4"
    blob += b"\xB9" + struct.pack("<I", MMAP_PAGES)
    mmap_touch = len(blob)
    blob += b"\x41\xC6\x04\x24" + bytes([MARKER])
    blob += b"\x41\x0F\xB6\x04\x24"
    blob += b"\x3C" + bytes([MARKER])
    jcc(b"\x0F\x85", "fail")
    blob += b"\x49\x81\xC4" + struct.pack("<I", PAGE)
    blob += b"\xFF\xC9"
    jcc(b"\x0F\x85", "mmap_touch")

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
        "brk_touch": brk_touch,
        "mmap_touch": mmap_touch,
        "pass": len(blob),
    }
    blob += PASS

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
