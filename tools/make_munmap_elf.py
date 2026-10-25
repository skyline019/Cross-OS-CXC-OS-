#!/usr/bin/env python3
"""Build a minimal static ET_EXEC ELF64 that mmap + munmap one anonymous page."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_MMAP = 9
SYS_MUNMAP = 11
SYS_EXIT = 60
PROT_READ = 1
PROT_WRITE = 2
MAP_PRIVATE = 0x02
MAP_ANONYMOUS = 0x20
LOAD_VA = 0x400000
PASS = b"USER_MUNMAP_PASS\n"
MARKER = 0x5A


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def i32(value: int) -> bytes:
    return struct.pack("<i", value)


def assemble() -> bytes:
    code = bytearray()

    def emit(chunk: bytes) -> int:
        at = len(code)
        code.extend(chunk)
        return at

    def emit_js() -> int:
        return emit(b"\x78\x00")

    def emit_jne() -> int:
        return emit(b"\x75\x00")

    # mmap(NULL, 4096, RW, PRIVATE|ANON, -1, 0)
    emit(b"\xB8" + u32(SYS_MMAP))
    emit(b"\x31\xFF")
    emit(b"\xBE" + u32(4096))
    emit(b"\xBA" + u32(PROT_READ | PROT_WRITE))
    emit(b"\x41\xBA" + u32(MAP_PRIVATE | MAP_ANONYMOUS))
    emit(b"\x49\xC7\xC0\xFF\xFF\xFF\xFF")
    emit(b"\x4D\x31\xC9")
    emit(b"\xCD\x80")
    emit(b"\x48\x89\xC3")
    emit(b"\x48\x85\xC0")
    js_mmap = emit_js()

    # touch
    emit(b"\xC6\x03" + bytes([MARKER]))
    emit(b"\x0F\xB6\x03")
    emit(b"\x3C" + bytes([MARKER]))
    jne_touch = emit_jne()

    # munmap(rbx, 4096)
    emit(b"\xB8" + u32(SYS_MUNMAP))
    emit(b"\x48\x89\xDF")
    emit(b"\xBE" + u32(4096))
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    js_munmap = emit_js()

    # write(1, pass, len)
    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\xBF" + u32(1))
    lea_pass = emit(b"\x48\x8D\x35" + i32(0))
    emit(b"\xBA" + u32(len(PASS)))
    emit(b"\xCD\x80")

    # exit(0)
    emit(b"\xB8" + u32(SYS_EXIT))
    emit(b"\x31\xFF")
    emit(b"\xCD\x80")
    emit(b"\x0F\x0B")

    fail = len(code)
    emit(b"\xB8" + u32(SYS_EXIT))
    emit(b"\xBF" + u32(1))
    emit(b"\xCD\x80")
    emit(b"\x0F\x0B")

    pass_off = len(code)

    def patch_rel8(at: int, target: int) -> None:
        disp = target - (at + 2)
        assert -128 <= disp <= 127, (at, target, disp)
        code[at + 1] = disp & 0xFF

    patch_rel8(js_mmap, fail)
    patch_rel8(jne_touch, fail)
    patch_rel8(js_munmap, fail)
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
    print(f"wrote {arguments.output} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
