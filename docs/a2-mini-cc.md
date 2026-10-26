# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M130 — **ld+musl 字符串**：`link.rsp` 链 as 对象与真实 musl/io ET_REL。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M128 | 多文件 as + `.rodata`/`lea` | 曾用 `ONBOX_ASRO_PASS` |
| M129 | mini-ld ET_REL→ET_EXEC | 曾用 `ONBOX_LD_PASS` |
| **M130（本窗）** | ld 链 musl string + io | `NMSG:42` + `WMSG:LL` + `ONBOX_LDLIB_PASS` + `edge: 1` |
| M131–M134 | as 加深 / 前端 / 整链合流 / 第十三次中场 | 见整链计划 |

**未宣称 A2**。

## 2. 实现形态

- 容量抬升：`CC_CODE_MAX`/`CC_LINK_MAX`/`CC_EXPORT_MAX` 等
- `link.rsp`：`main.o`+`lib.o`+musl string `.o`+`io.elf`（strlen 取自 io，避 DUP）
- as：`lea` + `mov %rax,%rdi` + 跨文件 `call strlen`

## 3. 纪律

大跨步；边缘同权；不拆通向 A2 的前端。
