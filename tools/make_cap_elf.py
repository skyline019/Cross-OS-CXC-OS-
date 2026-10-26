#!/usr/bin/env python3
"""Build ET_EXEC: 96 KiB CXCFS write/read on disk/ → CAP_PASS (M88)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_READ = 0
SYS_WRITE = 1
SYS_OPEN = 2
SYS_CLOSE = 3
SYS_EXIT = 60
O_RDWR = 2
O_CREAT = 64
O_TRUNC = 512
LOAD_VA = 0x400000
PASS = b"CAP_PASS\n"
PATH = b"/disk/cap.bin\0"
# One user stack page: keep I/O buffer small (96 KiB = 384 × 256).
CHUNK = 256
CHUNKS = 384
STACK = 0x200  # buffer + scratch


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

    # rsp layout: [0..CHUNK) = I/O buffer
    blob += b"\x48\x81\xEC" + struct.pack("<I", STACK)

    # open(/disk/cap.bin, RDWR|CREAT|TRUNC, 0644)
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("path")
    blob += b"\xBE" + struct.pack("<I", O_RDWR | O_CREAT | O_TRUNC)
    blob += b"\xBA\xA4\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"  # ebx = fd

    # r12d = chunk index 0..CHUNKS-1
    blob += b"\x41\xBC\x00\x00\x00\x00"

    write_loop = len(blob)
    # memset buffer: first byte = chunk index, rest 0xA5
    blob += b"\x48\x89\xE7"  # rdi = rsp
    blob += b"\xB9" + struct.pack("<I", CHUNK)
    blob += b"\xB0\xA5"
    blob += b"\xF3\xAA"  # rep stosb
    blob += b"\x44\x88\x24\x24"  # [rsp] = r12b

    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\x89\xDF"
    blob += b"\x48\x89\xE6"
    blob += b"\xBA" + struct.pack("<I", CHUNK)
    blob += b"\xCD\x80"
    blob += b"\x48\x3D" + struct.pack("<I", CHUNK)
    jcc(b"\x0F\x85", "fail")

    blob += b"\x41\xFF\xC4"  # inc r12d
    blob += b"\x41\x81\xFC" + struct.pack("<I", CHUNKS)
    # jl write_loop
    at = len(blob)
    blob.extend(b"\x0F\x8C" + struct.pack("<i", 0))
    jccs.append(("write_loop", at))

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    # reopen RDONLY
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("path")
    blob += b"\xBE\x00\x00\x00\x00"
    blob += b"\x31\xD2"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    blob += b"\x41\xBC\x00\x00\x00\x00"
    read_loop = len(blob)

    blob += b"\xB8" + struct.pack("<I", SYS_READ)
    blob += b"\x89\xDF"
    blob += b"\x48\x89\xE6"
    blob += b"\xBA" + struct.pack("<I", CHUNK)
    blob += b"\xCD\x80"
    blob += b"\x48\x3D" + struct.pack("<I", CHUNK)
    jcc(b"\x0F\x85", "fail")

    # verify [rsp] == r12b and [rsp+1] == 0xA5
    blob += b"\x0F\xB6\x04\x24"  # movzx eax, [rsp]
    blob += b"\x44\x38\xE0"  # cmp al, r12b
    jcc(b"\x0F\x85", "fail")
    blob += b"\x80\x7C\x24\x01\xA5"
    jcc(b"\x0F\x85", "fail")

    blob += b"\x41\xFF\xC4"
    blob += b"\x41\x81\xFC" + struct.pack("<I", CHUNKS)
    at = len(blob)
    blob.extend(b"\x0F\x8C" + struct.pack("<i", 0))
    jccs.append(("read_loop", at))

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
    targets["path"] = len(blob)
    blob += PATH

    label_at = {"fail": fail_at, "write_loop": write_loop, "read_loop": read_loop}
    for label, at in jccs:
        struct.pack_into("<i", blob, at + 2, label_at[label] - (at + 6))
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
