# CXC/OS 第十次中场评估：M95–M104 进度、证据与下一主线

日期：2026-10-26  
基线：Milestone 104 / v1.5.0  
对照：第九次中场（[`midpoint-assessment-9.md`](midpoint-assessment-9.md)）冻结在 **M85–M94**，裁定「表达式/多语句前端」服务 A2；本评估覆盖其后 **M95–M103** 能力切片，并以 **M104** 作为本中场窗口本身。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；mini-cc 路线：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证口径：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；网络合同不回退；机上编译合同至 `ONBOX_ENUM_PASS`（`exit: 42` + `edge: 1`）。QEMU 验收底线 **128 MiB**。

## 1. 结论摘要

第九次中场之后，机上 `cxc-cc` 从「return-N 玩具前端」连续加深到 **CRT 入口、多文件 include、控制流全集、数据布局、枚举/移位/sizeof**，合同只增不减，边缘路径同权。

1. **写串口与表达式**（M95 write → M96 freestanding if/while → M97 break/数组/逻辑）；
2. **I/O 与过程**（M98 input/print → M99 fn/递归 → M100 struct/指针）；
3. **多文件与 CRT 面**（M101 `#include`/`for` → M102 `main`/globals/`do`/`+=` → M103 switch/`?:`/`%`）；
4. **本中场窗口**（M104）：`enum`/`sizeof`/`<<`/`0x` + 评估成文 + CI 同步。

相对第九次中场，质变是：

- 从「单文件顶层语句」进入 **`fn main` CRT + `#include` + 全局 `.data`**；
- 从前端烟测进入 **可回归的语言面**（控制流/数据/枚举），宿主 `cc_host_test` 与 QEMU 同证；
- 仍 **不** 宣称 A2（机上 g++/gcc）；距 musl 真链接仍有明确缺口。

一句话状态：

| 维度 | 第十次中场判断 |
|---|---|
| M0–M94 定义内目标 | 仍完成且合同未降级 |
| M95–M103 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | **实质 C 子集 + CRT 形态**；非完整工具链 |
| 下一主线 | **musl/CRT 真链接或多目标 `.o`**（仍服务 A2） |
| 最大风险 | 假称 A2；或停在自研方言不向真实 C/链接靠拢 |

## 2. 增量盘点（M95–M104）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M95–M97 | write / freestanding / break·数组 | 曾用 WRITE/CTRL/LANG |
| M98–M100 | I/O / fn / struct·指针 | 曾用 IORW/FUNC/DATA |
| M101–M103 | include·for / CRT main / switch·?: | 曾用 INCL/CRT/CASE |
| **M104** | enum / sizeof / << / hex | `ONBOX_ENUM_PASS` + `WMSG:EN` + `enum: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. 无 relocatable `.o` / 外部符号解析；无法链真 musl。  
2. 类型系统仍为「全字宽」；无符号扩展/窄整型。  
3. 协作式调度；非抢占。  
4. 网络合同冻结面未扩大（刻意）。

## 4. 下一主线裁定

1. **优先**：多目标编译 + 与 planted CRT/musl 子集链接（可观测 `main` 调 libc 符号或等价）。  
2. **可选**：更完整 C 声明语法（`typedef`/`const`/多维数组）作为同窗加深。  
3. **禁止**：拆掉通向 A2 的前端；禁止仅靠超时/删断言刷绿。

退出标准（下阶段，仍非 A2）：机上产物能链接并调用至少一类外部运行时符号；`PASS` + `peer_ok=1` 不回退。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_ENUM_PASS`；  
3. 无超时刷绿；  
4. 基线推进至 **M104 / v1.5.0**；下一正式中场约 **M114**。
