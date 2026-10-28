# CXC/OS

[![CI](https://github.com/skyline019/Cross-OS-CXC-OS-/actions/workflows/ci.yml/badge.svg)](https://github.com/skyline019/Cross-OS-CXC-OS-/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Native UEFI x86-64 kernel** — no host OS runtime. Higher-half PE32+, SMP/IPI, PCI/MSI-X, e1000e packet DMA, userspace ELF + CXCFS `disk/`, and an on-box mini toolchain (`disk/cc` / `as` / `ld`). Acceptance requires kernel `CXC_OS_SELF_TEST_PASS`, on-box `ONBOX_CCDRV_PASS`, **and** external Python peer `peer_ok=1`.

> Chinese deep-dive (current features): [`docs/README.zh-CN.md`](docs/README.zh-CN.md) · Goals: [`docs/product-goals.md`](docs/product-goals.md) · Toolchain: [`docs/a2-mini-cc.md`](docs/a2-mini-cc.md) · Midfield 28: [`docs/midpoint-assessment-28.md`](docs/midpoint-assessment-28.md)

## Status

| Item | Value |
|---|---|
| Baseline | **Milestone 285 / v2.81.0** (mini-IDE + CCDRV) |
| Boot | Native UEFI → higher-half PE32+ (MinGW-w64) |
| On-box contract | `ONBOX_CCDRV_PASS` + `WMSG:CD` + `exit: 42` (`spawn_ld:1`; `spawn_as`/`spawn_cc` dual-window; `planted≤33`) |
| Optional GUI | GOP + PS/2 mini-IDE → `ONBOX_IDE_PASS`; GUIUX (mouse/wm/VT100/`/dev/fb0`/`tty0`) → `ONBOX_GUIUX_PASS` (not CI-gated) |
| Evidence | QEMU + OVMF, EDU + e1000e + virtio-blk, UDP dgram peer |
| Host tests | `ctest` (`cxc::net`) + `contract_test.py --host-only` |
| A2 claim | **Not claimed** — dialect + mini as/ld, not full gcc/g++ |

## Current features

### Boot / kernel / SMP

- UEFI boot, MBR+FAT32 ESP, inline-extent PMM, NX stacks, runtime TLB shootdown
- INIT-SIPI-SIPI SMP, per-CPU run queues, explicit migration, Event/Mutex, stack reclaim
- PCI mechanism #1, IOAPIC, UART IRQ, EDU MSI+DMA, e1000e MSI-X multi-vector (queue 0)

### Network

- ARP / IPv4 ICMP Echo / UDP Echo (fixed neighbor)
- Minimal UDP sockets: bind / recv / `recv_wait` / send; IRQ demux + TX back-pressure
- External peer validation + 256-frame stress; `peer_ok=1`

### Userspace & storage

- Ring-3, static ET_EXEC, Linux-ish syscalls (`int 0x80`): brk/mmap, ramfs + **`disk/` CXCFS**, fork/wait/execve, pipe/dup, cwd, time, …
- mini_libc; host musl-gcc static ELF probes (printf/fopen/malloc/…)
- Persistent virtio-blk + CXCFS (≈128 MiB floor, hundreds of inodes, multi-MiB files for toolchain jobs)

### On-box toolchain (CCDRV)

| Binary | Role |
|---|---|
| `disk/cc` / `disk/cxc-cc` | argv `-c` / `-o` / `@job.rsp`; default bare exec → `@disk/job.rsp` |
| `disk/as` | subset assembler `.s` → ET_REL |
| `disk/ld` | response-file link → static ET_EXEC |

- Language: **`CXC_A2` dialect** (`fn` / `import` / control flow / struct / `#include`…) — **not ISO C**
- Process boundaries audited by kernel `execve`: nest `cc -c` for `a.c`/`b.c`; spawn `as`/`ld` for assemble/link
- Some musl leaf `.o` files are **host-prebuilt and planted** on `disk/`

Example:

```text
disk/cc -c disk/a.c -o disk/a.o
disk/cc -o disk/out.elf @disk/job.rsp
disk/out.elf    # exit 42 → ONBOX_CCDRV_PASS
```

### Mini-IDE (optional)

- UEFI GOP framebuffer + PS/2 keyboard; kernel text editor saves to `disk/lib.c` / `disk/ide_main.c`, rebuilds via ONBXST stub
- Default CI stays headless (`-display none`). Visible run: `python3 tools/run_qemu.py --gui …`
- Interactive hold: place non-empty `EFI/BOOT/IDEHOLD` on the ESP
- Not claimed: mouse, windowing, desktop GUI

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
src/            Kernel, e1000e, net, userspace runtime tests
user/           mini_libc, cxc-cc / as / ld, musl subset, prebuilt ELFs
tests/host/     Freestanding net sources compiled with host g++
tools/          QEMU runner, network peer, contract & release validators
docs/           Architecture, contracts, midfield assessments
```

## Boundaries

Not claimed: full ISO C / gcc·gas·ld bootstrap (**A2**), dynamic linking, demand paging, TCP/IPv6, production NIC (multi-queue/PHY/reset), physical hardware, Windows QEMU in CI, mouse/multi-window desktop.

## License

[MIT](LICENSE) © 2026 skyline019
