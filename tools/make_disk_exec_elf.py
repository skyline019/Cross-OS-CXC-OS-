#!/usr/bin/env python3
"""Build a larger static ET_EXEC: verify execve argv, print M80/M81 markers."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_EXIT = 60
LOAD_VA = 0x400000
ARGV_PASS = b"DISK_ARGV_PASS\n"
EXEC_PASS = b"DISK_EXEC_PASS\n"
EXPECT0 = b"disk/run.elf\0"
EXPECT1 = b"alpha\0"
EXPECT2 = b"beta\0"
# M81: prove multi-page CXCFS ELF load (well under 32 KiB file cap).
PAD_TO = 20 * 1024


def assemble() -> bytes:
    """
    Entry ABI (Linux process image): [rsp]=argc, [rsp+8]=argv0, …
    Expect argc==3 and argv strings disk/run.elf / alpha / beta.
    """
    blob = bytearray()
    patches: list[tuple[str, int]] = []

    def jne_near(label: str) -> None:
        at = len(blob)
        blob.extend(b"\x0F\x85\x00\x00\x00\x00")
        patches.append((label, at))

    def patch_lea(lea_at: int, target: int) -> None:
        struct.pack_into("<i", blob, lea_at + 3, target - (lea_at + 7))

    # cmp qword [rsp], 3 ; jne fail
    blob += b"\x48\x83\x3C\x24\x03"
    jne_near("fail")

    lea_expects: list[int] = []
    for argv_index in range(3):
        blob.extend(b"\x48\x8B\x5C\x24" + bytes([8 + 8 * argv_index]))
        lea_at = len(blob)
        blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
        lea_expects.append(lea_at)
        loop = len(blob)
        blob += b"\xAC"  # lodsb
        blob += b"\x3A\x03"  # cmp al, (%rbx)
        jne_near("fail")
        blob += b"\x48\xFF\xC3"  # inc rbx
        blob += b"\x84\xC0"  # test al, al
        jnz_at = len(blob)
        blob += b"\x75\x00"
        struct.pack_into("b", blob, jnz_at + 1, loop - (jnz_at + 2))

    lea_msgs: list[int] = []
    for _ in range(2):
        blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
        blob += b"\xBF\x01\x00\x00\x00"
        lea_at = len(blob)
        blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
        lea_msgs.append(lea_at)
        # length patched after strings known — placeholder
        blob += b"\xBA\x00\x00\x00\x00"
        blob += b"\xCD\x80"

    # After lea rsi: opcode BA + imm32 length.
    len_patch0 = lea_msgs[0] + 8
    len_patch1 = lea_msgs[1] + 8

    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\x31\xFF"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    fail_at = len(blob)
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x02\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    for label, at in patches:
        assert label == "fail"
        # 0F 85 rel32 — rel from next insn (at+6)
        struct.pack_into("<i", blob, at + 2, fail_at - (at + 6))

    e0 = len(blob)
    blob += EXPECT0
    e1 = len(blob)
    blob += EXPECT1
    e2 = len(blob)
    blob += EXPECT2
    a_at = len(blob)
    blob += ARGV_PASS
    x_at = len(blob)
    blob += EXEC_PASS

    patch_lea(lea_expects[0], e0)
    patch_lea(lea_expects[1], e1)
    patch_lea(lea_expects[2], e2)
    patch_lea(lea_msgs[0], a_at)
    patch_lea(lea_msgs[1], x_at)
    struct.pack_into("<I", blob, len_patch0, len(ARGV_PASS))
    struct.pack_into("<I", blob, len_patch1, len(EXEC_PASS))

    if len(blob) > PAD_TO:
        raise SystemExit(f"payload {len(blob)} exceeds PAD_TO {PAD_TO}")
    pad = bytearray(PAD_TO - len(blob))
    for i in range(len(pad)):
        pad[i] = (i * 131 + 17) & 0xFF
    blob += pad
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
