# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M131 — **as 加深**：延迟 `lea`、`.data`/`.bss`、`cmp`/`je`/`push`/`pop`；ld 仍链 musl string+io。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M128 | 多文件 as + `.rodata`/`lea` | 曾用 `ONBOX_ASRO_PASS` |
| M129 | mini-ld ET_REL→ET_EXEC | 曾用 `ONBOX_LD_PASS` |
| M130 | ld 链 musl string + io | 曾用 `ONBOX_LDLIB_PASS` |
| **M131（本窗）** | as 延迟 lea + 指令/.data 子集 | `NMSG:42` + `WMSG:AD` + `ONBOX_ASDP_PASS` + `edge: 1` |
| M132–M134 | 前端 / 整链合流 / 第十三次中场 | 见整链计划 |

**未宣称 A2**。

## 2. 实现形态

- as：`.text` 可先于 `.rodata`（延迟 lea）；`sub`/`cmp`/`je`/`jne`/`push`/`pop`；`.data`/`.bss`/`.zero`
- host 边缘：非法助记符、缺 `)`、未定义标签、超长 `.asciz`、重复 `.globl`
- `link.rsp`：`main.o`+`lib.o`+musl string `.o`+`io.elf`（strlen 取自 io，避 DUP）

## 3. 纪律

大跨步；边缘同权；不拆通向 A2 的前端。
