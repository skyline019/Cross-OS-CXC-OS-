#!/usr/bin/env python3
"""Build ET_EXEC: mkdir/chdir/getcwd + relative open + edges → CHDIR_PASS (M86)."""

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
PASS = b"CHDIR_PASS\n"
DIR = b"cwdtest\0"
NOTE = b"note.txt\0"
ABS_NOTE = b"/cwdtest/note.txt\0"
DOTDOT = b"..\0"
MISSING = b"no_such_dir\0"
EXPECT_CWD = b"/cwdtest"
PAYLOAD = b"HI"


def assemble() -> bytes:
    blob = bytearray()
    jccs: list[tuple[str, int]] = []
    leas: list[tuple[str, int]] = []  # (kind, lea_insn_at)

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

    blob += b"\x48\x83\xEC\x40"  # sub rsp, 0x40

    # mkdir("cwdtest", 0755)
    blob += b"\xB8" + struct.pack("<I", SYS_MKDIR)
    lea_rdi("dir")
    blob += b"\xBE\xED\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    # chdir("cwdtest")
    blob += b"\xB8" + struct.pack("<I", SYS_CHDIR)
    lea_rdi("dir")
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    # getcwd(rsp, 32) → 9 bytes including NUL
    blob += b"\xB8" + struct.pack("<I", SYS_GETCWD)
    blob += b"\x48\x89\xE7"
    blob += b"\xBE\x20\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\x09"
    jcc(b"\x0F\x85", "fail")
    for i, ch in enumerate(EXPECT_CWD):
        if i == 0:
            blob += b"\x80\x3C\x24" + bytes([ch])
        else:
            blob += b"\x80\x7C\x24" + bytes([i, ch])
        jcc(b"\x0F\x85", "fail")

    # open("note.txt", CREAT|TRUNC|RDWR, 0644)
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("note")
    blob += b"\xBE" + struct.pack("<I", O_RDWR | O_CREAT | O_TRUNC)
    blob += b"\xBA\xA4\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\x89\xDF"
    lea_rsi("payload")
    blob += b"\xBA\x02\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\x02"
    jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"

    # chdir("..")
    blob += b"\xB8" + struct.pack("<I", SYS_CHDIR)
    lea_rdi("dotdot")
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    # open("/cwdtest/note.txt") RDONLY
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("absnote")
    blob += b"\xBE\x00\x00\x00\x00"
    blob += b"\x31\xD2"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    blob += b"\xB8" + struct.pack("<I", SYS_READ)
    blob += b"\x89\xDF"
    blob += b"\x48\x89\xE6"
    blob += b"\xBA\x02\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\x02"
    jcc(b"\x0F\x85", "fail")
    blob += b"\x80\x3C\x24\x48"
    jcc(b"\x0F\x85", "fail")
    blob += b"\x80\x7C\x24\x01\x49"
    jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"

    # chdir missing → -2
    blob += b"\xB8" + struct.pack("<I", SYS_CHDIR)
    lea_rdi("missing")
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\xFE"
    jcc(b"\x0F\x85", "fail")

    # getcwd(rsp, 1) → -34
    blob += b"\xB8" + struct.pack("<I", SYS_GETCWD)
    blob += b"\x48\x89\xE7"
    blob += b"\xBE\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\xDE"
    jcc(b"\x0F\x85", "fail")

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

    targets = {
        "pass": len(blob),
    }
    blob += PASS
    targets["dir"] = len(blob)
    blob += DIR
    targets["note"] = len(blob)
    blob += NOTE
    targets["absnote"] = len(blob)
    blob += ABS_NOTE
    targets["dotdot"] = len(blob)
    blob += DOTDOT
    targets["missing"] = len(blob)
    blob += MISSING
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
