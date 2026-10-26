# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M118 — **host-gcc `-fPIC -fno-plt`：`R_X86_64_GOTPCREL` 合成 GOT** + 双 `#lib` + `.rodata` + `DMSG`/`DHINT`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M116 | host-gcc `.rodata` + UND | 曾用 `ONBOX_HUND_PASS` |
| M117 | 双 host-gcc `#lib` | 曾用 `ONBOX_H2O_PASS` |
| **M118（本窗）** | `-fPIC` GOTPCREL 合成 GOT | `NMSG:42` + `WMSG:GT` + `DMSG:21` + `DHINT:UND` + `ONBOX_GOT_PASS` + `edge: 1` |
| M119+ | 更大 musl `.o` / 机上工具链 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **PIC/GOT 风格真实 gcc 对象可被机上链接运行**。

## 2. 实现形态

- `host_bump.c` → `gcc -c -fPIC -fno-plt` → GOTPCREL→`strlen`/`cwrite`/`add`
- `elflink`：按符号分配 8 字节 GOT 槽（`CC_LOAD_VA+sym`），写回 `G+A-P`
- `#lib` 顺序仍：`io.elf` → `math.o` → `host.o`

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-11.md`](midpoint-assessment-11.md) 下一主线一致。
