#!/usr/bin/env python3
"""Build ET_EXEC: print LOAD128_PASS (M89; padded on disk to >64 KiB)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_EXIT = 60
LOAD_VA = 0x400000
PASS = b"LOAD128_PASS\n"


def assemble() -> bytes:
    blob = bytearray()
    lea_at = 0

    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\xBF\x01\x00\x00\x00"
    lea_at = len(blob)
    blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
    blob += b"\xBA" + struct.pack("<I", len(PASS))
    blob += b"\xCD\x80"

    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\x31\xFF"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    pass_at = len(blob)
    blob += PASS
    struct.pack_into("<i", blob, lea_at + 3, pass_at - (lea_at + 7))
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
