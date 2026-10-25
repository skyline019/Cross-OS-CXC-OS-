# CXC/OS 当前架构

## 1. 启动与地址空间

内核是 PE32+ 原生 UEFI application，使用 MinGW-w64 GCC/G++ freestanding 和 higher-half 链接。固件完成第一次装载重定位，内核完成第二次 DIR64 重定位，并在高 GDT/IDT 就绪后激活低半部分为空的 runtime CR3。

ACPI 所需 MADT 字段在初始页表仍有效时复制。direct map、内核 text/data、栈和 MMIO 分别执行既有权限策略。SMP 阶段只额外保留两个权限分离的低 SIPI 页，不恢复固件的其他低映射。

## 2. ACPI、Local APIC 与 SMP

MADT 提供逐处理器 APIC ID、LAPIC、IOAPIC 和 ISO 信息。xAPIC 使用高地址 cache-disabled MMIO，x2APIC 使用 MSR。两种模式共享 BSP PIT 校准、timer、LVT 和 EOI 逻辑。

BSP 通过真实 INIT-SIPI-SIPI 启动 AP。两个固件预留的低页在 runtime root 中分别以 RX、RW/NX identity-map，AP 经 16→32→64 位 trampoline 进入共享 higher-half CR3。每个 AP 有独立 32 KiB 高栈/guard 和 GS-based CPU block；LAPIC 初始化不重复 BSP calibration。

AP 首先进入 HLT 停车循环，再通过 vector 49 在普通上下文进入/退出每 CPU 调度，vector 48 保留启动及最终交付证据。M10 增加 vector-50 迁移唤醒和 vector-51 TLB shootdown。PMM/heap 已有 SMP 锁域；新建 VMM 映射和串口仍由 BSP/指定路径控制，详见 `smp-memory-migration.md`。

## 3. IOAPIC、Message IRQ 与 MSI

IOAPIC 初始化接管并 masked 全部 RTE。通用路由根据 GSI base 定位 controller/pin，按 ISO flags 设置 polarity/trigger，并写入目标 APIC ID。

IRQ registry 使用 vector 34–47 的静态 slot，并区分两类 source：

- **GSI**：注册写 IOAPIC RTE，拒绝重复 GSI；注销先 mask RTE；
- **Message**：只分配向量和 handler，不操作 RTE；设备驱动先关闭 MSI 再注销；
- handler 在 ISR 中不分配、不打印，设备 handler 先 acknowledge 设备，再由分发器统一 EOI；
- 当前不支持 shared IRQ。

PIT IRQ0 和 UART IRQ4 有真实 IOAPIC 中断回归；EDU 有真实 MSI 回归，e1000e 有三向量 MSI-X/PBA 回归。非重映射 message 中断当前要求目标 APIC ID 不超过 255。

## 4. UART

正常日志使用 polling COM1。loopback 测试临时修改 MCR/FIFO，启用 receiver-data IRQ，通过 IRQ4 的实际 GSI 接收 4 个特殊字节。测试期间禁止日志输出，结束后注销 IRQ 并恢复 polling 配置。

## 5. PCI、EDU DMA 与 e1000e 数据路径

PCI 配置机制 1 通过 `0xCF8/0xCFC` 枚举和读写。按 device 先检查 function 0，只扫描 multifunction 的其余 function。保存 class/vendor/device/header，并安全遍历 capability 链。BAR0 经高地址 RW/NX、cache-disabled 页映射访问。

EDU 驱动设置 bus mastering、INTx disable 和单向量 MSI message address/data。DMA 使用两个低于 256 MiB 的物理页，完成 RAM→EDU→RAM 的 256 字节往返；两次传输各收到一次完成 MSI。ISR acknowledge EDU IRQ status，主路径校验数据、目标页 guard、IRQ 计数和清理状态。

MSI-X 已支持通用 16 字节 table entry、per-vector/function mask 和 PBA 查询。e1000e 使用 IVAR 将 RXQ0/TXQ0/RXQ1 映射到稀疏 entry 0/1/4，三个独立 message slot 分别进入 IDT vector 34/35/36；直接交付和两种 mask 后 PBA 交付均有真实中断验证。ISR 用 ICS 无副作用读取、按 expected cause 清 ICR，并单独处理 QEMU EITR 空通知。

M11 在同一 MSI-X 配置后追加 queue 0 数据路径：8 项 legacy RX/TX rings、7 个 2048 字节 RX buffer 和一个 TX buffer，所有 DMA 页低于 4 GiB。BSP 配置 MAC/link/RCTL/TCTL/RXDCTL/TXDCTL/TARC0，真实接收并验证 60 字节请求，再用 `EOP|IFCS|RS` descriptor 发送确定响应。除内核检查 DD、head、内容和 RXQ0/TXQ0 MSI-X 外，Python dgram peer 必须实际收到并逐字节验证响应。清理先停 RX/TX 和 bus mastering，再释放页并核对 PMM 账目。详见 `e1000e-packet-dma.md`。

M12 复用同一 TX 提交/RX 等待路径，在 descriptor 1/2 上处理固定地址的 ARP 和 IPv4 ICMP Echo。内核检查 ARP 全字段/padding、IPv4/ICMP checksum、地址和分片字段，构造响应并重算 checksum；外部 peer 再验证 raw、ARP、ICMP 三个响应。辅助 TXQE/LSC 和已知 queue cause 的跨向量 EITR 观察有独立规则，未知 ICR 位仍失败。详见 `arp-ipv4-icmp.md`。

M13 从 RDH/TDH=3、RDT=7 继续处理十个 UDP Echo。每帧消费一个 RX descriptor，并将最早已消费的物理页 FIFO 发布到旧 RDT，再把 RDT 模 8 加一；八项 CPU 指针跟踪七个不同 RX pages，始终保留四个可用 descriptors。RX/TX head 和 tail 真实绕回，最终 heads=5/5、tails=1/5、回收=10。IPv4/UDP checksum、端口、变长 payload、奇数长度和 Ethernet padding 由内核与外部 peer 双重验证。M14 在同环追加 256 帧 stress（独立计数，wrap=32），不改写 M13 的 10/10。详见 `udp-ring-wrap.md`。

PCI ECAM、通用 BAR 资源分配、IOMMU、AHCI/NVMe、多队列网络驱动和完整设备驱动模型尚未实现。EDU/e1000e 测试均不被描述为完整存储或网络 stack。

## 6. 中断帧与调度

完整中断帧为 176 字节。分发器可返回另一线程的帧，由汇编恢复并 `iretq`。调度状态包括 Ready、Running、Sleeping、Waiting、Finished、Empty 和两阶段迁移状态；Finished 可回收为 Empty（M15），Empty 槽栈页可由 BSP 归还 PMM（M19）；Event/Mutex 可将 Waiting 置回 Ready。每个 LAPIC timer 驱动自己的 run queue。

每个 worker 使用独立高栈和 guard。固定槽位不压缩，迁移帧只有在源 CPU 切换离开后才发布到目标空槽。全局 IRQ-safe queue lock 以正确性为先，vector 50 只唤醒目标，实际选择仍由目标本地 timer 完成。BSP 预建全部线程，停止时 AP 返回停车循环。

## 7. 物理内存与堆

Inline-Extent PMM 支持连续页和局部合并；堆经 direct map 提供对齐、分裂、合并和清零。M10 为 PMM/heap 加入 IRQ-safe SMP 锁，锁顺序固定为 heap→PMM。多核 worker 实际执行 8 轮分配、交叉内容检查和释放，页数账目必须闭合。

受限 DMA 分配可从指定物理上限以下取页。若上限切开空闲 extent，高侧 tail 仍保留在降序链表中；EDU 测试释放页面后要求空闲页数精确恢复。

共享 runtime root 中已存在的 4 KiB PTE 可由 BSP 修改 writable 权限。每次修改执行本地及逐 AP `invlpg`、sequence/ack 和有界等待；专用只读写故障及恢复写验证两代 shootdown。新建映射仍由 BSP 准备。

## 8. 验证矩阵

- xAPIC 与 x2APIC；
- 1/2/4/8/16 vCPU，INIT-SIPI-SIPI、GS/CR3/APIC ID 和独立 AP 栈；
- 启动期及回归后的 targeted fixed IPI；
- 全 CPU live-worker barrier、本地 ticks/switches/finished 和 start/stop 控制 IPI；
- 跨核共享锁、critical-overlap 检查和每 worker 四次睡眠唤醒；
- 同一 worker 往返迁移、相同栈地址和 vector-50 目标唤醒；
- 全 CPU PMM/heap 八轮分配、页唯一性、内容和精确账目；
- 两代 PTE/TLB shootdown、逐 CPU ack、实际只读故障和恢复写；
- 64/128/256/512 MiB；
- IOAPIC PIT IRQ0 一次实中断；
- UART IRQ4 四次实中断和四字节校验；
- EDU MMIO liveness、一次直接 MSI、两次 DMA 完成 MSI；
- 256 字节 DMA 内容、目标页 guard、28 位地址上限和 PMM 页数恢复；
- e1000e 三个 MSI-X entry、独立 IDT vector、per-vector/function PBA；
- queue 0 RX/TX descriptor DD、RDH/TDH、60 字节 DMA 内容和 buffer guard；
- 真实 RXQ0/TXQ0 MSI-X，以及外部 peer 完整响应校验；
- ARP request/reply、IPv4/ICMP checksum 和 74 字节 Echo 双向验证；
- 十个 UDP Echo、IPv4/UDP pseudo-header checksum、1–33 字节 payload 和 padding；
- 七页 FIFO RX 回收、RX/TX head/tail wrap、最终 heads=5/5 与 tails=1/5；
- ICS/ICR acknowledge、EITR 空通知/跨向量观察、packet 页账目和 message 清理；
- PCI function/capability 解析与无 EDU/无 e1000e/各阶段无 peer/坏 ICMP 或 UDP checksum 负对照；
- IRQ 重复注册拒绝和 GSI/message 注销清理；
- UP 两个 BSP worker 的既有抢占/睡眠回退路径；
- Release 重复启动与 artifacts 双模式复验。

## 9. 后续

1. e1000e TX `send` 提交与通用 RX 路径（M24 已在 M13 selftest demux）、完整 reset/PHY 管理和多队列；
2. 调度/内存锁拆分、自动迁移和负载平衡；
3. 通用 BAR 资源管理、设备模型和中断重映射；
4. 条件等待、中断线程化和动态线程生命周期；
5. 用户地址空间、系统调用和 ELF。
