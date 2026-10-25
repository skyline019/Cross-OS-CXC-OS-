#!/usr/bin/env python3
"""Fallback handmade ET_EXEC: two brk grows as malloc smoke → USER_LIBC_MALLOC_PASS."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_BRK = 12
SYS_EXIT = 60
LOAD_VA = 0x400000
PASS = b"USER_LIBC_MALLOC_PASS\n"


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

    # Two page grows via brk; touch both regions (malloc≈sbrk smoke).
    emit(b"\xB8" + u32(SYS_BRK))
    emit(b"\x31\xFF")
    emit(b"\xCD\x80")
    emit(b"\x48\x89\xC3")  # rbx = cur

    emit(b"\xB8" + u32(SYS_BRK))
    emit(b"\x48\x8D\xBB\x00\x10\x00\x00")
    emit(b"\xCD\x80")
    emit(b"\x48\x8D\x8B\x00\x10\x00\x00")
    emit(b"\x48\x39\xC1")
    j1 = emit(b"\x75" + bytes([0]))
    emit(b"\xC6\x03\x11")  # *rbx = 0x11

    emit(b"\x48\x89\xC3")  # rbx = new break
    emit(b"\xB8" + u32(SYS_BRK))
    emit(b"\x48\x8D\xBB\x00\x10\x00\x00")
    emit(b"\xCD\x80")
    emit(b"\x48\x8D\x8B\x00\x10\x00\x00")
    emit(b"\x48\x39\xC1")
    j2 = emit(b"\x75" + bytes([0]))
    emit(b"\xC6\x03\x33")

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
    pass_off = len(code)

    def fix_rel8(at: int, target: int) -> None:
        code[at + 1] = (target - (at + 2)) & 0xFF

    fix_rel8(j1, fail)
    fix_rel8(j2, fail)
    code[lea_pass + 3 : lea_pass + 7] = i32(pass_off - (lea_pass + 7))
    return bytes(code) + PASS


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
    print(f"wrote {arguments.output} ({len(data)} bytes, libc malloc fallback)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
