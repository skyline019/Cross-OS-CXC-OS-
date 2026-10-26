# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M108 — freestanding C `cxc-cc`：`#lib` 整节 `.text` 并入 + **`R_X86_64_PC32`**（库内 `twice`→`add`）+ SysV 多符号。

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
| M106 | `import fn`；`#lib "rt.elf"` 单符号 ET_REL（SysV） | 曾用 `ONBOX_LIB_PASS` |
| M107 | 同库多符号 `add`/`mul`/`mac`；SysV 至 4 参 | 曾用 `ONBOX_MATH_PASS` |
| **M108（本窗）** | 整节 `.text` + `R_X86_64_PC32`；`twice` 库内 call `add`；组合 `mac` | `NMSG:42` + `WMSG:PC` + `ONBOX_PC32_PASS` + `edge: 1` |
| M109+ | 更多 planted musl/CRT；跨目标未定义符号解析 | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。`#lib` 现为「整节并入 + 节内 PC32」，仍非完整 ELF 链接器（无 `R_X86_64_64`/GOT/PLT、无跨 `.o` 未定义解析）。

## 2. 实现形态

- `#include` 仍为源码展开；`#link` **不**展开，单独词法/解析后并入同一 ELF
- `extern fn` 走栈 ABI；`import fn` 走 SysV（最多四参）
- `#lib`：若任一 `import` 命中 ET_REL 的 GLOBAL FUNC，则拷贝整节可执行 `.text`，应用 `SHT_RELA` 中的 `R_X86_64_PC32`，再绑定入口
- 未支持的重定位类型或节外符号 → 编译失败
- 宿主 `cc_host_test` 覆盖 math 回归、`twice` PC32、组合调用、缺库/元数错误

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-10.md`](midpoint-assessment-10.md) 下一主线一致。
