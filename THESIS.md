# 毕设课题：XV6 上面向持久内存的文件子系统（PMFS）

## 题目表述（合题原题四要件）

在 xv6-riscv 内核中设计并实现一套**原生支持持久化内存的文件子系统（PMFS）**，与 virtio 根文件系统**并行**（不替换 `/`）。核心任务：

1. **改造内存管理**：映射 QEMU 模拟的 NVDIMM 窗口 `[PMEMBASE,PMEMEND)`（`-DPMEM_MAP` / `memory-backend-file`）。
2. **重构元数据与数据布局**：定长 inode/dentry + 双槽超级块 + 不可变 run，**消除块设备 bio/log 路径**。
3. **轻量崩溃一致性**：缓存行粒度 `pmem_flush` + `fence` 有序协议（store→flush→fence→SB），掉电安全可契约验收。
4. **QEMU 集成与对比**：相对 xv6 原生 `fs+log`，报告**延迟中位数与相对吞吐**。

## 摘要

> 针对块设备日志路径延迟高、写放大显著的问题，本文在 xv6-riscv 上实现字节寻址 PMEM 文件子系统 PMFS：映射 QEMU NVDIMM 仿真窗口；以双槽超级块、inode/dentry 页与有序行级刷写保障崩溃一致性；KV run 引擎并行提供批量 seal 路径。系统保留 virtio 根文件系统，通过串口契约 `ONBOX_PMFS_*`（含 `PERSIST`/`FILE`/`PMEM2`）自动化验收，并与原生小文件写路径对比延迟与吞吐。**不可替代点：教学 OS 上可机器判定的崩溃与掉电契约套件。**

**诚实边界**：并行根上的**文件对象子集**（非完整 VFS 挂载替换）；真物理 NVDIMM 见附录。

## 创新尖刺（答辩 15 秒）

> **教学 OS 上可机器判定的崩溃/掉电契约套件**（`ONBOX_PMFS_*` + `contract-pmem2`）——相对「又一个双槽 PMEM-FS」，方法学与验收资产是差异化。

## 里程碑 ↔ 契约

| 阶段 | 交付 | 验收 |
|---|---|---|
| M0 | 契约 + EMUL | `ONBOX_PMFS_PASS`（含 PERSIST/FILE） |
| M1 | NVDIMM 仿真 MAP | `mode=MAP backend=map` + PMEM1 |
| M1b | 掉电二次启动 | `contract-pmem2` |
| M2 | 文件 API + KV syscall | FILE / SYSCALL |
| M3 | 崩溃语义 | CRASH |
| M4 | 延迟+吞吐 bench | BENCH（`thr_file`/`median_file`） |

## 文档地图

| 文档 | 用途 |
|------|------|
| `docs/CONTRIBUTIONS.md` | C1–C3 + 尖刺 |
| `docs/BENCH.md` / `docs/figures/` | 数据与图 |
| `docs/PITFALLS.md` | 踩坑 |
| `docs/QEMU-NVDIMM.md` | NVDIMM 仿真 |
| `thesis/latex/main.pdf` | 论文 PDF |
