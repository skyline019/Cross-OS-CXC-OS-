# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M127 — **机上 as 子集**：`disk/lib.s`→SysV ET_REL，再与 musl 字符串工具箱同链。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M124 | `strcpy`/`__stpcpy`/`strncmp`；第十二次中场 | 曾用 `ONBOX_SCPY_PASS` |
| M125 | 延迟 UND + 自产 UND | 曾用 `ONBOX_LUND_PASS` |
| M126 | strcat + strchr 多跳闭包 | 曾用 `ONBOX_SKIT_PASS` |
| **M127（本窗）** | GAS 子集 as → ET_REL + 字符串闭包 | `NMSG:42` + `WMSG:AS` + `DMSG:21` + `DHINT:UND` + `ONBOX_ASM_PASS` + `edge: 1` |
| M128+ | 更大 as/ld 子集 / musl 闭包 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2；见 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **机上可汇编 SysV `.o` 并与真实 musl 对象同链运行**。

## 2. 实现形态

- `cxc-as`：`.text` / `.globl` / label / `ret` / `nop` / `call` / `mov`/`add`/`xor` 子集 → `cc_build_rel_elf`
- 主路径：优先 `disk/lib.s`→`as_compile`；否则 `disk/lib.c`→`cc_compile_obj`
- `#lib`×9：`lib`/`scat`/`schr`/`snul`/`scpy`/`stpc`/`sncmp`/`slen`/`io`
- 探针：`strcpy("XA")+strcat("YZ")+strchr('A')+strncmp("AYZ")+bump`（bump 来自 as）→ 42

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) 下一主线一致。
