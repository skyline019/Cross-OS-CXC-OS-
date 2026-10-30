#!/usr/bin/env python3
"""Check relative markdown links under the repo (docs / thesis / root)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def main() -> int:
    md_files = (
        list(ROOT.glob("*.md"))
        + list((ROOT / "docs").rglob("*.md"))
        + list((ROOT / "thesis").rglob("*.md"))
    )
    broken = []
    checked = 0
    for f in md_files:
        text = f.read_text(encoding="utf-8", errors="replace")
        for m in LINK_RE.finditer(text):
            url = m.group(1).strip()
            if url.startswith(("http://", "https://", "mailto:", "#")):
                continue
            url = url.split("#", 1)[0].split("?", 1)[0]
            if not url:
                continue
            checked += 1
            target = (f.parent / url).resolve()
            try:
                target.relative_to(ROOT)
            except ValueError:
                # outside repo — still require exists
                pass
            if not target.exists():
                broken.append((str(f.relative_to(ROOT)), url))
    print(f"checked_links={checked} broken={len(broken)}")
    for a, b in broken:
        print(f"BROKEN {a} -> {b}")
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
