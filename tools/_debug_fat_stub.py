#!/usr/bin/env python3
from pathlib import Path

b = Path("user/prebuilt/fat_stub.elf").read_bytes()
print("stub bytes", len(b))
print("path at", b.find(b"disk/fat.elf"))
print("X count", b.count(ord("X")))
print("long run", max((len(run) for run in b.split(b"Y")), default=0))
# contiguous X
import re

m = re.search(b"X+", b)
print("first X run", len(m.group(0)) if m else 0)

# Regenerated size estimate
N_ARGS = 64
LONG = 1024
# code ~40 + path 13 + long 1025 + align + argv 65*8 + envp 8
print("expected min", 40 + 13 + 1025 + 64 * 8 + 16)
