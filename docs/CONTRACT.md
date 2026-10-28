# PMFS 软件回归契约与故障覆盖

> 所有 `contract*`、基准、`test-recovery*` 的构建会 `make clean` 并重建 `fs.img`，旧 boot 自测还会修改 PMFS。只在可丢弃副本运行。没有任何一项单独证明硬件掉电安全。

## 自测和 API 冒烟

旧阶段行格式为 `ONBOX_PMFS_<STAGE>_PASS` / `..._FAIL stage=<S> code=<C>`；`make contract-full` 启用 `PMFS_SELFTEST`，检查内核 STORE/INDEX/ALGO/CRASH/PERSIST/FILE/CDC/STREAM 等、自测正负例、专用用户 syscall 和带完整计数的基准行。其 `REMOUNT` 同一启动重载，`PMEM2` 两次进程启动只验证共享 RAM 魔数；它们不是文件恢复或物理掉电证明。日志中的 `FILE_FAIL code=12`、`CDC_FAIL code=3`、`STREAM_FAIL code=9` 是被相应 `NEG_*_PASS` 覆盖的预期负例，脚本会检查其上下文。

```sh
make contract-full       # EMUL，自测 + 专用 API + benchmark 冒烟
make contract-pmem       # MAP，自测，普通 RAM，破坏性
make contract-pmem2      # 两次共享 RAM 魔数 smoke，临时文件
```

普通构建不启用这些破坏性 boot 自测；MAP 在空白、旧格式和无效元数据时不会自动格式化。

## 真实进程重启和软件切点 oracle

```sh
make test-recovery CPUS=3 RECOVERY_ARGS="--log-dir .ci-logs/recovery"
make test-recovery-zicbom CPUS=3 RECOVERY_ARGS="--log-dir .ci-logs/zicbom"
# 使用 tools/patches 下的 QEMU 9.2.0 补丁构建独立设备后：
python3 tools/check_nvdimm_device.py --qemu /path/to/patched/qemu-system-riscv64
make test-recovery-nvdimm QEMU=/path/to/patched/qemu-system-riscv64
make test-recovery-nvdimm-zicbom QEMU=/path/to/patched/qemu-system-riscv64
```

`tools/pmfs_recovery.py` 在临时目录使用可丢弃的块盘与 160 MiB 共享 guest RAM，或使用实验性 QEMU 补丁下的独立 32 MiB NVDIMM 文件后端；逐步运行 guest 命令、检查内容，然后 `SIGKILL` 整个 QEMU 进程并用相同后端重启。NVDIMM QMP 检查证明设备类型、地址和独立后端，但不证明物理持久性。Zicbom 测试启用 QEMU CPU 扩展及构建开关。覆盖：

- 空白拒绝挂载与无改写、显式格式化、内存不足拒绝 MAP 且不回退 EMUL；v2、校验正确但越界/重叠 checkpoint、损坏页、双槽损坏拒绝挂载且不改写，单槽损坏回退。
- KV 和文件的已同步内容跨多次新 QEMU 进程字节读回；更新/删除、未提交修改中断、边界/索引/容量、多进程、普通 fd/`dup`/`fork`、shell 重定向和全局 `sync`。
- 测试专用内核中 checkpoint 切点 1–5、两次 checkpoint 的旧/新 oracle，以及文件内容更新的新 run/inode 半页和整页写入切点 6–7；未修改的 dentry 根被复用。替换式 VFS rename 另覆盖两页元数据。每个断点暂停后由宿主杀进程，再检查允许状态。只覆盖这些**显式软件切点**，不穷举指令间位置，也不模拟硬件持久缓存。

脚本最终输出 `PMFS_RECOVERY_PASS boots=... cpus=... zicbom=... nvdimm=...`。测试专用 `PMFS_FAULT_INJECT` 可以故意挂起内核，不应部署生产镜像。日志目录只保存串口文本；测试中的磁盘与 RAM 临时文件被自动清理。

## VFS 语义与子树恢复

```sh
# 默认 EMUL，重建 fs.img；同一镜像不要与其他测试并行使用
CPUS=1 ./test-xv6.py vfs
CPUS=3 ./test-xv6.py vfs
# MAP 测试内核及临时磁盘/共享 RAM；会先 make clean
make test-vfs-recovery CPUS=3 RECOVERY_ARGS="--log-dir .ci-logs/vfs-recovery"
```

功能用例包括跨挂载点路径、cwd/目录引用、长名字、独立打开可见性和并发 append、open/rename 组合、替换与类型限制、描述符耗尽清理、sync/最后 close 竞争；默认 8 MiB EMUL 另反复重命名直到空间耗尽并检查旧路径回滚及同步/关闭失败。重启用例分别检查非替换（一页）和替换（两页）目录子树重命名正常后两次启动、checkpoint 1–5 和各新元数据页 6–7 切点。实际内核提前发布子树的变异负控以及设备模式与实验限制见[优化验证](results/2026-10-28-efficiency/README.md)。

`./test-xv6.py efficiency` 运行正常 fd 相同写入跳过、CDC/固定 fallback、单页/双页快照写入请求计数、512 键后索引溢出回退、旧键更新与冷查询的断言，并测试 `pmstats` 用户工具。计数只代表软件请求，见[读写放大与耐久风险](PERFORMANCE-ENDURANCE.md)。

## 有界持久化规格模型

`make test-model` 运行 `tools/check_persist_model.py` 和 `tools/check_vfs_model.py`。前者独立描述双槽 COW 协议，对三页各两条模拟缓存线及 checkpoint 的每个操作边界枚举所有当前脏线提前写回子集，假设屏障持久化已请求的写回、提交词 8 B 原子且校验无碰撞；成功确认后必须恢复新值。它还要求删除数据刷写、提前发布、删除提交词刷写三个负控找到反例。VFS 模型另枚举两页发布和复用不可变 inode 页两种有界状态，验证旧/新子树，并检测提前发布、部分子树和原地更新等变异。

**限制：** 此模型是手写协议规格，不从内核生成 store/flush trace；页数和线数被有界缩小，不模拟真实缓存、介质、校验碰撞或所有并发 interleaving。其通过和负控有效性不能取代对代码实际执行路径的验证，更不是形式化硬件证明。
