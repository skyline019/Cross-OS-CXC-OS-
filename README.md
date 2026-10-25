# CXC/OS

[![CI](https://github.com/skyline019/Cross-OS-CXC-OS-/actions/workflows/ci.yml/badge.svg)](https://github.com/skyline019/Cross-OS-CXC-OS-/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Native UEFI x86-64 kernel** — no host OS runtime. Higher-half PE32+, SMP/IPI, PCI/MSI-X, e1000e packet DMA, and a minimal UDP socket API. Acceptance requires kernel `CXC_OS_SELF_TEST_PASS` **and** external Python peer `peer_ok=1`.

> Chinese deep-dive: [`docs/README.zh-CN.md`](docs/README.zh-CN.md) · Cadence: [`docs/midfield-cadence.md`](docs/midfield-cadence.md) · Midfield: [`docs/midpoint-assessment-6.md`](docs/midpoint-assessment-6.md)

## Status

| Item | Value |
|---|---|
| Baseline | Milestone 65 / v0.66.0 |
| Boot | Native UEFI → higher-half PE32+ (MinGW-w64) |
| Evidence | QEMU + OVMF, EDU + e1000e, UDP dgram peer |
| Host tests | `ctest` (`cxc::net`) + `contract_test.py --host-only` |

## Features

- **Boot / memory** — UEFI, MBR+FAT32 ESP, inline-extent PMM, heap, NX stacks, runtime TLB shootdown
- **SMP** — INIT-SIPI-SIPI, per-CPU run queues, migration, Event/Mutex, stack reclaim
- **Devices** — PCI, IOAPIC, UART IRQ, EDU MSI+DMA, e1000e MSI-X multi-vector
- **Network** — ARP/ICMP/UDP Echo, 256-frame ring stress (+ IRQ demux/echo TX), socket `recv_wait`/`send`, M25–M28 TX submit/back-pressure/wait
- **Contracts** — positive path + negative controls (no device / bad checksum / no peer)

## Quick start (Linux)

```bash
sudo apt-get install \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
  cmake python3 qemu-system-x86 ovmf

cmake -S . -B build/uefi \
  -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-uefi-mingw.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCXC_WARNINGS_AS_ERRORS=ON
cmake --build build/uefi --parallel
cmake --build build/uefi --target qemu-test
```

Host-only protocol tests:

```bash
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests --parallel
ctest --test-dir build/host-tests --output-on-failure
python3 tools/contract_test.py --host-only --work-dir build/contract-host
```

## CI

GitHub Actions (`.github/workflows/ci.yml`) runs on every push/PR:

| Job | What it enforces |
|---|---|
| `linux-uefi-qemu` | Debug **and** Release builds (`-Werror`), host `ctest` + contract, default `qemu-test`, memory/APIC/SMP matrix, device-missing negatives, full midpoint contract |
| `windows-uefi-build` | MSYS2 UCRT64 toolchain build (`-Werror`), host contract, non-empty EFI/ESP |

Linux job uploads EFI, ESP, serial/peer logs, and SHA-256 evidence summaries as artifacts.

## Repository layout

```text
cmake/          UEFI MinGW toolchain
include/cxc/    Public headers (net, scheduler, drivers, …)
src/            Kernel, e1000e, net, runtime tests
tests/host/     Freestanding net sources compiled with host g++
tools/          QEMU runner, network peer, contract & release validators
docs/           Architecture, contracts, midfield assessments
```

## Boundaries

Not claimed: userspace/syscalls/ELF, storage/VFS, TCP/IPv6, production NIC driver (multi-queue/PHY/reset), physical hardware, Windows QEMU in CI.

## License

[MIT](LICENSE) © 2026 skyline019
