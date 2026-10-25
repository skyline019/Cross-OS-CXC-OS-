# SMP 启动、Per-CPU Block 与 IPI

## 1. 本轮范围

Milestone 8 将内核从“仅使用 BSP”推进到 **真实 AP 启动与每 CPU 状态**：

- 从 MADT Local APIC/x2APIC entries 保存每个已启用处理器的 APIC ID；
- BSP 通过 INIT-SIPI-SIPI 逐个启动 AP；
- AP 经实模式、保护模式进入共享的 higher-half 内核；
- 每个 AP 有独立 32 KiB RW/NX 高栈和未映射 guard page；
- 每个 CPU 有独立的 GS-based `CpuBlock`；
- AP 初始化自己的 LAPIC，接收 targeted fixed IPI；
- AP 首先停在 `sti; hlt` 循环；M9 可由此进入/退出每 CPU 调度。

本轮**不是 SMP 调度器**。BSP 仍独占 PMM/VMM、堆、设备测试、串口日志和当时的线程调度；AP 不执行这些全局子系统。M9 已在此停车循环上加入可进入/退出的每 CPU 调度，见 `smp-scheduler.md`。

## 2. MADT 与固定容量

`acpi::ProcessorDescriptor` 保存 APIC ID、firmware UID 和 MADT type。当前最多 16 个已启用处理器，重复 APIC ID 或超出容量会明确失败。

仅 flags bit 0 的 enabled entries 会进入启动列表。online-capable 但当前 disabled 的处理器需要另外的固件启用协议，不能直接当作普通 wait-for-SIPI CPU。

运行时将 BSP 放在 CPU index 0，其余 AP 按 MADT 顺序排列。BSP 的硬件 APIC ID 必须能在 MADT 中找到。

## 3. 两页低内存 trampoline

### 固件分配

在最终 `GetMemoryMap`/`ExitBootServices` 之前，内核用 `AllocateMaxAddress` 申请两个连续的 `EfiLoaderData` 页面，整体低于 1 MiB。它们不进入 Inline-Extent PMM，也不会被后续普通页分配复用。

实测 trampoline code page 为 `0x9E000`，参数页为 `0x9F000`；具体地址由固件决定，不作硬编码。

### 独立 flat binary

`src/smp_trampoline.S` 不由 PE 直接链接执行，而是经过：

1. GNU assembler 生成 COFF object；
2. GNU objcopy 抽取 `.smp_trampoline` 为 flat binary；
3. Python 生成 C++ byte array；
4. BSP 在低固件映射仍存在时复制到 SIPI page。

blob 当前为 504 字节，运行时还必须满足单页容量和逐字节复制校验。

### 权限

共享 runtime CR3 中只增加两个低 identity mappings：

- code page：**RX**；
- 下一页参数/临时栈：**RW/NX**。

代码不需要运行时自我修补，也没有 RWX 映射。AP 的 32 位过渡栈位于参数页，高半入口前切换到该 AP 的独立高栈。

## 4. 16→32→64 位进入路径

SIPI 以 `CS=code_page>>4, IP=0` 进入。trampoline：

1. 从 CS 计算 code page 物理基址并保存在 EBP；
2. 将 DS 指向后续参数页，加载 BSP 写入的 GDTR；
3. 设置 CR0.PE，经参数页中的 far pointer 进入 32 位代码；
4. 加载 BSP 的 CR4、共享 CR3 和 IA32_EFER；
5. 恢复 CR0 并开启 paging，低 code/data identity mappings 保证连续取指；
6. 经 64 位 code descriptor 进入 long mode；
7. 读取高栈顶、CPU index 和 higher-half C++ entry，调用 AP 主入口。

当前 32 位过渡要求共享 CR3 物理地址低于 4 GiB；超过该边界会明确失败。现有 64–512 MiB 实测均满足此约束。

## 5. Per-CPU Block 与 AP 停车

每个 cache-line-aligned `CpuBlock` 包含：

- self pointer、CPU index、APIC ID；
- online/parked/error 状态；
- 进入高半后的 CR3；
- 独立栈物理/虚拟基址；
- IPI payload、调用计数和最后接收值。

BSP 和每个 AP 都将自己的 block 地址写入 `IA32_GS_BASE`。AP 必须通过 `movl %gs:8` 读回自己的 index，并验证 GS base、CR3 和 LAPIC ID。

AP 不重复 PIT calibration，不启动 local timer，也不调用 `scheduler::start`。它只加载共享 GDT/IDT、初始化相同 xAPIC/x2APIC 模式的 LAPIC，然后发布 parked/online 并打开 IF 等待 IPI。

## 6. INIT-SIPI-SIPI 与 IPI

每个 AP 顺序启动：

1. targeted physical INIT IPI；
2. PIT channel 2 等待 10 ms；
3. targeted SIPI，vector 为 trampoline page number；
4. 等待 200 μs；
5. 只有尚未 online 且无错误时才发送第二个 SIPI；
6. 最长约 20 ms 等待 online/error 状态。

xAPIC 使用 ICR high/low 并等待 Delivery Status；x2APIC 使用一次 `0x830` MSR 写，目标 APIC ID 位于高 32 位，不错误等待不存在的 Delivery Status bit。

fixed IPI 使用独立 **IDT vector 48**，与 IOAPIC/PCI 的 34–47 注册范围分离。每个 AP 启动后接收两次不同 payload，所有 EDU/MSI-X/scheduler 回归结束后再接收第三次。handler 通过 GS 找到自己的 block，只更新 payload/counter；dispatcher 统一 EOI。

M9 的 scheduler start/stop 改用 vector 49 和独立计数，不污染这里的三次交付断言。AP 在普通 park-loop 上下文进入/退出调度，而不是在 IPI handler 内调度。

4 vCPU 的强制证据为：

- 启动时 4/4 online，三个 AP parked；
- 启动期 6 次 fixed IPI；
- 回归后再增加 3 次，总计 9 次；
- 每个 AP 最后的 payload 含 sequence 3 和自己的 CPU index。

单 CPU 配置不发送 SIPI/IPI，但必须完成 BSP GS 初始化和其余全部测试。

## 7. 锁域与并发边界

AP 启动和停车路径保持以下约束：

- 启动/停车路径不分配 PMM/heap，不修改页表；
- AP 不写串口、不调用普通日志；
- 所有 page-table/AP-stack 准备由 BSP 在启动对应 AP 前完成；
- IPI handler 只访问当前 CPU block 或指定 shootdown 状态，使用原子发布；
- 现有 IOAPIC、MSI、MSI-X 仍指定 BSP 的物理 APIC ID。

M9 已在停车循环上加入每 CPU 调度；M10 已允许普通 worker 经锁调用 PMM/heap，并加入受控迁移和 TLB shootdown。ISR 仍不分配内存，新建页表映射仍由 BSP 准备，见 `smp-memory-migration.md`。

## 8. 实测矩阵

同一 Release EFI 已通过：

- xAPIC 4 vCPU 连续 3 次；
- x2APIC 4 vCPU；
- x2APIC 1 vCPU 回退；
- x2APIC 8 vCPU；
- x2APIC 16 vCPU 固定容量边界；
- 4 vCPU 下 64/128/256/512 MiB；
- EDU/e1000e 缺失负对照。

16 vCPU 实测启动期 30 次 IPI、最终 45 次；其余设备、多向量和调度回归同时通过。

## 9. 当前后续边界

M10 已覆盖受控迁移、PMM/heap 锁、跨核 wakeup 和基本 TLB shootdown。尚未实现：

- 自动负载平衡、并发在途迁移和可扩展 run-queue 锁域；
- 通用并发页表编辑、用户 CR3 和按地址空间 shootdown；
- 每 CPU GDT/TSS、用户态和 syscall；
- CPU hotplug、offline 和在线 AP 的动态栈销毁；
- interrupt remapping 与复杂 MSI/MSI-X affinity。
