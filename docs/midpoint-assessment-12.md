# CXC/OS 第十二次中场评估：M115–M124 进度、证据与下一主线

日期：2026-10-26  
基线：Milestone 124 / v2.15.0  
对照：第十一次中场（[`midpoint-assessment-11.md`](midpoint-assessment-11.md)）冻结在 **M105–M114**，裁定「链真 musl/gcc 或补 `.rela`」；本评估覆盖 **M115–M123**，并以 **M124** 为本中场窗口。  
规划参照：[`phase1-assessment-roadmap.md`](phase1-assessment-roadmap.md)；mini-cc：[`a2-mini-cc.md`](a2-mini-cc.md)；节奏：[`midfield-cadence.md`](midfield-cadence.md)；产品目标：[`product-goals.md`](product-goals.md)。  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_SCPY_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 1. 结论摘要

第十一次中场之后，机上 `cxc-cc` 从「自产 SysV `.o` + C 声明子集」推进到 **可吞真实宿主/musl ET_REL，并自产 gcc 风格重定位**：

1. **宿主 gcc 对象**（M115–M118：单/双 `#lib`、`.rodata`、GOTPCREL）；
2. **真实 musl `.lo`**（M119–M121：labs / 多节 isdigit / mem·str 三联）；
3. **自产重定位**（M122 `.rela.text` PLT32；M123 `.rodata` + PC32）；
4. **本窗 M124**：跨 `#lib` 解析 musl `strcpy`→`__stpcpy` UND，并同链 `strncmp` + 自产字面量；第十二次中场成文。

相对第十一次中场，质变是：

- 链接器面对的是 **真实 musl/gcc ABI 对象**，不再只靠种植的玩具 `.o`；
- 自产 ET_REL 已带 **`.rela.text` + `.rodata`**，形态靠近 `gcc -c`；
- 仍 **不** 宣称 A2（机上无完整 g++/gcc 工具链）。

| 维度 | 第十二次中场判断 |
|---|---|
| M0–M114 定义内目标 | 仍完成且合同未降级 |
| M115–M123 增量 | 完成且有串口 + peer 证据 |
| A2 就绪度 | **真 musl 跨对象 UND + 自产 rela/rodata**；非完整工具链 |
| 下一主线 | **更大 musl 簇（如 strtol 依赖闭包）/ 机上 as+ld 子集 / 窄类型**（仍服务 A2） |
| 最大风险 | 假称 A2；或停在「能链几个 `.lo`」而不向可自举工具链收敛 |

## 2. 增量盘点（M115–M124）

| 里程碑 | 能力 | 证据 |
|---|---|---|
| M115–M118 | host-gcc ET_REL / UND / 双库 / GOTPCREL | 曾用 HOBJ…GOT |
| M119–M121 | musl labs / isdigit 多节 / mem·str | 曾用 MLAB…MMEM |
| M122 | 自产 `.rela.text` + musl `atoi` | 曾用 `ONBOX_ATOI_PASS` |
| M123 | 自产 `.rodata`/PC32 + musl `strcmp` | 曾用 `ONBOX_RDAT_PASS` |
| **M124** | `strcpy`→`__stpcpy` 跨库 + `strncmp` | `ONBOX_SCPY_PASS` + `WMSG:CP` + `scpy: 1` + `edge: 1` |

## 3. 债项（不阻塞本窗 PASS）

1. `strtol` 等依赖 `__intscan`/`__shgetc`/errno/stack_chk，尚未成闭包。  
2. 类型仍全字宽；无 `char`/`short`/符号扩展。  
3. 协作式调度；非抢占。  
4. 网络合同冻结面未扩大（刻意）。  
5. 自产对象仍无完整 DWARF/`eh_frame`；不追求与任意 `-c` 比特级一致。

## 4. 下一主线裁定

1. **优先**：组装更大 musl 依赖闭包（或机上汇编/链接子集），合同可回归。  
2. **可选**：窄整型 / `const` / 自产 ET_REL 发 UND。  
3. **禁止**：拆掉通向 A2 的前端；禁止超时/删断言刷绿；禁止宣称 A2。

退出标准（下阶段，仍非 A2）：机上能链接并运行一类 **多依赖真实 musl 簇**（或等价 as+ld 证据）；`PASS` + `peer_ok=1` 不回退。

## 5. 节奏契约兑现

1. 本文评估成文；  
2. CI / `contract_test.py` 同步到 `ONBOX_SCPY_PASS`；  
3. 无超时刷绿；  
4. 基线推进至 **M124 / v2.15.0**；下一正式中场约 **M134**。
