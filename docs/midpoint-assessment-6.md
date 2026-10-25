# CXC/OS 第六次中场评估：M55–M64 进度、证据与下一主线

日期：2026-10-25  
基线：Milestone 64 / v0.65.0  
对照：第五次中场（[`midpoint-assessment-5.md`](midpoint-assessment-5.md)）冻结在 **M45–M54** 并裁定下一刀「加深 mini-libc / 按缺口补 CRT」；本评估覆盖其后 **M55–M63** 能力切片，并以 **M64** 作为本中场窗口本身。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；地址空间 ADR：[`adr-user-address-space.md`](adr-user-address-space.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)。  
本地实证口径：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；冻结网络合同（M13 Echo 10/10、M14 stress、`266/266/266`、`submit: 266` 等）**不回退**；用户态合同自 M38 起扩展至 `USER_LIBC_PID_PASS`（见 §3）。

## 1. 结论摘要

第五次中场建议「加深 mini-libc（errno/sbrk/string/writev）→ 按链接缺口补 syscall → 再谈极小 musl」。本周期 **按该建议执行并落地**：

1. **堆与分配**（M55 `brk`/`sbrk` → M56 sbrk-backed `malloc`）；
2. **错误与字符串**（M57 `errno` + BSS/栈布局 → M58 string）；
3. **CRT 与向量 IO**（M59 `writev` → M60 crt0→`main`）；
4. **musl 早期缺口号**（M61 `uname` → M62 `set_tid_address` → M63 `getpid`/`getppid`）；
5. **本中场窗口**（M64）：评估成文、基线锚点、CI/合同与节奏表同步。

相对第五次中场，质变是：

- mini_libc 从「`write`/`open` 烟测」推进到 **可链接的 CRT 入口 + errno/string/malloc/writev/uname/tid/pid 面**（仍 **不是** musl）；
- 用户程序可写 **`main` 而非手写 `_start`**，并经 ESP `LIBCRT.ELF` 合同证明；
- ESP `/EFI/BOOT` 突破单簇目录上限（多簇目录），支撑持续堆积的用户 ELF；
- 网络合同路径 **整段冻结保持绿**，未用降级断言换 libc 进度。

当前没有已知的、处于冻结 M13–M63 验收范围内的未决失败。这 **不** 表示 musl 可链接、机上编译器或生产 VFS 已完成。

一句话状态：

| 维度 | 第六次中场判断 |
|---|---|
| M0–M54 定义内目标 | 仍完成且合同未降级 |
| M55–M63 增量 | 完成且有 QEMU 串口 + peer 证据 |
| Linux + QEMU 验证 | 强（默认 `qemu-test`；CI Assert 同步到 M63） |
| Windows / 远端 CI | workflow 在；本笔记不以单次 run 冒充长期绿 |
| 用户态 | stub CRT + 加深 mini_libc；仍非进程模型/信号/多用户 |
| 文件系统 | 内存 ramfs；非块设备/持久化 |
| 下一主线 | **继续 A2**：尝试 **极小静态 musl 子集链接**（按真实缺号补刀）；不抢 TCP/完整 VFS |
| 最大风险 | 过早宣称 A2；或一次硬上全量 musl 导致 ABI 爆炸稀释证据 |

## 2. 相对第五次中场：增量盘点

### 2.1 堆与分配（M55–M56）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M55 | mini-libc `brk`/`sbrk`；ESP `LIBBRK.ELF` | `USER_LIBC_SBRK_PASS`；`user libc sbrk: …` |
| M56 | sbrk-backed bump `malloc`/`free`；ESP `LIBMAL.ELF` | `USER_LIBC_MALLOC_PASS`；`user libc malloc: …` |

### 2.2 错误、布局与字符串（M57–M58）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M57 | `errno`/`__errno_location`；`.data`/BSS @ `0x402000`；栈抬至 `0x418000`；open→ENOENT；空 `PT_LOAD` 跳过 | `USER_LIBC_ERRNO_PASS`；`user libc errno: …` |
| M58 | `strlen`/`strcmp`/`memcpy`/`memset`/`memcmp`；ESP `LIBSTR.ELF` | `USER_LIBC_STRING_PASS`；`user libc string: …` |

### 2.3 CRT 与向量写（M59–M60）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M59 | syscall 20 `writev`；多 iovec 拼 stdout PASS；ESP `LIBWRV.ELF` | `USER_LIBC_WRITEV_PASS`；`user libc writev: …` |
| M60 | thin CRT `_start`→`main(argc,argv)`→`_exit`；ESP `LIBCRT.ELF` | `USER_LIBC_CRT_PASS`；`user libc crt: …` |

### 2.4 musl 早期缺口号（M61–M63）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M61 | syscall 63 `uname`（`CXC/OS` / `x86_64`）；ESP `LIBUNA.ELF` | `USER_LIBC_UNAME_PASS`；`user libc uname: …` |
| M62 | syscall 218 `set_tid_address`（存 clear_child_tid，返回 tid=1）；ESP `LIBTID.ELF` | `USER_LIBC_TID_PASS`；`user libc tid: …` |
| M63 | `getpid`=1 / `getppid`=0；ESP `LIBPID.ELF` | `USER_LIBC_PID_PASS`；`user libc pid: …` |

诚实边界：mini_libc **仍不是** musl；无动态链接、无完整 TLS 布局、无 `fork`/`exec`、无信号。

### 2.5 本中场（M64）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M64 | 第六次中场评估与节奏兑现 | 本文；版本锚点 v0.65.0；CI/合同核对 |

## 3. 证据强度（第六次中场口径）

必须同时满足才可声称「本阶段回归通过」：

1. 内核串口 `CXC_OS_SELF_TEST_PASS`；
2. 外部 peer `peer_ok=1`（M13 十帧 + M14 256 stress）；
3. 冻结网络串口：M13 Echo、demux、TX 合同、`266/266/266`、`submit: 266` 等（与第三–五次中场一致，**未改写**）；
4. 用户态合同串口：M38–M43 既有 PASS **加上** M45–M63（至 `USER_LIBC_PID_PASS`）及对应 `[ok] user …` 行；
5. `tools/contract_test.py` 成功路径与 `.github/workflows/ci.yml` Assert `grep` 同步到 M63+（本窗口再核对）。

相对第五次中场，证据面 **扩展** 了 CRT/heap/errno/string/writev/uname/tid/pid；**未宣称** 机上编译器、musl 全量、块设备、或多进程。

## 4. 工程质量再评估

| 维度 | 第五次中场 | 第六次中场 | 说明 |
|---|---|---|---|
| 机制真实性 | 强+ | 强+ | CRT 入口 + 多簇 ESP 目录 |
| 正确性验收 | 强 | 强 | 网络冻结；libc 合同独立 PASS |
| 文档 | 强 | 强 | ADR 里程碑表滚动；本评估 |
| 模块化 | 中偏强 | 中偏强 | `mini_libc` + 十余个 ESP 用户 ELF |
| 可扩展性 | 中+ | 中+ | 仍单会话、BSP-only 用户探针 |
| 通用 OS 完整度 | 早期++ | **早期+++** | stub CRT 可用；距 A2 仍远 |
| A2 就绪度 | stub libc 已通 | **CRT+加深 libc 已通** | 下一刀应是真 musl 静态子集链接实验 |

## 5. 技术债登记（第六次中场刷新）

### 已还清或主补完成（相对第五次 P0）

| 主题 | 结论 |
|---|---|
| `errno` / BSS 布局 / 抬栈 | M57 |
| `sbrk`/`brk` + bump `malloc` | M55–M56 |
| string / `writev` / CRT `_start` | M58–M60 |
| `uname` / `set_tid_address` / `getpid` | M61–M63 |
| ESP 目录单簇溢出 | M59 起多簇 `/EFI/BOOT` |

### 仍开放（按优先级）

#### P0（通向 A2）

- **极小静态 musl 子集**：以真实链接失败清单驱动补号（禁止盲堆）；证据须标明「musl 子集」而非 stub；
- **TLS 完整布局**：仅有 `arch_prctl` FS；缺完整 TLS 块/`__thread`；
- **进程生命周期**：仍是「单次探针会话」，非 `fork`/多进程/`exec`；`clear_child_tid` 尚未在 exit 路径清零。

#### P1（架构/验证）

- 用户页叶帧在 `destroy_user_address_space` 中的完整回收；
- `mmap` bump 区无完整 VMA；`munmap` 中间洞不压缩；
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

第五次中场推荐的「加深 mini-libc → 按缺口补 CRT」路径 **证明有效**：网络合同未回退，A2 脊柱又前进一步。

| 选项 | 收益 | 风险 | 本中场决定 |
|---|---|---|---|
| A. 继续堆零散 syscall 探针 | 表面 POSIX 变多 | 无真 musl 证据 | **否为主线**；仅按链接失败清单补 |
| B. 再加深 stub mini_libc | 平滑但边际递减 | 自造 ABI 漂移 | **可薄切**；非主叙事 |
| C. **极小静态 musl 子集链接** | 直指 A2 下一证据台阶 | 缺号/CRT 爆炸 | **下一刀优先**（极小、可回滚） |
| D. 重开网络/TCP | 网络更深 | 稀释 A2 | **否**（非主线） |
| E. 块设备完整 VFS | 持久化 | 过早 | **延后**；ramfs 仍够前期 |

**推荐下一主线（单线程）**：

1. **尝试静态链接极小 musl 子集**（或等价 freestanding 真 libc 入口），以链接器报缺驱动补 syscall/CRT；  
2. 新证据命名清晰区分 **stub mini_libc** 与 **musl 子集**；  
3. **合同保持** `PASS` + `peer_ok=1`；  
4. 仅在 musl 硬需要时再开更完整目录语义或只读块根。

退出标准（下阶段）：至少一条「真 musl（或标注等价）静态子集」用户程序路径可回归，且内核自检/peer **不回退**；仍 **不** 宣称 A2，直到机上工具链合同另立。

## 7. 最终评价

第五次中场证明了：能把外部 ELF → libc 缺口 → 可链接 mini_libc 收成可回归切片。

第六次中场证明了：同一冻结网络合同下，能把 **堆/errno/string/writev/CRT/uname/tid/pid** 收成可回归薄切，并保持 `PASS` + `peer_ok=1`。

下一阶段成败标准：

- 默认 `qemu-test` 仍 `PASS` + `peer_ok=1`；  
- 用户态/mini-libc 合同不回退；  
- 对外表述区分「stub mini_libc」与「A2 机上编译器 / 真 musl」。

**第六次中场建议：A1→A2 脊柱继续有效；主靶仍为 A2；下一刀优先「极小静态 musl 子集链接实验」，网络/完整 VFS 不抢主线。**

---

## 8. 节奏契约兑现

本中场窗口已核对：

1. 本文评估成文；  
2. CI Assert / `contract_test.py` 与 M55–M63 串口同步（随各里程碑同变更集；本窗口再核对到 M63）；  
3. 无超时刷绿；本窗口无新增未修根因；  
4. 基线推进至 **M64 / v0.65.0**；下一正式中场目标约 **M74**（见 [`midfield-cadence.md`](midfield-cadence.md)）。
