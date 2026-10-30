# QEMU 独立直接 PMEM 实验核心：原始结果

> **范围：**无 KV/CDC 的单合成文件发布核心，最大 512 B，**不是 VFS 文件系统**。对照是经 VFS 和 redo log 的 xv6/NVMe，API 与持久契约不同。本目录所有结果为 QEMU TCG、宿主普通文件后端、进程杀启或有界模型，不证明物理断电、缓存持久域、真实 SSD/PMEM 性能或磨损。实现、假设和复现命令见[实验说明](../../THESIS-DIRECTCORE-EXPERIMENT.md)。

## 环境和代码身份

QEMU 9.2.0 加本仓 RISC-V virt NVDIMM 补丁，32 MiB 独立文件映射，NVMe 根盘使用另一私有文件，`cache=writeback`；Zig 0.13/Clang 18.1.6，RV64GC `-O`，宿主 x86-64，未隔离 CPU。测试在 `7c527a9` 及本轮变更的独立构建副本中运行。版本、构建标志、kernel/fs.img/QEMU/补丁哈希见 [`environment.txt`](environment.txt)，每个副本的源码文件哈希见 [`recovery`](recovery-source-files.sha256)、[`enospc`](enospc-source-files.sha256)、[`legacy`](legacy-source-files.sha256) 和性能各目录 `source-files.sha256`。构建过程生成的 `user/usys.S` 也列入清单。镜像、串口和汇总保留在对应目录；测试工具不改输入的原始 kernel 或根盘。所有结果文件的摘要见 [`SHA256SUMS`](SHA256SUMS)。

`CBO.CLEAN` 仅在 QEMU TCG 中执行指令路径，TCG 不模拟物理缓存层次；对齐 8 B store 的失败原子性和 64 B 故障单元均为协议假设。宿主 page cache 中的文件和 `SIGKILL` 并不模拟宿主断电。

## 恢复、安全和实际轨迹

| 测试 | 结果与原始证据 | 范围 |
|---|---|---|
| 核心恢复 | 三核、Zicbom、NVDIMM+NVMe：35 次启动通过，包含空盘失败关闭、显式格式化、正常重启、相同内容无写、缩短、400 次覆写和两写者各 100 次并发。禁用旧 v3 格式化、KV、文件及 trace 调用，并检查介质哈希不变。[串口](recovery/) / [控制台](recovery-console.txt) | 一个 0–512 B 合成文件；锁串行，非完整 VFS |
| 软件故障点 | 1–3 本次重启为旧值；4–6 为新值。协议允许 4–5 出现旧或新，6 的 drain 后必须新。6 个切点及恢复串口在 [`recovery/`](recovery/) | 进程杀启、宿主页缓存；非硬件断电 |
| 元数据损坏及数据负控 | 根指针校验、版本、相同 epoch 异根、单根别名四种损坏拒绝且不写；翻转数据行时 metadata probe 仍接受，但独立内容检查失败 | 数据 CRC 关闭，**不能检测一般静默数据损坏** |
| 空间不足 | 14 个单元、故障注入关闭，8 次启动：ENOSPC 预检不修改镜像、旧数据重启正确、禁用的 fault syscall 不写、小文件缩短成功。[串口](enospc/) / [控制台](enospc-console.txt) | 小设备合成压力，不是全功能长期满盘或物理磨损测试 |
| 源级实际轨迹及镜像重放 | 6 个候选对象 store、2 个顺序 fence 事件、7 个 flush、2 个 drain 组、1 根词 store；实际轨迹可逐字节重建整个 32 MiB 最终镜像。43 个候选镜像：42 旧、1 新；漏数据 flush、提前发布根、覆盖受保护数据 3 个负控均被检测，累计 46 次重启。[原始 trace](recovery/34-trace-update.log)、[基线镜像](recovery/trace-baseline.pmem.gz)、[最终镜像](recovery/trace-final.pmem.gz)、[候选及负控](trace-replay/summary.json)、[逐镜像串口](trace-replay/serial/) | 单个 128 B 负载，采样部分撕裂、提前驱逐和 flush 子集；`memmove` 后按行记录，不是全部 CPU 指令或任意持久乱序的穷举 |
| 根 store 指令 | 最终 trace 构建的根 store 5 个 DWARF 映射点均为 RV64 `sd` 或 `c.sd`。[审计](isa/store-isa.json) | 指令宽度不是故障原子性；不覆盖其他编译器 |
| 兼容 | 三核默认 virtio 基本 VFS、6 文件并发日志写回读通过；此前同源码其他默认改动上的 `usertests -q` 通过。旧 v3 同机 NVDIMM+NVMe+Zicbom 62 次恢复启动通过。[日志](compatibility/) | 旧 v3 不是新核心；真实硬件不在范围内 |

`PMTRACE O` 在单条 `pmem_flush_batch_begin()` fence 后记录；`PMTRACE D` 在 `pmem_fence()` 内两条 CPU 同步 fence 后记录。`O` 不被当作持久 drain，`D` 是一组软件事件。其余计数、负控的候选 patch、序号和镜像哈希均在 JSON 中；trace/候选模型假定根词不撕裂且 flush+drain 达到所声明的模拟持久域。

## 同内容同步覆写：两轮完整保留

每组 2 次预热启动 + 20 次独立测量启动，每次单 hart，64 B 或 512 B；每启动两个后端各 4 个 guest 预热、32 次交替计时覆写，再各测 32 次连续短批。两组共 44 次启动/轮、每组每后端 640 个测量样本；两轮均零失败。每个单次覆写在计时后读回检查，短批最后一次在计时后读回。短批吞吐是全部成功事务数 / 全部短批 guest 时间，包含生成数据和循环；它不是长时间稳态吞吐。`r_time` 频率由 QEMU DTB 得到 10 MHz；p50/p95 为池化描述，95% 区间为 20 个独立启动重采样 2000 次。

**最终源码构建：**

| 内容字节 | 直接核心 p50/p95 (ms) | xv6/NVMe p50/p95 (ms) | 直接核心短批 txn/s [95%] | xv6/NVMe 短批 txn/s [95%] |
|---:|---:|---:|---:|---:|
| 64 | 0.131 / 0.230 | 3.011 / 3.990 | 8,691 [7,853, 9,614] | 331 [320, 343] |
| 512 | 0.152 / 0.256 | 2.924 / 4.025 | 7,354 [6,362, 8,262] | 311 [274, 346] |

**此前一轮：**

| 内容字节 | 直接核心 p50/p95 (ms) | xv6/NVMe p50/p95 (ms) | 直接核心短批 txn/s [95%] | xv6/NVMe 短批 txn/s [95%] |
|---:|---:|---:|---:|---:|
| 64 | 0.122 / 0.168 | 2.583 / 3.118 | 10,604 [10,404, 10,747] | 396 [382, 409] |
| 512 | 0.141 / 0.204 | 2.590 / 3.279 | 8,451 [8,166, 8,682] | 390 [376, 404] |

两轮源码的差别仅在后来增加的**跟踪开关代码**，两轮性能构建均未定义 `PMFS_TRACE`；内核哈希不同，源码差异保留在 [`pre-order-to-final.patch`](perf-first/pre-order-to-final.patch)，不能假装二者是同一二进制。两轮数值和区间差异较大，宿主没有 CPU 隔离，不能断言唯一原因。512 B 的直接核心短批吞吐两轮都低于 64 B，是本负载的负结果；不从中推断硬件写放大或普适大小趋势。

直接侧只调用整文件替换并同步发布的测试 syscall，没有 VFS、文件描述符或 seek；xv6 侧为预打开文件的 `lseek+write+fsync`，`fsync` 是**全局**日志同步并发 NVMe Flush。虽然内容和成功次数匹配，API/能力/持久语义不等价；这些数字不能称为完整 FS 的公平对照或真实器件优势。

每轮均保留 [`metadata.json`](perf-final/metadata.json)、[`raw.jsonl`](perf-final/raw.jsonl)、[`batches.jsonl`](perf-final/batches.jsonl)、[`summary.json`](perf-final/summary.json)、44 份[串口](perf-final/serial/)和[控制台](perf-final-console.txt)；上一轮同样保存在 [`perf-first/`](perf-first/)。元数据包含各自输入哈希、QEMU、构建说明、时间基准和每次失败记录。详细运行方式见[复现步骤](../../THESIS-DIRECTCORE-EXPERIMENT.md#5-复现仅隔离副本)。
