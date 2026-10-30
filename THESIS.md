# 毕设课题：xv6 上的持久内存文件系统

> **当前状态：v3 原型具备受限 VFS、硬链接、双槽保护的元数据与 run 页复用、显式 COW 压缩，以及测试构建的实际操作轨迹重放；仍未达到完整 VFS 或顶尖毕设的全部验收。** 精确语义见[语义](docs/SEMANTICS.md)，历史结果见[维护验证](docs/results/2026-10-28-gc/README.md)和[上一轮效率记录](docs/results/2026-10-28-efficiency/README.md)；早期基线与计划保留在[路线图](docs/EXCELLENCE-ROADMAP.md)。

## 原题目标

在 xv6 内核设计原生支持持久内存的文件系统：映射 QEMU 模拟的 NVDIMM、用字节寻址组织数据与元数据、通过缓存行刷写保证崩溃一致性，并与 xv6 原生 FS 比较延迟及吞吐。当前 `/pm` 是保留原根文件系统的受限子集；应与导师确认这一产品形态是否满足题意。

## 已实现的软件工作

- v3 以可校验的不可变 KV run 和 COW inode/dentry 为对象图，双槽 checkpoint 发布根；恢复重建索引，普通启动不运行破坏性自测。空白、v2、不兼容或损坏镜像失败关闭；显式 `pmformat --force` 才会格式化。v2 与 v3 不兼容，**没有迁移工具**。
- 保留原生根，新增逐组件 VFS 路径遍历及固定 `/pm` 挂载点，支持 cwd、`getdents`/`ls`、`lseek`、`O_APPEND`、PM 内子树 `rename`。独立打开同一 PM inode 的工作内容一致且偏移独立；`dup`/`fork` 共用打开对象偏移。仍有全局串行锁与严格资源上限，不是完整 VFS/POSIX。
- 同后端普通文件支持硬链接、共享 inode 和原子 link-count 更新；被打开文件的非最后链接可以删除，但最后链接的 unlink-open 仍不支持。双槽可达性扫描保护两代根与 run，不再被两槽引用的页可用于元数据或新 run；显式全链 COW 压缩保留最新公开 KV 和活文件 chunk，缺少不相交的连续暂存空间或存在活动 stream 时拒绝。它不是全设备 GC 或硬件磨损均衡。
- 为 QEMU 9.2.0 RISC-V `virt` 提供实验性补丁：冷插拔一个独立 `nvdimm`，以私有设备树节点告知 xv6 物理地址；QMP 检查设备类型、地址和独立后端。未修改的上游 QEMU 普通 MAP 仍不是 NVDIMM。
- 多次 QEMU 进程重启和软件故障切点 oracle 验证文件、KV 与目录子树；除独立的有界协议模型外，测试构建还记录实际 PMFS 写范围、flush 请求和 fence，并在假定持久域下重放部分写回和错误顺序负控。它不是逐机器指令跟踪，也不是物理掉电或全部内核路径的形式化证明。
- 保留 KV、CDC、stream 与 Bloom/fence；普通文件用有界 CDC/固定分块自适应。非替换 rename 与内容更新复用未改变的元数据根，相同覆盖写可避免无效提交；KV 使用有界精确索引并在超限时回退。提供 `pmstats` 软件读写/查询请求计数和[消融对照](docs/results/2026-10-28-efficiency/README.md)，不声称物理磨损改善或通用性能优势。
- 文件基准两侧统一普通 API、读回内容/EOF、成功操作数和路径深度；跨 QEMU 独立进程保存原始 ticks、短批吞吐、输入哈希和按启动分组的统计，另有同 PMFS 同步契约的提示消融；见[原始结果](docs/results/2026-10-28-perf/README.md)。原生 xv6 的 `fsync` 是全局日志同步且未协商设备 flush，持久语义仍不等价，不能据此声称真实持久器件性能优势。

## 尚未完成或未证实

1. **实验性 NVDIMM 的限制：** 独立设备只在本仓 QEMU 9.2.0 补丁、固定单设备布局和私有 FDT 描述下工作，不是上游通用 `virt` 支持，也没有 ACPI/NFIT、热插拔或通用设备发现；普通 MAP/整机共享 RAM 仍不是 NVDIMM。导师是否接受该设备模型需确认。
2. **物理持久域：** 默认 `pmem_flush` 是遍历与屏障，不是缓存写回。可选 Zicbom 路径发出 `CBO.CLEAN`，假定 64 B 缓存块及正确权限；QEMU TCG 不模拟缓存层级，不能证明硬件断电持久性。必须在实际平台确认持久域与原子性。
3. **更强验证和功能：** 实际轨迹重放是单 workload 的源级范围事件与采样持久模型，不是指令级或全部路径穷举；校验和碰撞、任意硬件故障、无限并发及完整 POSIX 均不在证明范围。动态挂载、打开后删除最后链接、跨后端及完整原生 rename、符号链接和权限等未实现；压缩没有近满盘预留或增量机制，长期写入仍可耗尽容量。
4. **正式论文定稿：** `thesis/latex` 正文已按 v3 证据重写并生成 PDF（含 matched 性能表与诚实限制句）；仍须更换学校封面模板、作者信息，并由导师审阅措辞。历史不等量基准与旧图不得混用。

## 文档入口

**总索引：** [docs/README.md](docs/README.md)（按角色导航 + 全量清单）。

| 文档 | 内容 |
|---|---|
| [题面对齐与收敛设计](docs/THESIS-OPTIMIZED-DESIGN.md) | 保留 KV/CDC 的 v3 单次提交与 v4 字节记录方案；明确为待实现设计 |
| [无 KV/CDC 的直接 PMEM 设计](docs/THESIS-DIRECT-PMEM-DESIGN.md) | 条件性的独立方案：直接索引、COW 双根和可重建回收；尚未实现 |
| [QEMU NVDIMM 与 NVMe 架构更新](docs/THESIS-QEMU-NVDIMM-NVME-ARCHITECTURE.md) | 当前模拟验收、原生 NVMe 对照、无 KV/CDC 的窗口/链群设计和可复验计划 |
| [修复与验证报告](docs/PMFS-REPAIR-REPORT.md) | 实际代码、测试命令、通过结果和残余风险 |
| [语义与限制](docs/SEMANTICS.md) | API、持久边界、失败行为及不支持项 |
| [架构](docs/ARCHITECTURE.md) / [核心代码直讲](docs/CORE-WALKTHROUGH.md) | 分层、checkpoint、调用链 |
| [软件读写放大与耐久](docs/PERFORMANCE-ENDURANCE.md)、[维护设计](docs/MAINTENANCE.md) | CDC/索引、双槽保护与 COW 压缩、软件请求计数及硬件磨损限制 |
| [QEMU 后端](docs/QEMU-NVDIMM.md) | 普通 RAM、共享文件、Zicbom 与真正 NVDIMM 的区别 |
| [契约与故障矩阵](docs/CONTRACT.md) | 自测、真实进程重启 oracle、协议模型的覆盖与限制 |
| [里程碑](docs/MILESTONES.md) / [论文大纲](docs/THESIS-OUTLINE.md) / [实验写法](docs/EXPERIMENT.md) / [答辩提纲](docs/DEFENSE.md) | 验收门、章节、答辩 |
| [结果索引](docs/results/README.md) | 2026-10-28 各轮原始证据 |
| [旧基准](docs/BENCH.md) | 历史数字，只留档，不能用作性能结论 |
| [基线审计和计划](docs/EXCELLENCE-ROADMAP.md)、[基线沙箱记录](docs/LOCAL-SANDBOX-TESTS.md) | 修复前的证据和规划，不应误读为当前状态 |
| [`thesis/latex/`](thesis/latex/README.md) | v3 正文 PDF；定稿前换学校模板 |
