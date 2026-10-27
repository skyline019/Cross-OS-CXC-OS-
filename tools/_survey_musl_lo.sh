#!/bin/bash
set -e
cd /tmp
rm -rf mb && mkdir mb && cd mb
for m in memcpy.lo memset.lo memcmp.lo strlen.lo strcmp.lo atoi.lo tolower.lo; do
  ar x /usr/lib/x86_64-linux-musl/libc.a "$m" 2>/dev/null || true
done
for f in *.lo; do
  echo "===== $f ====="
  ls -la "$f"
  readelf -S "$f" | grep -E 'PROGBITS|NOBITS|RELA' | head -25
  echo REL:
  readelf -r "$f" | head -20
  echo SYM:
  readelf -s "$f" | head -15
done
