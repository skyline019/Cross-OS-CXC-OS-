# 前序约束下沉说明（非第三方移植清单）

FCS、Aurora 为**作者原生前序项目**；本课题把其中设计约束下沉到 xv6，并**自研重写**全部内核代码。  
口径与关系图见 `docs/PRIOR-ART-OWN.md`。尖刺见 `docs/CONTRIBUTIONS.md`。

本仓库**不**把前序源码树拷进 git。

## 1. 来自作者 FCS 的约束 → xv6 落点

| FCS 约束 | 本仓库落点 |
|---|---|
| 页设备 + sync | `kernel/pmdev.*`；EMUL≈RamDev |
| 双槽 SB + commit word + 校验 | `pmfs_sb` / `pmfs_commit` |
| 只承诺完好 SB 提交点 | `CRASH` 自测 + 契约 |
| mem→单 run、Bloom/fence、B1–B3、WA | `pmalgo.*`（C 重写） |

**刻意不带入：** 异步 IO 线程、分层归并 DP 全量、溢出大值链。

## 2. 来自作者 Aurora 的约束 → xv6 落点

| Aurora 约束 | 本仓库落点 |
|---|---|
| 键量化 / 粗桶 | `pm_key_coarse` |
| 二值 posting | `pm_posting` |
| 查询 LRU | `pmindex` qcache |

详见 `docs/AURORA-INDEX.md`。

## 3. 来自 cxc-os 的工程手法

| 手法 | 落点 |
|---|---|
| 可脚本化契约 | `ONBOX_PMFS_*`（**重写**码表，非原样拷贝） |
| 持久与易失分离 | PMEM 窗 vs `kalloc` |

**不带入：** Mini-IDE / GUI / UEFI。

## 4. 差异声明（答辩用）

- 前序：用户态/其他内核场景；本课题：**xv6-riscv 教学内核内**。  
- 新增尖刺：可机器判定的崩溃/掉电契约套件（前序未提供）。  
- 失败史：`docs/PITFALLS.md`。
