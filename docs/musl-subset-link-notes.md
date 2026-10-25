# musl 子集链接实验笔记

基线：Milestone 68 / v0.69.0  
宿主探针：Ubuntu `musl` / `musl-dev` / `musl-tools` 1.2.4（WSL / Linux CI）。

## 1. 已验证（链接）

- `musl-gcc -static` + `user/musl_crt.ld`（`FILEHDR PHDRS` 并入 text PT_LOAD）产出可执行文件；入口在 headers 之后（例：`0x4000f0`）；PHDR VA=`0x400040`。
- 无 `musl-gcc` 时使用 `user/prebuilt/musl_crt.elf`（同配方预构建）。

## 2. 运行阻塞清单（驱动后续补号，禁止盲堆）

| # | 阻塞 | 证据 | 状态 |
|---|---|---|---|
| 1 | musl 使用 `syscall`（`0f 05`） | `objdump` | **M66 已还** |
| 2 | musl `_start` 期望栈上 `argc/argv/envp/auxv` | `_start` | **M67 已还** |
| 3 | CRT 启动 `set_tid_address` / `arch_prctl` / `exit_group` 等 | 反汇编 | `exit_group` **M67**；其余按失败清单 |
| 4 | PHDR 未映射 → `AT_PHDR=0` | 旧 `cross_hello.ld` | **M68 已还**（`musl_crt.ld` + loader 填 `at_phdr`） |

## 3. 已交付

- **M65–M67**：标注等价 `musl_subset`、LSTAR、用户栈种子。
- **M68**：真 musl-gcc 静态 CRT 用户程序路径（`USER_MUSL_CRT_PASS` / ESP `LIBMCR.ELF`）。
- **不宣称**：机上编译器（A2）、完整 POSIX、SYSRET 快路径。

## 4. 下一刀优先级

1. 若 CRT 探针仍失败：按串口/`ENOSYS` 清单补号（禁止盲堆）；  
2. 可选：SYSCALL 返回改 SYSRET；  
3. 加深 TLS / 多进程仍非本窗主线。
