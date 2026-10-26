#!/usr/bin/env python3
"""Build ET_EXEC: pipe+fork+dup + EPIPE edge → PIPE_PASS (M85)."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SYS_READ = 0
SYS_WRITE = 1
SYS_CLOSE = 3
SYS_PIPE = 22
SYS_DUP = 32
SYS_FORK = 57
SYS_EXIT = 60
SYS_WAIT4 = 61
LOAD_VA = 0x400000
PASS = b"PIPE_PASS\n"
PAYLOAD = b"PIPE_DATA\n"
DUP_BYTE = b"D"


def assemble() -> bytes:
    blob = bytearray()
    jccs: list[tuple[str, int]] = []
    leas: list[tuple[str, int, int]] = []  # (kind, insn_at, mid_len)

    def jcc(op: bytes, label: str) -> None:
        at = len(blob)
        blob.extend(op + b"\x00\x00\x00\x00")
        jccs.append((label, at))

    def sys_write_stdout(msg_label: str, length: int) -> None:
        nonlocal blob
        at = len(blob)
        blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
        blob += b"\xBF\x01\x00\x00\x00"
        blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
        blob += b"\xBA" + struct.pack("<I", length)
        blob += b"\xCD\x80"
        leas.append((msg_label, at, 10))  # B8(5)+BF(5)=10

    def sys_write_fd_reg_edi(msg_label: str, length: int, mid: bytes) -> None:
        """mid is bytes between mov eax and lea rsi (sets edi)."""
        nonlocal blob
        at = len(blob)
        blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
        blob += mid
        blob += b"\x48\x8D\x35" + struct.pack("<i", 0)
        blob += b"\xBA" + struct.pack("<I", length)
        blob += b"\xCD\x80"
        leas.append((msg_label, at, 5 + len(mid)))

    # Stack: [rsp]=rfd, [rsp+4]=wfd, [rsp+8]=status, [rsp+16]=readbuf[16]
    blob += b"\x48\x83\xEC\x30"  # sub rsp, 0x30

    # --- same-process smoke: pipe, write, close-w, read, close-r ---
    blob += b"\xB8" + struct.pack("<I", SYS_PIPE)
    blob += b"\x48\x89\xE7"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    sys_write_fd_reg_edi("payload", len(PAYLOAD), b"\x8B\x7C\x24\x04")
    blob += b"\x48\x83\xF8" + bytes([len(PAYLOAD)])
    jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x7C\x24\x04"
    blob += b"\xCD\x80"

    blob += b"\xB8" + struct.pack("<I", SYS_READ)
    blob += b"\x8B\x3C\x24"
    blob += b"\x48\x8D\x74\x24\x10"
    blob += b"\xBA" + struct.pack("<I", len(PAYLOAD))
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8" + bytes([len(PAYLOAD)])
    jcc(b"\x0F\x85", "fail")

    for i, ch in enumerate(PAYLOAD):
        blob += b"\x80\x7C\x24" + bytes([0x10 + i, ch])
        jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x3C\x24"
    blob += b"\xCD\x80"

    # --- fork IPC ---
    blob += b"\xB8" + struct.pack("<I", SYS_PIPE)
    blob += b"\x48\x89\xE7"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_FORK)
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x84", "child")
    jcc(b"\x0F\x88", "fail")

    # parent: close(w), read(r), close(r), wait4
    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x7C\x24\x04"
    blob += b"\xCD\x80"

    blob += b"\xB8" + struct.pack("<I", SYS_READ)
    blob += b"\x8B\x3C\x24"
    blob += b"\x48\x8D\x74\x24\x10"
    blob += b"\xBA" + struct.pack("<I", len(PAYLOAD))
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8" + bytes([len(PAYLOAD)])
    jcc(b"\x0F\x85", "fail")

    for i, ch in enumerate(PAYLOAD):
        blob += b"\x80\x7C\x24" + bytes([0x10 + i, ch])
        jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x3C\x24"
    blob += b"\xCD\x80"

    blob += b"\xB8" + struct.pack("<I", SYS_WAIT4)
    blob += b"\x48\xC7\xC7\xFF\xFF\xFF\xFF"
    blob += b"\x48\x8D\x74\x24\x08"
    blob += b"\x31\xD2"
    blob += b"\x4D\x31\xC0"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x8E", "fail")

    # --- EPIPE ---
    blob += b"\xB8" + struct.pack("<I", SYS_PIPE)
    blob += b"\x48\x89\xE7"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x3C\x24"
    blob += b"\xCD\x80"

    sys_write_fd_reg_edi("dupbyte", 1, b"\x8B\x7C\x24\x04")
    blob += b"\x48\x83\xF8\xE0"  # cmp rax, -32
    jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x7C\x24\x04"
    blob += b"\xCD\x80"

    # --- dup ---
    blob += b"\xB8" + struct.pack("<I", SYS_PIPE)
    blob += b"\x48\x89\xE7"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_DUP)
    blob += b"\x8B\x7C\x24\x04"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"  # mov ebx, eax

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x7C\x24\x04"
    blob += b"\xCD\x80"

    sys_write_fd_reg_edi("dupbyte", 1, b"\x89\xDF")
    blob += b"\x48\x83\xF8\x01"
    jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"

    blob += b"\xB8" + struct.pack("<I", SYS_READ)
    blob += b"\x8B\x3C\x24"
    blob += b"\x48\x8D\x74\x24\x10"
    blob += b"\xBA\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\x01"
    jcc(b"\x0F\x85", "fail")
    blob += b"\x80\x7C\x24\x10" + DUP_BYTE
    jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x3C\x24"
    blob += b"\xCD\x80"

    sys_write_stdout("pass", len(PASS))
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\x31\xFF"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    child_at = len(blob)
    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x3C\x24"
    blob += b"\xCD\x80"
    sys_write_fd_reg_edi("payload", len(PAYLOAD), b"\x8B\x7C\x24\x04")
    blob += b"\x48\x83\xF8" + bytes([len(PAYLOAD)])
    jcc(b"\x0F\x85", "fail")
    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x8B\x7C\x24\x04"
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

    pass_at = len(blob)
    blob += PASS
    payload_at = len(blob)
    blob += PAYLOAD
    dup_at = len(blob)
    blob += DUP_BYTE

    labels = {"child": child_at, "fail": fail_at}
    for label, at in jccs:
        struct.pack_into("<i", blob, at + 2, labels[label] - (at + 6))

    targets = {"pass": pass_at, "payload": payload_at, "dupbyte": dup_at}
    for kind, insn_at, mid_len in leas:
        lea = insn_at + mid_len
        assert blob[lea : lea + 3] == b"\x48\x8D\x35", (
            kind,
            lea,
            blob[lea : lea + 3].hex(),
        )
        struct.pack_into(
            "<i", blob, lea + 3, targets[kind] - (lea + 7)
        )

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
