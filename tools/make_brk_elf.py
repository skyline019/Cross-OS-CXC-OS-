#!/usr/bin/env python3
"""Build a minimal static ET_EXEC ELF64 that grows the heap via brk."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_BRK = 12
SYS_EXIT = 60
LOAD_VA = 0x400000
PASS = b"USER_BRK_PASS\n"
MARKER = 0xA5


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

    def emit_jne_placeholder() -> int:
        return emit(b"\x75\x00")

    # brk(0) → rbx = current
    emit(b"\xB8" + u32(SYS_BRK))
    emit(b"\x31\xFF")
    emit(b"\xCD\x80")
    emit(b"\x48\x89\xC3")

    # brk(current + 0x1000); require rax == current+0x1000
    emit(b"\xB8" + u32(SYS_BRK))
    emit(b"\x48\x8D\xBB\x00\x10\x00\x00")
    emit(b"\xCD\x80")
    emit(b"\x48\x8D\x8B\x00\x10\x00\x00")
    emit(b"\x48\x39\xC1")
    jne_brk = emit_jne_placeholder()

    # *rbx = MARKER; require *rbx == MARKER
    emit(b"\xC6\x03" + bytes([MARKER]))
    emit(b"\x0F\xB6\x03")
    emit(b"\x3C" + bytes([MARKER]))
    jne_touch = emit_jne_placeholder()

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

    patch_rel8(jne_brk, fail)
    patch_rel8(jne_touch, fail)
    disp = pass_off - (lea_pass + 7)
    code[lea_pass + 3 : lea_pass + 7] = i32(disp)
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
