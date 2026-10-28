# 毕业论文大纲（PMFS v3）

> 按现状重写正文时用。旧「仅 KV / 薄 shim」大纲作废。  
> 摘要与贡献口径见 [THESIS.md](../THESIS.md)、[CONTRIBUTIONS.md](CONTRIBUTIONS.md)。  
> **旧 `thesis/latex/main.pdf` 不可直接提交。**

## 摘要要点

- 问题：块日志路径 vs 字节寻址提交；教学 OS 上如何做**可复现**验收。  
- 方法：并行 `/pm` 受限 FS；COW 元数据 + 不可变 run；双槽 checkpoint；QEMU 进程重启 oracle / 有界模型。  
- 结果：功能与恢复证据（results）；对齐工作量延迟/吞吐（持久语义不等价）；软件消融。  
- 边界：非完整 POSIX；MAP≠NVDIMM；touch+fence / 可选 Zicbom；杀进程≠物理掉电。

关键词建议：持久内存；NVDIMM 仿真；文件系统；崩溃一致性；写放大；xv6；契约测试；COW。

## 1. 绪论

- 题面四要件与产品形态（并行 `/pm`，不换根）  
- 三个问题：形态、一致性模型、验收方法  
- 贡献候选 C*（方法学验收为主，不声称算法首创）  
- 主动非目标列表  

## 2. 相关工作

素材：[RELATED-WORK.md](RELATED-WORK.md)、[PRIOR-ART-OWN.md](PRIOR-ART-OWN.md)、[BORROWINGS.md](BORROWINGS.md)。  
须补充：PMFS/NOVA/BPFS；CrashMonkey / PMTest 等崩溃测试工作；写清继承与差异。

## 3. 设计

- 地址与后端：EMUL / MAP / 共享 RAM / 补丁 NVDIMM / Zicbom — [QEMU-NVDIMM.md](QEMU-NVDIMM.md)  
- 分层与布局 — [ARCHITECTURE.md](ARCHITECTURE.md)  
- 崩溃可见点与 POSIX 差集 — [SEMANTICS.md](SEMANTICS.md)  
- CDC/stream、索引、维护（页复用/压缩）— [MAINTENANCE.md](MAINTENANCE.md)  

## 4. 实现

- 模块地图与调用链 — [CORE-WALKTHROUGH.md](CORE-WALKTHROUGH.md)  
- VFS `/pm`、硬链接、rename  
- checkpoint 发布与恢复  
- 踩坑 — [PITFALLS.md](PITFALLS.md)  

## 5. 实验与分析

写法指引：[EXPERIMENT.md](EXPERIMENT.md)。  
证据目录：

| 主题 | 路径 |
|------|------|
| 修复与全量回归 | [results/2026-10-28-v3](results/2026-10-28-v3/) |
| VFS | [results/2026-10-28-vfs](results/2026-10-28-vfs/README.md) |
| 维护/GC | [results/2026-10-28-gc](results/2026-10-28-gc/README.md) |
| 效率消融 | [results/2026-10-28-efficiency](results/2026-10-28-efficiency/README.md) |
| 性能 | [results/2026-10-28-perf](results/2026-10-28-perf/README.md) |
| 轨迹 | [results/2026-10-28-trace](results/2026-10-28-trace/README.md) |

**禁止**把 [BENCH.md](BENCH.md) / [ABLATION.md](ABLATION.md) 历史表当新结论。

## 6. 结论与展望

- 方法学：oracle + 假设清单  
- 系统：受限但可测的 `/pm`  
- 不足：真机校准、完整 POSIX、论文重写、近满 GC  
- 展望：与导师确认设备模型；轨迹接入状态空间；正文重编  

## 附录

- NVDIMM 路线：[APPENDIX-NVDIMM.md](APPENDIX-NVDIMM.md)  
- 复现命令：[CONTRACT.md](CONTRACT.md)、[PMFS-REPAIR-REPORT.md](PMFS-REPAIR-REPORT.md)  
