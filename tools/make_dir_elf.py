#!/usr/bin/env python3
"""Build a minimal static ET_EXEC ELF64 that mkdir + nested open/read/write."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_READ = 0
SYS_WRITE = 1
SYS_OPEN = 2
SYS_CLOSE = 3
SYS_MKDIR = 83
SYS_EXIT = 60
O_WRONLY = 1
O_CREAT = 64
O_TRUNC = 512
OPEN_FLAGS = O_WRONLY | O_CREAT | O_TRUNC
LOAD_VA = 0x400000
DIR_NAME = b"data\0"
FILE_NAME = b"data/note.txt\0"
PASS = b"USER_DIR_PASS\n"


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

    def emit_js_near() -> int:
        # js rel32 — near form so the fail path can sit after a longer probe.
        return emit(b"\x0F\x88" + i32(0))

    # mkdir("data", 0755)
    emit(b"\xB8" + u32(SYS_MKDIR))
    lea_dir = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\xBE" + u32(0o755))
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    js_mkdir = emit_js_near()

    # open("data/note.txt", CREAT|WRONLY|TRUNC, 0644)
    emit(b"\xB8" + u32(SYS_OPEN))
    lea_file1 = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\xBE" + u32(OPEN_FLAGS))
    emit(b"\xBA" + u32(0o644))
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    js_open1 = emit_js_near()
    emit(b"\x48\x89\xC3")  # mov rbx, rax

    # write(fd, pass, len)
    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\x48\x89\xDF")
    lea_pass_w = emit(b"\x48\x8D\x35" + i32(0))
    emit(b"\xBA" + u32(len(PASS)))
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    js_write = emit_js_near()

    # close(fd)
    emit(b"\xB8" + u32(SYS_CLOSE))
    emit(b"\x48\x89\xDF")
    emit(b"\xCD\x80")

    # open("data/note.txt", O_RDONLY)
    emit(b"\xB8" + u32(SYS_OPEN))
    lea_file2 = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\x31\xF6")
    emit(b"\x31\xD2")
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    js_open2 = emit_js_near()
    emit(b"\x48\x89\xC3")

    # read(fd, rsp-64, 64)
    emit(b"\x48\x83\xEC\x40")
    emit(b"\xB8" + u32(SYS_READ))
    emit(b"\x48\x89\xDF")
    emit(b"\x48\x89\xE6")
    emit(b"\xBA" + u32(64))
    emit(b"\xCD\x80")
    emit(b"\x48\x85\xC0")
    js_read = emit_js_near()
    emit(b"\x49\x89\xC4")  # r12 = bytes

    # write(1, rsp, r12)
    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\xBF" + u32(1))
    emit(b"\x48\x89\xE6")
    emit(b"\x4C\x89\xE2")
    emit(b"\xCD\x80")

    # close; exit(0)
    emit(b"\xB8" + u32(SYS_CLOSE))
    emit(b"\x48\x89\xDF")
    emit(b"\xCD\x80")
    emit(b"\xB8" + u32(SYS_EXIT))
    emit(b"\x31\xFF")
    emit(b"\xCD\x80")
    emit(b"\x0F\x0B")

    fail = len(code)
    emit(b"\xB8" + u32(SYS_EXIT))
    emit(b"\xBF" + u32(1))
    emit(b"\xCD\x80")
    emit(b"\x0F\x0B")

    dir_off = len(code)
    file_off = dir_off + len(DIR_NAME)
    pass_off = file_off + len(FILE_NAME)

    def patch_js_near(at: int, target: int) -> None:
        # at points at 0F 88; disp32 at +2; rip = at+6
        code[at + 2 : at + 6] = i32(target - (at + 6))

    def patch_lea(at: int, target: int) -> None:
        code[at + 3 : at + 7] = i32(target - (at + 7))

    for at in (js_mkdir, js_open1, js_write, js_open2, js_read):
        patch_js_near(at, fail)
    patch_lea(lea_dir, dir_off)
    patch_lea(lea_file1, file_off)
    patch_lea(lea_file2, file_off)
    patch_lea(lea_pass_w, pass_off)

    return bytes(code) + DIR_NAME + FILE_NAME + PASS


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
