# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M123 — **自产 ET_REL `.rodata` + `R_X86_64_PC32`** + **PLT32 互调** + musl `strcmp`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M119 | musl `labs.lo` | 曾用 `ONBOX_MLAB_PASS` |
| M120 | musl `isdigit.lo` 多节 | 曾用 `ONBOX_MDIG_PASS` |
| M121 | musl mem/str 三联 | 曾用 `ONBOX_MMEM_PASS` |
| M122 | 自产 `.rela.text` + musl `atoi` | 曾用 `ONBOX_ATOI_PASS` |
| **M123（本窗）** | 自产 `.rodata`/PC32 + musl `strcmp` | `NMSG:42` + `WMSG:RD` + `DMSG:21` + `DHINT:UND` + `ONBOX_RDAT_PASS` + `edge: 1` |
| M124+ | 更大 musl / 机上工具链 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **机上 `lib.c` 字面量走 `.rodata` 重定位并可与真实 musl `strcmp` 同链**。

## 2. 实现形态

- `obj_mode`：调用 → `R_X86_64_PLT32`；`lea` 字面量 → `R_X86_64_PC32`（`.rodata` section sym，addend `off-4`）
- `CC_RELA_SEC_MAX=16`
- 探针：`lib.c`→`lib.o` + `scmp.o` + `slen.o` + `io.elf`；`strcmp(msg(),"RD")+bump` → 42

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-11.md`](midpoint-assessment-11.md) 下一主线一致。
