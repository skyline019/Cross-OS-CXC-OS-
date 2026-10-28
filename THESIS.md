# 毕设课题：xv6 上的持久内存文件系统

> **当前状态：已完成 v3 修复、受限 VFS 和一轮查询/软件写入优化，但离完整 VFS 和顶尖毕设的全部验收仍有差距。** 精确语义见[语义](docs/SEMANTICS.md)，最新结果与命令见[优化验证](docs/results/2026-10-28-efficiency/README.md)；早期基线与计划保留在[路线图](docs/EXCELLENCE-ROADMAP.md)。

## 原题目标

在 xv6 内核设计原生支持持久内存的文件系统：映射 QEMU 模拟的 NVDIMM、用字节寻址组织数据与元数据、通过缓存行刷写保证崩溃一致性，并与 xv6 原生 FS 比较延迟及吞吐。当前 `/pm` 是保留原根文件系统的受限子集；应与导师确认这一产品形态是否满足题意。

## 已实现的软件工作

- v3 以追加的、可校验的 KV run 和 COW inode/dentry 为对象图，双槽 checkpoint 发布根；恢复重建索引，普通启动不运行破坏性自测。空白、v2、不兼容或损坏镜像失败关闭；显式 `pmformat --force` 才会格式化。v2 与 v3 不兼容，**没有迁移工具**。
- 保留原生根，新增逐组件 VFS 路径遍历及固定 `/pm` 挂载点，支持 cwd、`getdents`/`ls`、`lseek`、`O_APPEND`、PM 内子树 `rename`。独立打开同一 PM inode 的工作内容一致且偏移独立；`dup`/`fork` 共用打开对象偏移。仍有全局串行锁与严格资源上限，不是完整 VFS/POSIX。
- 为 QEMU 9.2.0 RISC-V `virt` 提供实验性补丁：冷插拔一个独立 `nvdimm`，以私有设备树节点告知 xv6 物理地址；QMP 检查设备类型、地址和独立后端。未修改的上游 QEMU 普通 MAP 仍不是 NVDIMM。
- 多次 QEMU 进程重启和软件故障切点 oracle 验证文件、KV 与目录子树；有独立的有界**协议规格模型**和真实内核提前发布子树的变异负控。前者不是模拟硬件掉电，后者也不是对全部内核指令流的形式化证明。
- 保留 KV、CDC、stream 与 Bloom/fence；普通文件用有界 CDC/固定分块自适应。非替换 rename 与内容更新复用未改变的元数据根，相同覆盖写可避免无效提交；KV 使用有界精确索引并在超限时回退。提供 `pmstats` 软件读写/查询请求计数和[消融对照](docs/results/2026-10-28-efficiency/README.md)，不声称物理磨损改善或通用性能优势。
- 文件基准两侧统一普通 API、读回内容/EOF、成功操作数和路径深度；跨 QEMU 独立进程保存原始 ticks、输入哈希和按启动分组的统计。KV 另列；原生 xv6 的 `fsync` 是全局日志同步且未协商设备 flush，持久语义仍不等价，不能据此声称真实性能优势。

## 尚未完成或未证实

1. **实验性 NVDIMM 的限制：** 独立设备只在本仓 QEMU 9.2.0 补丁、固定单设备布局和私有 FDT 描述下工作，不是上游通用 `virt` 支持，也没有 ACPI/NFIT、热插拔或通用设备发现；普通 MAP/整机共享 RAM 仍不是 NVDIMM。导师是否接受该设备模型需确认。
2. **物理持久域：** 默认 `pmem_flush` 是遍历与屏障，不是缓存写回。可选 Zicbom 路径发出 `CBO.CLEAN`，假定 64 B 缓存块及正确权限；QEMU TCG 不模拟缓存层级，不能证明硬件断电持久性。必须在实际平台确认持久域与原子性。
3. **更强验证和功能：** 规格模型未追踪真实内核 store/flush，校验和碰撞、任意硬件故障、无限并发及完整 POSIX 均不在当前证明范围。动态挂载、硬链接/打开后 unlink、原生及跨后端 rename、符号链接和权限等未实现；未实现安全回收/compaction，长期写入会耗尽容量。
4. **正式论文实验：** 需要对齐可验证的持久边界、扩展负载与独立重复、补足统计和整体重写。已提供可移植证据章节草稿，但旧论文正文/PDF、旧图表和历史不等量基准不得作为新结果；新章节尚未编译。

## 文档入口

| 文档 | 内容 |
|---|---|
| [修复与验证报告](docs/PMFS-REPAIR-REPORT.md) | 实际代码、测试命令、通过结果和残余风险 |
| [语义与限制](docs/SEMANTICS.md) | API、持久边界、失败行为及不支持项 |
| [软件读写放大与耐久](docs/PERFORMANCE-ENDURANCE.md) | CDC/索引、软件请求计数、写入热点和未实现的安全回收 |
| [QEMU 后端](docs/QEMU-NVDIMM.md) | 普通 RAM、共享文件、Zicbom 与真正 NVDIMM 的区别 |
| [契约与故障矩阵](docs/CONTRACT.md) | 自测、真实进程重启 oracle、协议模型的覆盖与限制 |
| [旧基准](docs/BENCH.md) | 历史数字，只留档，不能用作性能结论 |
| [基线审计和计划](docs/EXCELLENCE-ROADMAP.md)、[基线沙箱记录](docs/LOCAL-SANDBOX-TESTS.md) | 修复前的证据和规划，不应误读为当前状态 |
| [`thesis/latex/`](thesis/latex/README.md) | 旧论文草稿，仍须依据实际证据整体重写 |
