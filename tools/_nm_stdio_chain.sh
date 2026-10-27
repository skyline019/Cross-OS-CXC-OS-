#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
TMP=$(mktemp -d)
cd "$TMP"
# Follow UND chain a few steps
need="strtol.lo intscan.lo shgetc.lo"
for round in 1 2 3 4 5; do
  echo "===== round $round need=$need ====="
  new=""
  for m in $need; do
    if [[ ! -f $m ]]; then
      ar x "$AR" "$m" 2>/dev/null || { echo "MISSING $m"; continue; }
    fi
    echo "--- $m ---"
    nm -u "$m" 2>/dev/null | awk '{print $NF}' | sort -u
    for u in $(nm -u "$m" 2>/dev/null | awk '{print $NF}'); do
      # map symbol to likely .lo
      case "$u" in
        __intscan) cand=intscan.lo ;;
        __shgetc|__shlim) cand=shgetc.lo ;;
        __uflow) cand=__uflow.lo ;;
        ___errno_location|__errno_location) cand=__errno_location.lo ;;
        __stack_chk_fail) cand=__stack_chk_fail.lo ;;
        __memcpy_fwd|memcpy) cand=memcpy.lo ;;
        *) cand="" ;;
      esac
      if [[ -n "$cand" ]]; then
        new="$new $cand"
      fi
    done
  done
  need=$(echo "$new" | tr ' ' '\n' | sort -u | tr '\n' ' ')
done
ar t "$AR" | grep -iE 'uflow|stdio' | head -30
rm -rf "$TMP"
