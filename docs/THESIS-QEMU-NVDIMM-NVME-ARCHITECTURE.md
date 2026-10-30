# QEMU NVDIMM 验收与 xv6 原生 NVMe 对照：架构更新

> **约定和状态（2026-10-30）：**当前验收以 QEMU 软件模型为准，真实硬件实验暂不作为本阶段门槛。这里把“NVMe 原生实现”解释为在 xv6 中实现独立 PCIe NVMe 块设备路径，作为原生 xv6 FS 的 SSD 对照；NVDIMM 文件系统仍直接访问持久内存，绝不经 NVMe、`bio` 或块日志。已有 KV/CDC v3 保留不删除。本文中的无 KV/CDC 文件系统及窗口分配优化仍是**待实现设计**；仓库另有受限的 NVMe 驱动原型，具体测试证据以[实验记录](results/2026-10-30-nvme-nvdimm/README.md)为准。

## 1. 两条设备路径，一组文件 API

```text
                            read / write / fsync / rename
                                      xv6 VFS
                      ┌─────────────────┴─────────────────┐
                      │                                   │
            /pm: 直接 PMEM FS                 /: 原生 xv6 文件系统
            无 KV、无 CDC（待实现）             1 KiB 块缓存 + redo log
                      │                                   │
        64 B 对象、COW、两个根槽                  rootdisk 后端选择
                      │                         ┌─────────┴─────────┐
            flush / drain / 顺序                │                   │
                      │                   virtio-mmio          PCIe NVMe
           QEMU NVDIMM + 独立文件         原行为兼容         SQ / CQ / Flush
```

- 正文主线是题面的**直接、字节寻址 PMEM 文件系统**。NVMe 是独立 SSD 对照和 xv6 底层扩展，不是把 NVMe LBA 映射伪装为持久内存，也不是把 PMEM 格式放进 NVMe。
- 可以在同一个 QEMU VM 中同时接入两个设备：NVDIMM 有自己的 32 MiB 文件后端，NVMe 使用另一份原生 `fs.img`。`tools/check_storage_topology.py` 用 QMP 验证两者类型、地址/BAR、内存后端及文件互不相同；它只检查设备拓扑，不替代 guest I/O 测试。
- 默认 root disk 仍为 virtio；`DISK=nvme` 需要干净重编译，并选择 QEMU NVMe root。二者继续使用相同的 xv6 原生磁盘格式。PMEM 的设备与格式选择独立，旧 v3 与新直接格式不兼容，不自动迁移或格式化。
- 题面的指标是在**注明配置和故障模型下的 QEMU 结果**：功能、失败行为、重启恢复、延迟与实际吞吐。QEMU 普通文件和进程杀启不模拟真实器件物理断电；这一限制写入结论即可，不把暂缓硬件当成本阶段失败。

## 2. NVMe SSD 特性：可利用什么、不可偷换什么

下表区分 NVMe 协议、SSD 实现、QEMU 9.2 仿真与 PMEM。QEMU 的具体能力以 [9.2 NVMe 文档](https://github.com/qemu/qemu/blob/v9.2.0/docs/system/devices/nvme.rst)、[控制器源码](https://github.com/qemu/qemu/blob/v9.2.0/hw/nvme/ctrl.c) 和实际 Identify 结果为准；并不是每个可选功能都默认启用。

| 特性 | NVMe / SSD 含义及 QEMU 9.2 注意事项 | 对直接 PMEM 与本阶段的取舍 |
|---|---|---|
| 命令与寻址 | NVMe 使用命名空间、LBA、控制器命令和 DMA，非 CPU 对 SSD 的普通 load/store；SSD 后端通常由 FTL 映射 NAND 页 | NVDIMM 映射为 PMEM 字节地址；只借鉴批处理思想，不引入 LBA、FTL 或 SSD 队列 |
| SQ/CQ 和门铃 | 主机将命令写 SQ，设备经 CQ 返回状态和 phase；`CAP.MQES`、`DSTRD`、命令 ID、队列大小和屏障都需处理 | 当前驱动只建 1 个 admin + 1 个 I/O 队列，队列容量最多 16，但**实际同时只有 1 条命令**；不是多队列/并行 SSD 驱动 |
| 传输范围 | PRP 描述 DMA 页面，设备页大小、MDTS、LBA 大小、元数据/PI 都要检查；QEMU 默认 `mdts=7`，在 4 KiB 最小页面下对应 512 KiB，设备可配置 | 当前只传 1 KiB xv6 块，支持跨两个 4 KiB 页面 PRP1/PRP2；Identify 验证 512/1024 B LBA，拒绝命名空间元数据和 PI；不宣称任意 PRP 列表支持 |
| 普通写完成、VWC 与 Flush | 写完成不自动等于已到非易失域；易失写缓存、同步策略和后端配置相关。QEMU 9.2 的 NVMe Flush 最终调用块层 `blk_aio_flush`，本实验明确用 `cache=writeback` 且不设置 `cache.no-flush=on` | 当前 NVMe 日志的每个顺序阶段都先等待 I/O 完成再发 namespace Flush，并等待 Flush 完成。宿主普通文件/OS 的物理掉电保证不在 QEMU 验收范围 |
| FUA | 规范中的 FUA 不能简单等同于普通写完成。查阅 QEMU 9.2.0 `ctrl.c`，普通 `nvme_blk_write` 未将 `NVME_RW_FUA` 传入块层写标志；**不要假设此版本单靠置位 FUA 已经过验证** | 原型不依赖 FUA，使用显式 Flush；之后若升级 QEMU/启用其他后端，需重新检查源码及做命令级负控 |
| 原子写及撕裂 | SSD 的 LBA、AWUN/AWUPF、namespace 覆盖及原子边界决定不同条件下的原子承诺，不能推断任意 1 KiB 文件系统块总是原子 | xv6 原生 redo header 无校验和；现有故障切点是 QEMU 进程重启，不覆盖任意撕裂。当前不声称已解决 NVMe 任意扇区撕裂 |
| 随机写、FTL 与 GC | 随机小写、内部页搬迁、预留空间和后台 GC 可能影响尾延迟及 NAND 写放大；具体结果取决于 SSD 实现与历史负载 | QEMU 原生 NVMe 接普通块文件，不建模真实 FTL、NAND 擦除、磨损或代表性的设备时延；禁止把软件写字节称为 NAND 写放大 |
| DSM / TRIM | 可提示不再使用的逻辑范围；回读值、延迟和回收收益取决于能力和配置，不能默认等于安全清零或立即完成擦除 | 当前驱动不发 DSM。直接 PMEM 对象释放由双根可达性决定，与 SSD TRIM 无关 |
| ZNS / FDP / 多命名空间 | QEMU 可选择性模拟 ZNS、FDP、额外 namespace 等；顺序区写入指针、资源限制和能力协商都不同 | 与直接 PMEM 核心无关，首版不引入。未来作为 NVMe 对照的独立实验，不把可选仿真当成真实 SSD 特性普遍存在 |
| CMB / PMR、MSI-X、中断合并 | 控制器内存、可持久内存区域、MSI-X 都有各自能力和顺序语义；QEMU 9.2 文档称中断合并未实现 | 当前不使用 CMB/PMR/MSI-X；轮询是简化的 QEMU 基线，不代表高性能或通用硬件支持 |
| 队列仲裁与背压 | 多队列、优先级及设备内部并行可增加并发，但也需要限制 in-flight、处理资源耗尽和乱序完成；深队列不保证单次延迟更低 | 当前单锁单请求不会产生这些并发收益；后续应先测队列深度、CPU 占用和公平性，再引入异步队列与唤醒机制 |
| 电源状态与热管理 | 功耗状态、温控、节流、断电保护电容和 VWC 策略因控制器及设备而异，并可显著改变长时间吞吐和尾延迟 | 现有普通文件后端没有可代表真实 SSD 的功耗、温度或断电保护；不以模拟结果推断这些性质 |
| 数据校验/SMART | NVMe 可选 metadata、端到端保护及 SMART；QEMU 指标和电源周期不是物理 SSD 耐久实测 | 直接 PMEM 元数据校验与可选数据 CRC 是文件格式特性；读回 oracle 必须独立检查，不能用仿真 SMART 推断真实寿命 |

软件写请求放大可定义为 `guest 向某后端请求写入字节 / 应用有效写入字节`，并分开统计 PMEM store、flush 行请求、NVMe 主机写命令和字节；真正的 NAND 写放大需要设备内部物理写入量，**本 QEMU 实验不能计算**。同理，CPU cache miss、介质耐久和真实尾延迟不可由 TCG 仿真外推。

## 3. 已加入的 xv6 NVMe 原型及日志边界

### 3.1 原型范围

`kernel/rootdisk.c` 把单根块设备的初始化、读写和日志屏障从 `bio.c` 解耦；不改变 `bread`/`bwrite` 的调用接口，亦不将 PMEM 置于块抽象之下。`kernel/nvme.c` 在 `XV6_DISK_NVME` 构建下：

1. 只扫描 QEMU `virt` PCI bus 0，要求恰好一个 NVMe 类控制器；为 64 位 BAR0 在预留窗口分配地址，启用内存访问和总线主控。页表只在该构建中映射有限 ECAM/MMIO 窗口。
2. 检查 `CAP`、页面大小和 NVM 命令集，建立 admin SQ/CQ，使用 Identify Controller/Namespace 1 检查条目大小、容量、LBA 和无 metadata；再建立一对轮询 I/O 队列。
3. `read`、`write`、namespace `Flush` 同步等待完成，检查 CQ phase、SQ ID、CID、状态和超时；命令错误、超时或不支持的几何直接 panic，避免错误地返回成功。相同的物理 BSS 页面既是 DMA 地址也是内核虚拟地址，依赖 xv6/QEMU 的身份映射和 DMA 一致性假设。
4. 串行请求在 spinlock 下轮询；这是**正确性优先的单队列基线**，可能占用 CPU、限制多核及吞吐，不是高性能最终驱动。不支持一般 PCI 枚举/桥、热插拔、IOMMU、多个控制器或 namespace、异步请求、MSI-X、中断恢复、分散列表、复位重试和真实设备的完整错误处理。

如需实用化，应首先实现可休眠的请求生命周期、队列并发、IRQ 处理、资源超时与复位、通用 DMA/IOMMU；每步保持旧后端，并用同负载/相同顺序契约作消融，而不是一次引入所有功能。

### 3.2 根日志的显式顺序

对于 NVMe root，**所有前一阶段写入已完成后**才发送并等待 Flush：

```text
redo 数据写入并完成 → Flush
commit header 写入并完成 → Flush
home blocks 写入并完成 → Flush
清零 header 并完成 → Flush → 返回提交成功
恢复：重放 home blocks → Flush → 清 header → Flush
```

失败时原型停止而不确认成功。header 的数量、目标范围及重复目标在恢复时验证，损坏时失败关闭；这仍不是带校验/版本的抗撕裂日志。`tools/nvme_trace_audit.py` 可抓取一次实际 QEMU 9.2 NVMe 命令和块层 Flush 轨迹，检查回调串行、日志四阶段及漏 Flush 负控；其地址/计数检查并不读取日志头 payload 来证明目标与内容完全一致。五个软件故障切点与负控另行验证**有限进程重启模型**的恢复路径。二者都不能证明 Flush 在真实断电中的效果。

**兼容注意：**默认 virtio 驱动目前不协商 `VIRTIO_BLK_F_FLUSH`，`rootdisk_log_barrier()` 对其保持历史 no-op。此时 virtio 与 NVMe 的设备持久契约不同；历史结果不能直接当作严格等价持久对照。要获得公平的 virtio 持久组，必须实现并测试 feature 协商、Flush 请求、错误返回和日志顺序，或者明确只把 virtio 当作**非等价的历史功能/性能参照**。

## 4. 无 KV/CDC 直接 PMEM：分层、递归、链群与窗口

本节更新但不替代[固定格式、提交和恢复细节](THESIS-DIRECT-PMEM-DESIGN.md)。64 B 对象、八叉节点 `line_id[8] + expected_crc[8]`、目录/inode、两条独立缓存行上的原子根词、保护双根的引用计数与重建协议均保持不变。目录项/inode 是文件系统结构，不是 KV 记录；没有 CDC、内容去重、LSM 或 run manifest。

### 4.1 自相似路径复制，而非堆叠“分形树”

- 文件偏移先算 `line = offset / 64`，每层取 3 bit 定位八叉索引，形成从大范围到小范围的同构递归分解。inode 表采用相同思想。这个布局可称**自相似/递归分层**；它不是严格意义的 Bε 分形树，不宣称继承其渐近界或现成创新。
- 一次同步的多个脏行先按逻辑偏移聚合；共享前缀的每个索引节点在事务中只复制一次，再自底向上计算校验并写入。这把顺序窗口的空间局部性和根发布结合，避免对同一前缀重复 COW。
- 普通读以文件逻辑行直达 PMEM 数据；可选 DRAM 映射缓存带 inode 代数和根版本，失效后总能回退持久树。目录可选 DRAM 哈希必须核对完整名称；它们不是持久 KV 层。

### 4.2 链群：可重建的 64 行分配摘要

- 把连续 64 个 **64 B 软件单元**称为一组：组大小碰巧为 4 KiB，只是分配/位图摘要，不是 NVDIMM 必须按 4 KiB 写的块设备。每组维护 DRAM 位图与自由数，并用有界 intrusive 链将非满组组织起来；满组不在非满链，空/非满状态必须互斥。
- 可分配位必须排除两个有效根的可达对象、在途事务预留、尚受读者保护或等待安全退役的对象。位图和链只是摘要；分配在锁内核验，任何链/计数不一致都不能把受保护行分配出去。
- 更新成功发布新根后，才退役被替换槽的旧入边；共享父对象的物理子边只计一次。崩溃后从两个有效根重建引用计数、位图和链，丢弃未发布预留。首版不搬迁活对象、不需要 compaction；空间不足可退回离散行并安全 ENOSPC。
- 32 MiB 有 8192 组；一份每组 64-bit 位图约 64 KiB，32-bit 每行引用计数约 2 MiB，组链索引、预留、校验与遍历状态另计。上述是空间预算，不是已实现数据结构。

### 4.3 Stream window：有界脏缓存，不是 CDC stream

- 每个共享 inode 的 DRAM 工作状态用 4 KiB 窗口和 64-bit dirty mask 标记 64 个逻辑 64 B 行。完整行覆盖可省旧数据读；部分覆盖先合成正确完整行。读优先看共享 dirty 状态，随后回退已提交根。
- 一个窗口内把相邻逻辑行和相同树前缀批量处理；批量分配时优先连续物理行，无法连续时正确回退。`fsync` 的单次原子性属于**整个文件的本次候选根**，不能因为窗口分批而先发布半个文件根；超过事务内存/预留界限必须事先分段并明确短写或限制。
- 沿用每 inode 64 KiB、全系统 1 MiB **已分配脏页容量**作为首版建议上限，描述符另计。提前提交可能使未 `fsync` 写在崩溃后出现；成功的 `fsync` 前不能丢 dirty 状态。首版串行提交，避免并发写和窗口冻结语义不明。

### 4.4 有界动态规划：只优化候选分配，不碰提交语义

现有 `tools/check_window_allocator.py` 是**独立 Python 模型，不是内核实现或性能结果**。它为最多 4 个组、最多 64 个请求行，从最多 16 个连续空闲候选 run 中选择每个 run 的前缀：

```text
DP[i,k] = min_{0 ≤ x ≤ min(run_i 长度, k)}
          DP[i-1,k-x] + cost(run_i, x)
cost(x) = 0 (x=0), 否则 2 + (x < run_i 长度 ? 1 : 0)
```

该代价只偏好少开的连续区间及少量剩余后缀，是**可修改的软件代理目标**，并非真实时间或全局最优性能。DP 只在给定候选集合和前缀限制内最优；被 16-run 截断后若无法满足请求，完整 first-fit 扫描所有受保护位图，不得因优化上限误报 ENOSPC。选定结果必须在同一分配锁下预留，或重新加锁验证所有位后重试；不能以旧快照在并发下分配。

模型对两个 4 行组的全部 2304 个空闲图/需求组合与独立穷举对照；另验证候选截断后的安全回退，以及忽略旧根、在途预留、读者的 3 个负控。它不覆盖 C 实现、引用计数重建、崩溃并发或 QEMU。后续仅在连续空间/碎片数据证明值得时开启 DP；默认 first-fit 可用作消融，计算开销也计入前台时延。

### 4.5 CPU 底层优化与约束

1. 以 dirty bitmask 跳过未修改行，合并共享前缀的 COW 和连续源/目的拷贝；所有地址、长度、CRC 与溢出检查保持有效。固定尺寸小对象避免大块内核栈和不受限递归。
2. group 自由摘要允许位扫描；优先采用 RV64GC 上有明确回退的实现，若单独使用 Zbb/`ctz` 必须做编译器、QEMU CPU 能力及回退测试，不把可选指令当成基线必有。
3. DMA 门铃/完成使用 I/O fence；PMEM `flush + drain` 是不同协议，不能把普通 CPU fence 当成缓存行写回。`CBO.CLEAN` 需要对应扩展、权限、块大小和持久域假设；QEMU TCG 不模拟真实缓存层次。
4. 依次测 dirty mask、前缀合并、链群、DP、CRC 和并发；每项设置开关/对照，统计 CPU 时间、总软件写入、恢复工作和 p95，负收益保留，不预设“算法越多越快”。

## 5. 面向 QEMU 的可复验验收矩阵

**所有构建/格式化都在可丢弃工作副本中执行**：`make clean` 删除该目录的 `fs.img`，`pmformat --force` 会擦除 PMEM 文件。不得对原始用户镜像直接做破坏性实验。

| 组别 | 设备与文件系统 | 目的与当前限制 |
|---|---|---|
| A | 补丁 QEMU NVDIMM + 新无 KV/CDC 直接 PMEM FS | 题面主组；**文件系统尚未实现**，不能沿用 v3 的数据冒充 |
| B | QEMU NVMe + 原生 xv6 FS + 显式 NVMe Flush | 原生块设备对照；现为有限轮询驱动，报告 QD=1 |
| C | QEMU virtio + 原生 xv6 FS | 原行为兼容；目前未协商 Flush，仅可作不等价历史对照 |
| D | 补丁 NVDIMM + 既有 KV/CDC v3、可选 NVMe root | 回归与设备共存验证；**不计为 A 组实现或 A 组性能** |

建议先验证：

```sh
# 指向自行构建且应用仓库补丁的 QEMU 9.2，可丢弃副本内执行
python3 tools/check_storage_topology.py --qemu "$QEMU"
# 下面两个构建需要在不同的干净副本中执行
make DISK=nvme EXTRA_CFLAGS='-DXV6_LOG_FAULT_INJECT' kernel/kernel fs.img
python3 tools/xv6_log_recovery.py --backend nvme --qemu "$QEMU" --cpus 1
python3 tools/native_rename_recovery.py --backend nvme --qemu "$QEMU" --cpus 1
python3 tools/nvme_trace_audit.py --qemu "$QEMU" --output .ci-logs/nvme-trace
# 另一份干净副本：这是既有 v3 共存/恢复，不是新直接 PMEM FS
make DISK=nvme EXTRA_CFLAGS='-DPMEM_MAP -DPMEM_NVDIMM -DPMFS_FAULT_INJECT' kernel/kernel fs.img
python3 tools/pmfs_recovery.py --nvdimm --backend nvme --qemu "$QEMU" --cpus 3
# 性能须再用一个无故障注入的干净副本；结果仍是既有 v3，不是新直接 PMEM FS
make DISK=nvme EXTRA_CFLAGS='-DPMEM_MAP -DPMEM_NVDIMM' kernel/kernel fs.img
python3 tools/bench_perf.py --nvdimm --backend nvme --qemu "$QEMU" \
  --runs 10 --warmup 1 --samples 32 --sizes 64,1024 \
  --modes roundtrip,overwrite --output /tmp/nvdimm-nvme-perf
```

最终 A/B 实验应做到：

1. **设备与功能：**保存 QMP、FDT、`NVME_READY`、QEMU/补丁/guest/编译器哈希，核实镜像分别存在且不互相覆盖；对稀疏洞、截断、并发打开、rename、硬链接、unlink-open、满盘重启执行可重复 guest oracle。原生 xv6 的文件大小和 VFS 上限须记录，不能拿不支持的 1 MiB 原生文件充当对照。
2. **同步与崩溃：**注明 `write`、`fsync`、`close` 和命名空间确认点；A 记录实际 data/node/checkpoint/root 的 store/flush/drain，做提前写回模型、双根保护和漏刷/提前发布负控；B 检验各阶段写与 Flush 次序、5 个进程重启切点和负控，另列撕裂及重排未覆盖范围。不同设备物理承诺不要伪装成相同；至少统一用户可见成功操作及本模拟故障模型。
3. **性能：**固定数据、路径深度、文件大小、offset 序列、有效字节和成功次数，分别测 `write+fsync+readback` 延迟、实际计时的短批和稳态吞吐；跨独立启动至少 10 次，固定种子、预热、交替顺序，记录所有原始 ticks/单位、p50/p95、失败、锁等待、后台回收和内存占用。区分 A/B 的数据持久范围与原生全局日志同步；不能只报更快的那组。
4. **扩展与负结果：**空载与近满盘、长时间覆写、顺序/随机和热点/冷读、窗口/链群/DP/CRC 开关、QD=1 及后续真实队列并发分别报告；无收益、崩溃、ENOSPC 和内存代价保留。QEMU TCG/宿主负载影响和 host cache 选项要如实披露。

## 6. 下一步顺序与退出条件

1. 保持默认 virtio 全部回归，验证新 NVMe root 在 QEMU 的单核/三核文件读写、日志恢复与拒绝不支持设备；失败不能静默回退。
2. 在补丁 QEMU 中验证 NVDIMM 与 NVMe 同 VM 的设备身份与 guest 运行；该步只证明旧 v3 可共存。
3. 独立实现新 PMEM 格式、提交、恢复与 VFS 接入；先过功能和故障 oracle，再加入可关闭的窗口聚合、链群与 DP。保持旧 v3 代码及镜像不变。
4. 最后跑上述 A/B 匹配实验并更新论文。只有真实 A 组完成、原始数据及负控存在，才能作新的架构性能结论；真实硬件作为未来工作，而非当前验收阻塞。
