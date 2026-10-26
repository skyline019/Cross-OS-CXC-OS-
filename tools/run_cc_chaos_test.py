#!/usr/bin/env python3
"""Build and run host-side cxc toolchain chaos tests (M134)."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CC_DIR = ROOT / "user" / "cxc_cc"
SOURCES = [
    ROOT / "tools" / "cc_chaos_test.c",
    CC_DIR / "lex.c",
    CC_DIR / "parse.c",
    CC_DIR / "emit.c",
    CC_DIR / "prep.c",
    CC_DIR / "elflink.c",
    CC_DIR / "as.c",
    CC_DIR / "ld.c",
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
    raise RuntimeError("no host C compiler (gcc/clang[/wsl]) found")


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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    prefix, cc = find_cc()
    use_wsl = bool(prefix)
    work = Path(tempfile.mkdtemp(prefix="cxc-cc-chaos-"))
    try:
        out = work / ("cc_chaos_test" if use_wsl else "cc_chaos_test.exe")
        cmd = [
            *prefix,
            cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DCC_HOST=1",
            f"-I{to_build_path(ROOT / 'user', use_wsl)}",
            "-o",
            to_build_path(out, use_wsl),
            *[to_build_path(p, use_wsl) for p in SOURCES],
        ]
        subprocess.check_call(cmd)
        disk = work / "disk"
        disk.mkdir(parents=True, exist_ok=True)
        if use_wsl:
            work_u = to_build_path(work, True)
            out_u = to_build_path(out, True)
            result = subprocess.run(
                [*prefix, "sh", "-c", f"cd '{work_u}' && '{out_u}'"],
                check=False,
                capture_output=True,
                text=True,
            )
        else:
            result = subprocess.run(
                [str(out)],
                check=False,
                capture_output=True,
                text=True,
                cwd=str(work),
            )
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            return result.returncode
        if "cc_chaos_test: PASS" not in result.stdout:
            print("missing PASS line", file=sys.stderr)
            return 1
        return 0
    finally:
        if args.keep:
            print(work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"run_cc_chaos_test: {exc}", file=sys.stderr)
        raise SystemExit(1)
