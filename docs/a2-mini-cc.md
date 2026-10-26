# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M121 — **musl `strlen`/`memcmp`/`memcpy` 三联 `#lib`** + keep-first 符号 + `st_size==0` + 扩容 `CC_LINK_MAX`/`CC_IDENT_MAX`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M119 | musl `labs.lo` | 曾用 `ONBOX_MLAB_PASS` |
| M120 | musl `isdigit.lo` 多节 | 曾用 `ONBOX_MDIG_PASS` |
| **M121（本窗）** | musl mem/str 三联 + 链接器放宽 | `NMSG:42` + `WMSG:MM` + `DMSG:21` + `DHINT:UND` + `ONBOX_MMEM_PASS` + `edge: 1` |
| M122+ | 更大 musl / 机上工具链 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **多枚真实 musl 字符串/内存对象可同链并覆盖 io 同名符号**。

## 2. 实现形态

- `CC_LINK_MAX=8`、`CC_IDENT_MAX=16`、`CC_EXPORT_MAX=32`
- 导出：同名 keep-first；`st_size==0` 按节余长（memcpy）
- 探针：`slen.o`→`mcmp.o`→`mcpy.o`→`io.elf`；`memcmp==0` → `+42`

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-11.md`](midpoint-assessment-11.md) 下一主线一致。
