# PMFS 软件读写放大、查询与耐久风险

> 项目保留 KV、CDC、stream、Bloom、fence 和索引核心。现有双 checkpoint 保护下的**元数据页复用**和显式 COW 压缩不是全设备 GC 或硬件磨损均衡；协议及限制见[维护设计](MAINTENANCE.md)。QEMU/TCG、普通文件后端和默认 touch+fence **不能**证明物理写回、NAND 擦除次数或器件寿命。

## 本轮有界优化

| 路径 | 旧路径 | 当前策略与正确性边界 |
|---|---|---|
| 非替换 PM rename | 每次写新 inode + dentry 两个 4 KiB 快照 | inode 图未改变，追加 1 页 dentry，复用之前已持久化的不可变 inode 根；一次 checkpoint 发布。替换目标仍要写两页。 |
| 单文件内容更新 | 一次 KV run 和两个元数据页 | 写新 run + 新 inode/extent 页，复用不可变 dentry 页；两个 checkpoint 顺序不变，文件的旧/新恢复 oracle 仍分别检查。 |
| 相同覆盖写 / 对空文件重复 truncate | 无意义地形成新版本与 checkpoint | 已有工作缓冲相同且不扩文件时只移动 offset；不能清除既有脏状态。空且干净的 truncate 不再重复发布。当前文件语义不提供 mtime。 |
| 冷 KV 读取 | 维护精确索引但仍逐 run 搜索 | 命中精确索引直接读最新 run/page；索引最多 512 键，未收录的键继续从新到旧搜索。所有 key 即使未进入精确表也更新 coarse posting；fallback 按倒序只遍历 posting 置位的 run，再经 Bloom、fence 跳过不相关页。 |
| 普通 fd 文件分块 | 为保证最多 4 extent 而总用固定分块 | 先模拟 CDC；若结果不超过 4 extent 且不多于 256 B 固定方案的块数，使用原 CDC，否则固定分块兜底。任意 0–1024 B 数据仍可表示。专用文件接口原有 CDC 行为不变。 |

选择性复用的元数据根必须是已提交的**不可变页**；新的元数据快照页可以从两个槽都不可达的页中分配，KV run 仍在 tail 追加。崩溃前后的 checkpoint 分别只能引用完整旧/新对象图。正常错误恢复旧的内存根和 tail；不可达的已写页可被后续元数据分配再利用。单页及双页 rename、run/文件 inode 页与 checkpoint 的进程重启切点分别测试。自适应 CDC 不做内容去重，分块数不增加也不代表所有输入的 CPU 时间更快。

## 观测与解释

`pmstats`（或 `pmfs_metrics(&m, sizeof(m))`，`struct pmfs_metrics` 定义在 `kernel/pmstats.h`）返回 v1 前缀及可选的尾部扩展，旧版 v1 结构长度仍可使用；小于原前缀及无效地址失败，不改变旧 `pmfs_stat` ABI。这是**易失软件计数**，重启/重新初始化会重置，不是介质 SMART/磨损统计，也不用于做寿命预测。

- `dev_write_bytes`：`pmdev_write` 请求复制的字节和 checkpoint 8 B 直接 store；`dev_page_writes`：完整 4 KiB `pmdev_write_page` 请求；`checkpoint_word_stores`：两个槽的 8 B 状态词 store 次数。显式格式化的整区 `memset`、绕过这些接口的自测/直接映射不在此计数。
- `dev_read_bytes`：`pmdev_read` 拷贝字节，包含恢复期间调用，**不**包括所有直接映射读取；`kv_page_reads` 仅为查询路径 run 页读取请求，不包括恢复/元数据全部读取。
- `requested_flush_lines`：代码请求的 64 B 行数，默认模式为 touch；Zicbom 是指令请求。它们都不是确认到达持久域或物理写放大。
- `file_write_bytes`、`file_identical_bytes`、`file_identical_ops` 计正常 fd 成功写和跳过的相同覆盖；`file_cdc_attempts`/`file_fixed_attempts` 是非空分块**尝试**，`file_cdc_planned_chunks` 是探测出的块数，失败也可能计入。单次写入请求与一次成功持久提交的分母不同，不能混用。
- `kv_exact_hits`、`kv_fallback_run_probes`（只计 posting 候选 run）、`kv_posting_skips`（倒序扫描时跳过的非候选 run）、`kv_page_reads` 是软件查询路径计数；冷、热缓存和不同 key 分布必须分开报告。用两次快照作差并排除其他任务，才能分析一个工作负载。
- `gc_scans`、`gc_trace_runs`、`gc_reused_pages`、`gc_failed_scans` 计可达性扫描、实际遍历的 run、复用分配尝试（含回滚）、失败扫描；`compact_*` 计成功压缩的输入/输出记录和输出页。`tail_pages` 是追加高水位而非当前活跃页数；这些都不表示实际擦除或器件磨损。

默认 `pmfs_stat` 的历史 `logical_bytes/phys_bytes` 主要覆盖 KV run seal（`phys_bytes` 也计压缩输出），**不含**全部元数据和 checkpoint；不能当全系统或硬件 WA。`pmstats` 可观察更广的调用范围，但仍没有介质写量、设备控制器内部写放大、擦除块计数或读放大（内部介质读取量）数据。

运行与当前样本见[优化验证](results/2026-10-28-efficiency/README.md)。单一指定工作负载的计数能说明该工作负载减少了多少**软件请求**，不证明所有负载更快、更耐久，更不能外推硬件写入寿命。

## 尚未解决：高频持久写入与介质磨损

- **已有有界维护，但并非全设备 GC。** 双槽均不可达的页可复用为元数据；显式 COW 压缩减少旧 KV 版本和无引用 chunk，要求临时尾部空间并拒绝活动 stream。run 分配仍追加，满盘不能靠它保证恢复空间，长期写入及 256 run 上限仍会失败。详见[维护设计与实验](MAINTENANCE.md)。
- **checkpoint 热点仍存在。** 固定双槽的提交词每次在相同位置写两次；不能声称本轮实现了均匀磨损。考虑未来版本化的多槽轮转/日志与批处理，必须定义恢复扫描、epoch 回绕、原子提交词、存活槽保护、格式迁移和故障测试，再实施；不能原地改变 v3 格式假装兼容。
- **后续软件路线：** 支持带预留空间的分步/后台搬迁、run 页的安全重用和受保护引用的完整生命周期，再以实际 store/flush trace、错误注入及不同负载验证；解决压缩全局锁和 near-full 失败。当前位图的 run 链缓存假设已验证的可达 run 不被原地修改，并不覆盖随后发生的任意在线介质损坏。
- **介质差异：** 持久内存、SSD、NAND 等的写/擦除粒度、设备控制器磨损均衡、flush 语义与寿命模型不同；高频逻辑写不等于一次物理擦除。真实平台需要设备指标、写入条件、持久域和厂商规范，并在不危及设备的前提下验证；当前没有这些证据。
