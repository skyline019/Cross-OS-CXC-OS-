# 原生 xv6 块日志：CI 失败与确定性恢复测试

## 观察到的失败

合并后的 [Linux CI run 40658535773](https://github.com/skyline019/Cross-OS-CXC-OS-/actions/runs/40658535773) 在 `xv6 block-log recovery` 失败。原始输出显示 `log attempt 20` 后为 `FAIL`：旧脚本每次让 `logstress` 跑两秒后杀死 QEMU，重启后只寻找 `recovering`，20 次都没观察到该标记就失败。它不能保证杀进程时日志头正处在已提交、尚未清除的窗口。该次失败没有保存磁盘/完整串口证据，**无法仅据此判断当时是没命中窗口还是有其他日志问题**；不能因为此前偶然通过就宣称内核正确。

## 测试如何工作

`make test-xv6-crash` 干净重建只用于测试的 `-DXV6_LOG_FAULT_INJECT` 内核。用户程序先写入并关闭旧内容和一个无关保护文件，再发起一次跨三个数据块、但属于同一事务的覆盖写。内核在下列边界输出标记并停住，宿主看到标记后立刻 `SIGKILL` QEMU，使用同一个临时磁盘镜像重启。没有定时重试：没有到达标记、数据不对或恢复行为不对都会失败。

| 切点 | 主机检查崩溃时的日志/主块 | 重启后应见 |
|---|---|---|
| 1. 日志数据写完、提交头之前 | 日志头为空 | 旧值，无重放 |
| 2. 提交头写完、安装主块之前 | 提交头有效，多个主块仍未安装 | 新值，完整重放 |
| 3. 安装第一个主块之后 | 已安装及待安装主块均存在 | 新值，完整重放 |
| 4. 全部主块安装后、清头之前 | 所有主块与日志一致 | 新值，幂等重放 |
| 5. 清头之后 | 日志头为空 | 新值，无重放 |

测试检查每一条 `recovering tail/dst` 与崩溃时磁盘日志头一致、日志头最终清零、文件长度及每个字节、保护文件内容，并在第二次重启验证数据与日志状态仍稳定。切点 2 另复制镜像、仅在该副本清除提交头以模拟丢失重放；数据检查必须明确报错，作为负控。宿主单元测试也检查错误头、越界/重复目标、部分安装与重放顺序。`logstress` 保留为单独的 3 核并发写入及读回测试：修正了旧版 500 字节缓冲区却写入 2000 字节的越界，以及单文件写入量超过 xv6 文件上限的问题。

孤儿文件和目录测试仍然执行，先等待 shell 提示再发送命令，并只匹配完整串口行；然后核对对应 inode，在完成 `fsinit` 后再重启一次，确认不会重复回收。这避免慢启动时提示符与程序输出交错、或分段输出被提前匹配。普通内核的 `xv6_log_fault` 一律返回 `-1`，不包含停机切点；CI 单独验证默认构建确实拒绝注入。测试内核可被该接口刻意停住，**不要用作正常运行内核**。切点只覆盖所示软件边界，不是所有可能的机器状态或并发交错。

## 隔离沙箱验证（2026-10-28）

在独立副本中，未修改工作目录的原始镜像；测试环境是自建无图形 QEMU 9.2.0 TCG、Zig/Clang 和 LLD 包装器，并对用户 ELF `--strip-debug`，不是 GitHub 的 GNU RISC-V GCC 环境。完整版本、编译包装器和哈希见[环境记录](results/2026-10-28-xv6-ci/environment.txt)，原始日志在同一目录。

- 默认内核注入开关拒绝测试通过；对默认内核运行故障测试按预期失败，测试前后源 `fs.img` 哈希一致，见[负控](results/2026-10-28-xv6-ci/default-control.log)。宿主解析器[7 项单元测试](results/2026-10-28-xv6-ci/oracle-unit-tests.log)通过。
- 从干净构建运行 `CPUS=1 make test-xv6-crash` 通过，含五个切点、错误重放负控和文件/目录孤儿回收：[日志](results/2026-10-28-xv6-ci/cpu1-clean-build-crash.log)、[逐启动串口](results/2026-10-28-xv6-ci/serial-cpu1.tar.gz)。1 核和 3 核五切点测试分别再重复 5 次全部通过（每轮 16 次启动），含负控；3 核[串口](results/2026-10-28-xv6-ci/serial-cpu3.tar.gz)。
- 两个**实际内核变异负控**只应用于独立副本：移除重放后，第 2 切点读回报错；提前写提交头后，第 1 切点的磁盘头检查报错。两者都按预期被发现，补丁和预期失败日志分别为 [no-replay](results/2026-10-28-xv6-ci/mutation-no-replay.patch) / [日志](results/2026-10-28-xv6-ci/mutation-no-replay-expected-failure.log)、[early-commit](results/2026-10-28-xv6-ci/mutation-early-commit.patch) / [日志](results/2026-10-28-xv6-ci/mutation-early-commit-expected-failure.log)。不要将负控补丁用于正常内核。
- 3 核文件/目录孤儿回收各重复 5 次、6 个写进程的 240,000 字节并发读写各重复 5 次，均通过；日志分别为 `cpu3-orphan-{1..5}.log` 和 `cpu3-stress-{1..5}.log`。
- 原生完整 `usertests` 3 核 74 项及新版驱动的快速 68 项均 `ALL TESTS PASSED`；完整测试执行于最终 shell 等待修正之前，底层内核相同。[完整日志](results/2026-10-28-xv6-ci/cpu3-usertests-full.log)、[快速日志](results/2026-10-28-xv6-ci/cpu3-usertests-quick.log)。
- PMFS `contract-full`、普通共享 RAM 重启/故障 oracle（3 核、66 次启动）及有界持久模型/三个负控通过；分别见[契约](results/2026-10-28-xv6-ci/pmfs-contract-full.log)、[恢复](results/2026-10-28-xv6-ci/pmfs-recovery.log)、[模型](results/2026-10-28-xv6-ci/persist-model.log)。契约中的指标只是 TCG 冒烟，不是物理设备性能。

## 运行与诊断

下面命令需要 RISC-V 工具链、QEMU、Python 和 `bc`。**仅在可丢弃 checkout 运行**：`make test-xv6-crash` 执行 `make clean` 并重建 `fs.img`，孤儿测试和 `test-xv6.py stress` 也会重建它。恢复 oracle 本身只修改镜像的临时副本；这不改变构建步骤会覆盖原镜像的事实。

```sh
make -j2 kernel/kernel fs.img
CPUS=1 python3 tools/xv6_log_recovery.py --expect-disabled
CPUS=1 XV6_LOG_DIR=.ci-logs/xv6-cpu1 make test-xv6-crash
# 使用上一步生成的插桩内核再验证 3 核：
CPUS=3 XV6_LOG_DIR=.ci-logs/xv6-cpu3 ./test-xv6.py log
CPUS=3 ./test-xv6.py stress
python3 -m unittest discover -s tools -p test_xv6_log_recovery.py
```

仅运行 `./test-xv6.py crash` 或 `log` 而没有先构建插桩内核会失败，并提示使用测试内核；不应忽略这个失败。CI 将每次启动的串口输出以及失败时的 `test-xv6.out` 上传为 `linux-test-serial-logs`，保留 14 天；实际运行结果以 GitHub Actions 为准。

这些是 QEMU 普通文件后端的**进程终止/重启**测试。宿主页缓存仍存在，不模拟物理断电、扇区撕裂、设备缓存失效或磁盘写入乱序；它不等于 PMFS 或真实 NVDIMM 的持久性验证。
