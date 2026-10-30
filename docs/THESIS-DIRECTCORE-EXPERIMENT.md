# 独立无 KV/CDC 直接 PMEM 发布核心：实现与实验边界

> **这是实验核心，不是完整文件系统。** 它仅支持一个合成文件、0–512 B 整文件替换和读回；没有 VFS 挂载、目录、路径、多个 inode、递归文件树、稀疏洞、unlink-open 或真正的 `write/fsync/rename`。保留旧 v3/KV/CDC；两个格式和运行路径不兼容。当前实验只验收 QEMU 软件模型，不证明物理掉电、实际 NVDIMM 持久域、真实 SSD 的 FTL/磨损或硬件性能。

## 1. 已实现的最小机制

`DIRECT_CORE=1` 单独编译 [`kernel/directcore.c`](../kernel/directcore.c) 和测试客户端。它要求 `PMEM_MAP` 与 `PMEM_NVDIMM`，拒绝 `PMFS_SELFTEST`；启动时禁用旧 v3 的初始化与所有旧 PMFS 文件/KV/格式化/trace syscall，另在旧格式化函数内再次拒绝。默认构建、virtio 和已有 v3 代码不删除、不替换。直接路径只操作独立映射的 NVDIMM，不经过 `bio`、xv6 块日志、KV、CDC 或 NVMe。

从设备头部保留 4 KiB 后开始分配，以下均为相对该起点的 64 B 单元 ID：

```text
根槽 0、根槽 1（不同 64 B 行，各自只有一个对齐 8 B 指针词）
    → checkpoint（版本、epoch、对象根、几何、CRC）
      → 8 项表节点 → 唯一 inode → 8 项数据节点 → 最多 8 个数据行
```

实验格式为独立 `DFC1`，不是 v3，也不保证与未来完整文件系统格式兼容。节点、inode、checkpoint 和根指针均有 CRC/结构/范围校验；**数据行没有 CRC**。当前实现用大小已静态断言的 RV64 小端 C 结构，不是可跨任意 ABI/大小端移植的序列化实现。两根必须都合法；同 epoch 仅允许指向同一 checkpoint，根图中同一对象身份/类型不一致或单根内部别名则失败关闭。CRC 不是密码认证，不能排除碰撞。

提交在全局睡眠锁下执行：先从两个根遍历最多 24 个对象并做有界标记，first-fit 为所有新行预留空间；预留失败前没有持久写。已修改数据行、数据节点、inode、表节点和 checkpoint 写入新行，相同数据行可复用。随后一次批量顺序 fence、逐行 clean 请求、一次 drain；再用一次对齐的 8 B store 更新非活动槽，顺序 fence、clean 该槽、第二次 drain 后返回。每次写都重新从双根重建标记，直接重用不再可达行；没有引用计数、后台 GC、链群分配器或在线根刷新。锁保护读、写与实验 trace；这使双写者语义容易审计，但不是可扩展并发设计。

`pmem_flush_batch_begin()` 是顺序 fence，不是持久完成；`pmem_flush_batch()` 不隐含每行 drain，调用者必须执行 `pmem_fence()`。旧 `pmem_flush()` 仍保留原有尾部同步行为。本实现不能因缺少空间而覆盖任一根可达的旧对象，且可能保守返回 ENOSPC；没有承诺物理满盘仍能覆写。为简化实验，first-fit 每次从头搜索，靠小对象集可控，并不适合声称大规模最优。

## 2. 明确的故障与安全假设

- 根词必须由平台提供**对齐 8 B 故障原子性**，两个根必须处于不同的实际持久故障单元；格式还假定 64 B cache-block 几何。代码发出 `CBO.CLEAN`、顺序 fence 和 drain，但真实平台还须验证指令支持、权限、PMA、块大小、顺序和持久域。
- QEMU TCG 的 Zicbom 是指令路径测试，不模拟真实缓存层次；普通宿主文件后端、内核 page cache 和 SIGKILL 不等于整机断电。只证明指定模拟与有界重排模型里的行为。
- 空白或损坏介质不自动格式化；`directcore format --force` 和 `directbench --force` **会破坏设备内容**，没有能力识别或保护有价值的 v3 镜像。构建和测试仅在独立可丢弃副本以及临时 NVDIMM/根盘中执行。`make clean` 也会删除当前目录的 `fs.img`。
- 根校验、metadata CRC 和双根遍历不保证静默数据损坏可检测。实验故意翻转已提交数据，内核元数据 probe 仍接受，独立的内容 oracle 才发现错误；这是明确的负控，不是完整数据完整性保证。

## 3. 实际运行和可复验数据

原始串口、输入/输出压缩镜像、测试清单、样本和环境信息见[本次结果](results/2026-10-30-directcore/README.md)。主要结果：

- **三核 NVDIMM + NVMe + Zicbom**：正常启动、显式格式化、相同内容无写、缩短至空、400 次覆写、两个并发写者各 100 次、旧 v3 syscall 隔离、6 个软件故障点、4 类元数据损坏和数据 CRC 关闭负控，共 35 次启动通过。故障点 1–3 本次恢复旧值，4–6 本次恢复新值；协议只要求 4–5 允许旧或新，6 必须新。
- **低空间**：14 行设备、关闭故障注入的独立构建，8 次启动验证全量预留失败不修改介质、重启保留旧值、关闭的注入调用不写，以及缩短为空成功。并不代表完整文件系统已实现近满/长期回收。
- **缓存行边界与双根内容**：额外覆盖 0、1、63、64、65、127、128、129、255、256、257、511、512 B 和固定种子的长短交替/重复写，共 53 个更新、107 次三核重启；host oracle 在每次更新后检查两个根的期望内容，53 次只读重启不改变介质。原始串口、逐例哈希和首尾镜像见[补充结果](results/2026-10-30-directcore-boundaries/README.md)。
- **实际操作轨迹**：某次 128 B 更新的已观测轨迹含 6 个 64 B 候选对象 store、2 个预 flush 顺序 fence 事件、7 个行 flush、2 个 drain 组、1 个 8 B 根 store 和 ACK。每个 drain 组对应当前 `pmem_fence()` 中的 `__sync_synchronize()` 与显式 `fence rw,rw`，不是把两条 CPU 指令当作一次指令。64 B store 事件记录 `memmove` 后的完整行，而非逐机器指令跟踪；预先写回、flush 子集和部分写仅做有限采样。实际轨迹所有 store 重建整个 32 MiB NVDIMM，与实测最终镜像**逐字节一致**；对 43 个不同候选镜像以及漏数据 flush、提前发布根、覆盖受保护数据 3 个负控进行 46 次重启，guest 和独立 oracle 一致。
- **机器码宽度审计**：[`tools/check_directcore_store.py`](../tools/check_directcore_store.py) 使用最终 RV64 ELF 的 DWARF 行表和原始指令字，检查根 store 映射到的 `sd`/`c.sd`。这只确认本次编译产物使用单个 64 位存储指令，并不能证明故障原子性，也不能替代对其他编译器、标志及真实平台的验证。
- 另验证默认 virtio 三核 VFS 和并发日志写；对保留的旧 v3 在 NVDIMM+NVMe 的恢复做独立回归。旧实现的数据不能被当作新核心结果。

## 4. 匹配工作量的微基准，不是完整 FS 对照

[`user/directbench.c`](../user/directbench.c) 在每次全新、显式格式化的 NVDIMM 和私有 NVMe 根盘上，对同一长度、相同变化字节做成对的固定偏移覆写。直接组调用一次“整个合成文件替换并同步发布”的实验 syscall；原生组预先打开已有文件，每次 `lseek + write + fsync`，其中 xv6 的 `fsync` 是全局日志同步并使用 NVMe Flush。每次单操作计时结束后都读回验证，读回不计入该次时延；然后分别计时连续短批，包含生成数据和循环成本，批后读回。样本组交替顺序、启动间交替批次顺序、固定种子打乱大小顺序，时间来自 QEMU DTB 的 `r_time` 频率；置信区间重采样独立启动，而不是把同一启动内的样本当独立实验。

**两组 API、功能、路径层级及持久契约不等价**：直接组没有 VFS/描述符/seek/常规文件语义，`CBO.CLEAN` 在 TCG 不模拟真实持久缓存；原生组经过 VFS、块日志与模拟 NVMe Flush。因而结果只能说明这两条具体软件路径在这一 QEMU/宿主配置下的差异，不能作为完整 PMEM 文件系统对原生 xv6 的公平性能结论，不能外推真实 NVDIMM/SSD。结果、所有预热、原始样本、短批、两轮和负结果均保留在结果目录。

## 5. 复现（仅隔离副本）

先按项目 CI 编译带本仓 QEMU 9.2 virt NVDIMM 补丁的 QEMU，并设置 `QEMU=/path/to/qemu-system-riscv64`。下面每个构建都应在不同的干净工作副本执行；命令中的 `/tmp/...` 输出目录必须不存在。

```sh
# A. 独立发布核心，故障点和 trace，三核 + Zicbom
make clean
make -j2 DISK=nvme DIRECT_CORE=1 \
  EXTRA_CFLAGS='-DPMEM_MAP -DPMEM_NVDIMM -DPMEM_ZICBOM -DDIRECT_CORE_FAULT_INJECT -DPMFS_TRACE' kernel/kernel fs.img
python3 tools/check_directcore_store.py --output /tmp/directcore-isa.json
python3 tools/directcore_recovery.py --qemu "$QEMU" --backend nvme --cpus 3 --zicbom --log-dir /tmp/directcore-recovery
TRACE=$(find /tmp/directcore-recovery -name '*-trace-update.log' -print)
python3 tools/directcore_trace_replay.py --qemu "$QEMU" --backend nvme --cpus 3 --zicbom \
  --baseline /tmp/directcore-recovery/trace-baseline.pmem.gz \
  --final /tmp/directcore-recovery/trace-final.pmem.gz --trace "$TRACE" --output /tmp/directcore-replay
python3 tools/directcore_boundary_recovery.py --qemu "$QEMU" --backend nvme --cpus 3 --zicbom \
  --random-steps 24 --seed 20261030 --output /tmp/directcore-boundaries
# B. 另一独立副本：14 行、故障注入关闭
make clean
make -j2 DISK=nvme DIRECT_CORE=1 \
  EXTRA_CFLAGS='-DPMEM_MAP -DPMEM_NVDIMM -DDIRECT_CORE_LINE_LIMIT=14' kernel/kernel fs.img
python3 tools/directcore_enospc.py --qemu "$QEMU" --backend nvme --log-dir /tmp/directcore-enospc
# C. 另一独立副本：微基准，禁用 trace 和故障注入
make clean
make -j2 DISK=nvme DIRECT_CORE=1 \
  EXTRA_CFLAGS='-DPMEM_MAP -DPMEM_NVDIMM -DPMEM_ZICBOM' kernel/kernel fs.img
python3 tools/directcore_bench.py --qemu "$QEMU" --zicbom --runs 20 --warmup 2 --samples 32 \
  --sizes 64,512 --output /tmp/directcore-bench --build-notes 'record compiler and build flags here'
```

## 6. 下一步缺口

实现真正的 VFS inode/目录和多文件、可增长/稀疏树、部分写与 `fsync`、崩溃原子 rename/link/unlink-open，再建立持久可重建的分配与读者保护。按已测瓶颈分别引入 stream window、相邻行分配/链群、递归共享前缀和候选集合内 DP；必须提供可关闭基线和包括内存、后台开销的负结果。补充大文件、近满长期写入、跨文件故障、真正同 API/同同步契约的延迟与长时吞吐之后，才能评估完整题面的架构优势。
