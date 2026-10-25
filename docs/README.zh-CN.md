# CXC/OS

CXC/OS 是一个从 **原生 UEFI** 启动的独立 x86-64 内核工程。宿主机只负责构建和 QEMU 测试，不依赖 Windows/Linux 内核运行。

当前为 **Milestone 37 / v0.38.0：ring-3 TSS/int80 探针（A2 脊柱）**（M13–M36 合同保持）。已经在 QEMU + OVMF 中实际启动并到达：

```text
CXC_OS_SELF_TEST_PASS
```

## 当前能力

| 模块 | 能力 |
|---|---|
| UEFI | LoadedImage、ACPI、内存图/map-key 重试、低 SIPI 页预留 |
| 内核映像 | PE32+ higher-half、DIR64 二次重定位、严格 MinGW-w64 构建 |
| 页表权限 | text RX、data/栈 NX、MMIO cache-disabled、栈 guard、SIPI code/data 分离 |
| ACPI | RSDP/XSDT/RSDT checksum，MADT processor ID/LAPIC/IOAPIC/ISO |
| SMP 启动 | MADT 枚举、真实 INIT-SIPI-SIPI、16→32→64 位 AP trampoline |
| Per-CPU | GS-based CPU block、APIC ID/CR3/状态、独立 32 KiB AP 高栈 |
| 每 CPU 调度 | 独立 run queue/current/tick/switch/finished、本地抢占与睡眠 |
| 线程迁移 | 两阶段 saved-frame 交接、同一线程跨 CPU 恢复、固定槽位不压缩 |
| SMP 同步 | 跨核原子 spinlock、critical-overlap 检测、共享计数实测 |
| SMP PMM/heap | IRQ-safe allocator 锁、heap→PMM 锁序、多核分配/内容/账目回归 |
| 运行时 TLB | 4 KiB PTE 权限更新、逐 CPU `invlpg`/ack、只读写故障探针 |
| IPI | 48 交付证据、49 scheduler 控制、50 迁移唤醒、51 TLB shootdown |
| Inline-Extent PMM | 连续分配、局部合并、物理上限以下的 DMA 页分配 |
| Local APIC | xAPIC MMIO/x2APIC MSR，BSP 校准和所有在线 CPU 的本地 timer |
| IOAPIC | 全 RTE 接管、通用 GSI route/mask、物理目标 APIC |
| 外部 IRQ | vector 34–47 静态注册，区分 IOAPIC GSI 与 message source |
| UART | COM1 polling 日志及 16550 loopback IRQ4 实中断测试 |
| PCI | 机制 1 枚举、配置读写、BAR 读取和安全 capability 遍历 |
| MSI | EDU message address/data、单向量直接 MSI 实际交付与注销 |
| DMA | EDU RAM→设备→RAM 256 字节往返、两次完成 MSI、越界保护检查 |
| MSI-X | table/PBA 映射、entry address/data/mask、function mask |
| 多向量验证 | e1000e entry 0/1/4→独立 IDT 向量，direct/PBA 后交付均实测 |
| e1000e rings | queue 0 各 8 项 legacy RX/TX descriptors、模 8 head/tail、DD write-back |
| 动态 RX 回收 | 七个物理页在八项 ring 中 FIFO 复用，真实 RX/TX wrap 和 guard 检查 |
| 双向包 DMA | 60 字节已知请求/响应、真实 RXQ0/TXQ0 MSI-X、DMA 页精确清理 |
| 外部网络证据 | Python 跨平台 UDP dgram peer 注入并逐字节验证实际响应 |
| ARP | 固定 IPv4 邻居的 request 校验、reply 构造及 padding 检查 |
| IPv4 / ICMP | header/message checksum、Echo Request 校验、Echo Reply 重算与外部验证 |
| UDP Echo | 十个 1–33 字节 payload、pseudo-header checksum、端口/长度/padding 验证 |
| UDP stress (M14) | M13 后同环 256 帧；独立 `UdpStressResult`；wrap=32；peer `m14-…-stress` |
| 线程回收 (M15) | Finished→Empty；栈映射复用；运行中 BSP recreate；finished=reclaimed |
| Event (M16/M17) | sticky `wait`/`signal` + FIFO 队列/`broadcast`；串口 `3/1/1/3` |
| Mutex (M18) | worker `mutex_lock`/`unlock`；争用走 Event 队列；串口 lock/unlock/wait |
| 栈页归还 (M19) | BSP `release_empty_stacks` unmap+free；recreate 再 map；串口 pages/TLB |
| UDP datagram (M20) | `parse_udp_datagram` / `build_udp_datagram`；echo 复用；host 往返测 |
| UDP socket (M21) | 固定 4 槽 bind/close；每槽 RX 深 4；`udp_deliver`/`recv`；串口合同 |
| UDP recv_wait (M22) | 每槽 `rx_gate` Event；`udp_recv_wait`；deliver 唤醒；worker 合同 |
| UDP send (M23) | `udp_send` 填 Ethernet/UDP 帧；不提交驱动；host/runtime 往返 |
| UDP demux (M24) | M13 每帧 `udp_deliver`+`recv`；串口 10/10；不改 Echo/peer |
| UDP socket TX (M25) | stress 后 `udp_send`→e1000e TX；独立 `socket_tx` 计数；不改 Echo/peer |
| UDP stress demux (M26) | M14 256 帧 `deliver`+`recv`；独立 `stress.socket_*` 256/256 |
| UDP TX backpressure (M27) | leave-one-empty 满环拒绝；`posted:7 rejects:1 drained:1`；不改 M25 串口 |
| UDP TX wait (M28) | 满环后 `post_tx_wait`；`waits:1 posted:1 drained:1`；不改 M25/M27 串口 |
| UDP IRQ RX demux (M29) | RXQ0 MSI-X 内 `udp_deliver`；BSP 仅 recv；`delivered:266`；M24/M26 串口不变 |
| UDP IRQ RX echo (M30) | IRQ 内 Echo 构造+TX post；BSP 等 DD；`deliver/echo:266/266` |
| UDP IRQ RDT recycle (M31) | IRQ 内 FIFO 页回收+RDT；BSP 仅确认；`deliver/echo/recycle:266/266/266` |
| Event TX wait (M32) | TXQ0→Event sticky；`post_tx_wait` 经 `event_try_consume`；`events:1`；Event 合同改 delta |
| Generic RX submit (M33) | `try_submit_rx_dd`（无 Echo/TestInfo）；独立 `submitted:266`；不改 M13/`266/266/266` |
| Third midfield (M34) | [`midpoint-assessment-3.md`](midpoint-assessment-3.md)；阶段 0 收束；主线改 A1 |
| User AS ADR (M35) | [`adr-user-address-space.md`](adr-user-address-space.md)；共享内核高半；每线程内核栈；M36 起 BSP CR3 探针 |
| User AS probe (M36) | `create`/`map_user_page`/CR3 往返；`cr3_switch/write/read/user_bit:1`；仍 CPL0 |
| Ring-3 probe (M37) | GDT 用户选择子 + TSS.RSP0；`iretq`→CPL3→`int 0x80`；`enter/int80/cpl/rsp0` |
| 中断帧 | 完整 176 字节，timer/IOAPIC/MSI/MSI-X/IPI/spurious 分发 |
| 启动盘 | Python 标准库生成 MBR + FAT32 ESP |

文档：

- [`docs/phase1-assessment-roadmap.md`](docs/phase1-assessment-roadmap.md)：一期总评、与生产级差距、A1 编译器可跑 / 生产级内核路线图；
- [`docs/adr-user-address-space.md`](docs/adr-user-address-space.md)：A1 用户 CR3 / 内核映射 / syscall 入口栈 ADR（M35）；
- [`docs/midfield-cadence.md`](docs/midfield-cadence.md)：每 10 阶段中场 + CI/测试同步契约；
- [`docs/midpoint-assessment-3.md`](docs/midpoint-assessment-3.md)：第三次中场评估（M25–M34、主线改 A1）；
- [`docs/midpoint-assessment-2.md`](docs/midpoint-assessment-2.md)：第二次中场评估（M14–M24、债项刷新与下一步）；
- [`docs/midpoint-assessment.md`](docs/midpoint-assessment.md)：第一次中场评估与 TD 登记（M0–M13 基线）；
- [`docs/from-zero-pitfalls.md`](docs/from-zero-pitfalls.md)：从零重建与持续踩坑记录；
- [`docs/smp-memory-migration.md`](docs/smp-memory-migration.md)：M10 锁域、两阶段迁移、TLB shootdown 和故障探针；
- [`docs/smp-scheduler.md`](docs/smp-scheduler.md)：M9 每 CPU run queue、共享锁和本地 timer；
- [`docs/smp-startup.md`](docs/smp-startup.md)：trampoline、GS CPU block、SIPI/IPI 和停车循环；
- [`docs/test-contract.md`](docs/test-contract.md)：中场正确/失败/掉电/事务/循环/高压/混沌测试契约与实测；
- [`docs/udp-ring-wrap.md`](docs/udp-ring-wrap.md)：M13–M14 UDP/ring、M20–M25 socket 栈、RX demux 与 TX submit；
- [`docs/arp-ipv4-icmp.md`](docs/arp-ipv4-icmp.md)：M12 ARP/IPv4/ICMP、checksum、跨向量 cause 与负对照；
- [`docs/e1000e-packet-dma.md`](docs/e1000e-packet-dma.md)：M11 queue 0 rings、双向 DMA、peer 协议与清理；
- [`docs/msix-e1000e.md`](docs/msix-e1000e.md)：M7 MSI-X table/PBA、IVAR、多向量和节流处理基线；
- [`docs/edu-msi-dma.md`](docs/edu-msi-dma.md)：EDU BAR、MSI、DMA、清理顺序和实测方法；
- [`docs/pci-irq.md`](docs/pci-irq.md)：PCI、GSI/message IRQ 生命周期和 UART loopback；
- [`docs/acpi-ioapic.md`](docs/acpi-ioapic.md)：MADT、x2APIC、IOAPIC；
- [`docs/interrupts-scheduler.md`](docs/interrupts-scheduler.md)：中断帧、每 CPU 调度、迁移和同步；
- [`docs/higher-half.md`](docs/higher-half.md)：二次重定位与权限；
- [`docs/algorithms.md`](docs/algorithms.md)：PMM、受限 DMA 分配和内存例程。

## 当前实测

同一 Release EFI 已在 4 vCPU 的 xAPIC 连续 3 次、x2APIC，以及 64/128/256/512 MiB 矩阵中通过；另通过 1/2/8/16 vCPU 拓扑。QEMU 默认使用 4 vCPU，并附加 EDU 和 e1000e。

- 默认 SMP：4/4 online，4 个 CPU 都有 live worker，5 个 worker 全部完成；
- 默认共享数据：counter=160、20 次本地 sleep/wake、critical overlap=0；
- 默认迁移：同一 worker 完成 CPU0→CPU1→CPU0 两次迁移，wake IPI 各一次；
- 默认内存：8 轮共 40 次 PMM、40 次 heap 分配/交叉检查/释放，账目闭合；
- 默认 TLB：2 次 shootdown、5 个实际只读写故障、每 CPU 2 次 invalidation，恢复写全部成功；
- 16 vCPU：17 个 worker、136 次 PMM、136 次 heap、17 个实际只读故障、2 次迁移；
- 单 CPU：2 个 worker、16 次 PMM/heap、2 个只读故障、无迁移，仍有 2 次本地 invalidation；
- 调度停止后 AP 返回停车循环，vector-48 复验继续通过；
- M11 原始帧保持 RX/TX 各 1 帧、60/60 字节、RDH/TDH=1/1、真实 RXQ0/TXQ0 MSI-X 各一次；
- M12 保持 ARP request/reply=1/1（60/60 字节）和 ICMP Echo request/reply=1/1（74/74 字节），协议阶段 head=3/3、IRQ=2/2；
- M13 再完成十个 UDP Echo request/reply，payload=148、RX/TX 各 642 字节、UDP IRQ=10/10；
- M14 在同环追加 256 帧 stress（IRQ/recycle=256，wrap=32），不改写 M13 串口 10/10；
- 最终 RX/TX heads=5/5、tails=1/5，wrap RXh/TXh/RXt/TXt=1/1/2/1，七页 FIFO 回收 10 次；
- 独立 peer 实际收到并逐字段验证 13 个响应，`network-peer.log` 记录全部 hex、`udp_replies_validated=10`、`peer_ok=1`；
- IOAPIC PIT、UART IRQ4、EDU MSI/DMA、e1000e 三向量 MSI-X/PBA 全部保持通过；
- GSI/MSI/MSI-X 注销后 active slot=0、unhandled=0、spurious IRQ=0，packet 页释放前后 PMM 账目闭合；
- `--no-network-peer`、`--no-protocol-peer`、`--bad-icmp-checksum`、`--no-udp-peer` 和 `--bad-udp-checksum` 分别覆盖各阶段失败；设备缺失负对照也保持，均不输出 PASS。

这些结果证明受控迁移、分配器互斥、本代 TLB 失效正确性、queue 0 真实 descriptor DMA、最小 ARP/ICMP/UDP 数据路径和受控 ring 回收；不表示已实现自动负载平衡、可扩展锁、通用并发 VMM、多队列网卡驱动或完整网络协议栈。

## 中场测试契约

`tools/contract_test.py` 覆盖正确、失败路径、硬掉电、复合、嵌套、循环、能力矩阵、复杂场景、事务、高压和确定性混沌测试。主机侧现为 **9/9**（含 256 帧稳定性与突发满环）；默认 `qemu-test` 要求 M14 stress 与 `peer_ok=1`：

- 6688 个单比特损坏、875 个截断/扩展、60 个隔离 UDP 故障 fixture；
- 主机 ring 模型 100×100 嵌套、256 帧稳定性、4-slot 突发拒绝；
- 七类 UDP 故障可指定第 0–9 帧，验证前序提交与故障帧无部分 reply/recycle；
- 历史完整矩阵曾为 QEMU 37/37；M14 后默认成功路径为 269 帧 peer（含 256 stress）。

```bash
python3 tools/contract_test.py --image artifacts/cxc-os.img \
  --work-dir contract-results --seed 20261025
python3 tools/contract_test.py --host-only \
  --work-dir build/contract-host --seed 20261025
```

掉电仅指 QEMU 进程硬断电及后续干净启动，不宣称持久化数据恢复；主机 ring 模型也不冒充 10000 帧真实 NIC 流量。详见 [`docs/test-contract.md`](docs/test-contract.md) 和 `contract-results/CONTRACT_VALIDATION.txt`。

## 构建与运行

### Linux

```bash
sudo apt-get install \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
  cmake python3 qemu-system-x86 ovmf

cmake -S . -B build/uefi \
  -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-uefi-mingw.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/uefi --parallel
cmake --build build/uefi --target qemu-test
```

发布验证入口（封装主机契约 / CI 子集 / 完整矩阵）：

```bash
python3 tools/release_validate.py --profile host \
  --work-dir build/release-validate-host
cmake --build build/uefi --target release-validate-host
# 对齐 CI 的 QEMU 子集（需已构建的 ESP）：
python3 tools/release_validate.py --profile ci \
  --image build/uefi/cxc-os.img --work-dir build/release-validate
# 干净构建 + M13 成功矩阵 + 7 负对照：
python3 tools/release_validate.py --profile full \
  --work-dir build/release-validate-full
```

主机侧协议/ring C++ 单测（普通 g++，不依赖 MinGW UEFI toolchain）：

```bash
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests --parallel
ctest --test-dir build/host-tests --output-on-failure
```

双 LAPIC、CPU 拓扑与 DMA 边界：

```bash
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 4 --cpu "qemu64,-x2apic" --work-dir build/test-xapic
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 4 --cpu "qemu64,+x2apic" --work-dir build/test-x2apic
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 1 --cpu "qemu64,+x2apic" --work-dir build/test-up
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 16 --cpu "qemu64,+x2apic" --work-dir build/test-smp16
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 4 --cpu "qemu64,+x2apic" --memory 512M \
  --work-dir build/test-512m
```

无注入负对照（预期 panic，命令成功表示捕获到该预期）：

```bash
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --no-network-peer --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-no-peer
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --no-protocol-peer --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-no-protocol
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --bad-icmp-checksum --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-bad-icmp
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --no-udp-peer --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-no-udp
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --bad-udp-checksum --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-bad-udp
```

正常 `--check` 的成功条件是串口 `CXC_OS_SELF_TEST_PASS` 和 `peer_ok=1` 同时成立；日志位于各自 work directory 的 `serial.log`、`network-peer.log`。

### Windows

在 **MSYS2 UCRT64** 终端：

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-python

cmake -S . -B build/uefi -G "MinGW Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-uefi-mingw.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/uefi --parallel
```

Windows 构建由 CI 配置覆盖；当前本机实测为 Linux + QEMU/OVMF，未把 CI 配置写成已经执行的结果。

## 当前边界

- 迁移由 worker 显式请求，任意时刻仅一个在途；没有自动迁移、work stealing 或负载平衡；
- 全局 run-queue/allocator 锁以正确性为先，不宣称可扩展性能；
- 线程仍由 BSP 创建；M15–M19 支持 Finished→Empty、可选栈页归还与再 map；尚无非 BSP 创建；
- M16–M18 有 Event 队列与非递归 worker Mutex；M22 将 Event 接到 UDP `recv_wait`；仍无 condvar、优先级继承或通用阻塞 IO；
- 运行时 VMM 仅支持 BSP 发起、共享内核 root 中已存在 4 KiB PTE 的权限更新；
- 新建映射仍由 BSP 准备，没有通用并发页表编辑、用户 CR3、PCID 或 INVPCID；
- 每 CPU 固定三个 slots，无通用阻塞 IO 或 CPU hotplug；
- 当前最多 16 个 enabled processors，32 位 AP 过渡要求共享 CR3 低于 4 GiB；
- IRQ 注册固定容量，不支持 shared IRQ 或中断线程化；
- MSI/MSI-X 设备测试仍使用 BSP 物理目标，APIC ID 不超过 255；
- e1000e 仅限 queue 0 legacy ring、七页 FIFO 回收和测试所需 MAC/link/RCTL/TCTL 初始化；没有完整 reset/PHY 管理、多队列、offload 或生产级异常恢复；
- M20–M34：datagram/socket/recv_wait/send + M13/M14 IRQ demux/Echo/RDT recycle + generic RX submit + socket TX 提交/背压/Event wait；第三次中场裁定下一主线为 A1 用户态；无 ephemeral 端口；协议仍为固定地址 ARP/ICMP/UDP Echo；
- 包测试使用 EtherType `0x88B5`、ARP/ICMP/UDP 和 QEMU 本机 dgram peer，未实测物理网卡；
- EDU DMA 不含 AHCI/block IO、IOMMU 或用户缓冲区 pinning；
- PCI 仅机制 1，不含 ECAM、通用 BAR 资源分配或完整设备驱动模型；
- 尚无用户态、系统调用、ELF、按需分页和 GUI。

**组织目标**：① **A2** 不依赖宿主的机上完整编译器；② **B** 生产级 OS。当前切片走 A1 脊柱（下一刀静态 ELF + syscall v0 → 可写 FS）。须保持 M13–M37 回归证据。
