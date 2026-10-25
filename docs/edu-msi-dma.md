# EDU：真实 MSI 与 DMA 验证

## 1. 范围与硬件契约

Milestone 6 使用 QEMU 文档化的 **EDU educational PCI device**，vendor/device 为 `1234:11e8`。它专门提供 MMIO、IRQ、MSI 和 DMA 教学接口，适合先验证内核机制，而不是直接跳进复杂 AHCI 驱动。

硬件契约以 [QEMU EDU specification](https://www.qemu.org/docs/master/specs/edu.html) 为准：

- BAR0 是 1 MiB、32-bit memory MMIO；本驱动只映射寄存器所在的第一页；
- 内部 DMA 缓冲区位于设备地址 `0x40000`，长度 4096 字节；
- 默认 DMA mask 只有 28 位，RAM 地址必须低于 `0x10000000`；
- 支持 INTx 和单向量 MSI；即使使用 MSI，ISR 仍必须 acknowledge `0x64`；
- DMA command bit 0 启动、bit 1 选择 EDU→RAM、bit 2 请求完成中断。

`tools/run_qemu.py` 默认附加 `-device edu`，`--no-edu` 仅用于负对照。内核不会在设备缺失时伪造成功。

## 2. 使用的 MMIO 寄存器

| Offset | 用途 |
|---:|---|
| `0x00` | identification，低 16 位应为 `0x00ed` |
| `0x04` | liveness，写入后读回应为按位取反 |
| `0x24` | 当前 IRQ status |
| `0x60` | 触发直接设备中断，本轮写入 `0x001` |
| `0x64` | ISR acknowledge，清除已处理 status |
| `0x80` | DMA source，64 位地址按两个 dword 写入 |
| `0x88` | DMA destination |
| `0x90` | DMA count，本轮为 256 |
| `0x98` | DMA command/status |

MMIO 页使用 `vmm::map_device_page`，要求 RW、NX、cache-disabled。驱动先检查 BAR 类型，完成映射并开启 PCI Memory Space / INTx Disable，再检查 identification 和 liveness，避免在 decode 尚未启用时访问 MMIO，也避免把无效 BAR 当成设备成功。

## 3. MSI 配置与 message IRQ 生命周期

### 注册

`irq::register_message` 从 vector 34–47 分配 slot，但**不写入 IOAPIC RTE**。slot 显式记录 source 为 `Message`，与 GSI source 区分。注册要求 IF=0 且 Local APIC 已初始化。

### PCI 配置

1. 安全遍历 capability，找到 MSI (`0x05`)；
2. 首次 MMIO 前设置 Memory Space、INTx Disable，注册 message vector 后再设置 Bus Master；
3. 暂禁 MSI，并在设备支持 per-vector masking 时先 mask；
4. message address 使用物理 fixed 目标：`0xFEE00000 | (APIC_ID << 12)`；
5. message data 写入分配的 IDT vector，delivery mode 为 fixed；
6. 同时支持 32-bit/64-bit MSI capability 的数据字段布局；
7. Multiple Message Enable 设为 0，只启用一个向量，再解除 per-vector mask。

代码覆盖 32-bit/64-bit capability 布局；当前 EDU 实际验证的是其 64-bit MSI 路径，未另找设备宣称 32-bit 路径已实测。

当前没有 interrupt remapping，目标 APIC ID 必须不超过 255。QEMU 单 BSP 的 APIC ID 为 0，因此同一驱动已在 xAPIC 和 x2APIC 两种模式下实测。

### ISR 与注销

ISR 只执行：

1. 读取 `0x24`；
2. 记录直接中断 `0x001` 或 DMA 完成中断 `0x100`；
3. 将已读 status 写入 `0x64` acknowledge；
4. 返回通用分发器，由分发器写 Local APIC EOI。

ISR 不打印、不分配。主路径关闭 IF 并停止 timer 后，先关闭/mask MSI、ack 残留状态、撤销 bus mastering，再注销 message slot。`irq::unregister` 对 message source 不会误调用 `ioapic::mask_gsi`。

## 4. DMA 往返与数据完整性

PMM 新增 `allocate_pages_below(count, limit)`。它从物理上限以下的区间高端分配；若上限切开一个 extent，会保留上限以上的 high tail，并保持降序链表和页数账目。

测试分配两个独立页，均满足：

```text
physical_address + 4096 <= 0x10000000
```

随后执行：

1. 源页前 256 字节写入确定图案，目标整页填 `0xA5`；
2. RAM source → EDU `0x40000`，command=`0x05`（start + IRQ）；
3. 等待第一次 DMA completion MSI，并确认 start bit 已清除；
4. EDU `0x40000` → RAM destination，command=`0x07`（start + EDU→RAM + IRQ）；
5. 等待第二次 DMA completion MSI；
6. 比较 256 字节内容，并检查目标页第 256–4095 字节仍为 `0xA5`；
7. 要求直接 MSI=1、DMA MSI=2、总 IRQ=3、unexpected status=0、IRQ status=0；
8. 释放两页，要求 PMM 空闲页数恢复到分配前的精确值。

x86 当前测试平台提供 DMA cache coherence；驱动在 buffer 写入、command 和读取之间使用显式原子 fence 保证顺序。这不扩张为所有架构或所有设备都适用的通用 DMA API。

## 5. 实测命令

```bash
# xAPIC / 128 MiB
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpu "qemu64,-x2apic" --work-dir build/edu-xapic

# x2APIC / 128 MiB
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpu "qemu64,+x2apic" --work-dir build/edu-x2apic

# 跨越 256 MiB 的 RAM 布局，覆盖 bounded-PMM extent 分割
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpu "qemu64,+x2apic" --memory 512M \
  --work-dir build/edu-512m

# 负对照：预期在内核 EDU 检查处 panic，而不是 PASS
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --no-edu --expect "CXC_OS_PANIC: EDU MSI/DMA" \
  --work-dir build/edu-absent
```

成功日志包含：

```text
[ok] EDU MMIO: 0x0000000081000000, MSI vector: 34, direct MSI: 1, DMA completion MSI: 2
[ok] EDU DMA source: ..., destination: ..., verified round-trip bytes: 256
CXC_OS_SELF_TEST_PASS
```

512 MiB 实测源/目的页为 `0x0FFFF000` 和 `0x0FFFE000`，确实落在 28 位上限内并覆盖 extent 分割路径。

## 6. 明确边界

本轮不是：

- AHCI、NVMe、磁盘或文件系统驱动；
- EDU 自身的多向量 MSI/MSI-X；e1000e 的 MSI-X 验证见 `msix-e1000e.md`；
- IOMMU、interrupt remapping 或多向量 affinity；
- 通用 PCI BAR 资源分配器或完整驱动模型；
- 用户态 DMA、缓冲区 pinning 或异步阻塞 IO 框架。

实现集中在 `src/edu.cpp`、`src/irq.cpp`、`src/pci.cpp` 和 `src/pmm.cpp`；所有原有 PIT、UART、timer、调度、PMM/VMM 测试仍必须同时通过。
