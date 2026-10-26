#!/usr/bin/env python3
"""Build and run host-side cxc-cc strict compile tests (M97)."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CC_DIR = ROOT / "user" / "cxc_cc"
SOURCES = [
    ROOT / "tools" / "cc_host_test.c",
    CC_DIR / "lex.c",
    CC_DIR / "parse.c",
    CC_DIR / "emit.c",
    CC_DIR / "prep.c",
    CC_DIR / "elflink.c",
    CC_DIR / "as.c",
    CC_DIR / "ld.c",
]


def find_cc() -> tuple[list[str], str]:
    """Return (prefix_argv, compiler_path). prefix may be ['wsl','-e'] on Windows."""
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
    # E:\foo → /mnt/e/foo
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
    work = Path(tempfile.mkdtemp(prefix="cxc-cc-host-"))
    try:
        out = work / ("cc_host_test" if use_wsl else "cc_host_test.exe")
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
        # M101: plant disk/lib.cx for #include host coverage (run cwd = work).
        disk = work / "disk"
        disk.mkdir(parents=True, exist_ok=True)
        (disk / "lib.cx").write_text(
            "fn add(a,b){\nreturn a+b;\n}\n", encoding="utf-8"
        )
        (disk / "lib.c").write_text(
            "fn add(a,b){\nreturn a+b;\n}\n", encoding="utf-8"
        )
        rt = ROOT / "user" / "prebuilt" / "rtmath.elf"
        (disk / "rt.elf").write_bytes(rt.read_bytes())
        crt = ROOT / "user" / "prebuilt" / "rtcrt.elf"
        (disk / "crt.elf").write_bytes(crt.read_bytes())
        io = ROOT / "user" / "prebuilt" / "rtio.elf"
        (disk / "io.elf").write_bytes(io.read_bytes())
        math = ROOT / "user" / "prebuilt" / "host_add.o"
        (disk / "math.o").write_bytes(math.read_bytes())
        host = ROOT / "user" / "prebuilt" / "host_bump.o"
        (disk / "host.o").write_bytes(host.read_bytes())
        musl = ROOT / "user" / "prebuilt" / "musl_labs.o"
        (disk / "musl.o").write_bytes(musl.read_bytes())
        (disk / "labs.o").write_bytes(musl.read_bytes())
        dig = ROOT / "user" / "prebuilt" / "musl_isdigit.o"
        (disk / "dig.o").write_bytes(dig.read_bytes())
        (disk / "slen.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_strlen.o").read_bytes()
        )
        (disk / "mcmp.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_memcmp.o").read_bytes()
        )
        (disk / "mcpy.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_memcpy.o").read_bytes()
        )
        (disk / "atoi.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_atoi.o").read_bytes()
        )
        (disk / "scmp.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_strcmp.o").read_bytes()
        )
        (disk / "stpc.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_stpcpy.o").read_bytes()
        )
        (disk / "scpy.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_strcpy.o").read_bytes()
        )
        (disk / "sncmp.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_strncmp.o").read_bytes()
        )
        (disk / "scat.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_strcat.o").read_bytes()
        )
        (disk / "schr.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_strchr.o").read_bytes()
        )
        (disk / "snul.o").write_bytes(
            (ROOT / "user" / "prebuilt" / "musl_strchrnul.o").read_bytes()
        )
        if use_wsl:
            work_u = to_build_path(work, True)
            out_u = to_build_path(out, True)
            run_cmd = [
                *prefix,
                "sh",
                "-c",
                f"cd '{work_u}' && '{out_u}'",
            ]
            result = subprocess.run(
                run_cmd, check=False, capture_output=True, text=True
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
        if "cc_host_test: PASS" not in result.stdout:
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
        print(f"run_cc_host_test: {exc}", file=sys.stderr)
        raise SystemExit(1)
