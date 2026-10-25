# CXC/OS 第八次中场评估：M75–M84 进度、证据与下一主线

日期：2026-10-26  
基线：Milestone 84 / v0.85.0  
对照：第七次中场（[`midpoint-assessment-7.md`](midpoint-assessment-7.md)）冻结在 **M65–M74**，裁定「块设备大跨步 + 边缘契约」；本评估覆盖其后 **M75–M83** 能力切片，并以 **M84** 作为本中场窗口本身。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；地址空间 ADR：[`adr-user-address-space.md`](adr-user-address-space.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；测试：[`test-contract.md`](test-contract.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证口径：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；冻结网络合同 **不回退**；用户/存储合同扩展至 `FORK_WAIT_PASS`。

## 1. 结论摘要

第七次中场裁定的「virtio-blk 整层大跨步」路径 **已执行并越过探针**：本周期落地持久块设备、CXCFS 可挂载根、`disk/` 用户态、execve 映像替换（含 argv/envp）、rename，以及协作式 fork/wait4。网络合同全程保持绿。

1. **块设备层**（M75 读探针 → M76 持久 R/W，设备保持在线）；
2. **持久根**（M77 CXCFS → M78 musl fopen → M79 目录/容量）；
3. **可执行映像**（M80 execve → M81 argv + 更大 ELF → M82 envp + rename）；
4. **进程模型起步**（M83 协作式 fork/wait4 + AS clone；修 destroy 叶子页债）；
5. **本中场窗口**（M84）：评估成文、债项刷新、下一主线裁定、基线锚点。

相对第七次中场，质变是：

- 从「ramfs 硬阻塞」进入 **virtio-blk + CXCFS `disk/` 可回归持久根**；
- 从「单映像探针」进入 **execve 换映像 + argv/envp**；
- 从「单会话」进入 **协作式父子进程（fork→exit→wait）**；
- 边缘合同在块设备 / rename / ECHILD 等路径上有自动化证据。

当前冻结 M13–M83 验收范围内无已知未决失败。这 **不** 表示 A2、完整 POSIX、或抢占式多进程调度已完成。

一句话状态：

| 维度 | 第八次中场判断 |
|---|---|
| M0–M74 定义内目标 | 仍完成且合同未降级 |
| M75–M83 增量 | 完成且有 QEMU 串口 + peer 证据 |
| Linux + QEMU 验证 | 强（CI Assert 同步到 M83） |
| 用户态 / musl | 真 musl 静态子集 + 持久 `disk/` 路径 |
| 文件系统 | CXCFS on virtio-blk（64×32 KiB）+ ramfs 并存 |
| 进程 | 协作式单子进程 fork/wait；非抢占调度 |
| 下一主线 | **pipe/dup + 工具链级 POSIX**（仍服务 A2） |
| 最大风险 | 假称 A2；或过早抢占多进程稀释证据 |

## 2. 相对第七次中场：增量盘点

### 2.1 块设备与持久根（M75–M79）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M75 | virtio-blk 现代 PCI 读探针 + OOB | `VIRTIO_BLK_PASS`；stage/failure |
| M76 | 持久扇区 R/W；设备在线 | `VIRTIO_BLK_RW_PASS`；roundtrip/oob_w/online |
| M77 | CXCFS + `disk/` 门面；remount | `CXCFS_PASS`；persist/disk_user |
| M78 | musl-gcc fopen `disk/` | `USER_MUSL_BLK_PASS`；`LIBBLK.ELF` |
| M79 | mkdir/opendir；64×16 KiB；4 MiB 盘 | `USER_MUSL_DIR_PASS`；big |

### 2.2 映像与 POSIX 文件（M80–M82）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M80 | `execve` 自 `disk/run.elf` | `DISK_EXEC_PASS`；EXSTB/DEXEC |
| M81 | argv + 32 KiB 文件 + ~20 KiB ELF | `DISK_ARGV_PASS`；argc: 3 |
| M82 | envp + `rename`（持久+边缘） | `DISK_ENV_PASS`/`DISK_RENAME_PASS` |

### 2.3 进程模型（M83）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M83 | 协作式 `fork`/`wait4`；AS 深拷贝；destroy 叶子页债 | `FORK_WAIT_PASS`；status: 42；ECHILD |

### 2.4 本中场（M84）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M84 | 第八次中场评估 + 下一主线裁定 | 本文；v0.85.0 |

## 3. 证据强度（第八次中场口径）

必须同时满足才可声称「本阶段回归通过」：

1. 内核串口 `CXC_OS_SELF_TEST_PASS`；
2. 外部 peer `peer_ok=1`（M13 十帧 + M14 256 stress）；
3. 冻结网络串口不变；
4. M75–M83 存储/exec/fork 合同全绿（含边缘标记）；
5. `contract_test.py` 与 CI Assert 同步到 M83。

**未宣称**：机上编译器（A2）、完整 POSIX、抢占式多进程、pipe/dup、SYSRET。

## 4. 工程质量再评估

| 维度 | 第七次中场 | 第八次中场 | 说明 |
|---|---|---|---|
| 机制真实性 | 强++ | 强+++ | 块设备 + execve + fork |
| 正确性验收 | 强 | 强 | 网络冻结；边缘有自动化 |
| A2 就绪度 | 真 musl；缺存储 | **持久根 + exec + 协作 fork** | 下一硬阻塞=pipe/工具链规模 |
| 可扩展性 | 中 | 中+ | 单子进程协作模型有意收窄 |
| 通用 OS 完整度 | 最小可用前夜 | **教学 Unix 初段** | 距 A2 仍缺管道与更大 POSIX |

## 5. 技术债登记（刷新）

### 已还清（相对第七次 P0）

| 主题 | 结论 |
|---|---|
| 块设备 + 可挂载根 | M75–M79 CXCFS `disk/` |
| 进程/exec/映像生命周期 | M80–M83 execve + fork/wait |
| destroy 用户叶页泄漏 | M83 修债 |

### 仍开放

#### P0（通向 A2）

- **pipe + dup/dup2**（编译器驱动管道与重定向）；
- 更大文件/盘容量与多级目录（工具链落盘）；
- `chdir`/`getcwd` 等构建目录语义；
- 仍非抢占：协作 fork 不支持父子并发跑。

#### P1

- TLS 完整布局；用户 VMA/页回收；FD 在 fork 上的显式复制表；
- envp/argv 容量与错误码精细化；TD-07 全局锁。

#### P2

- TCP、完整 VFS、信号、权限、多架构、soak、抢占式用户调度。

## 6. 主线裁定（本中场锁定）

### 6.1 选项

| 选项 | 收益 | 风险 | 决定 |
|---|---|---|---|
| A. 立即抢占式多进程调度 | 更「真」 | 面大；稀释 A2 证据 | **否**（本窗口） |
| B. **pipe/dup + 工具链 POSIX** | 直指 gcc 驱动 | 中等 | **下一主线** |
| C. 重开 TCP/网络 | 网络更深 | 稀释 A2 | **否** |
| D. 一次上机上 gcc | 直达 A2 | 缺管道/规模会炸 | **否**；先 B |

### 6.2 继续有效的纪律（沿用 M74）

1. 大跨步交付完整能力层；禁止 PASS 后拆除通向 A2 的路径。  
2. 大块变更预留调试点（stage/failure/计数）。  
3. 用测试调试；边缘与成功路径同权。  
4. 不宣称 A2，直到机上工具链合同另立。

### 6.3 推荐下一主线

1. `pipe` + `dup`/`dup2`（及最小读写路径）可回归合同；  
2. 可选：`chdir` 或文件容量再抬升，服务工具链落盘；  
3. 保持 `PASS` + `peer_ok=1` 与 M75–M83 合同不回退。

退出标准（下阶段，仍非 A2）：至少一条「管道参与」的用户态可回归路径；**仍不宣称 A2**。

## 7. 最终评价

第八次中场证明：在冻结网络合同下，可以从 virtio-blk 探针走到持久根、execve、协作式 fork/wait，而不回退 musl/网络证据。

下一阶段成败标准：

- 默认 `qemu-test` 仍 `PASS` + `peer_ok=1`；  
- M75–M83 合同不回退；  
- pipe/dup（或等价）带调试点与边缘；  
- 对外表述区分「持久根 + 协作进程」与「A2 机上编译器」。

**第八次中场建议：块设备大跨步收束有效；主靶仍为 A2；下一刀优先 pipe/dup 与工具链级 POSIX；抢占调度不抢主线。**

---

## 8. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 与 M75–M83 同步（本窗口再核对）；本窗口无新增用户 PASS；  
3. 无超时刷绿；  
4. 基线推进至 **M84 / v0.85.0**；节奏表登记第八次中场；下一正式中场目标约 **M94**。
