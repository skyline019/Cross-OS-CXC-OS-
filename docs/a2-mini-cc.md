# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M101 — freestanding C `cxc-cc`：控制流/数组/I/O/函数/struct/指针 + **`#include` / `for` / `ptr[i]`**。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M97 | 赋值；算术/比较；`if`/`while`；`break`/`continue`；`&&`/`\|\|`；一元；数组 | 曾用 CTRL/LANG |
| M98 | `input`/`print`；`write LIT` | 曾用 `ONBOX_IORW_PASS` |
| M99 | `fn` 定义/调用/递归 | 曾用 `ONBOX_FUNC_PASS` |
| M100 | `struct`；`v.f`；`&`/`*` 子集 | 曾用 `ONBOX_DATA_PASS` |
| **M101（本窗）** | `#include "path"`（`disk/`）；`for`；`ptr[i]` 字宽索引 | `NMSG:42` + `WMSG:IN` + `ONBOX_INCL_PASS` + `edge: 1` |
| M102+ | 与 musl/CRT 链接；逼近可编译真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。

## 2. 实现形态

- 布局：`jmp entry` → `fn`/`struct` 定义 → `entry` 顶层
- `#include` 在词法前展开（深度上限 4）；相对路径默认 `disk/`
- `for(init;cond;step)`：step 词法保存后于 body 之后发射；`continue` 落到 step
- struct 实例占连续栈槽；指针为字宽地址；`ptr[i]` 为 `*(ptr + i)`（8 字节步长）
- 宿主 `cc_host_test` 覆盖 for/include/ptr 索引 + 非法 include/字段/解引用

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-9.md`](midpoint-assessment-9.md) 下一主线一致。
