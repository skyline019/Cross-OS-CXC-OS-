# 最小 ARP、IPv4 与 ICMP Echo

> 本文保留 **M12 / v0.13.0 协议基线**。M13 已在相同 rings 上增加 UDP Echo、动态 RX 回收和真实 wrap；最新规则见 `udp-ring-wrap.md`，正式交付以 `artifacts/VALIDATION.txt` 为准。

## 1. M12 范围

Milestone 12 / v0.13.0 在 M11 的真实 queue 0 DMA 之后增加一个受限协议验证路径：

1. 保留 M11 EtherType `0x88B5` 的 60 字节原始请求/响应；
2. 接收并严格校验一个 ARP Request，返回 ARP Reply；
3. 接收并严格校验一个 IPv4 ICMP Echo Request，返回 Echo Reply；
4. 三个接收和三个发送都使用同一组 legacy rings、真实 DMA 和 RXQ0/TXQ0 MSI-X；
5. 外部 Python peer 逐字节验证全部三个响应，内核随后仍执行完整清理和既有系统回归。

固定测试地址：

| 节点 | MAC | IPv4 |
|---|---|---|
| CXC/OS | `52:54:00:43:58:43`（同时从 RAL/RAH 读取核对） | `10.0.12.2` |
| Python peer | `02:43:58:43:11:01` | `10.0.12.1` |

这不是通用网络栈。没有 ARP cache、DHCP、IP 路由、分片重组、UDP/TCP、socket API 或异步协议线程。

## 2. 与 M11 共用的 descriptor 路径

M11 的 rings、DMA 页和初始化保持不变。M12 将原始 TX 发布和协议 TX 发布统一到 `submit_tx_frame`，RX/TX 等待按阶段读取独立中断计数：

| Descriptor index | RX 请求 | TX 响应 | 完成后 RDH/TDH |
|---:|---|---|---:|
| 0 | M11 60 字节原始帧 | M11 60 字节原始响应 | 1 / 1 |
| 1 | 60 字节 ARP Request | 60 字节 ARP Reply | 2 / 2 |
| 2 | 74 字节 ICMP Echo Request | 74 字节 ICMP Echo Reply | 3 / 3 |

M11 `PacketResult` 保持在第一阶段的 1/1、60/60、IRQ 1/1；M12 `ProtocolResult` 单独记录 ARP/ICMP 次数、长度、最终 head 和两次协议中断。这样不会因累计值增加而丢失 M11 断言。

本轮只使用前三个 descriptor，不宣称已经实现 ring wrap、动态 buffer recycling 或多队列。

## 3. ARP Request / Reply

peer 在收到 M11 原始响应后，等待内核 `[net] e1000e protocol-ready`，再发送 ARP Request，避免原始阶段和协议阶段的中断计数发生竞态。

请求必须满足：

- Ethernet broadcast 目的、peer 源 MAC、EtherType `0x0806`；
- HTYPE=1、PTYPE=`0x0800`、HLEN=6、PLEN=4、OPER=1；
- sender 为 peer MAC/IP；target MAC 全零，target IP 为 `10.0.12.2`；
- Ethernet 最小帧 padding（偏移 42–59）全部为零。

内核构造 OPER=2 的响应：sender 为 NIC MAC/IP，target 为 peer MAC/IP，并显式清零 60 字节 TX buffer 后重建报文，避免继承上一帧 padding。

peer 逐字段检查完整 ARP Reply 和全部 padding，写 `arp_reply_valid=1`。

## 4. IPv4 与 ICMP Echo

测试 Echo Request 为 74 字节：

- Ethernet 14 字节；IPv4 header 20 字节；ICMP header 8 字节；payload 32 字节；
- IPv4 total length=60，IHL=5，无分片，protocol=1；
- 源 `10.0.12.1`，目的 `10.0.12.2`；
- ICMP type=8、code=0，固定 identifier/sequence 和确定 payload；
- 请求 TTL=53，防止把“原样复制”误认为完整响应构造。

内核验证：

- Ethernet 目的/源和 EtherType；
- IPv4 IHL、total length、fragment offset/MF、TTL、protocol 和地址；
- IPv4 header checksum；
- ICMP type/code 和整个 ICMP message checksum；
- descriptor DD+EOP、errors=0、长度 74、RDH=3，以及 RX buffer 对齐尾部 guard。

响应复制必要 identifier/sequence/payload，交换 Ethernet/IP 地址，将 IPv4 TTL 改为 64、ICMP type 改为 0，再把 IPv4 和 ICMP checksum 字段清零后分别重算。peer 检查全部 74 字节语义和两个 checksum，写 `icmp_reply_valid=1`。

实现使用逐字节大端读写和通用 16 位 Internet checksum，不对未对齐网络数据做 packed-struct 指针访问；checksum 代码也处理奇数字节长度。

## 5. MSI-X 辅助 cause 与跨向量观察

QEMU 寄存器定义中：

- `TXQE=0x00000002`；
- `LSC=0x00000004`；
- `RXQ0=0x00100000`；
- `TXQ0=0x00400000`。

M11 曾把 `0x2` 错命名为 LSC，实际处理的是 TXQE。M12 更正两个定义，并只在 packet/protocol 阶段允许明确的 TXQE/LSC 辅助原因。

快速连续收发还验证了一种真实时序：EITR 后续通知可能在 entry 1 上让 ICS 只显示仍属 entry 0 的 RXQ0。当前规则：

1. 先按阶段掩码检查，任何未知 ICR 位都记录 `error_cause` 并失败；
2. 辅助 TXQE/LSC 可单独确认；
3. packet/protocol 阶段观察到另一向量的 RXQ0/TXQ0 时，只增加 `cross_calls`，不增加 matched、不记 error，也**不清除该队列 cause**；
4. 所属 vector 仍负责 matched 计数和 queue acknowledge；
5. M7 synthetic 阶段不开放辅助或跨向量规则，原 direct/PBA 精确断言不变。

`empty_calls` 和 `cross_calls` 都是时序诊断，不要求固定值；有效帧数、协议内容、head、每阶段中断数和 errors=0 是强制断言。

## 6. 外部 peer 状态机

正式 `run_qemu.py --check` 顺序执行：

```text
等待 packet-ready
  → 发送 raw request → 验证 raw response
  → 等待 protocol-ready
  → 发送 ARP request → 验证 ARP reply
  → 发送 ICMP Echo request → 验证 ICMP Echo reply
  → validation_scope=m12-raw-arp-icmp, peer_ok=1
```

每次正式运行恰好发送三个请求，`requests_sent=3`。`network-peer.log` 保存三个请求和三个响应的长度及完整 hex。runner 只有在串口 `CXC_OS_SELF_TEST_PASS` 和完整协议 `peer_ok=1` 同时成立时才成功。

## 7. 负对照

- `--no-network-peer`：没有任何请求，M11 raw RX 阶段超时；
- `--no-protocol-peer`：raw 请求/响应完整成功，但不发送 ARP；内核在 protocol stage 2 超时，peer 仅标记 `validation_scope=m11-raw-only`；
- `--bad-icmp-checksum`：raw 和 ARP 均成功，随后故意翻转 ICMP checksum；内核在 protocol stage 4 拒绝请求，不产生 ICMP reply，最终 panic；
- `--no-e1000e`、`--no-edu`：保留原有设备缺失负对照。

这些负对照都不得输出 `CXC_OS_SELF_TEST_PASS`。预期 panic 的 runner 不错误要求完整成功 peer；bad-checksum 日志中 ARP 已为 1/1，而 ICMP 必须保持 0/0。

## 8. 清理和系统回归

协议完成后仍按 M11 顺序清理：IF=0、停止 timer、关闭 RCTL/TCTL 和 queue enable、屏蔽 MSI-X、撤销 bus mastering、逆序释放 10 个 DMA 页、核对 PMM 页数、注销 message slots。

M12 Release 已顺序通过 xAPIC 三次、x2APIC、64/128/256/512 MiB 和 UP/2/8/16 vCPU 矩阵；每次都包含 raw+ARP+ICMP 的外部响应。M10 SMP allocator/迁移/TLB、EDU、UART/PIT、PMM/heap 等回归保持。最终 artifacts 结果以 `artifacts/VALIDATION.txt` 为准。

## 9. 当前边界

- 固定 IP 和单一 peer，仅处理测试所需 ARP/ICMP，不维护邻居 cache；
- 仅 IPv4 IHL=5、无分片 Echo，不处理 options、分片重组、路由或其他 ICMP 类型；
- 不计算或卸载硬件 checksum，不使用多队列/RSS；
- 没有 UDP/TCP、DHCP、DNS、socket、阻塞 IO 或用户态网络接口；
- 没有 ring wrap/动态 buffer 回收、生产级 reset/PHY 管理或物理网卡验证；
- Windows/MSYS2 配置与跨平台 Python peer 保留，但 Windows 本地和远端 CI 未在本环境实际执行。
