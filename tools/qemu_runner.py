#!/usr/bin/env python3
"""QEMU/OVMF process lifecycle for CXC/OS tests."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

from network_peer import (
    DatagramPeer,
    NIC_MAC,
    PROTOCOL_READY_MARKER,
    READY_MARKER,
    UDP_FAULT_KINDS,
    UDP_PAYLOAD_SIZES,
    UDP_READY_MARKER,
    UDP_STRESS_READY_MARKER,
)

FIRMWARE_CANDIDATES = [
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
    ("/usr/share/edk2/x64/OVMF_CODE.4m.fd", "/usr/share/edk2/x64/OVMF_VARS.fd"),
    ("/usr/share/edk2/x64/OVMF_CODE.fd", "/usr/share/edk2/x64/OVMF_VARS.fd"),
]

# M75: second disk for virtio-blk probe (sector 0 magic CXCBLK01).
VIRTIO_BLK_MAGIC = b"CXCBLK01"
VIRTIO_BLK_BYTES = 1024 * 1024


def ensure_virtio_blk_image(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    need_write = True
    if path.is_file() and path.stat().st_size == VIRTIO_BLK_BYTES:
        with path.open("rb") as handle:
            need_write = handle.read(len(VIRTIO_BLK_MAGIC)) != VIRTIO_BLK_MAGIC
    if need_write:
        payload = bytearray(VIRTIO_BLK_BYTES)
        payload[0:8] = VIRTIO_BLK_MAGIC
        path.write_bytes(payload)
    return path



def find_firmware(code: str | None, variables: str | None) -> tuple[Path, Path]:
    if code and variables:
        return Path(code), Path(variables)
    for code_path, variables_path in FIRMWARE_CANDIDATES:
        if Path(code_path).is_file() and Path(variables_path).is_file():
            return Path(code_path), Path(variables_path)
    raise RuntimeError(
        "OVMF firmware not found; pass --ovmf-code and --ovmf-vars explicitly"
    )



def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--qemu", default=shutil.which("qemu-system-x86_64"))
    parser.add_argument("--ovmf-code", default=os.environ.get("OVMF_CODE"))
    parser.add_argument("--ovmf-vars", default=os.environ.get("OVMF_VARS"))
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--check", action="store_true", help="wait for serial and network evidence")
    parser.add_argument("--expect", default="CXC_OS_SELF_TEST_PASS")
    parser.add_argument("--memory", default="128M", help="QEMU guest RAM size, e.g. 64M or 256M")
    parser.add_argument("--cpu", default="qemu64", help="QEMU CPU model/properties")
    parser.add_argument("--cpus", type=int, default=4, help="guest CPU count (1-16); default: 4")
    parser.add_argument("--no-edu", action="store_true", help="do not attach QEMU's EDU device")
    parser.add_argument("--no-e1000e", action="store_true", help="do not attach the 82574L device")
    parser.add_argument(
        "--no-virtio-blk",
        action="store_true",
        help="do not attach virtio-blk-pci (negative M75 control)",
    )
    parser.add_argument(
        "--no-network-peer",
        action="store_true",
        help="attach e1000e without the datagram peer (negative raw-DMA control)",
    )
    parser.add_argument(
        "--no-protocol-peer",
        action="store_true",
        help="stop after the raw exchange (negative ARP/ICMP control)",
    )
    parser.add_argument(
        "--bad-icmp-checksum",
        action="store_true",
        help="corrupt the ICMP request checksum (negative validation control)",
    )
    parser.add_argument(
        "--no-udp-peer",
        action="store_true",
        help="stop after ARP/ICMP (negative UDP/ring control)",
    )
    parser.add_argument(
        "--bad-udp-checksum",
        action="store_true",
        help="corrupt the first UDP request checksum (negative validation control)",
    )
    parser.add_argument(
        "--udp-fault",
        choices=UDP_FAULT_KINDS,
        help="inject one isolated UDP validation fault (negative contract test)",
    )
    parser.add_argument(
        "--udp-fault-index",
        type=int,
        help="datagram index for --udp-fault (default: 0)",
    )
    parser.add_argument(
        "--power-cut",
        choices=("packet-ready", "protocol-ready", "udp-ready"),
        help="SIGKILL QEMU at a serial phase boundary (hard power-cut test)",
    )
    parser.add_argument(
        "--power-cut-after-udp",
        type=int,
        metavar="N",
        help="SIGKILL QEMU after N validated UDP replies (1-9)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="seconds to wait for serial/network evidence (M14 stress needs ~60s)",
    )
    arguments = parser.parse_args()

    if not arguments.qemu:
        parser.error("qemu-system-x86_64 was not found; use --qemu")
    if not 1 <= arguments.cpus <= 16:
        parser.error("--cpus must be between 1 and 16")
    phase_controls = (
        arguments.no_protocol_peer,
        arguments.bad_icmp_checksum,
        arguments.no_udp_peer,
        arguments.bad_udp_checksum,
        arguments.udp_fault is not None,
    )
    if arguments.no_network_peer and any(phase_controls):
        parser.error("--no-network-peer excludes protocol negative controls")
    if sum(bool(control) for control in phase_controls) > 1:
        parser.error("protocol negative controls are mutually exclusive")
    udp_fault = None
    if arguments.udp_fault is not None:
        fault_index = arguments.udp_fault_index or 0
        if not 0 <= fault_index < len(UDP_PAYLOAD_SIZES):
            parser.error("--udp-fault-index must be between 0 and 9")
        if arguments.udp_fault == "padding" and fault_index > 6:
            parser.error("padding faults require a short datagram at index 0-6")
        if arguments.udp_fault == "truncated" and fault_index < 7:
            parser.error("truncation faults require an unpadded datagram at index 7-9")
        udp_fault = (arguments.udp_fault, fault_index)
    elif arguments.udp_fault_index is not None:
        parser.error("--udp-fault-index requires --udp-fault")
    power_cut = (
        arguments.power_cut is not None
        or arguments.power_cut_after_udp is not None
    )
    if arguments.power_cut is not None and arguments.power_cut_after_udp is not None:
        parser.error("power-cut controls are mutually exclusive")
    if power_cut:
        if not arguments.check:
            parser.error("power-cut tests require --check")
        if arguments.no_edu or arguments.no_e1000e or arguments.no_network_peer:
            parser.error("power-cut tests require the normal EDU/e1000e/peer setup")
        if any(phase_controls):
            parser.error("power-cut tests exclude protocol negative controls")
        if arguments.power_cut_after_udp is not None and not (
            1 <= arguments.power_cut_after_udp < len(UDP_PAYLOAD_SIZES)
        ):
            parser.error("--power-cut-after-udp must be between 1 and 9")
    image = arguments.image.resolve()
    if not image.is_file():
        parser.error(f"image does not exist: {image}")

    try:
        code, variables = find_firmware(arguments.ovmf_code, arguments.ovmf_vars)
    except RuntimeError as error:
        parser.error(str(error))
    if not code.is_file() or not variables.is_file():
        parser.error("OVMF code/vars firmware path is invalid")

    work_dir = arguments.work_dir or image.parent / "qemu"
    work_dir.mkdir(parents=True, exist_ok=True)
    variables_copy = work_dir / "OVMF_VARS.fd"
    shutil.copyfile(variables, variables_copy)
    peer = None
    if not arguments.no_e1000e and not arguments.no_network_peer:
        peer = DatagramPeer(
            work_dir,
            synchronized=arguments.check,
            protocol_enabled=not arguments.no_protocol_peer,
            corrupt_icmp_checksum=arguments.bad_icmp_checksum,
            udp_enabled=not arguments.no_udp_peer,
            corrupt_udp_checksum=arguments.bad_udp_checksum,
            udp_fault=udp_fault,
            power_cut_after_udp=arguments.power_cut_after_udp,
            power_cut_marker=arguments.power_cut,
        )
    # Early expected panics never reach some or all network phases. External
    # evidence is mandatory for successful boot tests, not expected panics.
    peer_required = peer is not None and not arguments.expect.startswith(
        "CXC_OS_PANIC:"
    )

    command = [
        str(arguments.qemu),
        "-machine", "q35,accel=tcg",
        "-cpu", arguments.cpu,
        "-smp", str(arguments.cpus),
        "-m", arguments.memory,
        "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,file={variables_copy}",
        "-drive", f"if=ide,format=raw,file={image},snapshot=on",
        "-display", "none",
        "-serial", "stdio",
        "-monitor", "none",
        "-net", "none",
        "-no-reboot",
    ]
    if not arguments.no_edu:
        command += ["-device", "edu"]
    if not arguments.no_e1000e:
        if peer is not None:
            command += peer.qemu_arguments()
        else:
            command += ["-device", f"e1000e,mac={NIC_MAC.hex(':')}"]
    if not arguments.no_virtio_blk:
        virtio_img = ensure_virtio_blk_image(work_dir / "virtio-blk.img")
        command += [
            "-drive",
            f"if=none,id=virtioblk0,format=raw,file={virtio_img},snapshot=on",
            "-device",
            "virtio-blk-pci,drive=virtioblk0,disable-legacy=on",
        ]

    if not arguments.check:
        if peer is not None:
            peer.start(time.monotonic() + arguments.timeout)
        try:
            return subprocess.call(command)
        finally:
            if peer is not None:
                peer.finish(allow_response=False)

    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
    )
    assert process.stdout is not None
    output = bytearray()
    lock = threading.Lock()

    def collect_output() -> None:
        while True:
            chunk = process.stdout.read(4096)
            if not chunk:
                return
            with lock:
                output.extend(chunk)
                if peer is not None and READY_MARKER in output:
                    peer.notify_ready()
                if peer is not None and PROTOCOL_READY_MARKER in output:
                    peer.notify_protocol_ready()
                if peer is not None and UDP_READY_MARKER in output:
                    peer.notify_udp_ready()
                if peer is not None and UDP_STRESS_READY_MARKER in output:
                    peer.notify_udp_stress_ready()

    deadline = time.monotonic() + arguments.timeout
    if peer is not None:
        peer.start(deadline)
    reader = threading.Thread(target=collect_output, daemon=True)
    reader.start()
    serial_passed = False
    power_cut_observed = False
    power_cut_reason = ""
    try:
        while True:
            with lock:
                if power_cut:
                    marker = {
                        "packet-ready": READY_MARKER,
                        "protocol-ready": PROTOCOL_READY_MARKER,
                        "udp-ready": UDP_READY_MARKER,
                    }.get(arguments.power_cut)
                    if marker is not None and marker in output:
                        power_cut_observed = True
                        power_cut_reason = arguments.power_cut
                    elif (
                        arguments.power_cut_after_udp is not None
                        and peer is not None
                        and peer.udp_replies_validated
                        >= arguments.power_cut_after_udp
                    ):
                        power_cut_observed = True
                        power_cut_reason = (
                            f"after {arguments.power_cut_after_udp} UDP replies"
                        )
                    if power_cut_observed:
                        process.kill()
                        break
                if arguments.expect.encode() in output:
                    serial_passed = True
                    break
            if process.poll() is not None or time.monotonic() >= deadline:
                break
            time.sleep(0.05)
    finally:
        if process.poll() is None:
            if power_cut_observed:
                process.kill()
            else:
                process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)
        reader.join(timeout=2)
        if peer is not None:
            peer.finish(allow_response=serial_passed and peer_required)

    with lock:
        captured = bytes(output)
    (work_dir / "serial.log").write_bytes(captured)
    sys.stdout.buffer.write(captured)
    sys.stdout.buffer.flush()
    if peer is not None:
        peer_log = work_dir / "network-peer.log"
        if peer_log.is_file():
            sys.stdout.write(peer_log.read_text(encoding="utf-8"))
            sys.stdout.flush()

    if power_cut:
        if power_cut_observed and b"CXC_OS_SELF_TEST_PASS" not in captured:
            print(
                f"\nQEMU hard power-cut contract matched ({power_cut_reason}); "
                f"logs: {work_dir}"
            )
            return 0
        print(
            f"\nQEMU power-cut contract failed; timeout {arguments.timeout:g}s; "
            f"logs: {work_dir}",
            file=sys.stderr,
        )
        return 1

    if serial_passed and (not peer_required or peer.ok):
        print(f"\nQEMU serial/network expectations matched; logs: {work_dir}")
        return 0
    detail = "serial marker not seen" if not serial_passed else "network peer validation failed"
    print(
        f"\nQEMU self-test failed: {detail}; timeout {arguments.timeout:g}s; "
        f"logs: {work_dir}",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

