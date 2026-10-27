# CXC/OS 第十六次中场评估：M155–M164 进度、证据与下一主线

日期：2026-10-27  
基线：Milestone 164 / v2.32.0  
对照：第十五次中场（[`midpoint-assessment-15.md`](midpoint-assessment-15.md)）冻结在 **M145–M154**；本评估覆盖 **M155–M163**，并以 **M164** 为本中场窗口（整链 musl 闭包压力 + 文档/CI 统一）。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；mini-cc：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_MCLS_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十五次中场之后，机上工具链在 DEEP 合流上**可回归地扩大** 可链 musl 调用面、as 对 gcc -S 裁剪面、前端 `void*`/`unsigned`，并以整链探针收束为 `ONBOX_MCLS_PASS`：

1. **musl 浅闭包**（M155–M156）：种植并真调 `memchr`/`tolower`；`WMSG:MC`；
2. **as gcc -S 面**（M157–M158）：`lea disp(%rbp)`、`idiv`/`cdq`、更宽 `xor`/`mov`、`movq %rsp,%rbp`；
3. **前端**（M159–M160）：`void*` 局部、`sizeof(void*)`、`unsigned`；
4. **ld/混沌**（M161–M162）：`.rodata` 与 `.data` 同 RW 写清合同；混沌种子 `0xC164`（含 phdr 破坏）；
5. **本窗 M164**：单一探针同时锻炼 as+前端+musl+ld → `ONBOX_MCLS_PASS`。

相对第十五次中场，质变是：

- 机上成功路径覆盖 **新种植 musl** 与 **栈帧/lea(%rbp)**；
- PASS 标签从 `ONBOX_DEEP_PASS` **加深为** `ONBOX_MCLS_PASS`（非 A2）；
- 仍 **不** 宣称 A2。

| 维度 | 第十六次中场判断 |
|---|---|
| M0–M154 定义内目标 | 仍完成且合同未降级 |
| M155–M164 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | 自研整链合同加深；非完整 g++/gcc |
| 下一主线 | 更大 musl 闭包（忌盲开 strtol）/ 可选严格 RO PT_LOAD |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M155–M164）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M155–M156 | `memchr`/`tolower` 种植+真调 | `ONBOX_MCLS_PASS`；host trunc/UND 边缘 |
| M157–M158 | `lea(%rbp)` / `idiv` / 宽 xor-mov | host `m158-as` / `m158-gccs` |
| M159–M160 | `void*` / `unsigned` | host `m160-voidptr` + 类型边缘 |
| M161–M162 | RO 同页合同 + 混沌 `0xC164` | host `m162-*` + `cc_chaos_test` |
| **M164** | 整链 musl 闭包压力 | `ONBOX_MCLS_PASS` + `WMSG:MC` + `mcls: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 协作式调度；非抢占。  
4. 网络合同冻结面未扩大（刻意）。  
5. 真 gcc/g++ 二进制仍不落盘。  
6. `.bss` 名接受但非真 NOBITS。  
7. ET_EXEC 的 `.rodata` 与 `.data` 同属 RW 段（本窗合同：不拆第三 RO `PT_LOAD`）。

## 4. 下一主线裁定

1. **优先**：在不宣称 A2 的前提下继续扩大可链 musl 簇或可选严格 RO 段。  
2. **可选**：更多重定位类型；链接器容量。  
3. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退；混沌/边缘继续同权。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_MCLS_PASS` + host chaos `0xC164`；  
3. 无超时刷绿；  
4. 基线推进至 **M164 / v2.32.0**；下一正式中场约 **M174**。
