#!/usr/bin/env python3
"""Run pmfs_bench (N=32) and light-update docs/BENCH.md single-point section.
Prefer `make bench-sweep` for full depth tables."""
import os
import re
import subprocess
import sys


def main():
    root = os.path.join(os.path.dirname(__file__), "..")
    os.chdir(root)
    env = os.environ.copy()
    env.setdefault("CPUS", "1")
    r = subprocess.run(
        ["python3", "tools/contract_pmfs.py", "--bench"],
        env=env,
        capture_output=True,
        text=True,
    )
    out = r.stdout + r.stderr
    sys.stdout.write(out)
    if r.returncode != 0:
        return r.returncode

    m = re.search(
        r"ONBOX_PMFS_BENCH_PASS trials=(\d+) N=(\d+) median_pmfs=(\d+) "
        r"median_xv6=(\d+) ratio=(\d+) logical=(\d+) phys=(\d+)(?: wa_x100=(\d+))?",
        out,
    )
    if not m:
        print("run_bench_fill: parse failed", file=sys.stderr)
        return 1
    trials, n, mp, mx, ratio, logical, phys = m.group(1, 2, 3, 4, 5, 6, 7)
    wa = m.group(8) or "?"
    note = (
        f"\n\n## 单点快照（bench-fill，N={n}）\n\n"
        f"| median_pmfs | median_xv6 | ratio×100 | wa_x100 | logical | phys |\n"
        f"|-------------|------------|-----------|---------|---------|------|\n"
        f"| {mp} | {mx} | {ratio} | {wa} | {logical} | {phys} |\n\n"
        f"完整扫参与分析见上文；生成：`make bench-sweep`。\n"
    )
    path = "docs/BENCH.md"
    if os.path.exists(path):
        text = open(path, encoding="utf-8").read()
        # Replace or append snapshot
        if "## 单点快照" in text:
            text = re.sub(
                r"\n## 单点快照.*",
                note.rstrip() + "\n",
                text,
                flags=re.S,
            )
        else:
            text = text.rstrip() + note
        open(path, "w", encoding="utf-8").write(text)
    else:
        open(path, "w", encoding="utf-8").write("# BENCH\n" + note)
    print(f"run_bench_fill: updated {path} (N={n}, trials={trials})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
