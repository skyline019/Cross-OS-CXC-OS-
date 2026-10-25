# musl 子集链接实验笔记

基线：Milestone 73 / v0.74.0  
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
- **M68**：真 musl-gcc 静态 CRT（`USER_MUSL_CRT_PASS` / `LIBMCR.ELF`）。
- **M69**：真 musl `malloc`/`free`（`USER_MUSL_MAL_PASS` / `LIBMMA.ELF`）。
- **M70**：真 musl `printf`（`USER_MUSL_PRT_PASS` / `LIBMPR.ELF`）；brk 基址 `0x407000`。
- **M71**：真 musl `fopen`（`USER_MUSL_FOP_PASS` / `LIBFOP.ELF`）；brk `0x409000`、栈 `0x420000`；`mprotect`/`ioctl`/`fcntl` stub；`readv`（stdio 读路径）。
- **M72**：真 musl `opendir`/`readdir`（`USER_MUSL_ODR_PASS` / `LIBODR.ELF`）；复用 `mkdir` + `getdents64`。
- **M73**：真 musl `unlink`（`USER_MUSL_UNL_PASS` / `LIBUNL.ELF`）；syscall 87；常规文件、fd 未打开时可删。
- **不宣称**：机上编译器（A2）、完整 POSIX、SYSRET 快路径、持久块设备。

## 4. 下一刀优先级

1. **M74 中场**：正式评估并裁定是否切 **virtio-blk** 薄切片（A2 硬阻塞；当前树无块设备代码）；  
2. 可选：SYSCALL 返回改 SYSRET；  
3. A2 机上工具链仍需独立合同。
