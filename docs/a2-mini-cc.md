# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M103 — freestanding C `cxc-cc`：CRT/`#include`/struct + **`switch`/`case`、三元 `?:`、`%`、字符常量**。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M97 | 赋值；算术/比较；`if`/`while`；`break`/`continue`；`&&`/`\|\|`；一元；数组 | 曾用 CTRL/LANG |
| M98 | `input`/`print`；`write LIT` | 曾用 `ONBOX_IORW_PASS` |
| M99 | `fn` 定义/调用/递归 | 曾用 `ONBOX_FUNC_PASS` |
| M100 | `struct`；`v.f`；`&`/`*` 子集 | 曾用 `ONBOX_DATA_PASS` |
| M101 | `#include "path"`；`for`；`ptr[i]` | 曾用 `ONBOX_INCL_PASS` |
| M102 | `fn main` CRT；`int g=N;`；`"str"`；`do`；`+=`/`++` | 曾用 `ONBOX_CRT_PASS` |
| **M103（本窗）** | `switch`/`case`/`default`（无贯穿）；`?:`；`%`；`'c'` | `NMSG:42` + `WMSG:CS` + `ONBOX_CASE_PASS` + `edge: 1` |
| M104+ | musl 真链接 / 多目标；逼近可编译真实 C | 对齐 [`product-goals.md`](product-goals.md) A2；约第十次中场 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。

## 2. 实现形态

- `switch`：顺序比较 + `break`；case 之间不贯穿；`continue` 跳过 switch 帧找外层循环
- 三元右结合；`%` 用 `idiv` 余数；`'x'` 词法为立即数
- 宿主 `cc_host_test` 覆盖 case/三元/取模/字符 + 重复 `default`

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-9.md`](midpoint-assessment-9.md) 下一主线一致。
