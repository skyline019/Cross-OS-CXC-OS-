#!/usr/bin/env bash
# Run xv6 with 160MiB RAM so [PMEMBASE, PMEMEND) is backed (MAP mode).
# Optional: persist guest RAM to file for PMEM2 cold-reboot experiments.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CPUS="${CPUS:-3}"
MEM_PATH="${PMEM_RAM_FILE:-}"
EXTRA=()

if [[ -n "$MEM_PATH" ]]; then
  # Whole-guest RAM on host file (share=on) for power-loss style restarts.
  if [[ ! -f "$MEM_PATH" ]]; then
    truncate -s 160M "$MEM_PATH"
  fi
  EXTRA+=(
    -object "memory-backend-file,id=mem,size=160M,mem-path=${MEM_PATH},share=on"
    -numa "node,memdev=mem"
  )
  MEM_OPT=()
else
  MEM_OPT=(-m 160M)
fi

exec qemu-system-riscv64 \
  -machine virt -bios none -kernel kernel/kernel \
  "${MEM_OPT[@]}" \
  "${EXTRA[@]}" \
  -smp "$CPUS" -nographic \
  -global virtio-mmio.force-legacy=false \
  -drive file=fs.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  "$@"
