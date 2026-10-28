# 实验章初稿（历史草稿）

> 本页含未经充分验证的 NVDIMM、掉电及性能结论，不能直接用于论文。设备、故障模型和公平基准的现状与重做计划见[优秀毕设路线图](EXCELLENCE-ROADMAP.md)。

## 5.1 环境

| 项 | 配置 |
|----|------|
| 基线 | xv6-riscv，`thesis/pmem-fs` |
| QEMU | `virt`，`CPUS=1` |
| EMUL / MAP / PMEM2 | 见 `QEMU-NVDIMM.md` |
| 回归 | `usertests` / `crash`（CI） |

## 5.2 功能、崩溃与掉电（尖刺证据）

| 用例 | 命令 | 期望 | 图 |
|------|------|------|----|
| 总闸 | `make contract` | `ONBOX_PMFS_PASS` | — |
| MAP | `make contract-pmem` | `mode=MAP`+`PMEM1` | — |
| 掉电 | `make contract-pmem2` | `PMEM2_PASS` | `figures/fig-pmem2-serial.md` |
| CRASH | 内嵌自测 | `CRASH_PASS` | — |
| POSIX shim | `pmfs_posix` | `POSIX_PASS`（不升主线） | — |

**分析：** 契约套件使「完好 SB / 未提交不可见 / 窗口 persist」可机器判定；相对仅演示双槽 UI，这是方法学差异。

## 5.3 性能

数据：`docs/BENCH.md`（`make bench-sweep`）。

| 图 | 文件 |
|----|------|
| N–延迟 | `figures/fig-n-latency.svg` |
| WA vs N | `figures/fig-wa-vs-n.svg` |

### 为何不对等 / WA / EMUL 边界

见 `BENCH.md` §1–3。结论写**量级与边界**，不写「POSIX 文件快 1200×」。

### MAP vs EMUL

同量级 → 瓶颈在软件提交路径，假缓冲未主导结果。

## 5.4 实现难点与失败史

完整叙述：`docs/PITFALLS.md`（P0 stack0、P1 rdcycle、P2 NUMA、…）。

## 5.5 局限

1. QEMU 非真 NVDIMM（附录不喧宾夺主）。  
2. 对比 API 异构。  
3. 薄 POSIX 仅为 hash→KV shim。

## 5.6 小结

尖刺（契约套件）+ 支撑柱（存储核 / PMEM2）+ 可复核踩坑，构成顶批叙事主线。
