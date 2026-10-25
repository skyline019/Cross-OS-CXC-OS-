# e1000e Queue 0：Descriptor Ring 与双向包 DMA

> 本文保留 **M11 / v0.12.0 数据路径基线**。M12 已在同一 rings 上增加最小 ARP/IPv4/ICMP，M13 又增加 UDP Echo、七页 FIFO 回收和真实 ring wrap；当前协议规则见 `arp-ipv4-icmp.md` 与 `udp-ring-wrap.md`。

## 1. M11 范围

Milestone 11 / v0.12.0 在 M7 的 MSI-X/PBA 测试之后增加了 **82574L queue 0 单队列数据路径**：

- 8 项 legacy RX/TX descriptor rings；
- 低于 4 GiB 的 descriptor 和 packet-buffer DMA 页；
- 一个已知 Ethernet 请求帧的实际 RX descriptor write-back、MSI-X 和 DMA 内容校验；
- 一个确定响应帧的实际 TX descriptor write-back、MSI-X 和外部 peer 接收校验；
- 停止 RX/TX、撤销 bus mastering、释放 DMA 页并核对 PMM 页数。

这不是完整网卡驱动或网络协议栈。M7 的 entry 0/1/4 direct/per-vector PBA/function PBA 测试仍先独立执行并保持原断言；历史设计见 `msix-e1000e.md`。

## 2. CSR 映射与队列资源

M7 只访问 BAR0 的前 4 KiB。M11 还需要 RAL/RAH、MRQC 等 `0x5000` 范围寄存器，因此 BAR0 显式映射 `0x8000` 字节，全部为 RW/NX、cache-disabled。MSI-X table/PBA 仍按 capability 的 BIR/offset 映射。

每个 DMA 页由 `pmm::allocate_pages_below(1, 0x100000000)` 取得，经 direct map 由 CPU 访问，物理地址交给设备：

| 资源 | 数量/大小 | 说明 |
|---|---:|---|
| RX ring | 1 页，使用 128 字节 | 8 个 16 字节 legacy RX descriptors |
| TX ring | 1 页，使用 128 字节 | 8 个 16 字节 legacy TX descriptors |
| RX buffers | 7 页 | 每页前 2048 字节作为一个 RX buffer |
| TX buffer | 1 页 | 一个 60 字节响应帧 |

RX 只发布 7 个缓冲区，`RDT=7` 指向 descriptor 6 之后的位置。保留一个未使用槽，避免 `head==tail` 的空/满歧义；不把 8 写成会回绕的 tail。

## 3. 初始化顺序

在 M7 synthetic MSI-X 测试全部成功、IF=0 后执行：

1. 从 PMM 分配 rings/buffers，RX buffer 前 128 字节填 `0xA5` 作为写越界 guard；
2. 读取并保存 RAL0/RAH0 的 MAC，要求 `RAH.AV=1`；
3. 暂时关闭 RCTL/TCTL，设置 `CTRL.SLU` 并要求 `STATUS.LU=1`；
4. `MRQC=0`、`RFCTL.EXTEN=0`、`SRRCTL0=0`，清 128 个 MTA 项，选择单队列 legacy RX；
5. 写 RDBAL/RDBAH/RDLEN/RDH/RDT 和 TX 对应寄存器，两个 ring 长度均为 128；
6. RDTR/RADV/TIDV/TADV 写 0，配置 RXDCTL/TXDCTL queue enable、granularity 和阈值；
7. TARC0 设置 enable bit 10 和 82574 errata bit 26；TCTL 配置 EN/PSP/RTLC、CT=15、COLD=64；TIPG 使用 `8 | 2<<10 | 10<<20`；
8. release fence 后写 `RDT=7`，最后启用 TCTL 和 `RCTL.EN|BAM|SECRC`。

不依赖软件伪造 DD：DD、长度、错误位和 frame 内容必须由设备 DMA 写入。主路径在读 descriptor 前执行 acquire fence，在发布 RX/TX tail 前执行 release fence。

## 4. 确定的测试帧

测试只使用本地实验 EtherType `0x88B5`，不经过 IP/ARP。runner 将 NIC MAC 固定为 `52:54:00:43:58:43`，peer MAC 为 `02:43:58:43:11:01`。

### 请求：peer → 内核

- 60 字节，不含 FCS；
- 目的地址为 broadcast，源地址为 peer MAC；
- payload 以 `CXC11REQ` 开始；
- 后续 38 字节为 `((index * 7 + 0x2b) & 0xff)`，`index=8..45`。

内核必须同时验证：

- RX descriptor 0 的 `DD|EOP`、errors=0、length=60；
- `RDH=1`；
- 目的/源 MAC、EtherType、magic 和全部 payload；
- buffer 偏移 64–127 的 `0xA5` guard 未改变；
- 一次真实 RXQ0 MSI-X。

### 响应：内核 → peer

- 60 字节，目的地址为 peer MAC，源地址为 RAL/RAH 读出的 NIC MAC；
- 同一 EtherType，magic 改为 `CXC11RSP`；
- 后续 payload 为请求对应字节 XOR `0xA5`；
- TX descriptor 0 使用 `EOP|IFCS|RS`，发布 `TDT=1`。

内核要求 DD、`TDH=1` 和一次真实 TXQ0 MSI-X。Python peer 还必须在独立 UDP socket 上实际收到 60 字节响应，并逐字节验证全部字段。只有 TDH/TDT、串口日志或内核自报成功都不算完整 TX 证据。

## 5. 真实 MSI-X cause 与辅助 TXQE/LSC

82574L 在 MSI-X 模式下：

- queue 0 RX 使用 `RXQ0` (ICR bit 20)；
- queue 0 TX 使用 `TXQ0` (ICR bit 22)；
- 不以 legacy `RXT0`/`TXDW` 代替。

M7 的 IVAR 映射不变：RXQ0→entry 0/vector 34，TXQ0→entry 1/vector 35，RXQ1→entry 4/vector 36。因此 entry 0/1 最终日志通常为 `matched: 2`：一次来自 M7 ICS synthetic 测试，一次来自真实 packet；M7 在进入 packet phase 前仍要求三者 synthetic matched **恰好各一次**。

链路初始化和 TX 排空可能留下辅助 cause。M11 当时把 `0x2` 称为 LSC；M12 对照 QEMU 寄存器头确认它实际是 **TXQE**，真正 LSC 为 `0x4`，现已更正。当前只在 packet/protocol/UDP 阶段允许明确的 TXQE/LSC，并处理已知 queue cause 的跨向量 EITR 观察；未知位和 synthetic 阶段不放宽。ISR 仍读 ICS，不靠 read-to-clear ICR 一次清掉其他队列 cause。详见 `arp-ipv4-icmp.md`。

## 6. QEMU datagram peer

`tools/run_qemu.py` 默认创建两个本机 UDP 端口，并附加：

```text
-netdev dgram,id=cxcnet,local.type=inet,local.host=127.0.0.1,...
-device e1000e,netdev=cxcnet,mac=52:54:00:43:58:43
```

QEMU 的 `dgram` backend 让每个 UDP datagram 承载一个原始 Ethernet frame。peer 仅使用 Python 标准库 `socket`，Windows/Linux 均可运行；它是测试硬件后端，不是内核的宿主兼容层。

正式 `--check` 使用确定的一次注入：

1. peer 等待串口 `[net] e1000e packet-ready`，避免在 ring 就绪前盲目重试；
2. 恰好发送一个请求；
3. 保存请求和候选响应的长度/完整 hex 到 `network-peer.log`；
4. 仅当完整响应匹配时写 `validated_response=1` 和 `peer_ok=1`；
5. runner 只有在 **内核 serial PASS 且 peer_ok=1** 时返回成功。

非 `--check` 的手工运行没有串口 collector，使用低频重试；它不作为正式确定性验收日志。`--no-network-peer` 保留有 NIC、无注入的负对照，内核必须在 RX 等待阶段超时并 panic，不能产生 PASS。

## 7. 清理和页数账目

成功或失败都按生命周期反向清理：

1. `cli`，停止本轮 Local APIC test timer；
2. 关闭 RCTL/TCTL，清 RXDCTL/TXDCTL queue enable；
3. 执行既有 IMC/ICR、EIAC/IAM/IVAR、MSI-X function/entry mask 清理；
4. 撤销 PCI Bus Master，确保设备不再写 DMA 页；
5. 逆序释放 10 个 packet/rings 页；
6. 要求 `pmm::free_pages_count()` 与分配前精确相等；
7. 注销 message IRQ slots，检查无 unhandled interrupt。

因此不会在本轮已禁用前释放 DMA 页，也不会以 heap 扩展差额掩盖 packet 页泄漏。

## 8. 已执行验证

v0.12.0 代码已在 Linux + QEMU/OVMF 中完成严格 Debug/Release 构建；Release 顺序矩阵全部通过，每次均包含真实 peer 响应：

- xAPIC 4 vCPU、128 MiB 连续 3 次；
- x2APIC 4 vCPU、64/128/256/512 MiB；
- xAPIC 4 vCPU、256 MiB；
- x2APIC 1/2/8/16 vCPU、256 MiB；
- 缺 EDU、缺 e1000e、有 e1000e 但无 peer 三个预期 panic 负对照。

典型证据：

```text
[ok] e1000e packet RX/TX frames: 1/1, bytes: 60/60, heads: 1/1, IRQs: 1/1
CXC_OS_SELF_TEST_PASS
validated_response=1
peer_ok=1
```

上述为 M11 当时的矩阵。当前 artifacts 已推进到 M13，最终复验和哈希以 `artifacts/VALIDATION.txt` 为准；Windows 本地构建和远端 CI 配置保留，但未在此环境中实际执行。

## 9. 当前边界

- 仅 queue 0 和 legacy descriptors；没有多队列负载平衡、RSS 或通用驱动框架；
- 仅测试所需初始化，没有完整 reset/PHY 管理、节能、所有 errata 或生产级异常恢复；
- 没有 checksum/TSO/VLAN offload、中断 affinity、interrupt remapping 或 IOMMU；
- M11 尚无协议；M12 增加固定地址的最小 ARP/IPv4/ICMP，M13 增加最小 UDP Echo，但仍没有 TCP、socket API、阻塞网络 IO 或完整协议栈；
- 只验证 QEMU 82574L 模型和本机 dgram peer，未声称通过物理 82574L 网卡。
