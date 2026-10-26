# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M133 — **整链合流**：as→`.o` + cc→`.o` + ld→ET_EXEC + elflink 多节 musl（含 `mcmp.o`）。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M130 | ld 链 musl string + io | 曾用 `ONBOX_LDLIB_PASS` |
| M131 | as 延迟 lea + 指令/.data | 曾用 `ONBOX_ASDP_PASS` |
| M132 | `char` 截断 + `const` | 曾用 `ONBOX_CHAR_PASS` |
| **M133（本窗）** | as+cc+ld+musl 合流 | `NMSG:42` + `WMSG:MX` + `ONBOX_MERG_PASS` + `edge: 1` |
| M134 | 第十三次中场 + 混沌套件 | 见 cadence |

**未宣称 A2**。

## 2. 实现形态

- `lib.c`→cc ET_REL；`main.s`→as ET_REL；`link.rsp`→ld（含 musl string + `mcmp.o` + `io.elf`）
- host 边缘：DUP、坏 `e_type`、截断 ELF、未解析 UND / 乱序 `#lib`
- 未知重定位类型仍明确失败（不静默）

## 3. 纪律

大跨步；边缘同权；不拆通向 A2 的前端。
