# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M136 — **扩大 musl 闭包**：MERG 链入 `atoi`/`strcmp`/`labs`/`isdigit`（strlen 仅 `io.elf`）。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M133 | as+cc+ld+musl 合流 | 曾用 `ONBOX_MERG_PASS` |
| M134 | 中场统一 + host chaos | 曾用 MERG + `cc_chaos_test` |
| **M136（本窗）** | musl kit 扩大 | `NMSG:42` + `WMSG:MK` + `ONBOX_MKIT_PASS` + `edge: 1` |
| M137–M144 | as / 前端 / 链接混沌 / 第十四次中场 | 见整链计划 |

**未宣称 A2**。

## 2. 实现形态

- `link.rsp`：main.o + lib.o + atoi/scmp/labs/dig + string kit + io（无 `slen.o`）
- 容量：`CC_LINK_MAX`/`CC_EXPORT_MAX`/`CC_TEXT_SEC_MAX` 等抬升
- host 边缘：缺 musl 依赖 UND、坏路径、截断 ELF

## 3. 纪律

大跨步；边缘与混沌同权；不拆通向 A2 的前端；忌盲开 `strtol`。
