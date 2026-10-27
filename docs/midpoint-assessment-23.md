# CXC/OS 第二十三次中场评估：M225–M234 进度、证据与下一主线

日期：2026-10-27  
基线：Milestone 234 / v2.46.0  
对照：第二十二次中场（[`midpoint-assessment-22.md`](midpoint-assessment-22.md)）冻结在 **M215–M224**；本评估覆盖 **M225–M233**，并以 **M234** 为本中场窗口（浅 wrapper/叶子 + as 加厚 + 文档/CI 统一）。  
规划参照：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_WRAP_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第二十二次中场之后，机上工具链在 WALK 上**可回归地扩大**浅 musl wrapper/叶子，并加厚 as：

1. **浅种**（M225–M228）：`toascii`/`imaxabs`/`ffs`（`nm -u` 空）；`bcmp`（UND=`memcmp`）；
2. **as**（M229–M230）：`inc`/`dec %rdi`；`sub $imm,%rdx`；`seta`/`setb`→`%cl`；
3. **混沌**（M231–M232）：种子 `0xC1D4`；
4. **本窗 M234**：整链 → `ONBOX_WRAP_PASS` + `WMSG:WR` + `wrap: 1`。

仍 **不** 宣称 A2。忌盲开 `strtol`/`strrchr`/`memmove`；不以第三 RO `PT_LOAD` / 紧凑 char / `strcspn` 深串扫描为主推。

| 维度 | 第二十三次中场判断 |
|---|---|
| M0–M224 定义内目标 | 仍完成且合同未降级 |
| M225–M234 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | 自研整链合同加深；非完整 g++/gcc |
| 下一主线 | 更大 musl 闭包（忌盲开 strtol）/ 可选严格 RO PT_LOAD |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M225–M234）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M225–M226 | toascii/imaxabs 种植+真调 | host trunc/缺路径 |
| M227–M228 | bcmp/ffs + bcmp UND | host `m228-wrap-plant` |
| M229–M230 | inc/sub rdx/seta %cl | host `m230-as` / `m230-gccs` |
| M231–M232 | chaos `0xC1D4` | `cc_chaos_test` PASS |
| **M234** | 整链 wrap | `ONBOX_WRAP_PASS` + `WMSG:WR` + `wrap: 1` + `edge: 1` |

## 3. 本窗根因修补

- 探针 `cmp $imm,%rdx` 与 `sub $imm,%rdx` 配对（as）。
- cxcfs `max_files` **64→96**（种植 + `main.o`/`out.elf` + 前序探针残片）；`CC_LINK_MAX` **40→48**。

## 4. 债项（不阻塞本窗 PASS）

1. `strtol`/`strrchr`/`memmove`/`strcspn` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 真 gcc/g++ 二进制仍不落盘。  
4. `.bss` 名接受但非真 NOBITS。  
5. ET_EXEC 的 `.rodata` 与 `.data` 同属 RW 段；第三 RO `PT_LOAD` 未拆。  
6. 字段偏移仍 pad-to-8；禁止 `io.elf` 与 `slen.o`/`mcpy.o` 同 rsp。

## 5. 下一主线裁定

1. **优先**：继续扩大可链 musl 簇（如 `strcspn`/`strpbrk` 若 `nm` 门禁通过）或可选严格 RO 段 / 紧凑 char。  
2. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退。

## 6. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_WRAP_PASS` + chaos `0xC1D4`；  
3. 无超时刷绿；  
4. 基线推进至 **M234 / v2.46.0**；下一正式中场约 **M244**。
