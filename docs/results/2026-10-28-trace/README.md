# 实际 PMFS 操作轨迹：一次文件 fsync 的模型重放

本次使用 base `7b7d8d1` 加工作树源码、Zig 0.13.0、`PMEM_MAP PMFS_TRACE`、QEMU 9.2.0 TCG、单 hart。普通 RAM MAP 文件只是模型镜像的载体，**不是 NVDIMM 或物理断电**。详细的持久域、8 B 原子提交词、范围级记录和故障模型边界见[轨迹说明](../../TRACE-REPLAY.md)。

- 一个已初始化文件从旧内容写入新内容并成功 `fsync` 的真实内核执行，记录 **134 个 W 范围片段、134 个 F 行请求、8 个 D、1 个 ACK 和 1 个 END**。按记录的所有 W 重建的整个 31,453,184 B PMFS 设备区与真实执行结束时逐字节相同。
- 根据记录和三种选定的提前逐出历史生成 **92 个去重候选镜像**，全部重启真实内核执行旧/新文件及其他文件/KV oracle：91 个恢复旧值、1 个恢复新值；ACK 后候选为新值，92/92 符合允许状态。
- 两项负控——遗漏新数据页 flush、在依赖页之前发布最终 checkpoint——分别恢复为 `invalid`，均被 ACK 后必须为新值的 oracle 检出。
- 原始 [`trace.txt`](replay/trace.txt)、压缩基线、[`candidates.jsonl`](replay/candidates.jsonl)、[`reboots.jsonl`](replay/reboots.jsonl)、完整串口日志和 [`metadata.json`](replay/metadata.json)、[`summary.json`](replay/summary.json) 均保留。`console.txt` 中旧版输出 `exhaustive=1` **仅指全部 92 个已生成候选都重启**，不表示穷举所有可发生的硬件状态或代码路径；新脚本将该字段改名为 `all_generated`。

此轨迹仅覆盖一次文件更新；W 由真实 `pmdev_write` 完成后的内容按行切片，部分写入切点由 C 源级正向复制推导，并非逐条机器指令跟踪。默认 flush 只请求 touch，模型假定 flush/drain 与对齐 8 B 原子写成立；未覆盖所有 GC、并发、checksum 碰撞、硬件乱序及其他异常，不能表述为物理掉电证明。由于这批测试基于未提交的工作树源码，不应将其归因于单独的旧 commit 或历史 CI。
