#!/usr/bin/env python3
from pathlib import Path
blob = Path("/mnt/e/OS/workspace-study/cxc-os/user/prebuilt/hard_stubs.o").read_bytes()
parts = [f"0x{b:02x}" for b in blob]
lines = ["    " + ", ".join(parts[i : i + 12]) + "," for i in range(0, len(parts), 12)]
Path("/mnt/e/OS/workspace-study/cxc-os/include/cxc/hard_stubs_elf.hpp").write_text(
    "/* Generated freestanding HARD stubs. */\n#pragma once\n"
    "static const unsigned char hard_stubs_elf[] = {\n"
    + "\n".join(lines)
    + "\n};\n"
    f"static const unsigned hard_stubs_elf_size = {len(blob)};\n",
    encoding="ascii",
)
print("hard_stubs_elf_size", len(blob))
