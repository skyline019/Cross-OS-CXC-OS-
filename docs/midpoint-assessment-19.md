# CXC/OS 第十九次中场评估：M185–M194 进度、证据与下一主线

日期：2026-10-27  
基线：Milestone 194 / v2.38.0  
对照：第十八次中场（[`midpoint-assessment-18.md`](midpoint-assessment-18.md)）冻结在 **M175–M184**；本评估覆盖 **M185–M193**，并以 **M194** 为本中场窗口（ctype 加宽 + 布局 + 文档/CI 统一）。  
规划参照：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_WIDE_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十八次中场之后，机上工具链在 SURF 上**可回归地加宽**闭包 ctype，让字段类型驱动 `sizeof`，并加厚 as：

1. **闭包 ctype**（M185–M186）：`isalnum`/`islower`（`nm -u` 空）；
2. **结构体布局**（M187–M188）：字段宽度累加→`sizeof`；偏移仍 pad-to-8；
3. **as**（M189–M190）：`leave`/`neg`/`movsbl`；
4. **混沌**（M191–M192）：种子 `0xC194`；
5. **本窗 M194**：整链 → `ONBOX_WIDE_PASS` + `WMSG:WD` + `wide: 1`。

仍 **不** 宣称 A2。

| 维度 | 第十九次中场判断 |
|---|---|
| M0–M184 定义内目标 | 仍完成且合同未降级 |
| M185–M194 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | 自研整链合同加深；非完整 g++/gcc |
| 下一主线 | 更大 musl 闭包（忌盲开 strtol）/ 可选严格 RO PT_LOAD |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M185–M194）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M185–M186 | isalnum/islower 种植+真调 | host trunc/缺路径；`m186-ctype-plant` |
| M187–M188 | 字段宽度→sizeof | host `m188-layout` |
| M189–M190 | leave/neg/movsbl | host `m190-as` / `m190-gccs` |
| M191–M192 | chaos `0xC194` | `cc_chaos_test` PASS |
| **M194** | 整链加宽 | `ONBOX_WIDE_PASS` + `WMSG:WD` + `wide: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 真 gcc/g++ 二进制仍不落盘。  
4. `.bss` 名接受但非真 NOBITS。  
5. ET_EXEC 的 `.rodata` 与 `.data` 同属 RW 段；第三 RO `PT_LOAD` 未拆。  
6. 字段偏移仍 pad-to-8（未做紧凑 char 打包访问）；禁止 `io.elf` 与 `slen.o`/`mcpy.o` 同 rsp。

## 4. 下一主线裁定

1. **优先**：继续扩大可链 musl 簇或可选严格 RO 段。  
2. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_WIDE_PASS` + chaos `0xC194`；  
3. 无超时刷绿；  
4. 基线推进至 **M194 / v2.38.0**；下一正式中场约 **M204**。
