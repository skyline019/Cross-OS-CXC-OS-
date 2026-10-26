# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M116 — **host-gcc ET_REL：`.rodata` + `R_X86_64_32` + UND→`io.elf`** + 既有自产 `.o` / C-decl + `DMSG`/`DHINT`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M113 | 自产 ET_REL SysV | 曾用 `ONBOX_OBJ_PASS` |
| M114 | C-decl + 多函数 `.o` 互调；第十一次中场 | 曾用 `ONBOX_CFN_PASS` |
| M115 | `#lib` host-gcc 无 UND | 曾用 `ONBOX_HOBJ_PASS` |
| **M116（本窗）** | host-gcc `.rodata` + UND `strlen`/`cwrite` | `NMSG:42` + `WMSG:HG` + `DMSG:21` + `DHINT:UND` + `ONBOX_HUND_PASS` + `edge: 1` |
| M117+ | 更大 gcc/musl `.o` / GOT / 机上工具链 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **标准 gcc 对象的 rodata+跨库 UND 可被机上链接运行**。

## 2. 实现形态

- `user/host_bump.c` → `gcc -c -fno-pic -fno-pie` → `host_bump.o`
- `#lib "io.elf"` **先于** `#lib "host.o"`（UND 解析依赖已导入导出）
- `elflink`：放置 `.rodata`、`R_X86_64_32`/`PC32`/`PLT32`

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-11.md`](midpoint-assessment-11.md) 下一主线一致。
