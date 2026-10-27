#!/bin/bash
set -e
ROOT=/mnt/e/OS/workspace-study/cxc-os
cd "$ROOT"
for m in toascii imaxabs bcmp ffs; do
  python3 tools/make_musl_mem_elf.py \
    --member "$m" \
    --output "user/prebuilt/musl_${m}.o" \
    --header "include/cxc/musl_${m}_elf.hpp" \
    --prebuilt "user/prebuilt/musl_${m}.o"
  echo "ok $m"
done
