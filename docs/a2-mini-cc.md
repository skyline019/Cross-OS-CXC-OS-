# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M109 — **跨 `#lib` 未定义符号解析**（CRT `ans`→math `twice`）+ `R_X86_64_PC32`/`PLT32` + 失败 **`DMSG:NN`** 调试兜底。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96–M104 | 控制流 / I/O / fn / struct / include / CRT / switch / enum | 曾用各 ONBOX_* |
| M105 | `extern fn` / `#link` 多 TU | 曾用 `ONBOX_LINK_PASS` |
| M106 | `import` / `#lib` 单符号 ET_REL | 曾用 `ONBOX_LIB_PASS` |
| M107 | 多符号 math + SysV≤4 | 曾用 `ONBOX_MATH_PASS` |
| M108 | 整节 `.text` + 节内 PC32（`twice`→`add`） | 曾用 `ONBOX_PC32_PASS` |
| **M109（本窗）** | 多 `#lib`；UND→先前列出口；planted CRT；`DMSG` 边缘 | `NMSG:42` + `WMSG:XL` + `DMSG:21` + `ONBOX_XLIB_PASS` + `edge: 1` |
| M110+ | 更多 musl/CRT 符号；声明语法靠拢真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：仍无完整 ELF 链接器（无 GOT/PLT 运行时、无 `R_X86_64_64`、无动态链接）。

## 2. 实现形态

- 每个 `#lib` **总是**并入整节可执行 `.text`，导出 GLOBAL FUNC 供后续库解析
- 重定位：`R_X86_64_PC32` 与 `R_X86_64_PLT32`；`SHN_UNDEF` 按名解析到已导出符号
- 编译失败时 on-box `cxc-cc` 写 `DMSG:NN\n`（如 `21`=UND）再 `exit 4`
- 宿主 `cc_host_test`：xlib 成功路径 + `bad-cross-und` 边缘

## 3. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 边缘纳入合同（含调试标记）。  
3. 与 [`midpoint-assessment-10.md`](midpoint-assessment-10.md) 下一主线一致。
