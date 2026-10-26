#!/usr/bin/env python3
"""Build ET_EXEC on-box 'cc': read disk/in.c → write disk/out.elf → execve it (M92/A2)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_READ = 0
SYS_WRITE = 1
SYS_OPEN = 2
SYS_CLOSE = 3
SYS_EXECVE = 59
SYS_EXIT = 60
O_RDONLY = 0
O_WRONLY = 1
O_CREAT = 64
O_TRUNC = 512
LOAD_VA = 0x400000
SRC_PATH = b"disk/in.c\0"
OUT_PATH = b"disk/out.elf\0"
MAGIC = b"CXC_A2"
PASS = b"ONBOX_CC_PASS\n"  # emitted by nested out.elf after execve


def build_out_elf() -> bytes:
    """Minimal ET_EXEC: write ONBOX_CC_PASS and exit(0)."""
    code = bytearray()
    code += b"\xB8" + struct.pack("<I", SYS_WRITE)
    code += b"\xBF\x01\x00\x00\x00"
    lea = len(code)
    code += b"\x48\x8D\x35" + struct.pack("<i", 0)
    code += b"\xBA" + struct.pack("<I", len(PASS))
    code += b"\xCD\x80"
    code += b"\xB8" + struct.pack("<I", SYS_EXIT)
    code += b"\x31\xFF"
    code += b"\xCD\x80"
    code += b"\x0F\x0B"
    pass_at = len(code)
    code += PASS
    struct.pack_into("<i", code, lea + 3, pass_at - (lea + 7))

    ehdr_size = 64
    phdr_size = 56
    file_off = ehdr_size + phdr_size
    page = 4096
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
        len(code),
        memsz,
        page,
    )
    return ehdr + phdr + bytes(code)


def assemble(out_elf: bytes) -> bytes:
    blob = bytearray()
    jccs: list[tuple[str, int]] = []
    leas: list[tuple[str, int]] = []

    def jcc(op: bytes, label: str) -> None:
        at = len(blob)
        blob.extend(op + b"\x00\x00\x00\x00")
        jccs.append((label, at))

    def lea_rdi(kind: str) -> None:
        at = len(blob)
        blob.extend(b"\x48\x8D\x3D" + struct.pack("<i", 0))
        leas.append((kind, at))

    def lea_rsi(kind: str) -> None:
        at = len(blob)
        blob.extend(b"\x48\x8D\x35" + struct.pack("<i", 0))
        leas.append((kind, at))

    # rsp buffer for reading in.c
    blob += b"\x48\x81\xEC\x00\x01\x00\x00"  # sub rsp, 0x100

    # open(disk/in.c, RDONLY)
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("src")
    blob += b"\xBE" + struct.pack("<I", O_RDONLY)
    blob += b"\x31\xD2"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    # read(fd, rsp, 0x100)
    blob += b"\xB8" + struct.pack("<I", SYS_READ)
    blob += b"\x89\xDF"
    blob += b"\x48\x89\xE6"
    blob += b"\xBA\x00\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\x06"  # at least MAGIC len
    jcc(b"\x0F\x8C", "fail")  # jl fail
    blob += b"\x89\xC1"  # ecx = nread (unused beyond check)

    # verify MAGIC at rsp
    for i, ch in enumerate(MAGIC):
        if i == 0:
            blob += b"\x80\x3C\x24" + bytes([ch])
        else:
            blob += b"\x80\x7C\x24" + bytes([i, ch])
        jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"

    # open(disk/out.elf, WRONLY|CREAT|TRUNC, 0755)
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("out")
    blob += b"\xBE" + struct.pack("<I", O_WRONLY | O_CREAT | O_TRUNC)
    blob += b"\xBA\xED\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    # write embedded out.elf
    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\x89\xDF"
    lea_rsi("payload")
    blob += b"\xBA" + struct.pack("<I", len(out_elf))
    blob += b"\xCD\x80"
    blob += b"\x48\x3D" + struct.pack("<I", len(out_elf))
    jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    # execve(disk/out.elf, argv={out,0}, envp={0})
    blob += b"\xB8" + struct.pack("<I", SYS_EXECVE)
    lea_rdi("out")
    lea_rsi("argv")
    # lea rdx, envp — use lea_rsi pattern for rdx
    at = len(blob)
    blob.extend(b"\x48\x8D\x15" + struct.pack("<i", 0))
    leas.append(("envp", at))
    blob += b"\xCD\x80"

    # execve failure
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    fail_at = len(blob)
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    targets: dict[str, int] = {"fail": fail_at}
    targets["src"] = len(blob)
    blob += SRC_PATH
    targets["out"] = len(blob)
    blob += OUT_PATH
    while len(blob) % 8 != 0:
        blob += b"\x00"
    targets["argv"] = len(blob)
    # argv placeholders — patched after knowing LOAD_VA offsets relative to blob
    argv_at = len(blob)
    blob += struct.pack("<QQ", 0, 0)
    targets["envp"] = len(blob)
    blob += struct.pack("<Q", 0)
    targets["payload"] = len(blob)
    blob += out_elf

    for label, at in jccs:
        struct.pack_into("<i", blob, at + 2, targets[label] - (at + 6))
    for kind, at in leas:
        struct.pack_into("<i", blob, at + 3, targets[kind] - (at + 7))

    # Fix argv[0] = &out string VA = LOAD_VA + targets["out"]
    # When building ELF, payload is at LOAD_VA; marks are relative to payload start.
    # We'll patch in build_elf after knowing file layout — store relative offsets.
    return bytes(blob), targets, argv_at


def build_elf(payload: bytes, targets: dict[str, int], argv_at: int) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    file_off = ehdr_size + phdr_size
    page = 4096
    body = bytearray(payload)
    # Absolute VAs for argv/envp tables
    struct.pack_into(
        "<QQ",
        body,
        argv_at,
        LOAD_VA + targets["out"],
        0,
    )
    struct.pack_into("<Q", body, targets["envp"], 0)

    memsz = (len(body) + page - 1) & ~(page - 1)
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
        len(body),
        memsz,
        page,
    )
    return ehdr + phdr + bytes(body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out_elf = build_out_elf()
    payload, targets, argv_at = assemble(out_elf)
    data = build_elf(payload, targets, argv_at)
    args.output.write_bytes(data)
    print(
        f"wrote {args.output} ({len(data)} bytes; out.elf embed {len(out_elf)})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
