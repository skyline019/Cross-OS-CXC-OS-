# CXC/OS 第二十二次中场评估：M215–M224 进度、证据与下一主线

日期：2026-10-27  
基线：Milestone 224 / v2.44.0  
对照：第二十一次中场（[`midpoint-assessment-21.md`](midpoint-assessment-21.md)）冻结在 **M205–M214**；本评估覆盖 **M215–M223**，并以 **M224** 为本中场窗口（浅 wrapper/叶子 + as mem-walk + 文档/CI 统一）。  
规划参照：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_WALK_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第二十一次中场之后，机上工具链在 LEAF 上**可回归地扩大**浅 musl wrapper/叶子，并加厚 as mem-walk：

1. **浅种**（M215–M218）：`bzero`（UND=`memset`）、`strnlen`（UND=`memchr`）、`isascii`/`llabs`（`nm -u` 空）；
2. **as**（M219–M220）：`movzbl (%rdi|%rsi)`、`add $imm,%rdi|%rsi|%rdx`、`test %rdx`、`cmp %al,(%rdi)`；
3. **混沌**（M221–M222）：种子 `0xC1C4`；
4. **本窗 M224**：整链 → `ONBOX_WALK_PASS` + `WMSG:WK` + `walk: 1`。

仍 **不** 宣称 A2。忌盲开 `strtol`/`strrchr`/`memmove`；不以第三 RO `PT_LOAD` / 紧凑 char 打包为主推。

| 维度 | 第二十二次中场判断 |
|---|---|
| M0–M214 定义内目标 | 仍完成且合同未降级 |
| M215–M224 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | 自研整链合同加深；非完整 g++/gcc |
| 下一主线 | 更大 musl 闭包（忌盲开 strtol）/ 可选严格 RO PT_LOAD |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M215–M224）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M215–M216 | bzero/strnlen + UND 边缘 | host trunc/UND；`m218-walk-plant` |
| M217–M218 | isascii/llabs | host trunc/缺路径 |
| M219–M220 | mem-walk as | host `m220-as` / `m220-gccs` |
| M221–M222 | chaos `0xC1C4` | `cc_chaos_test` PASS |
| **M224** | 整链 walk | `ONBOX_WALK_PASS` + `WMSG:WK` + `walk: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol`/`strrchr`/`memmove` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 真 gcc/g++ 二进制仍不落盘。  
4. `.bss` 名接受但非真 NOBITS。  
5. ET_EXEC 的 `.rodata` 与 `.data` 同属 RW 段；第三 RO `PT_LOAD` 未拆。  
6. 字段偏移仍 pad-to-8；禁止 `io.elf` 与 `slen.o`/`mcpy.o` 同 rsp。

## 4. 下一主线裁定

1. **优先**：继续扩大可链 musl 簇或可选严格 RO 段 / 紧凑 char 访问。  
2. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_WALK_PASS` + chaos `0xC1C4`；  
3. 无超时刷绿；  
4. 基线推进至 **M224 / v2.44.0**；下一正式中场约 **M234**。
