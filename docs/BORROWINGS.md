# 借鉴说明：FCS 与 CXC/OS

本课题**不**把 FCS 或 cxc-os 代码整树拷进 xv6；只移植**可验证的设计约束**与**工程手法**。

## 1. 来自 `e:\FCS`（尤其讲义掉电章 + embdb Soft 提交语义）

| 思想 | 落点 |
|---|---|
| 「只承诺完好超级块指向的提交点」 | `THESIS.md` 掉电承诺；`pmfs` 恢复只认校验通过且 epoch 最大的槽 |
| 双槽超级块，半写不怕 | slot A/B；写新槽时旧槽仍可挂载 |
| 未 commit 的更新掉电即无 | 无「偷偷」在 SB 翻转前发布目录 |
| 提交边界清晰（数据 → 清单 → SB） | PMFS：数据/元数据 flush → 写 SB → fence |
| 对比测试要可复现 | 后续 `bench/`：固定负载 + 原版 fs vs PMFS |

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
