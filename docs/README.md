# PMFS 文档索引（v3）

> **权威入口：** 仓库根 [`THESIS.md`](../THESIS.md)。  
> 本文只做导航；具体语义、命令与限制以被链文档为准。  
> 旧草稿（`BENCH.md` 历史表、`ABLATION.md` 旧 A0–A4、`thesis/latex/main.pdf`）**不得**直接当作新性能/掉电结论。

## 1. 先读这五份

| 文档 | 用途 |
|------|------|
| [THESIS.md](../THESIS.md) | 课题状态总览、已实现/未完成、文档入口 |
| [SEMANTICS.md](SEMANTICS.md) | 支持的 API、可见点、失败语义、资源上限 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 分层、布局、checkpoint 发布顺序 |
| [CONTRACT.md](CONTRACT.md) | 自测 / 进程重启 oracle / 规格模型命令 |
| [PMFS-REPAIR-REPORT.md](PMFS-REPAIR-REPORT.md) | 修复内容、沙箱实测表、复现命令 |

## 2. 按读者角色

### 答辩 / 导师

1. [THESIS.md](../THESIS.md) → 一分钟状态  
2. [DEFENSE.md](DEFENSE.md) → 15–20 分钟答辩提纲  
3. [CONTRIBUTIONS.md](CONTRIBUTIONS.md) → 候选贡献与非声称  
4. [CORE-WALKTHROUGH.md](CORE-WALKTHROUGH.md) → 核心路径代码直讲  
5. [results/README.md](results/README.md) → 实验结果总索引  
6. [results/2026-10-28-perf/](results/2026-10-28-perf/README.md) → 对齐工作量性能  

### 复现实验

1. [CONTRACT.md](CONTRACT.md)  
2. [QEMU-NVDIMM.md](QEMU-NVDIMM.md)（含补丁 NVDIMM）  
3. [PMFS-REPAIR-REPORT.md](PMFS-REPAIR-REPORT.md) §3 命令  
4. 各 `docs/results/2026-10-28-*/README.md`  

### 改代码

1. [ARCHITECTURE.md](ARCHITECTURE.md) + [CORE-WALKTHROUGH.md](CORE-WALKTHROUGH.md)  
2. [MAINTENANCE.md](MAINTENANCE.md)（页复用 / COW 压缩）  
3. [SEMANTICS.md](SEMANTICS.md)（改行为前先改差集）  
4. [PITFALLS.md](PITFALLS.md)  

## 3. 全量清单

### 现状与语义

| 文档 | 说明 |
|------|------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | v3 架构与发布顺序 |
| [SEMANTICS.md](SEMANTICS.md) | 精确子集与边界 |
| [CORE-WALKTHROUGH.md](CORE-WALKTHROUGH.md) | 核心代码调用链讲解 |
| [MAINTENANCE.md](MAINTENANCE.md) | 双槽可达性、页复用、显式压缩 |
| [PERFORMANCE-ENDURANCE.md](PERFORMANCE-ENDURANCE.md) | 软件读写计数与磨损声称边界 |
| [TRACE-REPLAY.md](TRACE-REPLAY.md) | 源级写范围/flush/fence 轨迹重放 |

### 验收与实验

| 文档 | 说明 |
|------|------|
| [CONTRACT.md](CONTRACT.md) | 契约与故障矩阵 |
| [BENCH.md](BENCH.md) | **历史表留档** + 当前 bench-sweep 方法 |
| [ABLATION.md](ABLATION.md) | **历史** A0–A4；新消融见 results |
| [EXPERIMENT.md](EXPERIMENT.md) | 实验章写法指引（对齐现状） |
| [XV6-LOG-RECOVERY.md](XV6-LOG-RECOVERY.md) | 原生 xv6 日志恢复测试 |
| [QEMU-NVDIMM.md](QEMU-NVDIMM.md) | 后端：EMUL / MAP / 共享 RAM / 补丁 NVDIMM / Zicbom |
| [APPENDIX-NVDIMM.md](APPENDIX-NVDIMM.md) | 设备路线备忘 |

### 贡献、计划与历史

| 文档 | 说明 |
|------|------|
| [CONTRIBUTIONS.md](CONTRIBUTIONS.md) | 候选贡献与假设 |
| [DEFENSE.md](DEFENSE.md) | 答辩提纲 |
| [MILESTONES.md](MILESTONES.md) | 当前里程碑与验收门 |
| [THESIS-OUTLINE.md](THESIS-OUTLINE.md) | 论文大纲（v3） |
| [EXCELLENCE-ROADMAP.md](EXCELLENCE-ROADMAP.md) | 基线审计与 12 周计划（含历史） |
| [PMFS-REPAIR-REPORT.md](PMFS-REPAIR-REPORT.md) | v3 修复与验证报告 |
| [LOCAL-SANDBOX-TESTS.md](LOCAL-SANDBOX-TESTS.md) | 修复前沙箱失败史 |
| [PITFALLS.md](PITFALLS.md) | 可复核踩坑 |
| [RELATED-WORK.md](RELATED-WORK.md) | 相关工作对照素材 |
| [PRIOR-ART-OWN.md](PRIOR-ART-OWN.md) / [BORROWINGS.md](BORROWINGS.md) | 前序自有工作与借鉴边界 |
| [FCS-STORAGE.md](FCS-STORAGE.md) / [FCS-ALGORITHMS.md](FCS-ALGORITHMS.md) / [AURORA-INDEX.md](AURORA-INDEX.md) | 约束下沉笔记（非并入源码） |

### 结果归档（`docs/results/`）

总表：[results/README.md](results/README.md)。

| 目录 | 内容 |
|------|------|
| [2026-10-28-v3/](results/2026-10-28-v3/) | 修复后契约/recovery/usertests/bench/model |
| [2026-10-28-vfs/](results/2026-10-28-vfs/README.md) | VFS / 硬链接 |
| [2026-10-28-gc/](results/2026-10-28-gc/README.md) | 维护、页复用、压缩 |
| [2026-10-28-efficiency/](results/2026-10-28-efficiency/README.md) | CDC/索引/写跳过消融 |
| [2026-10-28-perf/](results/2026-10-28-perf/README.md) | 对齐工作量延迟/吞吐 |
| [2026-10-28-trace/](results/2026-10-28-trace/README.md) | 轨迹重放 |
| [2026-10-28-run-reuse/](results/2026-10-28-run-reuse/README.md) | run 页复用 |
| [2026-10-28-sandbox/](results/2026-10-28-sandbox/) | 早期沙箱 |
| [2026-10-28-xv6-ci/](results/2026-10-28-xv6-ci/) | 原生 CI 相关 |

### 论文

| 路径 | 说明 |
|------|------|
| [thesis/latex/README.md](../thesis/latex/README.md) | v3 正文已重写；仍须换学校封面模板后定稿 |
| [thesis/latex/BIB-AUDIT.md](../thesis/latex/BIB-AUDIT.md) | 文献审计 |

## 4. 常用命令（可丢弃 checkout）

```sh
make qemu                          # EMUL，每启动空 PMFS
make test-model
CPUS=3 make test-vfs
make test-recovery CPUS=3 RECOVERY_ARGS="--log-dir .ci-logs/recovery"
make contract-full                 # 破坏性旧自测
make bench-sweep                   # 对齐计数；打印原始 ticks
```

多数 `contract*` / recovery / bench 目标会 `make clean` 并重建 `fs.img`。详见根目录 `README` 与 [CONTRACT.md](CONTRACT.md)。

## 5. 版本与诚实边界（一句话）

- **格式：** PMFS v3（与 v2 不兼容，无迁移工具）。  
- **产品：** 并行于 virtio 根的受限 `/pm` 子集 + KV/CDC/stream，不是完整 POSIX/VFS。  
- **设备：** 未打补丁 MAP = 普通 guest RAM；实验性独立 NVDIMM 仅补丁 QEMU 9.2.0。  
- **持久：** 默认 touch+fence；可选 Zicbom `CBO.CLEAN`；进程杀 ≠ 物理掉电。  
