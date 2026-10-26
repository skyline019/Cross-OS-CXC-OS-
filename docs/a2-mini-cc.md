# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M126 — **musl 字符串工具箱闭包**：`strcat`→`strlen`/`strcpy`→`__stpcpy`，`strchr`→`__strchrnul`→`strlen`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M124 | `strcpy`/`__stpcpy`/`strncmp`；第十二次中场 | 曾用 `ONBOX_SCPY_PASS` |
| M125 | 延迟 UND + 自产 UND | 曾用 `ONBOX_LUND_PASS` |
| **M126（本窗）** | strcat + strchr 多跳闭包 | `NMSG:42` + `WMSG:SK` + `DMSG:21` + `DHINT:UND` + `ONBOX_SKIT_PASS` + `edge: 1` |
| M127+ | 更大 musl 闭包 / 机上 as+ld → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2；见 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **多枚真实 musl 字符串对象可按依赖闭包同链运行**。

## 2. 实现形态

- `#lib`×9：`lib`/`scat`/`schr`/`snul`/`scpy`/`stpc`/`sncmp`/`slen`/`io`（延迟 UND 允许任意序）
- 探针：`strcpy("XA")+strcat("YZ")+strchr('A')+strncmp("AYZ")+bump` → 42

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) 下一主线一致。
