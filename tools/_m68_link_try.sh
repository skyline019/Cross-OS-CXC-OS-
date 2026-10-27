#!/usr/bin/env bash
set -euo pipefail
ROOT=/mnt/e/OS/workspace-study/cxc-os
OUT=/tmp/m68_musl_crt
SRC="$ROOT/user/musl_crt_probe.c"
LD="$ROOT/user/musl_crt.ld"

musl-gcc -static -fno-pie -no-pie -fno-stack-protector \
  -Wl,--build-id=none "-Wl,-T,$LD" \
  -o "$OUT" "$SRC"
ls -la "$OUT"
readelf -l "$OUT"
echo "--- AT_PHDR candidate: e_phoff + load bias ---"
readelf -h "$OUT" | head -20
echo "--- syscalls ---"
objdump -d "$OUT" | grep -E 'syscall$|<(write|_start|__init_libc|exit_group)' | head -40
