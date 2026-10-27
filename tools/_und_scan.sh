#!/bin/bash
cd /mnt/e/OS/workspace-study/cxc-os || exit 1
for f in disk/main.o disk/lib.o disk/atoi.o disk/scmp.o disk/labs.o disk/dig.o \
         disk/scpy.o disk/scat.o disk/stpc.o disk/sncmp.o disk/schr.o disk/snul.o \
         disk/mcmp.o disk/io.elf; do
  echo "=== $f"
  readelf -s "$f" 2>/dev/null | awk '/UND/ {print $NF}'
done
echo "=== DEFINED GLOBALS ==="
for f in disk/*.o disk/io.elf; do
  readelf -s "$f" 2>/dev/null | awk -v f="$f" '/GLOBAL DEFAULT/ && !/UND/ {print f, $NF}'
done
