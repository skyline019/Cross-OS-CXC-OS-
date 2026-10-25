# CXC/OS 第二次中场评估：M14–M24 进度、证据与下一步

日期：2026-10-25  
基线：Milestone 24 / v0.25.0  
对照：第一次中场（[`midpoint-assessment.md`](midpoint-assessment.md)）冻结在 **M0–M13**；本评估覆盖其后 **M14–M24** 与同期技术债冲刺。  
本地实证口径：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；M13 串口仍为 10/10 Echo，另含 `UDP socket demux deliver/recv: 10/10`；`ctest` host net PASS。当前 Release `BOOTX64.efi` ≈ 290002 字节，ESP 仍为 68157440 字节。

## 1. 结论摘要

第一次中场之后，项目没有把「功能数量」当作主线，而是在 **不削弱 M13 双向证据** 的前提下，完成了三条并行推进：

1. **验证与解耦债务还清**（TD-02/03/04/12 主补，TD-01 部分）；
2. **调度/线程生命周期闭环**（M15–M19：回收、Event、Mutex、栈页归还）；
3. **最小网络 API 栈**（M20–M24：datagram → socket → 阻塞 recv → send 帧构造 → M13 RX demux）。

相对第一次中场，最大的质变不是「更像完整 OS」，而是：

- **协议与驱动已分层**（`cxc::net` / `e1000e` / `e1000e_selftest`），后续网络能力有明确生长点；
- **阻塞原语与 socket 已接线**（Event → `udp_recv_wait`），但仍不是通用阻塞 IO；
- **验收纪律保持**：peer 逐帧、负对照、host 契约与 `ctest` 仍在默认路径上。

当前没有已知的、处于冻结 M13–M14 验收范围内的未决失败。这不表示生产级网络或通用 OS 已完成。

一句话状态：

| 维度 | 第二次中场判断 |
|---|---|
| M0–M13 定义内目标 | 仍完成且合同未降级 |
| M14–M24 增量 | 完成且有 QEMU/host 证据 |
| Linux + QEMU 验证 | 强（默认路径反复 PASS） |
| Windows / 远端 CI | 仍未闭环（配置≠实证） |
| 网络子系统 | 测试向 API + selftest demux；非生产驱动 |
| 调度/同步 | 自检模型闭环；非通用线程库 |
| 最大风险 | 把 selftest 路径误认为生产栈，或同时开存储/用户态/TCP |

## 2. 相对第一次中场：增量盘点

### 2.1 验证工程与解耦（第一次中场后优先项）

| 项 | 状态 | 证据 |
|---|---|---|
| TD-02 网络拆层 | **完成** | `include/cxc/net/*` + `src/net/*`；驱动与自检分离 |
| TD-03 发布入口 | **完成** | `tools/release_validate.py` host/ci/full |
| TD-04 host 协议测 | **完成主补** | `tests/host` + `ctest`；含 datagram/socket |
| TD-12 runner 拆分 | **完成** | `network_peer` / `qemu_runner` / 薄 `run_qemu` |
| TD-01 Windows/CI | **部分** | workflow 与 artifact 上传就绪；本环境无远端 run 实证 |
| TD-05 ring 压力 | **M14 主补** | 256 帧 stress + host RingModel；非 soak/流控 |

### 2.2 调度与线程生命周期（M15–M19）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M15 | Finished→Empty 回收、栈映射复用 | finished=reclaimed；可 recreate |
| M16/M17 | sticky Event + FIFO/`broadcast` | 串口 wait/signal/broadcast/wake |
| M18 | worker Mutex（争用走 Event） | lock/unlock/wait 账目 |
| M19 | Empty 槽栈页 unmap+PMM free | pages/TLB 账目闭合 |

边界仍在：BSP 创建、每 CPU 固定 3 槽、无 condvar、无非 BSP 创建、无优先级/hotplug。

### 2.3 网络 API 与 demux（M20–M24）

| 里程碑 | 能力 | 边界 |
|---|---|---|
| M20 | `parse_udp_datagram` / `build_udp_datagram` | 纯帧；echo 复用 |
| M21 | 固定 4 槽 bind/close + RX 深 4 | 非阻塞 recv；payload ≤64 |
| M22 | `udp_recv_wait` + 每槽 Event | worker 阻塞；host 用 Event stub |
| M23 | `udp_send` 填帧 | **不**提交 e1000e TX |
| M24 | M13 每帧 deliver+recv demux | stress 未 demux；Echo/peer 不变 |

`cxc::net` 现状文件：`checksum`、`proto`、`datagram`、`ring_model`、`socket`（5 header / 4 实现；ring 为头文件模型）。

## 3. 证据强度（第二次中场口径）

必须同时满足才可声称「本阶段回归通过」：

1. 内核串口 `CXC_OS_SELF_TEST_PASS`；
2. 外部 peer `peer_ok=1`（含 M13 十帧与 M14 256 stress 合同）；
3. host `ctest`（`cxc::net`）通过。

相对第一次中场，证据面 **扩展** 了：

- M14：独立 `UdpStressResult`，不改写 M13 10/10；
- M15–M19：runtime 串口合同（Event/Mutex/栈页）；
- M20–M23：host 往返 + runtime socket 合同；
- M24：`socket demux deliver/recv: 10/10`。

相对第一次中场，证据面 **未宣称**：

- 远端 CI 真实执行；
- Windows 本机/CI QEMU 启动；
- 物理 NIC、多队列、流控、可接受丢包；
- 通用 RX 路径（非 selftest）、TX 驱动提交；
- sanitizer/ASan 覆盖 freestanding 内核。

历史 M13 artifacts 的 SHA-256 交付集仍以当时 `artifacts/` 为准；本评估不以「镜像字节变大」否定旧哈希，也不把旧 artifacts 冒充 v0.25.0 交付物。

## 4. 工程质量再评估

| 维度 | 第一次中场 | 第二次中场 | 说明 |
|---|---|---|---|
| 机制真实性 | 强 | 强 | 仍无宿主兼容层冒充内核 |
| 正确性验收 | 强 | 强 | 合同未降级；增量有独立计数 |
| 文档 | 强 | 强 | 里程碑边界写入 README/udp-ring-wrap |
| 模块化 | 中 | **中偏强** | net 分层落地；selftest 仍厚 |
| 可扩展性 | 中低 | 中低 | 固定槽/全局锁/单队列未变 |
| 跨宿主证据 | 未闭环 | 未闭环 | TD-01 仍是最大外部空洞 |
| 网络成熟度 | 早期测试路径 | **早期 API + 测试 demux** | 距生产栈仍远 |
| 调度成熟度 | 静态自检 | **自检生命周期闭环** | 距通用线程模型仍远 |
| 通用 OS 完整度 | 早期 | 早期 | 无用户态/存储/VFS |

## 5. 技术债登记（第二次中场刷新）

### 已还清或主补完成（不再当作阻塞下一功能的理由）

| ID | 结论 |
|---|---|
| TD-02 | 拆层完成 |
| TD-03 | 发布入口完成 |
| TD-04 | host C++ 测完成主补 |
| TD-05 | M14 256 帧主补；soak/流控仍开放但不阻塞薄切片 |
| TD-06 | M15–M19 主线闭环；condvar 另开 |
| TD-12 | runner 拆分完成 |

### 仍开放（按优先级）

#### P0

- **TD-01**：Windows 构建与远端 CI **无本环境实际执行证据**。不能把 workflow 写成已通过。

#### P1（架构/验证，加功能前应清醒面对）

- **TD-07**：全局 queue/PMM/heap 锁 — 正确性优先，无性能基准前勿「假并行」。
- **selftest≠生产路径**：M24 demux 仅挂在 M13 自检循环；通用 RX/TX 提交仍缺。
- **Event 计数耦合**：M22 抬高了全局 Event 账目合同；后续阻塞 IO 需避免脆弱的绝对计数，或改为增量断言。

#### P2（平台与生产化，与第一次中场基本一致）

- TD-08 受限 VMM / 无用户 CR3  
- TD-09 e1000e 非生产驱动  
- TD-10 QEMU 特定行为可移植性  
- TD-11 PCI/IRQ 固定容量  

### 新出现的「产品缺口」（不是偷工减料）

- e1000e **TX `send` 提交**（M23 只构造帧）；
- M14 stress / 通用路径上的 socket demux；
- ephemeral 端口、ARP cache、DHCP/DNS、路由/分片、TCP/IPv6；
- 存储 / VFS；用户态 / syscall / ELF；
- condvar、通用阻塞 IO、中断线程化、自动迁移。

## 6. 主线选择建议

第二次中场时，验证工程与 TD-06/网络薄切片已推进完毕。下一主线应 **只选一条**，并继续守住 PASS/`peer_ok=1`：

| 选项 | 收益 | 风险 | 建议切片 |
|---|---|---|---|
| A. e1000e TX send 提交 | 打通 socket→线缆闭环 | 易碰 Echo/peer 合同 | 仅额外测试帧或独立阶段计数；默认 Echo 路径先不动 |
| B. 存储最小块设备 | 打开新子系统 | 与网络并发会稀释证据 | 另开里程碑；先只读探测 |
| C. 用户态/ELF 探针 | 向「OS」形态跃进 | VMM/syscall 面极大 | 需先有地址空间 ADR |
| D. TD-01 证据闭环 | 降低跨宿主不透明性 | 不增加内核能力 | 触发一次真实 CI 并归档 |

**推荐判断**：

1. 若目标是网络主线收束 → **A（TX 提交）**，切片必须独立计数、不改写 M13 10/10。  
2. 若目标是工程可信度 → 穿插 **D（CI 实证）**，可与 A 并行且不改内核。  
3. **不要**同时开存储与用户态；第一次中场的「单主线」纪律仍然有效。

## 7. 最终评价

第一次中场证明了：CXC/OS 能在 QEMU 上把 **真实 SMP + 设备 DMA + 外部 peer** 做成可重复合同。

第二次中场证明了：在同一合同下，项目还能 **还债、拆层、长出可测试的 socket API，并接到自检 RX demux**，而不是靠堆单体驱动。

下一阶段的成败标准不应是里程碑编号继续涨，而是：

- 任一切片后默认 `qemu-test` 仍 PASS + `peer_ok=1`；
- 新增能力有独立计数或 host 测，不吞掉 M13/M14 证据；
- 对外表述区分「selftest 已通」与「生产栈已有」。

**第二次中场建议：保持验收不降级；网络主线优先薄 TX 提交或先做 CI 实证；存储/用户态另开且单主线。**

---

## 8. 节奏契约更新

自第二次中场起，项目采用显式节奏（见 [`midfield-cadence.md`](midfield-cadence.md)）：

- **每约 10 个里程碑**一次正式中场；
- 中场窗口内 **统一 CI、修问题、同步测试**；
- 里程碑落地时测试/CI 断言与串口证据同变更集更新。

后续兑现：M25 已完成选项 A（TX 提交）与选项 D（CI 实证）；**第三次中场已在 M34 完成**，见 [`midpoint-assessment-3.md`](midpoint-assessment-3.md)。
