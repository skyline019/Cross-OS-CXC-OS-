# PCI 与通用外部 IRQ

## 1. 当前范围

Milestone 5 建立固定容量、可注销的外部 IRQ 注册框架和 PCI capability 解析；Milestone 6 增加配置写、BAR 读取、message-source slot，并用 EDU 完成真实 MSI 与 DMA；Milestone 7 增加 MSI-X table/PBA 映射和 e1000e 多向量交付。

MSI、MSI-X 均有实际中断证据；网络 descriptor ring 和包数据路径仍不属于当前范围。

## 2. 向量布局

| Vector | 用途 |
|---:|---|
| 32 | Local APIC timer |
| 33 | IOAPIC PIT IRQ0 测试 |
| 34–47 | 通用 GSI/message IRQ 静态注册池 |
| 255 | Local APIC spurious |

汇编为 32–47 提供完整入口，34–47 由 `irq::dispatch` 查找注册 slot。

## 3. 两类 IRQ source

每个 slot 保存 handler、context、active 和 source；GSI slot 另存 GSI，message slot 的 GSI 字段不参与路由。

### IOAPIC GSI 注册

要求 IF=0：

1. 找空闲 slot，并拒绝已注册的重复 GSI；
2. 通过 IOAPIC 写入 masked RTE，再 unmask；
3. 发布 handler/context、GSI、source 和 active；
4. 返回分配向量。

注销时先 mask GSI，再清除 active/handler。当前一个 GSI 仅允许一个 handler，不支持 shared IRQ。

### Message 注册

`register_message` 只分配向量和 slot，不写 IOAPIC。设备驱动随后负责配置 MSI message address/data，并在注销前关闭设备 MSI、清理 pending status。

注销 message source 不会调用 `mask_gsi`；否则可能误操作 GSI0 或让注销失败。

两类 ISR 都不分配内存、不打印日志；handler 返回后由分发器统一写 Local APIC EOI。设备级 acknowledge 仍属于具体 handler，例如 EDU 的 `0x64`。

## 4. IOAPIC 通用路由

`route_gsi` 复用 MADT ISO flags：

- polarity 为 active-low 时设置 bit 13；
- trigger 为 level 时设置 bit 15；
- ISA conforming 默认使用 edge/high；
- high dword 写目标 APIC ID；
- 修改时先 masked，完成后 unmask。

仍使用物理目标模式，因此当前要求目标 APIC ID 不超过 255。

## 5. UART loopback 实中断

测试使用 COM1 的 ISA IRQ4：

1. 从 MADT 计算实际 GSI，q35 上为 GSI4；
2. MCR 保持 OUT2 并启用内部 loopback；
3. FIFO 改为 `0x07`，使用一字节接收触发；
4. 注册 GSI 和 handler，并验证重复注册被拒绝；
5. 开启 UART receiver-data interrupt；
6. 发送 4 个特殊字节，ISR 读取 IIR、LSR、RBR 并校验顺序；
7. 以本地 APIC tick 作为超时；
8. 关闭 IER、注销 GSI、恢复 polling MCR/FIFO；
9. 要求 4 个正确字节、IRQ calls 非零、active slot=0、unhandled=0。

当前实测为 4 次 IRQ、4 个字节。测试期间禁止日志写 UART，避免 loopback 数据被日志污染。

## 6. PCI 枚举与配置访问

采用配置机制 1 的 `0xCF8/0xCFC`：

- 每个 bus/device 先读 function 0 vendor；
- 不存在则跳过整个 device；
- 仅 multifunction 再扫描 function 1–7；
- 保存 vendor/device/class/subclass/prog-IF/header；
- 最多保存 128 个 function，溢出明确失败。

M6 公开 16/32 位配置读写和 BAR 读取。16 位写在选择 aligned dword 后向 `0xCFC + (offset & 2)` 执行 `outw`，不做 dword read-modify-write，避免误清相邻 RW1C 状态位；调用者必须使用对齐且合法的配置 offset。EDU 驱动用这些接口设置 command 和 MSI capability。

## 7. Capability 安全遍历

仅对 header type 0/1 且 status 指示 capability list 的 function 遍历：

- 最多 48 步；
- 检查最小 offset、自环和无效 ID；
- MSI 保存 capability offset 和 message control；
- MSI-X 保存 control、table/PBA BIR/offset 和条目数；
- MSI-X 需要完整 12 字节，防止 8 位配置 offset 回绕。

默认附加 EDU 和 e1000e 时，q35 实测为 7 个 PCI functions、3 个 MSI capability、1 个 MSI-X capability，解析错误为 0。`--no-edu` 或 `--no-e1000e` 会改变枚举结果，并在对应设备测试处明确失败。

## 8. EDU MSI/DMA

EDU 使用 message slot 和单向量 MSI，不占用 IOAPIC GSI。驱动配置 BAR0 MMIO、bus mastering 和 message address/data，实际得到：

- 1 次直接 MSI；
- 2 次 DMA completion MSI；
- 256 字节 RAM→EDU→RAM 数据一致；
- 目标页 guard 未改变；
- 设备与 IRQ slot 清理后 active=0、unhandled=0。

完整寄存器、DMA mask、清理顺序和测试命令见 [`edu-msi-dma.md`](edu-msi-dma.md)。

## 9. e1000e MSI-X

通用 MSI-X entry 操作负责 16 字节 table entry、address/data、per-vector mask 和 PBA 位读取。e1000e 驱动另外完成 IVAR cause→entry 映射与 capability function mask。

当前实测 entry 0/1/4 分别交付到 IDT vector 34/35/36；entry 1 覆盖 per-vector PBA，entry 4 覆盖 function-mask PBA。三个有效 cause 均 matched=1，mask 期间对应 handler 不得提前调用。完整流程见 [`msix-e1000e.md`](msix-e1000e.md)。

## 10. 尚未实现

- interrupt remapping、多向量 MSI 和 SMP affinity；
- PCI ECAM/MMIO 配置空间；
- 通用 BAR 资源分配和完整设备驱动模型；
- AHCI/NVMe/block IO、IOMMU 和用户 DMA；
- shared IRQ、中断线程化和每 CPU 路由。
