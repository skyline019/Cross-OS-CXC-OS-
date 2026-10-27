# CXC/OS 第十八次中场评估：M175–M184 进度、证据与下一主线

日期：2026-10-27  
基线：Milestone 184 / v2.36.0  
对照：第十七次中场（[`midpoint-assessment-17.md`](midpoint-assessment-17.md)）冻结在 **M165–M174**；本评估覆盖 **M175–M183**，并以 **M184** 为本中场窗口（musl 表面 + 无 io memcpy + 文档/CI 统一）。  
规划参照：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_SURF_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十七次中场之后，机上工具链在 CLSR 上**可回归地扩大**浅 musl 表面，成功路径改由 musl `memcpy` 提供，并加厚 as/前端：

1. **浅 ctype/mem 种植**（M175–M176）：`isalpha`/`memset`（`nm -u` 空）；
2. **提供者切换**（M177–M178）：成功 rsp 挂 `mcpy.o`/`slen.o`，移出 `io.elf`；`ONBOX_SURF_PASS` / `WMSG:SF`；
3. **as**（M179–M180）：无符号 `div`/`movzx` + reg-reg `and`/`or`；
4. **前端 + 混沌**（M181–M182）：结构体字段类型名；混沌种子 `0xC184`；
5. **本窗 M184**：整链 → `ONBOX_SURF_PASS` + `WMSG:SF` + `surf: 1`。

仍 **不** 宣称 A2。

| 维度 | 第十八次中场判断 |
|---|---|
| M0–M174 定义内目标 | 仍完成且合同未降级 |
| M175–M184 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | 自研整链合同加深；非完整 g++/gcc |
| 下一主线 | 更大 musl 闭包（忌盲开 strtol）/ 可选严格 RO PT_LOAD |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M175–M184）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M175–M176 | isalpha/memset 种植 | host trunc/缺路径边缘 |
| M177–M178 | mcpy 成功路径、去 io | `m178-mcpy` / DUP / UND |
| M179–M180 | div/movzx/and|or | host `m180-as` / `m180-gccs` |
| M181–M182 | 字段类型名 + chaos `0xC184` | host `m182-typed-fields` |
| **M184** | 整链 musl 表面 | `ONBOX_SURF_PASS` + `WMSG:SF` + `surf: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 真 gcc/g++ 二进制仍不落盘。  
4. `.bss` 名接受但非真 NOBITS。  
5. ET_EXEC 的 `.rodata` 与 `.data` 同属 RW 段；第三 RO `PT_LOAD` 未拆。  
6. 成功路径挂 `slen.o`（无 `io.elf`）；禁止 `io.elf` 与 `slen.o`/`mcpy.o` 同 rsp。

## 4. 下一主线裁定

1. **优先**：继续扩大可链 musl 簇或可选严格 RO 段。  
2. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_SURF_PASS` + chaos `0xC184`；  
3. 无超时刷绿；  
4. 基线推进至 **M184 / v2.36.0**；下一正式中场约 **M194**。
