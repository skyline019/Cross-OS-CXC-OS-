# PMFS v3 修复与验证报告（2026-10-28）

> **历史 v3 修复记录：** 本文的功能限制和测试时间点均为本轮 VFS 扩展之前；当前独立打开、目录和路径语义以[语义](SEMANTICS.md)及 [VFS 验证记录](results/2026-10-28-vfs/README.md)为准。此处记录跨进程文件/KV 恢复修复、受限普通 fd、非破坏挂载、持久化顺序与自动化回归，并在**修改过的 QEMU 9.2.0** 中接入一个独立 `nvdimm` 设备。结果仍不是物理掉电或真实 PMEM 性能证明。总索引：[README.md](README.md)；里程碑：[MILESTONES.md](MILESTONES.md)。

## 1. 修复内容

1. **可恢复格式和安全挂载。** v3 追加 checksummed KV run、COW inode/dentry 根及双槽 checkpoint，以对齐 8 字节提交词发布；重启从介质重建索引。空白、旧版、不兼容、越界/损坏镜像拒绝挂载且不自动格式化；`pmformat --force` 是显式破坏操作。v2 不兼容且没有迁移工具。
2. **错误与并发。** 文件工作副本仅在成功同步后发布；短读/损坏、容量不足、同步和最后关闭错误显式返回。PMFS syscall 与普通 fd 共用 sleep lock，禁止独立重复打开同一文件；独立句柄按进程清理。加入定向 dirty bitmap，暂时禁用可能丢数据的 compaction；不回收意味着长时间运行终会耗尽空间。
3. **普通路径子集。** `/pm` 绝对路径支持受限 `open/read/write/fsync/close/mkdir/unlink`、共享 offset 的 `dup/fork`、shell 重定向和全局 `sync` 快照。不是一般 VFS；目录枚举、相对路径、`chdir`、多写者等限制见[语义](SEMANTICS.md)。
4. **设备与缓存操作。** 默认 MAP 仍为普通 RAM 并排除 QEMU DTB；额外提供[实验 QEMU 补丁](../tools/patches/qemu-9.2.0-riscv-pmfs-nvdimm.patch)，以固定位置冷插拔独立 `nvdimm` 并通过私有 FDT 发现；QMP 可独立核查。默认 touch+fence **不写回缓存**；可选 `PMEM_ZICBOM` 发出 `CBO.CLEAN`，假设 64 B 块和有效权限，TCG 不模拟缓存持久域。
5. **验证与基准。** 新增真实 QEMU 进程重启、checkpoint/页切点、损坏拒绝和自动化 oracle；独立有界协议规格模型含三个故障负控。基准改为两侧普通 API、相同文件数和读回校验，保留原始 tick，**持久语义仍不等价**。

## 2. 已完成的隔离沙箱运行

原始日志在 [`results/2026-10-28-v3/`](results/2026-10-28-v3/)；修复前失败记录仍在[基线报告](LOCAL-SANDBOX-TESTS.md)。本地以 QEMU 9.2.0 TCG、Zig 0.13.0 / Clang 18.1.6、LLD 18.1.6 和 GNU binutils 2.42 包装器构建；用户 ELF 经 `objcopy --strip-debug`。这不是本机标准 GNU RISC-V GCC 测试。QEMU 依赖的 GLib gvdb 当时使用未固定 `main`；二进制散列和包装器见[环境记录](results/2026-10-28-v3/environment.txt)。

| 项目 | 已观察结果 | 证据 |
|---|---|---|
| 独立 NVDIMM 设备身份 | QMP：`type=nvdimm`、`memdev=/objects/nvd`、`addr=0x88000000`、大小 32 MiB，普通 RAM 128 MiB | [QMP 原始输出](results/2026-10-28-v3/nvdimm-qmp.log) |
| 实验 NVDIMM 进程重启与切点 | 3 核，66 次启动，`PMFS_RECOVERY_PASS ... nvdimm=1` | [运行日志](results/2026-10-28-v3/nvdimm-recovery.log)、[逐次串口归档](results/2026-10-28-v3/nvdimm-serial.tar.gz) |
| NVDIMM + Zicbom 进程重启与切点 | 3 核，66 次启动，`PMFS_RECOVERY_PASS ... zicbom=1 nvdimm=1` | [运行日志](results/2026-10-28-v3/final-nvdimm-zicbom.log)、[逐次串口归档](results/2026-10-28-v3/final-nvdimm-zicbom-serial.tar.gz) |
| 普通共享 RAM 进程重启与切点 | 3 核，66 次启动，默认及 Zicbom 路径均通过 | [默认](results/2026-10-28-v3/final-recovery.log)、[Zicbom](results/2026-10-28-v3/final-zicbom.log) |
| 原生 xv6 完整 usertests | 3 核 74 项、`ALL TESTS PASSED` | [日志](results/2026-10-28-v3/final-usertests.log) |
| xv6 原生块日志 crash | 当时的概率性脚本：1 核，退出 0；后续 CI 失败后已替换，见[新测试](XV6-LOG-RECOVERY.md) | [历史日志](results/2026-10-28-v3/final-crash.log) |
| `contract-full`、`contract-pmem`、`contract-pmem2` | 均退出 0；PMEM2 仅共享 RAM 魔数 | [EMUL](results/2026-10-28-v3/final-contract-full.log)、[MAP](results/2026-10-28-v3/final-contract-map.log)、[PMEM2](results/2026-10-28-v3/final-contract-pmem2.log) |
| `bench-sweep` | EMUL N=8/16/32、MAP N=32，各 5 次，计数相同，字节校验通过 | [原始输出](results/2026-10-28-v3/final-bench.log) |
| 有界规格模型 | 正确协议 64 个检查状态通过；删除数据刷写、提前发布、删除提交刷写三项负控均找到反例 | [日志](results/2026-10-28-v3/final-model.log) |

回归覆盖空白/v2/校验正确但根越界或重叠/数据损坏拒绝挂载和无改写、单槽回退、跨多次进程重启读回文件/KV、dirty 中断、索引/容量、并发、普通 fd/`dup`/`fork`、shell、全局 `sync`。首次 checkpoint 切点 1–5 恢复旧文件；第二次 checkpoint 的 1–3 恢复旧文件，4–5 在共享映射下恢复新文件；新 run、inode、dentry 的半页/整页写入切点恢复旧值。仅覆盖显式插桩切点。

旧自测中的 `FILE_FAIL code=12`、`CDC_FAIL code=3`、`STREAM_FAIL code=9` 是有对应 `NEG_*_PASS` 的预期负例；原生 usertests 的部分非法访问 trap 和资源耗尽提示是测试本身的预期输出。基线测试时原工作目录曾误执行一次构建并删除/rebuild `fs.img`，当时它是否包含用户数据未知；后续所有破坏性测试均在隔离副本中运行。

## 3. 复现命令与安全警告

在 RISC-V GCC/binutils、QEMU、Python 3 和 `bc` 可用的**可丢弃 checkout** 内：

```sh
make -j2 kernel/kernel fs.img
CPUS=3 ./test-xv6.py usertests
CPUS=1 python3 tools/xv6_log_recovery.py --expect-disabled
CPUS=1 make test-xv6-crash  # 重建为仅供测试的插桩内核
CPUS=3 ./test-xv6.py log    # 继续使用上一步构建的插桩内核
CPUS=3 ./test-xv6.py stress
CPUS=1 make contract-full
CPUS=1 make contract-pmem
CPUS=1 make contract-pmem2
CPUS=1 make bench-sweep
make test-model
make test-recovery CPUS=3 RECOVERY_ARGS="--log-dir .ci-logs/recovery"
make test-recovery-zicbom CPUS=3 RECOVERY_ARGS="--log-dir .ci-logs/zicbom"
# 先按 docs/QEMU-NVDIMM.md 构建补丁 QEMU：
python3 tools/check_nvdimm_device.py --qemu /path/to/patched/qemu-system-riscv64
make test-recovery-nvdimm QEMU=/path/to/patched/qemu-system-riscv64
make test-recovery-nvdimm-zicbom QEMU=/path/to/patched/qemu-system-riscv64
```

这些测试/模式切换会 `make clean`、删除并重建工作目录 `fs.img`；boot 自测和 `pmformat --force` 会破坏介质。先备份，不要在有价值的镜像上执行。测试器自己的磁盘、共享 RAM 和 NVDIMM 文件是临时副本。模式切换需要干净重建，不能混用旧内核。

GitHub Actions 已配置 Linux 标准 GNU RISC-V 工具链全量回归、实验 QEMU 补丁设备任务、固定版本 formatter 和 macOS 构建；实际 CI 结果以该分支的运行记录为准，不以工作流配置代替运行结果。

## 4. 仍需完成与不可外推的结论

- **设备发现：** 补丁只针对 QEMU 9.2.0、单个固定位置和私有 FDT，非上游通用 `virt`，不实现标准 ACPI/NFIT、标签命名空间或热插拔。普通 MAP 及整机共享 RAM 不是 NVDIMM。
- **硬件真实性：** QEMU 进程杀死后，宿主页缓存和普通共享文件仍在；TCG 的 CBO helper 不模拟缓存层级。没有真实设备、持久域、缓存逐出/乱序、校验碰撞或物理电源故障验证。COW 协议还依赖 8 字节提交原子性、刷写/屏障与平台持久域假设。
- **模型边界：** `check_persist_model.py` 是手写的小规模协议规格模型，不从内核提取实际 store/flush trace，不是端到端形式化证明或所有并发的穷举。
- **工程与论文：** 不支持安全回收/compaction、完整 POSIX、通用挂载及充分资源规模；正式性能对照还需可比较的持久保证、扩展负载、独立重复与统计，旧 PDF/数字不可直接提交。结果应如实呈现，不声称真实硬件加速。
