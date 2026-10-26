# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M114 — **C 风格函数声明**（`int f(int x)` / `void`）+ 自产多函数 ET_REL 内 SysV 互调 + `DMSG`/`DHINT`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M104 | 控制流 / I/O / fn / struct / include / CRT / switch / enum | 曾用各 ONBOX_* |
| M105–M108 | `#link` / `#lib` / 多符号 / 节内 PC32 | 曾用 LINK/LIB/MATH/PC32 |
| M109–M112 | 跨库 UND / cwrite / strlen·memcpy / memset·memcmp·typedef | 曾用 XLIB…MUSL |
| M113 | 自产 ET_REL + SysV `#lib` | 曾用 `ONBOX_OBJ_PASS` |
| **M114（本窗 / 第十一次中场）** | C 声明 `int`/`void`；多函数 `.o` 内互调 | `NMSG:42` + `WMSG:CF` + `DMSG:21` + `DHINT:UND` + `ONBOX_CFN_PASS` + `edge: 1` |
| M115+ | 真 musl `.o` 链接 / 完整类型 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链 / 动态链接 / 完整 C 类型系统。

## 2. 实现形态

- `int name(int a, …){…}` / `int main(void)`；`fn` 方言仍保留
- 对象模式：多 GLOBAL FUNC；同 TU SysV 互调后写入 ET_REL
- 驱动：`disk/lib.c`→`lib.o`，再 `in.c`→`out.elf`

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同（含调试标记）。  
3. 见 [`midpoint-assessment-11.md`](midpoint-assessment-11.md)。
