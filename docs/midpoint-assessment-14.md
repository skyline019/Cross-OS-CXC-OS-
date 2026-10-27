# CXC/OS 第十四次中场评估：M135–M144 进度、证据与下一主线

日期：2026-10-26  
基线：Milestone 144 / v2.28.0  
对照：第十三次中场（[`midpoint-assessment-13.md`](midpoint-assessment-13.md)）冻结在 **M125–M134**；本评估覆盖 **M135–M143**，并以 **M144** 为本中场窗口（整链压力 + 文档/CI 统一）。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；mini-cc：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_WHOL_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十三次中场之后，机上工具链从 MERG 合流推进到 **可回归扩大的 musl 闭包 + as/前端加深 + 链接混沌同权**，并以整链探针收束：

1. **musl kit 扩大**（M135–M136）：`atoi`/`strcmp`/`labs`/`isdigit` 进 MERG；strlen 仍单源 `io.elf`；
2. **as gas 加深**（M137–M138）：`jmp`/`jcc`/`test`/`and`/`.quad`/mem + gcc -S 噪声忽略；
3. **前端 `long` + `->`**（M139–M140）：指针成员存取；非法 `->`/解引用边缘；
4. **elflink/混沌加厚**（M141–M142）：未知 reloc / 坏类型 / 截断 / 多节头破坏；
5. **本窗 M144**：单一探针同时锻炼 as+cc+ld+musl+新前端 → `ONBOX_WHOL_PASS`。

相对第十三次中场，质变是：

- 机上成功路径覆盖 **扩大 musl 调用面** 与 **前端结构体指针语法**；
- host **边缘 ≥ 成功** 且混沌种子升级进 CI；
- 仍 **不** 宣称 A2。

| 维度 | 第十四次中场判断 |
|---|---|
| M0–M134 定义内目标 | 仍完成且合同未降级 |
| M135–M144 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | 自研整链合同加深；非完整 g++/gcc |
| 下一主线 | 更大 musl 闭包（忌盲开 strtol）/ 诊断与容量 |
| 最大风险 | 假称 A2；或削弱边缘/混沌刷绿 |

## 2. 增量盘点（M135–M144）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M135–M136 | musl kit 扩大 | 曾用 `ONBOX_MKIT_PASS` |
| M137–M138 | as gas 加深 | 曾用 `ONBOX_AGAS_PASS` |
| M139–M140 | `long` + `->` | host `m140-*` |
| M141–M142 | reloc/混沌加厚 | host `m142-*` + `cc_chaos_test` seed `0xC142` |
| **M144** | 整链压力 | `ONBOX_WHOL_PASS` + `WMSG:WH` + `whol: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol` 等依赖未齐，不开深闭包。  
2. 无完整 DWARF/`eh_frame` 消费。  
3. 协作式调度；非抢占。  
4. 网络合同冻结面未扩大（刻意）。  
5. 真 gcc/g++ 二进制仍不落盘。

## 4. 下一主线裁定

1. **优先**：在不宣称 A2 的前提下继续扩大可链 musl 簇或加深诊断合同。  
2. **可选**：更多重定位类型；链接器容量。  
3. **禁止**：拆掉 as/ld/`#lib`/延迟 UND；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上工具链合同只增不减；`PASS` + `peer_ok=1` 不回退；混沌/边缘继续同权。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_WHOL_PASS` + host chaos；  
3. 无超时刷绿；  
4. 基线推进至 **M144 / v2.28.0**；下一正式中场约 **M154**。
