# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M110 — 表达式语句调用 + planted **`io.elf`/`cwrite`**（SysV write）+ 字符串转义 + 跨 `#lib` UND。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M104 | 控制流 / I/O / fn / struct / include / CRT / switch / enum | 曾用各 ONBOX_* |
| M105–M108 | `#link` / `#lib` / 多符号 / 节内 PC32 | 曾用 LINK/LIB/MATH/PC32 |
| M109 | 跨 `#lib` UND；`DMSG` 边缘 | 曾用 `ONBOX_XLIB_PASS` |
| **M110（本窗）** | `call(...);` 语句；`#lib io.elf`/`cwrite`；`"...\n"` 转义 | `NMSG:42` + `WMSG:IO` + `DMSG:21` + `ONBOX_LIBC_PASS` + `edge: 1` |
| M111+ | 更多 musl 符号（`strlen`/`memcpy`…）；声明靠拢真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链 / 动态链接 / 完整 C 类型系统。

## 2. 实现形态

- 支持 `name(...);` 表达式语句（无赋值丢弃返回值）
- 字符串字面量指针 + C 风格转义；`cwrite` 在独立 `io.elf`
- 跨库 `ans`→`twice` 与 `DMSG` 调试兜底仍保留

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同（含调试标记）。  
3. 与 [`midpoint-assessment-10.md`](midpoint-assessment-10.md) 下一主线一致。
