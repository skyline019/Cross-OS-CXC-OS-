# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**基线起点**：M96 — 以 freestanding C 实现的 `disk/cxc-cc` 替换手写机器码前端。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| **M96（本窗）** | `CXC_A2`；赋值；`+ - * /`；比较；`if`/`else`；`while`；`write LIT`；`return expr`；单字母～短 ident 局部变量 | `WMSG:HI` + `exit: 42` + `ONBOX_CTRL_PASS`（循环算出 42） |
| M97+ | 多语句块加深；`break`/`continue`；逻辑 `&&`/`\|\|`；一元 `-`/`!` | 新 PASS / 更强源码 |
| M98+ | 数组；`struct` 字段；指针算术子集 | 数据结构合同 |
| M99+ | 函数定义/调用（本编译单元）；简单递归 | 调用约定合同 |
| 后续 | 多文件；与 musl/CRT 链接；逼近可编译真实 C | 对齐 [`product-goals.md`](product-goals.md) A2 |

**未宣称 A2**：在机上 g++/gcc 合同另立之前，不宣称完整工具链。

## 2. 实现形态

- **源码**：`user/cxc_cc/*.c` + `mini_libc`（brk/malloc、open/read/write/close、execve、exit）
- **宿主构建**：linux-gcc → ESP `ONBXCC.ELF`；无 linux-gcc 时用 `user/prebuilt/cxc_cc.elf`
- **客体运行**：stub `execve("disk/cxc-cc")` → 读 `disk/in.c` → 写 `disk/out.elf` → `execve`
- **约束**：用户栈 4 KiB → AST/缓冲走堆；I/O ≤4 KiB/次；ELF/文件 ≤128 KiB；ESP 装载 ONBXCC ≤128 KiB

## 3. 代码生成

- 目标：静态 `ET_EXEC` @ `0x400000`，`int 0x80` Linux x86-64 号
- 局部变量：栈帧固定槽位；表达式结果在 `rax`
- 禁止改写已映射 RX 映像；字符串与码缓冲在编译期堆上组装后再 `write`

## 4. 纪律

1. 大跨步加深语义，禁止 PASS 后拆除通向 A2 的前端。  
2. 每刀预留 stage/failure；边缘（非法 token、缺 `;`、除零策略）纳入合同权重。  
3. 与 [`midpoint-assessment-9.md`](midpoint-assessment-9.md) 下一主线一致。
