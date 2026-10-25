# 一期总评与后续规划（M0–M31）

**基线**：Milestone 61 / v0.62.0（2026-10；mini-libc uname；主靶 A2 / 次靶 B；ADR 见 [`adr-user-address-space.md`](adr-user-address-space.md)）。  
**证据契约**：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；Full midpoint（正确/失败/掉电/事务/循环/高压/混沌）绿；host `ctest` + ASan/UBSan/TSan（Linux CI）。  
**中场节奏**：见 [`midfield-cadence.md`](midfield-cadence.md)；第五次中场已完成（M54）；下一正式中场目标 **M64**。

本文回答两件事：（1）与生产级 OS 的真实差距；（2）若目标是「编译器/用户程序可跑」或「生产级内核」，后续应怎么排主线。

---

## 1. 一期总评：我们实际站在哪里

### 1.1 已证明的强项（不要低估）

| 维度 | 现状 |
|---|---|
| 启动真实性 | 原生 UEFI → higher-half PE32+，无 Linux/Windows 宿主兼容层冒充内核 |
| SMP | MADT 枚举、INIT-SIPI、每 CPU 调度/锁/PMM·heap、TLB shootdown、迁移槽位 |
| 设备与 IRQ | PCI 机制 1、IOAPIC、MSI/MSI-X、UART/EDU/e1000e 真实向量交付 |
| 网络 DMA | queue0 legacy ring、外部 peer 逐字节校验、ARP/ICMP/UDP、IRQ demux/Echo/RDT recycle |
| 验收纪律 | 合同不降级；根因修绿；禁止超时/删断言「刷绿」；host sanitizer 已进 CI |

一句话：**这是一个「机制可重复、证据可归档」的早期内核实验床**，不是玩具模拟器，也还不是可交付的通用 OS。

### 1.2 一期边界（必须写进对外表述）

已有能力几乎都挂在 **BSP 自检 / 固定合同路径** 上：

- 调度：固定每 CPU 槽位；无 work stealing、无非 BSP 创建、无优先级/实时类。
- 同步：Event + 非递归 Mutex；无 condvar、无优先级继承、无通用阻塞 IO 抽象。
- VMM：共享内核 root；无用户 CR3、无通用并发建映射、无按需分页/COW。
- 网络：固定地址 Echo；无 ephemeral、ARP cache、DHCP/DNS、路由、分片、TCP/IPv6；单队列、无 offload/完整 PHY/reset。
- 存储/VFS/用户态/ELF/syscall/GUI：**全部缺失**。
- 平台：以 QEMU + OVMF 为主证据；物理机/其他 NIC 未闭环。

**selftest ≠ 生产路径** 仍然是最大认知风险：M29–M31 的 IRQ 路径很真，但仍服务「合同 Echo」，不是通用 socket 服务模型。

### 1.3 与生产级 OS 的差距总表

按「能上生产」通常需要的子系统打分（主观、用于排期，不是嘲讽）：

| 子系统 | 生产级典型要求 | CXC/OS M31 | 差距量级 |
|---|---|---|---|
| 进程与用户态 | 隔离地址空间、syscall、信号、多进程 | 无 | **鸿沟** |
| 内存管理 | 用户页表、缺页、换页/策略、IOMMU | 内核 VMM 探针级 | **鸿沟** |
| 文件系统 | VFS、块设备、缓存、权限 | 无 | **鸿沟** |
| 存储驱动 | AHCI/NVMe、分区、错误恢复 | 仅 EDU DMA 探针 | **鸿沟** |
| 网络栈 | TCP/IP、路由、socket API、多设备 | UDP Echo + 测试 demux | **很大** |
| 驱动模型 | 总线/电源/热插拔、统一 DMA API | 手写 PCI 测试驱动 | **很大** |
| 调度 | 公平/实时类、负载均衡、cgroup 类 | 固定槽自检调度 | **很大** |
| 安全 | 权限、隔离、审计、ASLR/KASLR 等 | 基本页权 NX/RX | **很大** |
| 可观测/运维 | 日志管道、指标、崩溃转储、热升级 | 串口合同日志 | **大** |
| 工具链宿主 | 在 OS 上跑编译器/libc/包管理 | 无用户态 | **鸿沟** |
| 验证 | fuzz、多平台、长期 soak | QEMU 合同 + host sanitizer | **中**（方向对，覆盖面窄） |

结论：相对 Linux/NT/Darwin，CXC/OS 仍在 **「内核机制课」后半段 / 「最小可用系统」前半段**。相对很多 hobby OS，它在 **SMP + 真实设备 IRQ/DMA + 外部 peer 合同** 上更硬；相对「能编译并跑程序的 OS」，还缺整层用户态与存储。

---

## 2. 两个目标的定义（先选靶，再铺路）

两个目标 **不是同一条路的两个牌子**，收敛点不同。

### 目标 A — 「完整编译器可用运行」（Compiler-hostable / userspace-runnable）

**最小可宣称定义（建议写成合同）**：

1. 能加载并运行一个 **用户态 ELF**（静态链接优先）；
2. 有最小 **syscall** 面：至少 `exit` / `write`(或串口等价) / `mmap` 或固定堆 / `open-read` 对某个只读「根」；
3. 能跑通一条 **宿主编好的 freestanding 或 newlib/musl 极简程序**；
4. 「编译器可用」分两级，勿混谈：  
   - **A1（程序可跑）**：用户程序在 CXC/OS 上执行；编译仍在 Linux/Windows 交叉完成。  
   - **A2（机上编译）**：在 CXC/OS 上跑 clang/gcc + 读写真文件系统 —— 需要存储、更多 POSIX、内存与进程模型，工作量接近半个教学 Unix。

**组织目标（2026-10-25 锁定）**：

| 优先级 | 目标 | 含义 |
|---|---|---|
| **第一** | **A2** | **不依赖宿主机**：在 CXC/OS 内部用完整工具链编译并运行任意（合理范围内）程序 |
| **第二** | **B** | 完整生产级操作系统（驱动模型、隔离、运维、多平台证据等） |

A1（交叉编译用户程序可跑）是通向 A2 的 **必经脊柱**，不是终点：无用户态/ELF/syscall/可写存储/libc，就不可能机上养编译器。

### 目标 B — 「生产级内核」

**可宣称定义应至少包括**：

- 多核正确性 + 故障隔离（设备/驱动错误不拖垮全局）；
- 通用驱动模型与至少一类生产存储 + 一类生产网络路径；
- 用户态隔离与稳定 syscall ABI；
- 可运维证据（崩溃现场、回归矩阵、多平台或至少非单一 QEMU 配置）；
- 安全基线与长期 soak；不是「合同绿」 alone。

B 的工期以 **人年** 计；不能用薄切片里程碑编号假装到达。

### 推荐战略定位

| 组织目标 | 主靶 | 内核仍要做的 |
|---|---|---|
| **不依赖宿主的机上编译（第一目标）** | **A2**，途经 A1 | 用户页表 → ring-3 → ELF → syscall → 可写 FS → musl/libc → 移植 clang/gcc |
| **完整生产级 OS（第二目标）** | **B**，与 A2 共享 ABI | 在稳定 syscall/FS 之上补驱动框架、TCP、权限、soak、多平台 |
| 只刷网络里程碑 | 偏离 A2/B | 禁止作为主线 |

**总评裁定：主靶 = A2；第二北极星 = B；当前切片仍走 A1 脊柱（M35–…），每一步都要能解释「如何缩短到机上编译器」。**

---

## 3. 后续规划：分层路线图

原则沿用项目纪律：**单主线、薄切片、合同不降级、根因修绿**。

### 阶段 0 — 收束第三次中场（约 M32–M34）

**目的**：把网络 IRQ 栈收成「可停手」的状态，并写第三次中场评估。

| 候选切片 | 价值 | 备注 |
|---|---|---|
| Event 版 TX wait / 弱化绝对 Event 计数 | 阻塞 IO 形状更真 | 避免再绑死全局绝对串口数 |
| 通用 RX 提交路径与 selftest 解耦的一小步 | 减轻 selftest≠生产 | 独立计数，不改 M13 Echo 字符串 |
| 第三次中场 `midpoint-assessment-3.md` | 债项与主线重选 | M34 窗口必做 |

**不在此阶段开**：用户态、TCP、完整 VFS。

### 阶段 1 — 通向 A2 的 A1 脊柱（当前主线）

按依赖排序（可再拆成多个 M）：

1. ~~**地址空间 ADR**~~（M35 Accepted）；  
2. ~~**用户页表 + CR3 切换探针**~~（M36 CPL0）；  
3. **ring-3 + TSS.RSP0**（M37）：证明 CPL3 进入/返回；  
4. **ELF64 加载器**（静态）+ **syscall v0**（`exit` / `write` / `yield`）；合同 `USER_PROGRAM_PASS`；  
5. **只读根** → 很快升级为 **可写最小 FS**（A2 硬依赖）；  
6. 宿主交叉工具链仅作引导；最终证据是机上编译。

退出标准（阶段 1）：`USER_PROGRAM_PASS` + 内核自检不回退。

### 阶段 2 — 养工具链（直指 A2）

- 可写存储 + 目录 + 足够 POSIX（`open/read/write/close`、`brk`/`mmap`、进程或至少多线程）；  
- musl（或 newlib）子集；  
- 内存与进程模型能撑起 clang/gcc 链接规模；  
- 网络非本阶段主线（串口/块设备优先）。

### 阶段 3 — 宣称 A2，并启动 B 并行面

- **A2 退出**：在 CXC/OS 上用自带工具链编译并运行非玩具程序（合同另定，如 `ONBOX_COMPILER_PASS`）。  
- **B 并行**：virtio/NVMe、驱动框架、TCP/IP、权限、观测、soak；**不**阻断 A2 收口。

### 阶段 4 — 「生产级」清单（北极星，非近期 milestone 冲刺）

- 多架构或至少多机型；物理机或云镜像证据；  
- 驱动错误隔离、看门狗、崩溃转储；  
- 安全与供应链（签名启动可选）；  
- 长期 chaos/soak 与外部合规需求（按产品定）。

---

## 4. 债项与规划的交叉（刷新到 M31）

| ID / 主题 | M31 状态 | 对 A1 / B 的含义 |
|---|---|---|
| TD-01 跨宿主 CI 实证 | CI 已跑 Linux+Windows；表述可更新为「有远端证据」 | A1/B 都需要保持 |
| TD-07 全局锁 | 仍在 | A1 可暂忍；B 需性能与扩展策略 |
| TD-08 用户 CR3 / VMM | **A1 阻塞项** | 阶段 1 主线 |
| TD-09 e1000e 非生产 | 可继续作合同网卡 | B 需 virtio/多队列或真实 NIC 策略 |
| TD-11 固定 IRQ 容量 | 可暂忍 | B 需可扩展 |
| selftest≠生产 | M29–M31 改善 IRQ 路径，仍合同导向 | A1 可并行；B 必须拆生产 API |
| 无存储 | **A1 软阻塞 / A2·B 硬阻塞** | 阶段 1 末或阶段 2 初 |
| host sanitizer | 已进 CI（内核 freestanding 仍无 ASan） | 继续扩大可宿主模块覆盖 |

---

## 5. 明确不做什么（保护主线）

1. **不同时**开「用户态 + TCP 全栈 + 完整 VFS」。  
2. **不**用加超时/删 Full midpoint 用例换绿。  
3. **不**把 QEMU Echo 合同改写成「已有生产网络栈」。  
4. **不**在无 ADR 的情况下引入用户 CR3「顺便重构」整个 PMM。  
5. **不**宣称 A2/生产级，直到对应退出标准有串口/合同证据。

---

## 6. 一期最终判断

| 问题 | 判断 |
|---|---|
| 与生产级 OS 差多远？ | **子系统级鸿沟**（用户态/存储/通用网络/驱动模型）；机制层已超过多数入门内核。 |
| 能否很快「编译器机上可用」？ | **不能很快到 A2**；**可以规划到 A1**（交叉编译 + 用户 ELF），前提是把主线从网络薄切片转向地址空间/ELF/syscall。 |
| 下一刀切哪？ | **主靶 A2**：继续 A1 脊柱（M37 ring-3 → ELF/syscall → 可写 FS → libc → 机上编译器）；B 为第二目标并行面。 |
| 成功标准怎么变？ | **三层合同**：`CXC_OS_SELF_TEST_PASS` 不回退 → `USER_PROGRAM_PASS`（A1）→ 机上编译合同（A2）；B 另立生产级清单。 |

---

## 7. 与现有文档的关系

- 第一次/第二次中场：[`midpoint-assessment.md`](midpoint-assessment.md)、[`midpoint-assessment-2.md`](midpoint-assessment-2.md)  
- 节奏：[`midfield-cadence.md`](midfield-cadence.md)  
- 能力与边界清单：[`README.zh-CN.md`](README.zh-CN.md)  
- 第三次中场（M25–M34）：[`midpoint-assessment-3.md`](midpoint-assessment-3.md) 已裁定正式切换主线到 A1。
- 第四次中场（M35–M44）：[`midpoint-assessment-4.md`](midpoint-assessment-4.md) 确认 A1 最小闭环；下一刀外部 ELF / libc。
- 第五次中场（M45–M54）：[`midpoint-assessment-5.md`](midpoint-assessment-5.md) 确认外部 ELF→libc 缺口→mini_libc；下一刀加深 mini-libc / CRT。
