# CXC/OS 第十三次中场评估：M125–M134 进度、证据与下一主线

日期：2026-10-26  
基线：Milestone 134 / v2.25.0  
对照：第十二次中场（[`midpoint-assessment-12.md`](midpoint-assessment-12.md)）冻结在 **M115–M124**；本评估覆盖 **M125–M133**，并以 **M134** 为本中场窗口（混沌套件 + 文档/CI 统一）。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；mini-cc：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_MERG_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十二次中场之后，机上 `cxc-cc` 从「真 musl 跨对象 UND + 自产 rela」推进到 **自研 as+ld 子集与整链合流**，并加深前端窄类型合同：

1. **延迟 `#lib` UND / 字符串闭包**（M125–M126）；
2. **机上 as / as+.rodata / mini-ld**（M127–M129）；
3. **ld 链 musl string + as 加深 + char/const**（M130–M132）；
4. **本窗合流 M133**：`lib.c`(cc) + `main.s`(as) + `link.rsp`(ld+mcmp) → `ONBOX_MERG_PASS`；
5. **M134 中场**：固定种子 host 混沌套件进合同；评估成文；CI/grep/docs 统一。

相对第十二次中场，质变是：

- 机上链路同时锻炼 **as → .o、cc → .o、ld → ET_EXEC、elflink 多节 musl**；
- 前端有 **char 字宽截断 + const 拒绝再赋值** 的可回归合同；
- host **边缘与混沌同权**（非法 asm/C、DUP/坏 ELF、固定种子破坏）；
- 仍 **不** 宣称 A2。

| 维度 | 第十三次中场判断 |
|---|---|
| M0–M124 定义内目标 | 仍完成且合同未降级 |
| M125–M133 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | **自研 as+ld+cc 整链可回归**；非完整 g++/gcc |
| 下一主线 | **更大 musl 闭包（忌盲开 strtol）/ 前端加深 / 链接器健壮性** |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M125–M134）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M125–M126 | 延迟 UND / string kit 闭包 | 曾用 LUND / SKIT |
| M127–M129 | as / as+rodata / mini-ld | 曾用 ASM / ASRO / LD |
| M130 | ld+musl string | 曾用 `ONBOX_LDLIB_PASS` |
| M131 | as 延迟 lea + 指令/.data | 曾用 `ONBOX_ASDP_PASS` |
| M132 | char 截断 + const | 曾用 `ONBOX_CHAR_PASS` |
| **M133** | as+cc+ld+musl 合流 | `ONBOX_MERG_PASS` + `WMSG:MX` + `merg: 1` + `edge: 1` |
| **M134** | host 混沌套件 + 中场统一 | `cc_chaos_test: PASS` + 本文 + CI |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 协作式调度；非抢占。  
4. 网络合同冻结面未扩大（刻意）。  
5. 真 gcc/g++ 二进制仍不落盘。

## 4. 下一主线裁定

1. **优先**：在不宣称 A2 的前提下扩大可链 musl 簇或加深前端/后端合同。  
2. **可选**：更多重定位类型显式失败面；链接器容量与诊断。  
3. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退；混沌/边缘继续同权。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_MERG_PASS` + host chaos；  
3. 无超时刷绿；  
4. 基线推进至 **M134 / v2.25.0**；下一正式中场约 **M144**。
