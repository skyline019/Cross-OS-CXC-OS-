# 实验结果归档索引（2026-10-28）

> 论文与答辩**只引用本目录下带 README 的轮次**；`docs/BENCH.md` / `docs/ABLATION.md` 历史表作废为结论源。  
> 总文档索引：[../README.md](../README.md)。

| 目录 | 主题 | 论文用途 |
|------|------|----------|
| [2026-10-28-v3/](2026-10-28-v3/) | 修复后契约/recovery/usertests/bench/model | 正确性总表 |
| [2026-10-28-vfs/](2026-10-28-vfs/README.md) | `/pm`、硬链接、rename | 功能章 |
| [2026-10-28-gc/](2026-10-28-gc/README.md) | 页复用、压缩、空间 | 维护章 |
| [2026-10-28-efficiency/](2026-10-28-efficiency/README.md) | CDC/写跳过/索引/软件计数 | 公平面消融 |
| [2026-10-28-perf/](2026-10-28-perf/README.md) | 对齐工作量延迟/吞吐 | **架构面主表** |
| [2026-10-28-trace/](2026-10-28-trace/README.md) | 轨迹重放 | 方法学附录 |
| [2026-10-28-run-reuse/](2026-10-28-run-reuse/README.md) | run 页复用 | 维护补充 |
| [2026-10-28-sandbox/](2026-10-28-sandbox/) | 早期沙箱 | 失败史 |
| [2026-10-28-xv6-ci/](2026-10-28-xv6-ci/) | 原生日志 CI | 对照 |

**引用纪律：** 写清后端（EMUL/MAP/NVDIMM）、是否 Zicbom、持久契约是否等价；禁止把 EMUL matched bench 写成真机 PMEM 加速。
