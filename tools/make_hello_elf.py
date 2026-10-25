#!/usr/bin/env python3
"""Build a minimal static ET_EXEC ELF64 hello for the M38 user probe."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

# Linux x86-64 numbers kept so later musl/A2 work can reuse the ABI surface.
SYS_WRITE = 1
SYS_EXIT = 60
LOAD_VA = 0x400000
PASS = b"USER_PROGRAM_PASS\n"

# Machine code at LOAD_VA (rip-relative lea to msg).
#   mov $1,%eax; mov $1,%edi; lea msg(%rip),%rsi; mov $len,%edx; int $0x80
#   mov $60,%eax; xor %edi,%edi; int $0x80; ud2; msg
CODE = bytes(
    [
        0xB8,
        SYS_WRITE,
        0x00,
        0x00,
        0x00,
        0xBF,
        0x01,
        0x00,
        0x00,
        0x00,
        0x48,
        0x8D,
        0x35,
        0x12,
        0x00,
        0x00,
        0x00,  # disp = msg - (lea+7) = 0x12
        0xBA,
        len(PASS),
        0x00,
        0x00,
        0x00,
        0xCD,
        0x80,
        0xB8,
        SYS_EXIT,
        0x00,
        0x00,
        0x00,
        0x31,
        0xFF,
        0xCD,
        0x80,
        0x0F,
        0x0B,
    ]
) + PASS

assert CODE[0x0A:0x11] == bytes([0x48, 0x8D, 0x35, 0x12, 0x00, 0x00, 0x00])
assert CODE[0x23:] == PASS


def build_elf(payload: bytes) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    phoff = ehdr_size
    file_off = ehdr_size + phdr_size
    # Pad payload to a page so memsz/filesz stay simple for the loader.
    page = 4096
    filesz = file_off + len(payload)
    # Align file payload start; keep PT_LOAD p_offset == file_off.
    memsz = (len(payload) + page - 1) & ~(page - 1)
    if memsz < page:
        memsz = page

    e_entry = LOAD_VA
    # p_vaddr is LOAD_VA; p_offset is file_off → file bytes map to VA.
    # First file_off bytes of the segment are ehdr+phdr (not executed; entry
    # skips them). Simpler: put payload at p_offset 0 by embedding only
    # code in the load image and setting p_offset=file_off, p_vaddr=LOAD_VA.

    ehdr = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF"
        + bytes([2, 1, 1, 0])
        + bytes(8),  # EI_*
        2,  # ET_EXEC
        62,  # EM_X86_64
        1,  # EV_CURRENT
        e_entry,
        phoff,
        0,  # shoff
        0,  # flags
        ehdr_size,
        phdr_size,
        1,  # phnum
        0,  # shentsize
        0,  # shnum
        0,  # shstrndx
    )
    assert len(ehdr) == ehdr_size

    phdr = struct.pack(
        "<IIQQQQQQ",
        1,  # PT_LOAD
        5,  # PF_R|PF_X
        file_off,
        LOAD_VA,
        LOAD_VA,
        len(payload),
        memsz,
        page,
    )
    assert len(phdr) == phdr_size
    return ehdr + phdr + payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    data = build_elf(CODE)
    arguments.output.write_bytes(data)
    print(f"wrote {arguments.output} ({len(data)} bytes, entry=0x{LOAD_VA:x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
