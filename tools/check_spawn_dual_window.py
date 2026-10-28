#!/usr/bin/env python3
"""Assert CCDRV serial spawn dual-window (M293/M295). Usage: script serial.log"""
from __future__ import annotations

import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_spawn_dual_window.py SERIAL.log", file=sys.stderr)
        return 2
    s = Path(sys.argv[1]).read_bytes()
    legacy = (
        b"bridge: 1 tcap: 1 spawn_as: 4 spawn_ld: 1 spawn_cc: 3 "
        b"tdrv: 1 ccdrv: 1 edge: 1"
    )
    legacy_cc4 = (
        b"bridge: 1 tcap: 1 spawn_as: 4 spawn_ld: 1 spawn_cc: 4 "
        b"inputs_c: 3 tdrv: 1 ccdrv: 1 edge: 1"
    )
    m = re.search(
        rb"bridge: 1 tcap: 1 spawn_as: (\d+) spawn_ld: 1 spawn_cc: ([34])\b"
        rb".*?inputs_s: (\d+).*?planted: (\d+).*?tdrv: 1 ccdrv: 1 edge: 1",
        s,
    )
    ok = legacy in s or legacy_cc4 in s
    if m:
        sas, scc, ins, plant = (int(m.group(i)) for i in range(1, 5))
        ok = ok or (sas == ins and plant <= 22 and scc in (3, 4))
    if not ok:
        print("CCDRV spawn dual-window mismatch", file=sys.stderr)
        return 1
    plant_m = re.search(rb"planted: (\d+)", s)
    if plant_m and int(plant_m.group(1)) > 22:
        print("planted > 22", file=sys.stderr)
        return 1
    print("spawn dual-window ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
