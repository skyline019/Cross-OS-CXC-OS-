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
| M36 | BSP `create` + map 一页 + CR3 往返 + 读写探针 | 串口用户 AS 探针行；`SELF_TEST_PASS` |
| M37+ | ring-3 选择子/TSS、ELF、syscall v0 | `USER_PROGRAM_PASS` |

## 6. 参考

- [`midpoint-assessment-3.md`](midpoint-assessment-3.md) §6 主线重选  
- [`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md) 阶段 1  
- [`smp-memory-migration.md`](smp-memory-migration.md) TLB / 锁域  
- 代码：`include/cxc/vmm.hpp`、`src/vmm.cpp`、`src/gdt.cpp`、`src/idt.cpp`
