# 实施切片

| ID | 内容 | 完成判据 |
|---|---|---|
| M0 | 独立分支 `thesis/pmem-fs`、文档、`pmem`/`pmfs` 桩、原版仍可启动 | 本文档 + `THESIS.md`；串口见 `pmem:` / `pmfs:` 行 |
| M1 | QEMU 扩内存；`kvmmap` PMEM；`pmem_persist`；selftest 掉电读回 | `pmem_ready()==1`；重启魔数仍在 |
| M2 | 双槽 SB + inode/数据布局；create/read/write 不经 bio | 用户态探针程序 PASS |
| M3 | 恢复路径 + 故意崩溃用例 | 半写 SB 丢弃；完好 epoch 可挂 |
| M4 | vs log+virtio 延迟/吞吐表 | `docs/BENCH.md` 数字可复现 |

当前：**M0 + 存储核 + 算法骨架** — `STORAGE_SELFTEST` / `ALGO_SELFTEST`（EMUL）。下一步：真 PMEM 窗口、完整 B3 压实、用户态探针。
