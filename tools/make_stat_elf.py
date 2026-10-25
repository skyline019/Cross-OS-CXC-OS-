#!/usr/bin/env python3
"""Fallback handmade ET_EXEC exercising path stat for hosts without ELF gcc."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_OPEN = 2
SYS_CLOSE = 3
SYS_STAT = 4
SYS_EXIT = 60
SYS_MKDIR = 83
O_WRONLY = 1
O_CREAT = 64
O_TRUNC = 512
OPEN_FLAGS = O_WRONLY | O_CREAT | O_TRUNC
S_IFREG = 0o100000
S_IFDIR = 0o040000
S_IFMT = 0o170000
LOAD_VA = 0x400000
FILE_NAME = b"meta.bin\0"
DIR_NAME = b"meta_dir\0"
PAYLOAD = b"ABCDEFGH"
PASS = b"USER_STAT_PASS\n"
STAT_SIZE = 144


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def i32(value: int) -> bytes:
    return struct.pack("<i", value)


def assemble() -> bytes:
    parts: list[bytes] = []

    def emit(chunk: bytes) -> int:
        offset = sum(len(part) for part in parts)
        parts.append(chunk)
        return offset

    # mkdir(dir)
    emit(b"\xB8" + u32(SYS_MKDIR))
    lea_dir = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\xBE" + u32(0o755))
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    j1 = emit(b"\x75" + bytes([0]))

    # open/write/close file
    emit(b"\xB8" + u32(SYS_OPEN))
    lea_file = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\xBE" + u32(OPEN_FLAGS))
    emit(b"\xBA" + u32(0o644))
    emit(b"\xCD\x80")
    emit(b"\x48\x89\xC3")

    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\x48\x89\xDF")
    lea_payload = emit(b"\x48\x8D\x35" + i32(0))
    emit(b"\xBA" + u32(8))
    emit(b"\xCD\x80")

    emit(b"\xB8" + u32(SYS_CLOSE))
    emit(b"\x48\x89\xDF")
    emit(b"\xCD\x80")

    # stat(file)
    emit(b"\x48\x81\xEC" + u32(STAT_SIZE))
    emit(b"\x48\x89\xE7")
    emit(b"\x31\xC0")
    emit(b"\xB9" + u32(STAT_SIZE))
    emit(b"\xF3\xAA")
    emit(b"\xB8" + u32(SYS_STAT))
    lea_file2 = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\x48\x89\xE6")
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    j2 = emit(b"\x75" + bytes([0]))
    emit(b"\x48\x83\x7C\x24\x30\x08")  # st_size == 8
    j3 = emit(b"\x75" + bytes([0]))
    emit(b"\x8B\x44\x24\x18")
    emit(b"\x25" + u32(S_IFMT))
    emit(b"\x3D" + u32(S_IFREG))
    j4 = emit(b"\x75" + bytes([0]))

    # stat(dir)
    emit(b"\x48\x89\xE7")
    emit(b"\x31\xC0")
    emit(b"\xB9" + u32(STAT_SIZE))
    emit(b"\xF3\xAA")
    emit(b"\xB8" + u32(SYS_STAT))
    lea_dir2 = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\x48\x89\xE6")
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    j5 = emit(b"\x75" + bytes([0]))
    emit(b"\x8B\x44\x24\x18")
    emit(b"\x25" + u32(S_IFMT))
    emit(b"\x3D" + u32(S_IFDIR))
    j6 = emit(b"\x75" + bytes([0]))

    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\xBF" + u32(1))
    lea_pass = emit(b"\x48\x8D\x35" + i32(0))
    emit(b"\xBA" + u32(len(PASS)))
    emit(b"\xCD\x80")
    emit(b"\xB8" + u32(SYS_EXIT))
    emit(b"\x31\xFF")
    emit(b"\xCD\x80")
    fail = emit(b"\xB8" + u32(SYS_EXIT))
    emit(b"\xBF" + u32(1))
    emit(b"\xCD\x80")
    emit(b"\x0F\x0B")

    code_len = sum(len(part) for part in parts)
    file_off = code_len
    dir_off = file_off + len(FILE_NAME)
    payload_off = dir_off + len(DIR_NAME)
    pass_off = payload_off + len(PAYLOAD)

    blob = bytearray(b"".join(parts))
    for at in (j1, j2, j3, j4, j5, j6):
        blob[at + 1] = (fail - (at + 2)) & 0xFF
    for at, target in (
        (lea_dir, dir_off),
        (lea_dir2, dir_off),
        (lea_file, file_off),
        (lea_file2, file_off),
        (lea_payload, payload_off),
        (lea_pass, pass_off),
    ):
        blob[at + 3 : at + 7] = i32(target - (at + 7))
    return bytes(blob) + FILE_NAME + DIR_NAME + PAYLOAD + PASS


def build_elf(payload: bytes) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    file_off = ehdr_size + phdr_size
    page = 4096
    memsz = max(page, (len(payload) + page - 1) & ~(page - 1))
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
    arguments = parser.parse_args()
    data = build_elf(assemble())
    arguments.output.write_bytes(data)
    print(f"wrote {arguments.output} ({len(data)} bytes, stat fallback)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
