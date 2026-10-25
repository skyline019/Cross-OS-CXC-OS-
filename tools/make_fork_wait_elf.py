#!/usr/bin/env python3
"""Build ET_EXEC: fork, child exit(42), parent wait4 + FORK_WAIT_PASS (M83)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_FORK = 57
SYS_EXIT = 60
SYS_WAIT4 = 61
LOAD_VA = 0x400000
PASS = b"FORK_WAIT_PASS\n"
CHILD_MARK = b"FORK_CHILD\n"


def assemble() -> bytes:
    blob = bytearray()
    jccs: list[tuple[str, int]] = []

    def jcc(op: bytes, label: str) -> None:
        at = len(blob)
        blob.extend(op + b"\x00\x00\x00\x00")
        jccs.append((label, at))

    # status slot on stack: push 0
    blob += b"\x6A\x00"  # push 0
    # fork
    blob += b"\xB8" + struct.pack("<I", SYS_FORK)
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"  # test rax,rax
    jcc(b"\x0F\x84", "child")  # je child
    jcc(b"\x0F\x88", "fail")  # js fail

    # wait4(-1, rsp, 0, 0) — status at [rsp]
    blob += b"\xB8" + struct.pack("<I", SYS_WAIT4)
    blob += b"\x48\xC7\xC7\xFF\xFF\xFF\xFF"  # rdi=-1
    blob += b"\x48\x89\xE6"  # mov rsi, rsp
    blob += b"\x31\xD2"  # edx=0
    blob += b"\x4D\x31\xC0"  # r8=0
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x8E", "fail")  # jle fail

    # cmp dword [rsp], 42<<8
    blob += b"\x81\x3C\x24" + struct.pack("<I", 42 << 8)
    jcc(b"\x0F\x85", "fail")  # jne fail

    # second wait4 → -10
    blob += b"\xB8" + struct.pack("<I", SYS_WAIT4)
    blob += b"\x48\xC7\xC7\xFF\xFF\xFF\xFF"
    blob += b"\x48\x89\xE6"
    blob += b"\x31\xD2"
    blob += b"\x4D\x31\xC0"
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\xF6"  # cmp rax, -10
    jcc(b"\x0F\x85", "fail")

    # write PASS
    lea_pass = len(blob)
    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
    blob += b"\xBA" + struct.pack("<I", len(PASS))
    blob += b"\xCD\x80"
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\x31\xFF"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    child_at = len(blob)
    lea_child = len(blob)
    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
    blob += b"\xBA" + struct.pack("<I", len(CHILD_MARK))
    blob += b"\xCD\x80"
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x2A\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    fail_at = len(blob)
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    pass_at = len(blob)
    blob += PASS
    child_msg_at = len(blob)
    blob += CHILD_MARK

    labels = {"child": child_at, "fail": fail_at}
    for label, at in jccs:
        struct.pack_into("<i", blob, at + 2, labels[label] - (at + 6))

    # lea_pass: mov eax(5) + mov edi(5) = +10 → 48 8d 35
    pass_lea = lea_pass + 10
    assert blob[pass_lea : pass_lea + 3] == b"\x48\x8D\x35"
    struct.pack_into("<i", blob, pass_lea + 3, pass_at - (pass_lea + 7))
    child_lea = lea_child + 10
    assert blob[child_lea : child_lea + 3] == b"\x48\x8D\x35"
    struct.pack_into("<i", blob, child_lea + 3, child_msg_at - (child_lea + 7))
    return bytes(blob)


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
        5,  # PF_R|X
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
