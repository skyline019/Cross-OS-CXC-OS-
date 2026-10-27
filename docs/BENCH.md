# PMFS-KV vs xv6 fs 对比评测（深度版）

## 1. 方法与为何「不对等」

本实验**故意**对比两条异构路径，目的是量级说明而非绝对公平微基准：

| 维度 | PMFS-KV | xv6 `fs+log` |
|------|---------|--------------|
| API | `put`×N + **1×`seal`** + `get`×N | `open/create`+`write(8B)`×N + **`sync`** |
| 持久语义 | 双槽 SB 提交点 | 块日志检查点 |
| I/O 栈 | 字节寻址窗口 / EMUL 缓冲 | virtio-blk + bio + log |
| 元数据 | 无目录/inode | 目录项 + inode + 位图 |

**解读约束：** ratio≫1 主要反映「避免 virtio 日志与目录更新」的架构差，
不是「同等 POSIX 文件 API 下快 1200 倍」。论文应写作**量级优势 + 边界**。

计时：`cycles()` → 内核 `r_time()`；每 N 做 5 trial 取**中位数**。

```bash
make bench-sweep    # 本表数据
make bench-fill     # 单点快捷回填
```

## 2. EMUL 边界

- 默认无 `-DPMEM_MAP`：2MiB EMUL，验证算法与契约。
- MAP：`[PMEMBASE,PMEMEND)`；`contract-pmem2` 验证文件后端掉电。
- EMUL **不**代表真 NVDIMM 延迟。

## 3. 写放大 WA

`wa_x100 = phys*100/logical`。

- **logical**：用户 payload；**phys**：整页写入（对齐、run、B3 重写）。
- 小 value（8B）对 4KiB 页必然放大；与 xv6 日志放大不同源。

## 4. 随 N 变化（EMUL，中位数）

| N | median_pmfs | median_xv6 | ratio×100 | wa_x100 | logical | phys |
|---|-------------|------------|-----------|---------|---------|------|
| 8 | 9206 | 7408714 | 80477 | 8432 | 340 | 28672 |
| 16 | 12168 | 16440967 | 135116 | 4344 | 660 | 28672 |
| 32 | 35830 | 33130730 | 92466 | 2205 | 1300 | 28672 |

### 观察

- xv6 随 N 近似线性（每文件元数据+日志）。
- PMFS 以一次 seal 摊销提交；ratio 通常仍 ≫1。
- 高 wa_x100 由页对齐主导，属预期。

## 5. MAP vs EMUL（N=32）

| mode | median_pmfs | median_xv6 | ratio×100 | wa_x100 |
|------|-------------|------------|-----------|---------|
| EMUL | 35830 | 33130730 | 92466 | 2205 |
| MAP | 26477 | 23651219 | 89327 | 2205 |

若 EMUL/MAP 同量级，瓶颈在算法/提交而非假缓冲。

## 6. 论文图

| 图 | 路径 |
|----|------|
| N–延迟 | `docs/figures/fig-n-latency.svg` |
| WA vs N | `docs/figures/fig-wa-vs-n.svg` |
| PMEM2 串口 | `docs/figures/fig-pmem2-serial.md` |

## 7. 答辩一句话

> 量级优势来自 seal 对小文件+sync 的架构差；不可替代点是教学 OS 上可机器判定的崩溃/掉电契约套件。

踩坑与威胁：`docs/PITFALLS.md`。

