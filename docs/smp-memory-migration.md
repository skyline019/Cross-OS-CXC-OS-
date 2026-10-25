# SMP 内存锁域、受控线程迁移与 TLB Shootdown

## 1. M10 范围

Milestone 10 / v0.11.0 在 M9 每 CPU 调度之上实现：

- 可被所有在线 CPU 并发调用的 PMM 和 heap 锁域；
- 同一运行中 worker 的跨 CPU 保存、发布和恢复；
- vector-50 目标唤醒；
- 运行时 4 KiB 页权限修改、vector-51 TLB shootdown 和逐 CPU ack；
- 用实际只读写故障及恢复写验证失效路径。

本轮仍不是通用负载平衡、动态线程生命周期、完整 SMP VMM 或用户地址空间。

## 2. PMM / heap 锁域

### PMM

`allocate_pages`、`allocate_pages_below`、`free_pages`、空闲页数和 extent 快照都使用同一把 IRQ-safe 原子 spinlock。临界区覆盖：

- free-extent 链接和页数；
- high-tail 保留与局部合并；
- free-page 账目；
- 分配页清零。

初始化只在 BSP、SMP 启动前执行；region 数组和 direct-map offset 初始化后只读。

### Heap

free-list 查找、分裂、插入、合并、magic 和 reserved-page 账目由另一把 IRQ-safe 锁保护。锁顺序固定为：

```text
heap lock → PMM lock
```

heap 扩容可以调用 PMM；PMM 不调用 heap，因此没有反向锁路径。`allocate_zero` 在获得独占块后再清零，不在 heap 全局锁内执行整个 payload 清零。

### 多核验证

每个 worker 连续执行 8 轮：

1. 分配一个物理页和一个 96 字节 heap 块；
2. 写入与 `(worker, round)` 相关的首位/末尾及全块 pattern；
3. 等全部 worker 分配完成；
4. 交叉检查所有物理页唯一、所有页内容和其他 worker 的 heap 内容；
5. 等全部检查完成，再释放自己的页和块；
6. 等全部释放后进入下一轮。

N 个 CPU 有 N+1 个 worker，因此 PMM 和 heap 各有 `8 × (N+1)` 次成功分配。最终空闲页数满足：

```text
free_after = free_before - (heap_reserved_after - heap_reserved_before)
```

这允许 heap 永久保留扩容页，但不允许测试分配泄漏。测试还要求每个 CPU 都至少执行一次分配路径。

## 3. 为什么迁移需要两阶段

不能在一个 CPU 的 timer ISR 中保存当前帧后立刻让目标 CPU 可见。源 CPU 此时还没有执行 `iretq`，目标若马上恢复同一帧，两个 CPU 会短暂共享一个内核栈。

M10 使用以下状态转换：

```text
Running
  → MigrationPending       worker 调用 migrate_to(target)
  → MigrationSaved         源 timer 保存完整 176 字节帧并切换到其他上下文
  → 目标槽位 Ready          源 CPU 后续 tick 才发布
  → Running                目标 timer 选择并恢复同一帧
```

因此发布发生在源 CPU 已经离开该线程栈之后。`migrate_to` 的同一调用在目标 CPU 返回，局部变量、循环进度和栈地址保持不变。

## 4. 固定槽位与队列锁

- slot 0 永远是本地 bootstrap，不迁移、不睡眠；
- worker 使用 slot 1/2；
- 源槽迁移后变为 Empty，目标使用已有 Empty 槽；
- 队列**不压缩**，所以其他 Sleeping worker 的 Thread 地址保持有效；
- Finished 对象不销毁、不复用；
- 任意时刻只允许一个 migration 在途；
- 请求时检查目标空槽和两边 started 状态；
- 所有 run queue 状态变化由一把 correctness-first 全局 IRQ-safe queue lock 序列化。

每 CPU 仍有独立 run queue、current、tick/switch/finished 和 LAPIC timer；全局队列锁不把这些状态合并成全局 ready queue。该锁是本轮正确性取舍，不声称具有可扩展性能，后续可用有序双队列锁或 per-CPU 队列优化。

## 5. Vector 50：迁移唤醒

发布到目标队列后，源 CPU 向目标发送 targeted fixed vector 50：

- handler 只记录当前 CPU 的 wake IPI 计数；
- dispatcher 写 LAPIC EOI；
- handler 不直接调度、不修改目标 current；
- 目标本地 periodic timer 从自己的队列选择 Ready 线程。

vector 48 的交付证据、vector 49 的 start/stop 控制与 vector 50 的迁移唤醒完全分开。

强制回归让 BSP 第二个 worker：

1. 在第 8 轮从 CPU0 迁到 CPU1；
2. 检查相同栈地址并继续在 CPU1 执行；
3. 在第 24 轮迁回 CPU0；
4. 最终完成原有 32 轮、四次睡眠和后续内存/TLB 测试。

N>1 时必须恰好完成 2 次迁移，CPU0/CPU1 各收到一次 vector 50；UP 跳过迁移且计数为 0。

## 6. Vector 51：真实 TLB Shootdown

`vmm::set_page_writable` 当前只允许 BSP 修改共享 runtime root 中已存在的 4 KiB 映射，拒绝 huge page。流程：

1. 获取 IRQ-safe 权限锁并修改 PTE writable bit；
2. 发布 shootdown address 和递增 sequence；
3. BSP 本地 `invlpg` 并记录本代 ack；
4. 向其他每个在线 CPU 发送 vector 51；
5. AP handler 对相同地址执行 `invlpg`，再发布 sequence ack；
6. BSP 有界等待全部 ack 后返回。

handler 不获取 VMM 锁，不分配、不打印。每一代必须等所有 CPU ack 完成后才能开始下一代，避免地址/sequence 混用。

VMM 的新建映射和线程栈准备仍由 BSP 在调度启动前完成；本轮实现的是已存在 4 KiB 映射的运行时权限更新，不是通用并发页表编辑器。

## 7. 用实际 Page Fault 验证

仅检查 PTE 和 IPI 计数不足以证明写入权限已在所有 CPU 生效，因此运行时测试增加专用 RW/NX 页和汇编写探针：

1. 每个 CPU 的 worker 先写自己的页内 slot，填充本 CPU TLB；
2. 全员 barrier 后，BSP 改为只读并执行第一次 shootdown；
3. 每个 worker 调用指定写指令，必须产生 present+write 的 ring-0 page fault；
4. handler 仅当 CR2、RIP、error code 和测试阶段全部匹配时，把 saved RIP 改为恢复标签并返回 false；
5. BSP 恢复 writable 并执行第二次 shootdown；
6. 同一探针必须成功写入，且内容正确。

其他 page fault 仍然是 fatal，不使用通用“忽略写故障”处理。强制结果：

- shootdown 恰好 2 次；
- 每个 CPU 恰好 2 次本地 invalidation；
- 实际恢复写故障为 N+1 个；
- fault CPU mask 和恢复写成功 mask 都覆盖全部 N 个 CPU；
- 最终 mapping 恢复 RW/NX。

测试页及其物理页在内核剩余生命周期内保留，避免只释放物理页却留下可写映射。

## 8. 实测结果

全新 Debug 和 Release 严格构建均通过。Release 覆盖 xAPIC/x2APIC、UP/2/4/8/16 vCPU、64/128/256/512 MiB；xAPIC 4 vCPU 连续 3/3 通过。

| CPU 数 | Workers | PMM/heap 各自分配 | 实际 RO 写故障 | 完成迁移 |
|---:|---:|---:|---:|---:|
| 1 | 2 | 16 | 2 | 0 |
| 2 | 3 | 24 | 3 | 2 |
| 4 | 5 | 40 | 5 | 2 |
| 8 | 9 | 72 | 9 | 2 |
| 16 | 17 | 136 | 17 | 2 |

EDU MSI/DMA、e1000e MSI-X/PBA、UART/PIT、原有 PMM/heap 压力和最终 vector-48 parked 复验均保持通过。无 EDU/e1000e 负对照仍在各自检查处 panic，不产生 PASS。

## 9. 剩余边界

- 没有自动迁移、work stealing、优先级或负载平衡；
- 没有在途迁移并发、CPU hotplug、动态线程销毁或槽位复用；
- 全局队列锁和全局分配器锁不是可扩展性设计；
- 没有条件变量、blocking IO 或中断线程化；
- 运行时权限更新仅限 BSP 发起的共享内核 4 KiB PTE；
- 没有按地址空间区分的 TLB shootdown、PCID、INVPCID 或用户 CR3；
- 普通 AP worker 不写串口日志；ISR 仍不分配 PMM/heap；
- Windows/MSYS2 配置保留，实测仍仅为 Linux + QEMU/OVMF。
