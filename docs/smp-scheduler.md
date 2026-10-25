# 每 CPU 调度、SMP Spinlock 与本地 Sleep/Wake

> 本文保留 M9 的设计与实测基线。M10 已在此基础上实现 PMM/heap SMP 锁、受控迁移和真实 TLB shootdown；当前能力以 `smp-memory-migration.md` 和 `interrupts-scheduler.md` 为准。

## 1. 本轮范围

Milestone 9 在 M8 的真实 AP 启动之上，让 **每个在线 CPU 都执行自己的调度器和 worker**：

- 每 CPU 固定 run queue：bootstrap + 最多两个 worker；
- 每 CPU `current_index`、tick、switch/finished 计数和 IRQ-safe lock；
- 每个 LAPIC 使用自己的 periodic timer 驱动 vector 32；
- 线程固定 CPU 归属，不在运行中迁移；
- worker 栈全部由 BSP 在启动前分配并映射；
- 跨核共享计数由同一把 SMP 原子 spinlock 保护；
- 调度结束后 AP 停止本地 timer 并返回 M8 停车循环。

本轮不是动态负载平衡、通用 SMP 内存管理或完整线程生命周期框架。

## 2. 每 CPU Run Queue

`PerCpuScheduler` 包含：

- 三个 `Thread` slots；
- `thread_total`、`current_index`；
- 本地 tick/switch/finished；
- 本地 started 状态；
- 本地 IRQ-safe spinlock。

M9 测试在 BSP 创建两个 worker，在每个 AP 创建一个 worker。所有 `create_thread_on_cpu` 都由 BSP 在 IF=0、调度尚未启动时执行，因此 PMM/VMM 仍不需要多核锁。

虚拟栈槽位由 `(cpu_index, worker_index)` 唯一确定，仍为 32 KiB RW/NX + guard。启动后不再进行页表修改，因此本轮不需要 TLB shootdown。

## 3. 本地 Timer 与帧切换

原有 176 字节中断帧、合成线程帧和 `iretq` 返回机制不变。区别在于 vector 32 现在通过 GS 找到当前 CPU 的 run queue：

1. 只递增当前 CPU tick；
2. 只唤醒当前 CPU 的 Sleeping threads；
3. 只保存/选择当前 CPU 的 frame；
4. 使用当前 CPU 的 lock 防止本地 worker 与本地 timer 冲突；
5. 在当前 LAPIC 写 EOI。

AP 复用 BSP 的 calibration count，不再次访问共享 PIT。每个 worker 必须观察到本地 tick 前进，并完成四次 `sleep(2)`，所以测试依赖真实 AP LAPIC timer，而不是 BSP 代替 AP 调度。

早期 IOAPIC/UART/EDU/e1000e 等待仍使用独立 early-BSP tick 路径，直到 runtime scheduler 初始化，不受 per-CPU 改造影响。

## 4. Vector 49 控制与停车恢复

M8 的 vector 48 继续只用于启动和最终 IPI 交付证据。M9 新增 **vector 49** 作为调度控制 IPI：

- BSP 设置 control-start 后唤醒 AP park loop；
- AP 在普通上下文进入 `application_processor_main`，启动自己的 LAPIC timer；
- IPI handler 本身不运行调度器、不切换 frame；
- BSP 清除 control-start 并再次发送 vector 49；
- AP 停止 timer、清除 started、运行计数归零并返回停车循环。

每个 AP 必须观察到恰好两次控制 IPI（start/stop）。AP 返回停车后，M8 的第三次 vector-48 复验继续执行，保持原有证据不被控制 IPI 混淆。

## 5. 跨核执行与共享锁验证

### Live-worker barrier

测试共有 `N+1` 个 worker（BSP 两个，其余 N-1 个 CPU 各一个）。每个 worker：

1. 检查 `smp::current_cpu_index()` 等于自己的 owner；
2. 记录 owner 和 CPU bit；
3. 递增 arrived 计数；
4. 等待所有 worker 到达后才开始正式迭代。

没有 worker 能在其他 CPU 尚未有 live worker 时提前完成。UP 时本地 timer 会先抢占 worker 0，让 worker 1 也能到达同一 barrier。

最终要求 CPU mask 包含全部 N 个 CPU，而不是只检查全局 finished 总数。

### 共享计数与互斥

每个 worker 执行 32 轮：

- 获取同一把原子 spinlock，并保存/恢复本地 IF；
- `critical_active` 必须从 0→1，否则记录 overlap fault；
- 对普通 `shared_counter` 加一；
- 短延迟后恢复 `critical_active` 并解锁；
- 等待自己的本地 tick，再执行本地 sleep/wake。

强制结果：

- `shared_counter = 32 × (N+1)`；
- `critical_active=0` 且没有 critical-section overlap；
- 每个 worker 的 progress 都恰好为 32；
- sleep/wake 次数为 `4 × (N+1)`；
- BSP finished=2，每个 AP finished=1；
- 每个 CPU ticks≥8、switches≥8；
- 每个 AP 的 start/stop 控制 IPI 数恰好为 2。

这把 spinlock 是 test-and-test-and-set 原子锁，不宣称公平、无饥饿或可扩展性能；本轮验证的是正确互斥和 IF 语义。

## 6. 实测

同一 Release EFI 已通过 xAPIC 4 vCPU 连续 3 次、x2APIC，UP/2/4/8/16 vCPU，以及 4 vCPU 下 64/128/256/512 MiB。

典型结果：

| CPU 数 | Workers | Shared counter | Sleep/wake | Live-worker CPUs |
|---:|---:|---:|---:|---:|
| 1 | 2 | 64 | 8 | 1 |
| 4 | 5 | 160 | 20 | 4 |
| 8 | 9 | 288 | 36 | 8 |
| 16 | 17 | 544 | 68 | 16 |

4 vCPU 最终 vector-48 总数仍为 9，16 vCPU 为 45；EDU、MSI-X、UART/PIT 和内存回归同时通过。

tick/switch 数量只用于验证本地活动，不是性能指标，也不声称多核加速比。

## 7. 锁域与剩余边界

- AP 不调用 PMM/VMM/heap，不在 worker 或 ISR 中分配；
- AP 不写普通串口日志；
- 线程创建和 stack mapping 仅在 BSP、启动前执行；
- 每 CPU run queue 无跨核读写，远程只读原子统计；
- 没有运行中线程迁移、跨核 steal、优先级或负载平衡；
- M15–M19：Finished→Empty、Event/Mutex、BSP 可 unmap+free Empty 槽栈页；仍无 condvar、CPU hotplug 或通用 blocking IO；
- PMM/heap 仍不是通用 SMP-safe allocator；
- 运行中不改变页表，因此尚未实现 TLB shootdown；
- 尚无用户态、每 CPU TSS、syscall 或 SMP 用户地址空间。
