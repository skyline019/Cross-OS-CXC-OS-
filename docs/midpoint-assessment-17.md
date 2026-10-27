# CXC/OS 第十七次中场评估：M165–M174 进度、证据与下一主线

日期：2026-10-27  
基线：Milestone 174 / v2.34.0  
对照：第十六次中场（[`midpoint-assessment-16.md`](midpoint-assessment-16.md)）冻结在 **M155–M164**；本评估覆盖 **M165–M173**，并以 **M174** 为本中场窗口（调用面闭合压力 + 文档/CI 统一）。  
规划参照：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_CLSR_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十六次中场之后，机上工具链在 MCLS 上**可回归地闭合**已种植未调用的 musl 符号，并再扩浅 ctype、加厚 as/前端：

1. **闭合已种未调**（M165–M166）：真调 `memcmp`/`strchr`/`strchrnul`；
2. **浅 ctype 种植**（M167–M168）：`toupper`/`isspace`（无深 UND）；
3. **as**（M169–M170）：`shl`/`shr`/`sal` + 更宽 `cmp %reg,%eax`；
4. **前端 + 混沌**（M171–M172）：`char**`/`void**`；混沌种子 `0xC174`；
5. **本窗 M174**：整链 → `ONBOX_CLSR_PASS` + `WMSG:CL` + `clsr: 1`。

仍 **不** 宣称 A2。

| 维度 | 第十七次中场判断 |
|---|---|
| M0–M164 定义内目标 | 仍完成且合同未降级 |
| M165–M174 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | 自研整链合同加深；非完整 g++/gcc |
| 下一主线 | 更大 musl 闭包（忌盲开 strtol）/ 可选严格 RO PT_LOAD |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M165–M174）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M165–M166 | memcmp/strchr/strchrnul 真调 | host trunc/UND 边缘 |
| M167–M168 | toupper/isspace 种植+真调 | 探针路径算 42 |
| M169–M170 | shl/shr + 宽 cmp | host `m170-as` / `m170-gccs` |
| M171–M172 | char**/void** + chaos `0xC174` | host `m172-pptr` |
| **M174** | 整链调用面闭合 | `ONBOX_CLSR_PASS` + `WMSG:CL` + `clsr: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 真 gcc/g++ 二进制仍不落盘。  
4. `.bss` 名接受但非真 NOBITS。  
5. ET_EXEC 的 `.rodata` 与 `.data` 同属 RW 段。

## 4. 下一主线裁定

1. **优先**：继续扩大可链 musl 簇或可选严格 RO 段。  
2. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_CLSR_PASS` + chaos `0xC174`；  
3. 无超时刷绿；  
4. 基线推进至 **M174 / v2.34.0**；下一正式中场约 **M184**。
