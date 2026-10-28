# KV/CDC/VFS 软件效率与恢复验证（2026-10-28）

这是上一轮源码 `6d0f21a1d328910be3289be7ab19191a923e01d1` 的历史实测记录，与早先的 [VFS 第一阶段](../2026-10-28-vfs/README.md)分开；其追加耗尽和“没有 GC”描述不代表[本轮维护实现](../2026-10-28-gc/README.md)的当前状态。原始日志、可应用的消融/负控补丁、逐次串口输出及[源码散列/工具链](environment.txt)随文保存。沙箱以 QEMU 9.2.0 TCG、Zig 0.13.0/Clang 18.1.6 的 RISC-V 包装器及 GNU binutils 2.42 构建，不是标准 GNU RISC-V GCC 机器，也不是硬件实验；最终 GitHub Actions 应以实际运行结果为准。NVDIMM 本地构建快照与最终源码的 `pmindex.c` 只差一处注释措辞。

**所有数字是特定工作负载中的软件请求数，不是介质写入字节、物理擦除次数、器件磨损或真实硬件延迟。** QEMU 文件后端/进程杀死不模拟宿主断电，TCG 的 Zicbom 不模拟持久缓存层级。更多计数定义和未解决问题见[软件放大与耐久风险](../../PERFORMANCE-ENDURANCE.md)。

## 可重复消融：同一指定工作负载

单核、默认 8 MiB EMUL；[当前](optimized-emul-1.log)与三个仅改变指定路径的补丁分别执行相同数据和读回逻辑。消融测试为了断言对照值，会同步调整测试程序的预期；补丁与原始输出均保留。本表是一次确定性软件计数，不是时间统计或所有工作负载的性能结论。

| 指标 | 对照路径 | 当前路径 | 说明 |
|---|---:|---:|---|
| 非替换 rename，完整 4 KiB 页写请求 | 2 | 1 | 未变的 inode 快照复用；对照 [补丁](full-metadata-ablation.patch)/[日志](full-metadata-ablation.log) |
| 同上，软件 `dev_write_bytes` | 8256 | 4160 | 少 4096 B（49.6%）；仍有同样 2 次 checkpoint 字 store |
| 同一 4 B 内容的普通文件同步，完整页写请求 | 3 | 2 | 数据 run 和 inode 页各一；复用 dentry 页 |
| 同上，软件 `dev_write_bytes` | 12416 | 8320 | 少 4096 B（33.0%）；两次 checkpoint，共 4 次状态字 store |
| 替换目标 rename，页写请求 / 软件字节 | 2 / 8256 | 2 / 8256 | 替换同时修改两种元数据，保留一次原子根发布 |
| 最旧 run 的 80 B 冷 KV 键，run probe | 144 | 0 | 禁用精确直达[补丁](no-exact-index-ablation.patch)/[日志](no-exact-index-ablation.log)；两个路径都读取 **1 页**，此例未减少页读数 |
| 稀疏 posting 中的溢出键，run probe / 页面读 | 80 / 1 | 16 / 1 | 按位遍历跳过 64 个无关 run；线性遍历[补丁](linear-posting-ablation.patch)/[日志](linear-posting-ablation.log) |
| 未使用 posting 桶的缺失键，run probe / 页面读 | 214 / 0 | 0 / 0 | 直接处理空位图；原线性对照也不读页，但遍历 214 个 run |

其他精确断言：相同 4 B 覆盖只移动 offset，跳过 4 B，软件设备写请求和 flush 行请求均为零；先 dirty 后再次写同值不会清掉 dirty。空且干净的 truncate 不重复发布。1024 B 零数据走固定 fallback 并完整读回；另一 1024 B 模式被 CDC 接收，探测为四块，普通 fd 完整读回，在恢复矩阵中还跨启动验证。已跟踪的旧键在 512 键溢出后更新仍直达新 run；未跟踪键的最新更新必须 fallback，跨多个 64 位 posting 字依旧从新到旧查找。单核及三核计数见[1 核](optimized-emul-1.log)/[3 核](optimized-emul-3.log)。

相同 EMUL 容量测试中，当前源码以 `VFS_CAPACITY_PASS successful_renames=2030` 到达空间不足并正确保留旧路径、令 dirty `sync`/最后 `close` 失败；[完整元数据消融](full-metadata-vfs-capacity.log)为 1014。它只说明此有限追加分配测试延后耗尽；不是寿命倍数，**没有安全 GC/compaction 或 checkpoint 磨损均衡**。

## 功能、重启与负控

| 检查 | 结果与原始输出 |
|---|---|
| 默认 EMUL VFS + 容量、效率 | [1 核](vfs-emul-1.log)、[3 核](vfs-emul-3.log)，[效率 1 核](optimized-emul-1.log)、[效率 3 核](optimized-emul-3.log)通过 |
| 标准 xv6 用户测试、严格契约、原生日志故障 | [3 核 usertests](xv6-usertests-3.log) `ALL TESTS PASSED`、[contract-full](contract-full-3.log) `contract: OK`、[原生日志](xv6-native-crash-3.log)通过 |
| MAP 的 VFS 子树 rename | [43 次启动](map-vfs-recovery.log)，分别检查非替换的 1 页与替换的 2 页以及 checkpoint 的阶段 1–7 |
| MAP 的文件/KV 恢复 | [62 次启动](map-pmfs-recovery.log)：运行更新、dirty 中断、多个 posting 字/索引满后的更新与重启、两个 checkpoint、run/inode 页及损坏拒绝 |
| 实验 QEMU NVDIMM | [VFS 43 次](nvdimm-vfs-recovery.log)、[PMFS 62 次](nvdimm-pmfs-recovery.log)；独立设备的 QMP 识别见[先前 QMP 原始输出](../2026-10-28-v3/nvdimm-qmp.log) |
| NVDIMM + Zicbom 指令路径 | [VFS 43 次](nvdimm-zicbom-vfs-recovery.log)、[PMFS 62 次](nvdimm-zicbom-pmfs-recovery.log)，仅检查 TCG 软件行为 |
| 独立规格模型 | [原始输出](model.log)：两页 rename 14 个 crash 状态、一页复用 inode 根 10 个状态；提前发布、部分子树、原地更新等负控有反例。模型未绑定真实内核 trace |
| 真实内核提前发布负控 | [补丁](early-subtree-publication.patch)在后代改写完成前发布子树；[日志](early-subtree-publication.log)显示 `simple-checkpoint-4-new` 被 oracle 拒绝。该运行以预期失败退出，不能计为正常测试通过 |
| 精确索引满后缺 posting 负控 | [补丁](missing-overflow-posting.patch)故意不维护满表后 run 的 posting；[日志](missing-overflow-posting.log)中的 `PMFS_EFFICIENCY_FAIL code=13` 表明未跟踪键漏查被抓到。该运行以预期失败退出 |

完整逐启动串口日志在 [recovery-serial.tar.gz](recovery-serial.tar.gz)；每个 `PASS` 只表示已覆盖的显式软件切点，不穷举全部并发、每条指令、介质损坏或硬件故障。

## 安全复现

**先使用可丢弃的仓库副本和磁盘镜像。** `test-xv6.py`、`make clean`、模式切换、`pmformat --force` 和恢复目标会覆盖/删除测试镜像，不能在含重要数据的工作目录运行。依赖和补丁 QEMU 构建见仓库 `README` 与 [QEMU 设备说明](../../QEMU-NVDIMM.md)。在已安装 RISC-V GCC/binutils、QEMU、Python、`bc` 的副本中：

```sh
make -j2 kernel/kernel fs.img
CPUS=1 ./test-xv6.py efficiency
CPUS=3 ./test-xv6.py efficiency
CPUS=1 ./test-xv6.py vfs
CPUS=3 ./test-xv6.py vfs
CPUS=3 ./test-xv6.py usertests
make test-model
CPUS=3 make test-recovery RECOVERY_ARGS='--log-dir .ci-logs/map-pmfs'
# 上一步构建了 MAP + PMFS_FAULT_INJECT 内核：
python3 tools/vfs_recovery.py --cpus 3 --log-dir .ci-logs/map-vfs
# 使用 docs/QEMU-NVDIMM.md 编译的补丁 QEMU：
make test-recovery-nvdimm CPUS=3 QEMU=/path/to/patched/qemu-system-riscv64
python3 tools/vfs_recovery.py --cpus 3 --nvdimm --qemu /path/to/patched/qemu-system-riscv64
make test-recovery-nvdimm-zicbom CPUS=3 QEMU=/path/to/patched/qemu-system-riscv64
python3 tools/vfs_recovery.py --cpus 3 --nvdimm --zicbom --qemu /path/to/patched/qemu-system-riscv64
```

要复现一个消融，在**另一个干净且可丢弃的副本**中 `git apply docs/results/2026-10-28-efficiency/full-metadata-ablation.patch`，然后 `make clean && CPUS=1 ./test-xv6.py efficiency`；另两个对照分别使用 `no-exact-index-ablation.patch` 和 `linear-posting-ablation.patch`。不要将不同对照补丁叠加。负控补丁会故意引入错误，必须在单独副本运行；其预期是检测到失败，而不是 PASS。

历史独立启动的 1 预热 + 10 次测量及 bootstrap 数据仍保留在[第一阶段记录](../2026-10-28-vfs/benchmark/)；它不是本轮新源码的时间对照，且 xv6 与 PMFS 的设备持久语义不等价。论文可用的当前章节草稿在 [`thesis/latex/chapters/current-evidence.tex`](../../../thesis/latex/chapters/current-evidence.tex)，未接入旧正文、未编译，仍需整体改写与独立复核。
