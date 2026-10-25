#!/usr/bin/env python3
"""Convert a flat binary into a freestanding C++ byte array."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--array", required=True)
    parser.add_argument("--size", required=True)
    arguments = parser.parse_args()

    data = arguments.input.read_bytes()
    if not data:
        raise SystemExit("input binary is empty")
    if not arguments.array.isidentifier() or not arguments.size.isidentifier():
        raise SystemExit("array and size names must be C++ identifiers")

    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        rows.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    contents = "\n".join(rows)
    source = f"""// Generated from {arguments.input.name}; do not edit.
#include <cxc/smp_trampoline_blob.hpp>

namespace cxc::smp {{

alignas(16) const uint8_t {arguments.array}[] = {{
{contents}
}};
const size_t {arguments.size} = sizeof({arguments.array});

}}  // namespace cxc::smp
"""
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(source, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
