# CXC/OS 第二十次中场评估：M195–M204 进度、证据与下一主线

日期：2026-10-27  
基线：Milestone 204 / v2.40.0  
对照：第十九次中场（[`midpoint-assessment-19.md`](midpoint-assessment-19.md)）冻结在 **M185–M194**；本评估覆盖 **M195–M203**，并以 **M204** 为本中场窗口（闭包 ctype 叶子 + as 加厚 + 文档/CI 统一）。  
规划参照：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_CTYP_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十九次中场之后，机上工具链在 WIDE 上**可回归地扩大**闭包 ctype 叶子，并加厚 as 以吃 musl/gcc 常见形态：

1. **ctype 叶子**（M195–M198）：`isupper`/`isblank`/`isgraph`/`isprint`（`nm -u` 空）；
2. **as**（M199–M200）：忽略 `endbr64`；`sete`/`setbe`；`cmp $imm,%edi|%esi`；`jbe`/`ja`；
3. **混沌**（M201–M202）：种子 `0xC1A4`；
4. **本窗 M204**：整链 → `ONBOX_CTYP_PASS` + `WMSG:CT` + `ctyp: 1`。

仍 **不** 宣称 A2。忌盲开 `strtol`；不以第三 RO `PT_LOAD` 为主推。

| 维度 | 第二十次中场判断 |
|---|---|
| M0–M194 定义内目标 | 仍完成且合同未降级 |
| M195–M204 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | 自研整链合同加深；非完整 g++/gcc |
| 下一主线 | 更大 musl 闭包（忌盲开 strtol）/ 可选严格 RO PT_LOAD |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M195–M204）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M195–M196 | isupper/isblank 种植+真调 | host trunc/缺路径；`m198-ctype-plant` |
| M197–M198 | isgraph/isprint 种植+真调 | host trunc；探针算 42 |
| M199–M200 | endbr64/sete/cmp edi/jbe | host `m200-as` / `m200-gccs` |
| M201–M202 | chaos `0xC1A4` | `cc_chaos_test` PASS |
| **M204** | 整链 ctype | `ONBOX_CTYP_PASS` + `WMSG:CT` + `ctyp: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 真 gcc/g++ 二进制仍不落盘。  
4. `.bss` 名接受但非真 NOBITS。  
5. ET_EXEC 的 `.rodata` 与 `.data` 同属 RW 段；第三 RO `PT_LOAD` 未拆。  
6. 字段偏移仍 pad-to-8（未做紧凑 char 打包访问）；禁止 `io.elf` 与 `slen.o`/`mcpy.o` 同 rsp。

## 4. 下一主线裁定

1. **优先**：继续扩大可链 musl 簇（如 `ispunct`/`isxdigit`）或可选严格 RO 段。  
2. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_CTYP_PASS` + chaos `0xC1A4`；  
3. 无超时刷绿；  
4. 基线推进至 **M204 / v2.40.0**；下一正式中场约 **M214**。
