# MSI-X：e1000e 多向量与 PBA 验证

> 本文保留 **M7 / v0.8.0 的 MSI-X/PBA 基线**。M11 已加入 queue 0 legacy rings 和真实双向包 DMA，M12 加入最小 ARP/IPv4/ICMP，M13 再加入 UDP Echo 和真实 ring wrap；见 `e1000e-packet-dma.md`、`arp-ipv4-icmp.md` 与 `udp-ring-wrap.md`。

## 1. 范围

Milestone 7 使用 QEMU 的 **Intel 82574L e1000e** (`8086:10d3`) 验证真实 MSI-X table entry、独立 IDT 向量和 Pending Bit Array。测试只使用文档化的 CSR、ICS 和 IVAR，不配置 descriptor ring，也不需要网络后端。

因此本轮完成的是 **MSI-X 中断机制**，不是完整网卡驱动或网络协议栈。参考：

- [Intel 82574L datasheet](https://www.farnell.com/datasheets/1920783.pdf)；
- [QEMU e1000e_core.c](https://github.com/qemu/qemu/blob/master/hw/net/e1000e_core.c)，用于核对当前设备模型的中断和 EITR 行为。

M7 的 `run_qemu.py` 不需要网络后端；M11 默认同时附加 `edu`、带 dgram peer 的 `e1000e`。`--no-e1000e` 仍是设备缺失负对照，M11 另有 `--no-network-peer`。

## 2. Capability、BAR 与通用 entry 操作

PCI capability walk 现在同时保存：

- MSI-X table BIR/offset；
- MSI-X PBA BIR/offset；
- table entries 数量。

e1000e 实测为 5 个 entries，table 和 PBA 都位于 BAR3；QEMU 使用 table offset `0`、PBA offset `0x2000`。驱动根据 capability 计算并映射覆盖两者的 device pages。M7 的 CSR 只映射 BAR0 第一页；M11 为 RAL/RAH 等寄存器扩展为 `0x8000` 字节。所有映射均为 RW/NX、cache-disabled。

`src/msix.cpp` 提供与具体 NIC 无关的操作：

- 16 字节 `TableEntry { address_low, address_high, data, vector_control }`；
- entry 在 address/data 更新期间保持 masked，最后才按请求 unmask；
- per-vector mask/unmask；
- qword PBA 指定位读取。

message address 使用非重映射的物理 fixed 目标，APIC ID 必须不超过 255；data 是对应 IDT vector。MSI 与 MSI-X 不同时启用，PCI command 同时设置 Memory Space、Bus Master 和 INTx Disable。

## 3. IVAR 与三个独立向量

82574L 的 IVAR 每个 nibble 使用 bit 2:0 表示 table index、bit 3 表示 valid。本轮映射：

| Cause | ICS/ICR bit | IVAR nibble | MSI-X entry | IDT vector |
|---|---:|---:|---:|---:|
| RXQ0 | `0x00100000` | RXQ0 / shift 0 | 0 | 34 |
| TXQ0 | `0x00400000` | TXQ0 / shift 8 | 1 | 35 |
| RXQ1 | `0x00200000` | RXQ1 / shift 4 | 4 | 36 |

每个 entry 都注册独立的 `irq::register_message` slot 和 handler context。它们不经过 IOAPIC RTE；分发器仍统一写 Local APIC EOI。

## 4. 三条实际交付路径

### Entry 0：直接交付

1. entry 0 unmasked，IMS 只开启三个测试 cause；
2. 向 ICS 写 RXQ0；
3. vector 34 handler 必须观察到 RXQ0，matched 恰好为 1。

### Entry 1：per-vector mask 与 PBA

1. entry 1 保持 masked，向 ICS 写 TXQ0；
2. 必须观察到 PBA bit 1=1，同时 entry 1 handler 调用仍为 0；
3. unmask entry 1；
4. vector 35 必须交付 TXQ0，matched=1，PBA bit 1 随后清零。

### Entry 4：function mask 与 PBA

1. entry 4 unmasked，但设置 MSI-X capability 的 function mask；
2. 向 ICS 写 RXQ1；
3. 必须观察到 PBA bit 4=1，entry 4 handler 调用仍为 0；
4. 清除 function mask；
5. vector 36 必须交付 RXQ1，matched=1，PBA bit 4 随后清零。

M7 阶段最后要求三个 matched 都恰好为 1、foreign-cause error=0、三个 PBA 位都清空。后续阶段只在此断言通过后启动：M11/M12 另按阶段统计 raw/ARP/ICMP，M13 再独立要求 UDP RX/TX IRQ 各 10 次；不以 entry 总 calls 替代这些断言。最终仍要求 active IRQ slot=0、unhandled=0。

## 5. EITR 空通知与 ICR/ICS 处理

首次实现曾出现 entry 0 第二次进入 handler、ICR 已为 0 的情况。核对 QEMU 源码后确认：

- 写 EITR=0 会被当前 QEMU 钳到内部最小值 500；
- 首次通知可启动节流定时器；
- 定时器到期可能再次通知同一 MSI-X vector，即使软件已经清除 cause。

此外，清除 payload 后，ICS/ICR 可能仍只保留 `INT_ASSERTED` (bit 31)。该位不是 RXQ/TXQ payload，不能算成一次有效 cause 或 foreign error。

当前处理：

1. handler 读取 **ICS**，利用其与 ICR 同步但不具有 read-to-clear 副作用的行为；
2. 移除 bit 31 后再判断 payload；
3. 空通知只增加 `empty_calls`，不清除其他向量的 cause；
4. 只有观察到自身 expected cause 才增加 `matched_calls`，并向 ICR 写自身 cause 做 write-to-clear；
5. foreign payload 仍计为 error 并使测试失败；
6. 每个阶段后等待两个 Local APIC ticks，让当前 QEMU 的节流窗口 settled；
7. `empty_calls` 只作诊断，不要求固定数量，也不计入 matched。

M12/M13 仅在 packet/protocol/UDP 数据阶段允许明确的 TXQE/LSC，并对 RXQ0/TXQ0 的跨向量 EITR 观察只记 `cross_calls`、不抢清队列 cause；未知 payload 和 synthetic 阶段规则不变。详见 `arp-ipv4-icmp.md` 与 `udp-ring-wrap.md`。

这不是放宽“三个有效事件必须各交付一次”的断言，而是避免把合法的空调用误当成额外设备事件。

## 6. 清理与负对照

清理顺序：

1. IF=0 并停止测试 timer；
2. IMC 屏蔽全部 cause，读取 ICR 清除残留；
3. 清 EIAC/IAM/IVAR；
4. MSI-X function mask 并 disable，mask 三个 table entries；
5. 撤销 bus mastering；
6. 注销三个 message slots。

M11 在上述通用清理前先关闭 RCTL/TCTL 和 queue enable，并在撤销 bus mastering 后、注销 slots 前逆序释放 packet 页，核对 PMM 页数恢复。

`--no-e1000e` 必须在 e1000e stage 0 处 panic；`--no-edu` 仍在较早的 EDU 检查处 panic。两者都不会产生内核 PASS。

## 7. M7 实测基线

以下保留 M7 当时的 Release EFI 结果；M11 的扩展矩阵见 `e1000e-packet-dma.md` 和正式 `artifacts/VALIDATION.txt`。M7 已通过：

- xAPIC 128 MiB 连续 3 次；
- x2APIC 128 MiB；
- x2APIC 64 MiB；
- xAPIC 256 MiB；
- x2APIC 512 MiB；
- 最终 artifacts 的 xAPIC/x2APIC 独立复验。

典型日志：

```text
[ok] e1000e MSI-X entries: 5, table BAR: 0x0000000081180000, per-vector PBA: 1, function PBA: 1
[ok] MSI-X entry 0 -> IDT vector 34, matched: 1, empty: 1, cause: 0x0000000080100000
[ok] MSI-X entry 1 -> IDT vector 35, matched: 1, empty: 1, cause: 0x0000000080400000
[ok] MSI-X entry 4 -> IDT vector 36, matched: 1, empty: 1, cause: 0x0000000080200000
```

`empty` 数量允许随定时变化；matched、cause、PBA 和清理结果才是强制断言。

## 8. 当前后续边界

M11 已补上测试所需 MAC/link 初始化、queue 0 legacy descriptor rings 和 TX/RX packet DMA。仍未实现：

- 完整 reset/PHY 管理、生产级异常恢复和全部硬件 errata；
- 多队列、RSS、校验和/TSO/VLAN offload 和网络协议栈；
- interrupt remapping、IOMMU、多 CPU MSI-X affinity；
- 通用 PCI BAR 资源分配与完整设备驱动模型；
- 中断线程化和网络阻塞 IO。
