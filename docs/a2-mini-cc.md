# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M128 — **多文件 as + `.rodata`/`.asciz`/`lea`**：`lib.s`+`msg.s`→ET_REL，与 musl 字符串工具箱同链。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M125 | 延迟 UND + 自产 UND | 曾用 `ONBOX_LUND_PASS` |
| M126 | strcat + strchr 多跳闭包 | 曾用 `ONBOX_SKIT_PASS` |
| M127 | GAS 子集 as → ET_REL | 曾用 `ONBOX_ASM_PASS` |
| **M128（本窗）** | 多文件 as + `.rodata`/`lea` | `NMSG:42` + `WMSG:RO` + `DMSG:21` + `DHINT:UND` + `ONBOX_ASRO_PASS` + `edge: 1` |
| M129+ | 机上 ld 子集 / 更大 musl → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2；见 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **机上 as 可产带 `.rodata`+PC32 的 SysV `.o`，且可多源汇编后同链**。

## 2. 实现形态

- `cxc-as`：M127 指令子集 + `.rodata` / `.section` / `.asciz` / `.ascii` / `lea name(%rip),%rax`
- 主路径：`disk/lib.s`→`lib.o`，`disk/msg.s`→`msg.o`（若存在）
- `#lib`×10：`lib`/`msg`/`scat`/`schr`/`snul`/`scpy`/`stpc`/`sncmp`/`slen`/`io`
- 探针：`msg()`（as `.rodata`）+ strcat/strchr 闭包 + bump → 42

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) 下一主线一致。
