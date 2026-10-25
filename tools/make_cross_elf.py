#!/usr/bin/env python3
"""Fallback handmade ET_EXEC for hosts without an ELF-capable gcc (e.g. MinGW)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_EXIT = 60
LOAD_VA = 0x400000
PASS = b"USER_CROSS_ELF_PASS\n"

CODE = bytes(
    [
        0xB8,
        SYS_WRITE,
        0x00,
        0x00,
        0x00,
        0xBF,
        0x01,
        0x00,
        0x00,
        0x00,
        0x48,
        0x8D,
        0x35,
        0x12,
        0x00,
        0x00,
        0x00,
        0xBA,
        len(PASS),
        0x00,
        0x00,
        0x00,
        0xCD,
        0x80,
        0xB8,
        SYS_EXIT,
        0x00,
        0x00,
        0x00,
        0x31,
        0xFF,
        0xCD,
        0x80,
        0x0F,
        0x0B,
    ]
) + PASS


def build_elf(payload: bytes) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    phoff = ehdr_size
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
        phoff,
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
    arguments = parser.parse_args()
    data = build_elf(CODE)
    arguments.output.write_bytes(data)
    print(f"wrote {arguments.output} ({len(data)} bytes, fallback handmade)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
