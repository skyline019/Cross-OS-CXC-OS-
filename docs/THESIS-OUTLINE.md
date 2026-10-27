# 毕业论文大纲（PMFS-KV：字节寻址键值存储）

## 摘要

见 `THESIS.md`。关键词：持久内存；键值存储；崩溃一致性；写放大；xv6。

## 1. 背景与问题

- xv6 `fs+log`+virtio 的延迟与写放大
- 字节寻址 PMEM 一致性模型
- 定位：并行 KV，非替换根 FS

## 2. 相关工作

素材表：`docs/RELATED-WORK.md`；借鉴边界：`BORROWINGS.md`。

## 3. 设计

- EMUL / MAP / PMEM2
- 双槽 SB 与崩溃契约
- mem→seal→B3；量化索引
- API：`pmfs_put/get/seal/stat`；薄 shim `pmfs_posix`

## 4. 实现

模块与契约：`ARCHITECTURE.md`、`CONTRACT.md`。  
贡献三条：`CONTRIBUTIONS.md`。

## 5. 实验

草稿：`docs/EXPERIMENT.md`。数据：`make bench-sweep` → `BENCH.md`。

## 6. 结论与展望

C1–C3；附录真 NVDIMM：`APPENDIX-NVDIMM.md`。
