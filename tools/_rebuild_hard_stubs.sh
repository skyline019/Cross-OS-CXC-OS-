#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
gcc -c -Os -fno-stack-protector -fno-asynchronous-unwind-tables -fno-pic \
  -o user/prebuilt/hard_stubs.o tools/_hard_stubs.c
# Strip debug; keep relocatable.
objcopy --remove-section=.comment --remove-section=.note.GNU-stack \
  user/prebuilt/hard_stubs.o 2>/dev/null || true
python3 tools/_gen_hard_stubs_hdr.py
readelf -S user/prebuilt/hard_stubs.o | sed -n '1,25p'
nm user/prebuilt/hard_stubs.o
echo "size=$(wc -c < user/prebuilt/hard_stubs.o)"
