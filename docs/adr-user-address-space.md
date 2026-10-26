# ADR：用户地址空间（A1 / M35）

- **状态**：Accepted（M35 / v0.36.0）
- **日期**：2026-10-25
- **上下文**：第三次中场（[`midpoint-assessment-3.md`](midpoint-assessment-3.md)）裁定下一主线为 A1；本 ADR 冻结用户 CR3、内核映射策略与 syscall 入口栈，再允许写切换代码。
- **非目标**：本里程碑 **不** 实现 CR3 切换、ELF、syscall；仅书面契约。下一刀（M36）才做 BSP 探针。

## 1. 现状事实（必须遵守，勿「顺便重构」）

依据 `include/cxc/vmm.hpp`、`src/vmm.cpp`、`src/gdt.cpp`、`src/idt.cpp`、`src/smp.cpp`、`src/scheduler.cpp`：

| 事实 | 值 / 行为 |
|---|---|
| 内核映像基址 | `vmm::kernel_base = 0xffffffff80000000` |
| 直映基址 | `vmm::direct_map_base = 0xffff800000000000` |
| 活动页表根 | **一个共享** runtime PML4（`root_physical`）；`CpuBlock.cr3` 仅为快照 |
| 低半（PML4[0..255]） | runtime 激活后为空；例外：SIPI trampoline **恒等**映射仅在共享内核根 |
| PTE U/S | **从未置位**；全部 supervisor |
| GDT | 仅 ring-0 code `0x08` / data `0x10`；**无 TSS / IST** |
| IDT | DPL 0、`ist=0`；中断在当前 RSP 入栈 |
| 栈 | BSP high stack、AP `0xffffffff60000000+`、worker `0xffffffff70000000+`，均 `map_kernel_stack` |
| TLB | 无 PCID；权限变更靠 BSP + vector 51 shootdown |
| syscall | **不存在**（无 SYSCALL/SYSENTER/swapgs） |

PMM：`allocate_pages` / `allocate_pages_below`；DMA 上限另计。本 ADR **不**重写 PMM。

## 2. 决策

### D1 — 每个用户 CR3 共享内核高半映射

- 每个用户地址空间拥有 **独立 PML4 物理页**（及按需下层表）。
- **内核窗口**（直映 + `0xffffffff8…` 映像/栈/设备等今日已映射的高半）通过 **复制/共享高层 PML4 表项** 进入每个用户根，使 ring-0 在用户 CR3 下仍看到同一内核 VA。
- M35–M37 **不**为每个用户空间私有复制整棵内核页表树；内核 PTE 仍由共享内核根语义更新，shootdown 规则暂保持「全体 online CPU」。
- 用户根 **不得** 映射 SIPI 恒等页；SIPI 只留在共享内核根。

### D2 — 用户规范窗口与低半策略

- 用户可执行/可访问 VA 落在 **规范低半用户窗口**（canonical user）：实现探针起步可用固定区间，建议首探针：

  `user_probe_base = 0x0000000000400000`（4 MiB 对齐，避开 null 页与常见低留白）

- 用户页 PTE：**Present | User |**（按需 Writable）；**NX 默认开**，可执行页显式清 NX。
- 内核 VA（≥ `direct_map_base` 与 HH 映像）在用户根中保持 **U/S=0**。
- 普通用户根：PML4 低 256 项除用户映射路径外保持空；**禁止**把 firmware 低镜像重新恒等映射进用户根。

### D3 — Syscall / 陷入入口栈：每线程内核栈，不用 IST（A1 v0）

- 将来每个用户线程（含首个 `init`）配备 **独立 ring-0 内核栈**，复用 `map_kernel_stack` 模式（含 guard）。
- Syscall/异常从 ring-3 进入时，经 **TSS.RSP0**（后续里程碑引入 TSS）切到该线程内核栈；**M35–M37 不引入 IST**。
- IST 留给 NMI/double-fault 等后续；与今日 `ist=0` 一致演进，避免先开 TSS 三栈。
- 在尚无 TSS 的探针阶段（M36）：允许 **BSP 仅 CR3 往返 + 内核态触摸用户页**（仍 CPL0），不假装已有 syscall。

### D4 — 特权与选择子演进顺序

1. M36：BSP-only CR3 切换探针（仍可全程 CPL0 验证用户页 U/S 与映射）。
2. 随后：GDT 增加 ring-3 code/data + TSS；IDT/syscall 门 DPL 调整。
3. 再后：静态 ELF 加载到用户窗口；`syscall` v0：`exit` / `write`(串口) / `yield`。
4. 合同：`USER_PROGRAM_PASS` **独立于** `CXC_OS_SELF_TEST_PASS`；后者不得回退。

### D5 — API 形状（实现时遵守，M35 只命名）

规划中的最小表面（名称可微调，语义冻结）：

| 符号（预期） | 职责 |
|---|---|
| `vmm::create_user_address_space()` | 分配用户 PML4，安装共享内核高半 |
| `vmm::destroy_user_address_space(root)` | 释放用户私有表页；不拆内核共享表 |
| `vmm::map_user_page(root, va, pa, flags)` | 仅用户窗口；置 U/S |
| `vmm::activate_address_space(root)` | `mov cr3`；BSP-first |
| `vmm::kernel_address_space()` | 返回共享内核根物理地址 |

并发：M36–M38 **仅 BSP** 创建/销毁/激活用户空间；AP 上用户 CR3 另开里程碑。

## 3. 后果

**正面**

- 与现有单内核根、空低半、无 PCID 模型兼容，增量小。
- 内核 VA 在用户运行时仍可用，简化早期 syscall/`write`→串口。
- 每线程内核栈路径清晰，可接现有 worker 栈分配器经验。

**代价 / 约束**

- 共享内核页表意味着内核 PTE 变更仍全局可见；无 per-AS 内核隔离（A1 可接受，非 B）。
- 无 PCID 时 CR3 切换冲刷 TLB，探针可测、生产后需再议。
- 在 TSS 就绪前，不能诚实宣称 ring-3 syscall；M36 证据必须写明 privilege 假设。

## 4. 明确拒绝（M35 窗口）

- 每 CPU 私有内核根 / 完整内核页表写时复制  
- 用户 CR3 内保留 SIPI 恒等映射  
- 以 IST 作为首版 syscall 栈方案  
- 同里程碑重写 PMM、开 TCP/VFS、或并行上 AP 用户进程  
- 用加大超时或删自检合同换绿  

## 5. 后续里程碑挂钩

| 里程碑 | 内容 | 退出证据（预期） |
|---|---|---|
| **M35（本 ADR）** | 本文 Accepted | 文档入树；版本锚点；自检不回退 |
| **M36** | BSP `create` + map 一页 + CR3 往返 + 读写探针 | 串口 `user AS probe: cr3_switch: 1, …`；`SELF_TEST_PASS` |
| **M37** | ring-3 选择子 + TSS.RSP0 + `int 0x80` 往返 | 串口 `ring3 probe: enter: 1, int80: 1, cpl: 3, rsp0: 1` |
| **M38** | 静态 ELF + syscall v0（`write`/`exit`） | 串口 `USER_PROGRAM_PASS` + `user ELF: loaded: 1, …` |
| **M39** | 内存 ramfs + `open`/`read`/`write`/`close` | 串口 `USER_FS_PASS` + `user FS: open: 2, …` |
| **M40** | `brk` 用户堆扩展 | 串口 `USER_BRK_PASS` + `user brk: grow: 1, …` |
| **M41** | anonymous `mmap` | 串口 `USER_MMAP_PASS` + `user mmap: calls: 1, …` |
| **M42** | `mkdir` + 一层嵌套路径 | 串口 `USER_DIR_PASS` + `user dir: mkdir: 1, …` |
| **M43** | `munmap` 释放 anonymous 映射 | 串口 `USER_MUNMAP_PASS` + `user munmap: …` |
| **M44** | 第四次中场（M35–M44） | [`midpoint-assessment-4.md`](midpoint-assessment-4.md)；CI/合同统一；v0.45.0 |
| **M45** | ESP 外部静态 ELF（非嵌入 PE） | 串口 `USER_EXT_ELF_PASS` + `user ext ELF: …` |
| **M46** | host gcc 产出 `CROSS.ELF`（无 libc） | 串口 `USER_CROSS_ELF_PASS` + `user cross ELF: …` |
| **M47** | `lseek`（SEEK_SET/CUR/END） | 串口 `USER_LSEEK_PASS` + `user lseek: …` |
| **M48** | `fstat`（Linux x86-64 `struct stat`） | 串口 `USER_FSTAT_PASS` + `user fstat: …` |
| **M49** | 路径 `stat`（文件+目录） | 串口 `USER_STAT_PASS` + `user stat: …` |
| **M50** | `getdents64` + 目录只读 open | 串口 `USER_GETDENTS_PASS` + `user getdents: …` |
| **M51** | `arch_prctl` SET/GET_FS（拒 GS） | 串口 `USER_ARCH_PRCTL_PASS` + `user arch_prctl: …` |
| **M52** | mini-libc `write`/`_exit` 链接烟测 | 串口 `USER_LIBC_PASS` + `user libc: …` |
| **M53** | mini-libc `open`/`read`/`write`/`close` | 串口 `USER_LIBC_FS_PASS` + `user libc fs: …` |
| **M54** | 第五次中场（M45–M54） | [`midpoint-assessment-5.md`](midpoint-assessment-5.md)；CI/合同统一；v0.55.0 |
| **M55** | mini-libc `brk`/`sbrk` | 串口 `USER_LIBC_SBRK_PASS` + `user libc sbrk: …` |
| **M56** | mini-libc sbrk-backed `malloc` | 串口 `USER_LIBC_MALLOC_PASS` + `user libc malloc: …` |
| **M57** | mini-libc `errno` + BSS 布局 | 串口 `USER_LIBC_ERRNO_PASS` + `user libc errno: …` |
| **M58** | mini-libc string | 串口 `USER_LIBC_STRING_PASS` + `user libc string: …` |
| **M59** | mini-libc `writev`（syscall 20） | 串口 `USER_LIBC_WRITEV_PASS` + `user libc writev: …` |
| **M60** | mini-libc CRT `_start`→`main` | 串口 `USER_LIBC_CRT_PASS` + `user libc crt: …` |
| **M61** | mini-libc `uname`（syscall 63） | 串口 `USER_LIBC_UNAME_PASS` + `user libc uname: …` |
| **M62** | mini-libc `set_tid_address`（218） | 串口 `USER_LIBC_TID_PASS` + `user libc tid: …` |
| **M63** | mini-libc `getpid`/`getppid` | 串口 `USER_LIBC_PID_PASS` + `user libc pid: …` |
| **M64** | 第六次中场（M55–M64） | [`midpoint-assessment-6.md`](midpoint-assessment-6.md)；v0.65.0 |
| **M65** | musl 子集 `write`（标注等价） | 串口 `USER_MUSL_PASS`；ESP `LIBMSL.ELF`；[`musl-subset-link-notes.md`](musl-subset-link-notes.md) |
| **M66** | SYSCALL/LSTAR（GDT STAR 序） | musl_subset 改 `syscall`；`int 0x80` 合同不回退 |
| **M67** | 用户栈 argc/argv/auxv + `exit_group` | 串口 `USER_MUSL_AUXV_PASS`；ESP `LIBMAV.ELF` |
| **M68** | musl-gcc 静态 CRT + PHDR-in-LOAD | 串口 `USER_MUSL_CRT_PASS`；ESP `LIBMCR.ELF` |
| **M69** | musl-gcc `malloc`/`free` | 串口 `USER_MUSL_MAL_PASS`；ESP `LIBMMA.ELF` |
| **M70** | musl-gcc `printf` + brk 抬升 | 串口 `USER_MUSL_PRT_PASS`；ESP `LIBMPR.ELF` |
| **M71** | musl-gcc `fopen` + layout/stub | 串口 `USER_MUSL_FOP_PASS`；ESP `LIBFOP.ELF` |
| **M72** | musl-gcc `opendir`/`readdir` | 串口 `USER_MUSL_ODR_PASS`；ESP `LIBODR.ELF` |
| **M73** | musl-gcc `unlink` | 串口 `USER_MUSL_UNL_PASS`；ESP `LIBUNL.ELF` |
| **M74** | 第七次中场 + 新格局 | [`midpoint-assessment-7.md`](midpoint-assessment-7.md)；大跨步/边缘契约 |
| **M75** | virtio-blk 读探针 + OOB 边缘 | 串口 `VIRTIO_BLK_PASS`；调试点 stage/failure |
| **M76** | 持久 virtio-blk 扇区读写 + 回读边缘 | 串口 `VIRTIO_BLK_RW_PASS`；设备保持在线供 A2 根 |
| **M77** | CXCFS on virtio-blk；`disk/` 路径；remount 持久 | 串口 `CXCFS_PASS`；syscall 可见 |
| **M78** | musl-gcc fopen `disk/` + remount 校验 | 串口 `USER_MUSL_BLK_PASS`；ESP `LIBBLK.ELF` |
| **M79** | `disk/` mkdir/opendir；64×16KiB；4MiB 盘 | 串口 `USER_MUSL_DIR_PASS`；ESP `LIBDIR.ELF` |
| **M80** | `execve` + 从 `disk/run.elf` 换映像 | 串口 `DISK_EXEC_PASS`；ESP `EXSTB`/`DEXEC` |
| **M81** | `execve` argv + 32 KiB CXCFS + 更大盘上 ELF | 串口 `DISK_ARGV_PASS`；argc/argv 边缘 |
| **M82** | `execve` envp + `rename`（持久根） | 串口 `DISK_ENV_PASS`/`DISK_RENAME_PASS` |
| **M83** | 协作式 `fork`/`wait4` + AS clone | 串口 `FORK_WAIT_PASS`；ECHILD 边缘 |
| **M84** | 第八次中场（M75–M84） | [`midpoint-assessment-8.md`](midpoint-assessment-8.md)；下一刀 pipe/dup |
| **M85** | `pipe`/`dup`/`dup2`；fork 克隆 fd 表 | 串口 `PIPE_PASS`；EPIPE + dup 边缘 |
| **M86** | `chdir`/`getcwd`；相对路径 + `.`/`..` | 串口 `CHDIR_PASS`；ENOENT/ERANGE |
| **M87** | 多级 `mkdir`（ramfs+CXCFS）；孤儿边缘 | 串口 `NEST_PASS`；persist |
| **M88** | CXCFS 128 KiB/文件；16 MiB virtio；`max_io=4096` | 串口 `CAP_PASS`；96 KiB 回读 |
| **M89** | 静态 ELF 加载器 128 KiB；disk `execve` >64 KiB | 串口 `LOAD128_PASS`；elf_bytes 102400 |
| **M90** | 用户栈 `0x800000`；`brk`/`mmap` 各 1 MiB | 串口 `HEAP_PASS`；768+512 KiB 探针 |
| **M91** | `access`/`clock_gettime`/`gettimeofday` | 串口 `TIME_PASS`；ENOENT/EINVAL |
| **M92** | 机上 `cxc-cc`：in.c→out.elf→execve | 串口 `ONBOX_CC_PASS`；A2 闭环起步 |
| **M93** | `cxc-cc` 解析 `return N`→exit(N) | 串口 `ONBOX_CGEN_PASS`；`exit: 42` |
| **M94** | 第九次中场（M85–M94） | [`midpoint-assessment-9.md`](midpoint-assessment-9.md)；下一刀表达式/汇编 |
| **M95** | `write LIT` + `return N` | 串口 `WMSG:` + `ONBOX_WRITE_PASS`；`exit: 42` |
| **M96** | freestanding mini-cc；if/while/expr | 曾用 `ONBOX_CTRL_PASS` |
| **M97** | break/&&/!/数组 + 边缘 reject | 曾用 `ONBOX_LANG_PASS` |
| **M98** | input/print（`disk/io.in`）+ 边缘 | 曾用 `ONBOX_IORW_PASS` |
| **M99** | fn 定义/调用/递归 + 边缘 | 曾用 `ONBOX_FUNC_PASS` |
| **M100** | struct/指针子集 + 边缘 | 曾用 `ONBOX_DATA_PASS` |
| **M101** | `#include` / `for` / `ptr[i]` + 边缘 | 曾用 `ONBOX_INCL_PASS` |
| **M102** | CRT `main`/globals/字符串/`do`/`+=` + 边缘 | 曾用 `ONBOX_CRT_PASS` |
| **M103** | `switch`/`?:`/`%`/`'c'` + 边缘 | 曾用 `ONBOX_CASE_PASS` |
| **M104** | `enum`/`sizeof`/`<<`/`0x` + 边缘；第十次中场 | 曾用 `ONBOX_ENUM_PASS` |
| **M105** | `#link` / `extern fn` 多 TU + 边缘 | 曾用 `ONBOX_LINK_PASS`；[`a2-mini-cc.md`](a2-mini-cc.md) |
| **M106** | `#lib` / `import fn` planted ET_REL SysV + 边缘 | 曾用 `ONBOX_LIB_PASS`；[`a2-mini-cc.md`](a2-mini-cc.md) |
| **M107** | 多符号 `#lib`（add/mul/mac）+ SysV 至 4 参 + 边缘 | 曾用 `ONBOX_MATH_PASS`；[`a2-mini-cc.md`](a2-mini-cc.md) |
| **M108** | `#lib` 整节 `.text` + `R_X86_64_PC32`（`twice`→`add`）+ 边缘 | 曾用 `ONBOX_PC32_PASS`；[`a2-mini-cc.md`](a2-mini-cc.md) |
| **M109** | 跨 `#lib` UND + planted CRT + `DMSG` 边缘 | `ONBOX_XLIB_PASS`；[`a2-mini-cc.md`](a2-mini-cc.md) |
| **M110+** | 更多 planted musl/CRT；声明靠拢真实 C → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

## 6. 参考

- [`midpoint-assessment-4.md`](midpoint-assessment-4.md) §6 下一主线  
- [`midpoint-assessment-3.md`](midpoint-assessment-3.md) §6 主线重选  
- [`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md) 阶段 1  
- [`smp-memory-migration.md`](smp-memory-migration.md) TLB / 锁域  
- 代码：`include/cxc/vmm.hpp`、`src/vmm.cpp`、`src/gdt.cpp`、`src/idt.cpp`
