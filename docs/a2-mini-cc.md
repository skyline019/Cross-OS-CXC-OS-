# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M112 — planted **`memset`/`memcmp`/`strlen`/`memcpy`/`cwrite`** + `typedef` + 跨 `#lib` UND + `DMSG`/`DHINT` 调试兜底。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M104 | 控制流 / I/O / fn / struct / include / CRT / switch / enum | 曾用各 ONBOX_* |
| M105–M108 | `#link` / `#lib` / 多符号 / 节内 PC32 | 曾用 LINK/LIB/MATH/PC32 |
| M109 | 跨 `#lib` UND；`DMSG` 边缘 | 曾用 `ONBOX_XLIB_PASS` |
| M110 | `call(...);`；`io.elf`/`cwrite`；`"...\n"` 转义 | 曾用 `ONBOX_LIBC_PASS` |
| M111 | `strlen`/`memcpy` planted；`cwrite(…, strlen(…))` | 曾用 `ONBOX_CSTR_PASS` |
| **M112（本窗）** | `memset`/`memcmp`；`typedef int`；`DMSG`+`DHINT` | `NMSG:42` + `WMSG:MS` + `DMSG:21` + `DHINT:UND` + `ONBOX_MUSL_PASS` + `edge: 1` |
| M113+ | 更多 musl / 声明靠拢真实 C → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链 / 动态链接 / 完整 C 类型系统。

## 2. 实现形态

- `io.elf`：`cwrite` / `strlen` / `memcpy`（SysV）
- `strlen` 结果可作 `cwrite` 长度实参；`memcpy` 按字节拷贝
- 跨库 `ans`→`twice` 与 `DMSG` 调试兜底仍保留

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同（含调试标记）。  
3. 与 [`midpoint-assessment-10.md`](midpoint-assessment-10.md) 下一主线一致。
