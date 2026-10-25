# CXC/OS 第四次中场评估：M35–M44 进度、证据与下一主线

日期：2026-10-25  
基线：Milestone 44 / v0.45.0  
对照：第三次中场（[`midpoint-assessment-3.md`](midpoint-assessment-3.md)）冻结在 **M25–M34** 并裁定切换到 A1 脊柱；本评估覆盖其后 **M35–M43** 能力切片，并以 **M44** 作为本中场窗口本身。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；地址空间 ADR：[`adr-user-address-space.md`](adr-user-address-space.md)。  
本地实证口径：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；冻结网络合同（M13 Echo 10/10、M14 stress、`266/266/266`、`submit: 266` 等）**不回退**；新增独立用户态/FS/内存合同串口（见 §3）。

## 1. 结论摘要

第三次中场建议「停手网络薄切片，走 A1 脊柱服务 A2」。本周期 **按该建议执行并跑通最小用户态闭环**：

1. **地址空间契约与探针**（M35 ADR → M36 用户 CR3 → M37 ring-3/TSS/`int 0x80`）；
2. **静态 ELF + syscall v0**（M38 `write`/`exit` → `USER_PROGRAM_PASS`）；
3. **可写内存盘与 POSIX 切口**（M39 open/read/write/close → M42 `mkdir`/嵌套路径）；
4. **用户堆与匿名映射**（M40 `brk` → M41 `mmap` → M43 `munmap`）；
5. **本中场窗口**（M44）：评估成文、基线锚点、CI/合同与节奏表同步。

相对第三次中场，质变是：

- 从「用户态为零」推进到 **CPL3 可加载静态 ELF、可 syscall、可读写文件、可 brk/mmap/munmap**；
- 主靶 **A2（机上编译）** 仍未到达，但 A1 脊柱上阻塞项（用户 CR3 / ring-3 / ELF / 最小 FS / 堆）已有 **可回归串口合同**；
- 网络合同路径 **整段冻结保持绿**，未用降级断言换用户态进度。

当前没有已知的、处于冻结 M13–M43 验收范围内的未决失败。这不表示 musl/libc、机上编译器或生产 VFS 已完成。

一句话状态：

| 维度 | 第四次中场判断 |
|---|---|
| M0–M34 定义内目标 | 仍完成且合同未降级 |
| M35–M43 增量 | 完成且有 QEMU 串口 + peer 证据 |
| Linux + QEMU 验证 | 强（默认 `qemu-test`；CI Assert 同步到 M43） |
| Windows / 远端 CI | workflow 在；本笔记不以单次 run 冒充长期绿 |
| 用户态 | **最小闭环成立**；仍非进程模型/信号/多用户 |
| 文件系统 | 内存 ramfs + 一层目录；非块设备/持久化 |
| 下一主线 | **继续 A2**：更多 POSIX → 可移植 libc 子集 → 机上工具链 |
| 最大风险 | 过早宣称 A2；或并行开 TCP/完整 VFS 稀释证据 |

## 2. 相对第三次中场：增量盘点

### 2.1 地址空间与特权（M35–M37）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M35 | 用户地址空间 ADR Accepted | [`adr-user-address-space.md`](adr-user-address-space.md) |
| M36 | BSP `create`/`map_user_page`/CR3 往返 | `user AS probe: cr3_switch: 1, …` |
| M37 | 用户 GDT + TSS.RSP0 + `iretq`/`int 0x80` | `ring3 probe: enter: 1, int80: 1, cpl: 3, rsp0: 1` |

踩坑已入库：P-40（GDT 重装清零 `IA32_GS_BASE`）、P-41（`int 0x80` 返回污染非易失 GPR）。

### 2.2 ELF 与 syscall v0（M38）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M38 | 嵌入静态 ET_EXEC；`write`/`exit`（Linux 号） | `USER_PROGRAM_PASS`；`user ELF: loaded: 1, …` |

### 2.3 可写 FS（M39、M42）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M39 | ramfs `open`/`read`/`write`/`close` | `USER_FS_PASS`；`user FS: open: 2, …` |
| M42 | `mkdir` + 一层嵌套路径 | `USER_DIR_PASS`；`user dir: mkdir: 1, …` |

### 2.4 用户堆与匿名映射（M40–M41、M43）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M40 | `brk` 增长堆并触达 | `USER_BRK_PASS`；`user brk: grow: 1, …` |
| M41 | anonymous `mmap`（`r10` flags ABI） | `USER_MMAP_PASS`；`user mmap: calls: 1, …` |
| M43 | `munmap` + `unmap_user_page` 释放帧 | `USER_MUNMAP_PASS`；`user munmap: mmap: 1, munmap: 1, …` |

### 2.5 本中场（M44）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M44 | 第四次中场评估与节奏兑现 | 本文；版本锚点 v0.45.0；CI/合同核对 |

## 3. 证据强度（第四次中场口径）

必须同时满足才可声称「本阶段回归通过」：

1. 内核串口 `CXC_OS_SELF_TEST_PASS`；
2. 外部 peer `peer_ok=1`（M13 十帧 + M14 256 stress）；
3. 冻结网络串口：M13 Echo、demux、TX 合同、`266/266/266`、`submit: 266` 等（与第三次中场一致，**未改写**）；
4. 用户态/FS/内存合同串口：`USER_PROGRAM_PASS`、`USER_FS_PASS`、`USER_BRK_PASS`、`USER_MMAP_PASS`、`USER_DIR_PASS`、`USER_MUNMAP_PASS` 及对应 `[ok] user …` 行；
5. `tools/contract_test.py` 成功路径与 `.github/workflows/ci.yml` Assert `grep` 同步到 M43+（本窗口核对）。

相对第三次中场，证据面 **扩展** 了整条 A1 用户态脊柱合同；**未宣称** 机上编译器、musl 全量、块设备、或多进程。

## 4. 工程质量再评估

| 维度 | 第三次中场 | 第四次中场 | 说明 |
|---|---|---|---|
| 机制真实性 | 强（网络 MSI-X） | 强+ | 真实 CPL3/`int 0x80`/页表切换 |
| 正确性验收 | 强 | 强 | 网络合同冻结；用户态独立 PASS |
| 文档 | 强 | 强 | ADR + P-40/P-41 + 本评估 |
| 模块化 | 中偏强 | 中偏强 | `fs`/`syscall`/`elf`/`user` 探针分文件 |
| 可扩展性 | 中低 | 中 | 仍单会话、BSP-only 用户探针 |
| 通用 OS 完整度 | 早期（用户态为零） | **早期+** | 最小用户态闭环；距 A2 仍远 |
| A2 就绪度 | 规划 | **脊柱已通、libc 未上** | 缺 getdents/`stat`/更全 ABI |

## 5. 技术债登记（第四次中场刷新）

### 已还清或主补完成（相对第三次 P0）

| 主题 | 结论 |
|---|---|
| 用户 CR3 / 页表 | M35–M36 |
| ring-3 / TSS / syscall 入口 | M37–M38 |
| 可写最小 FS | M39 + M42 目录 |
| 用户堆 / 匿名映射 | M40–M41 + M43 munmap |
| GDT/GS 与 int80 GPR 陷阱 | P-40、P-41 已修并入库 |

### 仍开放（按优先级）

#### P0（通向 A2）

- **libc 可链接子集**：缺 `getdents`/`stat`/`fstat`/`lseek`、更完整 `errno`、TLS/`arch_prctl` 等；
- **可移植用户程序构建**：现为内核内嵌手写 ELF；需交叉工具链产出可加载映像；
- **进程生命周期**：仍是「单次探针会话」，非 `fork`/多进程/`exec`。

#### P1（架构/验证）

- 用户页叶帧在 `destroy_user_address_space` 中的完整回收（当前部分路径仍偏探针级）；
- `mmap` bump 区无完整 VMA 结构；`munmap` 中间洞不压缩；
- TD-07 全局锁；TD-01 远端 CI 长期绿 / Windows QEMU 实证。

#### P2（平台与生产化）

- 块设备 / 持久 FS；完整 VFS；
- TCP 与生产网络；
- 信号、权限、多架构。

### 产品缺口（诚实清单）

- musl/newlib 可跑与机上 clang/gcc（**A2 未宣称**）；
- 多进程 / 信号 / 动态链接；
- 持久存储与真实块驱动；
- 生产级驱动框架与 soak。

## 6. 主线裁定（本中场）

第三次中场选定的 A1 脊柱 **证明有效**：在不回退网络合同的前提下，用薄 TD 切片打通了用户态阻塞链。

| 选项 | 收益 | 风险 | 本中场决定 |
|---|---|---|---|
| A. 继续堆探针 syscall（零散号） | 表面 POSIX 变多 | 无 libc 仍难养编译器 | **否为主线**；按 libc 缺口选刀 |
| B. 交叉工具链 + 非嵌入 ELF | 逼近真实用户程序 | 构建/加载面变大 | **下一刀优先候选** |
| C. musl 子集移植 | 直指 A2 | ABI/缺 syscall 会爆 | **在 B 之后或并行薄切** |
| D. 重开网络/TCP | 网络更深 | 稀释 A2 | **否**（非主线） |
| E. 块设备完整 VFS | 持久化 | 过早 | **延后**；ramfs 够支撑前期 libc |

**推荐下一主线（单线程）**：

1. **可加载非嵌入静态 ELF**（构建系统产出 + 加载器吃外部映像）；  
2. **按 musl 链接缺口补 syscall**（`lseek`/`stat`/`getdents`/`arch_prctl` 等按需）；  
3. **用户态 smoke 以交叉 hello/libc 为准**，合同升级为可重复的 `USER_LIBC_PASS`（命名届时定）；  
4. 仅在 libc 需要时再开更完整目录语义或只读块根。

退出标准（下阶段）：交叉构建的用户程序在 QEMU 上跑通且内核自检/peer **不回退**；仍 **不** 宣称 A2，直到机上工具链合同另立。

## 7. 最终评价

第三次中场证明了：能停手网络并选定 A1。

第四次中场证明了：同一冻结网络合同下，能把 **ADR → ring-3 → ELF → FS → brk/mmap/munmap** 收成可回归的用户态最小闭环，并保持 `PASS` + `peer_ok=1`。

下一阶段成败标准：

- 默认 `qemu-test` 仍 `PASS` + `peer_ok=1`；  
- 用户态合同不回退；  
- 对外表述区分「最小用户态闭环」与「A2 机上编译器」。

**第四次中场建议：A1 脊柱阶段性打通；主靶仍为 A2；下一刀优先「外部静态 ELF + libc 缺口 syscall」，网络/完整 VFS 不抢主线。**

---

## 8. 节奏契约兑现

本中场窗口已核对：

1. 本文评估成文；  
2. CI Assert / `contract_test.py` 与 M35–M43 串口同步（随各里程碑同变更集；本窗口再核对）；  
3. 无超时刷绿；已知陷阱以根因修复入库（P-40/P-41）；  
4. 基线推进至 **M44 / v0.45.0**；下一正式中场目标约 **M54**（见 [`midfield-cadence.md`](midfield-cadence.md)）。
