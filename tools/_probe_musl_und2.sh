#!/bin/bash
set -e
ARCH=/usr/lib/x86_64-linux-musl/libc.a
TMP=$(mktemp -d)
cd "$TMP"
for lo in intscan.lo shgetc.lo memrchr.lo __memcpy_fwd.lo memcpy.lo \
          __stack_chk_fail.lo __errno_location.lo strtol.lo \
          __stdio_exit_needed.lo ofl.lo; do
  if ar t "$ARCH" | grep -qx "$lo"; then
    ar x "$ARCH" "$lo"
    echo "==== $lo"
    nm -u "$lo" 2>/dev/null || true
    echo "defined:"
    nm "$lo" | grep ' T \| D \| B \| R ' | head -20
  else
    echo "==== MISSING $lo"
    ar t "$ARCH" | grep -i "${lo%.lo}" | head -10 || true
  fi
done
# show memcpy exports for __memcpy_fwd
ar x "$ARCH" memcpy.lo
echo "==== memcpy exports"
nm memcpy.lo | head -30
rm -rf "$TMP"
