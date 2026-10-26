# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M134 — **第十三次中场**：整链合流合同锁定 + 固定种子 host 混沌套件。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M130 | ld 链 musl string + io | 曾用 `ONBOX_LDLIB_PASS` |
| M131 | as 延迟 lea + 指令/.data | 曾用 `ONBOX_ASDP_PASS` |
| M132 | `char` 截断 + `const` | 曾用 `ONBOX_CHAR_PASS` |
| M133 | as+cc+ld+musl 合流 | `NMSG:42` + `WMSG:MX` + `ONBOX_MERG_PASS` + `edge: 1` |
| **M134（本窗）** | 中场统一 + host chaos | 同上机上合同 + `cc_chaos_test: PASS` |

**未宣称 A2**。详见 [`midpoint-assessment-13.md`](midpoint-assessment-13.md)。

## 2. 实现形态

- 成功路径：`lib.c`→cc、`main.s`→as、`link.rsp`→ld（musl string + `mcmp.o` + `io.elf`）
- host 边缘：非法 asm/C、DUP、坏 `e_type`、截断 ELF、未解析 UND
- host 混沌：`tools/cc_chaos_test.c` 固定种子截断/翻转 `.c`/`.s`/`.o`/rsp 行 → 无 panic、可复现拒绝

## 3. 纪律

大跨步；边缘与混沌同权；不拆通向 A2 的前端；不删断言刷绿。
