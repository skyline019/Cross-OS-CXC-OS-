#!/usr/bin/env python3
"""Fat stub: execve(disk/fat.elf) with argc=64 + 1024-byte arg.
On execve failure, exit with -rax so serial shows the kernel failure code.
"""
from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SYS_EXECVE, SYS_EXIT, LOAD_VA = 59, 60, 0x400000
PATH = b"disk/fat.elf\0"
LONG = b"X" * 1024 + b"\0"
N = 64


def main() -> int:
    blob = bytearray()
    blob += b"\xB8" + struct.pack("<I", SYS_EXECVE)
    lea_path = len(blob)
    blob += b"\x48\x8D\x3D" + struct.pack("<i", 0)
    lea_argv = len(blob)
    blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
    lea_envp = len(blob)
    blob += b"\x48\x8D\x15" + struct.pack("<i", 0)
    blob += b"\xCD\x80"
    # execve failed: exit(-rax) so probe sees failure id
    blob += b"\x48\xF7\xD8"  # neg rax
    blob += b"\x48\x89\xC7"  # mov rdi, rax
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    marks: dict[str, int] = {}
    marks["path"] = len(blob)
    blob += PATH
    while len(blob) % 8:
        blob += b"\x00"
    marks["long"] = len(blob)
    blob += LONG
    while len(blob) % 8:
        blob += b"\x00"
    marks["argv"] = len(blob)
    for i in range(N):
        blob += struct.pack("<Q", marks["long"] if i == 1 else marks["path"])
    blob += struct.pack("<Q", 0)
    marks["envp"] = len(blob)
    blob += struct.pack("<Q", 0)

    def patch(at: int, target: int) -> None:
        struct.pack_into("<i", blob, at + 3, target - (at + 7))

    patch(lea_path, marks["path"])
    patch(lea_argv, marks["argv"])
    patch(lea_envp, marks["envp"])

    ehdr_size, phdr_size, page = 64, 56, 4096
    file_off = ehdr_size + phdr_size
    body = bytearray(blob)
    for i in range(N):
        off = marks["argv"] + i * 8
        va = LOAD_VA + (marks["long"] if i == 1 else marks["path"])
        struct.pack_into("<Q", body, off, va)
    struct.pack_into("<Q", body, marks["argv"] + N * 8, 0)
    struct.pack_into("<Q", body, marks["envp"], 0)
    memsz = max((len(body) + page - 1) & ~(page - 1), page)
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
        len(body),
        memsz,
        page,
    )
    data = ehdr + phdr + bytes(body)
    (ROOT / "user/prebuilt/fat_stub.elf").write_bytes(data)
    parts = [f"0x{b:02x}" for b in data]
    lines = [
        "    " + ", ".join(parts[i : i + 12]) + ","
        for i in range(0, len(parts), 12)
    ]
    (ROOT / "include/cxc/fat_stub_elf.hpp").write_text(
        "/* M259-M260: execve disk/fat.elf argc=64 + 1024-arg; fail exits -rax. */\n"
        "#pragma once\n"
        "static const unsigned char fat_stub_elf[] = {\n"
        + "\n".join(lines)
        + "\n};\n"
        + f"static const unsigned fat_stub_elf_size = {len(data)};\n",
        encoding="ascii",
    )
    print("stub", len(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
