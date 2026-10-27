# 实施切片 ↔ 契约（PMFS-KV）

| ID | 内容 | 判据 |
|---|---|---|
| M0–M4 | 契约 / MAP / CRASH / syscall / bench | `CONTRACT.md` |
| M1b | PMEM2 掉电 | `contract-pmem2` |
| 深度 | N 扫参 + MAP/EMUL + 实验分析 | `make bench-sweep` → `BENCH.md` / `EXPERIMENT.md` |
| 贡献 | C1–C3 口答 | `CONTRIBUTIONS.md` |
| 加分 | 薄 POSIX shim / NVDIMM 附录 / usertests CI | `pmfs_posix` · `APPENDIX-NVDIMM` · workflow |

题面：字节寻址 KV + **契约尖刺**；FCS/Aurora=作者前序原生；完整 VFS/真 NVDIMM 不升主线。  
图与踩坑：`docs/figures/`、`docs/PITFALLS.md`。
