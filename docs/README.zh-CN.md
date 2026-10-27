# CXC/OS

CXC/OS 是一个从 **原生 UEFI** 启动的独立 x86-64 内核工程。宿主机只负责构建和 QEMU 测试，**不依赖** Windows/Linux 内核运行。

| 项 | 当前值 |
|---|---|
| 基线 | **Milestone 284 / v2.80.0**（CCDRV：`disk/cc` argv 真驱动） |
| 证据 | QEMU + OVMF；串口 `CXC_OS_SELF_TEST_PASS` + 外部 peer `peer_ok=1` |
| 机上合同终点 | `ONBOX_CCDRV_PASS` + `WMSG:CD` + `exit: 42` |
| 路线说明 | [`a2-mini-cc.md`](a2-mini-cc.md) · 第二十八次中场 [`midpoint-assessment-28.md`](midpoint-assessment-28.md) |
| 产品目标 | [`product-goals.md`](product-goals.md)（一阶段主靶仍为 **A2**；**本窗未宣称 A2**） |

默认 `qemu-test` 成功时串口应同时出现：

```text
CXC_OS_SELF_TEST_PASS
ONBOX_CCDRV_PASS
peer_ok=1
```

以及工具链审计样例：`spawn_as: 4` `spawn_ld: 1` `spawn_cc: 3` `ccdrv: 1` `tdrv: 1` `edge: 1`。

---

## 当前功能详解

下面按层次说明**已经落地、可在默认 QEMU 合同中实证**的能力。里程碑编号只作溯源；完整分期见文末「阶段索引」。

### 1. 启动与内核底座

| 模块 | 具体能力 |
|---|---|
| UEFI | LoadedImage、ACPI、内存图/map-key 重试、低 SIPI 页预留 |
| 映像 | PE32+ higher-half、DIR64 二次重定位；MinGW-w64 严格构建 |
| 内存 | Inline-extent PMM、堆、text RX / data·栈 NX、MMIO UC、栈 guard |
| 中断 | 完整 176 字节中断帧；timer / IOAPIC / MSI / MSI-X / IPI / spurious |
| ACPI/APIC | MADT 枚举；xAPIC MMIO 与 x2APIC MSR；IOAPIC 全 RTE 接管 |
| 启动盘 | Python 生成 MBR + FAT32 ESP |

### 2. SMP、调度与同步

| 模块 | 具体能力 |
|---|---|
| AP 启动 | 真实 INIT-SIPI-SIPI；16→32→64 trampoline；GS per-CPU block |
| 调度 | 每 CPU 独立 run queue；本地 timer；显式两阶段线程迁移 |
| 同步 | 跨核 spinlock、Event（sticky wait/signal/broadcast）、worker Mutex |
| TLB | 4 KiB PTE 权限更新 + 逐 CPU `invlpg`/ack shootdown |
| 回收 | Finished→Empty；可选栈页 unmap+归还 PMM |

默认 4 vCPU 合同：worker 完成、迁移往返、PMM/heap 账目闭合、TLB 故障探针恢复写成功。

### 3. 设备与最小网络栈

| 模块 | 具体能力 |
|---|---|
| PCI | 机制 1 枚举、BAR、capability 安全遍历 |
| EDU | 单向量 MSI + RAM↔设备 DMA 往返与越界拒绝 |
| e1000e | queue 0 legacy RX/TX ring、MSI-X 多向量、FIFO 页回收 |
| 协议 | ARP / IPv4 ICMP Echo / UDP Echo（固定地址邻居） |
| Socket API | 4 槽 UDP bind/recv/recv_wait/send；IRQ 内 demux/echo；TX 背压与 Event wait |
| 外部证据 | Python UDP dgram peer 逐字段校验；`peer_ok=1`；256 帧 stress |

**不宣称**：TCP/IPv6、多队列、PHY/完整 reset、物理网卡、生产级驱动模型。

### 4. 用户态与系统调用面

| 层 | 具体能力 |
|---|---|
| 地址空间 | 每进程用户 CR3；共享内核高半；`iretq`→CPL3；`int 0x80` Linux 号 |
| ELF | 静态 ET_EXEC 加载（嵌入 / ESP / `disk/`）；argc/argv/envp/auxv |
| 内存 | `brk`/`sbrk`、`mmap`/`munmap`（匿名）；用户栈约 `0x800000` 量级 |
| 文件 | ramfs + **`disk/` CXCFS**；`open/read/write/close/lseek/stat/fstat/getdents64/mkdir/unlink/rename/chdir/getcwd/access` |
| 进程 | 协作式 `fork` + `wait4`；`execve`（换地址空间，传 argv/envp）；`pipe`/`dup`/`dup2` |
| 其它 | `arch_prctl(FS)`、`uname`、`getpid`/`getppid`、`set_tid_address`、`clock_gettime`/`gettimeofday`、`writev`；部分 `mprotect`/`ioctl`/`fcntl` stub |
| 用户库 | 自研 **mini_libc**；可加载宿主 **musl-gcc 静态 ELF**（printf/fopen/malloc/opendir 等探针） |

### 5. 持久存储（virtio-blk + CXCFS）

| 项 | 当前合同 |
|---|---|
| 块设备 | modern virtio-blk；扇区写回读；OOB 拒绝；设备保持在线 |
| 文件系统 | 块上 CXCFS；路径前缀 `disk/`；format/mount/remount 持久 |
| 容量（TCAP 后） | 验收底线 **128 MiB** virtio；`max_files≈256`；单文件可达数 MiB（胖 ELF/工具链作业） |
| 用途 | 工具链输入/输出、`.o` 种植、`out.elf` 落盘再 `execve` |

### 6. 机上工具链（本窗重点 · CCDRV）

目标路线是 A2（机上完整 gcc/g++），**当前未宣称 A2**。已落地的是可审计的迷你工具链闭环：

| 组件 | 盘上路径 | 作用 |
|---|---|---|
| 驱动器 | `disk/cc` / `disk/cxc-cc` | argv CLI：`-c` / `-o` / `@job.rsp`；默认无参 → `-o disk/out.elf @disk/job.rsp` |
| 汇编器 | `disk/as` | `.s` → ET_REL（子集 gas 形） |
| 链接器 | `disk/ld` | response 文件 → 静态 ET_EXEC（可含 R rodata 段、`.bss` NOBITS） |
| 前端方言 | `CXC_A2` | `fn` / `import fn` / 控制流 / struct·指针·`sizeof` / `#include` 等子集（**非 ISO C**） |

**进程边界（内核 `execve` 审计，禁止成功路径 in-process 冒充）：**

- `.s` → 仅 spawn `disk/as`
- 链接 ET_EXEC → 仅 spawn `disk/ld`
- 多 `.c`：HARD 作业中 `a.c`/`b.c` **嵌套** `disk/cc -c`；大 `lib.c` 可同进程编译
- 默认 HARD 审计：`spawn_cc: 3`、`spawn_as: 4`、`spawn_ld: 1`

**CLI 示例：**

```text
disk/cc -c disk/a.c -o disk/a.o
disk/cc -c disk/b.c -o disk/b.o
disk/as disk/main.s disk/main.o
disk/cc -o disk/out.elf @disk/job.rsp
disk/out.elf          # exit 42 → 串口 WMSG:CD / ONBOX_CCDRV_PASS
```

**`job.rsp` 形状（概念）：** 若干 `.s` + `a.c`/`b.c`/`lib.c` + 预种 musl/叶子 `.o` → 一次链接出 `out.elf`。

**语言面示例（HARD 探针同源）：**

```c
CXC_A2
#include "hdr.h"
fn a_sum(){
  int a[4]; int i; int s;
  s=0; a[0]=10; a[1]=10; a[2]=10; a[3]=12;
  for(i=0;i<4;i=i+1){s=s+a[i];}
  return s;   /* 42 */
}
```

```c
CXC_A2
import fn a_sum();
import fn b_val();
fn score(){
  /* struct / -> / sizeof(char**) / 调用 a_sum·b_val … */
  return n;   /* 42 */
}
```

**边缘合同：** 改写 `job.rsp` 仅含未定义符号的 `bad.c` → 非零退出 + `DMSG`，`edge: 1` 与成功路径同权。

**musl：** 合同作业链接的是宿主预先编译并种入 `disk/` 的叶子 `.o`（ctype/string/…），不是机上完整编译 musl 源码。

### 7. 验证与工程面

| 面 | 内容 |
|---|---|
| 默认验收 | `cmake --build build/uefi --target qemu-test` → `SELF_TEST_PASS` + `ONBOX_CCDRV_PASS` + `peer_ok=1` |
| 合同脚本 | `tools/contract_test.py`（主机协议 + 镜像混沌/边缘；CCDRV 种子 `0xC2C6`） |
| 主机单测 | `tests/host` + `ctest`（`cxc::net` 等） |
| CI | Linux QEMU 矩阵 + Windows MSYS2 构建（见根 [`README.md`](../README.md)） |
| 中场纪律 | 合同只增不减；修根因，禁止删断言/超时刷绿 |

---

## 阶段索引（能力如何长出来）

| 阶段 | 里程碑约 | 收束能力 | 代表 PASS / 文档 |
|---|---|---|---|
| 内核与网 | M0–M34 | UEFI/SMP/PCI/e1000e/UDP | `CXC_OS_SELF_TEST_PASS` · [中场 1–3](midpoint-assessment.md) |
| 用户态 A1 | M35–M74 | ring3/ELF/mini_libc/musl 子集 | `USER_*` / `USER_MUSL_*` · [中场 4–7](midpoint-assessment-4.md) |
| 持久根 | M75–M91 | virtio-blk、CXCFS、`execve`/`fork`/pipe | `CXCFS_PASS` / `DISK_*` · [中场 8](midpoint-assessment-8.md) |
| 机上 mini-cc | M92–M134 | 方言加深、`#lib`、as/ld 起步 | `ONBOX_*` · [中场 9–13](midpoint-assessment-9.md) |
| 整链加深 | M135–M254 | musl 叶子闭包、as 加厚、布局/ctype | `ONBOX_DEEP/MCLS/…` · [`a2-mini-cc.md`](a2-mini-cc.md) |
| TCAP/TDRV/CCDRV | M255–M284 | 容量还债 → 外部 as/ld → **`disk/cc` argv** | `ONBOX_CCDRV_PASS` · [中场 28](midpoint-assessment-28.md) |

更细的逐里程碑对照见各 `midpoint-assessment-*.md` 与 git 历史；本文不再逐条展开 M0–M284。

文档：

- [`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)：一期总评、与生产级差距、A1 编译器可跑 / 生产级内核路线图；
- [`adr-user-address-space.md`](adr-user-address-space.md)：A1 用户 CR3 / 内核映射 / syscall 入口栈 ADR（M35）；
- [`midfield-cadence.md`](midfield-cadence.md)：每 10 阶段中场 + CI/测试同步；M74 后大跨步 + 边缘契约；
- [`midpoint-assessment-7.md`](midpoint-assessment-7.md)：第七次中场（M65–M74、真 musl 收束、块设备大跨步）；
- [`midpoint-assessment-9.md`](midpoint-assessment-9.md)：第九次中场（M85–M94、pipe→机上 codegen）；
- [`midpoint-assessment-10.md`](midpoint-assessment-10.md)：第十次中场（M95–M104、mini-cc 加深→enum/sizeof）；
- [`midpoint-assessment-8.md`](midpoint-assessment-8.md)：第八次中场（M75–M84、持久根/exec/fork 收束、pipe/dup 下一刀）；
- [`midpoint-assessment-6.md`](midpoint-assessment-6.md)：第六次中场（M55–M64、mini_libc CRT）；
- [`midpoint-assessment-5.md`](midpoint-assessment-5.md)：第五次中场评估（M45–M54、外部 ELF/libc）；
- [`midpoint-assessment-4.md`](midpoint-assessment-4.md)：第四次中场评估（M35–M44、A1 闭环）；
- [`midpoint-assessment-3.md`](midpoint-assessment-3.md)：第三次中场评估（M25–M34、主线改 A1）；
- [`midpoint-assessment-2.md`](midpoint-assessment-2.md)：第二次中场评估（M14–M24）；
- [`midpoint-assessment.md`](midpoint-assessment.md)：第一次中场评估与 TD 登记（M0–M13）；
- [`from-zero-pitfalls.md`](from-zero-pitfalls.md)：从零重建与持续踩坑记录；
- [`smp-memory-migration.md`](smp-memory-migration.md)：M10 锁域、两阶段迁移、TLB shootdown 和故障探针；
- [`smp-scheduler.md`](smp-scheduler.md)：M9 每 CPU run queue、共享锁和本地 timer；
- [`smp-startup.md`](smp-startup.md)：trampoline、GS CPU block、SIPI/IPI 和停车循环；
- [`test-contract.md`](test-contract.md)：中场测试契约；§8 边缘合同（M74 起）；
- [`udp-ring-wrap.md`](udp-ring-wrap.md)：M13–M14 UDP/ring、M20–M25 socket 栈、RX demux 与 TX submit；
- [`arp-ipv4-icmp.md`](arp-ipv4-icmp.md)：M12 ARP/IPv4/ICMP、checksum、跨向量 cause 与负对照；
- [`e1000e-packet-dma.md`](e1000e-packet-dma.md)：M11 queue 0 rings、双向 DMA、peer 协议与清理；
- [`msix-e1000e.md`](msix-e1000e.md)：M7 MSI-X table/PBA、IVAR、多向量和节流处理基线；
- [`edu-msi-dma.md`](edu-msi-dma.md)：EDU BAR、MSI、DMA、清理顺序和实测方法；
- [`pci-irq.md`](pci-irq.md)：PCI、GSI/message IRQ 生命周期和 UART loopback；
- [`acpi-ioapic.md`](acpi-ioapic.md)：MADT、x2APIC、IOAPIC；
- [`interrupts-scheduler.md`](interrupts-scheduler.md)：中断帧、每 CPU 调度、迁移和同步；
- [`higher-half.md`](higher-half.md)：二次重定位与权限；
- [`algorithms.md`](algorithms.md)：PMM、受限 DMA 分配和内存例程。

## 当前实测

同一 Release EFI 已在 4 vCPU 的 xAPIC 连续 3 次、x2APIC，以及 64/128/256/512 MiB 矩阵中通过；另通过 1/2/8/16 vCPU 拓扑。QEMU 默认使用 4 vCPU，并附加 EDU 和 e1000e。

- 默认 SMP：4/4 online，4 个 CPU 都有 live worker，5 个 worker 全部完成；
- 默认共享数据：counter=160、20 次本地 sleep/wake、critical overlap=0；
- 默认迁移：同一 worker 完成 CPU0→CPU1→CPU0 两次迁移，wake IPI 各一次；
- 默认内存：8 轮共 40 次 PMM、40 次 heap 分配/交叉检查/释放，账目闭合；
- 默认 TLB：2 次 shootdown、5 个实际只读写故障、每 CPU 2 次 invalidation，恢复写全部成功；
- 16 vCPU：17 个 worker、136 次 PMM、136 次 heap、17 个实际只读故障、2 次迁移；
- 单 CPU：2 个 worker、16 次 PMM/heap、2 个只读故障、无迁移，仍有 2 次本地 invalidation；
- 调度停止后 AP 返回停车循环，vector-48 复验继续通过；
- M11 原始帧保持 RX/TX 各 1 帧、60/60 字节、RDH/TDH=1/1、真实 RXQ0/TXQ0 MSI-X 各一次；
- M12 保持 ARP request/reply=1/1（60/60 字节）和 ICMP Echo request/reply=1/1（74/74 字节），协议阶段 head=3/3、IRQ=2/2；
- M13 再完成十个 UDP Echo request/reply，payload=148、RX/TX 各 642 字节、UDP IRQ=10/10；
- M14 在同环追加 256 帧 stress（IRQ/recycle=256，wrap=32），不改写 M13 串口 10/10；
- 最终 RX/TX heads=5/5、tails=1/5，wrap RXh/TXh/RXt/TXt=1/1/2/1，七页 FIFO 回收 10 次；
- 独立 peer 实际收到并逐字段验证 13 个响应，`network-peer.log` 记录全部 hex、`udp_replies_validated=10`、`peer_ok=1`；
- IOAPIC PIT、UART IRQ4、EDU MSI/DMA、e1000e 三向量 MSI-X/PBA 全部保持通过；
- GSI/MSI/MSI-X 注销后 active slot=0、unhandled=0、spurious IRQ=0，packet 页释放前后 PMM 账目闭合；
- `--no-network-peer`、`--no-protocol-peer`、`--bad-icmp-checksum`、`--no-udp-peer` 和 `--bad-udp-checksum` 分别覆盖各阶段失败；设备缺失负对照也保持，均不输出 PASS。

这些结果证明受控迁移、分配器互斥、本代 TLB 失效正确性、queue 0 真实 descriptor DMA、最小 ARP/ICMP/UDP 数据路径和受控 ring 回收；不表示已实现自动负载平衡、可扩展锁、通用并发 VMM、多队列网卡驱动或完整网络协议栈。

## 中场测试契约

`tools/contract_test.py` 覆盖正确、失败路径、硬掉电、复合、嵌套、循环、能力矩阵、复杂场景、事务、高压和确定性混沌测试。主机侧现为 **9/9**（含 256 帧稳定性与突发满环）；默认 `qemu-test` 要求 M14 stress 与 `peer_ok=1`：

- 6688 个单比特损坏、875 个截断/扩展、60 个隔离 UDP 故障 fixture；
- 主机 ring 模型 100×100 嵌套、256 帧稳定性、4-slot 突发拒绝；
- 七类 UDP 故障可指定第 0–9 帧，验证前序提交与故障帧无部分 reply/recycle；
- 历史完整矩阵曾为 QEMU 37/37；M14 后默认成功路径为 269 帧 peer（含 256 stress）。

```bash
python3 tools/contract_test.py --image artifacts/cxc-os.img \
  --work-dir contract-results --seed 20261025
python3 tools/contract_test.py --host-only \
  --work-dir build/contract-host --seed 20261025
```

掉电仅指 QEMU 进程硬断电及后续干净启动，不宣称持久化数据恢复；主机 ring 模型也不冒充 10000 帧真实 NIC 流量。详见 [`test-contract.md`](test-contract.md) 和 `contract-results/CONTRACT_VALIDATION.txt`。

## 构建与运行

### Linux

```bash
sudo apt-get install \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
  cmake python3 qemu-system-x86 ovmf

cmake -S . -B build/uefi \
  -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-uefi-mingw.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/uefi --parallel
cmake --build build/uefi --target qemu-test
```

发布验证入口（封装主机契约 / CI 子集 / 完整矩阵）：

```bash
python3 tools/release_validate.py --profile host \
  --work-dir build/release-validate-host
cmake --build build/uefi --target release-validate-host
# 对齐 CI 的 QEMU 子集（需已构建的 ESP）：
python3 tools/release_validate.py --profile ci \
  --image build/uefi/cxc-os.img --work-dir build/release-validate
# 干净构建 + M13 成功矩阵 + 7 负对照：
python3 tools/release_validate.py --profile full \
  --work-dir build/release-validate-full
```

主机侧协议/ring C++ 单测（普通 g++，不依赖 MinGW UEFI toolchain）：

```bash
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests --parallel
ctest --test-dir build/host-tests --output-on-failure
```

双 LAPIC、CPU 拓扑与 DMA 边界：

```bash
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 4 --cpu "qemu64,-x2apic" --work-dir build/test-xapic
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 4 --cpu "qemu64,+x2apic" --work-dir build/test-x2apic
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 1 --cpu "qemu64,+x2apic" --work-dir build/test-up
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 16 --cpu "qemu64,+x2apic" --work-dir build/test-smp16
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --cpus 4 --cpu "qemu64,+x2apic" --memory 512M \
  --work-dir build/test-512m
```

无注入负对照（预期 panic，命令成功表示捕获到该预期）：

```bash
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --no-network-peer --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-no-peer
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --no-protocol-peer --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-no-protocol
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --bad-icmp-checksum --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-bad-icmp
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --no-udp-peer --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-no-udp
python3 tools/run_qemu.py --image build/uefi/cxc-os.img \
  --check --bad-udp-checksum --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/test-bad-udp
```

正常 `--check` / 默认 `qemu-test` 的成功条件是串口 `CXC_OS_SELF_TEST_PASS`、`ONBOX_CCDRV_PASS` 与 `peer_ok=1` 同时成立；日志位于各自 work directory 的 `serial.log`、`network-peer.log`。

### Windows

在 **MSYS2 UCRT64** 终端：

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-python

cmake -S . -B build/uefi -G "MinGW Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-uefi-mingw.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/uefi --parallel
```

Windows 构建由 CI 配置覆盖；当前本机实测为 Linux + QEMU/OVMF，未把 CI 配置写成已经执行的结果。

## 当前边界

**内核 / SMP / 网**

- 迁移由 worker 显式请求，任意时刻仅一个在途；无自动迁移、work stealing 或负载平衡；
- 全局 run-queue/allocator 锁以正确性为先，不宣称可扩展性能；
- 线程仍由 BSP 创建；支持 Finished→Empty 与可选栈页归还；尚无非 BSP 创建、condvar、优先级继承或通用阻塞 IO；
- 运行时 VMM：BSP 发起的已存在 4 KiB PTE 权限更新 + shootdown；无通用并发页表编辑、PCID/INVPCID；
- 最多 16 个 enabled CPU；IRQ 固定容量；无 shared IRQ / 中断线程化 / CPU hotplug；
- e1000e 仅 queue 0 legacy ring；无多队列、完整 PHY/reset、offload；包测走 QEMU dgram peer，未测物理网卡；
- 协议面：固定邻居 ARP/ICMP/UDP Echo + 最小 socket API；无 TCP/IPv6/ephemeral 端口。

**用户态 / 存储 / 工具链**

- 有 ring-3、静态 ELF、`disk/` CXCFS、协作式 fork/exec、mini_libc，以及可加载的宿主 musl 静态 ELF；
- **无**动态链接、按需分页、GUI、抢占式多用户调度；
- 机上工具链是 **CXC_A2 方言 + mini as/ld + `disk/cc` argv**，不是 ISO C / 真 gcc·gas·ld；
- HARD 作业中部分 musl/叶子 `.o` 为宿主预编译种植；嵌套 `cc -c` 与外部 as/ld 由内核 `execve` 审计；
- **未宣称 A2**（完整机上 g++/gcc 自举）；当前合同终点是 `ONBOX_CCDRV_PASS`。

**组织目标**：① **A2** 不依赖宿主的机上完整编译器；② **B** 生产级 OS。当前主线停在 CCDRV（见 [`a2-mini-cc.md`](a2-mini-cc.md)、[`midpoint-assessment-28.md`](midpoint-assessment-28.md)）；下一刀继续加深机上工具链/musl 闭包，合同只增不减，保持 `SELF_TEST_PASS` + `peer_ok=1` + `ONBOX_CCDRV_PASS` 不回退。
