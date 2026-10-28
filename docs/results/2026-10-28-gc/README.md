# 双槽保护回收、COW 压缩与硬链接：软件验证记录

本次变更以 `6d0f21a1d328910be3289be7ab19191a923e01d1` 为基线；设计、故障假设、接口和格式兼容见[维护设计](../../MAINTENANCE.md)。这里的数字是 QEMU TCG 中的**软件请求计数**，不是硬件时延、真实设备流量、物理掉电或磨损数据。此前的[效率记录](../2026-10-28-efficiency/README.md)属于旧版追加分配器，不能把它的 2030 次耗尽结果当作现状。

## 指定工作负载的观测

沙箱使用 QEMU 9.2.0 TCG、Zig 0.13.0/Clang 18.1.6 RISC-V 包装器和 GNU binutils 2.42；环境细节参见[上一轮环境记录](../2026-10-28-efficiency/environment.txt)。以下为本轮 3 hart、默认 8 MiB EMUL 的一次确定性工作负载：

| 检查 | 观测 | 限制 |
|---|---:|---|
| 元数据非替换 rename | 5000 次成功、5000 次旧页复用、tail 高水位 12→12 页 | 不是 5000 次擦除或寿命结论；5000 次扫描中 `traced_runs=0`，软件读请求 480000 B |
| 全链压缩 | 90→19 个 run；688→606 条记录；输出 19 页 | 该操作主动写 77888 软件字节、读 921600 软件字节；需要临时尾部空间 |
| 相同未跟踪冷键 | 候选 run probe 17→2；查询页读取 1→1 | 不是延迟测量，不能把减少 probe 说成减少实际页读取 |
| 源页复用消融 | 逐记录重新读页 3084288 B；最近源页缓存 921600 B | 单次指定压缩的软件 `pmdev_read` 字节下降约 70.1%；[唯一算法差异补丁](no-source-page-cache.patch)，不是硬件读放大 |
| 极小空间负控 | 32 页 EMUL 中 tail 32/32，26 个 run；压缩报 `NOSPACE`、创建失败且无写入 | 测的是预检和失败回滚，不意味着满盘可做 GC |

数据由 `vfstest capacity`、`pmfs_gc basic` 与 `pmfs_gc nospace` 的输出取得；缓存消融两侧执行相同数据和验证，基线 `PMFS_GC_METRIC` 给出 `runs_before=90 runs_after=19 input_records=688 output_records=606 output_pages=19 tail_before=100 tail_after=119 read_bytes=921600 write_bytes=77888`，消融的 `read_bytes=3084288`，写入和查询结果相同。`dev_read_bytes` 覆盖 `pmdev_read` 复制、包括压缩验证及索引重建，不覆盖所有 direct mapping；`dev_write_bytes` 包含设备写请求和 checkpoint 状态字。

## 恢复与负控观察

| 软件测试 | 本地沙箱已观察结果 |
|---|---|
| 默认 EMUL、多核 | `usertests` 的 `ALL TESTS PASSED`；VFS、效率、显式压缩、空间边界及 legacy `contract-full` 通过；原生 xv6 crash/orphan 测试命令成功。 |
| 共享 RAM MAP | VFS 134 次启动；压缩/GC 43 次启动，94→20 run，20 输出页，旧槽退役后 94 个旧 run 页的内容被元数据复用。 |
| 实验 NVDIMM | VFS 134 次启动；压缩/GC 43 次启动，计数同上。 |
| NVDIMM + Zicbom | VFS 134 次启动；压缩/GC 43 次启动，计数同上。TCG 不模拟真实缓存持久域。 |
| 双槽实际内核负控 | [补丁](ignore-fallback-slot.patch)使分配器忽略非活动槽；反复写页后返回错误的测试报告 `aborted metadata allocation overwrote the older checkpoint`。预期失败。 |
| 最新值实际内核负控 | [补丁](keep-oldest-version.patch)令归并错误地保留旧版本；`PMFS_GC_FAIL mode=basic code=10`。预期失败。 |
| 独立有界 GC 模型 | `allocations=3750 root_checks=36000`，忽略旧槽和过早释放旧 run 的负控有反例；模型不是从内核生成的 trace。 |

原始临时测试目录和完整逐启动日志位于沙箱缓存；会话环境重建后该缓存未保留。表中数字是当时工具输出的摘录，**不是**声称这里保存了完整原始串口；重新运行命令或查看 GitHub Actions 的上传制品以取得完整日志。原有 PMFS 62 次启动矩阵在环境重建时仍在运行，本地最终状态未知；GitHub Actions 的结果应另行核对，不能依据本表声称其通过。

VFS 134 次矩阵在增加“两个历史死 chunk”的压缩测试数据之前完成，内核和 VFS 测试逻辑相同；43 次压缩矩阵覆盖该数据更新，包括公开 KV map 比较、活文件及硬链接、内部 chunk 高水标记和重启后的新 chunk、写页回滚、旧链损坏拒绝、旧槽回退及退役后的页复用。进程 kill 与共享普通文件均不证明物理掉电正确。

## 在隔离副本中复现

不要在有用户数据的工作目录运行：下列目标可能 `make clean`、删除并重建 `fs.img` 或格式化 PMFS。安装 RISC-V GCC/binutils、`qemu-system-riscv64`、Python 3 和 `bc` 后：

```sh
make -j2 kernel/kernel fs.img
CPUS=3 ./test-xv6.py vfs
CPUS=3 make test-pmfs-gc
./test-xv6.py efficiency
make test-model
make test-pmfs-gc-nospace              # 重建 32 页 EMUL 内核
make test-pmfs-gc-recovery CPUS=3 \
  RECOVERY_ARGS="--log-dir .ci-logs/gc" # 重建 MAP/fault 内核
python3 tools/vfs_recovery.py --log-dir .ci-logs/vfs
python3 tools/pmfs_recovery.py --log-dir .ci-logs/pmfs
```

实验 NVDIMM 必须先依 [QEMU 说明](../../QEMU-NVDIMM.md) 构建补丁版本；先执行 `make test-recovery-nvdimm QEMU=/path/to/patched/qemu-system-riscv64` 得到对应内核，再用 `tools/vfs_recovery.py --nvdimm --qemu ...` 和 `tools/pmfs_gc_recovery.py --nvdimm --qemu ...`。Zicbom 需要对应内核、`--zicbom` 和 QEMU CPU 支持。`.github/workflows/test.yml` 含两种 NVDIMM 矩阵并上传串口制品。

负控及消融仅在**另一个一次性副本**内分别 `git apply` 对应补丁并完整重建，不能把负控补丁应用到正式实现。正常回归必须通过；前两个负控应被测试拒绝，缓存消融仍应保持正确值而只改变软件工作量。当前检查器未覆盖全部并发 interleaving、真实硬件缓存乱序、介质损坏或 checksum 碰撞。
