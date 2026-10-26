#!/usr/bin/env python3
"""Build ET_EXEC on-box 'cc': parse write LIT + return N → out.elf → execve (M95/A2)."""

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
WRITE_KEY = b"write "
RETURN_KEY = b"return "
PASS = b"ONBOX_WRITE_PASS\n"
MSG_PREFIX = b"WMSG:"
MSG_MAX = 8


def build_out_elf() -> tuple[bytes, int, int, int]:
    """ET_EXEC: write WMSG:<lit>\\n, ONBOX_WRITE_PASS, exit(N).

    Returns (elf, msg_buf_off, msg_len_imm_off, exit_imm_off) — file offsets.
    msg buffer holds PREFIX + up to MSG_MAX chars + '\\n' (patched len).
    """
    code = bytearray()
    # write(1, msg, len) — len and msg content patched at compile time
    code += b"\xB8" + struct.pack("<I", SYS_WRITE)
    code += b"\xBF\x01\x00\x00\x00"
    lea_msg = len(code)
    code += b"\x48\x8D\x35" + struct.pack("<i", 0)
    msg_len_imm = len(code) + 1  # mov edx, imm32
    code += b"\xBA" + struct.pack("<I", 0)
    code += b"\xCD\x80"
    # write(1, PASS, len(PASS))
    code += b"\xB8" + struct.pack("<I", SYS_WRITE)
    code += b"\xBF\x01\x00\x00\x00"
    lea_pass = len(code)
    code += b"\x48\x8D\x35" + struct.pack("<i", 0)
    code += b"\xBA" + struct.pack("<I", len(PASS))
    code += b"\xCD\x80"
    # exit(N)
    code += b"\xB8" + struct.pack("<I", SYS_EXIT)
    exit_imm = len(code) + 1
    code += b"\xBF" + struct.pack("<I", 0)
    code += b"\xCD\x80"
    code += b"\x0F\x0B"

    msg_at = len(code)
    # PREFIX + MSG_MAX bytes + '\n' reserved
    code += MSG_PREFIX + bytes(MSG_MAX) + b"\n"
    pass_at = len(code)
    code += PASS
    struct.pack_into("<i", code, lea_msg + 3, msg_at - (lea_msg + 7))
    struct.pack_into("<i", code, lea_pass + 3, pass_at - (lea_pass + 7))

    ehdr_size = 64
    phdr_size = 56
    file_off = ehdr_size + phdr_size
    page = 4096
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
        page,
        page,
    )
    elf = ehdr + phdr + bytes(code)
    return (
        elf,
        file_off + msg_at + len(MSG_PREFIX),  # first literal byte
        file_off + msg_len_imm,
        file_off + exit_imm,
    )


def assemble_v2(
    out_elf: bytes, msg_buf_off: int, msg_len_imm_off: int, exit_imm_off: int
) -> tuple[bytes, dict[str, int], int]:
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

    if len(out_elf) > 0x100:
        raise ValueError(f"out.elf too large: {len(out_elf)}")
    blob += b"\x48\x81\xEC\x08\x02\x00\x00"  # sub rsp, 0x208

    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("src")
    blob += b"\xBE" + struct.pack("<I", O_RDONLY)
    blob += b"\x31\xD2"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    blob += b"\xB8" + struct.pack("<I", SYS_READ)
    blob += b"\x89\xDF"
    blob += b"\x48\x89\xE6"
    blob += b"\xBA\x00\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x83\xF8\x06"
    jcc(b"\x0F\x8C", "fail")
    blob += b"\x41\x89\xC4"

    for i, ch in enumerate(MAGIC):
        if i == 0:
            blob += b"\x80\x3C\x24" + bytes([ch])
        else:
            blob += b"\x80\x7C\x24" + bytes([i, ch])
        jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"

    # clear lit scratch
    blob += b"\x48\xC7\x84\x24\x00\x02\x00\x00\x00\x00\x00\x00"
    blob += b"\x48\xC7\x84\x24\x04\x02\x00\x00\x00\x00\x00\x00"

    # --- write LIT ---
    blob += b"\x41\xB8\x00\x00\x00\x00"
    wscan = len(blob)
    blob += b"\x44\x89\xC0"
    blob += b"\x83\xC0" + bytes([len(WRITE_KEY)])
    blob += b"\x44\x39\xE0"
    jcc(b"\x0F\x8F", "fail")
    for i, ch in enumerate(WRITE_KEY):
        blob += b"\x42\x80\x7C\x04" + bytes([i, ch])
        at = len(blob)
        blob.extend(b"\x0F\x85" + b"\x00\x00\x00\x00")
        jccs.append((f"wtry_{i}", at))
    blob += b"\x41\x83\xC0" + bytes([len(WRITE_KEY)])
    blob += b"\x41\xBE\x00\x00\x00\x00"  # r14d = 0 len
    blob += b"\x45\x39\xE0"
    jcc(b"\x0F\x83", "fail")

    lit_loop = len(blob)
    blob += b"\x45\x39\xE0"
    jcc(b"\x0F\x83", "wend")
    blob += b"\x42\x0F\xB6\x04\x04"
    blob += b"\x3C\x3B"
    jcc(b"\x0F\x84", "wend")
    # validate alnum
    blob += b"\x3C\x30"
    jcc(b"\x0F\x82", "fail")
    blob += b"\x3C\x39"
    jcc(b"\x0F\x86", "lit_ok")
    blob += b"\x3C\x41"
    jcc(b"\x0F\x82", "fail")
    blob += b"\x3C\x5A"
    jcc(b"\x0F\x86", "lit_ok")
    blob += b"\x3C\x61"
    jcc(b"\x0F\x82", "fail")
    blob += b"\x3C\x7A"
    jcc(b"\x0F\x87", "fail")

    lit_ok_at = len(blob)
    blob += b"\x41\x83\xFE" + bytes([MSG_MAX])
    jcc(b"\x0F\x83", "fail")
    # lea rdi,[rsp+0x200]; add rdi,r14; mov [rdi],al
    blob += b"\x48\x8D\xBC\x24\x00\x02\x00\x00"
    blob += b"\x4C\x01\xF7"  # add rdi, r14
    blob += b"\x88\x07"  # mov [rdi], al
    blob += b"\x41\xFF\xC6"  # inc r14d
    blob += b"\x41\xFF\xC0"  # inc r8d
    at = len(blob)
    blob.extend(b"\xE9" + b"\x00\x00\x00\x00")
    jccs.append(("lit_loop", at))

    wtry_at = len(blob)
    for i in range(len(WRITE_KEY)):
        pass  # filled via targets
    blob += b"\x41\xFF\xC0"
    at = len(blob)
    blob.extend(b"\xE9" + b"\x00\x00\x00\x00")
    jccs.append(("wscan", at))

    wend_at = len(blob)
    blob += b"\x41\x83\xFE\x00"
    jcc(b"\x0F\x84", "fail")  # empty lit
    # skip ';'
    blob += b"\x45\x39\xE0"
    jcc(b"\x0F\x83", "fail")
    blob += b"\x42\x80\x3C\x04\x3B"  # cmp [rsp+r8], ';'
    jcc(b"\x0F\x85", "fail")

    # --- parse return N → r13d ---
    blob += b"\x41\xB8\x00\x00\x00\x00"
    rscan = len(blob)
    blob += b"\x44\x89\xC0"
    blob += b"\x83\xC0" + bytes([len(RETURN_KEY)])
    blob += b"\x44\x39\xE0"
    jcc(b"\x0F\x8F", "fail")
    for i, ch in enumerate(RETURN_KEY):
        blob += b"\x42\x80\x7C\x04" + bytes([i, ch])
        at = len(blob)
        blob.extend(b"\x0F\x85" + b"\x00\x00\x00\x00")
        jccs.append((f"rtry_{i}", at))
    blob += b"\x41\x83\xC0" + bytes([len(RETURN_KEY)])
    blob += b"\x41\xBD\x00\x00\x00\x00"
    blob += b"\x45\x39\xE0"
    jcc(b"\x0F\x83", "fail")
    blob += b"\x42\x0F\xB6\x04\x04"
    blob += b"\x83\xE8\x30"
    blob += b"\x83\xF8\x09"
    jcc(b"\x0F\x87", "fail")
    blob += b"\x41\x89\xC5"
    blob += b"\x41\xFF\xC0"
    dig_loop = len(blob)
    blob += b"\x45\x39\xE0"
    jcc(b"\x0F\x83", "parsed")
    blob += b"\x42\x0F\xB6\x04\x04"
    blob += b"\x83\xE8\x30"
    blob += b"\x83\xF8\x09"
    jcc(b"\x0F\x87", "parsed")
    blob += b"\x44\x89\xE9"
    blob += b"\x6B\xC9\x0A"
    blob += b"\x01\xC1"
    blob += b"\x81\xF9\xFF\x00\x00\x00"
    jcc(b"\x0F\x8F", "fail")
    blob += b"\x41\x89\xCD"
    blob += b"\x41\xFF\xC0"
    at = len(blob)
    blob.extend(b"\xE9" + b"\x00\x00\x00\x00")
    jccs.append(("dig_loop", at))

    rtry_at = len(blob)
    blob += b"\x41\xFF\xC0"
    at = len(blob)
    blob.extend(b"\xE9" + b"\x00\x00\x00\x00")
    jccs.append(("rscan", at))

    parsed_at = len(blob)

    # open out.elf
    blob += b"\xB8" + struct.pack("<I", SYS_OPEN)
    lea_rdi("out")
    blob += b"\xBE" + struct.pack("<I", O_WRONLY | O_CREAT | O_TRUNC)
    blob += b"\xBA\xED\x01\x00\x00"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")
    blob += b"\x89\xC3"

    # copy template to stack
    lea_rsi("payload")
    blob += b"\x48\x8D\xBC\x24\x00\x01\x00\x00"
    blob += b"\xB9" + struct.pack("<I", len(out_elf))
    blob += b"\xF3\xA4"

    # patch msg literal bytes, then '\n' immediately after lit
    blob += b"\x48\x8D\xBC\x24" + struct.pack("<I", 0x100 + msg_buf_off)
    blob += b"\x48\x8D\xB4\x24\x00\x02\x00\x00"  # rsi = lit scratch
    blob += b"\x44\x89\xF1"  # ecx = r14d
    blob += b"\xF3\xA4"
    # rdi now points just past lit; write newline
    blob += b"\xC6\x07\x0A"  # mov byte [rdi], '\n'

    # patch msg len = PREFIX + lit + 1
    blob += b"\x44\x89\xF0"  # eax = r14d
    blob += b"\x83\xC0" + bytes([len(MSG_PREFIX) + 1])
    blob += b"\x89\x84\x24" + struct.pack("<I", 0x100 + msg_len_imm_off)

    # patch exit
    blob += b"\x44\x89\xAC\x24" + struct.pack("<I", 0x100 + exit_imm_off)

    # write out.elf
    blob += b"\xB8" + struct.pack("<I", SYS_WRITE)
    blob += b"\x89\xDF"
    blob += b"\x48\x8D\xB4\x24\x00\x01\x00\x00"
    blob += b"\xBA" + struct.pack("<I", len(out_elf))
    blob += b"\xCD\x80"
    blob += b"\x48\x3D" + struct.pack("<I", len(out_elf))
    jcc(b"\x0F\x85", "fail")

    blob += b"\xB8" + struct.pack("<I", SYS_CLOSE)
    blob += b"\x89\xDF"
    blob += b"\xCD\x80"
    blob += b"\x48\x85\xC0"
    jcc(b"\x0F\x88", "fail")

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
        "wscan": wscan,
        "lit_loop": lit_loop,
        "lit_ok": lit_ok_at,
        "wend": wend_at,
        "rscan": rscan,
        "dig_loop": dig_loop,
        "parsed": parsed_at,
    }
    for i in range(len(WRITE_KEY)):
        targets[f"wtry_{i}"] = wtry_at
    for i in range(len(RETURN_KEY)):
        targets[f"rtry_{i}"] = rtry_at

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

    near = {"lit_loop", "wscan", "dig_loop", "rscan"}
    for label, at in jccs:
        if label in near:
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
    struct.pack_into("<QQ", body, argv_at, LOAD_VA + targets["out"], 0)
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
    out_elf, msg_buf_off, msg_len_imm_off, exit_imm_off = build_out_elf()
    payload, targets, argv_at = assemble_v2(
        out_elf, msg_buf_off, msg_len_imm_off, exit_imm_off
    )
    data = build_elf(payload, targets, argv_at)
    args.output.write_bytes(data)
    print(
        f"wrote {args.output} ({len(data)} bytes; out.elf {len(out_elf)}; "
        f"msg@{msg_buf_off} len@{msg_len_imm_off} exit@{exit_imm_off})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
