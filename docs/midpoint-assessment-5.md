# CXC/OS 第五次中场评估：M45–M54 进度、证据与下一主线

日期：2026-10-25  
基线：Milestone 54 / v0.55.0  
对照：第四次中场（[`midpoint-assessment-4.md`](midpoint-assessment-4.md)）冻结在 **M35–M44** 并裁定下一刀「外部静态 ELF + libc 缺口 syscall」；本评估覆盖其后 **M45–M53** 能力切片，并以 **M54** 作为本中场窗口本身。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；地址空间 ADR：[`adr-user-address-space.md`](adr-user-address-space.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)。  
本地实证口径：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；冻结网络合同（M13 Echo 10/10、M14 stress、`266/266/266`、`submit: 266` 等）**不回退**；用户态合同自 M38 起扩展至 `USER_LIBC_FS_PASS`（见 §3）。

## 1. 结论摘要

第四次中场建议「外部静态 ELF + 按 libc 缺口补 syscall + 交叉 hello/libc smoke」。本周期 **按该建议执行并落地**：

1. **ESP 外部 ELF 加载**（M45 `EXT.ELF` → M46 host-gcc `CROSS.ELF`）；
2. **musl 常见缺口 syscall**（M47 `lseek` → M48 `fstat` → M49 `stat` → M50 `getdents64` → M51 `arch_prctl` FS TLS）；
3. **可链接 mini-libc 烟测**（M52 `write`/`_exit` → M53 `open`/`read`/`close`）；
4. **本中场窗口**（M54）：评估成文、基线锚点、CI/合同与节奏表同步。

相对第四次中场，质变是：

- 从「内核内嵌手写 ELF」推进到 **构建系统产出的 ESP 用户映像**（含 host-gcc 路径与 Python fallback）；
- 从「裸 `int 0x80` 探针」推进到 **可链接的 in-tree mini_libc ABI 面**（仍 **不是** musl）；
- Linux 号 syscall 面覆盖 `lseek`/`stat`/`fstat`/`getdents64`/`arch_prctl`，满足第四次中场点名的多数 libc 缺口；
- 网络合同路径 **整段冻结保持绿**，未用降级断言换 libc 进度。

当前没有已知的、处于冻结 M13–M53 验收范围内的未决失败。这 **不** 表示 musl 可链接、机上编译器或生产 VFS 已完成。

一句话状态：

| 维度 | 第五次中场判断 |
|---|---|
| M0–M44 定义内目标 | 仍完成且合同未降级 |
| M45–M53 增量 | 完成且有 QEMU 串口 + peer 证据 |
| Linux + QEMU 验证 | 强（默认 `qemu-test`；CI Assert 同步到 M53） |
| Windows / 远端 CI | workflow 在；本笔记不以单次 run 冒充长期绿 |
| 用户态 | 最小闭环 + 外部 ELF + mini-libc；仍非进程模型/信号/多用户 |
| 文件系统 | 内存 ramfs + 目录/getdents；非块设备/持久化 |
| 下一主线 | **继续 A2**：加深 mini-libc / musl CRT 缺口 → 尝试静态 musl 子集；不抢 TCP/完整 VFS |
| 最大风险 | 过早宣称 A2；或一次硬上全量 musl 导致 ABI 爆炸稀释证据 |

## 2. 相对第四次中场：增量盘点

### 2.1 外部 ELF 与交叉构建（M45–M46）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M45 | ESP `EXT.ELF`；UEFI 读入后 `launch_user_elf` | `USER_EXT_ELF_PASS`；`user ext ELF: …` |
| M46 | host gcc → ESP `CROSS.ELF`（无 libc） | `USER_CROSS_ELF_PASS`；`user cross ELF: …` |

配套：EfiLoaderData 不在 direct map → 入更高半前拷入 PMM（M45 根因修复）。

### 2.2 libc 缺口 syscall（M47–M51）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M47 | ramfs `lseek` SEEK_SET/CUR/END | `USER_LSEEK_PASS`；`user lseek: calls: 3, …` |
| M48 | Linux `struct stat` / `fstat` | `USER_FSTAT_PASS`；`user fstat: …` |
| M49 | 路径 `stat`（文件+目录） | `USER_STAT_PASS`；`user stat: calls: 2, …` |
| M50 | `getdents64` + 目录只读 open | `USER_GETDENTS_PASS`；`user getdents: calls: 2, …` |
| M51 | `arch_prctl` SET/GET_FS（拒 GS，P-40） | `USER_ARCH_PRCTL_PASS`；`user arch_prctl: calls: 2, …` |

### 2.3 mini-libc 链接烟测（M52–M53）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M52 | in-tree `write`/`_exit`；ESP `LIBC.ELF` | `USER_LIBC_PASS`；`user libc: write: 1, …` |
| M53 | `open`/`read`/`write`/`close`；ESP `LIBFS.ELF` | `USER_LIBC_FS_PASS`；`user libc fs: open: 2, …` |

诚实边界：mini_libc **不是** musl；无 `errno`、无 malloc、无 CRT `_start` 兼容层、无动态链接。

### 2.4 本中场（M54）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M54 | 第五次中场评估与节奏兑现 | 本文；版本锚点 v0.55.0；CI/合同核对 |

## 3. 证据强度（第五次中场口径）

必须同时满足才可声称「本阶段回归通过」：

1. 内核串口 `CXC_OS_SELF_TEST_PASS`；
2. 外部 peer `peer_ok=1`（M13 十帧 + M14 256 stress）；
3. 冻结网络串口：M13 Echo、demux、TX 合同、`266/266/266`、`submit: 266` 等（与第三/四次中场一致，**未改写**）；
4. 用户态合同串口：M38–M43 既有 PASS **加上** M45–M53（`USER_EXT_ELF_PASS` … `USER_LIBC_FS_PASS`）及对应 `[ok] user …` 行；
5. `tools/contract_test.py` 成功路径与 `.github/workflows/ci.yml` Assert `grep` 同步到 M53+（本窗口再核对）。

相对第四次中场，证据面 **扩展** 了外部 ELF 与 mini-libc 合同；**未宣称** 机上编译器、musl 全量、块设备、或多进程。

## 4. 工程质量再评估

| 维度 | 第四次中场 | 第五次中场 | 说明 |
|---|---|---|---|
| 机制真实性 | 强+ | 强+ | 外部 ELF + FS base MSR TLS |
| 正确性验收 | 强 | 强 | 网络冻结；用户态/libc 独立 PASS |
| 文档 | 强 | 强 | ADR 里程碑表滚动；本评估 |
| 模块化 | 中偏强 | 中偏强 | `user/mini_libc` + ESP 多 ELF |
| 可扩展性 | 中 | 中+ | 仍单会话、BSP-only 用户探针 |
| 通用 OS 完整度 | 早期+ | **早期++** | 可链接 stub libc；距 A2 仍远 |
| A2 就绪度 | 脊柱已通、libc 未上 | **缺口 syscall 大半已补、stub libc 已通** | 缺 musl CRT/malloc/errno/`writev` 等 |

## 5. 技术债登记（第五次中场刷新）

### 已还清或主补完成（相对第四次 P0）

| 主题 | 结论 |
|---|---|
| 可移植用户程序构建（交叉/ESP） | M45–M46 |
| `lseek` / `stat` / `fstat` / `getdents` / `arch_prctl` | M47–M51 |
| 交叉 hello / libc smoke 合同 | M52–M53（`USER_LIBC_PASS` / `USER_LIBC_FS_PASS`） |

### 仍开放（按优先级）

#### P0（通向 A2）

- **musl/CRT 兼容**：`errno`、TLS 完整布局、`set_tid_address`/`writev`/`uname` 等按真实链接缺口补；
- **可增长堆 API**：mini_libc `sbrk`/`brk` 包装与极简 bump `malloc`（仍可先于真 musl）；
- **进程生命周期**：仍是「单次探针会话」，非 `fork`/多进程/`exec`。

#### P1（架构/验证）

- 用户页叶帧在 `destroy_user_address_space` 中的完整回收；
- `mmap` bump 区无完整 VMA；`munmap` 中间洞不压缩；
- 用户映像与栈窗口（`0x400000`/`0x402000`）拥挤，多 PT_LOAD / `.data` 需抬栈或改布局；
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

第四次中场推荐的「外部 ELF → libc 缺口 → libc smoke」路径 **证明有效**：网络合同未回退，A2 脊柱又前进一步。

| 选项 | 收益 | 风险 | 本中场决定 |
|---|---|---|---|
| A. 继续堆零散 syscall 探针 | 表面 POSIX 变多 | 无 CRT 仍难养 musl | **否为主线**；按真实链接缺口选刀 |
| B. 加深 mini-libc（errno/sbrk/string/`writev`） | 平滑通向 musl | 自造 ABI 漂移 | **下一刀优先薄切** |
| C. 一次硬上 musl 全量 | 直指 A2 | ABI/缺号爆炸 | **否**；先 B 再极小静态子集 |
| D. 重开网络/TCP | 网络更深 | 稀释 A2 | **否**（非主线） |
| E. 块设备完整 VFS | 持久化 | 过早 | **延后**；ramfs 仍够前期 libc |

**推荐下一主线（单线程）**：

1. **加深 mini-libc**：`errno`、`sbrk`/`brk` 包装、必要 string、`writev`（若 stdout 路径需要）；  
2. **按静态 musl/CRT 链接失败清单补 syscall**（禁止盲堆号）；  
3. **合同保持** `PASS` + `peer_ok=1`；新证据命名清晰区分 stub libc 与真 musl；  
4. 仅在 libc 硬需要时再开更完整目录语义或只读块根。

退出标准（下阶段）：至少一条「比 mini_libc 更深」的用户程序路径可回归（例如 bump malloc smoke 或极小 musl 静态子集），且内核自检/peer **不回退**；仍 **不** 宣称 A2，直到机上工具链合同另立。

## 7. 最终评价

第四次中场证明了：能把 A1 最小闭环收成可回归合同。

第五次中场证明了：同一冻结网络合同下，能把 **外部 ELF → libc 缺口 syscall → 可链接 mini_libc** 收成可回归切片，并保持 `PASS` + `peer_ok=1`。

下一阶段成败标准：

- 默认 `qemu-test` 仍 `PASS` + `peer_ok=1`；  
- 用户态/mini-libc 合同不回退；  
- 对外表述区分「stub mini_libc」与「A2 机上编译器 / 真 musl」。

**第五次中场建议：A1→A2 脊柱继续有效；主靶仍为 A2；下一刀优先「加深 mini-libc / 按缺口补 CRT」，网络/完整 VFS 不抢主线。**

---

## 8. 节奏契约兑现

本中场窗口已核对：

1. 本文评估成文；  
2. CI Assert / `contract_test.py` 与 M45–M53 串口同步（随各里程碑同变更集；本窗口再核对到 M53）；  
3. 无超时刷绿；本窗口无新增未修根因；  
4. 基线推进至 **M54 / v0.55.0**；下一正式中场目标约 **M64**（见 [`midfield-cadence.md`](midfield-cadence.md)）。
