#!/usr/bin/env python3
from pathlib import Path
import struct
import subprocess

b = Path("/tmp/hard_out.elf").read_bytes()
phoff = struct.unpack_from("<Q", b, 32)[0]
t, flg, foff, va, pa, fsz, msz, al = struct.unpack_from("<IIQQQQQQ", b, phoff)
crt_va = 0x401E68
off = foff + (crt_va - va)
Path("/tmp/crt.bin").write_bytes(b[off : off + 0x180])
print("crt file", hex(off))
subprocess.run(
    [
        "objdump",
        "-D",
        "-b",
        "binary",
        "-m",
        "i386:x86-64",
        f"--adjust-vma={crt_va}",
        "/tmp/crt.bin",
    ],
    check=False,
)
