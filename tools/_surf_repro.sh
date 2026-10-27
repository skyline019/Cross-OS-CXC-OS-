#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
WORK=$(mktemp -d)
mkdir -p "$WORK/disk"
for pair in \
  atoi:musl_atoi scmp:musl_strcmp labs:musl_labs dig:musl_isdigit \
  scat:musl_strcat schr:musl_strchr snul:musl_strchrnul scpy:musl_strcpy \
  stpc:musl_stpcpy sncmp:musl_strncmp mcmp:musl_memcmp mchr:musl_memchr \
  tlwr:musl_tolower tupr:musl_toupper ispc:musl_isspace ialp:musl_isalpha \
  mset:musl_memset mcpy:musl_memcpy slen:musl_strlen
do
  dst=${pair%%:*}
  src=${pair##*:}
  cp "user/prebuilt/$src.o" "$WORK/disk/$dst.o"
done
gcc -std=c11 -DCC_HOST=1 -Iuser -o "$WORK/repro" \
  tools/_surf_repro.c \
  user/cxc_cc/lex.c user/cxc_cc/parse.c user/cxc_cc/emit.c \
  user/cxc_cc/prep.c user/cxc_cc/elflink.c user/cxc_cc/as.c user/cxc_cc/ld.c
(cd "$WORK" && ./repro)
echo "WORK=$WORK"
