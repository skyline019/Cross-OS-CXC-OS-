#!/usr/bin/env python3
"""Build page-aligned ET_EXEC: mov $42,%edi; mov $60,%eax; syscall (exit 42).

M260 capacity gate — avoids musl PT_LOAD quirks; still a real static ELF.
Pad on-disk in the probe to >1 MiB.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

LOAD_VA = 0x400000
# mov edi, 42; mov eax, 60; int 0x80; ud2  (match CXC int80 exit ABI)
CODE = b"\xBF\x2A\x00\x00\x00\xB8\x3C\x00\x00\x00\xCD\x80\x0F\x0B"


def build() -> bytes:
    ehdr_size = 64
    phdr_size = 56
    file_off = ehdr_size + phdr_size
    page = 4096
    # Place code at start of first page after headers in file at offset page.
    # Single page-aligned PT_LOAD: file off 0 includes ehdr+phdr+pad+code.
    body = bytearray(page)
    body[0:len(CODE)] = CODE  # will relocate: actually code at VA LOAD_VA
    # Simpler: headers + code contiguous, one RX PT_LOAD at LOAD_VA.
    payload = bytearray(CODE)
    while len(payload) % 16:
        payload += b"\x90"
    memsz = page
    ehdr = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8),
        2,  # ET_EXEC
        62,
        1,
        LOAD_VA + file_off,  # entry = load + code offset in segment
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
    # PT_LOAD covers ehdr+phdr+code from file offset 0 → VA LOAD_VA
    filesz = file_off + len(payload)
    phdr = struct.pack(
        "<IIQQQQQQ",
        1,  # PT_LOAD
        5,  # PF_R|PF_X
        0,  # offset
        LOAD_VA,
        LOAD_VA,
        filesz,
        memsz,
        page,
    )
    raw = bytearray(ehdr + phdr + bytes(payload))
    # Fix entry: code starts at file_off
    struct.pack_into("<Q", raw, 24, LOAD_VA + file_off)
    return bytes(raw)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    args = parser.parse_args()
    data = build()
    args.output.write_bytes(data)
    parts = [f"0x{b:02x}" for b in data]
    lines = ["    " + ", ".join(parts[i : i + 12]) + "," for i in range(0, len(parts), 12)]
    args.header.write_text(
        "/* M260: page-aligned static exit(42) ELF; pad on-disk >1MiB. */\n"
        "#pragma once\n"
        "static const unsigned char fat42_elf[] = {\n"
        + "\n".join(lines)
        + "\n};\n"
        + f"static const unsigned fat42_elf_size = {len(data)};\n",
        encoding="ascii",
    )
    print(f"wrote {args.output} ({len(data)} bytes) and {args.header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
