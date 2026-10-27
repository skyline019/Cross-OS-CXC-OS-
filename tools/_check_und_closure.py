#!/usr/bin/env python3
from pathlib import Path
import subprocess

pre = Path(__file__).resolve().parent.parent / "user" / "prebuilt"
files = sorted(pre.glob("musl_*.o")) + [pre / "hard_stubs.o"]
und: set[str] = set()
defs: set[str] = set()
for f in files:
    u = subprocess.check_output(["nm", "-u", str(f)], text=True, stderr=subprocess.DEVNULL)
    for line in u.splitlines():
        s = line.strip().split()[-1]
        if s:
            und.add(s)
    g = subprocess.check_output(
        ["nm", "-g", "--defined-only", str(f)], text=True, stderr=subprocess.DEVNULL
    )
    for line in g.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            defs.add(parts[-1])
missing = sorted(und - defs)
print("UND count", len(und))
print("MISSING", missing)
for name in [
    "__stack_chk_fail",
    "___errno_location",
    "__memcpy_fwd",
    "__memrchr",
    "__strchrnul",
    "__shlim",
    "__intscan",
    "__shgetc",
    "__uflow",
    "malloc",
    "memset",
    "strlen",
    "memcpy",
    "strcspn",
]:
    print(f"  {name}: defined={name in defs}")
