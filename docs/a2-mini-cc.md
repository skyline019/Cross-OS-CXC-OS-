# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M102 — freestanding C `cxc-cc`：既有控制流/I/O/fn/struct/include/for + **CRT `main`、globals、字符串、`do`/`+=`/`++`**。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M97 | 赋值；算术/比较；`if`/`while`；`break`/`continue`；`&&`/`\|\|`；一元；数组 | 曾用 CTRL/LANG |
| M98 | `input`/`print`；`write LIT` | 曾用 `ONBOX_IORW_PASS` |
| M99 | `fn` 定义/调用/递归 | 曾用 `ONBOX_FUNC_PASS` |
| M100 | `struct`；`v.f`；`&`/`*` 子集 | 曾用 `ONBOX_DATA_PASS` |
| M101 | `#include "path"`；`for`；`ptr[i]` | 曾用 `ONBOX_INCL_PASS` |
| **M102（本窗）** | `fn main` CRT 入口；`int g=N;` 全局；`"str"`；`do`/`while`；`+=`/`++`/`--` | `NMSG:42` + `WMSG:CR` + `ONBOX_CRT_PASS` + `edge: 1` |
| M103+ | 与 musl 真链接 / 多目标文件；逼近可编译真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。

## 2. 实现形态

- 有 `fn main(){...}` 时：`_start` → call main → `ONBOX_*_PASS` → exit(eax)
- 无 `main` 时：保持顶层语句入口（宿主回归兼容）
- 全局：`int name[=N];` 落在 `.data`（与 write 字符串同段 RIP-relative）
- `write "CR"` 与 `write CR` 均可；`int` 可作局部声明噪声
- 宿主 `cc_host_test` 覆盖 CRT/复合赋值 + 非法 main 元数/重复全局

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-9.md`](midpoint-assessment-9.md) 下一主线一致。
