# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M120 — **musl 多 `.text.*`（`isdigit.lo`）+ 节间 PLT32** + `labs.lo` + host-gcc 回归 + `DMSG`/`DHINT`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M118 | `-fPIC` GOTPCREL | 曾用 `ONBOX_GOT_PASS` |
| M119 | musl `labs.lo` / 单 `.text.labs` | 曾用 `ONBOX_MLAB_PASS` |
| **M120（本窗）** | musl `isdigit.lo` 多节 + 节间 reloc | `NMSG:42` + `WMSG:MD` + `DMSG:21` + `DHINT:UND` + `ONBOX_MDIG_PASS` + `edge: 1` |
| M121+ | 更大 musl / 机上工具链 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **多可执行节 musl ET_REL 可完整放置并应用 sh_info 定向 `.rela`**。

## 2. 实现形态

- `isdigit.lo`：`.text.isdigit` + `.text.__isdigit_l` + `.rela.text.__isdigit_l`→`isdigit`
- `elflink`：收集全部非空 `SHF_EXECINSTR`；按 `rela.sh_info` 应用重定位
- 探针：`#lib dig.o` + `labs.o`；`isdigit(50)+41` → `labs` → 42

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-11.md`](midpoint-assessment-11.md) 下一主线一致。
