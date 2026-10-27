#!/bin/bash
set -euo pipefail
ROOT=/mnt/e/OS/workspace-study/cxc-os
cd "$ROOT"
PRE=$ROOT/user/prebuilt
INC=$ROOT/include/cxc
mkdir -p "$PRE" "$INC"

# Freestanding stubs (ssp / errno / uflow / malloc)
STUB_C=$ROOT/tools/_hard_stubs.c
cat > "$STUB_C" <<'EOF'
/* Freestanding stubs for deep musl HARD closure (M250–M252). */
long *___errno_location(void) {
    static long e;
    return &e;
}
void __stack_chk_fail(void) {
    for (;;) {
        __asm__ volatile("ud2");
    }
}
int __uflow(void *f) {
    (void)f;
    return -1;
}
static char heap[8192];
static unsigned long heap_off;
void *malloc(unsigned long n) {
    unsigned long a = (n + 15ull) & ~15ull;
    if (heap_off + a > sizeof(heap)) {
        return 0;
    }
    void *p = heap + heap_off;
    heap_off += a;
    return p;
}
void free(void *p) { (void)p; }
EOF
gcc -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-plt -c -o "$PRE/hard_stubs.o" "$STUB_C"
python3 - <<'PY'
from pathlib import Path
blob = Path("/mnt/e/OS/workspace-study/cxc-os/user/prebuilt/hard_stubs.o").read_bytes()
assert blob[:4]==b"\x7fELF" and blob[16]==1
parts=[f"0x{b:02x}" for b in blob]
lines=["    "+", ".join(parts[i:i+12])+"," for i in range(0,len(parts),12)]
Path("/mnt/e/OS/workspace-study/cxc-os/include/cxc/hard_stubs_elf.hpp").write_text(
"/* Generated freestanding HARD stubs. */\n#pragma once\n"
"static const unsigned char hard_stubs_elf[] = {\n"+ "\n".join(lines)+"\n};\n"
f"static const unsigned hard_stubs_elf_size = {len(blob)};\n", encoding="ascii")
print("hard_stubs", len(blob), "nm -u:")
import subprocess
print(subprocess.check_output(["nm","-u",str(Path('/mnt/e/OS/workspace-study/cxc-os/user/prebuilt/hard_stubs.o'))], text=True))
print(subprocess.check_output(["nm",str(Path('/mnt/e/OS/workspace-study/cxc-os/user/prebuilt/hard_stubs.o'))], text=True))
PY

MEMBERS="strcspn strspn strpbrk strrchr memmove strdup strtol intscan shgetc memrchr"
for m in $MEMBERS; do
  echo "=== plant $m"
  python3 tools/make_musl_mem_elf.py --member "$m" \
    --output "$PRE/musl_${m}.o" \
    --header "$INC/musl_${m}_elf.hpp" \
    --prebuilt "$PRE/musl_${m}.o"
done
echo DONE
