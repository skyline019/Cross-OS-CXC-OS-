#!/bin/bash
set -e
cd /tmp
rm -rf mwrap
mkdir mwrap
cd mwalk 2>/dev/null || true
cd /tmp/mwrap
ar x /usr/lib/x86_64-linux-musl/libc.a toascii.lo imaxabs.lo bcmp.lo ffs.lo htons.lo 2>&1 || true
for f in toascii imaxabs bcmp ffs htons; do
  echo "=== $f ==="
  if [ -f "$f.lo" ]; then
    nm -u "$f.lo" || true
    ls -la "$f.lo"
  else
    echo "missing $f.lo"
    ar t /usr/lib/x86_64-linux-musl/libc.a | grep -E "$f" || true
  fi
done
