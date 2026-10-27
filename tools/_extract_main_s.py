#!/usr/bin/env python3
"""Extract main_s_text from src/user.cpp into a .s file."""
from pathlib import Path
import re
import codecs

root = Path(__file__).resolve().parent.parent
text = (root / "src" / "user.cpp").read_text()
idx = text.find("static const char main_s_text[]")
if idx < 0:
    raise SystemExit("main_s_text not found")
end = text.find("static const char link_rsp_text[]", idx)
chunk = text[idx:end]
parts = re.findall(r'"((?:\\.|[^"\\])*)"', chunk)
src = "".join(codecs.decode(p, "unicode_escape") for p in parts)
out = Path("/tmp/hard_main_full.s")
out.write_text(src)
print("wrote", out, "bytes", len(src), "lines", src.count("\n"))
print("jne", src.count("jne "), "je", src.count("je "), "call", src.count("call "))
print("first40:", repr(src[:80]))
