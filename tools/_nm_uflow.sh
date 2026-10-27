#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
TMP=$(mktemp -d)
cd "$TMP"
ar x "$AR" __uflow.lo 2>/dev/null || ar x "$AR" uflow.lo
ls *.lo
for f in *.lo; do
  echo "=== $f ==="
  nm -u "$f" || true
done
# find toread
ar t "$AR" | grep -i toread
ar x "$AR" __toread.lo 2>/dev/null || ar x "$AR" toread.lo 2>/dev/null || true
ls toread* __toread* 2>/dev/null || true
if [[ -f __toread.lo ]]; then nm -u __toread.lo; nm __toread.lo | head; fi
if [[ -f toread.lo ]]; then nm -u toread.lo; nm toread.lo | head; fi
rm -rf "$TMP"
