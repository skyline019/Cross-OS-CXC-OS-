#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
tmp=$(mktemp -d)
cd "$tmp"
ar x "$AR" strtol.lo strcmp.lo strcpy.lo strncmp.lo
for f in strtol.lo strcmp.lo strcpy.lo strncmp.lo; do
  echo "=== $f ==="
  readelf -s "$f" | grep -E 'UND|GLOBAL.*FUNC' | head -30
  echo "-- relocs --"
  readelf -r "$f" | head -40
  echo "-- sections --"
  readelf -S "$f" | head -40
done
rm -rf "$tmp"
