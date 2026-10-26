# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M129 — **机上 mini-ld**：`as` 产 ET_REL，`link.rsp`→CRT+ET_EXEC。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M126 | strcat + strchr 多跳闭包 | 曾用 `ONBOX_SKIT_PASS` |
| M127 | GAS 子集 as → ET_REL | 曾用 `ONBOX_ASM_PASS` |
| M128 | 多文件 as + `.rodata`/`lea` | 曾用 `ONBOX_ASRO_PASS` |
| **M129（本窗）** | mini-ld：ET_REL×N + CRT → ET_EXEC | `NMSG:42` + `WMSG:LD` + `DMSG:21` + `DHINT:UND` + `ONBOX_LD_PASS` + `edge: 1` |
| M130+ | 更大 ld/as / musl → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2；见 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **机上 as+ld 可独立于 C 前端产出可执行 ELF**。

## 2. 实现形态

- `cxc-ld`：导入若干 SysV ET_REL，解析延迟 UND，合成 CRT（`main`→`NMSG`/`WMSG`/`PASS`/`exit`）
- 主路径：优先 `disk/link.rsp`→`ld_link`；否则 `disk/in.c`→`cc_compile`
- as：`lib.s` / `msg.s` / `main.s` → 对应 `.o`
- 探针：`main.s`+`lib.s`+`link.rsp` → exit 42；边缘删 rsp 走 cc UND

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) 下一主线一致。
