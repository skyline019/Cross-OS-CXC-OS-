#!/usr/bin/env python3
"""Datagram peer, frame builders and validators for CXC/OS QEMU network tests."""

from __future__ import annotations

import socket
import threading
import time
from pathlib import Path
from typing import Callable

READY_MARKER = b"[net] e1000e packet-ready"
PROTOCOL_READY_MARKER = b"[net] e1000e protocol-ready"
UDP_READY_MARKER = b"[net] e1000e udp-ready"
UDP_STRESS_READY_MARKER = b"[net] e1000e udp-stress-ready"
NIC_MAC = bytes.fromhex("525400435843")
PEER_MAC = bytes.fromhex("024358431101")
NIC_IP = bytes((10, 0, 12, 2))
PEER_IP = bytes((10, 0, 12, 1))
PACKET_SIZE = 60
ICMP_PACKET_SIZE = 74
NIC_UDP_PORT = 7
PEER_UDP_PORT = 0xC123
UDP_PAYLOAD_SIZES = (1, 2, 3, 7, 8, 15, 16, 31, 32, 33)
UDP_PAYLOAD_TOTAL = sum(UDP_PAYLOAD_SIZES)
UDP_STRESS_DATAGRAMS = 256
UDP_STRESS_PAYLOAD_TOTAL = sum(
    UDP_PAYLOAD_SIZES[index % len(UDP_PAYLOAD_SIZES)]
    for index in range(UDP_STRESS_DATAGRAMS)
)


def put16(frame: bytearray, offset: int, value: int) -> None:
    frame[offset:offset + 2] = value.to_bytes(2, "big")


def get16(frame: bytes, offset: int) -> int:
    return int.from_bytes(frame[offset:offset + 2], "big")


def internet_checksum(data: bytes) -> int:
    if len(data) & 1:
        data += b"\x00"
    total = 0
    for offset in range(0, len(data), 2):
        total += int.from_bytes(data[offset:offset + 2], "big")
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def build_request() -> bytes:
    frame = bytearray(PACKET_SIZE)
    frame[0:6] = b"\xff" * 6
    frame[6:12] = PEER_MAC
    put16(frame, 12, 0x88B5)
    frame[14:22] = b"CXC11REQ"
    for index in range(8, 46):
        frame[14 + index] = (index * 7 + 0x2B) & 0xFF
    return bytes(frame)


def valid_response(frame: bytes) -> bool:
    if len(frame) != PACKET_SIZE:
        return False
    if frame[0:6] != PEER_MAC or frame[6:12] != NIC_MAC:
        return False
    if get16(frame, 12) != 0x88B5 or frame[14:22] != b"CXC11RSP":
        return False
    for index in range(8, 46):
        expected = ((index * 7 + 0x2B) & 0xFF) ^ 0xA5
        if frame[14 + index] != expected:
            return False
    return True


def build_arp_request() -> bytes:
    frame = bytearray(PACKET_SIZE)
    frame[0:6] = b"\xff" * 6
    frame[6:12] = PEER_MAC
    put16(frame, 12, 0x0806)
    put16(frame, 14, 1)
    put16(frame, 16, 0x0800)
    frame[18] = 6
    frame[19] = 4
    put16(frame, 20, 1)
    frame[22:28] = PEER_MAC
    frame[28:32] = PEER_IP
    frame[32:38] = b"\x00" * 6
    frame[38:42] = NIC_IP
    return bytes(frame)


def valid_arp_reply(frame: bytes) -> bool:
    return (
        len(frame) == PACKET_SIZE
        and frame[0:6] == PEER_MAC
        and frame[6:12] == NIC_MAC
        and get16(frame, 12) == 0x0806
        and get16(frame, 14) == 1
        and get16(frame, 16) == 0x0800
        and frame[18] == 6
        and frame[19] == 4
        and get16(frame, 20) == 2
        and frame[22:28] == NIC_MAC
        and frame[28:32] == NIC_IP
        and frame[32:38] == PEER_MAC
        and frame[38:42] == PEER_IP
        and frame[42:60] == b"\x00" * 18
    )


def build_icmp_echo_request() -> bytes:
    frame = bytearray(ICMP_PACKET_SIZE)
    frame[0:6] = NIC_MAC
    frame[6:12] = PEER_MAC
    put16(frame, 12, 0x0800)
    frame[14] = 0x45
    frame[15] = 0
    put16(frame, 16, 60)
    put16(frame, 18, 0x4312)
    put16(frame, 20, 0)
    frame[22] = 53
    frame[23] = 1
    put16(frame, 24, 0)
    frame[26:30] = PEER_IP
    frame[30:34] = NIC_IP
    put16(frame, 24, internet_checksum(bytes(frame[14:34])))
    frame[34] = 8
    frame[35] = 0
    put16(frame, 36, 0)
    put16(frame, 38, 0x4D12)
    put16(frame, 40, 1)
    for index in range(32):
        frame[42 + index] = (index * 11 + 3) & 0xFF
    put16(frame, 36, internet_checksum(bytes(frame[34:74])))
    return bytes(frame)


def valid_icmp_reply(frame: bytes) -> bool:
    if (
        len(frame) != ICMP_PACKET_SIZE
        or frame[0:6] != PEER_MAC
        or frame[6:12] != NIC_MAC
        or get16(frame, 12) != 0x0800
        or frame[14] != 0x45
        or frame[15] != 0
        or get16(frame, 16) != 60
        or get16(frame, 18) != 0x4312
        or get16(frame, 20) != 0
        or frame[22] != 64
        or frame[23] != 1
        or frame[26:30] != NIC_IP
        or frame[30:34] != PEER_IP
        or internet_checksum(frame[14:34]) != 0
        or frame[34] != 0
        or frame[35] != 0
        or get16(frame, 38) != 0x4D12
        or get16(frame, 40) != 1
        or internet_checksum(frame[34:74]) != 0
    ):
        return False
    return frame[42:74] == bytes((index * 11 + 3) & 0xFF for index in range(32))


def udp_checksum(ipv4: bytes, udp: bytes) -> int:
    pseudo_header = (
        ipv4[12:20]
        + b"\x00"
        + bytes((17,))
        + len(udp).to_bytes(2, "big")
    )
    return internet_checksum(pseudo_header + udp)


def udp_payload(datagram: int) -> bytes:
    size = UDP_PAYLOAD_SIZES[datagram % len(UDP_PAYLOAD_SIZES)]
    return bytes(
        (0x40 + datagram * 17 + index * 13) & 0xFF
        for index in range(size)
    )


def build_udp_echo_request(datagram: int) -> bytes:
    payload = udp_payload(datagram)
    udp_length = 8 + len(payload)
    ip_total = 20 + udp_length
    frame = bytearray(max(PACKET_SIZE, 14 + ip_total))
    frame[0:6] = NIC_MAC
    frame[6:12] = PEER_MAC
    put16(frame, 12, 0x0800)
    frame[14] = 0x45
    frame[15] = 0
    put16(frame, 16, ip_total)
    put16(frame, 18, 0x5500 + datagram)
    put16(frame, 20, 0)
    frame[22] = 53
    frame[23] = 17
    put16(frame, 24, 0)
    frame[26:30] = PEER_IP
    frame[30:34] = NIC_IP
    put16(frame, 24, internet_checksum(bytes(frame[14:34])))
    put16(frame, 34, PEER_UDP_PORT)
    put16(frame, 36, NIC_UDP_PORT)
    put16(frame, 38, udp_length)
    put16(frame, 40, 0)
    frame[42:42 + len(payload)] = payload
    checksum = udp_checksum(bytes(frame[14:34]), bytes(frame[34:34 + udp_length]))
    put16(frame, 40, checksum or 0xFFFF)
    return bytes(frame)


def valid_udp_reply(frame: bytes, datagram: int) -> bool:
    payload = udp_payload(datagram)
    udp_length = 8 + len(payload)
    ip_total = 20 + udp_length
    ip_frame_length = 14 + ip_total
    if (
        len(frame) != max(PACKET_SIZE, ip_frame_length)
        or frame[0:6] != PEER_MAC
        or frame[6:12] != NIC_MAC
        or get16(frame, 12) != 0x0800
        or frame[14] != 0x45
        or frame[15] != 0
        or get16(frame, 16) != ip_total
        or get16(frame, 18) != 0x5500 + datagram
        or get16(frame, 20) != 0
        or frame[22] != 64
        or frame[23] != 17
        or frame[26:30] != NIC_IP
        or frame[30:34] != PEER_IP
        or internet_checksum(frame[14:34]) != 0
        or get16(frame, 34) != NIC_UDP_PORT
        or get16(frame, 36) != PEER_UDP_PORT
        or get16(frame, 38) != udp_length
        or get16(frame, 40) == 0
        or udp_checksum(frame[14:34], frame[34:34 + udp_length]) != 0
        or frame[42:42 + len(payload)] != payload
    ):
        return False
    return frame[ip_frame_length:] == b"\x00" * (len(frame) - ip_frame_length)


UDP_FAULT_KINDS = (
    "checksum",
    "ipv4-checksum",
    "udp-length",
    "padding",
    "port",
    "fragment",
    "truncated",
)


def reseal_udp_checksum(frame: bytearray) -> None:
    udp_length = get16(frame, 38)
    put16(frame, 40, 0)
    checksum = udp_checksum(
        bytes(frame[14:34]), bytes(frame[34:34 + udp_length])
    )
    put16(frame, 40, checksum or 0xFFFF)


def apply_udp_fault(frame: bytes, kind: str) -> bytes:
    """Return a request with one isolated, deterministic validation fault."""
    mutable = bytearray(frame)
    if kind == "checksum":
        mutable[40] ^= 0x01
    elif kind == "ipv4-checksum":
        mutable[24] ^= 0x01
    elif kind == "udp-length":
        udp_length = get16(mutable, 38)
        if udp_length <= 8:
            raise ValueError("UDP length fault requires a nonempty payload")
        put16(mutable, 38, udp_length - 1)
        reseal_udp_checksum(mutable)
    elif kind == "padding":
        ip_end = 14 + get16(mutable, 16)
        if len(mutable) <= ip_end:
            raise ValueError("padding fault requires a short Ethernet frame")
        mutable[-1] ^= 0x01
    elif kind == "port":
        put16(mutable, 36, NIC_UDP_PORT + 1)
        reseal_udp_checksum(mutable)
    elif kind == "fragment":
        put16(mutable, 20, 0x2000)
        put16(mutable, 24, 0)
        put16(mutable, 24, internet_checksum(bytes(mutable[14:34])))
    elif kind == "truncated":
        if len(mutable) <= 42:
            raise ValueError("truncation fault requires a UDP frame")
        return bytes(mutable[:-1])
    else:
        raise ValueError(f"unknown UDP fault kind: {kind}")
    return bytes(mutable)


def unused_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class DatagramPeer:
    """Raw Ethernet over QEMU dgram, with deterministic protocol handshakes."""

    def __init__(self, work_dir: Path, synchronized: bool,
                 protocol_enabled: bool = True,
                 corrupt_icmp_checksum: bool = False,
                 udp_enabled: bool = True,
                 corrupt_udp_checksum: bool = False,
                 udp_fault: tuple[str, int] | None = None,
                 power_cut_after_udp: int | None = None,
                 power_cut_marker: str | None = None):
        self.work_dir = work_dir
        self.synchronized = synchronized
        self.protocol_enabled = protocol_enabled
        self.corrupt_icmp_checksum = corrupt_icmp_checksum
        self.udp_enabled = udp_enabled
        self.corrupt_udp_checksum = corrupt_udp_checksum
        self.udp_fault = udp_fault
        self.power_cut_after_udp = power_cut_after_udp
        self.power_cut_marker = power_cut_marker
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.settimeout(0.05)
        self.port = int(self.socket.getsockname()[1])
        self.qemu_port = unused_udp_port()
        self.ready = threading.Event()
        self.protocol_ready = threading.Event()
        self.udp_ready = threading.Event()
        self.udp_stress_ready = threading.Event()
        self.stop = threading.Event()
        self.ok = False
        self.error = ""
        self.validation_scope = "none"
        self.requests_sent = 0
        self.udp_requests_sent = 0
        self.udp_replies_validated = 0
        self.udp_stress_replies_validated = 0
        self.lines: list[str] = []
        self.thread = threading.Thread(target=self._run, daemon=True)

    def qemu_arguments(self) -> list[str]:
        netdev = (
            "dgram,id=cxcnet,local.type=inet,local.host=127.0.0.1,"
            f"local.port={self.qemu_port},remote.type=inet,"
            f"remote.host=127.0.0.1,remote.port={self.port}"
        )
        return [
            "-netdev", netdev,
            "-device", f"e1000e,netdev=cxcnet,mac={NIC_MAC.hex(':')}",
        ]

    def start(self, deadline: float) -> None:
        self.thread = threading.Thread(
            target=self._run, args=(deadline,), daemon=True
        )
        self.thread.start()

    def notify_ready(self) -> None:
        self.ready.set()

    def notify_protocol_ready(self) -> None:
        self.protocol_ready.set()

    def notify_udp_ready(self) -> None:
        self.udp_ready.set()

    def notify_udp_stress_ready(self) -> None:
        self.udp_stress_ready.set()

    def _wait_marker(self, event: threading.Event, deadline: float,
                     description: str) -> bool:
        while not event.wait(0.05):
            if self.stop.is_set() or time.monotonic() >= deadline:
                self.error = f"{description} not observed"
                return False
        return True

    def _hold_at_power_cut_marker(self, marker: str) -> bool:
        if self.power_cut_marker != marker:
            return False
        self.lines.append(f"power_cut_hold={marker}")
        while not self.stop.wait(0.05):
            pass
        return True

    def _send(self, frame: bytes, label: str) -> None:
        self.socket.sendto(frame, ("127.0.0.1", self.qemu_port))
        self.requests_sent += 1
        self.lines.append(f"{label}_bytes={len(frame)}")
        self.lines.append(f"{label}_hex={frame.hex()}")

    def _expect(self, deadline: float, label: str,
                validator: Callable[[bytes], bool],
                retry_frame: bytes | None = None,
                retry_label: str = "request") -> bytes | None:
        next_retry = time.monotonic() + 0.25
        while not self.stop.is_set() and time.monotonic() < deadline:
            try:
                frame, _address = self.socket.recvfrom(65535)
            except socket.timeout:
                frame = None
            except OSError as error:
                if self.stop.is_set():
                    return None
                raise error
            if frame is not None:
                self.lines.append(f"{label}_bytes={len(frame)}")
                self.lines.append(f"{label}_hex={frame.hex()}")
                if validator(frame):
                    return frame
                # Ignore unrelated link noise (e.g. IPv6 ND from some QEMU/OVMF
                # combinations) and keep waiting for the contract frame.
                self.lines.append(f"{label}_ignored=1")
            if retry_frame is not None and time.monotonic() >= next_retry:
                self._send(retry_frame, retry_label)
                next_retry = time.monotonic() + 0.25
        if not self.stop.is_set() and not self.error:
            self.error = f"valid {label} not received before deadline"
        return None

    def _run(self, deadline: float) -> None:
        try:
            if self.synchronized:
                if not self._wait_marker(
                    self.ready, deadline, "kernel packet-ready marker"
                ):
                    return
                if self._hold_at_power_cut_marker("packet-ready"):
                    return
                self._send(build_request(), "request")
                response = self._expect(deadline, "candidate_response", valid_response)
            else:
                # Non-check runs have no serial collector. Retry only the raw
                # request; checked runs always use deterministic one-shot sends.
                request = build_request()
                self._send(request, "request")
                response = self._expect(
                    deadline, "candidate_response", valid_response,
                    retry_frame=request,
                )
            if response is None:
                return
            self.lines.append("validated_response=1")

            if not self.protocol_enabled:
                self.validation_scope = "m11-raw-only"
                self.ok = True
                return
            if self.synchronized:
                if not self._wait_marker(
                    self.protocol_ready, deadline, "kernel protocol-ready marker"
                ):
                    return
                if self._hold_at_power_cut_marker("protocol-ready"):
                    return
            else:
                time.sleep(0.05)

            self._send(build_arp_request(), "arp_request")
            if self._expect(deadline, "arp_reply", valid_arp_reply) is None:
                return
            self.lines.append("arp_reply_valid=1")

            icmp_request = bytearray(build_icmp_echo_request())
            if self.corrupt_icmp_checksum:
                icmp_request[36] ^= 0x01
                self.lines.append("intentionally_bad_icmp_checksum=1")
            self._send(bytes(icmp_request), "icmp_request")
            if self._expect(deadline, "icmp_reply", valid_icmp_reply) is None:
                return
            self.lines.append("icmp_reply_valid=1")
            self.validation_scope = "m12-raw-arp-icmp"
            if not self.udp_enabled:
                self.ok = True
                return

            if self.synchronized:
                if not self._wait_marker(
                    self.udp_ready, deadline, "kernel udp-ready marker"
                ):
                    return
                if self._hold_at_power_cut_marker("udp-ready"):
                    return
            else:
                time.sleep(0.05)

            for datagram in range(len(UDP_PAYLOAD_SIZES)):
                udp_request = build_udp_echo_request(datagram)
                fault_kind = None
                if self.corrupt_udp_checksum and datagram == 0:
                    fault_kind = "checksum"
                if self.udp_fault is not None:
                    kind, fault_index = self.udp_fault
                    if datagram == fault_index:
                        fault_kind = kind
                if fault_kind is not None:
                    udp_request = apply_udp_fault(udp_request, fault_kind)
                    self.lines.append(f"intentional_udp_fault={fault_kind}")
                    self.lines.append(f"udp_fault_index={datagram}")
                    if fault_kind == "checksum":
                        self.lines.append("intentionally_bad_udp_checksum=1")
                self._send(udp_request, f"udp_request_{datagram:02d}")
                self.udp_requests_sent += 1
                reply = self._expect(
                    deadline,
                    f"udp_reply_{datagram:02d}",
                    lambda frame, index=datagram: valid_udp_reply(frame, index),
                )
                if reply is None:
                    return
                self.udp_replies_validated += 1
                self.lines.append(f"udp_reply_{datagram:02d}_valid=1")
                if self.power_cut_after_udp == datagram + 1:
                    self.lines.append(
                        f"udp_power_cut_hold={self.power_cut_after_udp}"
                    )
                    while not self.stop.wait(0.05):
                        pass
                    return
            self.lines.append(f"udp_payload_bytes={UDP_PAYLOAD_TOTAL}")
            self.validation_scope = "m13-raw-arp-icmp-udp-ring"

            # Negative controls that stop at M13 must not require stress.
            if (
                self.udp_fault is not None
                or self.corrupt_udp_checksum
                or self.power_cut_after_udp is not None
                or self.power_cut_marker is not None
            ):
                self.ok = True
                return

            if self.synchronized:
                if not self._wait_marker(
                    self.udp_stress_ready,
                    deadline,
                    "kernel udp-stress-ready marker",
                ):
                    return
            else:
                time.sleep(0.05)

            stress_base = len(UDP_PAYLOAD_SIZES)
            for offset in range(UDP_STRESS_DATAGRAMS):
                datagram = stress_base + offset
                udp_request = build_udp_echo_request(datagram)
                self._send(udp_request, f"udp_stress_request_{offset:03d}")
                self.udp_requests_sent += 1
                reply = self._expect(
                    deadline,
                    f"udp_stress_reply_{offset:03d}",
                    lambda frame, index=datagram: valid_udp_reply(frame, index),
                )
                if reply is None:
                    return
                self.udp_stress_replies_validated += 1
            self.lines.append(
                f"udp_stress_replies_validated={self.udp_stress_replies_validated}"
            )
            self.lines.append(
                f"udp_stress_payload_bytes={UDP_STRESS_PAYLOAD_TOTAL}"
            )
            self.validation_scope = "m14-raw-arp-icmp-udp-ring-stress"
            self.ok = True
        except Exception as error:  # Preserve harness failure in the artifact.
            self.error = f"datagram peer error: {error}"

    def finish(self, allow_response: bool) -> None:
        if allow_response and not self.ok:
            self.thread.join(timeout=2.0)
        self.stop.set()
        self.thread.join(timeout=2.0)
        self.socket.close()
        if not self.ok and not self.error:
            self.error = "required network evidence not observed"
        self.lines.append(f"requests_sent={self.requests_sent}")
        self.lines.append(f"udp_requests_sent={self.udp_requests_sent}")
        self.lines.append(f"udp_replies_validated={self.udp_replies_validated}")
        self.lines.append(
            f"udp_stress_replies_validated={self.udp_stress_replies_validated}"
        )
        self.lines.append(f"validation_scope={self.validation_scope}")
        self.lines.append(f"peer_ok={int(self.ok)}")
        if self.error:
            self.lines.append(f"error={self.error}")
        (self.work_dir / "network-peer.log").write_text(
            "\n".join(self.lines) + "\n", encoding="utf-8"
        )



