# CXC/OS 第十五次中场评估：M145–M154 进度、证据与下一主线

日期：2026-10-27  
基线：Milestone 154 / v2.30.0  
对照：第十四次中场（[`midpoint-assessment-14.md`](midpoint-assessment-14.md)）冻结在 **M135–M144**；本评估覆盖 **M145–M153**，并以 **M154** 为本中场窗口（整链加深压力 + 文档/CI 统一）。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；mini-cc：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_DEEP_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十四次中场之后，机上工具链在 WHOL 合流上**可回归地加深** musl 调用面、as 帧/寻址、前端类型语义与链接容量/reloc，并以整链探针收束：

1. **musl str* 真调**（M145–M146）：探针调用 `strcpy`/`strcat`/`strncmp`/`stpcpy`；elflink 接受 STB_WEAK 导出；
2. **as 帧与 rip-mov**（M147–M148）：`push`/`pop` 扩寄存器、`imul`、`mov name(%rip)`；gcc -S 裁剪样例；
3. **sizeof / size_t / char***（M149–M150）：`sizeof(char)==1`、指针 8、`typedef unsigned long size_t`；
4. **容量/reloc/混沌**（M151–M152）：`R_X86_64_64`、节/导出/UND 上限抬升；混沌种子 `0xC152`；
5. **本窗 M154**：单一探针同时锻炼 as+sizeof+str*+ld → `ONBOX_DEEP_PASS`。
6. **中场根因修复**：`->` 字段偏移与栈局部降序布局对齐；ET_EXEC 拆 RX/RW 双 `PT_LOAD`（满足 W^X），导入 `.data` 进 RW 并延迟 rip-data 重定位。

相对第十四次中场，质变是：

- 机上成功路径覆盖 **已链 musl 的真实调用** 与 **帧/类型语义**；
- host **边缘 ≥ 成功** 且混沌种子升级进 CI；
- 仍 **不** 宣称 A2。

| 维度 | 第十五次中场判断 |
|---|---|
| M0–M144 定义内目标 | 仍完成且合同未降级 |
| M145–M154 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | 自研整链合同加深；非完整 g++/gcc |
| 下一主线 | 更大 musl 闭包（忌盲开 strtol）/ 诊断与容量 |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M145–M154）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M145–M146 | musl str* 真调 + WEAK 导出 | 曾用 `ONBOX_SCALL_PASS`；host `m146-scall` |
| M147–M148 | as 帧 / imul / rip-mov | host `m148-afrm` |
| M149–M150 | sizeof / size_t / char* | host `m150-sizeof` |
| M151–M152 | 容量 / `R_X86_64_64` / 混沌 | host `m152-*` + `cc_chaos_test` seed `0xC152` |
| **M154** | 整链加深压力 | `ONBOX_DEEP_PASS` + `WMSG:DP` + `deep: 1` + `edge: 1` |
| 中场修根 | `->` 栈布局；双 PT_LOAD + 导入 data→RW | 串口 `NMSG:42` 可回归 |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 协作式调度；非抢占。  
4. 网络合同冻结面未扩大（刻意）。  
5. 真 gcc/g++ 二进制仍不落盘。  
6. `.bss` 名接受但非真 NOBITS。  
7. ET_EXEC 的 `.rodata` 与 `.data` 同属 RW 段（W^X 权衡；非严格 RO）。

## 4. 下一主线裁定

1. **优先**：在不宣称 A2 的前提下继续扩大可链 musl 簇或加深诊断合同。  
2. **可选**：更多重定位类型；链接器容量。  
3. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退；混沌/边缘继续同权。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_DEEP_PASS` + host chaos；  
3. 无超时刷绿；  
4. 基线推进至 **M154 / v2.30.0**；下一正式中场约 **M164**。
