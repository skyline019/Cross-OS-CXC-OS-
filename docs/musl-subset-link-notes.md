# M65 musl 子集链接实验笔记

基线：Milestone 65 / v0.66.0  
宿主探针：Ubuntu `musl` / `musl-dev` / `musl-tools` 1.2.4（WSL）。

## 1. 已验证（链接）

- `musl-gcc -static` 可产出 ~19KiB 可执行文件（含 `_start` / `_start_c` / `__libc_start_main`）。
- 搭配本仓 `user/cross_hello.ld`：`musl-gcc -static -fno-pie -no-pie … -Wl,-T,cross_hello.ld` **链接成功**；入口 `0x400000`，`.data/.bss` 落在 `0x402000`（与现有用户布局一致）。

## 2. 运行阻塞清单（驱动后续补号，禁止盲堆）

| # | 阻塞 | 证据 | 建议切片 |
|---|---|---|---|
| 1 | musl 使用 `syscall`（`0f 05`），内核仅 `int 0x80` | `objdump` 于 `write`/`__init_libc` | **LSTAR / SYSCALL 入口** |
| 2 | musl `_start` 期望栈上 `argc/argv/envp/auxv` | `_start`：`mov %rsp,%rdi` → 读 argc | 用户栈种子 / auxv |
| 3 | CRT 启动路径调用 `set_tid_address` / `arch_prctl` / `exit_group` 等 | 反汇编 `eax=0xda/0x9e/0xe7` | 按失败清单补号（`exit_group` 等） |
| 4 | 完整 `libc.a` + 非 musl-gcc 默认 CRT 易缺 `__init_array_*` / `__ehdr_start` | nostdlib 混链失败日志 | 坚持 musl CRT 或极小子集，不混 glibc |

## 3. 本里程碑交付（标注等价子集）

- 路径：`user/musl_subset/`（**不是** `mini_libc` stub）。
- 行为：静态链接 `write`/`_exit` + 薄 CRT → 串口 `USER_MUSL_PASS`；ESP `LIBMSL.ELF`。
- **明确偏差**：syscall 传输仍为 `int $0x80`（与上游 musl x86_64 的 `syscall` 指令不同），直至 LSTAR 落地。
- **不宣称**：完整 musl CRT 可跑、机上编译器（A2）、或与发行版 `libc.a` 无补丁 ABI 等价。

## 4. 下一刀优先级

1. 内核 `SYSCALL`/`SYSRET`（或等价）入口，复用现有 syscall 分发；  
2. 用户栈 `argc/argv/auxv` 种子，使真 musl `_start` 可进入；  
3. 按 `__init_libc` 失败清单补 `exit_group` 等缺口号。
