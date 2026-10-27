import struct
from pathlib import Path

b = Path("user/prebuilt/rtcrt.elf").read_bytes()
print("size", len(b), "magic", b[:4])
shoff = struct.unpack_from("<Q", b, 40)[0]
ehsize, phentsz, phnum, shentsz, shnum, shstrndx = struct.unpack_from(
    "<HHHHHH", b, 52
)
print("shoff", shoff, "shentsz", shentsz, "shnum", shnum, "shstrndx", shstrndx)
for i in range(shnum):
    sh = b[shoff + i * shentsz : shoff + (i + 1) * shentsz]
    name, typ, flags, addr, off, sz, link, info, align, entsz = struct.unpack(
        "<IIQQQQIIQQ", sh
    )
    print(
        f"  [{i}] typ={typ} off={off} sz={sz} link={link} info={info} entsz={entsz}"
    )
# symtab is section 4
symoff = struct.unpack_from("<Q", b, shoff + 4 * 64 + 24)[0]
symsz = struct.unpack_from("<Q", b, shoff + 4 * 64 + 32)[0]
stroff = struct.unpack_from("<Q", b, shoff + 5 * 64 + 24)[0]
print("symoff", symoff, "symsz", symsz, "stroff", stroff)
for i in range(symsz // 24):
    sy = b[symoff + i * 24 : symoff + (i + 1) * 24]
    noff, info, other, shndx, val, size = struct.unpack("<IBBHQQ", sy)
    end = b.find(b"\x00", stroff + noff)
    name = b[stroff + noff : end]
    print(f"  sym[{i}] {name!r} info={info:#x} shndx={shndx} val={val} size={size}")
