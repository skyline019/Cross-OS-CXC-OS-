# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M122 — **自产 ET_REL `.rela.text`（PLT32）** + **musl `atoi` `#lib`** + `strlen`/`bump` 同链。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M119 | musl `labs.lo` | 曾用 `ONBOX_MLAB_PASS` |
| M120 | musl `isdigit.lo` 多节 | 曾用 `ONBOX_MDIG_PASS` |
| M121 | musl mem/str 三联 | 曾用 `ONBOX_MMEM_PASS` |
| **M122（本窗）** | 自产 `.rela.text` + musl `atoi` | `NMSG:42` + `WMSG:AT` + `DMSG:21` + `DHINT:UND` + `ONBOX_ATOI_PASS` + `edge: 1` |
| M123+ | 更大 musl / 机上工具链 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **机上 `lib.c` 互调走 `.rela` 且可链真实 musl `atoi`**。

## 2. 实现形态

- `obj_mode`：调用留下 `e8 00..00`，`cc_build_rel_elf` 发 `R_X86_64_PLT32`（addend −4）
- 探针：`lib.c`→`lib.o` + `slen.o` + `atoi.o` + `io.elf`；`atoi("41")+bump` → 42

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-11.md`](midpoint-assessment-11.md) 下一主线一致。
