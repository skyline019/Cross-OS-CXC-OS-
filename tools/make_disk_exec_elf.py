#!/usr/bin/env python3
"""Build ET_EXEC: verify execve argv+envp, print M80–M82 markers."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_WRITE = 1
SYS_EXIT = 60
LOAD_VA = 0x400000
ARGV_PASS = b"DISK_ARGV_PASS\n"
ENV_PASS = b"DISK_ENV_PASS\n"
EXEC_PASS = b"DISK_EXEC_PASS\n"
EXPECT_ARGV = [b"disk/run.elf\0", b"alpha\0", b"beta\0"]
EXPECT_ENV = [b"CXC_M82=1\0", b"PATH=/disk\0"]
PAD_TO = 20 * 1024


def assemble() -> bytes:
    """
    [rsp]=argc, argv…, NULL, envp…, NULL, auxv…
    Expect argc==3, argv strings, two env strings.
    """
    blob = bytearray()
    patches: list[tuple[str, int]] = []

    def jne_near(label: str) -> None:
        at = len(blob)
        blob.extend(b"\x0F\x85\x00\x00\x00\x00")
        patches.append((label, at))

    def patch_lea(lea_at: int, target: int) -> None:
        struct.pack_into("<i", blob, lea_at + 3, target - (lea_at + 7))

    def emit_strcmp_rbx_to_expect() -> int:
        nonlocal blob
        lea_at = len(blob)
        blob += b"\x48\x8D\x35" + struct.pack("<i", 0)  # lea rsi, expect
        loop = len(blob)
        blob += b"\xAC"
        blob += b"\x3A\x03"
        jne_near("fail")
        blob += b"\x48\xFF\xC3"
        blob += b"\x84\xC0"
        jnz_at = len(blob)
        blob += b"\x75\x00"
        struct.pack_into("b", blob, jnz_at + 1, loop - (jnz_at + 2))
        return lea_at

    # cmp qword [rsp], 3
    blob += b"\x48\x83\x3C\x24\x03"
    jne_near("fail")

    lea_argv: list[int] = []
    for index in range(3):
        blob.extend(b"\x48\x8B\x5C\x24" + bytes([8 + 8 * index]))
        lea_argv.append(emit_strcmp_rbx_to_expect())

    # envp base = rsp + 8 + argc*8 + 8 = rsp + 40 when argc==3
    # mov rbx, [rsp+40] ; envp[0]
    # mov r12, [rsp+48] ; envp[1]
    # cmp qword [rsp+56], 0 ; envp terminator
    blob += b"\x48\x8B\x5C\x24\x28"  # mov rbx, [rsp+0x28]
    lea_env0 = emit_strcmp_rbx_to_expect()
    blob += b"\x48\x8B\x5C\x24\x30"  # mov rbx, [rsp+0x30]
    lea_env1 = emit_strcmp_rbx_to_expect()
    blob += b"\x48\x83\x7C\x24\x38\x00"  # cmpq $0, [rsp+0x38]
    jne_near("fail")

    msgs = [ARGV_PASS, ENV_PASS, EXEC_PASS]
    lea_msgs: list[int] = []
    len_patches: list[int] = []
    for _ in msgs:
        blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
        blob += b"\xBF\x01\x00\x00\x00"
        lea_at = len(blob)
        blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
        lea_msgs.append(lea_at)
        blob += b"\xBA\x00\x00\x00\x00"
        len_patches.append(lea_at + 8)
        blob += b"\xCD\x80"

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
        struct.pack_into("<i", blob, at + 2, fail_at - (at + 6))

    expect_offs: list[int] = []
    for text in EXPECT_ARGV + EXPECT_ENV:
        expect_offs.append(len(blob))
        blob += text
    msg_offs: list[int] = []
    for text in msgs:
        msg_offs.append(len(blob))
        blob += text

    for lea_at, off in zip(lea_argv, expect_offs[:3], strict=True):
        patch_lea(lea_at, off)
    patch_lea(lea_env0, expect_offs[3])
    patch_lea(lea_env1, expect_offs[4])
    for lea_at, off, text, len_at in zip(
        lea_msgs, msg_offs, msgs, len_patches, strict=True
    ):
        patch_lea(lea_at, off)
        struct.pack_into("<I", blob, len_at, len(text))

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
