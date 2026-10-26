# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M99 — freestanding C `cxc-cc`：控制流/数组/I/O + **函数定义与调用（含递归）**。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M97 | 赋值；算术/比较；`if`/`while`；`break`/`continue`；`&&`/`\|\|`；一元；数组 | 曾用 CTRL/LANG |
| M98 | `input`/`print`；`write LIT` | 曾用 `ONBOX_IORW_PASS` |
| **M99（本窗）** | `fn name(a,b){...}`；调用 `name(...)`；嵌套调用；同单元递归；顶层 `return` 仍写 PASS+exit | `NMSG:42` + `WMSG:FN` + `ONBOX_FUNC_PASS` + `edge: 1` |
| M100+ | `struct` 字段；指针算术子集；多文件 | 数据结构/链接合同 |
| 后续 | 与 musl/CRT 链接；逼近可编译真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。

## 2. 实现形态

- 布局：`jmp entry` → 函数体 → `entry` 顶层 prologue+语句
- 调用约定：参数左到右压栈；`call`/`leave`/`ret`；arity ≤4
- 函数须定义在顶层语句之前；宿主 `cc_host_test` 覆盖 arity/未知符号拒绝

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘（arity 不匹配、非法源）纳入合同。  
3. 与 [`midpoint-assessment-9.md`](midpoint-assessment-9.md) 下一主线一致。
