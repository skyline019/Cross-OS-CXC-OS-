#!/usr/bin/env python3
"""Sweep N∈{8,16,32} EMUL + N=32 MAP; rewrite docs/BENCH.md depth tables."""
import os
import re
import signal
import subprocess
import sys
import time

NS = [8, 16, 32]
TIMEOUT = 120.0


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
        rf"ONBOX_PMFS_BENCH_PASS trials=(\d+) N={n} median_pmfs=(\d+) "
        rf"median_xv6=(\d+) ratio=(\d+) logical=(\d+) phys=(\d+) wa_x100=(\d+)",
        out,
    )
    if not m:
        raise RuntimeError(f"parse failed for N={n} mode={mode}\n---\n{out[-2000:]}")
    return {
        "n": n,
        "trials": int(m.group(1)),
        "median_pmfs": int(m.group(2)),
        "median_xv6": int(m.group(3)),
        "ratio": int(m.group(4)),
        "logical": int(m.group(5)),
        "phys": int(m.group(6)),
        "wa_x100": int(m.group(7)),
        "mode": mode,
    }


def write_bench_md(emul_rows, map_row):
    lines = [
        "# PMFS-KV vs xv6 fs 对比评测（深度版）",
        "",
        "## 1. 方法与为何「不对等」",
        "",
        "本实验**故意**对比两条异构路径，目的是量级说明而非绝对公平微基准：",
        "",
        "| 维度 | PMFS-KV | xv6 `fs+log` |",
        "|------|---------|--------------|",
        "| API | `put`×N + **1×`seal`** + `get`×N | `open/create`+`write(8B)`×N + **`sync`** |",
        "| 持久语义 | 双槽 SB 提交点 | 块日志检查点 |",
        "| I/O 栈 | 字节寻址窗口 / EMUL 缓冲 | virtio-blk + bio + log |",
        "| 元数据 | 无目录/inode | 目录项 + inode + 位图 |",
        "",
        "**解读约束：** ratio≫1 主要反映「避免 virtio 日志与目录更新」的架构差，",
        "不是「同等 POSIX 文件 API 下快 1200 倍」。论文应写作**量级优势 + 边界**。",
        "",
        "计时：`cycles()` → 内核 `r_time()`；每 N 做 5 trial 取**中位数**。",
        "",
        "```bash",
        "make bench-sweep    # 本表数据",
        "make bench-fill     # 单点快捷回填",
        "```",
        "",
        "## 2. EMUL 边界",
        "",
        "- 默认无 `-DPMEM_MAP`：2MiB EMUL，验证算法与契约。",
        "- MAP：`[PMEMBASE,PMEMEND)`；`contract-pmem2` 验证文件后端掉电。",
        "- EMUL **不**代表真 NVDIMM 延迟。",
        "",
        "## 3. 写放大 WA",
        "",
        "`wa_x100 = phys*100/logical`。",
        "",
        "- **logical**：用户 payload；**phys**：整页写入（对齐、run、B3 重写）。",
        "- 小 value（8B）对 4KiB 页必然放大；与 xv6 日志放大不同源。",
        "",
        "## 4. 随 N 变化（EMUL，中位数）",
        "",
        "| N | median_pmfs | median_xv6 | ratio×100 | wa_x100 | logical | phys |",
        "|---|-------------|------------|-----------|---------|---------|------|",
    ]
    for r in emul_rows:
        lines.append(
            f"| {r['n']} | {r['median_pmfs']} | {r['median_xv6']} | {r['ratio']} | "
            f"{r['wa_x100']} | {r['logical']} | {r['phys']} |"
        )
    lines += [
        "",
        "### 观察",
        "",
        "- xv6 随 N 近似线性（每文件元数据+日志）。",
        "- PMFS 以一次 seal 摊销提交；ratio 通常仍 ≫1。",
        "- 高 wa_x100 由页对齐主导，属预期。",
        "",
        "## 5. MAP vs EMUL（N=32）",
        "",
        "| mode | median_pmfs | median_xv6 | ratio×100 | wa_x100 |",
        "|------|-------------|------------|-----------|---------|",
    ]
    e32 = next((x for x in emul_rows if x["n"] == 32), None)
    if e32:
        lines.append(
            f"| EMUL | {e32['median_pmfs']} | {e32['median_xv6']} | {e32['ratio']} | {e32['wa_x100']} |"
        )
    if map_row:
        lines.append(
            f"| MAP | {map_row['median_pmfs']} | {map_row['median_xv6']} | {map_row['ratio']} | {map_row['wa_x100']} |"
        )
    lines += [
        "",
        "若 EMUL/MAP 同量级，瓶颈在算法/提交而非假缓冲。",
        "",
        "## 6. 答辩一句话",
        "",
        "> 在不对等但动机清晰的对比下，PMFS-KV 用一次 seal 替代多次小文件+sync，",
        "> 中位数延迟低两个数量级以上；WA 由页对齐解释；EMUL/MAP/PMEM2 划清边界。",
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
