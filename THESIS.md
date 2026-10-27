# 毕设课题：XV6 原生持久化内存文件系统（PMFS）

## 题目表述

在 XV6 操作系统内核中设计并实现一套**原生支持持久化内存（PMEM）**的文件系统。核心任务：

1. **改造内存管理**：映射 QEMU 模拟的 NVDIMM / 持久内存后端，使内核获得字节寻址的 PMEM 窗口。
2. **重构文件系统布局**：元数据与数据适配 PMEM 字节寻址，**消除传统块设备 + bio 缓冲层**作为主路径。
3. **轻量崩溃一致性**：基于缓存行刷写（`fence` / `cbo.flush` 或 x86 `clflushopt`+`sfence` 语义的可移植封装），保障掉电安全。
4. **QEMU 集成与对比评测**：相对 XV6 原生 `fs`+`log`+virtio-disk，验证延迟与吞吐的架构级优势。

## 代码基线

| 项 | 选择 |
|---|---|
| 基线内核 | [mit-pdos/xv6-riscv](https://github.com/mit-pdos/xv6-riscv) |
| 工作分支 | `thesis/pmem-fs`（本仓库独立课题分支） |
| 借鉴 | `e:\FCS`（掉电语义 / 双槽超级块）· `workspace-study/cxc-os`（QEMU 工程化、virtio、持久盘契约） |

## 里程碑（建议）

| 阶段 | 交付 | 验收 |
|---|---|---|
| M0 | 分支 + 架构文档 + QEMU PMEM 启动参数 | 文档齐；`make qemu` 仍可跑原版 |
| M1 | `pmem_*`：物理窗口、直映射、persist 原语 | 探针写-刷-读回；掉电文件仍在 |
| M2 | `pmfs_*`：超级块双槽、inode/目录/数据区布局 | 无 bio 主路径的 mount/create/read/write |
| M3 | 崩溃一致性：提交点 + 恢复 | kill -9 / QEMU quit 后回到完好提交点 |
| M4 | 对比 bench：vs xv6 logfs | 延迟/吞吐表 + 分析 |

## 掉电承诺（一句话）

**只承诺完好超级块所指向的最近一次提交点**；未过提交点的更新视为未发生（对齐 FCS 讲义）。

## 算法创新（学 FCS，落 PMEM）

详见 `docs/FCS-ALGORITHMS.md`：

1. **CPU 耐久**：缓存行 `persist` 替代块日志 + fsync；不可变 run 避免整页 COW。
2. **防写放大**：每提交 1 个有序 run；Cell 分裂零数据重写；B3 有界压实。
3. **高速读**：Bloom 跳过 + fence 页跳 + 新→旧命中即停。
4. **抗碎片**：B1/B2/B3 分形布局（Cell 不拥有字节）。
5. **高速索引**（Aurora Files）：有序前缀表 + 键量化桶 + 二值 posting + 查询 LRU（`docs/AURORA-INDEX.md`）。

## 非目标（本阶段）

- 不一次搬完 FCS 异步 IO / 分层归并 / 溢出链全套。
- 不替代用户态完整 POSIX；以 XV6 风格系统调用子集为准。
- 不把 cxc-os GUI/IDE 并入本课题。
