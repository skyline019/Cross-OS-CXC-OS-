#!/usr/bin/env python3
"""Plant M260–M262 fat ELF + mini-as headers and rebuild notes."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRE = ROOT / "user" / "prebuilt"
INC = ROOT / "include" / "cxc"


def write_header(path: Path, symbol: str, blob: bytes, comment: str) -> None:
    parts = [f"0x{b:02x}" for b in blob]
    lines = ["    " + ", ".join(parts[i : i + 12]) + "," for i in range(0, len(parts), 12)]
    path.write_text(
        f"/* {comment} */\n#pragma once\n"
        f"static const unsigned char {symbol}[] = {{\n"
        + "\n".join(lines)
        + "\n};\n"
        + f"static const unsigned {symbol}_size = {len(blob)};\n",
        encoding="ascii",
    )


def main() -> int:
    PRE.mkdir(parents=True, exist_ok=True)
    INC.mkdir(parents=True, exist_ok=True)

    fat = PRE / "fat42.elf"
    if not fat.is_file():
        src = Path("/tmp/fat42.c")
        src.write_text("#include <unistd.h>\nint main(void){_exit(42);}\n")
        subprocess.check_call(
            ["musl-gcc", "-static", "-Os", "-s", "-o", str(fat), str(src)]
        )
    blob = fat.read_bytes()
    assert blob[:4] == b"\x7fELF"
    write_header(
        INC / "fat42_elf.hpp",
        "fat42_elf",
        blob,
        f"M260 musl-static fat42; file={len(blob)}; pad on-disk >1MiB. "
        + subprocess.check_output(["file", str(fat)], text=True).strip(),
    )
    print("fat42", len(blob))

    as_bin = PRE / "as"
    subprocess.check_call(
        [
            "musl-gcc",
            "-static",
            "-Os",
            "-s",
            "-o",
            str(as_bin),
            str(ROOT / "tools" / "mini_as.c"),
        ]
    )
    as_blob = as_bin.read_bytes()
    assert as_blob[:4] == b"\x7fELF"
    nm = subprocess.run(
        ["nm", "-u", str(as_bin)], capture_output=True, text=True
    )
    size = subprocess.check_output(["size", str(as_bin)], text=True)
    readelf = subprocess.check_output(
        ["readelf", "-h", str(as_bin)], text=True
    )
    note = (
        "M261 musl-static mini as (AT&T subset -> ET_REL). "
        f"bytes={len(as_blob)}. size:\n{size}nm -u:\n{nm.stdout or nm.stderr}"
        f"readelf:\n{readelf}"
    )
    write_header(
        INC / "mini_as_elf.hpp",
        "mini_as_elf",
        as_blob,
        note.replace("\n", " | "),
    )
    print("mini_as", len(as_blob))
    print("nm -u:\n", nm.stdout or nm.stderr)
    print(size)
    print(readelf)

    # Host gcc -c bridge object for M262
    bridge_s = PRE / "tcap_bridge.s"
    bridge_s.write_text(
        ".text\n.globl tcap_bridge_add\ntcap_bridge_add:\n"
        "mov %edi, %eax\nadd %esi, %eax\nret\n"
    )
    bridge_o = PRE / "tcap_bridge.o"
    subprocess.check_call(["gcc", "-c", "-o", str(bridge_o), str(bridge_s)])
    br = bridge_o.read_bytes()
    write_header(
        INC / "tcap_bridge_elf.hpp",
        "tcap_bridge_elf",
        br,
        "M262 host gcc -c ET_REL bridge (tcap_bridge_add).",
    )
    print("tcap_bridge", len(br))
    return 0


if __name__ == "__main__":
    sys.exit(main())
