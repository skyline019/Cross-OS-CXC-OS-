# UDP Echo、FIFO RX 回收与真实 Ring Wrap

## 1. M13 范围

Milestone 13 / v0.14.0 在 M11 原始帧和 M12 ARP/IPv4/ICMP 之后，继续使用同一组 queue-0 legacy rings 完成十个变长 UDP Echo 请求/响应。目标是验证：

- RX/TX descriptor head 与 tail 的真实模 8 推进；
- 只有七个 RX buffer pages 时的动态 FIFO 回收；
- IPv4/UDP checksum、奇数长度、最短 Ethernet 帧 padding；
- 分阶段 MSI-X 计数和独立外部 peer 证据。

这不是 socket API 或通用网络栈。没有 TCP、DHCP、ARP cache、IP 分片重组、多队列、checksum offload 或异步网络线程。

## 2. 进入 UDP 时的真实 ring 状态

RX/TX rings 各有 8 个 16 字节 descriptors。初始只给 RX descriptor 0–6 分配七页 buffer，descriptor 7 为空，`RDT=7`；不是 6。raw、ARP、ICMP 分别消费 descriptor 0、1、2，因此 UDP 入口必须为：

| 寄存器 | 值 | 含义 |
|---|---:|---|
| RDH | 3 | 下一个 RX descriptor 为 3 |
| RDT | 7 | descriptor 3–6 共四个可用 |
| TDH | 3 | 前三个 TX 已完成 |
| TDT | 3 | 下一个 TX descriptor 为 3 |

QEMU e1000e 以 `RDH==RDT` 表示无可用 RX descriptor。不能把 tail 当作已消费 descriptor 的编号，也不能在 head==tail 时假设还剩一个 buffer。

两个 ring 页在发布地址前显式清零。CPU 可见的 `rx_buffers` 数组有 **8 项**，因为 descriptor 7 会在回收时获得页面；七个物理 RX pages 的数量没有改变。

## 3. FIFO 回收算法

令第 `i` 个 UDP datagram 从 0 开始：

- 当前 RX/TX descriptor：`(i + 3) mod 8`；
- 完成后的 RX/TX head：`(i + 4) mod 8`；
- 最早已消费、此次回收的源 descriptor：`i mod 8`；
- 重新发布该页的目标 descriptor（旧 RDT）：`(i + 7) mod 8`；
- 新 RDT：`i mod 8`。

每次收到并验证请求、完成 Echo reply 后：

1. 检查 FIFO 源 descriptor 已经有 DD，目标 descriptor 的地址和状态为空；
2. 保存源物理页，清源 descriptor 和对应 CPU 指针；
3. 将该页前 2048 字节清零，再恢复前 128 字节的 `0xA5` guard；
4. 把物理页地址写入目标 descriptor，清 length/checksum/status/errors/special；
5. release fence 后将 RDT 模 8 加一。

当前请求对应的 descriptor 保持已消费状态，直到它自己的 FIFO 回收轮次。每帧消费一个、补充一个，因此始终有四个可用 RX descriptors；七个物理页保持不同，不能通过扩大 rings 或增加 pages 回避 wrap。

完整序列：

| i | RX/TX descriptor | 完成后 head | FIFO source → target | 新 RDT |
|---:|---:|---:|---:|---:|
| 0 | 3 | 4 | 0 → 7 | 0 |
| 1 | 4 | 5 | 1 → 0 | 1 |
| 2 | 5 | 6 | 2 → 1 | 2 |
| 3 | 6 | 7 | 3 → 2 | 3 |
| 4 | 7 | 0 | 4 → 3 | 4 |
| 5 | 0 | 1 | 5 → 4 | 5 |
| 6 | 1 | 2 | 6 → 5 | 6 |
| 7 | 2 | 3 | 7 → 6 | 7 |
| 8 | 3 | 4 | 0 → 7 | 0 |
| 9 | 4 | 5 | 1 → 0 | 1 |

因此最终确定状态是：

- RX/TX heads：`5/5`；RX/TX tails：`1/5`；
- RX head wrap=1，TX head wrap=1；
- RX tail wrap=2（第 0、8 帧的 `7→0`），TX tail wrap=1；
- recycled RX buffers=10。

从初始 RDT=7 做十次单调模 8 回收，数学上最终就是 `(7+10) mod 8 = 1`。不通过额外 tail 写或倒退 tail 来制造其他计数。

## 4. UDP 测试帧与 checksum

固定地址沿用 M12：NIC `10.0.12.2` / `52:54:00:43:58:43`，peer `10.0.12.1` / `02:43:58:43:11:01`。peer 源端口为 `0xC123`，NIC Echo 目的端口为 7。

| datagram | payload | Ethernet frame bytes |
|---:|---:|---:|
| 0–6 | 1, 2, 3, 7, 8, 15, 16 | 各 60 |
| 7 | 31 | 73 |
| 8 | 32 | 74 |
| 9 | 33 | 75 |

payload 总计 148 字节，十个请求和十个响应各总计 642 字节。IPv4 identification 为 `0x5500+i`，请求 TTL=53，响应 TTL=64。

内核对每个请求检查：

- DD+EOP、errors=0、预期长度和 RX head；
- 目的/源 MAC、EtherType IPv4、IHL=5、DSCP=0；
- IPv4 total length、无分片、非零 TTL、protocol=17、源/目的 IPv4；
- IPv4 header checksum；
- UDP 源/目的端口、UDP length 与 IPv4 length 的一致性、非零 checksum；
- 含源/目的 IP、protocol、UDP length 的 pseudo-header checksum；
- 短帧中 IPv4 数据结束后的全部 Ethernet padding 必须为零；
- 对应 RX page guard 未被 DMA 越界修改。

Echo reply 复制 identification、UDP payload 等必要字段，交换 Ethernet/IP 地址和 UDP 端口，TTL 改为 64。IPv4 与 UDP checksum 字段分别清零后重算；奇数 UDP 长度只在 checksum 运算末尾补一个零字节，不把该字节计入 UDP length。若计算出的 UDP checksum 为零，线上字段写 `0xFFFF`。TX buffer 先按完整响应长度清零，保证最短帧 padding。

外部 peer 独立检查每个响应的地址、端口、identification、TTL、长度、两个 checksum、payload 和 padding，并保存全部请求/响应 hex。

## 5. 阶段同步、统计与失败诊断

peer 依次等待 `packet-ready`、`protocol-ready`、`udp-ready`，正式成功运行先发送 13 个请求：raw 1、ARP 1、ICMP 1、UDP 10。M13 UDP 阶段要求：

- request/reply=`10/10`；RX/TX IRQ=`10/10`；
- payload=148，RX/TX frame bytes=`642/642`；
- 上述 heads/tails/wrap/recycle 不变量全部满足；
- M11 raw 仍为 `1/1`，M12 协议 heads=`3/3`、IRQ=`2/2`；
- 所有 vector errors=0。

M13 串口证据冻结在 `UdpResult` 后，内核再发 `udp-stress-ready`，进入 **M14 / v0.15.0** 同环 256 帧压力（datagram index 10…265，payload 长度循环 1/2/3/7/8/15/16/31/32/33）。`UdpStressResult` 独立计数，不得改写 M13 的 10/10：

- stress request/reply=`256/256`；RX/TX IRQ=`256/256`；
- wraps RXh/TXh/RXt/TXt=`32/32/32/32`；recycle=256；
- peer `validation_scope=m14-raw-arp-icmp-udp-ring-stress`，`requests_sent=269`，`udp_stress_replies_validated=256`。

`UdpResult.failure` / `UdpStressResult.failure` 保留失败位置：1=ring/FIFO 前置状态，2=RX 等待，3=RX 内容/descriptor/guard，4=TX 提交，5=TX head/tail/IRQ/vector，6=RDT 发布，7=UDP 入口状态，8=最终汇总不变量。`datagram` 和 RX/TX descriptor 字段给出具体位置。

不能在等待前要求当前 RX status 仍为 0：peer 在 `udp-ready` / `udp-stress-ready` 后可以立即完成 DMA。主路径应等待并验证 DD，而不是与已经到达的合法包竞争。

## 6. 负对照与清理

除既有 no-device/no-peer/no-protocol/bad-ICMP 外：

- `--no-udp-peer`：raw+ARP+ICMP 完整成功，peer 停在 M12 scope；内核在 UDP datagram 0 等待并预期 panic；
- `--bad-udp-checksum`：翻转第一个 UDP checksum，内核可观察到 RX head=4 和一次 RX IRQ，但 UDP request/reply=`0/0`、TX IRQ=0，不发送响应并预期 panic。

故障与掉电负对照在完成 M13 十帧（或更早失败）后**不要求** 256 帧 stress；正常成功必须同时有内核 PASS、`m14-raw-arp-icmp-udp-ring-stress` peer 与 `peer_ok=1`。

清理仍在 IF=0 后停止 timer/RCTL/TCTL/queue enable，屏蔽 MSI-X、撤销 bus mastering，再逆序释放两个 ring 页、七个 RX 页和一个 TX 页，核对 PMM 页数精确恢复。

## 7. 验证口径与边界

正式结果只取包含上述修复的全新最终 Debug/Release 构建及 `artifacts/VALIDATION.txt`。开发期曾出现七项 `rx_buffers` 数组被 descriptor 7 越界访问的问题；包含该问题的早期日志不能作为交付证据。

仅实测 QEMU 82574L 和本机 dgram backend。dgram 是测试硬件后端，不是宿主网络兼容层；Windows/MSYS2 构建配置保留，但不声称已在本环境执行 Windows 构建或远端 CI。

## 8. M20 帧级 datagram API

Milestone 20 / v0.21.0 在不改变 M13–M14 外部证据的前提下，抽出 `cxc::net::parse_udp_datagram` / `build_udp_datagram`（`include/cxc/net/datagram.hpp`）。`build_udp_echo_reply` 经该 API 构造回包；host `ctest` 覆盖任意端口往返与 echo 路径。

## 9. M21 固定 socket 表

Milestone 21 / v0.22.0 增加 `cxc::net` 固定 4 槽 UDP socket（`udp_bind`/`udp_close`/`udp_deliver`/`udp_recv`），每槽 RX 深 4、payload 上限 64。demux 仅按目的端口；满队列计 `dropped`。runtime 串口合同与 host 测覆盖；**不**改 e1000e Echo 热路径或 peer 帧数。

## 10. M22 阻塞 recv

Milestone 22 / v0.23.0 为每槽增加 sticky `scheduler::Event rx_gate`：`udp_deliver` 入队后 `signal`；worker 可 `udp_recv_wait`。runtime：worker0 wait、worker1 sleep 后 deliver；Event 账目含 M22 增量。

## 11. M23 socket send

Milestone 23 / v0.24.0 增加 `udp_send`：从绑定槽取 `local_port`，调用 `build_udp_datagram` 写入调用方缓冲。payload ≤64；不提交 e1000e TX。host 覆盖 send→parse 与双槽 deliver；runtime 合同并入 `udp_socket_contract_ok`。

## 12. M24 e1000e RX demux

Milestone 24 / v0.25.0 在 M13 UDP selftest 中 bind `nic_udp_port`，每帧校验通过后 `udp_deliver` + 立即 `udp_recv`；`UdpResult.socket_delivered/received` 须为 10/10。Echo reply 与 peer 合同不变；M14 stress 不走 demux。

## 13. M25 socket TX submit

Milestone 25 / v0.26.0 在 M14 stress 冻结后：bind `0x4D25`，`udp_send` 写入 TX DMA 页，经既有 `submit_tx_frame` 提交；`UdpSocketTxResult` 独立计数 frames/IRQs/bytes/head/tail。不改写 M13 Echo 热路径与 peer 合同；仍无通用生产 TX 背压或非 selftest RX 路径。

## 14. M26 stress RX demux

Milestone 26 / v0.27.0 将 M24 的 `udp_deliver`+`udp_recv` 延伸到 M14 stress：复用 `nic_udp_port` 绑定与文件内 `demux_validated_udp_rx`；`UdpStressResult.socket_delivered/received` 须为 256/256。M13 demux 10/10、Echo/peer、M25 TX 串口保持不变；仍非通用 IRQ RX 路径。

## 15. M27 TX back-pressure

Milestone 27 / v0.28.0：`tx_ring_free`/`tx_ring_full`（leave-one-empty）；`try_post_tx_frame` 满环拒绝推进 TDT。M25 后短暂关闭 TCTL 填满 7 槽、第 8 次 reject、再开 TCTL 按 TDH==TDT 排空（TX IRQ 可能合并，不以 IRQ 数=7 为合同）；串口 `posted:7 rejects:1 drained:1 heads/tails:5/5`。不改写 M25 `1/1` 串口。

## 16. M28 TX wait

Milestone 28 / v0.29.0：`post_tx_wait` 在满环时 BSP `wait_for_interrupt` 重试直到 `try_post` 成功。M27 后再次 TCTL 冻结填满、同步 reject 记 `waits:1`、开 TCTL 后 `post_tx_wait` 提交 1 帧并 TDH==TDT 排空；串口 `waits:1 posted:1 drained:1 heads/tails:5/5`。不改写 M25/M27 串口；仍非 Event/`udp_send_wait` 通用阻塞 IO。

## 17. M29 IRQ RX demux

Milestone 29 / v0.30.0：M13/M14 的 `udp_deliver` 改在 RXQ0 MSI-X 中完成（按 `buffer_address` 扫 DD 描述符）；BSP 仅 `udp_recv` 排空。`irq_rx.delivered` 须为 266（10+256）；M24/M26 `deliver/recv` 串口合同不变。仍非通用生产 RX 线程化路径。
