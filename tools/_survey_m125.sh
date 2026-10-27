#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
tmp=$(mktemp -d)
cd "$tmp"

# BFS-ish: start from strtol.lo, collect UND GLOBAL symbols and find members
need=("strtol.lo")
seen=""
queue=("strtol.lo")
while [ ${#queue[@]} -gt 0 ]; do
  m="${queue[0]}"
  queue=("${queue[@]:1}")
  case " $seen " in
    *" $m "*) continue ;;
  esac
  seen="$seen $m"
  if ! ar t "$AR" | grep -qx "$m"; then
    echo "MISSING member $m"
    continue
  fi
  ar x "$AR" "$m"
  echo "=== $m ==="
  und=$(readelf -s "$m" | awk '/UND/ && /GLOBAL/ {print $NF}' | sort -u)
  echo "UND: $und"
  for sym in $und; do
    # map symbol -> .lo via nm on archive is slow; try common names
    cand="${sym}.lo"
    cand2="${sym#__}.lo"
    cand3="${sym#___}.lo"
    for c in "$cand" "$cand2" "__${sym}.lo" "${sym}.o"; do
      if ar t "$AR" | grep -qx "$c"; then
        queue+=("$c")
        echo "  -> $c for $sym"
        break
      fi
    done
  done
done

echo
echo "==== members extracted ===="
ls -1 *.lo 2>/dev/null || true

echo
echo "==== per-file UND + rela counts ===="
for f in *.lo; do
  [ -f "$f" ] || continue
  nu=$(readelf -s "$f" | awk '/UND/ && /GLOBAL/ {c++} END{print c+0}')
  nr=$(readelf -r "$f" | grep -c 'Relocation section' || true)
  ns=$(readelf -S "$f" | grep -c 'RELA' || true)
  echo "$f und=$nu rela_secs~$ns"
  readelf -s "$f" | awk '/UND/ && /GLOBAL/ {print "   UND", $NF}'
done

# also find shgetc shlim errno stack_chk
echo
echo "==== archive grep ===="
ar t "$AR" | grep -iE 'shgetc|shlim|intscan|errno|stack_chk|strtol|stdio_impl|file' | head -60

rm -rf "$tmp"
