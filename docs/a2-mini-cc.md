# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M98 — freestanding C `cxc-cc`：控制流/数组 + **input/print 文件 I/O** + 边缘合同。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M96 | `CXC_A2`；赋值；`+ - * /`；比较；`if`/`else`；`while`；`write LIT`；`return expr` | 曾用 `ONBOX_CTRL_PASS` |
| M97 | `break`/`continue`；`&&`/`\|\|`；一元 `-`/`!`；数组 `a[N];` / `a[i]` | 曾用 `ONBOX_LANG_PASS` |
| **M98（本窗）** | `input ident;`（读 `disk/io.in` 十进制）；`print expr;`（`NMSG:`）；保留 `write LIT` | `NMSG:35` + `WMSG:IO` + `exit: 42` + `ONBOX_IORW_PASS` + `edge: 1`；宿主 `cc_host_test`（含 Win32 `ull` 移位修复） |
| M99+ | `struct` 字段；指针算术子集；函数定义/调用 | 数据结构/调用约定合同 |
| 后续 | 多文件；与 musl/CRT 链接；逼近可编译真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。

## 2. 实现形态

- **源码**：`user/cxc_cc/*.c` + `mini_libc`
- **宿主构建**：linux-gcc → ESP `ONBXCC.ELF`；无 linux-gcc 时用 `user/prebuilt/cxc_cc.elf`
- **宿主严格测试**：`tools/run_cc_host_test.py`（`-DCC_HOST`）纳入 `contract_test.py --host-only`
- **客体运行**：stub → `cxc-cc` → `out.elf`；`input` 打开 `disk/io.in`；非法源 exit 4
- **约束**：I/O ≤4 KiB/次；ELF/文件 ≤128 KiB；ESP ONBXCC ≤128 KiB

## 3. 代码生成

- 目标：静态 `ET_EXEC` @ `0x400000`，`int 0x80` Linux x86-64 号（open/read/write/close/exit）
- `print`：栈上 itoa → `NMSG:<digits>\n`；`input`：open/read/parse → 局部变量
- 禁止改写已映射 RX 映像

## 4. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 每刀预留 stage/failure；边缘纳入合同权重。  
3. 与 [`midpoint-assessment-9.md`](midpoint-assessment-9.md) 下一主线一致。
