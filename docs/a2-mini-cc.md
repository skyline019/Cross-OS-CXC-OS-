# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M104 — freestanding C `cxc-cc`：CRT/switch/`?:` + **`enum`、`sizeof`、`<<`/`>>`、十六进制**。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M97 | 赋值；算术/比较；`if`/`while`；`break`/`continue`；`&&`/`\|\|`；一元；数组 | 曾用 CTRL/LANG |
| M98 | `input`/`print`；`write LIT` | 曾用 `ONBOX_IORW_PASS` |
| M99 | `fn` 定义/调用/递归 | 曾用 `ONBOX_FUNC_PASS` |
| M100 | `struct`；`v.f`；`&`/`*` 子集 | 曾用 `ONBOX_DATA_PASS` |
| M101 | `#include "path"`；`for`；`ptr[i]` | 曾用 `ONBOX_INCL_PASS` |
| M102 | `fn main` CRT；`int g=N;`；`"str"`；`do`；`+=`/`++` | 曾用 `ONBOX_CRT_PASS` |
| M103 | `switch`/`case`/`default`；`?:`；`%`；`'c'` | 曾用 `ONBOX_CASE_PASS` |
| **M104（本窗 / 第十次中场）** | `enum { A=N, B };`；`sizeof`；`<<`/`>>`；`0xNN` | `NMSG:42` + `WMSG:EN` + `ONBOX_ENUM_PASS` + `edge: 1` |
| M105+ | musl 真链接 / 多目标；逼近可编译真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。

## 2. 实现形态

- `enum` 常量在定义阶段登记；表达式中作立即数
- `sizeof(int|struct|expr)`：标量 8；struct 为 `nfields*8`
- `<<`/`>>` 介于加减与比较；十六进制 `0x` 词法
- 宿主 `cc_host_test` 覆盖 enum/hex/shift + 重复 enum 名

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-10.md`](midpoint-assessment-10.md) 下一主线一致。
