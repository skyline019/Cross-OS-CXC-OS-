#!/bin/bash
set -e
TMP=$(mktemp -d)
cd "$TMP"
ARCH=/usr/lib/x86_64-linux-musl/libc.a
for lo in strcspn.lo strspn.lo strtol.lo intscan.lo shgetc.lo __uflow.lo \
          strrchr.lo memmove.lo strdup.lo strpbrk.lo memrchr.lo; do
  ar x "$ARCH" "$lo"
  echo "==== $lo"
  objdump -d "$lo" 2>/dev/null | grep -E 'fs:|stack_chk|rep |%r8|%r9|scas|movs|stos|cld' | head -20 || true
  echo "und:"
  nm -u "$lo" | head -20
done
# also find __stdio_exit_needed
ar t "$ARCH" | grep -i stdio_exit
rm -rf "$TMP"
