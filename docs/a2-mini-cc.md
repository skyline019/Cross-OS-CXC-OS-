# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M132 — **前端 char/const**：字宽槽 + 显式截断；`const` 拒绝再赋值。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M129 | mini-ld ET_REL→ET_EXEC | 曾用 `ONBOX_LD_PASS` |
| M130 | ld 链 musl string + io | 曾用 `ONBOX_LDLIB_PASS` |
| M131 | as 延迟 lea + 指令/.data | 曾用 `ONBOX_ASDP_PASS` |
| **M132（本窗）** | `char` 截断 + `const` 合同 | `NMSG:42` + `WMSG:CH` + `ONBOX_CHAR_PASS` + `edge: 1` |
| M133–M134 | 整链合流 / 第十三次中场 | 见整链计划 |

**未宣称 A2**。

## 2. 实现形态

- `char`：按字宽槽存储，store 前 `and eax,0xff`
- `const`：须初始化；再赋值/`++`/`+=` 拒绝
- host 边缘：非法 const、缺分号、过深 while、超 `CC_LOCAL_MAX`
- as/ld 仍种植可用；本窗成功路径走 `in.c`（无 `link.rsp`）

## 3. 纪律

大跨步；边缘同权；不拆通向 A2 的前端。
