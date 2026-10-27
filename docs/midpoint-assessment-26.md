# 第二十六次中场评估（M255–M264 / TCAP）

**版本**：v2.60.0  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_TCAP_PASS`（`exit: 42` + `edge: 1` + `tcap: 1` + `fat: 1` + `as: 1` + `WMSG:TC`）。QEMU 底线 **256 MiB**；virtio-blk **128 MiB**。

## 本窗交付

1. **M255–M256**：cxcfs 去掉整文件 RAM 缓存，改为共享扇区池 + 按扇区 I/O；`max_file_bytes=4MiB`、`max_files=256`、inode **512B**（单扇区对齐）；virtio **128 MiB**；>128 KiB 回读 + 超帽拒绝。
2. **M257–M258**：brk/mmap 各 **16 MiB**（mmap 基址 `0x2000000`，栈 `0x3800000`）；`max_path=255`、`fd_table_end=64`、`max_io=64KiB`。
3. **M259**：`max_exec_argc/envc=64`、`max_exec_arg_bytes=1025`、`pipe_capacity=4096`；fat stub 长 argv 证明。
4. **M260**：musl-static `fat42` 种植并垫到 **>1 MiB** 后 `execve` → 42。
5. **M261–M262**：musl-static mini `as`（AT&T 子集→ET_REL）机上写出 `disk/t.o`；宿主 `gcc -c` ET_REL bridge 种植；HARD 深闭包面不回退。
6. **M263–M264**：host `expect_fail` ≥3；整链 `ONBOX_TCAP_PASS` + chaos `0xC2A6`。

| 里程碑 | 要点 | 合同 |
|---|---|---|
| **M264** | 整链 TCAP | `ONBOX_TCAP_PASS` + `WMSG:TC` + `tcap: 1` + `edge: 1` |

## 本窗已清零的 OS 债

| 债 | 落地 |
|---|---|
| 单文件帽 | `max_file_bytes = 4 MiB` |
| inode / 盘 | `max_files = 256`；virtio `128 MiB` 共享池 |
| 堆 | brk/mmap 各 `16 MiB` |
| 路径 / FD / IO | `max_path=255`；fd≥64；`max_io=64 KiB` |
| exec 命令行 | argc/envc≥64；单参容量≥1024 |

## 仍未还（不得挡本窗）

- 完整机上 gcc/g++ 自举与完整 GNU as/ld 驱动面
- 父子并发 fork 生产模型
- 完整 file-backed mmap
- DWARF / 调试器
- 目录 rename 全语义

## mini-as UND / syscall 面

musl-static stripped `as`（`user/prebuilt/as`，~17 KiB file / ~58 KiB text）：依赖既有 musl 运行面（`open`/`read`/`write`/`close`/`exit`/`brk`/`arch_prctl` 等）。机上已成功 `execve` 并对 `disk/t.s` 写出 **ET_REL**；无新增 ENOSYS 阻塞项。完整 binutils as 仍未种植。

## 验收清单

1. `contract_test.py` / CI grep → `ONBOX_TCAP_PASS` + `WMSG:TC` + `tcap: 1` + `fat: 1`
2. chaos 种子 `0xC2A6`
3. VERSION **2.60.0**；重建 `user/prebuilt/cxc_cc.elf` 与 TCAP 种植头
