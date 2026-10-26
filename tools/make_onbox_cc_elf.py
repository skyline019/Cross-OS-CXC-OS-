#!/usr/bin/env python3
"""Build ET_EXEC on-box 'cc': parse return N from disk/in.c → emit out.elf → execve (M93/A2)."""

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
RETURN_KEY = b"return "
PASS = b"ONBOX_CGEN_PASS\n"  # nested out.elf after real codegen


def build_out_elf() -> tuple[bytes, int]:
    """Minimal ET_EXEC: write ONBOX_CGEN_PASS and exit(N). Returns (elf, exit_imm_off)."""
    code = bytearray()
    code += b"\xB8" + struct.pack("<I", SYS_WRITE)
    code += b"\xBF\x01\x00\x00\x00"
    lea = len(code)
    code += b"\x48\x8D\x35" + struct.pack("<i", 0)
    code += b"\xBA" + struct.pack("<I", len(PASS))
    code += b"\xCD\x80"
    code += b"\xB8" + struct.pack("<I", SYS_EXIT)
    exit_imm = len(code) + 1  # offset of imm32 in `mov edi, imm32`
    code += b"\xBF" + struct.pack("<I", 0)  # patched at compile time from return N
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
    elf = ehdr + phdr + bytes(code)
    return elf, file_off + exit_imm


def assemble(out_elf: bytes, exit_imm_off: int) -> tuple[bytes, dict[str, int], int]:
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

    # rsp[0x000..0x100): in.c; rsp[0x100..0x200): writable out.elf copy (RX image is not writable)
    if len(out_elf) > 0x100:
        raise ValueError(f"out.elf too large for stack copy: {len(out_elf)}")
    blob += b"\x48\x81\xEC\x00\x02\x00\x00"  # sub rsp, 0x200

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
    jcc(b"\x0F\x8C", "fail")
    blob += b"\x41\x89\xC4"  # r12d = nread

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

    # --- parse "return N" in [rsp .. rsp+r12) ---
    # r8d = index; r13d = accumulator; r9d scratch
    blob += b"\x41\xB8\x00\x00\x00\x00"  # mov r8d, 0
    scan_at = len(blob)
    # if r8d + 7 > r12d → fail (need room for "return ")
    blob += b"\x44\x89\xC0"  # mov eax, r8d
    blob += b"\x83\xC0\x07"  # add eax, 7
    blob += b"\x44\x39\xE0"  # cmp eax, r12d
    jcc(b"\x0F\x8F", "fail")  # jg fail

    # compare "return " at rsp+r8+i (mod=01 disp8)
    for i, ch in enumerate(RETURN_KEY):
        # cmp byte [rsp+r8+i], ch  → 42 80 7C 04 disp imm8
        blob += b"\x42\x80\x7C\x04" + bytes([i, ch])
        at = len(blob)
        blob.extend(b"\x0F\x85" + b"\x00\x00\x00\x00")
        jccs.append((f"try_next_{i}", at))

    # matched — advance r8 past "return ", parse decimal into r13d
    blob += b"\x41\x83\xC0\x07"  # add r8d, 7
    blob += b"\x41\xBD\x00\x00\x00\x00"  # mov r13d, 0
    # require at least one digit
    blob += b"\x45\x39\xE0"  # cmp r8d, r12d
    jcc(b"\x0F\x83", "fail")  # jae fail
    blob += b"\x42\x0F\xB6\x04\x04"  # movzx eax, byte [rsp+r8]
    blob += b"\x83\xE8\x30"  # sub eax, '0'
    blob += b"\x83\xF8\x09"
    jcc(b"\x0F\x87", "fail")  # ja fail
    blob += b"\x41\x89\xC5"  # mov r13d, eax
    blob += b"\x41\xFF\xC0"  # inc r8d

    # optional more digits (cap 255)
    dig_loop = len(blob)
    blob += b"\x45\x39\xE0"  # cmp r8d, r12d
    jcc(b"\x0F\x83", "parsed")  # jae parsed
    blob += b"\x42\x0F\xB6\x04\x04"  # movzx eax, byte [rsp+r8]
    blob += b"\x83\xE8\x30"
    blob += b"\x83\xF8\x09"
    jcc(b"\x0F\x87", "parsed")  # ja → end number
    # r13d = r13d*10 + digit
    blob += b"\x44\x89\xE9"  # mov ecx, r13d
    blob += b"\x6B\xC9\x0A"  # imul ecx, 10
    blob += b"\x01\xC1"  # add ecx, eax
    blob += b"\x81\xF9\xFF\x00\x00\x00"  # cmp ecx, 255
    jcc(b"\x0F\x8F", "fail")  # jg fail
    blob += b"\x41\x89\xCD"  # mov r13d, ecx
    blob += b"\x41\xFF\xC0"  # inc r8d
    # jmp dig_loop
    at = len(blob)
    blob.extend(b"\xE9" + b"\x00\x00\x00\x00")
    jccs.append(("dig_loop", at))

    # try_next labels: inc r8, jmp scan
    try_next_targets: dict[str, int] = {}
    for i in range(len(RETURN_KEY)):
        try_next_targets[f"try_next_{i}"] = len(blob)
    blob += b"\x41\xFF\xC0"  # inc r8d
    at = len(blob)
    blob.extend(b"\xE9" + b"\x00\x00\x00\x00")
    jccs.append(("scan", at))

    parsed_at = len(blob)

    # open(disk/out.elf, WRONLY|CREAT|TRUNC, 0755)
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("out")
    blob += b"\xBE" + struct.pack("<I", O_WRONLY | O_CREAT | O_TRUNC)
    blob += b"\xBA\xED\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    # copy embedded template to writable stack, then patch exit(N)
    lea_rsi("payload")
    blob += b"\x48\x8D\xBC\x24\x00\x01\x00\x00"  # lea rdi, [rsp+0x100]
    blob += b"\xB9" + struct.pack("<I", len(out_elf))  # mov ecx, len
    blob += b"\xF3\xA4"  # rep movsb
    blob += b"\x44\x89\xAC\x24" + struct.pack(
        "<I", 0x100 + exit_imm_off
    )  # mov [rsp+0x100+exit_imm], r13d

    # write patched out.elf from stack
    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\x89\xDF"
    blob += b"\x48\x8D\xB4\x24\x00\x01\x00\x00"  # lea rsi, [rsp+0x100]
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
    at = len(blob)
    blob.extend(b"\x48\x8D\x15" + struct.pack("<i", 0))
    leas.append(("envp", at))
    blob += b"\xCD\x80"

    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    fail_at = len(blob)
    blob += b"\xB8" + struct.pack("<I", SYS_EXIT)
    blob += b"\xBF\x01\x00\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x0F\x0B"

    targets: dict[str, int] = {
        "fail": fail_at,
        "parsed": parsed_at,
        "scan": scan_at,
        "dig_loop": dig_loop,
    }
    targets.update(try_next_targets)
    targets["src"] = len(blob)
    blob += SRC_PATH
    targets["out"] = len(blob)
    blob += OUT_PATH
    while len(blob) % 8 != 0:
        blob += b"\x00"
    targets["argv"] = len(blob)
    argv_at = len(blob)
    blob += struct.pack("<QQ", 0, 0)
    targets["envp"] = len(blob)
    blob += struct.pack("<Q", 0)
    targets["payload"] = len(blob)
    blob += out_elf

    for label, at in jccs:
        if label == "dig_loop" or label == "scan":
            # near jmp E9
            struct.pack_into("<i", blob, at + 1, targets[label] - (at + 5))
        else:
            struct.pack_into("<i", blob, at + 2, targets[label] - (at + 6))
    for kind, at in leas:
        struct.pack_into("<i", blob, at + 3, targets[kind] - (at + 7))

    return bytes(blob), targets, argv_at


def build_elf(payload: bytes, targets: dict[str, int], argv_at: int) -> bytes:
    ehdr_size = 64
    phdr_size = 56
    file_off = ehdr_size + phdr_size
    page = 4096
    body = bytearray(payload)
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
    out_elf, exit_imm_off = build_out_elf()
    payload, targets, argv_at = assemble(out_elf, exit_imm_off)
    data = build_elf(payload, targets, argv_at)
    args.output.write_bytes(data)
    print(
        f"wrote {args.output} ({len(data)} bytes; out.elf embed {len(out_elf)}; "
        f"exit_imm@{exit_imm_off})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
