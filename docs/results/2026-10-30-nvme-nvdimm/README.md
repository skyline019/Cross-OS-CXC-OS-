# QEMU NVDIMM + 原生 NVMe：功能、顺序与性能基线

> **范围：**本次 NVDIMM 文件系统是仓库已有的 **v3 KV/CDC 实现**，不是新设计的无 KV/CDC 直接文件系统。新文件系统目前只有设计和有界模型，尚无内核功能或性能结果。本记录中的 QEMU TCG、宿主普通文件后端和 SIGKILL/重启不证明物理掉电、任意扇区撕裂、真实 NVDIMM 缓存持久域、SSD NAND 写放大或器件性能。

## 环境与拓扑

- QEMU 9.2.0 加仓库的 RISC-V `virt` NVDIMM 补丁；Zig 0.13.0 交叉编译、QEMU TCG，Linux x86-64 沙箱，无 CPU 隔离。版本、二进制及补丁哈希、编译器包装见 [`environment.txt`](environment.txt)，各隔离副本源码逐文件哈希见同目录 `test-*-source.sha256`。实验开始于 `af70b67` 加本轮工作树改动，并非该提交的纯净构建。
- QMP 检查同一 VM 中的 32 MiB NVDIMM、独立 memory-backend-file、PCI class `0x0108` 的 NVMe 及 16 KiB 64 位 BAR0；原始结果见 [`correctness/topology/`](correctness/topology/)。QMP 查询在 guest 配置 BAR 前显示 `address=-1`；guest 启动日志显示分配到 `0x40000000`。NVDIMM 地址为 `0x88000000`，其文件和 NVMe raw 根盘文件不同。QMP 是拓扑验证，不能单独证明 guest I/O。
- 根盘采用 QEMU `cache=writeback`、无 `cache.no-flush=on`；当前 NVMe 驱动实际 QD=1，日志阶段等待显式 namespace Flush。旧 v3 的默认 PMEM 路径是 touch+fence，TCG 不模拟持久缓存层。默认 virtio 驱动未协商 Flush，故不能将 virtio 与 NVMe 称为同一设备持久契约。

## 功能、重启与负控

| 实验 | 结果和证据 | 范围 |
|---|---|---|
| NVMe 独立根盘 | 512 B 和 1024 B LBA 分别写入、重启读回；缺设备、两个控制器、4 KiB LBA、非法日志计数/目的地/重复目的地共 6 个负例失败关闭。连续三轮、每轮 4 次正向启动与 6 个负例通过，输入镜像哈希不变。日志：[`nvme-boot-1`](correctness/nvme-boot-1/)、[`-2`](correctness/nvme-boot-2/)、[`-3`](correctness/nvme-boot-3/) | 三核；受限 QEMU virt/单 namespace/轮询驱动，不是通用 NVMe |
| 原生 redo 与同目录 rename | 各 5 个软件切点的崩溃/恢复/再次重启通过；各自丢弃已提交日志的负控被检测。日志：[`nvme-log`](correctness/nvme-log/)、[`nvme-rename`](correctness/nvme-rename/) | 单核；不涵盖任意乱序、撕裂、硬件断电 |
| NVMe 实际命令轨迹 | 97 条 I/O、4 个非空事务、31 写入与 18 Flush token；检查完成回调、`data→Flush→header→Flush→home→Flush→clear→Flush`，删除数据阶段 Flush 的负控被拒绝。[`nvme-trace/`](correctness/nvme-trace/) 含 QEMU 原始 trace、串口及摘要 | 一次具体 QEMU 9.2 软件轨迹；该次审计 guest 未挂 NVDIMM，单独验证 NVMe 顺序 |
| NVMe 三核 VFS 与并发 | VFS 基本路径通过；6 文件共 240,000 字节并发日志写回读通过；`usertests -q` 报 `ALL TESTS PASSED`。[`nvme-smp/`](correctness/nvme-smp/) 为串口原文 | 仍不是完整 POSIX、真实多队列或硬件压力测试 |
| 同机 NVDIMM + NVMe、既有 v3 | PMFS 62 次、VFS 134 次、GC 48 次启动通过，包含 checkpoint 故障点、回退、复用和破坏负例。日志：[`combined-pmfs`](correctness/combined-pmfs/)、[`combined-vfs`](correctness/combined-vfs/)、[`combined-gc`](correctness/combined-gc/) | 旧 v3，三核，Zicbom 关闭；不是新直接 FS 的验收 |
| 默认 virtio 回归 | 原生日志与 rename 各 5 切点、各自负控通过；三核基本 VFS 与并发写回读通过。日志：[`virtio-log`](correctness/virtio-log/)、[`virtio-rename`](correctness/virtio-rename/)、[`virtio-smp`](correctness/virtio-smp/) | 保留历史 no-op barrier，不宣称 virtio Flush |

**保留的测试负结果：**一次三核负例中，另一 hart 尚未输出完的 `hart 2 star` 与真实 `panic: log: invalid header count` 拼到同一串口行；原脚本要求 panic 位于行首，因而超时误报。随后压力重跑又出现 `phanic: ...`（另一 hart 的 `h` 插入 `panic` 内）；仅放宽行首仍不够。两次原始串口分别保存在 [`bad-count.log`](correctness/nvme-boot-harness-false-negative/bad-count.log) 和 [`interleaved-bad-destination.log`](correctness/nvme-boot-harness-false-negative/interleaved-bad-destination.log)。当前 oracle 仅容许一个已在执行的、已知 `hart N starting` 启动串口片段与完整目标 panic 交错，仍要求精确 panic 消息、换行和未出现 shell prompt；host 单测覆盖随机交错及错误消息。修正后又完成 [10 轮三核重跑](correctness/nvme-stability/summary.txt)，每轮 4 个正向启动和 6 个负例通过。它不接受任意噪声或缺失目标 panic。

## 同 API 的延迟与实际短批吞吐（同步契约不等价）

在**同一 VM** 内，比较已有 v3 NVDIMM 路径与原生 xv6 FS + NVMe 根盘；每个后端都执行相同的 open/write/fsync/close/reopen/readback+EOF 等操作，但 PMFS 为 touch+fence，xv6 为全局 redo 同步加 NVMe Flush，**不具备相同持久契约**，不得据此宣称 PMEM 相对 SSD 的等价持久性优势。

4 组各 1 个预热启动 + 10 个独立测量启动，单 hart、每次私有根盘和新建并显式格式化的 32 MiB NVDIMM；每启动每后端 4 个 guest 预热事务、32 个交替单次计时事务，再分别计时 32 次连续事务批。批次顺序随启动交替，固定种子打乱组次序；4 组共 44 次启动、零失败。时间为 guest `r_time`，频率从 QEMU FDT 得到 10 MHz；p50/p95 是每组 320 个样本的池化描述，短批吞吐为成功事务总数除以整个批次 guest 时间之和；区间是对 10 次独立启动重采样 2000 次。不是长时稳态、多线程或外部 wall-clock 吞吐。

| 工作负载 | 有效字节 | v3 NVDIMM p50/p95 (ms) | xv6 NVMe p50/p95 (ms) | v3 短批 txn/s [95%] | xv6 短批 txn/s [95%] |
|---|---:|---:|---:|---:|---:|
| overwrite | 64 | 1.234 / 2.062 | 5.690 / 8.481 | 793.7 [743.5, 831.7] | 160.3 [153.5, 167.7] |
| overwrite | 1024 | 1.506 / 1.919 | 5.657 / 6.714 | 633.6 [604.5, 663.1] | 168.1 [163.8, 172.4] |
| roundtrip | 64 | 1.774 / 2.349 | 15.085 / 18.603 | 571.2 [553.9, 590.5] | 56.3 [52.7, 59.7] |
| roundtrip | 1024 | 1.981 / 3.335 | 15.144 / 21.486 | 510.9 [495.2, 525.8] | 58.7 [57.0, 60.4] |

`overwrite` 更新已存在的文件；`roundtrip` 还包括检查不存在、创建和 unlink。每次均修改内容并校验完整读回及 EOF。v3 在本样本中 1024 B 覆写短批吞吐低于 64 B；这也是一个负结果。v3 平均每事务软件写请求：覆写 8,320 B、创建删除 24,832 B；这是软件计数，不是介质物理写入或磨损。

完整 [`perf/metadata.json`](perf/metadata.json)、[`raw.jsonl`](perf/raw.jsonl)、[`batches.jsonl`](perf/batches.jsonl)、[`summary.json`](perf/summary.json)、44 份启动串口及 [`perf-console.txt`](perf-console.txt) 均保留，包含所有预热、测量、失败记录和哈希。结果只代表该 VM、该工作量和该宿主环境，不能外推到新设计或真实器件。

## 复现

只在可丢弃的隔离副本中执行，`make clean` 会删除该目录的 `fs.img`，`pmformat --force` 会擦除 PMEM 文件。先按仓库 CI 构建应用补丁的 QEMU 9.2，并设置 `QEMU=/path/to/qemu-system-riscv64`：

```sh
python3 tools/check_storage_topology.py --qemu "$QEMU"
# 独立副本 A：NVMe 和日志恢复
make clean
make -j2 DISK=nvme EXTRA_CFLAGS='-DXV6_LOG_FAULT_INJECT' kernel/kernel fs.img
python3 tools/nvme_boot_tests.py --qemu "$QEMU" --cpus 3 --log-dir /tmp/nvme-boot
python3 tools/xv6_log_recovery.py --backend nvme --qemu "$QEMU" --cpus 1 --log-dir /tmp/nvme-log
python3 tools/native_rename_recovery.py --backend nvme --qemu "$QEMU" --cpus 1 --log-dir /tmp/nvme-rename
python3 tools/nvme_trace_audit.py --qemu "$QEMU" --output /tmp/nvme-trace
# 独立副本 B：旧 v3 + NVDIMM + NVMe，同步故障钩子仅用于恢复 oracle
make clean
make -j2 DISK=nvme EXTRA_CFLAGS='-DPMEM_MAP -DPMEM_NVDIMM -DPMFS_FAULT_INJECT' kernel/kernel fs.img
python3 tools/pmfs_recovery.py --nvdimm --backend nvme --qemu "$QEMU" --cpus 3 --log-dir /tmp/nvdimm-recovery
python3 tools/vfs_recovery.py --nvdimm --backend nvme --qemu "$QEMU" --cpus 3 --log-dir /tmp/nvdimm-vfs
python3 tools/pmfs_gc_recovery.py --nvdimm --backend nvme --qemu "$QEMU" --cpus 3 --log-dir /tmp/nvdimm-gc
# 独立副本 C：性能版本，禁用故障钩子
make clean
make -j2 DISK=nvme EXTRA_CFLAGS='-DPMEM_MAP -DPMEM_NVDIMM' kernel/kernel fs.img
python3 tools/bench_perf.py --nvdimm --backend nvme --qemu "$QEMU" \
  --runs 10 --warmup 1 --samples 32 --sizes 64,1024 \
  --modes roundtrip,overwrite --output /tmp/nvdimm-nvme-perf
```
