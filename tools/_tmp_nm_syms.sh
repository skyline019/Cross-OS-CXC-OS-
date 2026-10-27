#!/bin/bash
set -e
cd /mnt/e/OS/workspace-study/cxc-os
for f in bzero strnlen isascii llabs; do
  echo "=== $f ==="
  nm "user/prebuilt/musl_${f}.o"
done
