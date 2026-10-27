#!/bin/bash
set -e
ARCH=""
for p in /usr/lib/x86_64-linux-musl/libc.a /usr/lib/musl/lib/libc.a; do
  if [[ -f "$p" ]]; then ARCH="$p"; break; fi
done
echo "archive=$ARCH"
TMP=$(mktemp -d)
cd "$TMP"
for m in strcspn strspn strpbrk strrchr memmove strdup strtol __intscan __shgetc __uflow __toread __stdio_exit __errno_location __stack_chk_fail __stack_chk_guard memcpy; do
  lo=$(ar t "$ARCH" | grep -E "^${m}\\.lo$" | head -1 || true)
  if [[ -z "$lo" ]]; then
    lo=$(ar t "$ARCH" | grep -E "${m}" | head -3 || true)
    echo "MISSING-or-alt $m => $lo"
    continue
  fi
  ar x "$ARCH" "$lo"
  echo "== $lo"
  nm -u "$lo" 2>/dev/null | head -40 || nm "$lo" | head -40
done
# also find intscan / shgetc member names
echo "== members matching intscan/shgetc/uflow/stack"
ar t "$ARCH" | grep -Ei 'intscan|shgetc|uflow|toread|stack_chk|strdup|memmove|strcspn|strspn|strpbrk|strrchr|strtol|errno' | head -60
rm -rf "$TMP"
