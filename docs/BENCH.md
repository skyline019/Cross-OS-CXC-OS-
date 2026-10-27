# PMFS 文件子系统 vs xv6 fs 对比评测（合题深度版）

## 1. 方法与为何「不对等」

本实验对比三条路径，目的是量级说明而非绝对公平微基准：

| 维度 | PMFS-KV | PMFS-File | xv6 `fs+log` |
|------|---------|-----------|--------------|
| API | `put`×N + 1×`seal` + `get`×N | `create/write/fsync`×N | `open/write`×N + `sync` |
| 持久语义 | 双槽 SB | inode 页 + SB | 块日志检查点 |
| I/O 栈 | 字节寻址 / EMUL | 同左 | virtio-blk + bio + log |

**解读约束：** ratio≫1 主要反映「避免 virtio 日志与目录更新」的架构差。
吞吐 `thr_*` 为相对单位（`N×1e6/median_r_time`），不作墙钟 MB/s。

计时：`cycles()` → 内核 `r_time()`；每 N 做 5 trial 取**中位数**。

```bash
make bench-sweep
```

## 2. EMUL / NVDIMM 仿真边界

- 默认 EMUL：算法与契约快回归。
- MAP / PMEM2（`memory-backend-file`）= QEMU **NVDIMM 仿真**主路径。
- EMUL **不**代表真 NVDIMM 延迟。

## 3. 写放大 WA

`wa_x100 = phys*100/logical`（KV 路径）。小 value 对 4KiB 页必然放大。

## 4. 随 N 变化（EMUL，中位数）

| N | median_kv | median_file | median_xv6 | fratio×100 | thr_file | thr_xv6 | wa_x100 |
|---|-----------|-------------|------------|------------|----------|---------|---------|
| 8 | 33507 | 304451 | 5684939 | 1867 | 26 | 1 | 15384 |
| 16 | 38448 | 486211 | 11208338 | 2305 | 32 | 1 | 17889 |
| 32 | 53065 | 659056 | 25648605 | 3891 | 48 | 1 | 17721 |

### 观察

- xv6 随 N 近似线性；PMFS-File 每文件 fsync 仍避免块日志栈。
- `thr_file` / `thr_xv6` 给出吞吐相对量级（合题「延迟与吞吐」）。

## 5. MAP vs EMUL（N=32）

| mode | median_kv | median_file | median_xv6 | fratio×100 | thr_file |
|------|-----------|-------------|------------|------------|----------|
| EMUL | 53065 | 659056 | 25648605 | 3891 | 48 |
| MAP | 113804 | 4301198 | 24439543 | 568 | 7 |

若 EMUL/MAP 同量级，瓶颈在软件提交路径。

## 6. 两套结论（勿混读）

| 平面 | 证据 | 可说 | 不可说 |
|------|------|------|--------|
| 架构面 | `median_*` / `thr_*` vs xv6 | 去日志栈的量级差 | 同等 POSIX 千倍加速 |
| 公平面 | `ABLATION_* cycles/wa_x100` | 切分/刷写策略效应 | 未重扫前用旧表外推 |

CDC 融合后本表由 `make bench-sweep` 重填；消融效应量见 `docs/ABLATION.md`。

## 7. 答辩一句话

> 合题四要件均有落点；不可替代点是可机器判定的契约套件（含 remount 字节等价）。
> 架构面看量级，公平面看消融效应量。

