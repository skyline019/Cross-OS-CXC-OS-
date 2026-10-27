#!/bin/bash
set -e
ROOT=/mnt/e/OS/workspace-study/cxc-os
WORK=/tmp/asprobe
rm -rf "$WORK"
mkdir -p "$WORK"
cd "$ROOT"
python3 tools/_tmp_extract_main_s.py
# Link as with deps like host test (from cc_host_test build pattern)
gcc -O2 -std=c11 -Wall -Wno-unused -I user \
  -c user/cxc_cc/as.c -o "$WORK/as.o"
gcc -O2 -std=c11 -Wall -Wno-unused -I user \
  -c user/cxc_cc/elflink.c -o "$WORK/elflink.o"
# Minimal stubs if needed — check undefined
gcc -O2 -std=c11 -Wall -I user -c tools/_tmp_as_probe.c -o "$WORK/probe.o"
# Find what as_compile needs
und=$(nm -u "$WORK/as.o" | awk '{print $NF}' | sort -u)
echo "as.o UND: $und"
# Compile remaining cc pieces commonly needed
for f in emit prep lex parse ld main; do
  if [ -f "user/cxc_cc/$f.c" ]; then
    gcc -O2 -std=c11 -Wall -Wno-unused -I user -c "user/cxc_cc/$f.c" -o "$WORK/$f.o" 2>/dev/null || true
  fi
done
gcc -O2 -o "$WORK/asprobe" "$WORK/probe.o" "$WORK/as.o" "$WORK/elflink.o" \
  "$WORK/emit.o" 2>"$WORK/link.err" || {
  echo "link fail:"; cat "$WORK/link.err"
  # try with more objs
  gcc -O2 -o "$WORK/asprobe" "$WORK"/*.o -lc 2>"$WORK/link2.err" || {
    cat "$WORK/link2.err"; exit 1
  }
}
"$WORK/asprobe" < /tmp/probe_main.s
echo exit=$?
