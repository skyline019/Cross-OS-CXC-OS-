# APIC、定时器与每 CPU 抢占调度

## 1. 当前范围

当前实现包括所有在线 CPU 的 xAPIC/x2APIC 本地定时器、完整 64 位中断帧、每 CPU run queue、IRQ-safe spinlock、ring-0 抢占轮转、本地 timer sleep/wake，以及 M10 的受控跨 CPU 迁移和唤醒。

M8 启动见 `smp-startup.md`，M9 基础调度见 `smp-scheduler.md`，M10 内存/迁移/TLB 细节见 `smp-memory-migration.md`。

不包括自动负载平衡、用户态、优先级调度或完整动态线程生命周期。

## 2. LAPIC 与向量

- xAPIC 使用高地址 cache-disabled RW/NX MMIO；x2APIC 使用 MSR；
- AP 复用 BSP calibration，但各自运行 LAPIC periodic timer；
- legacy PIC 和未使用 LVT 项屏蔽；
- timer vector 32，IOAPIC 测试 vector 33；
- vector 34–47 注册 IOAPIC GSI 或 MSI/MSI-X message source；
- vector 48：SMP 交付证据；49：scheduler start/stop；
- vector 50：迁移后的目标唤醒；51：TLB shootdown；
- spurious vector 255 不写 EOI，其余上述 LAPIC 中断按路径写 EOI；
- 设备自己的 acknowledge 由 handler 完成，例如 EDU ISR 写 `0x64`，e1000e ISR 按自身 cause 写 ICR；M12/M13 packet/protocol/UDP 阶段仅额外允许明确的 TXQE/LSC 和队列跨向量观察，规则见 `arp-ipv4-icmp.md`、`udp-ring-wrap.md`。

## 3. PIT 校准

1. BSP 的 PIT channel 2 以 mode 0 运行一次 10 ms；
2. APIC timer divider 16，从 `0xFFFFFFFF` 开始计数；
3. PIT 到期后读取 current count，差值作为 10 ms 计数；
4. 其十分之一作为约 1 ms 周期；
5. AP 使用这个已验证的周期值，不并发访问 PIT。

该校准不声称是高精度实时时钟或跨硬件恒定频率。

## 4. 完整 64 位中断帧

`InterruptFrame` 为 176 字节：15 个通用寄存器、vector/error、RIP/CS/RFLAGS 和 SS:RSP。64 位入口与 `iretq` 即使 CPL 不变也处理 SS:RSP，新线程合成帧同样必须包含它们。

汇编公共入口调用：

```cpp
cpu::InterruptFrame* interrupt_dispatch(cpu::InterruptFrame* frame);
```

返回原帧表示继续当前上下文；timer 可返回另一线程的帧。普通调度只选择当前 CPU 的 run queue；M10 迁移则显式把一个已保存且已离开源 CPU 的帧发布到目标队列。

## 5. 每 CPU 线程与固定槽位

每个 run queue 固定为 bootstrap 加最多两个 worker：

- slot 0 为本地 bootstrap，不睡眠、不迁移；
- slot 1/2 为 worker；
- 源槽迁出后变为 Empty，目标空槽接管；
- 不做队列压缩，避免改变其他 Sleeping Thread 的地址；
- Finished 槽不销毁、不复用。

每个 worker 有由创建时 `(cpu, worker)` 确定的 32 KiB higher-half RW/NX 栈、guard、完整合成帧、CS `0x08`、SS `0x10`、RFLAGS `0x202` 和符合 Microsoft x64 内核 ABI 的初始对齐。迁移后栈地址保持不变。

所有创建和映射由 BSP 在启动前完成。

## 6. 本地 Timer 与 Sleep/Wake

每个本地 timer tick：

1. 递增当前 CPU tick；
2. 发布此前已安全保存的迁移帧；
3. 将当前 CPU 到期 Sleeping 线程改为 Ready；
4. 保存当前帧；Running 改为 Ready，MigrationPending 改为 MigrationSaved；
5. 在固定槽位中循环寻找下一个 Ready；
6. 选中则改为 Running 并返回其帧，否则继续当前上下文。

`worker sleep(n)` 只设置当前队列中的 wake tick/state。slots 不压缩，因此 sleeper 的 Thread 指针在另一 worker 迁移时仍有效。到期由本地 timer 唤醒，实际再次运行时返回。

没有条件变量、阻塞 IO、优先级或动态时间片。

## 7. 两阶段迁移

`migrate_to(target)` 只由非 bootstrap 的运行中 worker 显式调用：

```text
Running → MigrationPending → MigrationSaved → 目标 Ready → Running
```

- 源 timer 保存帧并切换到其他本地上下文；
- 不能在同一个 ISR 立即发布，否则源 CPU 尚未 `iretq`，两个 CPU 可能共享一个栈；
- 后续源 tick 才把 Thread 移到目标空槽；
- 目标 vector 50 只唤醒/计数，本地 timer 负责选择；
- 同一 `migrate_to` 调用在目标 CPU 返回。

任意时刻仅一个在途迁移，请求时检查目标空槽。全局 IRQ-safe queue lock 序列化各 run queue 的修改，是 correctness-first 设计，不是可扩展锁方案。

## 8. IRQ-safe / SMP Spinlock

```cpp
uint64_t flags = lock.lock_irqsave();
// critical section
lock.unlock_irqrestore(flags);
```

获取前保存当前 CPU 的 IF 并 `cli`，原子 test-and-test-and-set 提供跨核互斥，释放后仅在原 IF 为 1 时恢复。

M9/M10 用所有 CPU 的 worker 竞争同一把测试锁，检查 critical-section overlap 和普通共享计数。PMM/heap 另有独立锁域，顺序为 heap→PMM。锁不保证公平性、无饥饿或可扩展性能。

## 9. 强制运行时验证

- 所有 CPU 都有 live worker 到达跨核 barrier；
- 每个 worker 完成 32 轮和 4 次 `sleep(2)`；
- 检查共享计数、critical overlap、进度、完成数和本地 tick/switch；
- N>1 时同一 BSP worker 完成 CPU0→CPU1→CPU0 两次迁移，栈地址不变；
- 每个 CPU 都执行 PMM/heap 分配，8 轮交叉检查页唯一性、内容和释放账目；
- 每个 AP 收到 start/stop 两次 vector 49；vector 50/51 不污染既有计数；
- 每 CPU 执行两次真实 `invlpg`，只读探针产生实际 page fault，恢复写成功；
- 停止后 AP 返回停车，再完成 vector 48 复验；
- UP 保留两个 BSP worker 的抢占、睡眠、内存和本地 TLB 路径。

tick/switch 数量随构建和拓扑变化，不作为固定性能数值。

## 10. 锁域边界

- timer/IPI/设备 ISR 不分配 PMM 或 heap；
- 调度锁内不打印、不分配、不调用复杂子系统；
- 普通 AP worker 不写串口日志；
- PMM/heap 可在 worker 普通上下文并发调用，不能在 ISR 中调用；
- 新建 VMM 映射仍由 BSP 在启动前执行；
- 运行时权限更新由 BSP 串行发起，AP vector-51 handler 只做 `invlpg` 和 ack；
- 没有自动迁移、hotplug、动态线程销毁或用户地址空间。

## 11. 后续

- 有序双队列锁、迁移并发和自动负载平衡；
- per-CPU allocator 缓存与批量释放；
- 条件等待、阻塞 IO、动态线程销毁和 CPU hotplug；
- 用户地址空间、系统调用和 ELF。
