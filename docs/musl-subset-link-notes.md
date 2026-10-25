# musl 子集链接实验笔记

基线：Milestone 66 / v0.67.0  
宿主探针：Ubuntu `musl` / `musl-dev` / `musl-tools` 1.2.4（WSL）。

## 1. 已验证（链接）

- `musl-gcc -static` 可产出 ~19KiB 可执行文件（含 `_start` / `_start_c` / `__libc_start_main`）。
- 搭配本仓 `user/cross_hello.ld`：`musl-gcc -static -fno-pie -no-pie … -Wl,-T,cross_hello.ld` **链接成功**；入口 `0x400000`，`.data/.bss` 落在 `0x402000`（与现有用户布局一致）。

## 2. 运行阻塞清单（驱动后续补号，禁止盲堆）

| # | 阻塞 | 证据 | 状态 |
|---|---|---|---|
| 1 | musl 使用 `syscall`（`0f 05`），内核仅 `int 0x80` | `objdump` | **M66 已还**：LSTAR→同 `interrupt_dispatch`；GDT 用户段改为 data→code；`int 0x80` 仍保留 |
| 2 | musl `_start` 期望栈上 `argc/argv/envp/auxv` | `_start`：`mov %rsp,%rdi` | **下一刀** |
| 3 | CRT 启动路径调用 `set_tid_address` / `arch_prctl` / `exit_group` 等 | 反汇编 `eax=0xda/0x9e/0xe7` | 按失败清单补号 |
| 4 | 完整 `libc.a` + 非 musl-gcc 默认 CRT 易缺 `__init_array_*` / `__ehdr_start` | nostdlib 混链失败 | 坚持 musl CRT 或极小子集 |

## 3. 已交付

- **M65**：`user/musl_subset/` + ESP `LIBMSL.ELF` + `USER_MUSL_PASS`（标注等价子集）。
- **M66**：`syscall` 指令经 LSTAR 入核；`musl_subset` 改用 `syscall`；`mini_libc` 仍 `int $0x80`。
- **不宣称**：完整 musl CRT、机上编译器（A2）、SYSRET 快路径（当前 SYSCALL 返回仍走 `iretq`）。

## 4. 下一刀优先级

1. 用户栈 `argc/argv/auxv` 种子，使真 musl `_start` 可进入；  
2. 按 `__init_libc` 失败清单补 `exit_group` 等缺口号；  
3. 可选：SYSCALL 返回改 SYSRET。
