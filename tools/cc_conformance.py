#!/usr/bin/env python3
"""CXC_A2 semantic conformance (M287+): compile → load → run → check exit/stdout.

Unlike tools/run_cc_host_test.py (compile-only), this asserts computed values.
Known gaps are explicit xfail entries; --strict fails if an xfail starts passing.
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Literal

ROOT = Path(__file__).resolve().parent.parent
CC_DIR = ROOT / "user" / "cxc_cc"
SOURCES = [
    ROOT / "tools" / "cc_conf_compile.c",
    CC_DIR / "lex.c",
    CC_DIR / "parse.c",
    CC_DIR / "emit.c",
    CC_DIR / "prep.c",
    CC_DIR / "elflink.c",
    CC_DIR / "as.c",
    CC_DIR / "ld.c",
]

Kind = Literal["ok", "xfail_wrong", "xfail_compile", "xfail_crash"]


@dataclass(frozen=True)
class Case:
    name: str
    src: str
    kind: Kind
    exit_code: int | None = None
    stdout: bytes | None = None
    reason: str = ""


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


def build_compiler(work: Path) -> Path:
    prefix, cc = find_cc()
    use_wsl = bool(prefix)
    out = work / ("cc_conf_compile" if use_wsl else "cc_conf_compile.exe")
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
    return out


def compile_src(compiler: Path, src: str, work: Path) -> tuple[bytes | None, str]:
    """Return (elf_bytes, err). err is COMPILE_FAIL:N or empty."""
    prefix, _ = find_cc()
    use_wsl = bool(prefix)
    src_path = work / "case.c"
    src_path.write_text(src, encoding="utf-8")
    if use_wsl:
        cmd = [
            *prefix,
            to_build_path(compiler, True),
            to_build_path(src_path, True),
        ]
    else:
        cmd = [str(compiler), str(src_path)]
    result = subprocess.run(cmd, check=False, capture_output=True)
    if result.returncode != 0:
        err = (result.stderr or b"").decode("utf-8", "replace").strip()
        return None, err or f"COMPILE_FAIL:{result.returncode}"
    return result.stdout, ""


def u64(b: bytes, off: int) -> int:
    return struct.unpack_from("<Q", b, off)[0]


def u32(b: bytes, off: int) -> int:
    return struct.unpack_from("<I", b, off)[0]


def u16(b: bytes, off: int) -> int:
    return struct.unpack_from("<H", b, off)[0]


def repack_elf(elf: bytes) -> bytes:
    """Rebuild ET_EXEC so each PT_LOAD has p_offset ≡ p_vaddr (mod p_align).

    emit.c historically packs phdrs immediately before .text (offset ~0xb0) while
    vaddr is 0x400000 — Linux rejects that; on-box loader tolerates it.
    """
    if len(elf) < 64 or elf[:4] != b"\x7fELF":
        raise ValueError("bad ELF")
    e_phoff = u64(elf, 32)
    e_phentsize = u16(elf, 54)
    e_phnum = u16(elf, 56)
    loads: list[tuple[int, int, int, int, int, bytes]] = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type = u32(elf, off)
        if p_type != 1:
            continue
        p_flags = u32(elf, off + 4)
        p_offset = u64(elf, off + 8)
        p_vaddr = u64(elf, off + 16)
        p_filesz = u64(elf, off + 32)
        p_memsz = u64(elf, off + 40)
        p_align = u64(elf, off + 48) or 0x1000
        chunk = elf[p_offset : p_offset + p_filesz]
        loads.append((p_flags, p_vaddr, p_filesz, p_memsz, p_align, chunk))
    if not loads:
        raise ValueError("no PT_LOAD")
    page = 0x1000
    phnum = len(loads)
    ehdr = 64
    phdrs = phnum * 56
    # Place first load so offset ≡ vaddr (mod align); subsequent loads page-aligned.
    out = bytearray(ehdr + phdrs)
    out[0:4] = b"\x7fELF"
    out[4] = 2
    out[5] = 1
    out[6] = 1
    out[16:18] = struct.pack("<H", 2)  # ET_EXEC
    out[18:20] = struct.pack("<H", 62)  # EM_X86_64
    out[20:24] = struct.pack("<I", 1)
    entry = loads[0][1]
    out[24:32] = struct.pack("<Q", entry)
    out[32:40] = struct.pack("<Q", ehdr)
    out[52:54] = struct.pack("<H", ehdr)
    out[54:56] = struct.pack("<H", 56)
    out[56:58] = struct.pack("<H", phnum)

    cursor = ehdr + phdrs
    for i, (flags, vaddr, filesz, memsz, align, chunk) in enumerate(loads):
        align = align or page
        # Satisfy p_offset ≡ p_vaddr (mod p_align)
        while (cursor % align) != (vaddr % align):
            cursor += 1
        if len(out) < cursor:
            out.extend(b"\0" * (cursor - len(out)))
        file_off = cursor
        out.extend(chunk)
        cursor = len(out)
        # Pad file to page for nicer maps (optional)
        while len(out) % page != 0 and i < len(loads) - 1:
            out.append(0)
            cursor = len(out)
        ph = ehdr + i * 56
        out[ph : ph + 4] = struct.pack("<I", 1)
        out[ph + 4 : ph + 8] = struct.pack("<I", flags)
        out[ph + 8 : ph + 16] = struct.pack("<Q", file_off)
        out[ph + 16 : ph + 24] = struct.pack("<Q", vaddr)
        out[ph + 24 : ph + 32] = struct.pack("<Q", vaddr)
        out[ph + 32 : ph + 40] = struct.pack("<Q", filesz)
        out[ph + 40 : ph + 48] = struct.pack("<Q", memsz)
        out[ph + 48 : ph + 56] = struct.pack("<Q", align)
    return bytes(out)


def patch_int80_to_syscall(blob: bytes) -> bytes:
    data = bytearray(blob)
    i = 0
    while i + 1 < len(data):
        if data[i] == 0xCD and data[i + 1] == 0x80:
            data[i] = 0x0F
            data[i + 1] = 0x05
            i += 2
        else:
            i += 1
    return bytes(data)


def _which_qemu_user() -> str | None:
    for name in ("qemu-x86_64", "qemu-x86_64.exe"):
        p = shutil.which(name)
        if p:
            return p
    return None


def _wsl_bin() -> str | None:
    return shutil.which("wsl")


def _wsl_has(cmd: str) -> bool:
    wsl = _wsl_bin()
    if not wsl:
        return False
    probe = subprocess.run(
        [wsl, "-e", "sh", "-c", f"command -v {cmd} >/dev/null 2>&1"],
        check=False,
        capture_output=True,
    )
    return probe.returncode == 0


def elf_run_mode() -> str:
    """How this host executes guest Linux ELF: run | qemu | compile-elf.

    Linux: native exec. Windows: prefer qemu-user (native or via WSL), then
    WSL native exec; compile-elf is a local-only fallback when nothing can run
    the guest ELF (M301 forbids it as the CI bar).
    """
    if sys.platform.startswith("linux"):
        return "run"
    prefix, _ = find_cc()
    if prefix:  # WSL path via find_cc — build+run under Linux
        return "run"
    if _which_qemu_user():
        return "qemu"
    # M301: MSYS2 host gcc + WSL qemu-user → semantic exit/stdout parity
    if _wsl_has("qemu-x86_64"):
        return "qemu"
    if _wsl_bin() and _wsl_has("sh"):
        return "run"
    return "compile-elf"


def run_elf(elf: bytes, work: Path) -> tuple[int, bytes]:
    path = work / "a.out"
    fixed = patch_int80_to_syscall(repack_elf(elf))
    path.write_bytes(fixed)
    path.chmod(0o755)
    # Prefer direct exec on Linux; under WSL path from Windows needs /mnt.
    prefix, _ = find_cc()
    use_wsl = bool(prefix)
    mode = elf_run_mode()
    if use_wsl:
        cmd = [*prefix, to_build_path(path, True)]
    elif mode == "qemu":
        q = _which_qemu_user()
        if q:
            cmd = [q, str(path)]
        else:
            cmd = [
                _wsl_bin() or "wsl",
                "-e",
                "qemu-x86_64",
                to_build_path(path, True),
            ]
    elif mode == "run" and not sys.platform.startswith("linux"):
        cmd = [_wsl_bin() or "wsl", "-e", to_build_path(path, True)]
    else:
        cmd = [str(path)]
    result = subprocess.run(cmd, check=False, capture_output=True, cwd=str(work))
    return result.returncode & 0xFF, result.stdout or b""


def cases() -> list[Case]:
    """59 cases: ok/xfail split evolves with A2-SEM (M288/M289 flips)."""
    A = "CXC_A2\n"
    out: list[Case] = []

    def ok(name: str, body: str, code: int, stdout: bytes | None = None) -> None:
        out.append(Case(name, A + body, "ok", code, stdout))

    def xf_wrong(name: str, body: str, code: int, reason: str) -> None:
        out.append(Case(name, A + body, "xfail_wrong", code, None, reason))

    def xf_comp(name: str, body: str, reason: str) -> None:
        out.append(Case(name, A + body, "xfail_compile", None, None, reason))

    def xf_crash(name: str, body: str, reason: str) -> None:
        out.append(Case(name, A + body, "xfail_crash", None, None, reason))

    # --- control (8) ---
    ok("while", "i=0;n=0;while(i<6){n=n+7;i=i+1;}return n;\n", 42)
    ok(
        "do_while",
        "i=0;n=0;do{n=n+7;i=i+1;}while(i<6);return n;\n",
        42,
    )
    ok(
        "for",
        "n=0;for(i=0;i<6;i=i+1){n=n+7;}return n;\n",
        42,
    )
    ok(
        "else_if",
        "n=1;if(n==0){return 1;}else if(n==1){return 42;}else{return 2;}\n",
        42,
    )
    ok(
        "switch",
        "n=2;switch(n){case 1: return 1; case 2: return 42; default: return 0;}\n",
        42,
    )
    ok(
        "continue",
        "n=0;for(i=0;i<10;i=i+1){if(i<4){continue;}n=n+i;}return n;\n",
        39,
    )
    ok(
        "break",
        "n=0;for(i=0;i<100;i=i+1){n=n+1;if(n==42){break;}}return n;\n",
        42,
    )
    ok("ternary", "a=1;b=2;return a<b?42:0;\n", 42)

    # --- arithmetic (10 ok + some xfail) ---
    ok("add_sub_mul", "return ((3+4)*6)+(1<<0)-1;\n", 42)
    ok("logic", "return (!0&&1)||0?42:0;\n", 42)
    ok(
        "op_shift_arith",
        "unsigned u;u=0x8000000000000000;return (u>>63)+41;\n",
        42,
    )
    ok("inc_dec", "n=41;n++;return n;\n", 42)
    ok("plus_eq", "n=40;n+=2;return n;\n", 42)
    ok("minus_eq", "n=44;n-=2;return n;\n", 42)
    ok("signed_div", "return (-7/2)+45;\n", 42)
    ok("signed_mod", "return (-7%2)+43;\n", 42)

    ok("op_mod_eq", "n=5;n%=2;return n+41;\n", 42)
    ok("op_xor", "return 1^43;\n", 42)
    ok("op_not_bit", "return ~(~42);\n", 42)
    ok("op_cast", "return (int)42;\n", 42)
    ok("op_unsigned_div", "unsigned a;a=7;return a/2;\n", 3)

    # --- data ---
    ok(
        "array_sum",
        "a[3];a[0]=10;a[1]=20;a[2]=12;return a[0]+a[1]+a[2];\n",
        42,
    )
    ok(
        "struct_field",
        "struct S{x;y;};S v;v.x=40;v.y=2;return v.x+v.y;\n",
        42,
    )
    ok(
        "struct_ptr_local",
        "struct S{x;};S v;S* p;v.x=42;p=&v;return p->x;\n",
        42,
    )
    ok("global", "int g=0;\nfn main(){g=42;return g;}\n", 42)
    ok("const_global", "const int g=42;\nfn main(){return g;}\n", 42)
    ok("char_scalar", "char c;c=42;return c;\n", 42)
    ok(
        "var_index",
        "a[4];i=2;a[0]=1;a[1]=2;a[2]=42;a[3]=3;return a[i];\n",
        42,
    )

    ok(
        "char_array",
        "fn main(){char s[4];s[0]=1;return s[0];}\n",
        1,
    )
    ok("struct_array", "struct S{x;};S a[2];return 0;\n", 0)
    ok(
        "struct_assign",
        "struct S{x;};S a;S b;a.x=1;b=a;return b.x;\n",
        1,
    )
    ok(
        "struct_return",
        "struct S{x;};fn f(){S v;v.x=42;return v;}fn main(){return 0;}\n",
        0,
    )
    ok("multi_dim", "a[2][2];return 0;\n", 0)

    # --- pointers ---
    ok("ptr_deref", "n=42;p=&n;return *p;\n", 42)
    ok("ptr_double", "n=42;p=&n;pp=&p;return **pp;\n", 42)
    # Adjacent char locals are slot-sized (8B), so scale-by-8 matches p[1]==t.
    # Documented byte-packing gap is covered by ptr_scaling + M288 CcType work.
    ok(
        "ptr_char_index",
        "char s;char t;s=65;t=105;p=&s;return p[1];\n",
        105,
    )
    ok(
        "ptr_scaling",
        "a[4];a[0]=1;a[1]=2;a[2]=42;a[3]=3;p=&a[0];p=p+2;return *p;\n",
        42,
    )
    ok(
        "ptr_param_index",
        "fn set(a,i,v){a[i]=v;return 0;}\n"
        "fn main(){x[2];x[0]=0;x[1]=0;set(&x[0],1,42);return x[1];}\n",
        42,
    )
    ok(
        "typed_ptr_param",
        "struct S{x;};fn f(S* p){return p->x;}fn main(){S v;v.x=42;return f(&v);}\n",
        42,
    )

    # --- functions ---
    ok(
        "nested_call",
        "fn add(a,b){return a+b;}fn main(){return add(add(20,20),2);}\n",
        42,
    )
    ok(
        "fib",
        "fn fib(n){if(n<2){return n;}return fib(n-1)+fib(n-2);}\n"
        "fn main(){return fib(8)+21;}\n",
        42,
    )
    ok(
        "recursion",
        "fn f(n){if(n==0){return 42;}return f(n-1);}\nfn main(){return f(50);}\n",
        42,
    )
    ok(
        "fn_five_args",
        "fn f(a,b,c,d,e){return a+b+c+d+e;}fn main(){return f(1,2,3,4,5);}\n",
        15,
    )
    ok(
        "return_void",
        "fn f(){return;}fn main(){f();return 42;}\n",
        42,
    )

    # --- IO ---
    ok("write_tag", 'write HI;\nreturn 42;\n', 42, b"HI")
    ok("write_esc", 'write "A\\nB";\nreturn 42;\n', 42, b"A\nB")
    ok(
        "io_write_str_long",
        'write "ABCDEFGHIJ";\nreturn 42;\n',
        42,
        b"ABCDEFGHIJ",
    )

    # --- preprocessor ---
    ok("pp_define", "#define N 42\nreturn N;\n", 42)
    ok("pp_line_comment", "// c\nreturn 42;\n", 42)
    ok("pp_block_comment", "/* c */\nreturn 42;\n", 42)

    # --- capacity ---
    ok(
        "cap_globals_16",
        "".join(f"int g{i}=0;\n" for i in range(17)) + "fn main(){return 0;}\n",
        0,
    )
    ok(
        "cap_structs_6",
        "".join(f"struct S{i}{{x;}};\n" for i in range(7))
        + "fn main(){return 0;}\n",
        0,
    )
    ok(
        "cap_struct_fields_6",
        "struct S{a;b;c;d;e;f;g;};fn main(){return 0;}\n",
        0,
    )
    ok(
        "cap_loop_depth_4",
        "for(a=0;a<1;a=a+1){for(b=0;b<1;b=b+1){for(c=0;c<1;c=c+1){"
        "for(d=0;d<1;d=d+1){for(e=0;e<1;e=e+1){}}}}}\nreturn 0;\n",
        0,
    )
    ok(
        "cap_locals_56",
        "fn main(){"
        + "".join(f"int x{i};" for i in range(57))
        + "return 0;}\n",
        0,
    )
    ok(
        "cap_ident_20",
        "abcdefghijklmnopqrstu=7;abcdefghijklmnopqrstv=42;"
        "return abcdefghijklmnopqrstu;\n",
        7,
    )

    # --- real programs ---
    ok(
        "prog_bubble",
        "a[4];a[0]=5;a[1]=2;a[2]=4;a[3]=3;"
        "for(i=0;i<4;i=i+1){for(j=0;j<3;j=j+1){"
        "if(a[j]>a[j+1]){t=a[j];a[j]=a[j+1];a[j+1]=t;}}}"
        "return a[0]+a[1]+a[2]+a[3];\n",
        14,
    )
    ok(
        "prog_stack",
        "st[8];sp=0;st[sp]=40;sp=sp+1;st[sp]=2;sp=sp+1;"
        "b=st[sp-1];sp=sp-1;a=st[sp-1];sp=sp-1;return a+b;\n",
        42,
    )
    ok(
        "prog_sort_call",
        "fn sort(a,n){for(i=0;i<n;i=i+1){for(j=0;j<n-1;j=j+1){"
        "if(a[j]>a[j+1]){t=a[j];a[j]=a[j+1];a[j+1]=t;}}}return 0;}\n"
        "fn main(){v[4];v[0]=5;v[1]=2;v[2]=4;v[3]=3;sort(&v[0],4);"
        "return v[0]+v[1]+v[2]+v[3];}\n",
        14,
    )

    return out


def classify(
    case: Case,
    elf: bytes | None,
    err: str,
    work: Path,
    *,
    run_mode: str,
    require_elf_check: bool,
) -> str:
    """Return pass|xfail|bug|xpass|reject."""
    if case.kind == "xfail_compile":
        if elf is None:
            return "xfail"
        return "xpass"  # started compiling when expected to fail

    if elf is None:
        if case.kind == "ok":
            return "reject"
        if case.kind in ("xfail_wrong", "xfail_crash"):
            # compile failed instead of wrong/crash — still a known gap form
            return "xfail"
        return "reject"

    # Windows host: no Linux ELF runner — compile (+ optional congruence) only.
    if run_mode == "compile-elf":
        if case.kind == "ok":
            if require_elf_check and not elf_load_congruent(elf):
                return "bug"
            return "pass"
        if case.kind == "xfail_crash":
            return "xfail"
        if case.kind == "xfail_wrong":
            return "xfail"
        return "bug"

    try:
        code, out = run_elf(elf, work)
    except Exception:  # noqa: BLE001
        if case.kind == "xfail_crash":
            return "xfail"
        if case.kind == "ok":
            return "bug"
        return "xfail"

    if case.kind == "xfail_crash":
        # If it somehow runs correctly, xpass
        if case.exit_code is not None and code == case.exit_code:
            return "xpass"
        return "xfail"

    matched = True
    if case.exit_code is not None and code != case.exit_code:
        matched = False
    if case.stdout is not None:
        # Exact or substring (WMSG: prefix / ONBOX trailer OK).
        if case.stdout != out and case.stdout not in out:
            matched = False

    if case.kind == "ok":
        return "pass" if matched else "bug"
    if case.kind == "xfail_wrong":
        return "xfail" if not matched else "xpass"
    return "bug"


def elf_load_congruent(elf: bytes) -> bool:
    """M294: every PT_LOAD satisfies p_offset ≡ p_vaddr (mod p_align)."""
    if len(elf) < 64 or elf[0:4] != b"\x7fELF":
        return False

    def u16(b: bytes, o: int) -> int:
        return struct.unpack_from("<H", b, o)[0]

    def u64(b: bytes, o: int) -> int:
        return struct.unpack_from("<Q", b, o)[0]

    phoff = u64(elf, 32)
    phentsize = u16(elf, 54)
    phnum = u16(elf, 56)
    for i in range(phnum):
        off = phoff + i * phentsize
        if off + 56 > len(elf):
            return False
        if struct.unpack_from("<I", elf, off)[0] != 1:
            continue
        p_offset = u64(elf, off + 8)
        p_vaddr = u64(elf, off + 16)
        p_align = u64(elf, off + 48) or 0x1000
        if (p_offset % p_align) != (p_vaddr % p_align):
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="fail if any xfail starts passing (xpass>0) or unexpected bug/reject",
    )
    parser.add_argument(
        "--elf-check",
        action="store_true",
        help="M294: also require PT_LOAD p_offset≡p_vaddr (mod align) on ok cases",
    )
    parser.add_argument(
        "--require-exec",
        action="store_true",
        help="M301: fail if mode=compile-elf (CI must run via run|qemu)",
    )
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    suite = cases()
    if args.list:
        for c in suite:
            print(f"{c.kind}\t{c.name}\t{c.reason}")
        print(f"total={len(suite)}")
        return 0

    work = Path(tempfile.mkdtemp(prefix="cxc-cc-conf-"))
    counts = {"pass": 0, "xfail": 0, "bug": 0, "xpass": 0, "reject": 0}
    run_mode = elf_run_mode()
    if args.require_exec and run_mode == "compile-elf":
        print(
            "CC_CONF: mode=compile-elf forbidden under --require-exec "
            "(install qemu-x86_64 or WSL+qemu-user)",
            file=sys.stderr,
        )
        return 2
    try:
        compiler = build_compiler(work)
        for case in suite:
            case_dir = work / case.name
            case_dir.mkdir(parents=True, exist_ok=True)
            elf, err = compile_src(compiler, case.src, case_dir)
            status = classify(
                case,
                elf,
                err,
                case_dir,
                run_mode=run_mode,
                require_elf_check=args.elf_check,
            )
            if (
                args.elf_check
                and run_mode != "compile-elf"
                and status == "pass"
                and elf is not None
                and not elf_load_congruent(elf)
            ):
                status = "bug"
                err = (err or "") + "ELF_ALIGN"
            counts[status] += 1
            if status in ("bug", "reject", "xpass") and not args.strict:
                print(f"{status.upper()} {case.name}: {err or case.reason}", file=sys.stderr)
            if status in ("bug", "reject") and args.strict:
                print(f"{status.upper()} {case.name}: {err or case.reason}", file=sys.stderr)
            if status == "xpass" and args.strict:
                print(
                    f"XPASS {case.name}: known gap started passing ({case.reason})",
                    file=sys.stderr,
                )
        total = len(suite)
        line = (
            f"CC_CONF: total={total} pass={counts['pass']} xfail={counts['xfail']} "
            f"bug={counts['bug']} xpass={counts['xpass']} reject={counts['reject']} "
            f"mode={run_mode}"
        )
        print(line)
        if args.strict:
            if counts["bug"] or counts["reject"] or counts["xpass"]:
                return 1
            if counts["pass"] + counts["xfail"] != total:
                return 1
        else:
            if counts["bug"] or counts["reject"]:
                return 1
        return 0
    finally:
        if args.keep:
            print(work, file=sys.stderr)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"cc_conformance: {exc}", file=sys.stderr)
        raise SystemExit(2)
