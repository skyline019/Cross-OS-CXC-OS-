# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M100 — freestanding C `cxc-cc`：控制流/数组/I/O/函数 + **struct 字段与指针子集**。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M97 | 赋值；算术/比较；`if`/`while`；`break`/`continue`；`&&`/`\|\|`；一元；数组 | 曾用 CTRL/LANG |
| M98 | `input`/`print`；`write LIT` | 曾用 `ONBOX_IORW_PASS` |
| M99 | `fn` 定义/调用/递归 | 曾用 `ONBOX_FUNC_PASS` |
| **M100（本窗）** | `struct Name { f; };`；`Name v;`；`v.f`；`&v.f` / `&a[i]`；`*p` / `*p=expr` | `NMSG:42` + `WMSG:ST` + `ONBOX_DATA_PASS` + `edge: 1` |
| M101+ | 多文件；与 musl/CRT 链接；逼近可编译真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。

## 2. 实现形态

- 布局：`jmp entry` → `fn`/`struct` 定义 → `entry` 顶层
- struct 实例占连续栈槽；指针为字宽地址；`&`/`*` 子集（无通用指针算术）
- 宿主 `cc_host_test` 覆盖非法字段/解引用

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-9.md`](midpoint-assessment-9.md) 下一主线一致。
