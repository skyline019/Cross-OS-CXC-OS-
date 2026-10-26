# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M124 — **musl `strcpy`→`__stpcpy` 跨 `#lib` UND** + `strncmp` + 自产 `.rodata`/PC32（第十二次中场）。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M121 | musl mem/str 三联 | 曾用 `ONBOX_MMEM_PASS` |
| M122 | 自产 `.rela.text` + musl `atoi` | 曾用 `ONBOX_ATOI_PASS` |
| M123 | 自产 `.rodata`/PC32 + musl `strcmp` | 曾用 `ONBOX_RDAT_PASS` |
| **M124（本窗/中场）** | `strcpy`/`__stpcpy`/`strncmp` | `NMSG:42` + `WMSG:CP` + `DMSG:21` + `DHINT:UND` + `ONBOX_SCPY_PASS` + `edge: 1` |
| M125+ | 更大 musl 闭包 / 机上 as+ld → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2；见 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **真实 musl 对象间 UND 可在机上解析并与自产字面量同链**。

## 2. 实现形态

- `#lib` 顺序：`stpc.o` 先于 `scpy.o`；链接器按导入序积累导出
- 探针：`strcpy(&buf[0], msg())` + `strncmp(...,"CP",2)` + `bump` → 42

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) 下一主线一致。
