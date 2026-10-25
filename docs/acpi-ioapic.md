# ACPI/MADT、x2APIC 与 IOAPIC

## 1. ACPI 发现的生命周期

UEFI Configuration Table 提供 RSDP，优先 ACPI 2.0+ GUID，保留 ACPI 1.0 fallback。

解析发生在初始固件页表仍有效的短窗口内：

1. 校验 RSDP signature 和前 20 字节 checksum；
2. ACPI 2.0+ 再校验扩展长度和完整 checksum；
3. 校验 XSDT 或 RSDT header、length 和 checksum；
4. 找到签名 `APIC` 的 MADT并校验；
5. 复制所需字段，不保留任何 ACPI 表指针。

因此 runtime CR3 清除低映射后，LAPIC/IOAPIC 初始化只读取内核高地址数据。

## 2. 当前支持的 MADT entry

| Type | 用途 |
|---:|---|
| 0 | Local APIC 处理器的 APIC ID、UID 与 enabled 状态 |
| 1 | IOAPIC ID、MMIO 地址、GSI base |
| 2 | Interrupt Source Override，尤其 ISA IRQ→GSI |
| 5 | 64 位 Local APIC address override |
| 9 | x2APIC 处理器的 32 位 APIC ID、UID 与 enabled 状态 |

当前静态上限为 16 个 enabled processors、4 个 IOAPIC、16 个 ISO，重复 APIC ID 或超出容量会明确失败。它不是完整 ACPI namespace/AML 实现。

## 3. xAPIC 与 x2APIC 共用语义

初始化先检查 MADT LAPIC 地址与 `IA32_APIC_BASE` 是否一致。

- CPUID 支持 x2APIC 时，设置 IA32_APIC_BASE 的 global enable 和 x2 enable；
- x2APIC 通过 `0x800 + MMIO_offset/16` MSR 访问；
- 无 x2APIC 时映射传统 MMIO 页并设置 PCD/PWT/NX；
- xAPIC ID 取 bit 31:24，x2APIC ID 使用完整 32 位；
- timer、EOI、SVR、LVT 和 calibration 共享上层代码。

AP 使用相同的 xAPIC/x2APIC 模式但不重复 calibration、不启动 timer。xAPIC IPI 分别写 ICR high/low 并等待 Delivery Status；x2APIC 合并为一次 `0x830` MSR 写。真实 INIT-SIPI-SIPI 与 vector-48 IPI 见 `smp-startup.md`。

QEMU 测试命令：

```bash
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpu "qemu64,-x2apic"
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpu "qemu64,+x2apic"
```

## 4. IOAPIC 接管

每个 MADT IOAPIC 都映射到独立高地址 MMIO 窗口。初始化读取版本以获得 RTE 数量，并在启用 IF 前屏蔽所有 RTE。

RTE 操作使用 IOREGSEL/IOWIN 间接访问：

- low dword：vector、polarity、trigger、mask；
- high dword：物理目标 APIC ID 的 8 位字段；
- 修改路由时保持 masked，完成后再 unmask。

当前物理目标模式要求 APIC ID 不超过 255；这不等于完整的 x2APIC 集群/逻辑路由支持。

## 5. 真实 IRQ0 测试

测试不是只读 IOAPIC version：

1. 从 MADT bus-0 override 将 ISA IRQ0 转换为 GSI；
2. 在 q35/OVMF 上实际得到 GSI2；
3. 为该 GSI 配置 vector 33 和启动 CPU APIC ID；
4. 本地 APIC timer 提供等待超时；
5. PIT channel 0 配置为 10 ms mode-0 one-shot；
6. unmask RTE 并等待硬件中断；
7. vector 33 ISR 重新 mask RTE、递增计数并 EOI；
8. 主路径关闭 IF、停止本地 timer，并要求计数严格为 1。

ISR 内 mask 同时覆盖 level-triggered override 可能保持线路有效的情况。

## 6. 当前实测

同一 Release EFI 已在以下配置通过：

- xAPIC，4 vCPU、128 MiB，连续 3 次；
- x2APIC，4 vCPU、128 MiB；
- x2APIC，4 vCPU、64 MiB；
- xAPIC，4 vCPU、256 MiB；
- x2APIC，4 vCPU、512 MiB；
- x2APIC，1/8/16 vCPU、128 MiB。

每次均完成 MADT、SMP/IPI、IOAPIC IRQ0、EDU MSI/DMA、e1000e 三向量 MSI-X/PBA、每 CPU 调度与本地睡眠唤醒和全部内存回归，spurious IRQ 为 0。tick/switch 数量随构建布局可变化，不作为固定性能指标。

## 7. 尚未实现

- PCI INTx 的完整设备路由策略；
- interrupt remapping 和每 CPU message affinity；EDU MSI 与 e1000e MSI-X 见对应设备文档；
- shared IRQ、通用 PCI 设备路由策略与中断线程化；
- CPU hotplug、用户地址空间和 PCID/INVPCID；
- ACPI AML、电源管理和热插拔；
- 超过 255 的 IOAPIC 物理目标或复杂逻辑集群路由。
