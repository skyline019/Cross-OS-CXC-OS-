#!/usr/bin/env python3
"""Build ET_EXEC: nested mkdir + relative open on disk/ → NEST_PASS (M87)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_READ = 0
SYS_WRITE = 1
SYS_OPEN = 2
SYS_CLOSE = 3
SYS_GETCWD = 79
SYS_CHDIR = 80
SYS_MKDIR = 83
SYS_EXIT = 60
O_RDWR = 2
O_CREAT = 64
O_TRUNC = 512
LOAD_VA = 0x400000
PASS = b"NEST_PASS\n"
DIR_A = b"/disk/tree\0"
DIR_B = b"/disk/tree/sub\0"
DIR_BAD = b"/disk/nope/leaf\0"
FILE_REL = b"leaf.txt\0"
FILE_ABS = b"/disk/tree/sub/leaf.txt\0"
CHDIR_SUB = b"/disk/tree/sub\0"
EXPECT_CWD = b"/disk/tree/sub"
PAYLOAD = b"NEST"


def assemble() -> bytes:
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

    blob += b"\x48\x83\xEC\x40"

    # mkdir /disk/tree
    blob += b"\xB8" + struct.pack("<I", SYS_MKDIR)
    lea_rdi("dir_a")
    blob += b"\xBE\xED\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    # mkdir /disk/tree/sub
    blob += b"\xB8" + struct.pack("<I", SYS_MKDIR)
    lea_rdi("dir_b")
    blob += b"\xBE\xED\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    # Edge: mkdir without parent → fail (negative)
    blob += b"\xB8" + struct.pack("<I", SYS_MKDIR)
    lea_rdi("dir_bad")
    blob += b"\xBE\xED\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x89", "fail")  # jns fail — must be negative

    # chdir /disk/tree/sub
    blob += b"\xB8" + struct.pack("<I", SYS_CHDIR)
    lea_rdi("chdir_sub")
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    # getcwd → "/disk/tree/sub\0" len 15 (14 chars + NUL)
    blob += b"\xB8" + struct.pack("<I", SYS_GETCWD)
    blob += b"\x48\x89\xE7"
    blob += b"\xBE\x20\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\x0F"  # 15
    jcc(b"\x0F\x85", "fail")
    for i, ch in enumerate(EXPECT_CWD):
        if i == 0:
            blob += b"\x80\x3C\x24" + bytes([ch])
        else:
            blob += b"\x80\x7C\x24" + bytes([i, ch])
        jcc(b"\x0F\x85", "fail")

    # open leaf.txt relative
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("file_rel")
    blob += b"\xBE" + struct.pack("<I", O_RDWR | O_CREAT | O_TRUNC)
    blob += b"\xBA\xA4\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\x89\xDF"
    lea_rsi("payload")
    blob += b"\xBA" + struct.pack("<I", len(PAYLOAD))
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8" + bytes([len(PAYLOAD)])
    jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"

    # chdir / then open absolute and read
    blob += b"\xB8" + struct.pack("<I", SYS_CHDIR)
    lea_rdi("root")
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("file_abs")
    blob += b"\xBE\x00\x00\x00\x00"
    blob += b"\x31\xD2"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    blob += b"\xB8" + struct.pack("<I", SYS_READ)
    blob += b"\x89\xDF"
    blob += b"\x48\x89\xE6"
    blob += b"\xBA" + struct.pack("<I", len(PAYLOAD))
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8" + bytes([len(PAYLOAD)])
    jcc(b"\x0F\x85", "fail")
    for i, ch in enumerate(PAYLOAD):
        if i == 0:
            blob += b"\x80\x3C\x24" + bytes([ch])
        else:
            blob += b"\x80\x7C\x24" + bytes([i, ch])
        jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"

    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\xBF\x01\x00\x00\x00"
    lea_rsi("pass")
    blob += b"\xBA" + struct.pack("<I", len(PASS))
    blob += b"\xCD\x80"

    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\x31\xFF"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    fail_at = len(blob)
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    targets: dict[str, int] = {}
    targets["pass"] = len(blob)
    blob += PASS
    targets["dir_a"] = len(blob)
    blob += DIR_A
    targets["dir_b"] = len(blob)
    blob += DIR_B
    targets["dir_bad"] = len(blob)
    blob += DIR_BAD
    targets["file_rel"] = len(blob)
    blob += FILE_REL
    targets["file_abs"] = len(blob)
    blob += FILE_ABS
    targets["chdir_sub"] = len(blob)
    blob += CHDIR_SUB
    targets["root"] = len(blob)
    blob += b"/\0"
    targets["payload"] = len(blob)
    blob += PAYLOAD

    for label, at in jccs:
        struct.pack_into("<i", blob, at + 2, {"fail": fail_at}[label] - (at + 6))
    for kind, at in leas:
        assert blob[at : at + 3] in (b"\x48\x8D\x3D", b"\x48\x8D\x35")
        struct.pack_into("<i", blob, at + 3, targets[kind] - (at + 7))
    return bytes(blob)


def build_elf(payload: bytes) -> bytes:
    ehdr_size = 64
    phdr_size = 56
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
    args = parser.parse_args()
    data = build_elf(assemble())
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
