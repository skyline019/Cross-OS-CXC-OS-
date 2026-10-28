# 旧实施切片 ↔ 原型契约

> 以下是旧 smoke 阶段，不表示题面要求已完成。当前 12 周计划与强验收门见[优秀毕设路线图](EXCELLENCE-ROADMAP.md)。

| ID | 内容 | 判据 |
|---|---|---|
| M0–M4 | 契约 / MAP / CRASH / syscall / bench | `CONTRACT.md` |
| M1b | PMEM2 进程重启魔数（非掉电） | `contract-pmem2` |
| M5 | CDC/stream 融合 + 消融效应量 | `ABLATION.md` |
| M6 | Remount 字节等价 + 故障注入 | `SEMANTICS.md` / `REMOUNT`/`FAULT` |
| 深度 | 历史 N 扫参 + MAP/EMUL | 旧表 `BENCH.md`；新 `make bench-sweep` 只打印原始输出 |
| 贡献 | C1–C3 口答 | `CONTRIBUTIONS.md` |
| 加分 | 深化 POSIX 子集 / NVDIMM 附录 / CI | `pmfs_posix` · `APPENDIX-NVDIMM` |

题面：字节寻址 **文件子系统** + KV 核 + **契约尖刺**；完整 VFS/真 NVDIMM 不升主线。  
语义边界：`docs/SEMANTICS.md`。图与踩坑：`docs/figures/`、`docs/PITFALLS.md`。
