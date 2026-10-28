# 实际 PMFS 操作轨迹与有界故障重放

> 这是以**真实执行的内核 PMFS 写入、flush 请求和 fence**为输入的软件模型，不是 RISC-V 机器指令级跟踪，也不是实际掉电试验。默认 `pmem_flush` 仅 touch；即使启用 Zicbom，QEMU TCG 也不模拟缓存持久化。模型的结论取决于下列显式假设。

## 如何取得轨迹

仅测试构建 `-DPMEM_MAP -DPMFS_TRACE` 开启插桩；普通构建的 `pmfs_trace()` 返回 `-1`、不记录、不改变持久语义。单 hart、无并行工作负载下，`pmfs_regress trace` 从一个已验证的持久基线开始，打开文件，追踪一次真实文件写及成功 `pmfs_fsync`，随后发出 ACK。采集范围包括：

- `pmdev_write` 执行后复制到设备区的实际字节与偏移，按假设的 64 B 行边界切分；以及 `pmfs_write_sb_slot` 对状态/校验和的两次直接 8 B 对齐 store。
- `pmem_flush` 循环中每个设备缓存行的请求，及 `pmem_fence` 的顺序。默认后端是软件 touch，**不是**真实写回事件。
- ACK 在 fsync 返回成功后记录；重启时由真实内核的独立文件、其他文件与 KV oracle 检查：ACK 前只允许完整旧值或完整新值，ACK 后必须为新值。

`pmdev_write` 内的 `memmove` 在 C 源中按字节正向复制（无重叠的本工作负载）。插桩在范围复制完成后读取结果；每个 W 是**源级范围片段**，不是硬件指令、微架构退休顺序或真实 cache-line traffic。直接提交词被记录为单个对齐 8 B store；模型假设它故障原子，不对其构造半字。其他工作负载若绕过这些接口（如格式化的整区 `memset`、破坏性自测或未插桩的直接映射写）不在此覆盖内。重放器把所有记录的 W 施加到基线后，要求与真实执行结束的**整个 PMFS 设备区**逐字节相同；这能发现最终可见的遗漏，但不能证明没有中途被覆盖的未记录 store。

## 持久域与状态生成

给定一个冷基线，模型分别维护 CPU 可见值、已经 drain 的持久值和尚未 drain 的行写回快照。**假设**行 flush 请求在相应 fence 完成后进入指定持久域；fence 前允许被请求的全部、部分或不发生提前写回。另采样未请求写回的脏行提前逐出：持续延迟、每次写都提前逐出、隔行提前逐出三类历史，以及某次写的当前行逐出。对非原子范围写，构造首字节和半片段写入后逐出的切点；这些是从源级范围推导的模拟切点，并非观测到的逐条 CPU store。模型不模拟任意瞬态指令重排、失效 flush、半个原子提交词、checksum 碰撞或设备内部行为。

候选镜像按相对于基线的行差分去重；包含每个 drain/提交词/ACK 状态及多种部分写入、提前写回和逐出状态。`--max-images 0` 对**本次生成的全部不同候选**逐一重启；非零值保留关键状态并按固定种子抽样。即使所有生成候选都重启，也不表示穷举所有持久化历史或所有工作负载。每张镜像在私有共享 RAM 文件中重建，再启动真正的测试内核执行旧/新 oracle；原始输入磁盘和内核不会被修改。

两项故意违反模型顺序的负控：①丢掉首个新数据页的所有 flush 请求而保留后续提交；②提前发布最终 checkpoint，但不先持久化其依赖的页。它们应使 ACK 后的新值检查失败（可能拒绝挂载），用来检验 oracle 能发现这些故障，而不是声称系统能容忍违反平台前提的写回。

## 运行与证据

必须在可丢弃的 checkout 里运行（以下会重建 `fs.img`）：

```sh
make clean
make EXTRA_CFLAGS='-DPMEM_MAP -DPMFS_TRACE' kernel/kernel fs.img
python3 tools/pmfs_trace_replay.py --qemu qemu-system-riscv64 \
  --output /tmp/pmfs-trace --max-images 0 --build-notes 'toolchain and flags'
```

结果目录包含 `metadata.json`（二进制/源树/QEMU 哈希、假设和计数）、压缩基线 PMFS 字节、原始 `trace.txt`、全部候选及选择信息、每次重启 oracle 和完整串口日志、负控与汇总。`make test-trace-replay` 是 32 镜像 CI smoke；它也重建内核和镜像。基线的普通 RAM MAP、QEMU 文件和进程杀死只是生成/重启手段，不是 NVDIMM 断电，也不验证器件磨损。
