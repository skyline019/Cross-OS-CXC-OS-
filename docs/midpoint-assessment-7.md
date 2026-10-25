# CXC/OS 第七次中场评估：M65–M74 进度、证据与新格局

日期：2026-10-26  
基线：Milestone 74 / v0.75.0  
对照：第六次中场（[`midpoint-assessment-6.md`](midpoint-assessment-6.md)）冻结在 **M55–M64** 并裁定下一刀「极小静态 musl 子集链接」；本评估覆盖其后 **M65–M73** 能力切片，并以 **M74** 作为本中场窗口本身。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；地址空间 ADR：[`adr-user-address-space.md`](adr-user-address-space.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；测试：[`test-contract.md`](test-contract.md)；musl：[`musl-subset-link-notes.md`](musl-subset-link-notes.md)。  
本地实证口径：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；冻结网络合同 **不回退**；用户态合同扩展至 `USER_MUSL_UNL_PASS`。

## 1. 结论摘要

第六次中场建议的「极小静态 musl 子集」路径 **已执行并越过标注子集**：本周期落地真 `musl-gcc` 静态 CRT 与多条 stdio/目录/生命周期 API，网络合同全程保持绿。

1. **SYSCALL 面**（M66 LSTAR/STAR；M67 栈 auxv + `exit_group`）；
2. **真 musl CRT / 堆 / stdio**（M68 CRT → M69 malloc → M70 printf → M71 fopen）；
3. **目录与生命周期**（M72 opendir/readdir → M73 unlink）；
4. **布局/stub**（brk/栈抬升；`mprotect`/`ioctl`/`fcntl` stub；`readv`）；
5. **本中场窗口**（M74）：评估成文、**新格局锁定**、测试契约扩边缘、基线锚点。

相对第六次中场，质变是：

- 从「stub mini_libc」进入 **真 musl-gcc 静态可回归路径**（CRT→malloc→printf→fopen→opendir→unlink）；
- ENOSYS 驱动补号有效（尤其 `readv`、布局冲突）；
- **ramfs 仍是 A2 硬阻塞**：无块设备则无诚实的机上工具链日程。

当前冻结 M13–M73 验收范围内无已知未决失败。这 **不** 表示 A2、完整 POSIX、或持久存储已完成。

一句话状态：

| 维度 | 第七次中场判断 |
|---|---|
| M0–M64 定义内目标 | 仍完成且合同未降级 |
| M65–M73 增量 | 完成且有 QEMU 串口 + peer 证据 |
| Linux + QEMU 验证 | 强（CI Assert 同步到 M73） |
| 用户态 / musl | 真 musl 静态子集已通；仍单会话 BSP 探针 |
| 文件系统 | 内存 ramfs；**无块设备** |
| 下一主线 | **大跨步**：virtio-blk（或等价）薄但是整层；**非**继续 musl 烟测堆栈 |
| 最大风险 | 大跨步无调试点/无边缘合同 → 难定位；或假称 A2 |

## 2. 相对第六次中场：增量盘点

### 2.1 SYSCALL / 启动（M65–M67）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M65 | `musl_subset` 静态 write | `USER_MUSL_PASS`；`LIBMSL.ELF` |
| M66 | LSTAR/`syscall` 入口 | 与 int80 同分发 |
| M67 | 用户栈 argc/argv/envp/auxv；`exit_group` | `USER_MUSL_AUXV_PASS`；`LIBMAV.ELF` |

### 2.2 真 musl-gcc（M68–M71）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M68 | PHDR-in-LOAD CRT | `USER_MUSL_CRT_PASS`；`LIBMCR.ELF` |
| M69 | `malloc`/`free` | `USER_MUSL_MAL_PASS`；`LIBMMA.ELF` |
| M70 | `printf`/`fflush`；brk→`0x407000` | `USER_MUSL_PRT_PASS`；`LIBMPR.ELF` |
| M71 | `fopen` 族；brk `0x409000`/栈 `0x420000`；`readv`+stub | `USER_MUSL_FOP_PASS`；`LIBFOP.ELF` |

### 2.3 目录与生命周期（M72–M73）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M72 | `mkdir`/`opendir`/`readdir`/`closedir` | `USER_MUSL_ODR_PASS`；`LIBODR.ELF` |
| M73 | `unlink`（syscall 87）+ 再创建 | `USER_MUSL_UNL_PASS`；`LIBUNL.ELF` |

### 2.4 本中场（M74）

| 里程碑 | 能力 | 证据要点 |
|---|---|---|
| M74 | 第七次中场 + **新格局** + 边缘契约扩域 | 本文；v0.75.0；[`test-contract.md`](test-contract.md) §8 |

## 3. 证据强度（第七次中场口径）

必须同时满足才可声称「本阶段回归通过」：

1. 内核串口 `CXC_OS_SELF_TEST_PASS`；
2. 外部 peer `peer_ok=1`（M13 十帧 + M14 256 stress）；
3. 冻结网络串口不变；
4. 用户态合同至 `USER_MUSL_UNL_PASS` 及对应 `[ok] user musl …` 行；
5. `contract_test.py` 与 CI Assert 同步到 M73（本窗口核对；无新增用户 PASS）。

**未宣称**：机上编译器（A2）、完整 POSIX、SYSRET、块设备。

## 4. 工程质量再评估

| 维度 | 第六次中场 | 第七次中场 | 说明 |
|---|---|---|---|
| 机制真实性 | 强+ | 强++ | 真 musl + LSTAR |
| 正确性验收 | 强 | 强 | 网络冻结；musl 合同独立 |
| A2 就绪度 | CRT+mini_libc | **真 musl 子集已通** | 下一硬阻塞=持久存储 |
| 可扩展性 | 中+ | 中 | 探针堆叠；大跨步前需调试点纪律 |
| 通用 OS 完整度 | 早期+++ | **早期++++ / 最小可用前夜** | 距 A2 仍缺块设备+进程 |

## 5. 技术债登记（刷新）

### 已还清（相对第六次 P0）

| 主题 | 结论 |
|---|---|
| 极小静态 musl 子集链接 | M68–M73 真 musl-gcc 路径 |
| LSTAR / auxv / PHDR-in-LOAD | M66–M68 |
| stdio 读路径 `readv` | M71 |
| 文件生命周期 `unlink` | M73 |

### 仍开放

#### P0（通向 A2）

- **块设备 + 可挂载根**（virtio-blk 或等价）：**本中场裁定为主线大跨步**；
- 进程/`exec`/可执行映像生命周期；
- 编译器级 POSIX 与内存规模。

#### P1

- TLS 完整布局；用户页回收/VMA；TD-07 全局锁；边缘合同落地实现（本窗口先立契约）。

#### P2

- TCP、完整 VFS、信号、权限、多架构、soak。

## 6. 主线裁定与新格局（本中场锁定）

### 6.1 选项

| 选项 | 收益 | 风险 | 决定 |
|---|---|---|---|
| A. 继续 musl 薄烟测（sprintf/getenv…） | 表面 API 变多 | 边际递减；不解除 A2 硬阻塞 | **否为主线** |
| B. **virtio-blk（或等价）整层大跨步** | 直指持久根 / A2 | 面大、难调 | **下一主线** |
| C. 重开 TCP/网络 | 网络更深 | 稀释 A2 | **否** |
| D. 完整 VFS 一次做完 | 理想 | 爆炸 | **否**；随块设备薄垂直切片 |

### 6.2 新格局（M74 之后生效）

1. **大跨步，非薄切片堆叠**：单变更集可跨驱动/syscall/ESP/合同多面，但仍须可回归、可回滚叙事清晰。  
2. **大块更新必须预留调试点**：串口 stage/failure/exit、关键路径计数、可选 `CXC_DEBUG_*` 门控；禁止「整层绿或整层哑」。  
3. **用测试调试 bug**：先补失败现场与边缘用例，再改产品代码；禁止靠加超时/删断言刷绿（延续中场硬契约）。  
4. **测试契约将边缘纳入范围，且边缘比较重要**：成功路径仍必要；**负例/边界/非法输入/资源耗尽** 进入默认验收权重（见 [`test-contract.md`](test-contract.md) §8）。冻结的 M13 Echo 字符串仍不改写。

### 6.3 推荐下一主线

1. virtio-blk（QEMU）只读或读写探针 → 用户态可见块/文件根的最小合同；  
2. 同变更集带齐调试点 + 边缘用例（坏扇区/越界 LBA/空设备等按切片定义）；  
3. 保持 `PASS` + `peer_ok=1` 与既有 musl 合同不回退。

退出标准（下阶段，仍非 A2）：至少一条「块设备参与」的可回归路径；**仍不宣称 A2**，直到机上工具链合同另立。

## 7. 最终评价

第七次中场证明：在冻结网络合同下，真 musl 静态子集可以从 CRT 走到 fopen/目录/unlink。

下一阶段成败标准：

- 默认 `qemu-test` 仍 `PASS` + `peer_ok=1`；  
- musl 合同不回退；  
- 大跨步交付带调试点；边缘合同开始有自动化证据；  
- 对外表述区分「真 musl 子集」与「A2 机上编译器」。

**第七次中场建议：结束 musl 烟测主叙事；主靶仍为 A2；下一刀大跨步切块设备；测试以边缘为重。**

---

## 8. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 与 M65–M73 同步（本窗口再核对）；无新增用户 PASS；  
3. 无超时刷绿；  
4. 基线推进至 **M74 / v0.75.0**；新格局与边缘契约写入 [`midfield-cadence.md`](midfield-cadence.md)、[`test-contract.md`](test-contract.md)；下一正式中场目标约 **M84**（或按大跨步重新标定，见节奏表）。
