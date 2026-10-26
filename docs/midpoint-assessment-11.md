# CXC/OS 第十一次中场评估：M105–M114 进度、证据与下一主线

日期：2026-10-26  
基线：Milestone 114 / v2.5.0  
对照：第十次中场（[`midpoint-assessment-10.md`](midpoint-assessment-10.md)）冻结在 **M95–M104**，裁定「musl/CRT 真链接或多目标 `.o`」；本评估覆盖其后 **M105–M113**，并以 **M114** 为本中场窗口。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；mini-cc：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_CFN_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十次中场之后，机上 `cxc-cc` 从「单 TU 方言」推进到 **可链接对象 + 靠拢真实 C 声明**：

1. **多 TU / 库面**（M105 `#link` → M106–M108 `#lib`/多符号/PC32）；
2. **跨库与 CRT plant**（M109 UND+`DMSG` → M110 call-stmt/`cwrite` → M111–M112 字符串/内存/`typedef`）；
3. **自产 ET_REL**（M113：`lib.c`→SysV `lib.o`→`#lib`）；
4. **本窗 M114**：C 风格 `int f(int x)` / `void` + 多函数对象内 SysV 互调 + 中场成文。

相对第十次中场，质变是：

- 从「只能链种植的 ET_REL」进入 **机上自产可重定位目标**；
- 从纯 `fn` 方言进入 **可识别的 C 函数声明子集**；
- 仍 **不** 宣称 A2（机上 g++/gcc）。

| 维度 | 第十一次中场判断 |
|---|---|
| M0–M104 定义内目标 | 仍完成且合同未降级 |
| M105–M113 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | **自产 `.o` + C 声明子集**；非完整工具链 |
| 下一主线 | **链真 musl/gcc 产物或补 `.rela`/窄类型**（仍服务 A2） |
| 最大风险 | 假称 A2；或停在自研对象格式不向宿主 ABI 收敛 |

## 2. 增量盘点（M105–M114）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M105–M108 | `#link` / `#lib` / math / PC32 | 曾用 LINK/LIB/MATH/PC32 |
| M109–M112 | UND+`DMSG` / cwrite / cstr / memset·typedef | 曾用 XLIB…MUSL |
| M113 | 自产 ET_REL SysV | 曾用 `ONBOX_OBJ_PASS` |
| **M114** | C-decl + 多函数 `.o` 互调 | `ONBOX_CFN_PASS` + `WMSG:CF` + `cfn: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. 自产 ET_REL 尚无 `.rela.text`（同 TU 调用在发射前已拍死）；难直接吞任意 gcc `-c` 产物若依赖未解析重定位组合。  
2. 类型仍全字宽；无 `char`/`short`/符号扩展。  
3. 协作式调度；非抢占。  
4. 网络合同冻结面未扩大（刻意）。

## 4. 下一主线裁定

1. **优先**：导入真实 musl/gcc ET_REL（或补齐自产 `.rela` + 未定义符号延迟绑定）并有可回归合同。  
2. **可选**：窄整型 / `const` / 更多声明语法。  
3. **禁止**：拆掉通向 A2 的前端；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上能链接并运行至少一类 **非自产种植** 的标准 ABI 对象（或等价证据）；`PASS` + `peer_ok=1` 不回退。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_CFN_PASS`；  
3. 无超时刷绿；  
4. 基线推进至 **M114 / v2.5.0**；下一正式中场约 **M124**。
