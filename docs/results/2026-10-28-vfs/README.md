# VFS 第一阶段：实现、软件故障与复现实验记录

日期 2026-10-28。代码基于 `363c7ca02445c4f46cf7c0d4a9593be2213a69c1` 的本轮未提交修改；测试在独立副本及镜像运行。沙箱使用 QEMU 9.2.0 TCG、自建 GNU RISC-V `objcopy`/`objdump` 和 Zig/Clang RISC-V 编译包装器，**不是** Linux CI 的 GNU RISC-V GCC，也不是硬件实测。普通 MAP 是共享 guest RAM；实验 NVDIMM 使用仓库补丁 QEMU 和普通文件后端。

## 本阶段实现及不变量

- 原生根保留，`/pm` 是固定合成挂载点；组件逐个查找，PM 根的 `..` 返回原生根；PM cwd 和目录 fd 按 inode pin，引用在 `fork`、退出和关闭中维护。`ls` 使用不截断 PM 名称的统一 `getdents` ABI。
- 多个独立普通 fd 在同一 PM inode 上共享工作缓冲及脏状态，却各有偏移；`dup`/`fork` 共享打开对象及偏移。PMFS sleep lock 串行化读写、末尾选取、append 和同步。普通 fd 与专用 PM 句柄互斥；打开文件不支持 unlink。
- PM 内目录 rename：检查目标类型/引用/非空、拒绝自身后代及所有超长新路径；保存旧表，重写子树，最后一次 COW checkpoint 发布。普通失败恢复内存旧表；未被新根引用的追加页可能泄漏。目录/cwd 的 inode 引用不随路径名改变。
- 该有界设计仍只有 40 inode（含根）、8 个底层打开槽、1024 B 文件、27 B 的 PM 后端完整路径、追加式分配且不回收。原生/跨后端 rename、PM 硬链接、打开后 unlink、动态挂载、权限、符号链接和 `mmap` 等没有实现。具体语义见 [SEMANTICS](../../SEMANTICS.md)。

## 实测矩阵

| 检验 | 已观察结果 | 证据 |
|---|---|---|
| 基础 VFS + 空间耗尽，1 核 | `VFS_TEST_PASS mode=basic`；1,013 次成功 rename 后空间不足，旧路径回滚，`VFS_CAPACITY_PASS` | [日志](vfs-cpu1.log) |
| 基础 VFS + 空间耗尽，3 核 | 同上；包含 3 个进程各 20 次独立打开 append 的结果与计数 | [日志](vfs-cpu3.log) |
| MAP 中 rename 切点 + 重启，3 核 | `VFS_RECOVERY_PASS boots=24 ... nvdimm=0 zicbom=0` | [汇总](vfs-map-recovery.log)、[串口](vfs-map-serial.tar.gz) |
| 独立实验 NVDIMM，3 核 | QMP 确认 `nvdimm`、`addr=0x88000000`、32 MiB 和 `/objects/nvd`；同一 rename oracle 24 次启动通过 | [QMP](nvdimm-qmp.log)、[汇总](vfs-nvdimm-recovery.log)、[串口](vfs-nvdimm-serial.tar.gz) |
| 实验 NVDIMM + Zicbom，3 核 | 24 次启动通过且 guest 确认指令路径已启用 | [汇总](vfs-nvdimm-zicbom-recovery.log)、[串口](vfs-nvdimm-zicbom-serial.tar.gz) |
| PMFS 既有恢复 oracle，3 核 MAP | 新内核下 66 次启动通过（包括 KV、文件、坏槽、故障切点和容量） | [日志](pmfs-map-recovery.log) |
| 原生 xv6 回归 | 最新内核三核完整 usertests `ALL TESTS PASSED`；单核原生日志五切点与 orphan 重启通过 | [usertests](usertests-cpu3.log)、[日志/orphan](native-crash-cpu1.log) |
| 原有 PMFS 完整契约 | `contract-full`（含 legacy 自测、专用 API、匹配数基准）通过 | [日志](contract-full.log) |
| 手写规格模型与负控 | 既有双槽模型 64 状态，新目录根模型 14 个有界状态；各 3 种变异均产生反例 | [日志](models.log) |
| **实际内核变异** | 提前在源目录更新后发布尚未完成的子树；正常 rename 完成但 checkpoint 4 发生中断后恢复失败，oracle 以非零退出发现 | [变异补丁](early-subtree-publication.patch)、[日志](early-subtree-publication.log)、[串口](negative-serial.tar.gz) |
| 小规模软件基准 | 1 次预热 + 10 次独立 QEMU 启动，每次 5 组、每组 8 个事务；保存原始 ticks 和按启动重采样的描述性区间 | [配置与 hash](benchmark/metadata.json)、[原始样本](benchmark/raw.jsonl)、[统计](benchmark/summary.json)、[串口](benchmark-serial.tar.gz) |

基础 VFS 用例还检查真正的 `missing/../x` 查找、长名、cwd 继承、打开目录的引用、目录 fd offset 共享、越界 seek、truncate、独立打开可见性、打开的脏文件被 rename、替换非空/已打开目录失败，以及描述符耗尽后底层槽释放。容量测试只用于默认 8 MiB EMUL，不用于 30 MiB MAP 构建。

`tools/vfs_recovery.py` 对正常完成后启动两次验证完整新子树及无关文件；checkpoint 切点 1–3 期望旧子树，4–5 期望新子树；两页元数据的半页/全页切点 6–7 期望旧子树。负控的内核修改仅在隔离副本中构建，未应用到生产代码；恢复失败（拒绝损坏图）是被检测出的结果，不能把该失败称作正确变体的数据无损恢复。

基准测量同样包括 PM 与原生侧的名称不存在检查、创建、写、`fsync`、关闭、重开、完整字节/EOF 校验及删除；每组内部固定 N 并交替执行顺序。10 个测量启动是同一受控程度有限的沙箱主机上的顺序重启，并非 10 台独立机器。每次启动先对 5 次结果取中位数，再对 10 个启动中位数以固定种子做 2,000 次整启动重采样；输出是每组 `r_time` tick，**不是** cycles、ops/s 或 p99。guest 的原生 `fsync` 和 PMFS 的持久语义不等价，不报告或外推硬件加速；当前仅 EMUL 小文件且无完整路径成本归因。输入二进制 hash 保存于配置中；隔离副本没有 `.git`，故 JSON 中 git 字段为 null，来源基线在本页列出。

## 复现（使用可丢弃 checkout）

下面的构建/测试可能 `make clean` 或重建 `fs.img`；不要在包含重要数据的工作目录上运行，测试期间不要共享镜像。

```sh
make -j2 kernel/kernel fs.img                 # 默认 EMUL，非自测构建
CPUS=1 ./test-xv6.py vfs
CPUS=3 ./test-xv6.py vfs
CPUS=3 ./test-xv6.py usertests
python3 -m unittest discover -s tools -p test_bench_repro.py
python3 tools/bench_repro.py --runs 10 --warmup 1 --n 8 \
  --build-notes '记录编译器和 flags' --output /tmp/pmfs-bench-new
make test-model
make test-vfs-recovery CPUS=3 RECOVERY_ARGS='--log-dir .ci-logs/vfs-map'
# 测试专用 MAP 内核构建后：
python3 tools/pmfs_recovery.py --cpus 3 --log-dir .ci-logs/pmfs-map
# 先按 docs/QEMU-NVDIMM.md 构建补丁 QEMU；以下目标各自 clean 重建：
make test-recovery-nvdimm QEMU=/path/to/patched/qemu-system-riscv64
python3 tools/vfs_recovery.py --nvdimm --qemu /path/to/patched/qemu-system-riscv64
make test-recovery-nvdimm-zicbom QEMU=/path/to/patched/qemu-system-riscv64
python3 tools/vfs_recovery.py --nvdimm --zicbom --qemu /path/to/patched/qemu-system-riscv64
```

真实内核负控仅在另一个 disposable 副本执行：`git apply docs/results/2026-10-28-vfs/early-subtree-publication.patch`，运行上面的 `make test-vfs-recovery`，预期 oracle 在 `checkpoint-4-new` 非零退出；不要在正常工作树或用户镜像上应用变异。

## 推论边界

所有 `SIGKILL` 测试只覆盖插桩软件切点及共享文件的进程重启语义；QEMU TCG 的 Zicbom 路径不模拟持久缓存层级。规格模型没有从真实内核提取 store/flush/drain 指令，也没有覆盖所有并发调度、校验碰撞或真实硬件故障。实际内核负控增强了 oracle 的针对性，但不意味着所有实现缺陷都会被发现。没有物理掉电或真实持久域验证，也没有证据证明算法首创或性能优势；后续缺口见[路线图](../../EXCELLENCE-ROADMAP.md)。
