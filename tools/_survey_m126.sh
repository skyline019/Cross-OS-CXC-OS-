#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
echo "=== members of interest ==="
ar t "$AR" | grep -iE 'shlim|errno|stack_chk|stdio_exit|strchr|strstr|strcat|strdup|memmove|memcmp' | head -40

tmp=$(mktemp -d)
cd "$tmp"

# find who defines __shlim
ar t "$AR" | while read m; do
  ar p "$AR" "$m" 2>/dev/null | readelf -s /dev/stdin 2>/dev/null | grep -q ' GLOBAL .* __shlim$' && echo "defines __shlim: $m"
done || true

# direct extract candidates
for m in shlim.lo __shlim.lo strchr.lo strstr.lo strcat.lo memmove.lo __stdio_exit.lo __errno_location.lo; do
  if ar t "$AR" | grep -qx "$m"; then
    ar x "$AR" "$m"
    echo "=== $m ==="
    readelf -s "$m" | grep -E 'UND|GLOBAL|WEAK' | head -25
    readelf -r "$m" | head -20
    readelf -S "$m" | head -25
  fi
done

# nm scan for __shlim definition
echo "=== nm __shlim ==="
nm "$AR" 2>/dev/null | grep ' __shlim$' | head -10
nm "$AR" 2>/dev/null | grep ' ___errno_location$' | head -5
nm "$AR" 2>/dev/null | grep ' __stdio_exit' | head -10

rm -rf "$tmp"
