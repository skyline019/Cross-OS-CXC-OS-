#!/usr/bin/env python3
"""Fallback handmade ET_EXEC exercising lseek for hosts without ELF gcc."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_READ = 0
SYS_WRITE = 1
SYS_OPEN = 2
SYS_CLOSE = 3
SYS_LSEEK = 8
SYS_EXIT = 60
O_RDONLY = 0
O_WRONLY = 1
O_CREAT = 64
O_TRUNC = 512
OPEN_FLAGS = O_WRONLY | O_CREAT | O_TRUNC
SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2
LOAD_VA = 0x400000
NAME = b"seek.bin\0"
PAYLOAD = b"ABCDEFGH"
EXPECT = b"EFGH"
PASS = b"USER_LSEEK_PASS\n"


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def i32(value: int) -> bytes:
    return struct.pack("<i", value)


def i64(value: int) -> bytes:
    return struct.pack("<q", value)


def assemble() -> bytes:
    parts: list[bytes] = []

    def emit(chunk: bytes) -> int:
        offset = sum(len(part) for part in parts)
        parts.append(chunk)
        return offset

    # open(creat)
    emit(b"\xB8" + u32(SYS_OPEN))
    lea_name = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\xBE" + u32(OPEN_FLAGS))
    emit(b"\xBA" + u32(0o644))
    emit(b"\xCD\x80")
    emit(b"\x48\x89\xC3")  # rbx = fd

    # write(fd, payload, 8)
    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\x48\x89\xDF")
    lea_payload = emit(b"\x48\x8D\x35" + i32(0))
    emit(b"\xBA" + u32(8))
    emit(b"\xCD\x80")

    # close
    emit(b"\xB8" + u32(SYS_CLOSE))
    emit(b"\x48\x89\xDF")
    emit(b"\xCD\x80")

    # open rdonly
    emit(b"\xB8" + u32(SYS_OPEN))
    lea_name2 = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\x31\xF6")
    emit(b"\x31\xD2")
    emit(b"\xCD\x80")
    emit(b"\x48\x89\xC3")

    # lseek(fd, 4, SEEK_SET)
    emit(b"\xB8" + u32(SYS_LSEEK))
    emit(b"\x48\x89\xDF")
    emit(b"\xBE" + u32(4))
    emit(b"\xBA" + u32(SEEK_SET))
    emit(b"\xCD\x80")

    # read into rsp-16
    emit(b"\x48\x83\xEC\x10")
    emit(b"\xB8" + u32(SYS_READ))
    emit(b"\x48\x89\xDF")
    emit(b"\x48\x89\xE6")
    emit(b"\xBA" + u32(4))
    emit(b"\xCD\x80")

    # compare 4 bytes to EXPECT (inline)
    # mov eax,[rsp]; cmp eax, 'EFGH' little-endian
    emit(b"\x8B\x04\x24")  # mov eax, [rsp]
    emit(b"\x3D" + EXPECT)  # cmp eax, imm32
    jcc_ne = emit(b"\x75" + bytes([0]))  # jne fail (patched)

    # lseek END
    emit(b"\xB8" + u32(SYS_LSEEK))
    emit(b"\x48\x89\xDF")
    emit(b"\x31\xF6")
    emit(b"\xBA" + u32(SEEK_END))
    emit(b"\xCD\x80")

    # lseek CUR -3
    emit(b"\xB8" + u32(SYS_LSEEK))
    emit(b"\x48\x89\xDF")
    emit(b"\x48\xC7\xC6" + i32(-3))  # mov rsi, -3
    emit(b"\xBA" + u32(SEEK_CUR))
    emit(b"\xCD\x80")

    # close; write pass; exit 0
    emit(b"\xB8" + u32(SYS_CLOSE))
    emit(b"\x48\x89\xDF")
    emit(b"\xCD\x80")
    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\xBF" + u32(1))
    lea_pass = emit(b"\x48\x8D\x35" + i32(0))
    emit(b"\xBA" + u32(len(PASS)))
    emit(b"\xCD\x80")
    emit(b"\xB8" + u32(SYS_EXIT))
    emit(b"\x31\xFF")
    emit(b"\xCD\x80")
    fail_at = emit(b"\xB8" + u32(SYS_EXIT))
    emit(b"\xBF" + u32(1))
    emit(b"\xCD\x80")
    emit(b"\x0F\x0B")

    code_len = sum(len(part) for part in parts)
    name_off = code_len
    payload_off = name_off + len(NAME)
    pass_off = payload_off + len(PAYLOAD)

    blob = bytearray(b"".join(parts))
    # patch short jne: rel8 = fail_at - (jcc_ne + 2)
    blob[jcc_ne + 1] = (fail_at - (jcc_ne + 2)) & 0xFF
    for at, target in (
        (lea_name, name_off),
        (lea_name2, name_off),
        (lea_payload, payload_off),
        (lea_pass, pass_off),
    ):
        disp = target - (at + 7)
        blob[at + 3 : at + 7] = i32(disp)
    return bytes(blob) + NAME + PAYLOAD + PASS


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
    arguments = parser.parse_args()
    data = build_elf(assemble())
    arguments.output.write_bytes(data)
    print(f"wrote {arguments.output} ({len(data)} bytes, lseek fallback)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
