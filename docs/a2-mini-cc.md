# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M115 — **#lib 真实 host-gcc ET_REL**（非自产种植）+ C 声明 / 自产 `.o` 仍保留 + `DMSG`/`DHINT`。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M105–M112 | `#lib` / plant CRT / typedef | 曾用各 ONBOX_* |
| M113 | 自产 ET_REL SysV | 曾用 `ONBOX_OBJ_PASS` |
| M114 | C-decl + 多函数 `.o` 互调；第十一次中场 | 曾用 `ONBOX_CFN_PASS` |
| **M115（本窗）** | `#lib` **host-gcc** `host_bump.o` | `NMSG:42` + `WMSG:GH` + `DMSG:21` + `DHINT:UND` + `ONBOX_HOBJ_PASS` + `edge: 1` |
| M116+ | 更大 gcc/musl `.o` / `.rela` 面 → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明的是 **标准 ABI 对象可被机上链接运行**。

## 2. 实现形态

- `user/host_bump.c` → `gcc -c -fno-pic` → `user/prebuilt/host_bump.o` + `host_bump_elf.hpp`
- 探针种植 `disk/host.o`；`import fn bump` + `cwrite`/`strlen`
- 自产 `lib.c`→`lib.o` 路径仍可用（回归保留）

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-11.md`](midpoint-assessment-11.md) 下一主线一致。
