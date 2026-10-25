#!/usr/bin/env python3
"""Build a minimal static ET_EXEC ELF64 that exercises open/read/write/close."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_READ = 0
SYS_WRITE = 1
SYS_OPEN = 2
SYS_CLOSE = 3
SYS_EXIT = 60
O_WRONLY = 1
O_CREAT = 64
O_TRUNC = 512
OPEN_FLAGS = O_WRONLY | O_CREAT | O_TRUNC  # 577
LOAD_VA = 0x400000
NAME = b"note.txt\0"
PASS = b"USER_FS_PASS\n"


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def i32(value: int) -> bytes:
    return struct.pack("<i", value)


def assemble() -> bytes:
    """Emit position-independent code; rip-relative slots filled at the end."""

    # Layout (filled after emission):
    #   code...
    #   name
    #   pass
    parts: list[bytes] = []

    def emit(chunk: bytes) -> int:
        offset = sum(len(part) for part in parts)
        parts.append(chunk)
        return offset

    # rbx = fd across syscalls (saved/restored by interrupt frame).
    # open(name, OPEN_FLAGS, 0644)
    emit(b"\xB8" + u32(SYS_OPEN))  # mov eax, SYS_OPEN
    lea_name = emit(b"\x48\x8D\x3D" + i32(0))  # lea rdi, name(%rip)
    emit(b"\xBE" + u32(OPEN_FLAGS))  # mov esi, flags
    emit(b"\xBA" + u32(0o644))  # mov edx, 0644
    emit(b"\xCD\x80")  # int 0x80
    emit(b"\x48\x89\xC3")  # mov rbx, rax

    # write(fd, pass, len)
    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\x48\x89\xDF")  # mov rdi, rbx
    lea_pass_w = emit(b"\x48\x8D\x35" + i32(0))  # lea rsi, pass(%rip)
    emit(b"\xBA" + u32(len(PASS)))
    emit(b"\xCD\x80")

    # close(fd)
    emit(b"\xB8" + u32(SYS_CLOSE))
    emit(b"\x48\x89\xDF")
    emit(b"\xCD\x80")

    # open(name, O_RDONLY, 0)
    emit(b"\xB8" + u32(SYS_OPEN))
    lea_name2 = emit(b"\x48\x8D\x3D" + i32(0))
    emit(b"\x31\xF6")  # xor esi, esi
    emit(b"\x31\xD2")  # xor edx, edx
    emit(b"\xCD\x80")
    emit(b"\x48\x89\xC3")  # mov rbx, rax

    # read(fd, rsp-64, 64) — use stack buffer
    emit(b"\x48\x83\xEC\x40")  # sub rsp, 64
    emit(b"\xB8" + u32(SYS_READ))
    emit(b"\x48\x89\xDF")
    emit(b"\x48\x89\xE6")  # mov rsi, rsp
    emit(b"\xBA" + u32(64))
    emit(b"\xCD\x80")
    emit(b"\x49\x89\xC4")  # mov r12, rax  (bytes read)

    # write(1, rsp, r12)
    emit(b"\xB8" + u32(SYS_WRITE))
    emit(b"\xBF" + u32(1))
    emit(b"\x48\x89\xE6")
    emit(b"\x4C\x89\xE2")  # mov rdx, r12
    emit(b"\xCD\x80")

    # close(fd); exit(0)
    emit(b"\xB8" + u32(SYS_CLOSE))
    emit(b"\x48\x89\xDF")
    emit(b"\xCD\x80")
    emit(b"\xB8" + u32(SYS_EXIT))
    emit(b"\x31\xFF")
    emit(b"\xCD\x80")
    emit(b"\x0F\x0B")  # ud2

    code_len = sum(len(part) for part in parts)
    name_off = code_len
    pass_off = name_off + len(NAME)

    def patch_lea(at: int, target: int) -> None:
        # lea is 7 bytes; disp32 is at +3; rip = at+7
        disp = target - (at + 7)
        parts_flat = bytearray(b"".join(parts))
        parts_flat[at + 3 : at + 7] = i32(disp)
        # rebuild parts as single blob for simplicity after first patch batch
        parts.clear()
        parts.append(bytes(parts_flat))

    # First join then patch all leas
    blob = bytearray(b"".join(parts))
    for at, target in (
        (lea_name, name_off),
        (lea_pass_w, pass_off),
        (lea_name2, name_off),
    ):
        disp = target - (at + 7)
        blob[at + 3 : at + 7] = i32(disp)

    return bytes(blob) + NAME + PASS


def build_elf(payload: bytes) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    phoff = ehdr_size
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
        len(payload),
        memsz,
        page,
    )
    return ehdr + phdr + payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    payload = assemble()
    data = build_elf(payload)
    arguments.output.write_bytes(data)
    print(f"wrote {arguments.output} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
