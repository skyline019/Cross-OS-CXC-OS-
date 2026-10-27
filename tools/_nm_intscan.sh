#!/usr/bin/env bash
set -euo pipefail
TMP=$(mktemp -d)
cd "$TMP"
ar x /usr/lib/x86_64-linux-musl/libc.a intscan.lo shgetc.lo memrchr.lo
for f in intscan.lo shgetc.lo memrchr.lo; do
  echo "=== $f ==="
  nm -u "$f" || true
  nm "$f" | head -12 || true
done
ar t /usr/lib/x86_64-linux-musl/libc.a | grep -i shlim || true
rm -rf "$TMP"
