#!/usr/bin/env python3
"""Executable CXC/OS test contract: host models, QEMU faults, power cuts and stress."""

from __future__ import annotations

import argparse
import hashlib
import json
import random
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path

import run_qemu as rq

PROJECT_ROOT = Path(__file__).resolve().parent.parent
E1000E_PANIC = "CXC_OS_PANIC: e1000e MSI-X"
UDP_SUCCESS = (
    "UDP Echo request/reply: 10/10, payload bytes: 148, "
    "frame RX/TX bytes: 642/642"
)
RING_SUCCESS = (
    "UDP ring heads RX/TX: 5/5, tails RX/TX: 1/5, "
    "wraps RXh/TXh/RXt/TXt: 1/1/2/1, recycled RX buffers: 10"
)
# M24 RX demux (M13 path only) and M25 post-stress socket→TX submit.
SOCKET_DEMUX_SUCCESS = "UDP socket demux deliver/recv: 10/10"
SOCKET_TX_SUCCESS = (
    "UDP socket TX submit: 1/1, bytes: 60, heads/tails: 6/6"
)
STRESS_SUCCESS = (
    "UDP stress request/reply: 256/256, payload bytes: 3736, "
    "frame RX/TX bytes: 16410/16410"
)
STRESS_SOCKET_DEMUX_SUCCESS = (
    "UDP stress socket demux deliver/recv: 256/256"
)
SOCKET_TX_BP_SUCCESS = (
    "UDP socket TX backpressure: posted: 7, rejects: 1, drained: 1, "
    "heads/tails: 5/5"
)
SOCKET_TX_WAIT_SUCCESS = (
    "UDP socket TX wait: waits: 1, posted: 1, drained: 1, "
    "events: 1, heads/tails: 5/5"
)
IRQ_RX_DEMUX_SUCCESS = "UDP IRQ RX demux deliver/echo/recycle: 266/266/266"
IRQ_RX_SUBMIT_SUCCESS = "UDP IRQ RX submit: 266"
USER_AS_PROBE_SUCCESS = (
    "user AS probe: cr3_switch: 1, write: 1, read: 1, user_bit: 1, "
    "va: 0x0000000000400000"
)
RING3_PROBE_SUCCESS = "ring3 probe: enter: 1, int80: 1, cpl: 3, rsp0: 1"
USER_ELF_PROBE_SUCCESS = (
    "user ELF: loaded: 1, enter: 1, exit: 1, pass: 1"
)
USER_PROGRAM_PASS = "USER_PROGRAM_PASS"
USER_FS_PASS = "USER_FS_PASS"
USER_FS_PROBE_SUCCESS = (
    "user FS: open: 2, read: 1, write: 1, close: 2, pass: 1"
)
USER_BRK_PASS = "USER_BRK_PASS"
USER_BRK_PROBE_SUCCESS = "user brk: grow: 1, pages: 1, pass: 1"
USER_MMAP_PASS = "USER_MMAP_PASS"
USER_MMAP_PROBE_SUCCESS = "user mmap: calls: 1, pages: 1, pass: 1"
USER_DIR_PASS = "USER_DIR_PASS"
USER_DIR_PROBE_SUCCESS = "user dir: mkdir: 1, open: 2, dirs: 1, pass: 1"
USER_MUNMAP_PASS = "USER_MUNMAP_PASS"
USER_MUNMAP_PROBE_SUCCESS = "user munmap: mmap: 1, munmap: 1, pass: 1"
USER_EXT_ELF_PASS = "USER_EXT_ELF_PASS"
USER_EXT_ELF_PROBE_SUCCESS = "user ext ELF: loaded: 1, enter: 1, exit: 1, pass: 1"
USER_CROSS_ELF_PASS = "USER_CROSS_ELF_PASS"
USER_CROSS_ELF_PROBE_SUCCESS = (
    "user cross ELF: loaded: 1, enter: 1, exit: 1, pass: 1"
)
USER_LSEEK_PASS = "USER_LSEEK_PASS"
USER_LSEEK_PROBE_SUCCESS = "user lseek: calls: 3, pass: 1"


@dataclass
class HostCheck:
    category: str
    name: str
    passed: bool
    detail: str


@dataclass
class CaseResult:
    name: str
    tags: list[str]
    passed: bool
    seconds: float
    command: list[str]
    stdout_log: str
    work_dir: str
    detail: str


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def raw_response(request: bytes) -> bytes:
    frame = bytearray(request)
    frame[0:6] = request[6:12]
    frame[6:12] = rq.NIC_MAC
    frame[14:22] = b"CXC11RSP"
    for index in range(8, 46):
        frame[14 + index] ^= 0xA5
    return bytes(frame)


def arp_response(request: bytes) -> bytes:
    frame = bytearray(request)
    frame[0:6] = rq.PEER_MAC
    frame[6:12] = rq.NIC_MAC
    rq.put16(frame, 20, 2)
    frame[22:28] = rq.NIC_MAC
    frame[28:32] = rq.NIC_IP
    frame[32:38] = rq.PEER_MAC
    frame[38:42] = rq.PEER_IP
    return bytes(frame)


def icmp_response(request: bytes) -> bytes:
    frame = bytearray(request)
    frame[0:6] = rq.PEER_MAC
    frame[6:12] = rq.NIC_MAC
    frame[22] = 64
    frame[26:30] = rq.NIC_IP
    frame[30:34] = rq.PEER_IP
    rq.put16(frame, 24, 0)
    rq.put16(frame, 24, rq.internet_checksum(bytes(frame[14:34])))
    frame[34] = 0
    rq.put16(frame, 36, 0)
    rq.put16(frame, 36, rq.internet_checksum(bytes(frame[34:74])))
    return bytes(frame)


def udp_response(request: bytes) -> bytes:
    frame = bytearray(request)
    frame[0:6] = rq.PEER_MAC
    frame[6:12] = rq.NIC_MAC
    frame[22] = 64
    frame[26:30] = rq.NIC_IP
    frame[30:34] = rq.PEER_IP
    rq.put16(frame, 24, 0)
    rq.put16(frame, 24, rq.internet_checksum(bytes(frame[14:34])))
    rq.put16(frame, 34, rq.NIC_UDP_PORT)
    rq.put16(frame, 36, rq.PEER_UDP_PORT)
    rq.reseal_udp_checksum(frame)
    return bytes(frame)


def golden_frames() -> list[tuple[str, bytes, object]]:
    frames: list[tuple[str, bytes, object]] = [
        ("raw", raw_response(rq.build_request()), rq.valid_response),
        ("arp", arp_response(rq.build_arp_request()), rq.valid_arp_reply),
        ("icmp", icmp_response(rq.build_icmp_echo_request()), rq.valid_icmp_reply),
    ]
    for index in range(len(rq.UDP_PAYLOAD_SIZES)):
        request = rq.build_udp_echo_request(index)
        frames.append(
            (
                f"udp-{index}",
                udp_response(request),
                lambda frame, i=index: rq.valid_udp_reply(frame, i),
            )
        )
    return frames


class RingModel:
    """Independent index/page model; this is not a kernel transaction API."""

    def __init__(self) -> None:
        self.slots: list[int | None] = list(range(7)) + [None]
        self.done = {0, 1, 2}
        self.head = 3
        self.tail = 7
        self.tx_head = 3
        self.tx_tail = 3
        self.requests = 0
        self.replies = 0
        self.recycled = 0
        self.rx_irq = 0
        self.tx_irq = 0
        self.rx_head_wraps = 0
        self.tx_head_wraps = 0
        self.rx_tail_wraps = 0
        self.tx_tail_wraps = 0

    def free_slots(self) -> int:
        return (self.tail - self.head) % 8

    def check_pages(self, expected_free: int = 4) -> None:
        pages = [page for page in self.slots if page is not None]
        assert len(pages) == 7 and len(set(pages)) == 7
        assert set(pages) == set(range(7))
        assert self.free_slots() == expected_free

    def observe_only(self, index: int) -> None:
        """RX observed without reply/recycle — models burst fill / back-pressure."""
        current = (index + 3) % 8
        assert self.slots[current] is not None
        assert current not in self.done
        self.done.add(current)
        self.head = (current + 1) % 8
        self.rx_irq += 1
        if self.head == 0:
            self.rx_head_wraps += 1

    def receive(self, index: int, valid: bool = True) -> None:
        current = (index + 3) % 8
        assert self.slots[current] is not None
        self.done.add(current)
        self.head = (current + 1) % 8
        self.rx_irq += 1
        if self.head == 0:
            self.rx_head_wraps += 1
        if not valid:
            self.check_pages(expected_free=3)
            return

        self.requests += 1
        self.tx_head = self.head
        self.tx_tail = self.head
        self.replies += 1
        self.tx_irq += 1
        if self.tx_head == 0:
            self.tx_head_wraps += 1
            self.tx_tail_wraps += 1

        source = index % 8
        target = self.tail
        assert source in self.done
        assert self.slots[source] is not None
        assert self.slots[target] is None and target not in self.done
        page = self.slots[source]
        self.slots[source] = None
        self.done.remove(source)
        self.slots[target] = page
        self.tail = (target + 1) % 8
        self.recycled += 1
        if self.tail == 0:
            self.rx_tail_wraps += 1
        self.check_pages()


def run_host_checks(seed: int) -> list[HostCheck]:
    checks: list[HostCheck] = []

    def record(category: str, name: str, function, detail: str) -> None:
        try:
            result_detail = function()
            checks.append(HostCheck(category, name, True, result_detail or detail))
        except Exception as error:
            checks.append(HostCheck(category, name, False, f"{detail}: {error}"))

    frames = golden_frames()

    def correctness() -> str:
        for _name, frame, validator in frames:
            assert validator(frame)
        udp_sizes = [len(rq.build_udp_echo_request(i)) for i in range(10)]
        assert udp_sizes == [60, 60, 60, 60, 60, 60, 60, 73, 74, 75]
        assert sum(udp_sizes) == 642
        assert sum(len(frame) for _name, frame, _validator in frames) == 836
        return "13 golden responses; UDP payload=148, frame bytes=642, all bytes=836"

    record("正确", "golden frames and checksums", correctness, "golden validation")

    def exhaustive_bits() -> str:
        mutations = 0
        for name, frame, validator in frames:
            for offset in range(len(frame)):
                for bit in range(8):
                    changed = bytearray(frame)
                    changed[offset] ^= 1 << bit
                    assert not validator(bytes(changed)), (name, offset, bit)
                    mutations += 1
        return f"{mutations} single-bit corruptions rejected"

    record("失败路径", "exhaustive single-bit mutation", exhaustive_bits, "bit mutation")

    def truncation_extension() -> str:
        mutations = 0
        for name, frame, validator in frames:
            for length in range(len(frame)):
                assert not validator(frame[:length]), (name, length)
                mutations += 1
            for suffix in (b"\x00", b"\xff", b"\x00" * 16):
                assert not validator(frame + suffix), name
                mutations += 1
        return f"{mutations} truncation/extension variants rejected"

    record("失败路径", "truncation and extension", truncation_extension, "length mutation")

    def isolated_faults() -> str:
        count = 0
        for kind in rq.UDP_FAULT_KINDS:
            for index in range(10):
                if kind == "padding" and index > 6:
                    continue
                if kind == "truncated" and index < 7:
                    continue
                original = rq.build_udp_echo_request(index)
                faulty = rq.apply_udp_fault(original, kind)
                assert faulty != original
                if kind == "truncated":
                    assert len(faulty) + 1 == len(original)
                else:
                    assert len(faulty) == len(original)
                if kind in ("port", "fragment", "udp-length", "padding"):
                    udp_length = rq.get16(faulty, 38)
                    assert rq.udp_checksum(
                        faulty[14:34], faulty[34:34 + udp_length]
                    ) == 0
                count += 1
        return f"{count} isolated UDP fault fixtures verified"

    record("复合", "isolated fault fixture matrix", isolated_faults, "fault fixtures")

    def ring_loop() -> str:
        model = RingModel()
        for _outer in range(100):
            for inner in range(100):
                model.receive(_outer * 100 + inner)
        assert model.requests == model.replies == model.recycled == 10000
        assert model.rx_irq == model.tx_irq == 10000
        assert model.head == model.tx_head == 3
        assert model.tail == 7 and model.tx_tail == 3
        assert model.rx_head_wraps == model.tx_head_wraps == 1250
        assert model.rx_tail_wraps == model.tx_tail_wraps == 1250
        return "100x100 nested iterations; 10000 commits; 1250 wraps per ring"

    record("嵌套/循环/高压", "ring model nested loop", ring_loop, "ring model loop")

    def ring_long_stability() -> str:
        model = RingModel()
        for index in range(256):
            model.receive(index)
        assert model.requests == model.replies == model.recycled == 256
        assert model.rx_irq == model.tx_irq == 256
        assert model.free_slots() == 4
        assert model.head == model.tx_head == (3 + 256) % 8
        assert model.tail == (7 + 256) % 8
        assert model.rx_head_wraps == model.tx_head_wraps == 32
        assert model.rx_tail_wraps == model.tx_tail_wraps == 32
        return "256 sequential commits; 32 wraps per ring; free slots remain 4"

    record("高压", "ring model 256-frame stability", ring_long_stability, "ring stability")

    def ring_burst_backpressure() -> str:
        model = RingModel()
        assert model.free_slots() == 4
        for index in range(4):
            model.observe_only(index)
            assert model.free_slots() == 3 - index
        try:
            model.observe_only(4)
            raise AssertionError("fifth observe_only should fail: ring full")
        except AssertionError as error:
            if "fifth observe_only" in str(error):
                raise
        assert model.rx_irq == 4 and model.requests == 0 and model.recycled == 0
        # Drain via normal commits starting at index 0 would be wrong state;
        # prove a fresh model still accepts a full M13 after burst rejection.
        healthy = RingModel()
        for index in range(10):
            healthy.receive(index)
        assert healthy.recycled == 10 and healthy.free_slots() == 4
        return "burst fill 4/4 without recycle; overfill rejected; M13 path intact"

    record("高压", "ring burst fill back-pressure", ring_burst_backpressure, "ring burst")

    def transaction_failures() -> str:
        for fault_index in range(10):
            model = RingModel()
            for index in range(10):
                model.receive(index, valid=index != fault_index)
                if index == fault_index:
                    break
            assert model.requests == model.replies == model.recycled == fault_index
            assert model.tx_irq == fault_index and model.rx_irq == fault_index + 1
            assert model.head == (fault_index + 4) % 8
            assert model.tx_head == (fault_index + 3) % 8
            assert model.tail == (fault_index + 7) % 8
        return "all 10 failure points: RX observed, no reply/recycle commit"

    record("事务", "failure before commit", transaction_failures, "transaction failure")

    def deterministic_chaos() -> str:
        rng = random.Random(seed)
        for iteration in range(10000):
            length = rng.randrange(0, 97)
            blob = bytes(rng.getrandbits(8) for _ in range(length))
            assert not rq.valid_response(blob)
            assert not rq.valid_arp_reply(blob)
            assert not rq.valid_icmp_reply(blob)
            assert not rq.valid_udp_reply(blob, iteration % 10)
        return f"seed={seed}; 10000 random blobs x 4 validators rejected without exceptions"

    record("混沌", "deterministic malformed-frame chaos", deterministic_chaos, "random frames")
    return checks


class ContractRunner:
    def __init__(self, image: Path, runner: Path, work_dir: Path, seed: int) -> None:
        self.image = image
        self.runner = runner
        self.work_dir = work_dir
        self.seed = seed
        self.results: list[CaseResult] = []
        self.image_hash = file_sha256(image)

    def success_evidence(self, work: Path) -> str:
        serial = (work / "serial.log").read_bytes()
        peer = (work / "network-peer.log").read_text()
        assert b"CXC_OS_SELF_TEST_PASS" in serial
        assert UDP_SUCCESS.encode() in serial and RING_SUCCESS.encode() in serial
        assert SOCKET_DEMUX_SUCCESS.encode() in serial
        assert STRESS_SUCCESS.encode() in serial
        assert STRESS_SOCKET_DEMUX_SUCCESS.encode() in serial
        assert SOCKET_TX_SUCCESS.encode() in serial
        assert SOCKET_TX_BP_SUCCESS.encode() in serial
        assert SOCKET_TX_WAIT_SUCCESS.encode() in serial
        assert IRQ_RX_DEMUX_SUCCESS.encode() in serial
        assert IRQ_RX_SUBMIT_SUCCESS.encode() in serial
        assert USER_AS_PROBE_SUCCESS.encode() in serial
        assert RING3_PROBE_SUCCESS.encode() in serial
        assert USER_PROGRAM_PASS.encode() in serial
        assert USER_ELF_PROBE_SUCCESS.encode() in serial
        assert USER_FS_PASS.encode() in serial
        assert USER_FS_PROBE_SUCCESS.encode() in serial
        assert USER_BRK_PASS.encode() in serial
        assert USER_BRK_PROBE_SUCCESS.encode() in serial
        assert USER_MMAP_PASS.encode() in serial
        assert USER_MMAP_PROBE_SUCCESS.encode() in serial
        assert USER_DIR_PASS.encode() in serial
        assert USER_DIR_PROBE_SUCCESS.encode() in serial
        assert USER_MUNMAP_PASS.encode() in serial
        assert USER_MUNMAP_PROBE_SUCCESS.encode() in serial
        assert USER_EXT_ELF_PASS.encode() in serial
        assert USER_EXT_ELF_PROBE_SUCCESS.encode() in serial
        assert USER_CROSS_ELF_PASS.encode() in serial
        assert USER_CROSS_ELF_PROBE_SUCCESS.encode() in serial
        assert USER_LSEEK_PASS.encode() in serial
        assert USER_LSEEK_PROBE_SUCCESS.encode() in serial
        assert "requests_sent=269\n" in peer
        assert "udp_replies_validated=10\n" in peer
        assert "udp_stress_replies_validated=256\n" in peer
        assert (
            "validation_scope=m14-raw-arp-icmp-udp-ring-stress\n" in peer
        )
        assert "peer_ok=1\n" in peer
        return (
            "kernel PASS + net contracts + M36–M47 lseek"
        )

    def negative_evidence(self, work: Path) -> str:
        serial = (work / "serial.log").read_bytes()
        assert b"CXC_OS_PANIC:" in serial
        assert b"CXC_OS_SELF_TEST_PASS" not in serial
        return "expected panic and no PASS"

    def run_case(
        self,
        name: str,
        tags: list[str],
        extra: list[str],
        mode: str = "success",
        fault: tuple[str, int] | None = None,
        power: tuple[str, int | str] | None = None,
        transaction9: bool = False,
    ) -> None:
        work = self.work_dir / "qemu" / name
        work.mkdir(parents=True, exist_ok=True)
        stdout_log = self.work_dir / "logs" / f"{name}.stdout.log"
        command = [
            sys.executable,
            str(self.runner),
            "--image",
            str(self.image),
            "--check",
            "--timeout",
            "120",
            "--work-dir",
            str(work),
            *extra,
        ]
        started = time.monotonic()
        passed = False
        detail = ""
        try:
            completed = subprocess.run(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=150,
                check=False,
            )
            output = completed.stdout
            stdout_log.write_bytes(output)
            assert completed.returncode == 0, f"runner exit={completed.returncode}"
            if mode == "success":
                detail = self.success_evidence(work)
            elif mode == "negative":
                detail = self.negative_evidence(work)
            elif mode == "power":
                serial = (work / "serial.log").read_bytes()
                assert b"QEMU hard power-cut contract matched" in output
                assert b"CXC_OS_SELF_TEST_PASS" not in serial
                assert file_sha256(self.image) == self.image_hash
                detail = "SIGKILL observed; no PASS; image SHA-256 unchanged"
            else:
                raise AssertionError(f"unknown mode {mode}")

            if fault is not None:
                kind, index = fault
                peer = (work / "network-peer.log").read_text()
                serial = (work / "serial.log").read_bytes()
                udp_debug = next(
                    line for line in serial.splitlines() if b"e1000e UDP stage:" in line
                )
                assert b"failure: 3" in udp_debug
                assert f"datagram: {index},".encode() in udp_debug
                assert f"req/reply: {index}/{index},".encode() in udp_debug
                assert f"intentional_udp_fault={kind}\n" in peer
                assert f"udp_fault_index={index}\n" in peer
                assert f"udp_replies_validated={index}\n" in peer
                detail += f"; {kind} at datagram {index}, validation failure=3, prior replies={index}"
            if power is not None:
                kind, value = power
                if kind == "marker":
                    serial = (work / "serial.log").read_bytes()
                    if value == "packet-ready":
                        assert b"protocol-ready" not in serial and b"udp-ready" not in serial
                    elif value == "protocol-ready":
                        assert b"udp-ready" not in serial
                    peer = (work / "network-peer.log").read_text()
                    assert f"power_cut_hold={value}\n" in peer
                else:
                    peer = (work / "network-peer.log").read_text()
                    assert f"udp_power_cut_hold={value}\n" in peer
                    assert f"udp_replies_validated={value}\n" in peer
                detail += f"; cut={value}"
            if transaction9:
                serial = (work / "serial.log").read_bytes()
                assert b"req/reply: 9/9" in serial
                assert b"UDP heads/tails: 5/4, 0/4, IRQs: 10/9" in serial
                assert b"payload/frame bytes: 115, 567/567" in serial
                assert b"wraps RXh/TXh/RXt/TXt: 1/1/2/1, recycled: 9" in serial
                detail += "; failure did not commit tenth reply/recycle"
            passed = True
        except Exception as error:
            detail = f"{detail}; {error}" if detail else str(error)
            if not stdout_log.exists():
                stdout_log.write_text(f"contract runner exception: {error}\n")
        seconds = time.monotonic() - started
        for variable_copy in work.glob("OVMF_VARS.fd"):
            variable_copy.unlink()
        result = CaseResult(
            name,
            tags,
            passed,
            seconds,
            command,
            str(stdout_log.relative_to(self.work_dir)),
            str(work.relative_to(self.work_dir)),
            detail,
        )
        self.results.append(result)
        if not passed:
            # Root-cause contract: surface serial/peer tails in CI logs.
            serial_path = work / "serial.log"
            peer_path = work / "network-peer.log"
            if serial_path.is_file():
                serial_text = serial_path.read_text(errors="replace")
                tail = "\n".join(serial_text.splitlines()[-40:])
                print(f"--- {name} serial.log tail ---\n{tail}\n--- end serial ---")
            if peer_path.is_file():
                peer_text = peer_path.read_text(errors="replace")
                peer_tail = "\n".join(peer_text.splitlines()[-20:])
                print(f"--- {name} network-peer.log tail ---\n{peer_tail}\n--- end peer ---")
        print(
            f"{'PASS' if passed else 'FAIL'} {name} ({seconds:.2f}s) "
            f"[{', '.join(tags)}] {detail}",
            flush=True,
        )

    def topology(self, cpu: str, cpus: int, memory: str) -> list[str]:
        return ["--cpu", cpu, "--cpus", str(cpus), "--memory", memory]

    def success(self, name: str, tags: list[str], cpu="qemu64,-x2apic", cpus=4, memory="128M") -> None:
        self.run_case(name, tags, self.topology(cpu, cpus, memory))

    def negative(self, name: str, flags: list[str], tags=["失败路径"], expect=E1000E_PANIC) -> None:
        self.run_case(name, tags, [*flags, "--expect", expect], mode="negative")

    def fault(self, kind: str, index: int, name: str, tags: list[str], transaction9=False) -> None:
        self.run_case(
            name,
            tags,
            [
                "--udp-fault",
                kind,
                "--udp-fault-index",
                str(index),
                "--expect",
                E1000E_PANIC,
            ],
            mode="negative",
            fault=(kind, index),
            transaction9=transaction9,
        )

    def power_cut(self, name: str, option: str, value: int | str | None = None, chaos=False) -> None:
        tags = ["混沌", "掉电"] if chaos else ["掉电"]
        extra = self.topology("qemu64,+x2apic", 4, "256M")
        extra.append(option)
        if value is not None:
            extra.append(str(value))
        power = ("after", value) if option == "--power-cut-after-udp" else ("marker", value)
        self.run_case(name, tags, extra, mode="power", power=power)

    def run_full(self, loops: int, chaos_runs: int) -> None:
        self.success("correct-xapic", ["正确"])
        self.success("composite-smp16-512", ["复合", "复杂"], "qemu64,+x2apic", 16, "512M")
        self.success("capability-up-64", ["能力矩阵"], "qemu64,+x2apic", 1, "64M")
        self.success("capability-smp8-xapic", ["能力矩阵"], "qemu64,-x2apic", 8, "256M")

        self.negative("failure-no-edu", ["--no-edu"], expect="CXC_OS_PANIC: EDU")
        self.negative("failure-no-e1000e", ["--no-e1000e"])
        self.negative("failure-no-network-peer", ["--no-network-peer"])
        self.negative("failure-no-protocol-peer", ["--no-protocol-peer"])
        self.negative("failure-bad-icmp", ["--bad-icmp-checksum"])
        self.negative("failure-no-udp", ["--no-udp-peer"])
        self.negative("failure-bad-udp", ["--bad-udp-checksum"])

        self.fault("ipv4-checksum", 4, "fault-ipv4-checksum-wrap", ["失败路径", "复合"])
        self.fault("udp-length", 6, "fault-udp-length", ["失败路径"])
        self.fault("padding", 6, "fault-padding", ["失败路径"])
        self.fault("port", 8, "fault-port-after-wrap", ["失败路径", "复合"])
        self.fault("fragment", 7, "fault-fragment", ["失败路径"])
        self.fault("truncated", 9, "fault-truncated", ["失败路径"])
        self.fault(
            "checksum",
            9,
            "transaction-ninth-commit-then-fault",
            ["事务", "嵌套", "复杂", "失败路径"],
            transaction9=True,
        )

        self.power_cut("power-packet-ready", "--power-cut", "packet-ready")
        self.power_cut("power-protocol-ready", "--power-cut", "protocol-ready")
        self.power_cut("power-udp-ready", "--power-cut", "udp-ready")
        self.power_cut("power-after-3-replies", "--power-cut-after-udp", 3)
        self.power_cut("power-after-8-replies", "--power-cut-after-udp", 8)
        self.success("power-recovery-clean-boot", ["掉电", "正确"], "qemu64,+x2apic", 4, "256M")

        for index in range(loops):
            cpu = "qemu64,-x2apic" if index % 2 == 0 else "qemu64,+x2apic"
            self.success(f"loop-boot-{index + 1:02d}", ["循环"], cpu, 4, "128M")

        self.success("stress-smp16-512-a", ["高压", "复杂"], "qemu64,+x2apic", 16, "512M")
        self.success("stress-smp16-512-b", ["高压", "复杂"], "qemu64,+x2apic", 16, "512M")

        rng = random.Random(self.seed)
        for index in range(chaos_runs):
            category = index % 3
            name = f"chaos-{index + 1:02d}"
            cpus = rng.choice((1, 2, 4, 8, 16))
            memory_choices = ("256M", "512M") if cpus >= 8 else ("64M", "128M", "256M", "512M")
            memory = rng.choice(memory_choices)
            cpu = rng.choice(("qemu64,-x2apic", "qemu64,+x2apic"))
            if category == 0:
                self.run_case(name, ["混沌"], [*self.topology(cpu, cpus, memory)])
            elif category == 1:
                kind = rng.choice(rq.UDP_FAULT_KINDS)
                if kind == "padding":
                    fault_index = rng.randrange(7)
                elif kind == "truncated":
                    fault_index = rng.randrange(7, 10)
                else:
                    fault_index = rng.randrange(10)
                self.run_case(
                    name,
                    ["混沌", "失败路径"],
                    [
                        *self.topology(cpu, cpus, memory),
                        "--udp-fault",
                        kind,
                        "--udp-fault-index",
                        str(fault_index),
                        "--expect",
                        E1000E_PANIC,
                    ],
                    mode="negative",
                    fault=(kind, fault_index),
                )
            else:
                if rng.randrange(2) == 0:
                    marker = rng.choice(("packet-ready", "protocol-ready", "udp-ready"))
                    self.run_case(
                        name,
                        ["混沌", "掉电"],
                        [*self.topology(cpu, cpus, memory), "--power-cut", marker],
                        mode="power",
                        power=("marker", marker),
                    )
                else:
                    replies = rng.randrange(1, 10)
                    self.run_case(
                        name,
                        ["混沌", "掉电"],
                        [
                            *self.topology(cpu, cpus, memory),
                            "--power-cut-after-udp",
                            str(replies),
                        ],
                        mode="power",
                        power=("after", replies),
                    )


def write_outputs(
    work_dir: Path,
    image: Path,
    seed: int,
    host_checks: list[HostCheck],
    cases: list[CaseResult],
    host_only: bool,
) -> None:
    all_passed = all(check.passed for check in host_checks) and all(
        case.passed for case in cases
    )
    report = {
        "contract": "CXC/OS midpoint test contract",
        "seed": seed,
        "host_only": host_only,
        "image": str(image),
        "image_sha256": file_sha256(image) if image.is_file() else None,
        "overall": "PASS" if all_passed else "FAIL",
        "host_checks": [asdict(check) for check in host_checks],
        "qemu_cases": [asdict(case) for case in cases],
        "power_loss_scope": (
            "SIGKILL of the QEMU process; snapshot image immutability and clean next boot. "
            "No guest persistence, filesystem recovery or transactional durability is claimed."
        ),
    }
    (work_dir / "contract-report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    lines = [
        "CXC/OS 中场测试契约结果",
        "======================",
        f"总体：{'PASS' if all_passed else 'FAIL'}",
        f"确定性种子：{seed}",
        f"模式：{'host-only' if host_only else 'host + QEMU'}",
        f"主机检查：{sum(c.passed for c in host_checks)}/{len(host_checks)} 通过",
        f"QEMU 场景：{sum(c.passed for c in cases)}/{len(cases)} 通过",
        "",
        "主机检查",
        "--------",
    ]
    for check in host_checks:
        lines.append(
            f"[{'PASS' if check.passed else 'FAIL'}] {check.category} / {check.name}: {check.detail}"
        )
    lines += ["", "QEMU 场景", "---------"]
    for case in cases:
        lines.append(
            f"[{'PASS' if case.passed else 'FAIL'}] {case.name} "
            f"({', '.join(case.tags)}, {case.seconds:.2f}s): {case.detail}"
        )
    lines += [
        "",
        "掉电与事务边界",
        "--------------",
        "掉电测试使用 SIGKILL 强杀 QEMU，属于虚拟机硬断电，不是宿主机断电。",
        "镜像使用 snapshot=on；每次掉电后核对镜像 SHA-256 不变，并执行干净启动恢复。",
        "当前没有持久化文件系统或掉电恢复状态机，因此不宣称数据持久性、fsync 原子性或掉电事务恢复。",
        "事务测试覆盖当前自检路径的软件提交顺序：RX 可已观察，失败帧不得提交 reply/recycle；不是通用内核事务 API。",
        "嵌套测试覆盖多层子系统已成功后，在 UDP/ring 内层失败并逆序清理；不宣称支持嵌套事务。",
        "混沌测试采用固定种子和可复现故障注入，不把随机测试描述为形式证明。",
        "",
        "复现",
        "----",
        "python3 tools/contract_test.py --image artifacts/cxc-os.img --work-dir contract-results --seed 20261025",
        "python3 tools/contract_test.py --host-only --work-dir build/contract-host --seed 20261025",
    ]
    (work_dir / "CONTRACT_VALIDATION.txt").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )

    files = sorted(
        path for path in work_dir.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS"
    )
    manifest = [
        f"{file_sha256(path)}  {path.relative_to(work_dir).as_posix()}"
        for path in files
    ]
    (work_dir / "SHA256SUMS").write_text("\n".join(manifest) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=PROJECT_ROOT / "artifacts/cxc-os.img")
    parser.add_argument("--runner", type=Path, default=PROJECT_ROOT / "tools/run_qemu.py")
    parser.add_argument("--work-dir", type=Path, default=PROJECT_ROOT / "contract-results")
    parser.add_argument("--seed", type=int, default=20261025)
    parser.add_argument("--loops", type=int, default=5)
    parser.add_argument("--chaos-runs", type=int, default=6)
    parser.add_argument("--host-only", action="store_true")
    arguments = parser.parse_args()
    if arguments.loops < 0 or arguments.chaos_runs < 0:
        parser.error("--loops and --chaos-runs must be nonnegative")
    image = arguments.image.resolve()
    runner = arguments.runner.resolve()
    work_dir = arguments.work_dir.resolve()
    if work_dir in (Path("/"), PROJECT_ROOT):
        parser.error("unsafe --work-dir")
    if not arguments.host_only and not image.is_file():
        parser.error(f"image does not exist: {image}")
    if not runner.is_file():
        parser.error(f"runner does not exist: {runner}")
    if work_dir.exists():
        shutil.rmtree(work_dir)
    (work_dir / "logs").mkdir(parents=True)

    host_checks = run_host_checks(arguments.seed)
    for check in host_checks:
        print(
            f"{'PASS' if check.passed else 'FAIL'} host/{check.name}: {check.detail}",
            flush=True,
        )
    cases: list[CaseResult] = []
    if not arguments.host_only:
        contract = ContractRunner(image, runner, work_dir, arguments.seed)
        contract.run_full(arguments.loops, arguments.chaos_runs)
        cases = contract.results
        if file_sha256(image) != contract.image_hash:
            cases.append(
                CaseResult(
                    "image-immutability-final",
                    ["掉电", "事务"],
                    False,
                    0.0,
                    [],
                    "",
                    "",
                    "image SHA-256 changed during contract run",
                )
            )
    write_outputs(work_dir, image, arguments.seed, host_checks, cases, arguments.host_only)
    passed = all(check.passed for check in host_checks) and all(case.passed for case in cases)
    print(
        f"CONTRACT_TEST_{'PASS' if passed else 'FAIL'} "
        f"host={sum(c.passed for c in host_checks)}/{len(host_checks)} "
        f"qemu={sum(c.passed for c in cases)}/{len(cases)} "
        f"results={work_dir}"
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
