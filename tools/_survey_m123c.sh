#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
ar t "$AR" | grep -i stpcpy
tmp=$(mktemp -d)
cd "$tmp"
ar x "$AR" stpcpy.lo strcpy.lo 2>/dev/null || true
ls
for f in *.lo; do
  echo "=== $f ==="
  readelf -s "$f" | grep -E 'UND|GLOBAL' | head -20
  readelf -r "$f" | head -20
  readelf -S "$f" | head -25
done
# also check CC_LINK and whether we need more plants for a meaningful stride:
# host-gcc -c with -fno-stack-protector of a multi-file that uses PC32 to .rodata
rm -rf "$tmp"
