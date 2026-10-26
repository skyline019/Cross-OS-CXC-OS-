# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M119 — **真实 musl `labs.lo`（`.text.labs`）`#lib`** + 既有 host-gcc GOT/双 `#lib` 回归 + `DMSG`/`DHINT`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M117 | 双 host-gcc `#lib` | 曾用 `ONBOX_H2O_PASS` |
| M118 | `-fPIC` GOTPCREL 合成 GOT | 曾用 `ONBOX_GOT_PASS` |
| **M119（本窗）** | musl `libc.a`→`labs.lo` / `.text.*` | `NMSG:42` + `WMSG:ML` + `DMSG:21` + `DHINT:UND` + `ONBOX_MLAB_PASS` + `edge: 1` |
| M120+ | 更大 musl 多节 / 机上工具链 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **发行版 musl ET_REL 可被机上 `#lib` 链接调用**。

## 2. 实现形态

- `ar x …/libc.a labs.lo` → `musl_labs.o` + `musl_labs_elf.hpp`
- `elflink`：跳过空 `.text`，选用首个非空 `SHF_EXECINSTR`（musl `.text.labs`）
- 探针：`#lib "io.elf"` + `#lib "musl.o"`；`labs(0-42)` → 42

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-11.md`](midpoint-assessment-11.md) 下一主线一致。
