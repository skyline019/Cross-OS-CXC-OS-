#!/usr/bin/env python3
"""One-shot CXC/OS release validation: host, CI subset, or full M13 matrix."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
E1000E_PANIC = "CXC_OS_PANIC: e1000e MSI-X"
EDU_PANIC = "CXC_OS_PANIC: EDU MSI/DMA"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(command), flush=True)
    completed = subprocess.run(command, cwd=cwd, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def ensure_work_dir(path: Path) -> Path:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True)
    return path


def write_manifest(work_dir: Path, extra: dict) -> None:
    files = sorted(
        path for path in work_dir.rglob("*")
        if path.is_file() and path.name not in ("SHA256SUMS", "release-manifest.json")
    )
    entries = [
        {
            "path": path.relative_to(work_dir).as_posix(),
            "sha256": file_sha256(path),
            "bytes": path.stat().st_size,
        }
        for path in files
    ]
    payload = {
        "contract": "CXC/OS release_validate",
        "generated_unix": int(time.time()),
        **extra,
        "files": entries,
    }
    (work_dir / "release-manifest.json").write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    lines = [f"{entry['sha256']}  {entry['path']}" for entry in entries]
    (work_dir / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")


def clean_build(build_dir: Path, build_type: str) -> Path:
    if build_dir.exists():
        shutil.rmtree(build_dir)
    run(
        [
            "cmake",
            "-S",
            str(PROJECT_ROOT),
            "-B",
            str(build_dir),
            f"-DCMAKE_TOOLCHAIN_FILE={PROJECT_ROOT / 'cmake' / 'x86_64-uefi-mingw.cmake'}",
            f"-DCMAKE_BUILD_TYPE={build_type}",
            "-DCXC_WARNINGS_AS_ERRORS=ON",
        ]
    )
    run(["cmake", "--build", str(build_dir), "--parallel"])
    image = build_dir / "cxc-os.img"
    if not image.is_file():
        raise SystemExit(f"missing ESP image: {image}")
    return image


def run_host_contract(work_dir: Path, seed: int) -> None:
    host_dir = ensure_work_dir(work_dir / "contract-host")
    run(
        [
            sys.executable,
            str(PROJECT_ROOT / "tools" / "contract_test.py"),
            "--host-only",
            "--work-dir",
            str(host_dir),
            "--seed",
            str(seed),
        ]
    )


def run_qemu(
    image: Path,
    work_dir: Path,
    *,
    expect: str = "CXC_OS_SELF_TEST_PASS",
    extra: list[str] | None = None,
) -> None:
    command = [
        sys.executable,
        str(PROJECT_ROOT / "tools" / "run_qemu.py"),
        "--image",
        str(image),
        "--check",
        "--work-dir",
        str(work_dir),
        "--expect",
        expect,
    ]
    if extra:
        command.extend(extra)
    run(command)


def profile_host(work_dir: Path, seed: int) -> dict:
    run_host_contract(work_dir, seed)
    return {"profile": "host", "image": None}


def profile_ci(work_dir: Path, seed: int, build_type: str, image: Path | None) -> dict:
    """Mirror the Linux job subset in .github/workflows/ci.yml."""
    if image is None:
        image = clean_build(work_dir / "uefi", build_type)
    else:
        image = image.resolve()
        if not image.is_file():
            raise SystemExit(f"image does not exist: {image}")

    run_host_contract(work_dir, seed)
    run_qemu(image, work_dir / "qemu-test")

    for name, extra in (
        ("qemu-128m", ["--memory", "128M"]),
        ("qemu-256m", ["--memory", "256M"]),
        ("qemu-512m-dma-mask", ["--cpu", "qemu64,+x2apic", "--memory", "512M"]),
        ("qemu-xapic", ["--cpu", "qemu64,-x2apic"]),
        ("qemu-x2apic", ["--cpu", "qemu64,+x2apic"]),
        ("qemu-up", ["--cpus", "1", "--cpu", "qemu64,+x2apic"]),
        ("qemu-smp-8", ["--cpus", "8", "--cpu", "qemu64,+x2apic"]),
    ):
        run_qemu(image, work_dir / name, extra=extra)

    run_qemu(
        image,
        work_dir / "qemu-no-edu",
        expect=EDU_PANIC,
        extra=["--no-edu"],
    )
    run_qemu(
        image,
        work_dir / "qemu-no-e1000e",
        expect=E1000E_PANIC,
        extra=["--no-e1000e"],
    )

    contract_dir = ensure_work_dir(work_dir / "contract-full")
    run(
        [
            sys.executable,
            str(PROJECT_ROOT / "tools" / "contract_test.py"),
            "--image",
            str(image),
            "--work-dir",
            str(contract_dir),
            "--seed",
            str(seed),
        ]
    )
    return {
        "profile": "ci",
        "image": str(image),
        "image_sha256": file_sha256(image),
        "image_bytes": image.stat().st_size,
    }


def profile_full(work_dir: Path, seed: int, build_type: str, image: Path | None) -> dict:
    """Clean build, M13 success matrix, seven negative controls, then contract."""
    if image is None:
        image = clean_build(work_dir / "uefi", build_type)
    else:
        image = image.resolve()
        if not image.is_file():
            raise SystemExit(f"image does not exist: {image}")

    run_host_contract(work_dir, seed)

    success = (
        ("default", []),
        ("xapic", ["--cpu", "qemu64,-x2apic"]),
        ("x2apic", ["--cpu", "qemu64,+x2apic"]),
        ("up", ["--cpus", "1", "--cpu", "qemu64,+x2apic"]),
        ("smp2", ["--cpus", "2", "--cpu", "qemu64,+x2apic"]),
        ("smp8", ["--cpus", "8", "--cpu", "qemu64,+x2apic"]),
        ("smp16", ["--cpus", "16", "--cpu", "qemu64,+x2apic"]),
        ("mem128", ["--memory", "128M"]),
        ("mem256", ["--memory", "256M"]),
        ("mem512", ["--cpu", "qemu64,+x2apic", "--memory", "512M"]),
        ("smp16-512", ["--cpus", "16", "--cpu", "qemu64,+x2apic", "--memory", "512M"]),
        ("xapic-repeat", ["--cpu", "qemu64,-x2apic"]),
    )
    for name, extra in success:
        run_qemu(image, work_dir / "matrix" / name, extra=extra)

    negatives = (
        ("no-edu", EDU_PANIC, ["--no-edu"]),
        ("no-e1000e", E1000E_PANIC, ["--no-e1000e"]),
        ("no-network-peer", E1000E_PANIC, ["--no-network-peer"]),
        ("no-protocol-peer", E1000E_PANIC, ["--no-protocol-peer"]),
        ("bad-icmp", E1000E_PANIC, ["--bad-icmp-checksum"]),
        ("no-udp", E1000E_PANIC, ["--no-udp-peer"]),
        ("bad-udp", E1000E_PANIC, ["--bad-udp-checksum"]),
    )
    for name, expect, extra in negatives:
        run_qemu(image, work_dir / "negative" / name, expect=expect, extra=extra)

    contract_dir = ensure_work_dir(work_dir / "contract-full")
    run(
        [
            sys.executable,
            str(PROJECT_ROOT / "tools" / "contract_test.py"),
            "--image",
            str(image),
            "--work-dir",
            str(contract_dir),
            "--seed",
            str(seed),
        ]
    )
    return {
        "profile": "full",
        "image": str(image),
        "image_sha256": file_sha256(image),
        "image_bytes": image.stat().st_size,
        "success_matrix": len(success),
        "negative_controls": len(negatives),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--profile",
        choices=("host", "ci", "full"),
        default="host",
        help="host: Python contract only; ci: Linux CI subset; full: clean build + matrix",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=PROJECT_ROOT / "build" / "release-validate",
    )
    parser.add_argument("--seed", type=int, default=20261025)
    parser.add_argument(
        "--build-type",
        choices=("Debug", "Release"),
        default="Release",
    )
    parser.add_argument(
        "--image",
        type=Path,
        help="reuse an existing ESP image (skips clean build for ci/full)",
    )
    parser.add_argument(
        "--keep-work-dir",
        action="store_true",
        help="do not wipe --work-dir before starting",
    )
    arguments = parser.parse_args()
    work_dir = arguments.work_dir.resolve()
    if work_dir in (Path("/"), PROJECT_ROOT):
        parser.error("unsafe --work-dir")
    if not arguments.keep_work_dir:
        ensure_work_dir(work_dir)
    else:
        work_dir.mkdir(parents=True, exist_ok=True)

    started = time.monotonic()
    if arguments.profile == "host":
        meta = profile_host(work_dir, arguments.seed)
    elif arguments.profile == "ci":
        meta = profile_ci(work_dir, arguments.seed, arguments.build_type, arguments.image)
    else:
        meta = profile_full(
            work_dir, arguments.seed, arguments.build_type, arguments.image
        )

    meta["seconds"] = round(time.monotonic() - started, 2)
    meta["overall"] = "PASS"
    write_manifest(work_dir, meta)
    print(
        f"release_validate profile={arguments.profile} PASS "
        f"({meta['seconds']}s); manifest: {work_dir / 'release-manifest.json'}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
