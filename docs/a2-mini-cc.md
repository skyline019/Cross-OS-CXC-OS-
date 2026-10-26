# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M106 — freestanding C `cxc-cc`：CRT/enum/switch + `#link`/`extern` + **`#lib` planted ET_REL + `import fn` SysV 合并**。

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
| M104 | `enum`；`sizeof`；`<<`/`>>`；`0xNN` | 曾用 `ONBOX_ENUM_PASS` |
| M105 | `extern fn`；`#link "tu"` 第二翻译单元合并；未解析符号失败 | 曾用 `ONBOX_LINK_PASS` |
| **M106（本窗）** | `import fn`；`#lib "rt.elf"` 合并 planted ET_REL（SysV `rdi`/`rsi`）；缺库/未解析失败 | `NMSG:42` + `WMSG:LB` + `ONBOX_LIB_PASS` + `edge: 1` |
| M107+ | 更多 planted musl/CRT 符号；逼近可编译真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。`#link` 是自研多 TU 合并；`#lib` 只抽取 ET_REL 的 GLOBAL FUNC 机器码并入同一映像，不是完整 ELF 链接器。

## 2. 实现形态

- `#include` 仍为源码展开；`#link` **不**展开，单独词法/解析后并入同一 ELF
- `extern fn` 走栈 ABI；`import fn` 走 SysV（最多两参：`rdi`/`rsi`）
- `#lib "rt.elf"` 读 planted ET_REL，按符号名把 FUNC 字节拷进代码缓冲并回填 `call rel32`
- 任一 `extern`/`import` 未定义，或 `#lib` 读失败 → 编译失败（边缘合同）
- 宿主 `cc_host_test` 覆盖 link TU、`#lib`/`import`、未解析 extern、缺库

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-10.md`](midpoint-assessment-10.md) 下一主线一致。
