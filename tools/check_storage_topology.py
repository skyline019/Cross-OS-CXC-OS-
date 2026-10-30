#!/usr/bin/env python3
"""QMP-check a distinct NVDIMM and PCIe NVMe in one patched QEMU virt VM.

This only verifies QEMU device topology and different ordinary-file backends.
It does not boot xv6, test an NVMe command, or prove persistence/power loss.
"""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

NVDIMM_BASE = 0x88000000
NVDIMM_SIZE = 32 * 1024 * 1024
NVME_SIZE = 8 * 1024 * 1024


def pci_devices(buses):
    def walk(devices):
        for device in devices:
            yield device
            yield from walk(device.get("pci_bridge", {}).get("devices", []))

    for bus in buses:
        yield from walk(bus.get("devices", []))


def verify_topology(buses, memories, summary, blocks, nvme_path, nvdimm_path,
                    memory_backend_path):
    if Path(nvme_path).resolve() == Path(nvdimm_path).resolve():
        raise ValueError("NVMe and NVDIMM must use different backend files")
    if Path(memory_backend_path).resolve() != Path(nvdimm_path).resolve():
        raise ValueError("NVDIMM memory backend points to the wrong file")
    devices = [d for d in pci_devices(buses) if d.get("qdev_id") == "nvme0"]
    if len(devices) != 1:
        raise ValueError("expected exactly one nvme0 PCI function")
    nvme = devices[0]
    if nvme.get("bus") != 0 or nvme.get("class_info", {}).get("class") != 0x0108:
        raise ValueError("nvme0 is not an NVM storage controller on PCI bus 0")
    bar = [r for r in nvme.get("regions", []) if r.get("bar") == 0]
    if (len(bar) != 1 or bar[0].get("type") != "memory" or
            not bar[0].get("mem_type_64") or not 4096 <= bar[0].get("size", 0) <= 16 * 1024 * 1024):
        raise ValueError("nvme0 BAR0 is not a supported 64-bit memory BAR")
    if len(memories) != 1 or memories[0].get("type") != "nvdimm":
        raise ValueError("expected one NVDIMM memory device")
    nvdimm = memories[0].get("data", {})
    if (nvdimm.get("id") != "pmem0" or nvdimm.get("addr") != NVDIMM_BASE or
            nvdimm.get("size") != NVDIMM_SIZE or
            not nvdimm.get("memdev", "").endswith("/nvd") or
            summary.get("base-memory") != 128 * 1024 * 1024):
        raise ValueError("unexpected NVDIMM identity, geometry, or RAM layout")
    block = [b for b in blocks if b.get("device") == "x0"]
    if len(block) != 1 or not block[0].get("inserted"):
        raise ValueError("missing NVMe block backend x0")
    filename = block[0]["inserted"].get("file", "")
    if Path(filename).resolve() != Path(nvme_path).resolve():
        raise ValueError("NVMe backend points to the wrong file")
    return {"nvme": nvme, "nvdimm": memories[0], "memory": summary,
            "nvme_backend": filename}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="storage-qmp-") as tmp:
        root = Path(tmp)
        nvdimm = root / "nvdimm.img"
        nvme = root / "nvme.img"
        for path, size in ((nvdimm, NVDIMM_SIZE), (nvme, NVME_SIZE)):
            with path.open("wb") as f:
                f.truncate(size)
        sock = root / "qmp.sock"
        cmd = [args.qemu, "-machine", "virt,nvdimm=on,acpi=off", "-bios", "none",
               "-m", "128M,maxmem=160M,slots=1", "-S", "-display", "none",
               "-serial", "none", "-monitor", "none",
               "-object", f"memory-backend-file,id=nvd,size=32M,mem-path={nvdimm},share=on",
               "-device", "nvdimm,id=pmem0,memdev=nvd,addr=0x88000000",
               "-drive", f"file={nvme},if=none,format=raw,id=x0,cache=writeback",
               "-device", "nvme,id=nvme0,serial=xv6nvme,drive=x0",
               "-qmp", f"unix:{sock},server=on,wait=off"]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        conn = socket.socket(socket.AF_UNIX)
        stream = None
        try:
            deadline = time.monotonic() + 30
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(f"QEMU exited {proc.returncode}: " +
                                       proc.stdout.read().decode(errors="replace"))
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

            def execute(name, arguments=None):
                request = {"execute": name}
                if arguments is not None:
                    request["arguments"] = arguments
                stream.write((json.dumps(request) + "\n").encode())
                while True:
                    line = stream.readline()
                    if not line:
                        raise RuntimeError(f"QMP closed while executing {name}")
                    reply = json.loads(line)
                    if "error" in reply:
                        raise RuntimeError(f"{name}: {reply['error']}")
                    if "return" in reply:
                        return reply["return"]

            execute("qmp_capabilities")
            memory_path = execute("qom-get", {"path": "/objects/nvd", "property": "mem-path"})
            result = verify_topology(execute("query-pci"), execute("query-memory-devices"),
                                     execute("query-memory-size-summary"), execute("query-block"),
                                     nvme, nvdimm, memory_path)
            print("QEMU_STORAGE_TOPOLOGY_PASS " + json.dumps(result, sort_keys=True))
        finally:
            if stream is not None:
                stream.close()
            conn.close()
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
            proc.stdout.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"QEMU_STORAGE_TOPOLOGY_FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
