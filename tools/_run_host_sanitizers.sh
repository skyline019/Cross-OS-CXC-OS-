#!/usr/bin/env bash
set -euo pipefail
cd /mnt/e/OS/workspace-study/cxc-os
for san in address undefined thread; do
  echo "======== host sanitizer: ${san} ========"
  cmake -S tests/host -B "build/host-${san}" -DCXC_HOST_SANITIZER="${san}"
  cmake --build "build/host-${san}" --parallel
  ctest --test-dir "build/host-${san}" --output-on-failure
done
echo "ALL HOST SANITIZERS OK"
