# 自有贡献与创新尖刺

## 尖刺（唯一不可替代点）

**在教学操作系统 xv6 上，构建可机器判定的崩溃与掉电契约套件。**

- 产物：`ONBOX_PMFS_*` 阶段码表 + `make contract` / `contract-pmem` / `contract-pmem2`
- 一句话：正确性与掉电语义 **grep 一行即可判过**，可 CI 复现。
- 相对「又一个双槽 PMEM-KV」：方法学与验收资产，而非仅数据结构复现。

摘要末句、答辩开场均用此尖刺；C2/C3 是支撑尖刺的系统与实验柱。

---

## C1. 契约化崩溃与验收门（尖刺主体 · 工程/方法）

- **做了什么：** `docs/CONTRACT.md`；EMUL/MAP/用户态全阶段串口契约；CI 挂接。
- **前序关系：** cxc-os 有 `ONBOX_*` 手法；本课题 **重写** 为 PMFS 阶段/失败码，服务存储语义。
- **证据：** `make contract` → `ONBOX_PMFS_PASS`。

## C2. xv6 内双槽存储核 + 可观测 B3（系统柱）

- **做了什么：** 双槽 SB、`seal`、Bloom/fence、真页重写 B3、`pmfs_stat` WA、索引自 run 重建。
- **前序关系：** 设计约束来自作者原生 **FCS**；实现为 xv6 C **自研重写**（见 `PRIOR-ART-OWN.md`）。
- **证据：** `CRASH_PASS` + `ALGO_PASS` + `wa_x100`。

## C3. PMEM2 文件后端掉电一行过（实验柱 · 尖刺延伸）

- **做了什么：** `memory-backend-file` 杀启；耐久魔数；`ONBOX_PMFS_PMEM2_PASS`。
- **定位：** 尖刺在「可机器判定」上的掉电实例化，不是另起炉灶的第三套故事。
- **证据：** `make contract-pmem2` → `PMEM2_CONTRACT_PASS boots=2`；图 `docs/figures/fig-pmem2-serial.md`。

---

## 相关工作（防「移植题」）

| 系统 | 关系 |
|------|------|
| 通用 PMEM FS（PMFS/NOVA） | 动机参考；本文 KV 子集 + 教学 OS |
| **作者 FCS / Aurora** | **前序原生**；约束下沉，代码不入库 |
| xv6 log+virtio | 对照基线与根 FS |
| 本文 | **尖刺 = 契约套件**；C2/C3 支撑 |

## 非贡献（主动瘦身）

- 完整内核 VFS；真 NVDIMM 主线化；并入 FCS/Aurora 源码树。
