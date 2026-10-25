# CXC/OS 从零开始踩坑记录

这份日志只记录实际遇到、已经定位的问题，以及对应的回归方法。它不是“理想开发流程”，也不把未执行的 Windows 构建或硬件测试写成已完成。

## 0. 从零重建环境

Linux 当前实测依赖：

```bash
sudo apt-get install \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
  cmake python3 qemu-system-x86 ovmf

cmake -S . -B build/from-zero \
  -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-uefi-mingw.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCXC_WARNINGS_AS_ERRORS=ON
cmake --build build/from-zero --parallel
python3 tools/run_qemu.py --image build/from-zero/cxc-os.img --check
```

注意：沙箱快照会保存源码，但不保证保存 apt 安装的工具。曾出现新一轮 `cmake: command not found`；普通 apt 又无权写锁，需通过 `sudo` 恢复依赖。Windows 仅提供 MSYS2 UCRT64 构建和 CI 配置，当前没有本地执行结果。

## 1. 启动盘与固件阶段

### P-01：FAT 容量固定点振荡

- **现象**：早期 FAT 大小计算在 1008/1009 间振荡。
- **根因**：把“刚好够用”的固定点计算当成稳定性要求。
- **修复**：采用保守且充分的 FAT 容量，不恢复脆弱的固定值断言。
- **回归**：每次构建重新生成 MBR + FAT32 ESP，并由 OVMF 实际加载。

### P-02：串口日志重复

- **现象**：OVMF ConOut 与内核同时写 UART，输出重复。
- **根因**：退出 Boot Services 后仍保留两个输出 sink。
- **修复**：`ExitBootServices` 成功后禁用 UEFI console，串口独占。
- **回归**：检查完整启动日志和唯一 PASS 标记。

### P-03：并行 QEMU 镜像锁

- **现象**：多个测试同时打开同一 raw 镜像失败；`readonly=on` 又出现 `Block node is read-only`。
- **根因**：IDE 设备仍需要可写块节点，简单只读不是快照。
- **修复**：QEMU drive 使用 `snapshot=on`，每次测试复制 OVMF vars。
- **回归**：64/128/256 MiB 测试可并行运行。

## 2. PMM、内存与高半部分

### P-04：PMM 合并形成自环

- **现象**：扩展 PMM 后启动在后续路径停止。
- **根因**：插入空闲节点后再次扫描整条链，合并逻辑可能把节点接到自身。
- **修复**：释放路径一次地址查找，只合并直接前驱和后继。
- **回归**：奇偶碎片、完整合并、连续 8 页及 2000 步固定种子压力。

### P-05：MinGW 的 kernel code model 不可用

- **现象**：`-mcmodel=kernel` 即使配合 `-fno-pie -fno-pic` 仍报不支持 PIC。
- **根因**：MinGW 目标后端对该 code model 的限制。
- **修复**：改用已验证的 `-mcmodel=large`，不重复无效组合。
- **回归**：检查 AMD64 重定位、严格链接和真实启动。

### P-06：高 ImageBase 不等于固件会高地址执行

- **现象**：链接为高地址的 EFI 仍由 OVMF 装入低地址。
- **根因**：固件按自身内存分配装载，并应用第一次重定位。
- **修复**：LoadedImage 获取实际基址，再复制到内核物理页，按 `.reloc` 做第二次 DIR64 重定位。
- **回归**：不同 RAM 布局、多次 Release 启动及高地址 RIP 检查。

### P-07：OVMF 改写内存中的 ImageBase

- **现象**：文件 PE 字段合法，运行时 `parse_pe` 仍拒绝。
- **根因**：OVMF 将 OptionalHeader.ImageBase 改为实际低装载地址。
- **修复**：接受“首选高基址”或“实际装载基址”两种合法形态；高目标仍由链接契约决定。
- **回归**：文件头与运行时日志对照，高半部分完整自测。

### P-08：高映像拿到过期 PMM 状态

- **现象**：如果过早复制映像，后续分配只修改低映像中的 PMM 全局状态。
- **根因**：高低两份数据在最终复制前相互独立。
- **修复**：完成所有 PMM/页表准备后再做最后一次复制，此后不再分配，直接跳高入口。
- **回归**：高地址 PMM/heap 压力与页表查询。

### P-09：低映射移除后堆立刻页 fault

- **现象**：高半部分迁移成功，堆扩展时 error=2、写未映射页。
- **根因**：堆把 PMM 返回的物理地址直接当指针；旧低身份映射掩盖了错误。error=2 是“写不存在页”，不是“写只读页”（后者通常为 3）。
- **修复**：所有堆页通过 `pmm::physical_to_virtual()` 访问，并全树搜索同类残留。
- **回归**：低 256 个 PML4 项为空后，堆 1000 步压力通过。

### P-10：先拔低映射会丢失异常诊断

- **现象**：高入口初期若仍使用低 IDT，异常无法可靠进入处理程序。
- **根因**：GDT/IDT 中的处理地址也属于需要迁移的运行时状态。
- **修复**：先装高 GDT/IDT，再激活只含高映射的 runtime CR3。
- **回归**：高地址 `int3` 返回和页 fault 的 RIP/CR2 日志。

## 3. APIC、同步与调度

### P-11：RAII 锁 guard 引入 SEH/unwind

- **现象**：严格链接出现 `__gxx_personality_seh0` 和 `_Unwind_Resume` 未解析。
- **根因**：局部非平凡析构对象让 MinGW 生成异常清理依赖，而内核使用 `-nostdlib`。
- **修复**：内核锁路径显式 `lock_irqsave/unlock_irqrestore`，不链接宿主 C++ 运行库。
- **回归**：最终 PE 无相关未解析符号、无 DLL 导入，严格链接通过。

### P-12：首次线程切换在 `iretq` 页 fault

- **现象**：worker 尚未进入，CR2 指向栈顶之外。
- **根因**：合成帧只含 RIP/CS/RFLAGS；64 位模式实际完整帧还包含 RSP/SS。
- **修复**：`InterruptFrame` 修正为 176 字节，新线程显式初始化 RSP/SS；不是多映射一页掩盖越界。
- **回归**：两个 worker 实际进入、完成并发生真实上下文切换。

### P-13：开 IF 前必须确定中断所有权

- **现象风险**：legacy PIC、未使用 LVT 或 spurious 路径不清楚时，开 IF 会进入未分配 vector。
- **修复**：mask PIC/LVT，安装 timer 与 spurious gate，普通 timer 写 EOI，spurious 不写 EOI。
- **回归**：定时器计数、0 spurious、完整运行时测试和超时保护。

## 4. ACPI、IOAPIC 与睡眠唤醒

### P-14：ACPI 表指针也有页表生命周期

- **风险**：高半部分运行后直接保留 RSDP/XSDT 低地址指针，会与旧固件映射一起失效。
- **处理**：在初始固件页表仍有效时校验 RSDP、XSDT/RSDT 和 MADT，只复制 LAPIC、IOAPIC、ISO 与处理器计数字段，不保留表指针。
- **回归**：低 256 个 PML4 项为空后仍能初始化 LAPIC/IOAPIC。
- **说明**：这是由实际地址布局约束得到的预防项，不虚构一次未发生的崩溃。

### P-15：默认 QEMU CPU 不能覆盖 x2APIC

- **现象**：默认 `qemu64` 日志为 xAPIC。
- **处理**：测试工具增加 `--cpu`，同一 EFI 分别使用 `qemu64,-x2apic` 和 `qemu64,+x2apic`。
- **回归**：两种模式都完成 calibration、timer、IOAPIC 和调度测试；CI 同时保留两条命令。

### P-16：x2APIC 不能继续访问 xAPIC MMIO 页

- **根因**：两种模式的寄存器访问机制不同。
- **修复**：共用 `read_register/write_register`，x2APIC 使用 `0x800 + offset/16` MSR，xAPIC 才使用高地址 MMIO；x2APIC ID 不再右移 24 位。
- **回归**：日志分别出现 `xAPIC ID: 0` 和 `x2APIC ID: 0`，全部自测通过。

### P-17：ISA IRQ 编号不等于 GSI

- **实际数据**：OVMF/q35 的 MADT 有 5 个 ISO，ISA IRQ0 被重定向到 GSI2，而不是 GSI0。
- **处理**：先查 bus-0/source override，再定位包含该 GSI 的 IOAPIC 和 pin。
- **回归**：PIT channel 0 单次中断经 vector 33 到达，计数严格为 1，随后 RTE 重新 masked。

### P-18：电平触发与睡眠状态都需要顺序保证

- **IOAPIC**：RTE 初始化先全部 masked；测试 ISR 内再次 mask 后 EOI，避免 PIT OUT 保持有效时重复进入。
- **调度**：定时器先唤醒到期 Sleeping 项，再处理当前 Running→Ready；睡眠函数保存 self 指针，不能读取已变化的 current_index。
- **回归**：两个 worker 仍完成全部进度，worker 0 完成 4 次 `sleep(2)`；当前观测累计睡眠 11 ticks，未提前返回。

## 5. 每轮收尾必须保留的检查

- 全新目录严格构建，不以旧对象文件代替干净构建；
- Debug 和 Release 都跑，避免未初始化状态或优化差异被隐藏；
- 64/128/256 MiB 和两种 LAPIC 模式覆盖同一最终 EFI；
- 从 `artifacts/` 再启动一次，日志、镜像和 SHA-256 必须对应同一版本；
- 检查无 DLL 导入、无 SEH/unwind 未解析依赖；
- 文档只写实际执行过的结果，Windows CI 配置不等于本地已运行。

## 6. Milestone 4 当前收尾快照

- 全新 Release 严格构建通过；xAPIC 连续 3 次启动通过；
- 128 MiB 的 xAPIC/x2APIC、64 MiB 的 x2APIC、256 MiB 的 xAPIC 均通过；
- 最终 `artifacts/cxc-os.img` 又以两种 LAPIC 模式各启动一次并通过；
- 每次均完成 1 次 IOAPIC PIT 中断、4 次睡眠唤醒和全部内存/调度回归；
- 当前正式日志为 107 ticks、107 context switches、0 spurious IRQ；
- 交付日志、验证说明和 SHA-256 已更新到 v0.5.0。

## 7. PCI 与通用外部 IRQ

### P-19：有 ISR 不等于有 IRQ 生命周期管理

- **风险**：只安装 vector handler，却不管理 GSI 路由、重复注册和注销，容易留下仍在触发的 RTE。
- **处理**：固定 34–47 向量池和静态 slot；注册保存 GSI/handler/context，注销先 mask GSI 再清除 slot；同一 GSI 重复注册明确失败，不冒充 shared IRQ。
- **回归**：UART 测试结束后 active slot=0、unhandled=0；重复注册 GSI4 必须失败。

### P-20：UART loopback 测试期间不能继续打印日志

- **风险**：MCR 进入 loopback 后，日志写 UART 也会进入测试接收路径，污染数据和 IRQ 计数。
- **处理**：测试函数内不打印；handler 只读 IIR/LSR/RBR 并记录状态；恢复 polling MCR/FIFO 后才由主路径输出结果。
- **回归**：实际收到 4 次 IRQ、4 个预期字节，无额外 unhandled IRQ。

### P-21：FIFO trigger 会改变测试时序

- **风险**：沿用 `0xC7` 的 14 字节触发阈值，4 字节测试可能只能等 character timeout。
- **处理**：loopback 测试使用 `0x07` 的一字节接收触发，结束后恢复原 FIFO 配置。
- **回归**：4 个 loopback 字节各触发一次 IRQ，IRQ calls=4。

### P-22：PCI capability 链不能盲目信任

- **风险**：错误 next pointer、自环、长循环或末尾 MSI-X 结构可造成无限遍历或 8 位 offset 回绕。
- **处理**：限制 48 步、检查最小 offset、自环和 MSI-X 12 字节边界；只在 header type 0/1 且 status 指示 capability list 时遍历。
- **回归**：q35 实际枚举 5 个 function 和 1 个 MSI capability，parse errors=0。

### P-23：发现 MSI capability 不等于收到 MSI

- **边界**：当前只解析 MSI/MSI-X capability，没有设置 message address/data，也没有设备动作触发 MSI。
- **实际数据**：当前 QEMU 配置有 1 个 MSI capability、0 个 MSI-X capability。
- **记录原则**：文档只写“能力解析通过”，不能写成“MSI/MSI-X 中断已验证”。

## 8. Milestone 5 当前收尾快照

- 全新 Release 严格构建通过；xAPIC 连续 3 次、x2APIC 均通过；
- 64 MiB x2APIC、256 MiB xAPIC 通过；最终 artifacts 镜像又以两种模式通过；
- 每次均为 PCI 5 functions、1 个 MSI capability、0 个 MSI-X capability；
- UART IRQ4→GSI4/vector34，4 次 IRQ 收到 4 个正确字节；
- 重复 GSI 注册被拒绝，注销后 active slot=0、unhandled=0；
- 调度回归保持 107 ticks、107 context switches、4 次睡眠唤醒、0 spurious IRQ；
- 交付日志、验证说明和 SHA-256 已更新到 v0.6.0。

## 9. MSI 与 DMA

### P-24：普通高端 PMM 分配不满足 EDU 的 28 位 DMA mask

- **现象**：512 MiB 启动布局中，普通 PMM 高端页面已高于 `0x10000000`；EDU 默认只能访问 28 位 DMA 地址，不能直接沿用普通分配结果。
- **根因**：Inline-Extent PMM 从空闲区间高端取页，而 EDU 的默认 DMA mask 是独立硬件约束。
- **修复**：增加 `allocate_pages_below`；上限切开 extent 时保留 high tail、低侧余量和降序链接，不能只截短原节点。
- **回归**：512 MiB x2APIC 实测 DMA 页为 `0x0FFFF000`、`0x0FFFE000`，256 字节往返和两次完成 MSI 通过；释放后空闲页数精确恢复，后续内存/调度回归继续通过。

### P-25：负对照匹配 panic 时，工具文案仍写 self-test passed

- **现象**：`--no-edu --expect "CXC_OS_PANIC: EDU MSI/DMA"` 正确匹配预期 panic 后，Python 工具仍打印 `QEMU self-test passed`，容易把负对照误读为内核 PASS。
- **根因**：`--expect` 已支持任意字符串，但成功摘要硬编码为 self-test passed。
- **修复**：摘要改为中性的 `QEMU serial expectation matched`；默认 PASS 标记与自定义 panic 期望只表示各自字符串被匹配。
- **回归**：无 EDU 时串口仍在 EDU 检查处 panic，且绝不出现 `CXC_OS_SELF_TEST_PASS`；附加 EDU 的双 APIC/内存矩阵正常通过。

### P-26：PCI 16 位配置写不能借用 dword RMW

- **现象**：最终代码审查发现，初版 `config_write16` 通过 aligned dword 的 read-modify-write 保留相邻字段；command 与 status 同处一个 dword，而 status 中可能有 RW1C 位，写回旧值并不等于无副作用。
- **根因**：把“保留相邻位的数值”误当成“保留相邻寄存器状态”，忽略 PCI 字段的写一语义。
- **修复**：CONFIG_ADDRESS 选择 aligned dword 后，向 `0xCFC + (offset & 2)` 执行真正的 `outw`，不再读取并写回整个 dword。
- **回归**：修正后的全新 Release 在双 APIC、64/128/256/512 MiB、重复启动和无 EDU 负对照中重新验证，最终 artifacts 再次双启动通过。

### P-27：首次 MMIO 访问前必须开启 PCI Memory Space

- **现象**：初版 EDU 路径在设置 command register 之前读取 identification/liveness；当前 OVMF 已开启 decode，所以测试能通过，但驱动依赖了固件遗留状态。
- **根因**：把页表映射误当成 PCI device decode 已启用，二者是不同层次。
- **修复**：BAR 页映射后先设置 Memory Space 和 INTx Disable，再做 MMIO liveness；注册 message vector 后再启用 Bus Master。
- **回归**：修正后重新完成 Debug/Release 构建和完整 QEMU 矩阵，MSI/DMA 计数、数据和清理结果不变。

### P-28：EITR=0 不等于 QEMU 中绝不再通知

- **现象**：entry 0 的 RXQ0 有效中断已到达 vector 34 并完成 cause 清除，随后同一 entry 又进入 handler，但 ICR 没有有效 cause；最初的总调用次数断言因此失败。
- **根因**：QEMU `e1000e_set_eitr` 将写入的 0 钳到最小值 500。首次通知可能启动节流定时器，定时器到期可产生没有新 cause 的通知。
- **修复**：ISR 改读与 ICR 同步但无 read-to-clear 副作用的 ICS；只按自身 expected cause 写 ICR，区分 `matched_calls`、`empty_calls` 和 foreign errors，并在每阶段后等待两个 scheduler ticks 让节流窗口 settled。
- **回归**：三个有效 cause 仍必须各 matched=1；空通知数量不作为固定断言。entry 0 直接交付、entry 1 per-vector PBA、entry 4 function PBA 均在双 APIC 和内存矩阵中通过。

### P-29：`INT_ASSERTED` 残留不是 RXQ/TXQ payload

- **现象**：两种 PBA 路径已走通后，空调用读到 `0x80000000`，被当成 foreign cause；有效 TXQ0 记录则为 `0x80400000`。
- **根因**：清除具体 cause 后，`INT_ASSERTED` (bit 31) 仍可保留；它不是需要匹配的队列 payload。
- **修复**：判断空调用和最终清空状态时先移除 bit 31；matched 仍要求自身真实 RXQ/TXQ bit，foreign payload 仍会使测试失败。
- **回归**：entry 0/1/4 分别交付到 vector 34/35/36，matched 各为 1，errors=0；空调用只进入 empty 计数，不产生 unhandled IRQ。

### P-30：碎片自测不能假设初始内存图给出一串连续页

- **现象**：`--no-e1000e` 改变 OVMF 内存布局后，PMM 自测在到达设备检查前报“未创建独立 extents”；附加 e1000e 时相同测试通过。
- **根因**：测试连续执行 8 次普通高端分配并假定它们来自同一 extent；不同固件内存图可让分配跨越无关区间，使奇偶释放后的 extent 数不满足该偶然布局下的预期。
- **修复**：先分配并释放 16 页 arena，随后用 `allocate_pages_below` 限定测试分配，并显式验证 8 页连续；继续保留 extent 增加、完全合并和账目恢复断言。
- **回归**：两种内存布局均通过碎片测试；无 e1000e 负对照现在到达 stage 0 的设备检查并 panic，完整双 APIC/64–512 MiB 矩阵也通过。

## 10. Milestone 10 当前收尾快照

- 全新 Debug/Release 严格构建通过；M8/M9 启动、每 CPU timer、抢占和睡眠回归保持；
- Release xAPIC 4 vCPU 连续 3 次、x2APIC 4 vCPU 均通过；UP/2/8/16 vCPU 也全部通过；
- PMM 与 heap 已有独立 IRQ-safe SMP 锁，锁顺序固定为 heap→PMM，ISR 仍禁止分配；
- 所有 CPU 的普通 worker 都执行 8 轮 PMM/heap 分配、交叉内容检查和释放；
- 默认 4 vCPU 为 40 次 PMM、40 次 heap；16 vCPU 各 136 次，最终页数账目闭合；
- BSP 第二个 worker 在同一调用和同一栈上完成 CPU0→CPU1→CPU0 两次真实迁移；
- 迁移采用 MigrationPending/MigrationSaved 两阶段，源 CPU 后续 tick 才发布，避免双 CPU 共用栈；
- N>1 时 vector 50 在 CPU0/CPU1 各交付一次；UP 无迁移，既有本地调度不退化；
- 运行时专用 4 KiB 页经历 RW→RO→RW 两次权限更新和 vector-51 shootdown；
- 每个 CPU 都执行两次 `invlpg` 并 ack；RO 阶段实际恢复 N+1 个写故障，RW 恢复后写入成功；
- page-fault 恢复只匹配指定 CR2/RIP/error/阶段，其他页故障仍 fatal；
- 默认 4 vCPU 保持 shared counter=160、20 次 sleep/wake；16 vCPU 为 544 和 68；
- Release 4 vCPU 下 64 MiB x2APIC、128 MiB 双模式、256 MiB xAPIC、512 MiB x2APIC 全部通过；
- EDU MSI/DMA、e1000e 三向量 MSI-X/PBA、UART/PIT 和原有 PMM/heap 压力均未退化；
- 无 e1000e、无 EDU 负对照分别按预期 panic，不产生内核 PASS；
- 最终 artifacts 镜像以双 APIC、UP/16 vCPU 复验，vector-48 parked 证据和注销清理保持；
- 交付 EFI、ESP、日志、验证说明和 SHA-256 更新到 v0.11.0；Windows 与远端 CI 仍未实际执行。

## 11. Milestone 11：真实包路径的新增踩坑与快照

### P-31：旧 MSI-X 测试的一页 CSR 映射不能覆盖 MAC 寄存器

- **现象**：首次加入 packet 初始化后，在读取 RAL0/RAH0 时发生 page fault，CR2 为 CSR 高映射加 `0x5400`；此前 MSI-X synthetic 测试可以访问其低位寄存器。
- **根因**：M7 只需 BAR0 前 4 KiB，因此只映射一页；RAL0 位于 `0x5400`，MRQC 等还在更高页。
- **修复**：按实际寄存器范围显式映射 BAR0 的 `0x8000` 字节，保持 RW/NX、cache-disabled，再执行 MAC/link/ring 初始化。
- **回归**：不再以“旧 MSI-X 能跑”推断整个 CSR BAR 已映射；真实 RX/TX 和完整内存矩阵通过。

### P-32：辅助 cause 的数值和名称必须一起核对

- **现象**：M11 peer 已收到响应时出现 `0x80000002` 辅助原因，当时按 LSC 处理；M12 连续 TX 后又出现真正的 `0x4` link cause，促使重新核对寄存器定义。
- **根因**：QEMU `e1000x_regs.h` 明确为 TXQE=`0x2`、LSC=`0x4`。M11 把 TXQE 错命名为 LSC，虽然辅助路径因此覆盖了 `0x2`，但名称和解释并不正确。
- **修复**：M12 更正两个常量，只在 packet/protocol 阶段允许 TXQE/LSC；与 expected cause 同来时只额外清辅助位，仅剩辅助位时也只清这些位。其他未知位仍失败，synthetic 阶段不变。
- **回归**：M11 raw 和 M12 连续三次 TX 都通过；文档保留这次纠正，不把旧的 `0x2` 日志继续解释为 LSC。

### P-33：外部 peer 是验收条件，但不能破坏早期设备负对照

- **现象风险**：正常启动必须同时满足 serial PASS 和 peer 响应；若机械地对所有 `--expect` 强制 peer，`--no-edu` 会在到达网卡前按预期 panic，却被 harness 错判为网络失败。
- **修复**：非 panic 的成功用例强制 `peer_ok=1`；预期 `CXC_OS_PANIC:` 的负对照只要求对应串口证据。`--no-network-peer` 则专门验证有 NIC、无请求时必须在 RX 阶段超时。
- **回归**：缺 EDU、缺 e1000e、无 peer 三项都产生各自预期 panic，且不产生 `CXC_OS_SELF_TEST_PASS`。

### P-34：重试注入与 exactly-one descriptor 断言必须二选一

- **设计问题**：若 peer 不知道 ring 何时就绪而持续重试，可能在主路径检查前完成多个 RX descriptor，RDH 和中断次数就不再是确定的一次。
- **处理**：正式 `--check` 由内核输出 `[net] e1000e packet-ready`，peer 等待该标记后只发送一个请求；只有非 check 的手工运行才低频重试，且不用于正式 exactly-one 证据。
- **收益**：每次正式日志都可严格要求 RX/TX 各一帧、60/60 字节、RDH/TDH=1/1、RXQ0/TXQ0 各一次，而不是放宽计数来适配测试端不确定性。

### M11 当前收尾快照

- M11 使用 8 项 legacy RX/TX rings、7 个 RX buffer 和一个 TX buffer，所有 DMA 页低于 4 GiB；
- 真实请求/响应均为 60 字节，内核验证 descriptor、head、payload、guard 和真实 queue MSI-X；
- 独立 Python UDP peer 逐字节验证响应，runner 同时要求 serial PASS 与 `peer_ok=1`；
- RX/TX 停止、bus mastering 撤销后逆序释放 10 页，PMM 空闲页数精确恢复；
- 严格 Debug/Release 构建通过；Release xAPIC 三次、x2APIC 和 64/128/256/512 MiB 矩阵通过；
- UP/2/8/16 vCPU 全部保持 M10 allocator、迁移、TLB 和故障探针结果；
- 16 vCPU 仍为 PMM/heap 各 136 次、17 个真实 RO 故障；UP 仍为各 16 次、0 迁移；
- 缺 EDU、缺 e1000e、无 peer 三个负对照均按预期 panic；
- 交付 EFI、ESP、串口/peer 日志、验证说明和 SHA-256 更新到 v0.12.0；Windows 与远端 CI 仍未实际执行。

## 12. Milestone 12：协议阶段的新增踩坑与快照

### P-35：`last_cause` 会掩盖较早的跨向量 foreign cause

- **现象**：ARP/ICMP 均已被 peer 验证，但 entry 1 偶发 errors=1；最后 `last_cause` 常为 TXQ0|TXQE，无法解释此前错误。
- **诊断**：给 `VectorResult` 增加独立 `error_cause` 后，抓到实际错误为 entry 1 上的 `0x80100000`，即 RXQ0，而不是最后显示的 TX cause。
- **根因**：快速收发时，QEMU EITR 后续通知可在一个 entry 上暴露共享 ICS 中仍属于另一个 entry 的 queue cause。直接记 foreign error 会把合法的延迟归属观察当成设备错误；直接清除又会抢走 owner 的事件。
- **修复**：packet/protocol 阶段仅对 RXQ0/TXQ0 的跨向量观察增加 `cross_calls`，不增加 matched、不清该队列 cause；所属 vector 继续 acknowledge。任何未知位先失败，RXQ1 也不在 packet 允许集内。
- **回归**：同一 Debug 构建连续 5 次通过，`cross_calls` 随时序为 0 或 1；完整 Release 双 APIC/SMP/内存矩阵通过，有效协议计数不受影响。

### P-36：协议负对照必须区分“没进入”和“校验拒绝”

- **设计问题**：只做完整成功交换不能证明协议阶段不可绕过，也不能证明 checksum 真正参与验收。
- **处理**：`--no-protocol-peer` 在 raw 成功后停止，要求 protocol stage 2 等 ARP 超时；`--bad-icmp-checksum` 在 ARP 成功后翻转 ICMP checksum，要求 stage 4 拒绝且 ICMP 计数保持 0/0。
- **回归**：前者 peer 明确为 `m11-raw-only`；后者日志为 ARP=1/1、ICMP=0/0、heads=3/2，两个内核负对照均无 PASS。

### M12 当前收尾快照

- 保留 M11 raw 60 字节交换，新增固定地址 `10.0.12.2` 的 ARP 和 IPv4 ICMP Echo；
- ARP 60/60 字节、ICMP 74/74 字节，最终 RDH/TDH=3/3，协议 RX/TX MSI-X 各两次；
- 内核验证 IPv4/ICMP checksum、地址、分片字段和 descriptor/guard，响应重算两个 checksum；
- 外部 peer 完成 raw、ARP、ICMP 三个响应的逐字段验证，正式运行 `requests_sent=3`；
- TXQE/LSC 位定义已更正，未知 ICR 位严格失败，跨向量 queue cause 不被抢清；
- 严格 Debug/Release 和完整双 APIC、UP/2/8/16 vCPU、64–512 MiB 矩阵通过；
- M10 SMP allocator/迁移/TLB、EDU、UART/PIT、PMM/heap 回归保持；
- 无 raw peer、无协议 peer、坏 ICMP checksum、缺 EDU、缺 e1000e 五类负对照均按预期失败；
- 交付 EFI、ESP、串口/peer 日志、验证说明和 SHA-256 更新到 v0.13.0；Windows 与远端 CI 仍未实际执行。

## 13. Milestone 13：真实回收与 wrap 的新增踩坑

### P-37：RDT 不是“最后使用的 descriptor”，初始值也不是 6

- **现象**：第一版 M13 在 `udp-ready` 前失败，实际状态为 RDH=3、RDT=7，而代码错误要求 RDT=6。
- **核对**：QEMU `e1000e_ring_empty` 以 head==tail 表示空；初始 RDT=7 发布 descriptor 0–6，raw/ARP/ICMP 只推进 head，不修改 tail。
- **修复**：每帧将最早已消费的一页发布到旧 RDT，再把 RDT 模 8 加一。十帧后 RX head=5、RDT=1，RX tail 真实 `7→0` 两次；不通过倒退或额外 tail 写制造计数。

### P-38：七个物理 pages 不等于七项 descriptor 指针数组

- **现象**：加入 FIFO 目标槽状态检查后，第 0 帧以 failure=1 失败；此前未检查目标槽时曾表面通过。
- **根因**：`rx_buffers` 仍声明为 `rx_buffer_count=7`，但 descriptor 7 在 M13 会被赋予回收页，`rx_buffers[7]` 越界。早期成功日志因此无效。
- **修复**：CPU 指针数组改为 `descriptor_count=8`，物理 pages 仍只有七页；每帧检查源已消费、目标指针/地址/状态为空，防止重复发布或覆盖 live page。
- **额外加固**：两个 4 KiB descriptor ring 页在发布前显式清零，不依赖 allocator 碰巧返回零页。

### P-39：`ready` 后不能再要求当前 RX status 仍为零

- **现象**：增加“等待前 status==0”检查后，peer 已注入并产生 RX IRQ，内核却没有回复。
- **根因**：`udp-ready` 日志先于主循环检查，peer 可以立即完成 DMA；等待前看到 DD 是合法竞态，不是 stale descriptor。
- **修复**：移除该前提，继续由等待函数和后续 DD+EOP/长度/内容/guard 检查确认完成。`UdpResult.failure` 记录 ring 前置、RX、TX、tail 和最终不变量失败位置。

### M13 收尾口径

- UDP 十个 payload 为 1/2/3/7/8/15/16/31/32/33 字节，总计 148；每方向 frame bytes=642；
- 最终 heads=5/5、tails=1/5，wrap=1/1/2/1，FIFO 回收=10；
- 正常成功仍同时要求内核 PASS 和完整外部 peer；新增 no-UDP 与坏 UDP checksum 负对照；
- 最终矩阵、正式镜像复验和哈希只取包含上述修复的全新构建，详见 `udp-ring-wrap.md` 与 `artifacts/VALIDATION.txt`；Windows 与远端 CI 仍未实际执行。

## 37. Milestone 37：ring-3 / GDT 重装

### P-40：`lgdt` 后重装 FS/GS 会清零 `IA32_GS_BASE`

- **现象**：M37 在 `initialize_tss`→`gdt_load` 之后 `#PF`，RIP=`current_cpu_index`（`mov %gs:0x8`），CR2=`0x8`。
- **根因**：对 FS/GS 执行 `mov %ax, %gs` 会按 GDT 描述符重写隐藏 base；平坦 data 段 base=0，覆盖此前 `wrmsr IA32_GS_BASE` 写入的 per-CPU 块。
- **修复**：long-mode 重装 GDT 时只重载 DS/ES/SS/CS，保留 FS/GS（及 MSR base）；TSS 描述符更新后 `ltr` 即可。

### P-41：`int 0x80` 返回内核时会弹出用户 GPR

- **现象**：M38 已打印 `USER_PROGRAM_PASS`，随后 `#PF` error=0x3，CR2=`0x40002F`，RIP 在 `result.entered = 1`。
- **根因**：syscall 退回 `ring3_after_user` 时，`isr_common` 先按中断帧弹出 GPR；帧里仍是用户寄存器（`rsi` 指向 msg）。内核继续用被污染的 `rsi` 写 `ElfProbeResult`。
- **修复**：`ring3_enter_iretq` 在造 iretq 帧前 push Microsoft 非易失 GPR，把 `ring3_saved_kernel_rsp` 指到该保存区；`ring3_after_user` 先 pop 再 `ret`。
