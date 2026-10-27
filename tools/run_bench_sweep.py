#!/usr/bin/env python3
"""Sweep N∈{8,16,32} EMUL + N=32 MAP; rewrite docs/BENCH.md depth tables."""
import os
import re
import signal
import subprocess
import sys
import time

NS = [8, 16, 32]
TIMEOUT = 180.0


def kill_tree(proc):
    try:
        ps = subprocess.run(
            ["ps", "-opid", "--no-headers", "--ppid", str(proc.pid)],
            stdout=subprocess.PIPE,
            encoding="utf8",
            check=False,
        )
        for line in ps.stdout.splitlines():
            try:
                os.kill(int(line.strip()), signal.SIGKILL)
            except OSError:
                pass
        proc.terminate()
    except Exception:
        pass


def run_one(env, make_target: str, n: int, mode: str) -> dict:
    proc = subprocess.Popen(
        ["make", make_target],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )
    os.set_blocking(proc.stdout.fileno(), False)
    out = ""
    deadline = time.time() + TIMEOUT
    sent = False
    try:
        while time.time() < deadline:
            try:
                chunk = os.read(proc.stdout.fileno(), 4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                out += text
                if (not sent) and ("init: starting sh" in out or "$ " in out):
                    proc.stdin.write(f"pmfs_bench {n}\n".encode())
                    proc.stdin.flush()
                    sent = True
                if (
                    sent
                    and "ONBOX_PMFS_BENCH_PASS" in out
                    and f" N={n} " in out
                    and "wa_x100=" in out
                    and "thr_file=" in out
                ):
                    time.sleep(0.25)
                    break
            else:
                time.sleep(0.05)
            if proc.poll() is not None:
                break
    finally:
        kill_tree(proc)

    m = re.search(
        rf"ONBOX_PMFS_BENCH_PASS trials=(\d+) N={n} "
        rf"median_pmfs=(\d+) median_file=(\d+) median_xv6=(\d+) "
        rf"ratio=(\d+) fratio=(\d+) thr_file=(\d+) thr_xv6=(\d+) "
        rf"logical=(\d+) phys=(\d+) wa_x100=(\d+)",
        out,
    )
    if not m:
        raise RuntimeError(f"parse failed for N={n} mode={mode}\n---\n{out[-2500:]}")
    return {
        "n": n,
        "trials": int(m.group(1)),
        "median_pmfs": int(m.group(2)),
        "median_file": int(m.group(3)),
        "median_xv6": int(m.group(4)),
        "ratio": int(m.group(5)),
        "fratio": int(m.group(6)),
        "thr_file": int(m.group(7)),
        "thr_xv6": int(m.group(8)),
        "logical": int(m.group(9)),
        "phys": int(m.group(10)),
        "wa_x100": int(m.group(11)),
        "mode": mode,
    }


def write_bench_md(emul_rows, map_row):
    lines = [
        "# PMFS 文件子系统 vs xv6 fs 对比评测（合题深度版）",
        "",
        "## 1. 方法与为何「不对等」",
        "",
        "本实验对比三条路径，目的是量级说明而非绝对公平微基准：",
        "",
        "| 维度 | PMFS-KV | PMFS-File | xv6 `fs+log` |",
        "|------|---------|-----------|--------------|",
        "| API | `put`×N + 1×`seal` + `get`×N | `create/write/fsync`×N | `open/write`×N + `sync` |",
        "| 持久语义 | 双槽 SB | inode 页 + SB | 块日志检查点 |",
        "| I/O 栈 | 字节寻址 / EMUL | 同左 | virtio-blk + bio + log |",
        "",
        "**解读约束：** ratio≫1 主要反映「避免 virtio 日志与目录更新」的架构差。",
        "吞吐 `thr_*` 为相对单位（`N×1e6/median_r_time`），不作墙钟 MB/s。",
        "",
        "计时：`cycles()` → 内核 `r_time()`；每 N 做 5 trial 取**中位数**。",
        "",
        "```bash",
        "make bench-sweep",
        "```",
        "",
        "## 2. EMUL / NVDIMM 仿真边界",
        "",
        "- 默认 EMUL：算法与契约快回归。",
        "- MAP / PMEM2（`memory-backend-file`）= QEMU **NVDIMM 仿真**主路径。",
        "- EMUL **不**代表真 NVDIMM 延迟。",
        "",
        "## 3. 写放大 WA",
        "",
        "`wa_x100 = phys*100/logical`（KV 路径）。小 value 对 4KiB 页必然放大。",
        "",
        "## 4. 随 N 变化（EMUL，中位数）",
        "",
        "| N | median_kv | median_file | median_xv6 | fratio×100 | thr_file | thr_xv6 | wa_x100 |",
        "|---|-----------|-------------|------------|------------|----------|---------|---------|",
    ]
    for r in emul_rows:
        lines.append(
            f"| {r['n']} | {r['median_pmfs']} | {r['median_file']} | {r['median_xv6']} | "
            f"{r['fratio']} | {r['thr_file']} | {r['thr_xv6']} | {r['wa_x100']} |"
        )
    lines += [
        "",
        "### 观察",
        "",
        "- xv6 随 N 近似线性；PMFS-File 每文件 fsync 仍避免块日志栈。",
        "- `thr_file` / `thr_xv6` 给出吞吐相对量级（合题「延迟与吞吐」）。",
        "",
        "## 5. MAP vs EMUL（N=32）",
        "",
        "| mode | median_kv | median_file | median_xv6 | fratio×100 | thr_file |",
        "|------|-----------|-------------|------------|------------|----------|",
    ]
    e32 = next((x for x in emul_rows if x["n"] == 32), None)
    if e32:
        lines.append(
            f"| EMUL | {e32['median_pmfs']} | {e32['median_file']} | {e32['median_xv6']} | "
            f"{e32['fratio']} | {e32['thr_file']} |"
        )
    if map_row:
        lines.append(
            f"| MAP | {map_row['median_pmfs']} | {map_row['median_file']} | {map_row['median_xv6']} | "
            f"{map_row['fratio']} | {map_row['thr_file']} |"
        )
    lines += [
        "",
        "若 EMUL/MAP 同量级，瓶颈在软件提交路径。",
        "",
        "## 6. 答辩一句话",
        "",
        "> 合题四要件均有落点：NVDIMM 仿真窗口、去块设备文件布局、行级刷写协议、",
        "> 延迟+吞吐对比；不可替代点仍是可机器判定的崩溃/掉电契约套件。",
        "",
    ]
    with open("docs/BENCH.md", "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote docs/BENCH.md")


def main():
    os.chdir(os.path.join(os.path.dirname(__file__), ".."))
    env = os.environ.copy()
    env["CPUS"] = "1"

    subprocess.run(["make", "kernel/kernel", "fs.img"], check=True, env=env)
    emul_rows = []
    for n in NS:
        print(f"=== EMUL N={n} ===", flush=True)
        emul_rows.append(run_one(env, "qemu", n, "EMUL"))

    print("=== MAP rebuild ===", flush=True)
    subprocess.run(["make", "kernel-pmem"], check=True, env=env)
    map_row = run_one(env, "qemu-pmem-run", 32, "MAP")

    write_bench_md(emul_rows, map_row)

    print("=== restore EMUL ===", flush=True)
    subprocess.run(["make", "kernel-emul"], check=True, env=env)
    print("bench-sweep: OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print("bench-sweep:", e, file=sys.stderr)
        sys.exit(1)
