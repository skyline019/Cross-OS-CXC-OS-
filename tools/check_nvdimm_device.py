#!/usr/bin/env python3
"""Verify the patched QEMU advertises a separate cold-plugged NVDIMM via QMP.

Uses a temporary ordinary file, not hardware PMEM. This proves device identity,
backend and guest address/size, not cache writeback or power-loss durability.
"""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pmfs-qmp-") as tmp:
        root = Path(tmp)
        backing = root / "nvdimm.img"
        backing.touch()
        with backing.open("r+b") as f:
            f.truncate(32 * 1024 * 1024)
        sock = root / "qmp.sock"
        cmd = [args.qemu, "-machine", "virt,nvdimm=on,acpi=off", "-bios", "none",
               "-m", "128M,maxmem=160M,slots=1", "-S", "-display", "none",
               "-serial", "none", "-monitor", "none",
               "-object", f"memory-backend-file,id=nvd,size=32M,mem-path={backing},share=on",
               "-device", "nvdimm,id=nv0,memdev=nvd,addr=0x88000000",
               "-qmp", f"unix:{sock},server=on,wait=off"]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        conn = socket.socket(socket.AF_UNIX)
        try:
            deadline = time.monotonic() + 30
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(f"QEMU exited {proc.returncode}: {proc.stdout.read().decode(errors='replace')}")
                try:
                    conn.connect(str(sock))
                    break
                except (ConnectionRefusedError, FileNotFoundError):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("QMP socket did not appear")
                    time.sleep(0.05)
            conn.settimeout(10)
            stream = conn.makefile("rwb", buffering=0)
            greeting = json.loads(stream.readline())
            if "QMP" not in greeting:
                raise RuntimeError("missing QMP greeting")

            def execute(name):
                stream.write((json.dumps({"execute": name}) + "\n").encode())
                while True:
                    reply = json.loads(stream.readline())
                    if "error" in reply:
                        raise RuntimeError(f"{name}: {reply['error']}")
                    if "return" in reply:
                        return reply["return"]

            execute("qmp_capabilities")
            devices = execute("query-memory-devices")
            summary = execute("query-memory-size-summary")
            if len(devices) != 1 or devices[0].get("type") != "nvdimm":
                raise RuntimeError(f"not exactly one NVDIMM: {devices}")
            data = devices[0]["data"]
            if (data.get("id") != "nv0" or data.get("addr") != 0x88000000 or
                    data.get("size") != 32 * 1024 * 1024 or
                    not data.get("memdev", "").endswith("/nvd") or
                    summary.get("base-memory") != 128 * 1024 * 1024):
                raise RuntimeError(f"unexpected device/ordinary-RAM layout: {devices}, {summary}")
            print("NVDIMM_QMP_PASS " + json.dumps({"device": devices[0], "memory": summary}, sort_keys=True))
        finally:
            conn.close()
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
            proc.stdout.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"NVDIMM_QMP_FAIL: {exc}")
        raise SystemExit(1)
