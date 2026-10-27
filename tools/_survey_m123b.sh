#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
ar t "$AR" | grep -E 'intscan|shlim|stack_chk|strtol' | head -40
tmp=$(mktemp -d)
cd "$tmp"
# find member names
ar t "$AR" | grep -E 'intscan|shlim' 
for m in $(ar t "$AR" | grep -E '^(intscan|__intscan|shlim|__shlim|strtol)\.lo$'); do
  echo "member $m"
done
ar x "$AR" intscan.lo 2>/dev/null || ar x "$AR" __intscan.lo 2>/dev/null || true
ar x "$AR" shlim.lo 2>/dev/null || ar x "$AR" __shlim.lo 2>/dev/null || true
ls -la
for f in *.lo; do
  [ -f "$f" ] || continue
  echo "=== $f ==="
  readelf -s "$f" | grep -E 'UND|GLOBAL' | head -40
  readelf -r "$f" | head -50
done
rm -rf "$tmp"
