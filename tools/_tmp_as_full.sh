#!/bin/bash
set -e
ROOT=/mnt/e/OS/workspace-study/cxc-os
WORK=/tmp/asfull
rm -rf "$WORK"
mkdir -p "$WORK"
cd "$ROOT"
python3 tools/_tmp_extract_main_s.py
cat > "$WORK/t.c" << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int as_compile(const char* src, size_t n, unsigned char** out, size_t* out_n);
int main(void) {
  FILE* f = fopen("/tmp/probe_main.s", "rb");
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* buf = malloc((size_t)n + 1);
  fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[n] = 0;
  unsigned char* o = 0;
  size_t on = 0;
  int rc = as_compile(buf, (size_t)n, &o, &on);
  fprintf(stderr, "rc=%d on=%zu\n", rc, on);
  return rc != 0;
}
EOF
gcc -std=c11 -O2 -DCC_HOST=1 -I user -o "$WORK/t" \
  "$WORK/t.c" \
  user/cxc_cc/as.c \
  user/cxc_cc/elflink.c \
  user/cxc_cc/emit.c \
  user/cxc_cc/lex.c \
  user/cxc_cc/parse.c \
  user/cxc_cc/prep.c \
  user/cxc_cc/ld.c
"$WORK/t"
echo PASS_AS_PROBE
