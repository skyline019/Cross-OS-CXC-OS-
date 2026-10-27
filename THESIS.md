# 毕设课题：XV6 上字节寻址 PMEM 键值存储（PMFS-KV）

## 题目表述（对齐实现）

在 xv6-riscv 内核中设计并实现一套**字节寻址的持久内存键值存储引擎（PMFS-KV）**，与原有 virtio 根文件系统**并行**，不替换 `/`。核心任务：

1. **PMEM 窗口**：映射 QEMU 扩容 RAM / `memory-backend-file`，获得 `[PMEMBASE, PMEMEND)` 字节寻址窗口；无窗口时 EMUL 回退。
2. **耐久存储核**：双槽超级块 + epoch + 校验；数据先写后 `commit`；**严格崩溃契约**可自动化验收。
3. **算法层**：不可变 run、Bloom/fence 快读、B1/B2/B3 抗碎片与有界压实；量化索引（作者前序 Aurora 约束）。
4. **用户接口**：`pmfs_put` / `pmfs_get` / `pmfs_seal` / `pmfs_stat`（KV 子集，非 POSIX inode FS）。
5. **对比评测**：相对 xv6 `fs+log`+virtio 小文件写路径，报告中位数延迟与写放大。

## 摘要（可直接用于开题/封面）

> 针对块设备日志路径延迟高、写放大显著的问题，本文在 xv6-riscv 上实现字节寻址 PMEM 键值存储 PMFS-KV：以双槽超级块与 `persist` 原语提供崩溃一致性，以不可变 run 与有界压实控制写放大，并以量化索引加速查询。系统保留 virtio 根文件系统，通过串口契约 `ONBOX_PMFS_*` 与文件后端掉电二次启动（PMEM2）自动化验收，并与原生小文件写路径对比评测。**本文相对已有 PMEM-KV 工作的不可替代点在于：在教学操作系统上构建可机器判定的崩溃与掉电契约套件，使正确性与掉电语义可一键复现，而非仅再实现一套双槽存储核。** 存储与索引的设计约束来自作者前序原生项目 FCS / Aurora，本仓库为 xv6 内自研 C 重写，不入库拷贝。

**非宣称**：本课题**不**实现完整 POSIX 文件/目录 inode 文件系统；薄 shim 与真 NVDIMM 仅作附录/演示，不冲淡 KV+契约主线。

## 创新尖刺（答辩 15 秒版）

> **教学 OS 上可机器判定的崩溃/掉电契约套件**（`ONBOX_PMFS_*` + `contract-pmem2`）——评委若问「和已有 PMEM-KV 差在哪」，先答这句，再展开 C2 存储核与 C3 方法细节。

## 代码基线与前序项目

| 项 | 选择 |
|---|---|
| 基线内核 | [mit-pdos/xv6-riscv](https://github.com/mit-pdos/xv6-riscv) |
| 工作分支 | `thesis/pmem-fs` |
| 作者前序原生 | `e:\FCS`（耐久存储约束）· Aurora（索引约束）· `cxc-os`（契约工程手法） |
| 本仓库 | xv6 内 **自研重写**；不把前序树拷入库 |

## 里程碑 ↔ 契约

| 阶段 | 交付 | 验收 |
|---|---|---|
| M0 | 契约文档 + EMUL 存储核 | `make contract` → `ONBOX_PMFS_PASS` |
| M1 | MAP 窗口 + persist | `PMEM0 mode=MAP` + `PMEM1` |
| M1b | 掉电二次启动 | `make contract-pmem2` → `PMEM2_PASS` |
| M2 | KV syscall | `SYSCALL_PASS` |
| M3 | 崩溃语义 | `CRASH_PASS` |
| M4 | 对比 bench | `BENCH_PASS` + 图 `docs/figures/` |

## 掉电承诺（一句话）

**只承诺完好超级块所指向的最近一次提交点**；未过提交点的更新视为未发生。PMEM 窗口探针魔数在 `memory-backend-file` 杀进程再启后仍可读（`PMEM2`）。

## 文档地图

| 文档 | 用途 |
|------|------|
| `docs/CONTRIBUTIONS.md` | C1–C3 + 尖刺 |
| `docs/EXPERIMENT.md` | 实验章 |
| `docs/BENCH.md` / `docs/figures/` | 数据与论文图 |
| `docs/PITFALLS.md` | 踩坑与失败叙述 |
| `docs/PRIOR-ART-OWN.md` | FCS/Aurora 作者前序说明 |
| `docs/APPENDIX-NVDIMM.md` | 真设备附录（不喧宾夺主） |

## 非目标（刻意不做大）

- 完整内核 VFS/inode FS。
- 把真 NVDIMM 升为主线。
- 把 FCS/Aurora 源码树并入 xv6。
