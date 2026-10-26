# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M117 — **双 host-gcc `#lib`（`math.o`→`host.o`）+ `.rodata` + UND→`io.elf`** + 自产 `.o` / C-decl + `DMSG`/`DHINT`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M115 | `#lib` host-gcc 无 UND | 曾用 `ONBOX_HOBJ_PASS` |
| M116 | host-gcc `.rodata` + UND `strlen`/`cwrite` | 曾用 `ONBOX_HUND_PASS` |
| **M117（本窗）** | 双 host-gcc `#lib`：`add`←`bump` + io | `NMSG:42` + `WMSG:H2` + `DMSG:21` + `DHINT:UND` + `ONBOX_H2O_PASS` + `edge: 1` |
| M118+ | GOT / 更大 musl `.o` / 机上工具链 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **多个真实 gcc 对象可互调并链入 plant 库**。

## 2. 实现形态

- `host_add.c` → `math.o`；`host_bump.c` → `host.o`（UND `add`/`strlen`/`cwrite`）
- `#lib` 顺序：`io.elf` → `math.o` → `host.o`
- `elflink`：`.rodata` + `R_X86_64_32`/`PC32`/`PLT32` + 跨 `#lib` UND

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-11.md`](midpoint-assessment-11.md) 下一主线一致。
