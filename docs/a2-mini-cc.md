# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M113 — 机上自产 **ET_REL**（`disk/lib.c`→`lib.o`，SysV）+ `#lib` 链接 + `DMSG`/`DHINT`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M104 | 控制流 / I/O / fn / struct / include / CRT / switch / enum | 曾用各 ONBOX_* |
| M105–M108 | `#link` / `#lib` / 多符号 / 节内 PC32 | 曾用 LINK/LIB/MATH/PC32 |
| M109 | 跨 `#lib` UND；`DMSG` 边缘 | 曾用 `ONBOX_XLIB_PASS` |
| M110 | `call(...);`；`io.elf`/`cwrite`；`"...\n"` 转义 | 曾用 `ONBOX_LIBC_PASS` |
| M111 | `strlen`/`memcpy` planted；`cwrite(…, strlen(…))` | 曾用 `ONBOX_CSTR_PASS` |
| M112 | `memset`/`memcmp`；`typedef int`；`DMSG`+`DHINT` | 曾用 `ONBOX_MUSL_PASS` |
| **M113（本窗）** | 自产 ET_REL + SysV `#lib`；`lib.c`→`lib.o`→exec | `NMSG:42` + `WMSG:OB` + `DMSG:21` + `DHINT:UND` + `ONBOX_OBJ_PASS` + `edge: 1` |
| M114+ | 更多真实 C / 真 musl 链接 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链 / 动态链接 / 完整 C 类型系统。

## 2. 实现形态

- `cc_compile_obj`：无 `main` 的 TU → ET_REL（`.text` + GLOBAL FUNC）
- 对象函数用 **SysV** 序言，与 `import fn` / planted `#lib` 一致
- 驱动：若存在 `disk/lib.c` 先写出 `disk/lib.o`，再编 `in.c`→`out.elf`

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同（含调试标记）。  
3. 与 [`midpoint-assessment-10.md`](midpoint-assessment-10.md) 下一主线（多目标 / 真链接感）一致。
