#!/usr/bin/env python3
"""Audit the compiled RV64 store(s) mapped to directcore's root word.

Uses readelf's DWARF line table and decodes RV64 SD/C.SD opcodes directly;
no cross-objdump or Python package is required. A single 64-bit instruction
is necessary but NOT proof that the hardware provides failure atomicity.
The result only covers the compiler, flags and ELF supplied to this script.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys


def store_kind(data):
    if len(data) < 2:
        raise ValueError("short instruction")
    half = struct.unpack_from("<H", data)[0]
    if half & 3 != 3:
        # RVC quadrant 0, funct3=111: C.SD (64-bit store to a register base).
        return "c.sd" if half & 3 == 0 and half >> 13 == 7 else None
    if len(data) < 4:
        raise ValueError("short 32-bit instruction")
    word = struct.unpack_from("<I", data)[0]
    return "sd" if word & 0x7f == 0x23 and (word >> 12) & 7 == 3 else None


def elf_bytes_at(image, address):
    if (len(image) < 64 or image[:4] != b"\x7fELF" or image[4:6] != b"\x02\x01" or
            struct.unpack_from("<H", image, 18)[0] != 243):
        raise ValueError("expected a little-endian RV64 ELF")
    offset = struct.unpack_from("<Q", image, 32)[0]
    entry_size, count = struct.unpack_from("<HH", image, 54)
    if entry_size < 56 or offset > len(image) or count > (len(image) - offset) // entry_size:
        raise ValueError("invalid ELF program-header table")
    for i in range(count):
        ptype, _, fileoff, vaddr, _, filesz, _, _ = struct.unpack_from("<IIQQQQQQ", image, offset + i * entry_size)
        if ptype == 1 and vaddr <= address < vaddr + filesz:
            pos = fileoff + address - vaddr
            if pos > len(image) - 4 or address + 4 > vaddr + filesz:
                raise ValueError("instruction outside ELF file-backed segment")
            return image[pos:pos + 4]
    raise ValueError("debug address is not in a loadable ELF segment")


def source_line(path):
    matches = [i for i, line in enumerate(path.read_text().splitlines(), 1)
               if line.strip() == "*p = word;"]
    if len(matches) != 1:
        raise ValueError("expected one explicit root-word store in source")
    return matches[0]


def debug_addresses(text, line):
    pattern = re.compile(r"(?:^|[ /])directcore\.c\s+" + str(line) + r"\s+(0x[0-9a-fA-F]+)(?:\s|$)")
    return sorted({int(match.group(1), 16) for row in text.splitlines()
                   if (match := pattern.search(row))})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, default=Path("kernel/kernel"))
    parser.add_argument("--source", type=Path, default=Path("kernel/directcore.c"))
    parser.add_argument("--readelf", default="readelf")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    line = source_line(args.source)
    run = subprocess.run([args.readelf, "--debug-dump=decodedline", str(args.kernel)],
                         capture_output=True, text=True, check=True, timeout=60)
    addresses = debug_addresses(run.stdout, line)
    if not addresses:
        raise ValueError("no DWARF addresses for the root store; rebuild with debug information")
    image = args.kernel.read_bytes()
    instructions = []
    for addr in addresses:
        encoded = elf_bytes_at(image, addr)
        kind = store_kind(encoded)
        if kind is None:
            raise ValueError(f"root-store line maps to a non-SD opcode at {addr:#x}: {encoded.hex()}")
        size = 2 if kind == "c.sd" else 4
        instructions.append({"address": hex(addr), "instruction": kind,
                             "bytes": encoded[:size].hex()})
    result = {"source_line": line, "sites": instructions,
              "scope": "one compiler/build; instruction width is not failure atomicity"}
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("DIRECTCORE_STORE_ISA_PASS " + json.dumps(result, sort_keys=True), flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"DIRECTCORE_STORE_ISA_FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
