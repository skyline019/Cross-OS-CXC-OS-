# CXC/OS 第三次中场评估：M25–M34 进度、证据与主线重选

日期：2026-10-25  
基线：Milestone 34 / v0.35.0  
对照：第二次中场（[`midpoint-assessment-2.md`](midpoint-assessment-2.md)）冻结在 **M14–M24**；本评估覆盖其后 **M25–M34**，并裁定阶段 0 收束后的主线。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)（A1 编译器可跑 / B 生产级内核）。  
本地实证口径：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；M13 Echo 串口仍为 10/10；`deliver/echo/recycle: 266/266/266`；`UDP IRQ RX submit: 266`；host `ctest` + ASan/UBSan/TSan（Linux CI）。当前 Release `BOOTX64.efi` ≈ 308636 字节，ESP 仍为 68157440 字节。

## 1. 结论摘要

第二次中场建议「网络主线优先薄 TX 提交」。本周期 **执行了该建议并完成阶段 0 收束**：

1. **Socket→线缆 TX 闭环**（M25–M28：submit / back-pressure / Event wait）；
2. **IRQ 路径 RX 闭环**（M29–M31：deliver / Echo TX / RDT recycle）；
3. **阻塞 IO 与 selftest 解耦各一刀**（M32 Event TX wait + Event 计数 delta；M33 `try_submit_rx_dd` + 独立 `submitted`）；
4. **验证纪律加固**（Full midpoint 根因修绿；host sanitizer 进 CI；死代码清理）。

相对第二次中场，质变是：

- 网络自检路径已从「BSP 轮询 + 帧构造」推进到 **真实 MSI-X 上的 demux/Echo/recycle**，并有独立于 Echo 串口的 submit 计数；
- TX 侧具备 leave-one-empty 背压与 **Event 形阻塞 wait**，不再只靠裸 `wait_for_interrupt` 合同；
- **一期总评与 A1/B 分叉已成文**；本中场裁定：**停止以网络薄切片为主线，切换到 A1 最小用户态脊柱**。

当前没有已知的、处于冻结 M13–M33 验收范围内的未决失败。这不表示生产网络栈或用户态 OS 已完成。

一句话状态：

| 维度 | 第三次中场判断 |
|---|---|
| M0–M24 定义内目标 | 仍完成且合同未降级 |
| M25–M33 增量 | 完成且有 QEMU/host/CI 证据 |
| Linux + QEMU 验证 | 强（默认路径 + Full midpoint 矩阵） |
| Windows / 远端 CI | **有 workflow 与构建门禁**；本笔记不以单次 artifact 冒充长期绿 |
| 网络子系统 | IRQ 合同路径闭环；**仍非**生产驱动/通用 socket 服务 |
| 调度/同步 | Event/Mutex 可用；计数合同已改 delta/`>=` |
| 下一主线 | **A2（第一目标）途经 A1 脊柱**；**B 为第二目标** |
| 最大风险 | 把 A1 当终点、延误可写 FS/libc；或并行开 TCP/VFS/用户态稀释证据 |

## 2. 相对第二次中场：增量盘点

### 2.1 TX 提交与背压（M25–M28）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M25 | `udp_send`→e1000e TX 提交 | `socket TX submit: 1/1`；不改 Echo/peer |
| M26 | M14 stress 路径 socket demux | `stress demux: 256/256` |
| M27 | leave-one-empty 满环拒绝 | `posted:7 rejects:1 drained:1` |
| M28 | 满环后 `post_tx_wait` | `waits:1 posted:1 drained:1` |

### 2.2 IRQ RX 路径（M29–M31）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M29 | RXQ0 内 `udp_deliver` | `delivered` 计入 266；BSP 仅 recv |
| M30 | IRQ 内 Echo 构造+TX | `deliver/echo: 266/266` |
| M31 | IRQ 内 RDT FIFO recycle | `deliver/echo/recycle: 266/266/266` |

边界：单 TX DMA 页串行；IRQ 最多超前 BSP 一帧；合同 Echo 地址/端口固定。

### 2.3 收束与解耦（M32–M33）+ 本中场（M34）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M32 | Event-backed TX wait；Event 合同 delta/`>=` | `events:1`；弱化绝对 Event 串口绑死 |
| M33 | `try_submit_rx_dd`（无 Echo/TestInfo） | 独立 `UDP IRQ RX submit: 266` |
| M34 | 第三次中场评估与主线重选 | 本文；版本锚点 v0.35.0 |

### 2.4 验证工程（同期，非整十里程碑编号）

| 项 | 状态 | 说明 |
|---|---|---|
| Full midpoint 根因修绿 | **完成多轮** | fault/IRQ/chaos/layout — 禁超时刷绿 |
| Host ASan/UBSan/TSan | **进 CI** | `CXC_HOST_SANITIZER`；TSan 经 `setarch -R` |
| 死代码清理 | **完成一刀** | 去掉未用 IRQ echo 旁路 |
| 一期总评/路线图 | **成文** | A1 vs B；阶段 0=本中场周期 |

## 3. 证据强度（第三次中场口径）

必须同时满足才可声称「本阶段回归通过」：

1. 内核串口 `CXC_OS_SELF_TEST_PASS`；
2. 外部 peer `peer_ok=1`（M13 十帧 + M14 256 stress）；
3. 冻结串口：M13 Echo、M24/M26 demux、M25–M28 TX、`266/266/266`、`submit: 266`、M32 `events:1`；
4. host `ctest`（`cxc::net`）通过；
5. Linux CI 默认路径含 sanitizer 矩阵（与 `.github/workflows/ci.yml` 同步）。

相对第二次中场，证据面 **扩展** 了 IRQ 路径与 Event TX wait，并 **收紧** 了「绝对 Event 计数」 fragility。

相对第二次中场，证据面 **仍未宣称**：

- 物理 NIC / 多队列 / 流控 / 可接受丢包；
- 通用生产 RX 线程或 `udp_send_wait` API；
- 用户态 / ELF / syscall；
- 存储 / VFS；
- 内核 freestanding ASan（仅 host 可测模块）。

## 4. 工程质量再评估

| 维度 | 第二次中场 | 第三次中场 | 说明 |
|---|---|---|---|
| 机制真实性 | 强 | 强 | MSI-X 上真实 demux/Echo/DMA |
| 正确性验收 | 强 | 强 | 合同未降级；独立计数增多 |
| 文档 | 强 | 强 | udp-ring-wrap M25–M33；一期路线图 |
| 模块化 | 中偏强 | **中偏强+** | `try_post_tx_frame` / `try_submit_rx_dd` 对称切口 |
| 可扩展性 | 中低 | 中低 | 单队列/固定槽/全局锁未变 |
| 跨宿主证据 | 未闭环 | **部分闭环** | CI workflow + sanitizer；长期绿仍靠远端 |
| 网络成熟度 | 早期 API + 测试 demux | **合同 IRQ 闭环** | 距生产栈仍远 |
| 调度成熟度 | 自检生命周期 | 同左 + Event 阻塞 IO 形状 | 距通用线程库仍远 |
| 通用 OS 完整度 | 早期 | 早期 | **用户态仍为零** — 下阶段主缺口 |

## 5. 技术债登记（第三次中场刷新）

### 已还清或主补完成（相对第二次中场 P1）

| ID / 主题 | 结论 |
|---|---|
| TX send 提交 | M25 完成 |
| stress demux | M26 完成 |
| TX 背压 / wait | M27–M28；M32 Event 化 |
| IRQ deliver/Echo/recycle | M29–M31 |
| Event 绝对计数脆弱 | M32 改为 delta + `>=` |
| selftest RX 提交耦合 | M33 抽出 `try_submit_rx_dd`（一小步，未拆完） |
| Host sanitizer | 进 CI |

### 仍开放（按优先级）

#### P0（切换主线后立即面对）

- **TD-08**：无用户 CR3 / 用户页表 — **A1 阻塞项**。
- **地址空间 ADR**：M35 已 Accepted（[`adr-user-address-space.md`](adr-user-address-space.md)）；实现从 M36 BSP 探针起。

#### P1（架构/验证）

- **TD-07**：全局 queue/PMM/heap 锁 — A1 可暂忍。
- **selftest≠生产**：M33 仅解耦 submit 一步；Echo/recycle/pacing 仍挂自检。
- **TD-01**：远端 CI 长期绿与 Windows QEMU 实证 — 保持门禁，不挡 A1 切片。

#### P2（平台与生产化）

- TD-09 e1000e 非生产驱动  
- TD-10 QEMU 特定行为可移植性  
- TD-11 PCI/IRQ 固定容量  
- 内核 freestanding sanitizer / 物理机  

### 产品缺口（诚实清单）

- 用户态 ELF、syscall、信号、多进程；
- 存储 / VFS / 只读根；
- TCP/IPv6、路由、ARP cache、DHCP/DNS、ephemeral 端口；
- 通用 `udp_send_wait`、中断线程化、多队列；
- condvar、优先级、工作窃取。

## 6. 主线重选（本中场裁定）

阶段 0（网络 IRQ 收束）**可以停手**。继续堆 M35+ 网络薄切片的边际收益低于打开用户态阻塞项。

| 选项 | 收益 | 风险 | 本中场决定 |
|---|---|---|---|
| A. 继续网络（TCP / send_wait / 多队列） | 网络更深 | 远离 A1；selftest 更厚 | **否**（非主线） |
| B. 存储最小块设备 | 为 A1 只读根铺路 | 与用户态并行会稀释 | **延后**：在 ELF 可加载需求出现时以只读切口进入 |
| C. A1 脊柱（服务 A2） | 通向机上编译器 | VMM/syscall/FS 面大 | **是 — 下一主线（A2 必经）** |
| D. 仅还 TD-01/文档 | 工程可信 | 不增加能力 | 穿插，不独占主线 |

**推荐切片顺序（单主线）**：

1. **用户地址空间 ADR**（用户 CR3、内核映射、syscall 入口栈）；  
2. **BSP 单核用户页表切换**探针；  
3. **静态 ELF64 加载**；  
4. **syscall v0**：`exit` / `write`(串口) / `yield`；  
5. 合同：`USER_PROGRAM_PASS`（独立于 `CXC_OS_SELF_TEST_PASS`）；  
6. 需要时再开 **只读根**（内存盘或块设备）加载 `hello.elf`。

退出标准：串口出现用户态 PASS；内核自检合同 **不回退**。

## 7. 最终评价

第二次中场证明了：在 M13 合同下能长出可测 socket API。

第三次中场证明了：同一合同下能把 **TX 提交、背压、Event wait、IRQ demux/Echo/recycle、generic submit** 收成可停手的网络自检栈，并把验证面扩到 sanitizer，而 **不** 假装这是生产 OS。

下一阶段成败标准：

- 默认 `qemu-test` 仍 `PASS` + `peer_ok=1`（网络合同冻结）；  
- 新增用户态能力有 **独立** `USER_PROGRAM_PASS`（或等价）；  
- 对外表述区分「合同 IRQ 栈」与「生产网络 / 通用 OS」。

**第三次中场建议：阶段 0 收束完成；主靶升级为 A2（机上完整编译），第二目标为生产级 B；执行上仍走 A1 脊柱（ADR → ring-3 → ELF → syscall → 可写 FS）；网络仅在服务该路径时再开。**

> **2026-10-25 目标确认**：第一目标 = 不依赖宿主机的机上编译器可编译运行任意程序（A2）；第二目标 = 完整生产级 OS（B）。

---

## 8. 节奏契约兑现

本中场窗口已核对：

1. 本文评估成文；  
2. CI `grep` / `contract_test.py` 与 M25–M33 串口同步（随各里程碑同变更集）；  
3. Full midpoint / sanitizer 问题按根因修复，无超时刷绿；  
4. 基线推进至 **M34 / v0.35.0**；下一正式中场目标约 **M44**（见 [`midfield-cadence.md`](midfield-cadence.md)）。
