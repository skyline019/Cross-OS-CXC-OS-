#!/usr/bin/env python3
"""Build a tiny ET_EXEC that execve(\"disk/run.elf\", argv, NULL) (M80/M81)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_EXECVE = 59
SYS_EXIT = 60
LOAD_VA = 0x400000
PATH = b"disk/run.elf\0"
ARG1 = b"alpha\0"
ARG2 = b"beta\0"


def assemble() -> bytes:
    """
    Layout after code:
      path, arg1, arg2, argv_table[4] (path, arg1, arg2, NULL)
    """
    blob = bytearray()
    blob += b"\xB8" + struct.pack("<I", SYS_EXECVE)
    lea_path = len(blob)
    blob += b"\x48\x8D\x3D" + struct.pack("<i", 0)  # lea rdi, path(%rip)
    lea_argv = len(blob)
    blob += b"\x48\x8D\x35" + struct.pack("<i", 0)  # lea rsi, argv(%rip)
    blob += b"\x31\xD2"  # xor edx, edx  (envp=NULL)
    blob += b"\xCD\x80"
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"  # exit(1)
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    path_at = len(blob)
    blob += PATH
    arg1_at = len(blob)
    blob += ARG1
    arg2_at = len(blob)
    blob += ARG2

    # Align argv table to 8 bytes.
    while len(blob) % 8 != 0:
        blob += b"\x00"
    argv_at = len(blob)
    # File offsets → VAs once PHDR maps at LOAD_VA + file_off for payload.
    # We fix up after knowing file_off in build_elf; store relative placeholders
    # here as raw offsets from payload start; build_elf rewrites.
    blob += struct.pack("<QQQQ", path_at, arg1_at, arg2_at, 0)

    struct.pack_into("<i", blob, lea_path + 3, path_at - (lea_path + 7))
    struct.pack_into("<i", blob, lea_argv + 3, argv_at - (lea_argv + 7))
    return bytes(blob), path_at, arg1_at, arg2_at, argv_at


def build_elf(payload: bytes, path_at: int, arg1_at: int, arg2_at: int,
              argv_at: int) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    phoff = ehdr_size
    file_off = ehdr_size + phdr_size
    page = 4096
    memsz = (len(payload) + page - 1) & ~(page - 1)
    if memsz < page:
        memsz = page

    # Patch argv table to absolute VAs (LOAD_VA is also entry = file offset 0
    # of the LOAD segment start… entry is LOAD_VA, and payload starts at
    # LOAD_VA, so VA = LOAD_VA + offset_in_payload).
    body = bytearray(payload)
    struct.pack_into(
        "<QQQQ",
        body,
        argv_at,
        LOAD_VA + path_at,
        LOAD_VA + arg1_at,
        LOAD_VA + arg2_at,
        0,
    )

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
        len(body),
        memsz,
        page,
    )
    return ehdr + phdr + bytes(body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    payload, path_at, arg1_at, arg2_at, argv_at = assemble()
    data = build_elf(payload, path_at, arg1_at, arg2_at, argv_at)
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
