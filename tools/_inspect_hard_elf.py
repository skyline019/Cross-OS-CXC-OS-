#!/usr/bin/env python3
from pathlib import Path
import struct
import subprocess

elf_path = Path("/tmp/hard_out.elf")
b = elf_path.read_bytes()
assert b[:4] == b"\x7fELF"
entry = struct.unpack_from("<Q", b, 24)[0]
phoff = struct.unpack_from("<Q", b, 32)[0]
phentsize, phnum = struct.unpack_from("<HH", b, 54)
print("entry", hex(entry), "phoff", phoff, "phnum", phnum)
segs = []
for i in range(phnum):
    off = phoff + i * phentsize
    t, flg, foff, va, pa, fsz, msz, align = struct.unpack_from("<IIQQQQQQ", b, off)
    segs.append((t, flg, foff, va, fsz, msz))
    print(i, "type", t, "flags", flg, "foff", hex(foff), "va", hex(va),
          "fsz", hex(fsz), "msz", hex(msz))

fault = 0x401205
for t, flg, foff, va, fsz, msz in segs:
    if va <= fault < va + max(fsz, msz):
        fpos = foff + (fault - va)
        chunk = b[fpos : fpos + 48]
        print("fault file", hex(fpos), "bytes", chunk.hex())
        # ndisasm or manual: write blob and objdump -D -b binary -m i386:x86-64
        blob = Path("/tmp/fault.bin")
        # include 32 bytes before
        start = max(0, fpos - 32)
        blob.write_bytes(b[start : fpos + 64])
        print("context file", hex(start), "..", hex(fpos + 64))
        subprocess.run(
            [
                "objdump",
                "-D",
                "-b",
                "binary",
                "-m",
                "i386:x86-64",
                f"--adjust-vma={va + (start - foff)}",
                str(blob),
            ],
            check=False,
        )

# Also dump entry trampoline
t, flg, foff, va, fsz, msz = segs[0]
Path("/tmp/text.bin").write_bytes(b[foff : foff + min(fsz, 0x200)])
subprocess.run(
    [
        "objdump",
        "-D",
        "-b",
        "binary",
        "-m",
        "i386:x86-64",
        f"--adjust-vma={va}",
        "/tmp/text.bin",
    ],
    check=False,
)
