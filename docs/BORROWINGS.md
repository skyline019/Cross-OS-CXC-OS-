# 借鉴说明：FCS 与 CXC/OS

本课题**不**把 FCS 或 cxc-os 代码整树拷进 xv6；只移植**可验证的设计约束**与**工程手法**。

## 1. 来自 `e:\FCS` 底层存储（已落到代码）

| FCS 构件 | 本仓库落点 |
|---|---|
| `BlockDev` / `RamDev`（页读写 + sync） | `kernel/pmdev.h/.c`；EMUL 缓冲 = RamDev 类比 |
| `PAGE=4096`、页号寻址 | `PMDEV_PAGE`；`pm_page_t` |
| 双槽 SB（页 0/1）、`COMMIT_WORD`、校验 | `struct pmfs_sb`；`PMFS_COMMIT_WD`；`pmfs_sb_chk` |
| `read_sb` / 选更高 `vers` | `pmfs_read_sb` + `pmfs_mount` |
| `sb_flush`：sync → 写交替槽 → sync；成功才翻槽 | `pmfs_write_sb_slot` + `pmfs_commit` |
| `tail` 页分配从 2 起 | `PMFS_DATA0`；`pmfs_alloc_pages` |
| 「只承诺完好 SB 指向的提交点」 | `pmfs_storage_selftest` 损坏非活动槽仍可挂载 |

参考（只读，不入库）：

- `e:\FCS\embdb\x\include\blockdev.h`、`layout.h`、`fcs.h`（`read_sb`/`write_sb`/`sb_flush`）
- `e:\FCS\backup\fcs-from-zero\latex\chapters\25-crash-walk.tex`

**已移植算法骨架（`kernel/pmalgo.*`）：** mem→单 run 提交、Bloom/fence 读路径、B1 零重写分裂、B2 段数上限、WA 计量。

**已移植 Aurora 索引（`kernel/pmindex.*`）：** 有序键二分（RB 前缀类比）、键 MSB 量化、二值 run posting、查询 LRU。见 `docs/AURORA-INDEX.md`。

**刻意延后：** 异步 IO 线程、分层归并 DP 全量、溢出大值链、清单链页、完整跳表节点堆。见 `docs/FCS-ALGORITHMS.md`。

参考路径（只读借鉴，不入库）：

- `e:\FCS\backup\fcs-from-zero\latex\chapters\25-crash-walk.tex`
- `e:\FCS\embdb\x\include\fcs.h` 导读中的提交/回退叙述
- `e:\FCS\embdb\x\README.md` 中 commit / fsync 相关评测习惯

## 2. 来自 `workspace-study/cxc-os`

| 思想 | 落点 |
|---|---|
| QEMU 脚本化、可重复契约 | `docs/QEMU-NVDIMM.md` + 后续 `tools/run_pmem_qemu.sh` |
| 持久后端与易失 RAM 分离 | CXCFS/`disk/` ↔ 本课题 PMEM 窗口 vs DRAM `kalloc` |
| virtio / 设备探测要有失败码与 stage | `pmem_init` / `pmfs_mount` 返回明确 failure，串口打印 PASS 行 |
| 内核侧直管存储、少层抽象 | PMFS 主路径不经 bio（对标 cxc 对块层的取舍讨论） |

**明确不借鉴：** Mini-IDE、GUI、UEFI 引导；那些属于 cxc-os GUIUX 线。

## 3. 差异声明

- FCS 是用户态存储引擎；本课题是**教学内核内**文件系统。
- cxc-os 是 UEFI/x86_64 自研内核；本课题基线是 **xv6-riscv**。
- PMEM 刷写指令在 RISC-V 上用 `fence` + 可用的 CMO/`cbo.flush`（视工具链/QEMU 而定）；语义对齐「缓存行对持久域可见」，实现可分阶段降级为「全内存 fence + 文件后端」。
