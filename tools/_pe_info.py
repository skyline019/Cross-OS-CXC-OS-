#!/usr/bin/env python3
from pathlib import Path

pe = Path("build/uefi/BOOTX64.efi").read_bytes()
assert pe[:2] == b"MZ"
e_lfanew = int.from_bytes(pe[0x3C:0x40], "little")
opt = e_lfanew + 24
print("magic", hex(int.from_bytes(pe[opt : opt + 2], "little")))
print("SizeOfCode", int.from_bytes(pe[opt + 4 : opt + 8], "little"))
print("SizeOfInit", int.from_bytes(pe[opt + 8 : opt + 12], "little"))
print("SizeOfUninit", int.from_bytes(pe[opt + 12 : opt + 16], "little"))
print("SizeOfImage", int.from_bytes(pe[opt + 56 : opt + 60], "little"))
print("file size", len(pe))
