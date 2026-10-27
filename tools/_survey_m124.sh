#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
tmp=$(mktemp -d)
cd "$tmp"
ar t "$AR" | grep -iE 'stpcpy|strcpy|strncmp' | head -20
ar x "$AR" stpcpy.lo strcpy.lo strncmp.lo 2>/dev/null || true
ls -la
for f in *.lo; do
  echo "=== $f ==="
  readelf -s "$f" | grep -E 'UND|GLOBAL' | head -25
  readelf -r "$f" | head -15
  readelf -S "$f" | head -20
done
rm -rf "$tmp"
