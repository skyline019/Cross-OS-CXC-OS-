# PMFS 软件读写放大、查询与耐久风险

> 项目保留 KV、CDC、stream、Bloom、fence 和索引核心。本页区分已经实现并测试的软件请求优化，与尚未实现的持久介质回收、磨损均衡及硬件寿命验证。QEMU/TCG、普通文件后端和默认 touch+fence **不能**证明物理写回、NAND 擦除次数或器件寿命。

## 本轮有界优化

| 路径 | 旧路径 | 当前策略与正确性边界 |
|---|---|---|
| 非替换 PM rename | 每次写新 inode + dentry 两个 4 KiB 快照 | inode 图未改变，追加 1 页 dentry，复用之前已持久化的不可变 inode 根；一次 checkpoint 发布。替换目标仍要写两页。 |
| 单文件内容更新 | 一次 KV run 和两个元数据页 | 写新 run + 新 inode/extent 页，复用不可变 dentry 页；两个 checkpoint 顺序不变，文件的旧/新恢复 oracle 仍分别检查。 |
| 相同覆盖写 / 对空文件重复 truncate | 无意义地形成新版本与 checkpoint | 已有工作缓冲相同且不扩文件时只移动 offset；不能清除既有脏状态。空且干净的 truncate 不再重复发布。当前文件语义不提供 mtime。 |
| 冷 KV 读取 | 维护精确索引但仍逐 run 搜索 | 命中精确索引直接读最新 run/page；索引最多 512 键，未收录的键继续从新到旧搜索。所有 key 即使未进入精确表也更新 coarse posting；fallback 按倒序只遍历 posting 置位的 run，再经 Bloom、fence 跳过不相关页。 |
| 普通 fd 文件分块 | 为保证最多 4 extent 而总用固定分块 | 先模拟 CDC；若结果不超过 4 extent 且不多于 256 B 固定方案的块数，使用原 CDC，否则固定分块兜底。任意 0–1024 B 数据仍可表示。专用文件接口原有 CDC 行为不变。 |

复用的页必须是已提交的**不可变页**；追加空间不复用，崩溃前后的 checkpoint 分别只能引用完整旧/新对象图。正常错误恢复旧的内存根和 tail；设备故障可使不可达空间泄漏。单页及双页 rename、run/文件 inode 页与 checkpoint 的进程重启切点分别测试。自适应 CDC 不做内容去重，分块数不增加也不代表所有输入的 CPU 时间更快。

## 观测与解释

`pmstats`（或 `pmfs_metrics(&m, sizeof(m))`，`struct pmfs_metrics` 定义在 `kernel/pmstats.h`）返回固定 v1 结构；长度过小及无效地址失败，不改变旧 `pmfs_stat` ABI。这是**易失软件计数**，重启/重新初始化会重置，不是介质 SMART/磨损统计，也不用于做寿命预测。

- `dev_write_bytes`：`pmdev_write` 请求复制的字节和 checkpoint 8 B 直接 store；`dev_page_writes`：完整 4 KiB `pmdev_write_page` 请求；`checkpoint_word_stores`：两个槽的 8 B 状态词 store 次数。显式格式化的整区 `memset`、绕过这些接口的自测/直接映射不在此计数。
- `dev_read_bytes`：`pmdev_read` 拷贝字节，包含恢复期间调用，**不**包括所有直接映射读取；`kv_page_reads` 仅为查询路径 run 页读取请求，不包括恢复/元数据全部读取。
- `requested_flush_lines`：代码请求的 64 B 行数，默认模式为 touch；Zicbom 是指令请求。它们都不是确认到达持久域或物理写放大。
- `file_write_bytes`、`file_identical_bytes`、`file_identical_ops` 计正常 fd 成功写和跳过的相同覆盖；`file_cdc_attempts`/`file_fixed_attempts` 是非空分块**尝试**，`file_cdc_planned_chunks` 是探测出的块数，失败也可能计入。单次写入请求与一次成功持久提交的分母不同，不能混用。
- `kv_exact_hits`、`kv_fallback_run_probes`（只计 posting 候选 run）、`kv_posting_skips`（倒序扫描时跳过的非候选 run）、`kv_page_reads` 是软件查询路径计数；冷、热缓存和不同 key 分布必须分开报告。用两次快照作差并排除其他任务，才能分析一个工作负载。

默认 `pmfs_stat` 的历史 `logical_bytes/phys_bytes` 仅覆盖 KV run seal，**不含**全部元数据和 checkpoint；不能当全系统或硬件 WA。`pmstats` 可观察更广的调用范围，但仍没有介质写量、设备控制器内部写放大、擦除块计数或读放大（内部介质读取量）数据。

运行与当前样本见[优化验证](results/2026-10-28-efficiency/README.md)。单一指定工作负载的计数能说明该工作负载减少了多少**软件请求**，不证明所有负载更快、更耐久，更不能外推硬件写入寿命。

## 尚未解决：高频持久写入与介质磨损

- **没有安全回收或 compaction。** v3 只追加，空间及 run 数最终耗尽；即使本轮减少一些写入，也只是延后失败。不得启用旧的丢弃存活数据式 compaction，也不得在线复用可能被任一 checkpoint 或打开引用访问的页。
- **checkpoint 热点仍存在。** 固定双槽的提交词每次在相同位置写两次；不能声称本轮实现了均匀磨损。考虑未来版本化的多槽轮转/日志与批处理，必须定义恢复扫描、epoch 回绕、原子提交词、存活槽保护、格式迁移和故障测试，再实施；不能原地改变 v3 格式假装兼容。
- **后续可验证的软件路线：** 先以持久对象可达性模型覆盖 *所有仍受保护的 checkpoint* 和运行时引用，再设计 COW 搬迁、原子发布、延迟回收、故障后可重建空闲表、空间水位和有界后台预算；对每个搬迁/复用切点构造重启 oracle 与负控。在做这些之前容量不足应明确失败。
- **介质差异：** 持久内存、SSD、NAND 等的写/擦除粒度、设备控制器磨损均衡、flush 语义与寿命模型不同；高频逻辑写不等于一次物理擦除。真实平台需要设备指标、写入条件、持久域和厂商规范，并在不危及设备的前提下验证；当前没有这些证据。
