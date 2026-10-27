#!/usr/bin/env bash
set -euo pipefail
AR=/usr/lib/x86_64-linux-musl/libc.a
tmp=$(mktemp -d)
cd "$tmp"
ar t "$AR" | grep -i strchr
ar x "$AR" strchrnul.lo
echo "=== strchrnul.lo ==="
readelf -s strchrnul.lo | grep -E 'UND|GLOBAL|WEAK'
readelf -r strchrnul.lo | head -15
# who defines __shlim
nm "$AR" | grep ' T __shlim$'
# find member for T __shlim
python3 - <<'PY'
import subprocess
ar="/usr/lib/x86_64-linux-musl/libc.a"
# ar t and nm - print members... use llvm-nm or:
out=subprocess.check_output(["nm","-A",ar], text=True, errors="replace")
for line in out.splitlines():
    if " T __shlim" in line or " T __shlim\n" in line or line.endswith(" T __shlim"):
        print(line)
PY
rm -rf "$tmp"
