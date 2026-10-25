#!/usr/bin/env python3
"""Build ET_EXEC: execve(\"disk/run.elf\", argv, envp) (M80–M82)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_EXECVE = 59
SYS_EXIT = 60
LOAD_VA = 0x400000
PATH = b"disk/run.elf\0"
ARG1 = b"alpha\0"
ARG2 = b"beta\0"
ENV0 = b"CXC_M82=1\0"
ENV1 = b"PATH=/disk\0"


def assemble() -> tuple[bytes, dict[str, int]]:
    blob = bytearray()
    blob += b"\xB8" + struct.pack("<I", SYS_EXECVE)
    lea_path = len(blob)
    blob += b"\x48\x8D\x3D" + struct.pack("<i", 0)  # lea rdi, path
    lea_argv = len(blob)
    blob += b"\x48\x8D\x35" + struct.pack("<i", 0)  # lea rsi, argv
    lea_envp = len(blob)
    blob += b"\x48\x8D\x15" + struct.pack("<i", 0)  # lea rdx, envp
    blob += b"\xCD\x80"
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    marks: dict[str, int] = {}
    marks["path"] = len(blob)
    blob += PATH
    marks["arg1"] = len(blob)
    blob += ARG1
    marks["arg2"] = len(blob)
    blob += ARG2
    marks["env0"] = len(blob)
    blob += ENV0
    marks["env1"] = len(blob)
    blob += ENV1

    while len(blob) % 8 != 0:
        blob += b"\x00"
    marks["argv"] = len(blob)
    blob += struct.pack("<QQQQ", marks["path"], marks["arg1"], marks["arg2"], 0)
    marks["envp"] = len(blob)
    blob += struct.pack("<QQQ", marks["env0"], marks["env1"], 0)

    def patch_lea(at: int, target: int) -> None:
        struct.pack_into("<i", blob, at + 3, target - (at + 7))

    patch_lea(lea_path, marks["path"])
    patch_lea(lea_argv, marks["argv"])
    patch_lea(lea_envp, marks["envp"])
    return bytes(blob), marks


def build_elf(payload: bytes, marks: dict[str, int]) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    phoff = ehdr_size
    file_off = ehdr_size + phdr_size
    page = 4096
    memsz = (len(payload) + page - 1) & ~(page - 1)
    if memsz < page:
        memsz = page

    body = bytearray(payload)
    struct.pack_into(
        "<QQQQ",
        body,
        marks["argv"],
        LOAD_VA + marks["path"],
        LOAD_VA + marks["arg1"],
        LOAD_VA + marks["arg2"],
        0,
    )
    struct.pack_into(
        "<QQQ",
        body,
        marks["envp"],
        LOAD_VA + marks["env0"],
        LOAD_VA + marks["env1"],
        0,
    )

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
        len(body),
        memsz,
        page,
    )
    return ehdr + phdr + bytes(body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    payload, marks = assemble()
    data = build_elf(payload, marks)
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
