#!/usr/bin/env python3
import struct
from pathlib import Path

b = Path("user/prebuilt/fat42.elf").read_bytes()
# Also simulate padded
pad = b + b"\xA5" * (1024 * 1024 + 512 - len(b))
for label, data in [("raw", b), ("padded", pad)]:
    (
        mag,
        typ,
        machine,
        version,
        entry,
        phoff,
        shoff,
        flags,
        ehsize,
        phentsize,
        phnum,
        shentsize,
        shnum,
        shstrndx,
    ) = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    print(label, "len", len(data))
    print("  mag", mag[:4], "class", mag[4], "data", mag[5], "ver", mag[6])
    print("  type", typ, "machine", machine, "version", version)
    print("  entry", hex(entry), "phoff", phoff)
    print("  ehsize", ehsize, "phentsize", phentsize, "phnum", phnum)
    ok = (
        mag[4] == 2
        and mag[5] == 1
        and mag[6] == 1
        and typ == 2
        and machine == 62
        and version == 1
        and ehsize == 64
        and phentsize == 56
        and 0 < phnum <= 8
        and entry != 0
    )
    print("  would_pass_f4", ok)
