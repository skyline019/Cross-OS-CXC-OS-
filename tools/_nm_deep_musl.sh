#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
TMP=$(mktemp -d)
cd "$TMP"
for m in strcspn.lo strspn.lo strpbrk.lo strrchr.lo memmove.lo strdup.lo strtol.lo \
         __intscan.lo __shgetc.lo __errno_location.lo __stack_chk_fail.lo \
         __memrchr.lo strchrnul.lo malloc.lo memcpy.lo strlen.lo; do
  echo "=== $m ==="
  if ar t "$AR" | grep -qx "$m"; then
    ar x "$AR" "$m"
    nm -u "$m" 2>/dev/null || true
    nm "$m" 2>/dev/null | head -6 || true
  else
    echo "MISSING"
  fi
done
rm -rf "$TMP"
