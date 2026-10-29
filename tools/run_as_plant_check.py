#!/usr/bin/env python3
"""Host-assemble all user/plant_src/*.s and verify exported globals (M295)."""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CC_DIR = ROOT / "user" / "cxc_cc"
SOURCES = [
    ROOT / "tools" / "_as_plant_check.c",
    CC_DIR / "as.c",
    CC_DIR / "emit.c",
    CC_DIR / "elflink.c",
    CC_DIR / "prep.c",
]


def find_cc() -> tuple[list[str], str]:
    for name in ("gcc", "clang"):
        path = shutil.which(name)
        if path:
            return [], path
    wsl = shutil.which("wsl")
    if wsl:
        probe = subprocess.run(
            [wsl, "-e", "sh", "-c", "command -v gcc || command -v clang"],
            check=False,
            capture_output=True,
            text=True,
        )
        cc = (probe.stdout or "").strip().splitlines()
        if probe.returncode == 0 and cc:
            return [wsl, "-e"], cc[0]
    raise RuntimeError("no host C compiler found")


def to_build_path(path: Path, use_wsl: bool) -> str:
    text = str(path.resolve())
    if not use_wsl:
        return text
    if len(text) >= 3 and text[1] == ":" and text[2] in "\\/":
        drive = text[0].lower()
        rest = text[3:].replace("\\", "/")
        return f"/mnt/{drive}/{rest}"
    return text.replace("\\", "/")


def main() -> int:
    prefix, cc = find_cc()
    use_wsl = bool(prefix)
    work = Path(tempfile.mkdtemp(prefix="cxc-as-plant-"))
    out = work / ("as_plant_check" if use_wsl else "as_plant_check.exe")
    cmd = [
        *prefix,
        cc,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-DCC_HOST=1",
        f"-I{to_build_path(ROOT / 'user', use_wsl)}",
        "-o",
        to_build_path(out, use_wsl),
        *[to_build_path(p, use_wsl) for p in SOURCES],
    ]
    subprocess.check_call(cmd)
    run = [*prefix, to_build_path(out, use_wsl)] if use_wsl else [str(out)]
    completed = subprocess.run(
        run,
        cwd=str(ROOT),
        check=False,
        capture_output=True,
        text=True,
    )
    sys.stdout.write(completed.stdout)
    sys.stderr.write(completed.stderr)
    if completed.returncode != 0:
        print("as_plant_check: FAIL", file=sys.stderr)
        return completed.returncode or 1
    print("as_plant_check: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
