#!/usr/bin/env python3
"""Build ET_EXEC: execve(\"disk/fat.elf\") with long argv (M259–M260)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_EXECVE = 59
SYS_EXIT = 60
LOAD_VA = 0x400000
PATH = b"disk/fat.elf\0"
# Prove argc≥64 and one arg ≥1024 (stack has 256 KiB).
N_ARGS = 64
LONG_ARG = b"X" * 1024 + b"\0"


def assemble() -> tuple[bytes, dict[str, int]]:
    blob = bytearray()
    blob += b"\xB8" + struct.pack("<I", SYS_EXECVE)
    lea_path = len(blob)
    blob += b"\x48\x8D\x3D" + struct.pack("<i", 0)
    lea_argv = len(blob)
    blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
    lea_envp = len(blob)
    blob += b"\x48\x8D\x15" + struct.pack("<i", 0)
    blob += b"\xCD\x80"
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    marks: dict[str, int] = {}
    marks["path"] = len(blob)
    blob += PATH
    while len(blob) % 8 != 0:
        blob += b"\x00"
    marks["long"] = len(blob)
    blob += LONG_ARG
    while len(blob) % 8 != 0:
        blob += b"\x00"
    marks["argv"] = len(blob)
    # argv[0]=path, argv[1]=long, argv[2..63]=path (reuse), then NULL
    for i in range(N_ARGS):
        if i == 1:
            blob += struct.pack("<Q", marks["long"])
        else:
            blob += struct.pack("<Q", marks["path"])
    blob += struct.pack("<Q", 0)
    marks["envp"] = len(blob)
    blob += struct.pack("<Q", 0)

    def patch_lea(at: int, target: int) -> None:
        struct.pack_into("<i", blob, at + 3, target - (at + 7))

    patch_lea(lea_path, marks["path"])
    patch_lea(lea_argv, marks["argv"])
    patch_lea(lea_envp, marks["envp"])
    return bytes(blob), marks


def build_elf(payload: bytes, marks: dict[str, int]) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    file_off = ehdr_size + phdr_size
    page = 4096
    body = bytearray(payload)
    # Patch argv VAs
    for i in range(N_ARGS):
        off = marks["argv"] + i * 8
        if i == 1:
            struct.pack_into("<Q", body, off, LOAD_VA + marks["long"])
        else:
            struct.pack_into("<Q", body, off, LOAD_VA + marks["path"])
    struct.pack_into("<Q", body, marks["argv"] + N_ARGS * 8, 0)
    struct.pack_into("<Q", body, marks["envp"], 0)
    memsz = (len(body) + page - 1) & ~(page - 1)
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
        len(body),
        memsz,
        page,
    )
    return ehdr + phdr + bytes(body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    payload, marks = assemble()
    data = build_elf(payload, marks)
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
