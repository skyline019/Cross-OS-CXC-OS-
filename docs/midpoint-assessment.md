# CXC/OS 第一次中场评估：进度、证据与技术债

> **第二次中场**（M14–M24 增量与债项刷新）见 [`midpoint-assessment-2.md`](midpoint-assessment-2.md)。本文保留 M0–M13 基线论述与 TD 登记原文；文末冲刺表仍有效，但「当前基线」以第二次中场为准。

日期：2026-10-25（初稿；后续债项冲刺有追加节）  
基线（初稿）：Milestone 13 / 冻结交付；债项冲刺后文档头部曾滚动到 M24，正式对照请用第二次中场。  
评估范围：源码结构、当前文档、构建/CI；M13 合同与后续冲刺实证。历史 artifacts SHA-256 仍以交付目录为准。

## 1. 结论摘要

CXC/OS 已经从一个原生 UEFI 启动项目推进为 **可在 QEMU/OVMF 中反复验证的 x86-64 SMP 内核与设备数据路径工程**。它已经具备真实的高半内核、内存管理、APIC/IOAPIC、SMP 启动、每 CPU 调度、受控迁移、TLB shootdown、PCI/MSI/MSI-X、DMA，以及 e1000e 上的 raw/ARP/ICMP/UDP 双向数据路径。

按已经定义的 **M0–M13 共 14 个里程碑**，完成度是 **14/14（100%）**。这不等于“通用操作系统完成 100%”：用户态、系统调用、ELF、存储/VFS、通用网络接口、生产级设备管理和 GUI 仍未实现或仍属后续范围。

当前状态可概括为：

- **定义内功能：完成且有较强实测证据。**
- **Linux + QEMU/OVMF 验证：强。**
- **Windows 跨宿主构建与远端 CI：配置存在，实证未闭环。**
- **内核基础架构：正确性优先，仍有明显静态容量与全局锁限制。**
- **网络与驱动：测试路径已通，不是生产级子系统。**
- **发布与文档：较完整，但完整发布矩阵仍主要靠手工命令，CI 只覆盖子集。**

当前没有已知的、处于冻结 M13 验收范围内的未决失败；这不表示没有潜在缺陷，也不扩大已验证范围。

## 2. 完成进度盘点

| 阶段 | 已完成能力 | 证据与状态 |
|---|---|---|
| M0–M4：启动与内核基础 | 原生 UEFI、MBR+FAT32 ESP、串口、PMM/heap、higher-half PE32+ 二次重定位、页权限、ACPI/MADT、xAPIC/x2APIC、IOAPIC、基础调度与 sleep/wake | Linux + QEMU/OVMF 多轮启动与回归完成 |
| M5–M7：设备中断与 DMA | PCI 枚举/capability、GSI/message IRQ 生命周期、UART 实中断、EDU MSI 和 256 字节 DMA、e1000e MSI-X table/PBA/IVAR 多向量 | 真实设备模型中断与 DMA，含清理和负对照 |
| M8–M10：SMP 与运行时内存同步 | INIT-SIPI-SIPI、per-CPU 状态、每 CPU run queue/timer、跨核 spinlock、PMM/heap SMP 锁、受控两阶段迁移、vector-50 唤醒、vector-51 TLB shootdown 与实际只读故障探针 | UP/2/4/8/16 vCPU 回归；最多 16 CPU 是当前明确边界 |
| M11：e1000e 原始包路径 | queue-0 legacy rings、低于 4 GiB 的 DMA 页、60 字节 raw 请求/响应、RXQ0/TXQ0 真实 MSI-X、外部 peer 验证 | 完成 |
| M12：最小协议路径 | 固定地址 ARP request/reply、IPv4/ICMP checksum、74 字节 ICMP Echo、跨向量 cause 处理 | 完成，独立协议统计和负对照保持 |
| M13：UDP 与 ring 生命周期 | 十个变长 UDP Echo、IPv4/UDP pseudo-header checksum、padding、七页 FIFO RX 回收、真实 RX/TX wrap | 完成；最终 heads=5/5、tails=1/5、wrap=1/1/2/1、recycle=10 |
| Windows 宿主构建 | MSYS2 UCRT64 toolchain 和 CMake 配置、Windows CI job | **仅配置；本环境未实际执行** |
| 远端 CI | Linux QEMU job 与 Windows build job 的 workflow | **仅配置；本环境无远端运行证据** |
| 通用 OS 能力 | 用户态、syscall、ELF、存储/VFS、通用 socket、生产级网络/驱动、GUI | 未实现或后续范围，不计入 M0–M13 的完成声明 |

## 3. 当前交付证据强度

M13 冻结交付包含：

- 全新严格 Debug 和 Release 构建，`-Wall -Wextra -Wpedantic -Werror`；
- 最终 Release 成功矩阵 **12/12**；
- 正式 ESP 直接成功复验 **4/4**；
- 正式 ESP 负对照 **7/7**，全部预期 panic 且无 PASS；
- Release 标准 `qemu-test` 通过；
- 独立解析并验证 13 个请求/响应，包含截断、追加、选定损坏和 checksum 负例；
- 23 个 artifact 文件，其中 22 个纳入 `SHA256SUMS`；本次复核 **22/22 OK**；
- Release EFI 为 224727 字节，ESP 为 68157440 字节。

尤其有价值的是：正常启动并不只依赖内核自报 PASS，还要求外部 peer 逐帧验证；设备缺失、无注入和坏 checksum 也有独立负对照。这一验收纪律应当保留。

## 4. 工程质量评估

| 维度 | 评价 | 说明 |
|---|---|---|
| 启动与底层机制真实性 | 强 | 不是 Linux 发行版、宿主兼容层或仅 boot hello；真实 UEFI、页表、SMP、IPI、DMA、MSI-X |
| 正确性验收 | 强 | Debug/Release、双 APIC、内存/CPU 矩阵、正式镜像复验、外部 peer、负对照、哈希 |
| 文档与问题追踪 | 强 | 当前架构、各子系统设计和 39 项实际踩坑记录较完整；历史里程碑与当前边界有区分 |
| 模块化 | 中 | 内存/APIC/PCI 等有模块边界，但 e1000e 驱动、协议、同步测试和统计仍高度耦合 |
| 可扩展性 | 中低 | 固定槽位、固定容量、全局 correctness-first 锁、单队列和 BSP 中心路径仍是主动取舍 |
| 跨宿主证据 | 未闭环 | Windows/MSYS2 与 CI 配置不能替代实际执行 |
| 生产驱动成熟度 | 早期 | 同步固定测试帧、有限初始化、无异常恢复/多队列/物理硬件验证 |
| 通用 OS 完整度 | 早期 | 尚无用户态、存储栈和通用应用运行环境 |

## 5. 技术债登记与优先级

### P0：应优先闭环的外部验收债务

#### TD-01：Windows 构建与远端 CI 没有实际执行证据

- **现状（部分还清）**：GitHub Actions 已上传 Linux/Windows 构建与 host-contract 证据产物（EFI、ESP、摘要与契约报告）。Windows job 仍只构建+主机契约，不跑 QEMU。本环境无远端 Actions 触发权，因此「产物上传已就绪」≠「远端 run 已发生」。
- **影响**：跨宿主仍缺一次真实 CI 运行日志；不能把 workflow 配置写成已通过。
- **建议**：在已连接的 GitHub 仓库触发一次 CI，保存 artifact；若声称 Windows 启动验证，需另行 QEMU/OVMF，不能由 build job 推断。

### P1：继续加功能前应降低的架构与验证债务

#### TD-02：NIC 驱动、协议实现和自检状态机耦合

- **现状（已拆层）**：协议纯逻辑在 `cxc::net`（`include/cxc/net/*`、`src/net/*`）；设备/MMIO/MSI-X 在 `src/e1000e.cpp`；包/ARP/ICMP/UDP 自检阶段在 `src/e1000e_selftest.cpp`，经 `e1000e_detail.hpp` 共享。Release 重建后 `qemu-test` 取得 `CXC_OS_SELF_TEST_PASS` 与 `peer_ok=1`。
- **影响**：协议扩展应优先改 `cxc::net`，避免再向单体驱动堆逻辑；自检 panic-on-failure 语义仍仅属测试路径。
- **建议**：M20–M24 已覆盖 net API 与 M13 RX demux；后续 TX 提交或通用 RX 路径再扩；生产丢包语义与自检失败语义继续分开。

#### TD-03：完整发布矩阵没有固化为一键、可重复执行的工具

- **现状（已还清入口）**：`tools/release_validate.py` 提供 `--profile host|ci|full`，CMake 有 `release-validate-host` / `release-validate`；README 已记录。`ci` 对齐 Linux workflow 子集，`full` 含干净构建、成功矩阵与 7 负对照并写 SHA256 manifest。
- **影响**：完整 `full` 仍偏长，日常以 host/ci 为主。
- **建议**：发布前跑 `--profile full`；CI 继续跑可承受子集。

#### TD-04：缺少主机侧协议/ring 单元测试与静态检查

- **现状（主补已完成）**：除 Python `contract_test --host-only` 外，`tests/host` 用普通 g++ 编译同一套 `cxc::net` 源并 `ctest`；CI Linux job 已接入。内核 freestanding 部分仍无 ASan，不能据此宣称 sanitizer 覆盖。
- **影响**：checksum/帧/ring 索引错误可在秒级发现；设备时序仍依赖 QEMU。
- **建议**：对可宿主编译模块逐步加 sanitizer；保持 QEMU 双向证据。

#### TD-05：ring 只验证了十次顺序请求/响应，没有长期和突发压力

- **现状（M14 / v0.15.0 主补）**：M13 十帧合同保持不变（`info.udp` 仍为 10/10）。其后同环追加 256 帧 UDP stress（`info.stress`），peer 等待 `udp-stress-ready` 并校验 256 回复；主机 `RingModel` 增加 256 稳定性与 4-slot 突发满环拒绝。默认 `qemu-test` 超时约 70s。仍无生产级 TX 背压、中断合并或可接受丢包语义。
- **影响**：确定性 wrap 与中等长度回收账目已有双向证据；尚不能宣称流控或长时间 soak。
- **建议**：再扩展到数千帧/异步路径前，先明确超时与丢包合同；生产驱动流控与自检“一帧一 IRQ”继续分开。

#### TD-06：线程生命周期和调度模型仍是静态自检模型

- **现状（M15–M19）**：Finished→Empty；Event/Mutex；M19 BSP `release_empty_stacks`（`unmap_kernel_stack` + PMM free，范围 TLB shootdown），recreate 可再 allocate/map。runtime：`released stack pages` 与 PMM/TLB 账目闭合。仍无 condvar、非 BSP 创建、优先级或 hotplug。
- **影响**：线程槽与栈页生命周期可闭环；尚非通用阻塞 IO 或驱动线程模型。
- **建议**：TD-06 主线可告一段落；condvar/阻塞 IO 或转 socket/存储另开切片（M20 已落地帧级 datagram API）。

#### TD-07：全局锁是正确性取舍，不是可扩展 SMP 设计

- **现状**：run queue 使用全局 IRQ-safe queue lock；PMM、heap 也各有全局锁。多核内容、互斥和账目已实测，但不代表性能可扩展。
- **影响**：CPU 数和分配/调度频率上升后，锁竞争会成为主要瓶颈；若先在锁内加入阻塞或更复杂路径，也会放大死锁/延迟风险。
- **建议**：先建立竞争与延迟基准，再考虑 per-CPU 队列、有序双队列锁或更细粒度 allocator；不要在缺乏基准时只做“看起来并行”的改写。

### P2：中期平台与生产化债务

#### TD-08：VMM 仍是共享内核、BSP 中心的受限模型

- **现状**：运行时权限更新仅限 BSP 发起的共享 runtime root 中已有 4 KiB PTE；新建映射由 BSP 准备；无通用并发页表编辑、用户 CR3、PCID、INVPCID、LA57 或 KASLR。旧固件下级表与低映像尚未安全回收。
- **影响**：不能支持独立用户地址空间，也限制大型运行时内存管理和安全隔离。
- **建议**：先定义地址空间对象、映射生命周期和 TLB 一致性合同，再扩展到用户 CR3；旧固件资源回收需单独证明没有残留引用。

#### TD-09：e1000e 初始化与 DMA 模型不是生产驱动

- **现状**：仅 queue 0 legacy descriptors、单 TX page 的同步测试路径、固定 MAC/link/RCTL/TCTL 初始化；无完整 reset/PHY 管理、生产级异常恢复、多队列/RSS、offload、interrupt remapping 或 IOMMU。精确“一帧一次 IRQ”是测试合同，不应当作通用硬件假设。
- **影响**：无法据此推断真实硬件、异常链路或高负载行为；DMA 也缺少通用隔离与设备资源管理。
- **建议**：保持当前最小路径作为回归，另设生产驱动能力清单；在物理硬件测试前，不删除严格校验和负对照。

#### TD-10：QEMU 特定行为与物理硬件之间仍有可移植性缺口

- **现状**：EITR 空通知、TXQE/LSC 辅助 cause 和跨向量观察已按 QEMU 源码处理并记录；只实测 QEMU 82574L。dgram peer 是测试后端，不是宿主网络兼容层。
- **影响**：这些规则在当前模型上有依据，但不能自动推广到全部硬件或其他 VMM。
- **建议**：把设备模型兼容逻辑与通用 ISR 合同分开文档化；未来增加另一种 e1000e 实现或物理 NIC 验证，不放宽未知 cause 的失败规则。

#### TD-11：PCI/IRQ 和虚拟资源管理仍有固定容量

- **现状**：PCI 仅机制 1，无 ECAM、通用 BAR 资源分配和完整设备模型；IRQ 为固定向量/slot，不支持 shared IRQ 或中断线程化；MSI/MSI-X 使用 BSP 物理目标且 APIC ID 不超过 255。
- **影响**：增加多个设备或更大拓扑时需要重新处理资源分配、冲突和中断归属。
- **建议**：在设备数量增加前引入资源所有权和虚拟映射管理；不要继续为每个驱动增加硬编码 MMIO 地址或专用 IRQ 假设。

#### TD-12：测试 runner 本身也在变大

- **现状（已拆分）**：`network_peer.py`（帧/validators/DatagramPeer）、`qemu_runner.py`（固件与进程生命周期）、`run_qemu.py`（薄 CLI，re-export 以兼容 `contract_test`）。peer 对非合同帧（如部分 QEMU/OVMF 组合上的 IPv6 ND）改为忽略并继续等待，避免环境噪声误杀。仍仅依赖 Python 标准库。
- **影响**：协议与进程生命周期可独立演进；contract_test 导入路径不变。
- **建议**：新增协议时优先扩展 `network_peer`；矩阵入口继续走 `release_validate.py`。

### 开放问题与已修复问题必须分开

- **仍开放**：早期并行 QEMU 曾出现 UART panic，根因未确定；顺序矩阵未复现，不能宣称已经修复。正式验证目前继续使用顺序执行。
- **仍开放**：Windows 本地构建和远端 CI 无实际执行证据。
- **已修复，不再列为当前缺陷**：M13 descriptor 7 指针数组越界、ring 页未显式清零、RDT 初始值误解，以及把合法的 ready 后即时 DD 当成 stale 状态。相关早期开发日志不作为正式证据。

## 6. 不应误称为“技术债”的后续功能缺口

以下主要是尚未进入范围的路线图，而不是已有实现偷工减料：

- 用户态、系统调用、ELF loader、按需分页和独立用户地址空间；
- 存储设备、block layer、VFS/文件系统；
- e1000e TX 提交、通用 RX demux（非仅 selftest）、ephemeral 端口、DHCP/DNS、路由、分片重组、TCP/IPv6；
- 完整 PCI 设备模型、IOMMU、更多生产驱动；
- 自动负载平衡、通用 blocking IO、中断线程化；
- GUI（仍按原始约束属于未来工作）。

这些缺口说明项目仍处于内核基础与设备验证阶段，不应为了“功能数量”跳过当前优先级更高的验证和模块化工作。

## 7. 建议的下一阶段顺序

### 第一优先：证据闭环

1. 实际执行 Windows 构建/CI，并保存结果；
2. 把完整发布矩阵和负对照固化为受版本控制的工具；
3. 将协议和 ring 纯逻辑纳入主机侧单元测试。

### 第二优先：在不改变外部行为的情况下解耦

1. 分离 e1000e、协议与自检状态机；
2. 拆分 QEMU runner；
3. 每次重构都重新执行完整 M13 回归，不用“编译通过”替代数据路径证据。

### 第三优先：增加真正的新通用能力

1. 动态线程生命周期、等待队列和 blocking IO 基础；
2. ring 长稳/突发测试与更通用 buffer 管理；
3. 再选择通用 UDP/socket、存储/VFS 或用户态中的一条主线，避免同时扩大多个子系统。

**推荐判断**：下一阶段先做“验证工程 + 网络解耦”比直接叠加 DHCP、TCP 或 socket 更能降低累计风险。若必须选择新功能，也应先完成 TD-02 的最小拆分。

## 8. 最终评价

项目最强的地方不是功能列表，而是已经形成了 **真实机制、双向外部证据、负对照、干净构建、正式镜像复验和哈希交付** 的工程闭环。M0–M13 的定义内目标已经完成。

当前最大的风险也不是某个已知崩溃，而是：如果把同步自检路径误当成生产子系统、继续向单体驱动叠加协议，或长期不执行 Windows/CI，后续每个里程碑的验证成本和不透明性都会快速上升。

因此，中场建议是：**保持验收标准不降级，先补跨宿主/CI 证据和发布自动化，再做网络与线程模型解耦，然后进入更通用的 OS 能力。**

## 9. 中场测试补强后的状态更新

随后已新增 `tools/contract_test.py`、`docs/test-contract.md` 和独立 `contract-results/`，实际完成主机 7/7、QEMU 37/37：

- TD-03 已有受版本控制的完整契约 runner 和 CI 配置，但远端 CI 仍未实际执行，发布干净构建/镜像生产仍可继续自动化；
- TD-04 已有 checksum、frame、ring 和故障点的主机模型测试，但 freestanding 内核静态分析/ sanitizer 覆盖仍不能据此宣称；
- TD-05 仍需真实 NIC 的长时间、突发和背压测试；主机 10000 次 ring 模型与五次完整启动循环不替代该项；
- 新增七次 QEMU SIGKILL 硬掉电和干净启动恢复，但当前无持久化状态，不能称为掉电事务恢复；
- Windows/CI 实证、网络解耦、动态线程和生产驱动等其余债务优先级不变。

## 10. 技术债还清冲刺（2026-10-25）状态更新

本轮按「证据闭环 → 行为不变解耦」落地，**未降低** PASS/`peer_ok=1` 验收标准：

| ID | 本轮结论 |
|---|---|
| TD-01 | 部分：CI 已上传 Linux/Windows 证据 artifact；远端真实 run 仍待仓库触发 |
| TD-02 | 完成拆层：`cxc::net` + `e1000e` + `e1000e_selftest`；本地 Release `qemu-test` PASS |
| TD-03 | 完成：`tools/release_validate.py`（host/ci/full）+ CMake/README |
| TD-04 | 完成主补：`tests/host` C++ 单测 + CI；sanitizer 仍未宣称 |
| TD-12 | 完成：`network_peer` / `qemu_runner` / 薄 `run_qemu`；忽略链路噪声帧 |
| TD-05+ / P2 | 未做，优先级不变 |

本地实证：`contract_test --host-only` 7/7；`ctest` host net PASS；Release 重建后串口 `CXC_OS_SELF_TEST_PASS` 且 `peer_ok=1`。

## 11. 指向第二次中场

M14–M24（stress、线程生命周期、socket API、M13 RX demux）的完整盘点、债项刷新与下一步主线建议，见 [`midpoint-assessment-2.md`](midpoint-assessment-2.md)。
