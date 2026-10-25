#!/usr/bin/env python3
"""Fallback handmade ET_EXEC: ARCH_SET_FS/GET_FS smoke + USER_ARCH_PRCTL_PASS."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_EXIT = 60
SYS_ARCH_PRCTL = 158
ARCH_SET_FS = 0x1002
ARCH_GET_FS = 0x1003
LOAD_VA = 0x400000
PASS = b"USER_ARCH_PRCTL_PASS\n"
MAGIC = 0xC0FFEE42


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

    # stack: [rsp]=tls magic slot, [rsp+8]=got
    emit(b"\x48\x83\xEC\x10")  # sub rsp, 16
    emit(b"\x48\xC7\x04\x24" + u32(MAGIC))  # mov qword [rsp], MAGIC

    # arch_prctl(SET_FS, rsp)
    emit(b"\xB8" + u32(SYS_ARCH_PRCTL))
    emit(b"\xBF" + u32(ARCH_SET_FS))
    emit(b"\x48\x89\xE6")  # mov rsi, rsp
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    j1 = emit(b"\x75" + bytes([0]))

    # arch_prctl(GET_FS, rsp+8)
    emit(b"\xB8" + u32(SYS_ARCH_PRCTL))
    emit(b"\xBF" + u32(ARCH_GET_FS))
    emit(b"\x48\x8D\x74\x24\x08")  # lea rsi, [rsp+8]
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    j2 = emit(b"\x75" + bytes([0]))

    # cmp [rsp+8], rsp
    emit(b"\x48\x8B\x44\x24\x08")
    emit(b"\x48\x39\xE0")  # cmp rax, rsp
    j3 = emit(b"\x75" + bytes([0]))

    # mov rax, fs:0; cmp MAGIC
    emit(b"\x64\x48\x8B\x04\x25\x00\x00\x00\x00")  # mov rax, fs:0
    emit(b"\x48\x3D" + u32(MAGIC))
    j4 = emit(b"\x75" + bytes([0]))

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
    pass_off = code_len
    blob = bytearray(b"".join(parts))
    for at in (j1, j2, j3, j4):
        blob[at + 1] = (fail - (at + 2)) & 0xFF
    blob[lea_pass + 3 : lea_pass + 7] = i32(pass_off - (lea_pass + 7))
    return bytes(blob) + PASS


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
    print(f"wrote {arguments.output} ({len(data)} bytes, arch_prctl fallback)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
