#!/usr/bin/env python3
"""Fallback handmade ET_EXEC: open/write/read/close → USER_LIBC_FS_PASS."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_READ = 0
SYS_WRITE = 1
SYS_OPEN = 2
SYS_CLOSE = 3
SYS_EXIT = 60
O_RDONLY = 0
O_WRONLY = 1
O_CREAT = 64
O_TRUNC = 512
OPEN_FLAGS = O_WRONLY | O_CREAT | O_TRUNC
LOAD_VA = 0x400000
PATH = b"libcfs.txt\0"
PAYLOAD = b"LIBC-FS"
PASS = b"USER_LIBC_FS_PASS\n"


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

    # Buffer on stack; fd in rbx.
    emit(b"\x48\x83\xEC\x10")  # sub rsp, 16

    emit(b"\xB8" + u32(SYS_OPEN))
    lea_path = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\xBE" + u32(OPEN_FLAGS))
    emit(b"\xBA" + u32(0o644))
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    j1 = emit(b"\x78" + bytes([0]))
    emit(b"\x48\x89\xC3")  # mov rbx, rax

    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\x48\x89\xDF")
    lea_pay = emit(b"\x48\x8D\x35" + i32(0))
    emit(b"\xBA" + u32(len(PAYLOAD)))
    emit(b"\xCD\x80")
    emit(b"\x48\x3D" + u32(len(PAYLOAD)))
    j2 = emit(b"\x75" + bytes([0]))

    emit(b"\xB8" + u32(SYS_CLOSE))
    emit(b"\x48\x89\xDF")
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    j3 = emit(b"\x75" + bytes([0]))

    emit(b"\xB8" + u32(SYS_OPEN))
    lea_path2 = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\xBE" + u32(O_RDONLY))
    emit(b"\x31\xD2")
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    j4 = emit(b"\x78" + bytes([0]))
    emit(b"\x48\x89\xC3")

    emit(b"\xB8" + u32(SYS_READ))
    emit(b"\x48\x89\xDF")
    emit(b"\x48\x89\xE6")
    emit(b"\xBA" + u32(len(PAYLOAD)))
    emit(b"\xCD\x80")
    emit(b"\x48\x3D" + u32(len(PAYLOAD)))
    j5 = emit(b"\x75" + bytes([0]))

    # Compare [rsp+i] with payload[i]
    emit(b"\x31\xC9")  # xor ecx, ecx
    cmp_top = emit(b"")  # placeholder offset = current
    cmp_top = sum(len(part) for part in parts)
    emit(b"\x8A\x04\x0C")  # mov al, [rsp+rcx]
    lea_pay2 = emit(b"\x48\x8D\x35" + i32(0))
    emit(b"\x3A\x04\x0E")  # cmp al, [rsi+rcx]
    j6 = emit(b"\x75" + bytes([0]))
    emit(b"\xFF\xC1")  # inc ecx
    emit(b"\x83\xF9" + bytes([len(PAYLOAD)]))
    j_back = emit(b"\x72" + bytes([0]))

    emit(b"\xB8" + u32(SYS_CLOSE))
    emit(b"\x48\x89\xDF")
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    j7 = emit(b"\x75" + bytes([0]))

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
    pay_off = path_off + len(PATH)
    pass_off = pay_off + len(PAYLOAD)

    def fix_lea(at: int, target: int) -> None:
        code[at + 3 : at + 7] = i32(target - (at + 7))

    def fix_rel8(at: int, target: int) -> None:
        code[at + 1] = (target - (at + 2)) & 0xFF

    for at in (j1, j2, j3, j4, j5, j6, j7):
        fix_rel8(at, fail)
    fix_rel8(j_back, cmp_top)
    fix_lea(lea_path, path_off)
    fix_lea(lea_path2, path_off)
    fix_lea(lea_pay, pay_off)
    fix_lea(lea_pay2, pay_off)
    fix_lea(lea_pass, pass_off)
    return bytes(code) + PATH + PAYLOAD + PASS


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
    print(f"wrote {arguments.output} ({len(data)} bytes, libc fs fallback)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
