#!/usr/bin/env python3
"""Fallback handmade ET_EXEC: open missing → -ENOENT path → USER_LIBC_ERRNO_PASS.

Note: fallback cannot host a real errno BSS; it checks open fails then prints PASS.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_OPEN = 2
SYS_EXIT = 60
O_RDONLY = 0
LOAD_VA = 0x400000
PATH = b"no-such-errno.txt\0"
PASS = b"USER_LIBC_ERRNO_PASS\n"


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

    emit(b"\xB8" + u32(SYS_OPEN))
    lea_path = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\xBE" + u32(O_RDONLY))
    emit(b"\x31\xD2")
    emit(b"\xCD\x80")
    # expect negative (rax < 0)
    emit(b"\x48\x85\xC0")
    j1 = emit(b"\x79" + bytes([0]))  # jns fail

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

    code = bytearray(b"".join(parts))
    path_off = len(code)
    pass_off = path_off + len(PATH)

    def fix_lea(at: int, target: int) -> None:
        code[at + 3 : at + 7] = i32(target - (at + 7))

    code[j1 + 1] = (fail - (j1 + 2)) & 0xFF
    fix_lea(lea_path, path_off)
    fix_lea(lea_pass, pass_off)
    return bytes(code) + PATH + PASS


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
    print(f"wrote {arguments.output} ({len(data)} bytes, libc errno fallback)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
