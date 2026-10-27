# Aurora Files 高速索引 → PMFS

来源：`e:\Agent\apple-style-windows-file-manager`（`electron/index/`、`shared/rbTree`、`pathSkip`、`segmentMap`）。

## 1. 高速索引（直接可迁）

| Aurora | 作用 | PMFS |
|---|---|---|
| `RbTree.rangeByPrefix` | 有序前缀区间扫描 | `pmindex` 有序键表 + 二分 lower_bound |
| `PathSkipList` | 多层跳跃 O(log n) 前缀 | 内核用**量化桶 + 有序表**近似（无堆分配跳表节点） |
| `SegmentInvertedMap` | 路径段倒排交集 | **量化桶倒排**：bucket → (run,page) 列表 |
| `QueryResultCache` LRU | 热查询二次命中 | `pm_qcache` 小容量 LRU |
| FTS5 侧车 | 大库多词 | 延后；PMEM 上可做 token 位图 |
| USN / watch 增量 | 脏目录刷新 | 对应：seal 时增量 upsert 索引 |

## 2. 「二值化」在本课题的含义

Aurora 本身不是深度学习二值网；其对 PMFS 有用的是 **bit 级存在性结构**：

| 概念 | 落地 |
|---|---|
| 存在/不存在二值判定 | FCS Bloom（已有）+ 桶级 **bitset**（`pmindex` posting bits） |
| 倒排集合交集 | 桶 bit AND → 候选 run/page，再精查 |
| 减少指针追逐 | 固定位图，适合 PMEM 顺序刷写 |

## 3. 「量化」在本课题的含义

| Aurora 启发 | PMFS 量化 |
|---|---|
| 缩略 `maxEdge=512`（分辨率量化） | 键空间 **MSB 量化**：`bucket = key >> QBITS` |
| 搜索 score 分档（前缀/包含/路径） | 读路径优先级：mem → qcache → 桶倒排 → bloom → fence → page |
| 段倒排用「段」作粗粒度 | 量化桶 = 粗粒度键区间（类似 fence 之上再加一层） |

**效果：** 先廉价排除整桶，再进 Bloom/页解码 —— 与 Aurora「先倒排再精排」同构。

## 4. 与 FCS 算法叠合

```
put → mem
seal → 不可变 run（FCS WA）
     → pmindex_upsert（量化桶 + 二值 posting + 有序键）
get  → qcache → 桶过滤 → bloom → fences → page（FCS 高速读）
maintain → B1/B2（抗碎片）；索引随 cell 视图更新（增量）
```

代码：`kernel/pmindex.h/.c`。
