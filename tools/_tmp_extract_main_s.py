#!/usr/bin/env python3
import re
from pathlib import Path

text = Path("/mnt/e/OS/workspace-study/cxc-os/src/user.cpp").read_text(
    encoding="utf-8"
)
m = re.search(
    r"static const char main_s_text\[\] =\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+);",
    text,
)
if not m:
    raise SystemExit("main_s_text not found")
parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
out = "".join(bytes(p, "utf-8").decode("unicode_escape") for p in parts)
Path("/tmp/probe_main.s").write_text(out, encoding="utf-8")
print(f"wrote /tmp/probe_main.s bytes={len(out)}")
print("lea", out.count("\nlea "))
print("jne", out.count("\njne "))
print("je ", out.count("\nje "))
print("jbe", out.count("\njbe "))
print("jb ", out.count("\njb "))
print("jae", out.count("\njae "))
print("jmp", out.count("\njmp "))
