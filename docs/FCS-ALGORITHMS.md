# FCS 创新算法 → PMFS（CPU 耐久 / 写放大 / 高速读 / 抗碎片）

课题要学的不是「再实现一个块设备 FS」，而是把 FCS 的**算法内核**迁到 PMEM 上。

## 1. CPU 侧耐久（低 CPU 的掉电安全）

| FCS / 存储常识 | PMFS 做法 |
|---|---|
| 文件设备每提交 2×`fdatasync`，CPU 堵在 syscall | PMEM：`pmem_persist` = 刷缓存行 + `fence`，无块层排队 |
| 提交点清晰：数据先落 → SB 翻转 | 已有 `pmfs_commit`；算法层只在 run 写完后调 commit |
| 避免整页 COW 重写（CPU memcpy 巨头） | **不可变 run**：覆盖=追加新 run，旧页不动 |

**命题：** 耐久成本应从「设备同步 + 日志重写」降为「对脏缓存行的 CPU 存储指令」。

## 2. 防止写放大（Write Amplification）

FCS 写路径一句话：`mem → 每提交 1 个有序 run（顺序写）→ 清单/SB`。

| 机制 | 效果 | PMFS 符号 |
|---|---|---|
| 追加不可变 run，无 B+ 叶 COW | WA ≈ 1 + 头开销 + 压实摊销 | `pm_run_seal` |
| 单元分裂/合并**零数据重写**（只改目录视图） | 平衡不触发页重写 | `pm_cell_split` |
| B3：压实预算 = 垃圾速率自适应 + 0/1 背包 | 不盲目全量 GC | `pm_wa.budget` / `pm_maintain` |
| `FCS_GC_RATIO`：dead≥ratio×live 才重写 | 控制压实触发 | `PM_GC_RATIO` |
| 计量 `logical_bytes` vs `phys_bytes` | 论文对比表口径 | `struct pm_wa` |

## 3. 高速读

| 机制 | 效果 | PMFS |
|---|---|---|
| 目录二分 O(log C) | 定位单元 | `pm_cell` 有序表（后续） |
| 段从新到旧，Bloom 跳过 | 少打开旧 run | `pm_bloom_maybe` |
| `fences[]` 每页首键 | 页级跳跃，不扫全 run | `pm_run.fences` |
| 命中即停 | 通常 1 页 | `pm_get` |

## 4. 抗碎片化布局（分形：Cell / Run / Page）

FCS 命题：**树只是平衡的一种形态；直接维护 B1/B2/B3，树消失。**

```
store ── Cell（键区间 + 段视图，不拥有字节）
  run ── 不可变有序页序列 + fences/bloom
 page ── 有序记录
```

| 不变量 | 含义 | 抗碎片作用 |
|---|---|---|
| **B1** 单元物理字节 ≤ BMAX | 过大则在页边界切开 | 防止大单元拖慢维护 |
| **B2** 单元段数 ≤ SMAX | 过多则压实成一段 | 限制读放大与段碎片 |
| **B3** 每提交重写预算自适应 | DP 选净收益压实子集 | 碎片清理有界、不抖 |

**关键创新（必须写进毕设）：**  
Cell **不拥有数据** → split/merge 只改「(run, 页区间)」视图 → **O(#段) 元数据，零 payload 重写**。这是相对 xv6 原地块更新 / 传统 B+ COW 的架构优势来源之一。

## 5. 与 xv6 原版 FS 的对比轴（评测用）

| 轴 | xv6 log+virtio | PMFS+FCS 算法 |
|---|---|---|
| 耐久 CPU | 日志块 + 磁盘 sync | 缓存行 persist |
| 写放大 | 日志至少 2× 块写 | 追加 run + 有界压实 |
| 读 | 按块 bio | Bloom+fence → 常 1 页 |
| 碎片 | inode/间接块更新局部写 | B1/B2 视图平衡 |

## 6. 代码落点

| 文件 | 内容 |
|---|---|
| `kernel/pmalgo.h/.c` | Run/Seg/Cell、Bloom、WA、B1/B2 骨架 |
| `docs/FCS-STORAGE.md` | 双槽提交协议 |
| `docs/FCS-ALGORITHMS.md` | 本文 |
