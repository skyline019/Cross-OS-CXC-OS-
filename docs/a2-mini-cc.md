# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M125 — **跨 `#lib` 延迟 UND 解析** + **自产 ET_REL 发 UND**（`scpy` 可先于 `stpc`；`lib.o` 可 `import` musl）。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M122 | 自产 `.rela.text` + musl `atoi` | 曾用 `ONBOX_ATOI_PASS` |
| M123 | 自产 `.rodata`/PC32 + musl `strcmp` | 曾用 `ONBOX_RDAT_PASS` |
| M124 | `strcpy`/`__stpcpy`/`strncmp`；第十二次中场 | 曾用 `ONBOX_SCPY_PASS` |
| **M125（本窗）** | 延迟 UND + 自产 UND | `NMSG:42` + `WMSG:LU` + `DMSG:21` + `DHINT:UND` + `ONBOX_LUND_PASS` + `edge: 1` |
| M126+ | 更大 musl 闭包 / 机上 as+ld → g++/gcc | 对齐 [`product-goals.md`](product-goals.md) A2；见 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) |

**未宣称 A2**：仍无完整工具链机上编译；本窗证明 **链接器不再依赖 `#lib` 拓扑序，且自产 `.o` 可带外部 UND**。

## 2. 实现形态

- `cc_und_resolve`：导入阶段未解析的 UND 记入挂起表，全部 `#lib` 导出后再打补丁
- `obj_mode`：`import fn` → ET_REL `STT_FUNC` + `SHN_UNDEF` + `R_X86_64_PLT32`
- `CC_LINK_MAX=12`、`CC_EXPORT_MAX=48`、`CC_UND_PEND_MAX=64`
- 探针：`lib.o`(UND `strncmp`) → `scpy`→`stpc`（逆序）→ `chk()+bump` → 42

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同。  
3. 与 [`midpoint-assessment-12.md`](midpoint-assessment-12.md) 下一主线一致。
