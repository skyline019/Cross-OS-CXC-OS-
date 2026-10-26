# CXC/OS 第九次中场评估：M85–M94 进度、证据与下一主线

日期：2026-10-26  
基线：Milestone 94 / v0.95.0  
对照：第八次中场（[`midpoint-assessment-8.md`](midpoint-assessment-8.md)）冻结在 **M75–M84**，裁定「pipe/dup + 工具链级 POSIX」；本评估覆盖其后 **M85–M93** 能力切片，并以 **M94** 作为本中场窗口本身。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；地址空间 ADR：[`adr-user-address-space.md`](adr-user-address-space.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；测试：[`test-contract.md`](test-contract.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证口径：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；冻结网络合同 **不回退**；用户/存储/机上编译合同扩展至 `ONBOX_CGEN_PASS`（`exit: 42`）。QEMU 验收底线 **128 MiB**（64M 无法装下当前 BOOTX64）。

## 1. 结论摘要

第八次中场裁定的「pipe/dup → 工具链 POSIX」路径 **已执行并越过机上编译闭环起步**：本周期落地管道、目录语义、容量抬升、更大 ELF/堆、时间 syscall，以及机上 `cxc-cc` 从嵌入模板走到 **真实 `return N` 前端 codegen**。网络合同全程保持绿。

1. **工具链 POSIX**（M85 pipe/dup → M86 chdir → M87 多级 mkdir）；
2. **规模抬升**（M88 CXCFS 128 KiB / 16 MiB 盘 → M89 ELF 128 KiB → M90 brk/mmap 1 MiB）；
3. **时间与探测**（M91 access/clock_gettime/gettimeofday）；
4. **A2 机上编译起步**（M92 in.c→out.elf→execve → M93 解析 `return N`→exit(N)）；
5. **本中场窗口**（M94）：评估成文、债项刷新、下一主线裁定、基线锚点。

相对第八次中场，质变是：

- 从「缺管道」进入 **pipe/dup + fork IPC**；
- 从「单层目录 / 小文件」进入 **多级 mkdir + 128 KiB 文件 + 100 KiB execve**；
- 从「无机构上编译」进入 **`cxc-cc` 读源 → 写 ELF → execve**，且 **exit 码由源码决定**（非固定嵌入）；
- QEMU 底线明确为 **128 MiB**，避免 64M OOM 假失败。

当前冻结 M13–M93 验收范围内无已知未决失败。这 **不** 表示 A2（机上 g++/gcc）已完成，也不表示完整 C 前端或抢占式多进程已完成。

一句话状态：

| 维度 | 第九次中场判断 |
|---|---|
| M0–M84 定义内目标 | 仍完成且合同未降级 |
| M85–M93 增量 | 完成且有 QEMU 串口 + peer 证据 |
| Linux + QEMU 验证 | 强（CI Assert 同步到 M93；底线 128M） |
| 用户态 / musl | 真 musl 静态子集 + 持久 `disk/` + 机上 cxc-cc |
| 文件系统 | CXCFS 64×128 KiB、16 MiB virtio；嵌套目录 |
| 进程 | 协作式 fork/wait + pipe IPC；非抢占 |
| A2 就绪度 | **编译→运行闭环 + return-N 前端**；距 g++/gcc 仍远 |
| 下一主线 | **表达式/多语句前端或嵌入汇编**（仍服务 A2） |
| 最大风险 | 假称 A2；或停在玩具前端不加深语义 |

## 2. 相对第八次中场：增量盘点

### 2.1 工具链 POSIX（M85–M87）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M85 | `pipe`/`dup`/`dup2`；fork 克隆 fd | `PIPE_PASS`；EPIPE + dup |
| M86 | `chdir`/`getcwd`；相对路径 | `CHDIR_PASS`；ENOENT/ERANGE |
| M87 | 多级 `mkdir`；孤儿边缘 | `NEST_PASS`；persist |

### 2.2 规模与加载（M88–M90）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M88 | 128 KiB/文件；16 MiB virtio；`max_io=4096` | `CAP_PASS`；96 KiB 回读；QEMU≥128M |
| M89 | 静态 ELF 加载器 128 KiB；disk execve | `LOAD128_PASS`；elf_bytes 102400 |
| M90 | 用户栈 `0x800000`；brk/mmap 各 1 MiB | `HEAP_PASS`；768+512 KiB |

### 2.3 时间 syscall（M91）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M91 | `access`/`clock_gettime`/`gettimeofday` | `TIME_PASS`；ENOENT/EINVAL |

### 2.4 A2 机上编译（M92–M93）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M92 | `cxc-cc`：in.c→out.elf→execve | 曾用 `ONBOX_CC_PASS`；闭环起步 |
| M93 | 解析 `return N`；栈上拷贝 patch（避 RX 写） | `ONBOX_CGEN_PASS`；`exit: 42` |

### 2.5 本中场（M94）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M94 | 第九次中场评估 + 下一主线裁定 | 本文；v0.95.0 |

## 3. 证据强度（第九次中场口径）

必须同时满足才可声称「本阶段回归通过」：

1. 内核串口 `CXC_OS_SELF_TEST_PASS`；
2. 外部 peer `peer_ok=1`（M13 十帧 + M14 256 stress）；
3. 冻结网络串口不变；
4. M85–M93 合同全绿（含 `ONBOX_CGEN_PASS` + `exit: 42`）；
5. `contract_test.py` 与 CI Assert 同步到 M93；QEMU 底线 128 MiB。

**未宣称**：机上 g++/gcc（A2 完成）、完整 C/C++ 前端、抢占式多进程、SYSRET。

## 4. 工程质量再评估

| 维度 | 第八次中场 | 第九次中场 | 说明 |
|---|---|---|---|
| 机制真实性 | 强+++ | 强++++ | +pipe + 机上 codegen |
| 正确性验收 | 强 | 强 | 网络冻结；边缘有自动化 |
| A2 就绪度 | 持久根 + exec + fork | **+pipe + 容量 + return-N 前端** | 下一硬阻塞=语义前端/汇编 |
| 可扩展性 | 中+ | 中+ | cxc-cc 仍为手写机器码前端 |
| 通用 OS 完整度 | 教学 Unix 初段 | **教学 Unix + 玩具编译器** | 距生产工具链仍远 |

## 5. 技术债登记（刷新）

### 已还清（相对第八次 P0）

| 主题 | 结论 |
|---|---|
| pipe + dup/dup2 | M85 |
| chdir/getcwd；多级目录 | M86–M87 |
| 更大文件/盘/ELF/堆 | M88–M90 |
| 机上编译闭环起步 | M92–M93 |

### 仍开放

#### P0（通向 A2）

- **表达式 / 多语句 / 调用约定** 前端（不止 `return N`）；
- 嵌入汇编或可链接目标格式加深；
- 更大堆/映射若工具链需要再抬；
- 仍非抢占：协作 fork 不支持父子并发跑。

#### P1

- TLS 完整布局；用户 VMA/页回收；
- envp/argv 容量与错误码精细化；TD-07 全局锁；
- cxc-cc 错误诊断（非法源 → 非 panic 的可回归失败路径）。

#### P2

- TCP、完整 VFS、信号、权限、多架构、soak、抢占式用户调度；
- 宿主无关的完整 gcc/g++ 移植。

## 6. 主线裁定（本中场锁定）

### 6.1 选项

| 选项 | 收益 | 风险 | 决定 |
|---|---|---|---|
| A. 立即抢占式多进程调度 | 更「真」 | 面大；稀释 A2 | **否**（本窗口） |
| B. **表达式/多语句前端或嵌入汇编** | 直指可编译非玩具程序 | 中等 | **下一主线** |
| C. 重开 TCP/网络 | 网络更深 | 稀释 A2 | **否** |
| D. 一次上完整 gcc | 直达 A2 | 前端/规模不足会炸 | **否**；先 B |

### 6.2 继续有效的纪律（沿用 M74）

1. 大跨步交付完整能力层；禁止 PASS 后拆除通向 A2 的路径。  
2. 大块变更预留调试点（stage/failure/计数）。  
3. 用测试调试；边缘与成功路径同权。  
4. 不宣称 A2，直到机上 g++/gcc 合同另立。

### 6.3 推荐下一主线

1. `cxc-cc` 加深：至少 **write 字符串** 或 **简单算术表达式 → 立即数/栈演算** 可回归；  
2. 可选：嵌入 `.S` 迷你汇编路径；  
3. 保持 `PASS` + `peer_ok=1` 与 M85–M93 合同不回退。

退出标准（下阶段，仍非 A2）：源码变化能改变 **可观测输出**（不只 exit 码）；**仍不宣称 A2**。

## 7. 最终评价

第九次中场证明：在冻结网络合同与 128 MiB QEMU 底线下，可以从 pipe/容量抬升走到机上 `cxc-cc` 真实 `return N` codegen，而不回退 musl/存储/网络证据。

下一阶段成败标准：

- 默认 `qemu-test` 仍 `PASS` + `peer_ok=1`；  
- M85–M93 合同不回退；  
- 前端语义加深（表达式/多语句/汇编）带调试点与边缘；  
- 对外表述区分「玩具机上编译器」与「A2 机上 g++/gcc」。

**第九次中场建议：POSIX/规模/机上闭环收束有效；主靶仍为 A2；下一刀优先表达式或多语句前端（或嵌入汇编）；抢占调度不抢主线。**

---

## 8. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 已同步到 M93（本窗口再核对）；本窗口无新增用户 PASS；  
3. 无超时刷绿；  
4. 基线推进至 **M94 / v0.95.0**；节奏表登记第九次中场；下一正式中场目标约 **M104**。
