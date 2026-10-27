# 实施切片 ↔ 契约（PMFS 文件子系统）

| ID | 内容 | 判据 |
|---|---|---|
| M0–M4 | 契约 / MAP / CRASH / syscall / bench | `CONTRACT.md` |
| M1b | PMEM2 掉电 | `contract-pmem2` |
| M5 | CDC/stream 融合 + 消融效应量 | `ABLATION.md` |
| M6 | Remount 字节等价 + 故障注入 | `SEMANTICS.md` / `REMOUNT`/`FAULT` |
| 深度 | N 扫参 + MAP/EMUL + 实验分析 | `make bench-sweep` → `BENCH.md` |
| 贡献 | C1–C3 口答 | `CONTRIBUTIONS.md` |
| 加分 | 深化 POSIX 子集 / NVDIMM 附录 / CI | `pmfs_posix` · `APPENDIX-NVDIMM` |

题面：字节寻址 **文件子系统** + KV 核 + **契约尖刺**；完整 VFS/真 NVDIMM 不升主线。  
语义边界：`docs/SEMANTICS.md`。图与踩坑：`docs/figures/`、`docs/PITFALLS.md`。
