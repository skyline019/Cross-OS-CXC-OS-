# 本地沙箱内核测试记录（2026-10-28，修复前基线）

> 本文保留对基线 `d22596a` 的原始测试与失败证据，不能当作 v3 的当前状态；后续修复和重测见[修复与验证报告](PMFS-REPAIR-REPORT.md)。历史源码行号应以 `git show d22596a:<path>` 为准。总索引：[README.md](README.md)。

## 结论

可以在本地沙箱构建和运行 xv6/RISC-V：本次在**隔离副本**中使用自建 QEMU、临时交叉编译包装器和独立镜像完成了启动、契约及用户态回归。它不是仓库默认 GCC 工具链上的复现，不能由此认定真实 NVDIMM 或物理掉电测试已经通过。

| 测试 | 实际结果 | 日志 |
|---|---|---|
| EMUL 严格契约及 `pmfs_probe`、`pmfs_posix`、`pmfs_file` 用户态冒烟，1 vCPU | **通过**，脚本退出 0 | [contract-emul.log](results/2026-10-28-sandbox/contract-emul.log) |
| MAP 严格契约及同一组用户态冒烟，1 vCPU | **通过**，脚本退出 0；重新单独运行确认 | [contract-map.log](results/2026-10-28-sandbox/contract-map.log) |
| xv6 `usertests -q`，EMUL，1 vCPU | **通过**，列出 68 项，末尾 `ALL TESTS PASSED`，退出 0 | [usertests-quick-cpu1.log](results/2026-10-28-sandbox/usertests-quick-cpu1.log) |
| xv6 `usertests -q`，EMUL，3 vCPU | **通过**，列出 68 项，末尾 `ALL TESTS PASSED`，退出 0 | [usertests-quick-cpu3.log](results/2026-10-28-sandbox/usertests-quick-cpu3.log) |
| xv6 完整 `usertests`（含慢速项），EMUL，3 vCPU | **通过**，列出 74 项，末尾 `ALL TESTS PASSED`，退出 0 | [usertests-full-cpu3.log](results/2026-10-28-sandbox/usertests-full-cpu3.log) |
| xv6 原生块文件系统 `test-xv6.py crash`，EMUL，1 vCPU | **通过**，退出 0；日志恢复第 4 次尝试命中，孤儿文件和目录回收均 `OK` | [xv6-native-crash-cpu1.log](results/2026-10-28-sandbox/xv6-native-crash-cpu1.log) |
| 仓库 PMEM2，MAP、整机 RAM 共享文件、1 vCPU | **通过**，退出 0；第二次启动读到预设魔数 | [contract-pmem2.log](results/2026-10-28-sandbox/contract-pmem2.log) |
| 补充的跨 QEMU 进程重启文件/KV 读回测试，原始 MAP 内核 | **失败**，2/2 次未读回；测试退出 1 | [汇总](results/2026-10-28-sandbox/reboot-normal-summary.log)，[试验 1 启动 1](results/2026-10-28-sandbox/reboot-normal-trial1-boot1.log)，[启动 2](results/2026-10-28-sandbox/reboot-normal-trial1-boot2.log)，[试验 2 启动 1](results/2026-10-28-sandbox/reboot-normal-trial2-boot1.log)，[启动 2](results/2026-10-28-sandbox/reboot-normal-trial2-boot2.log) |
| 仅作定位：隔离副本中禁用 PMFS 启动自测后重复读回 | **失败**，2/2 次未读回文件内容及 KV；测试退出 1 | [汇总](results/2026-10-28-sandbox/reboot-diagnostic-summary.log)，[试验 1 启动 1](results/2026-10-28-sandbox/reboot-diagnostic-trial1-boot1.log)，[启动 2](results/2026-10-28-sandbox/reboot-diagnostic-trial1-boot2.log)，[试验 2 启动 1](results/2026-10-28-sandbox/reboot-diagnostic-trial2-boot1.log)，[启动 2](results/2026-10-28-sandbox/reboot-diagnostic-trial2-boot2.log) |

`usertests` 测的是常规 xv6 系统调用，3 vCPU 增加了调度并发覆盖，但不等于 PMFS 多核安全证明。原生 `crash` 测的是 xv6 日志及孤儿 inode 的 QEMU 进程终止后恢复，不是 PMFS 崩溃矩阵：其日志恢复脚本允许重试 20 次，本次前三次未观察到 `recovering`，第 4 次命中并通过，并非穷举故障时机。契约日志中的 `ONBOX_PMFS_FILE_FAIL stage=FILE code=12`、`CDC_FAIL code=3`、`STREAM_FAIL code=9` 是代码有意输出的负例，后面分别有 `NEG_*_PASS`。`usertests` 中用于验证非法访问的 `usertrap()`，以及慢速 `diskfull` / `outofinodes` 中资源耗尽的提示也是预期现象；套件最终判定为 `ALL TESTS PASSED`。

## 跨启动读回实验的含义

新写的测试程序在首启执行 `pmfs_put` / `pmfs_seal`，以及 `pmfs_create` / `pmfs_fwrite` / `pmfs_fsync` / 关闭再打开；检查 put、seal、create、open、write、fsync 和读操作的结果，并逐字节核对 **18 字节 KV** 和 **20 字节文件**，均成功后回到 shell（未单独检查 close 返回值）。然后强杀 QEMU，以**同一个** 160 MiB `memory-backend-file,share=on` 文件启动新进程，在第二次 shell 中尝试读回。每次试验都使用新建的临时 RAM 文件，两次试验彼此独立。

- 原始内核的两次试验：首启均为 `PROBE_WRITE_OK kvlen=18 filelen=20 sameboot=1 sync=0`；第二次启动均出现 `ONBOX_PMFS_PMEM2_PASS persist=ok`，但 `pmfs_get` 返回未命中（`khit=0`），`pmfs_fopen` 返回 `-1`，整体 `PROBE_READ_FAIL`。
- 定位用内核的两次试验：只在隔离副本应用[此补丁](results/2026-10-28-sandbox/diagnostic-no-boot-selftests.patch)，保留 mount、`pm_algo_init`、`pm_algo_rebuild_index` 和 `pmfile_init`，跳过 `pmfs_init` 里的启动自测和契约 gate。第二次启动文件能打开（`fd=0`），但 `pmfs_fread` 返回命中且长度 0，KV 仍未命中；整体仍为 `PROBE_READ_FAIL`。此变体**不是未修改内核的测试结果**。

代码中，[`pmfile_fault_selftest`](../kernel/pmfile.c#L699-L790) 启动时会清空 inode/dentry 并重写元数据，解释了默认启动对已有文件路径的破坏；[`pm_algo_init`](../kernel/pmalgo.c#L79-L87) 清空运行时 run 描述符，而 [`pm_algo_rebuild_index`](../kernel/pmalgo.c#L373-L406) 只遍历内存中的 run 列表，与去除启动自测后内容仍读不到的现象一致。修复后还需要独立回归，不能只凭这次定位宣称其他恢复问题已排除。

这是**进程重启**实验：共享普通文件及宿主页缓存仍在，未模拟宿主电源故障、缓存逐出或乱序。现有 `pmem_flush` 只做缓存行读取及屏障，不是真正的缓存行写回。当前 QEMU 命令也没有创建独立 NVDIMM 设备。上述文件/KV 测试是针对当前专用 PMFS syscall，不是普通 POSIX 文件接口。契约中的同启动 `REMOUNT_PASS` 和 PMEM2 魔数通过都不能替代这个读回检查。

## 环境与偏差

- 源码基线：`d22596aec697b85c34e560921df4571ce6f60f7f`；正式契约和回归所用的内核源码未修改，测试仅运行在无 `.git` 的独立副本。定位变体使用的源码差异见上方补丁，未写入原内核源码。
- Debian 12 x86-64；QEMU **9.2.0**、RISC-V `virt`、TCG 软件模拟，1 或 3 个 guest vCPU。EMUL 使用 `-m 128M`；MAP 使用 `-m 160M`，其中 PMEM 是预留普通 guest RAM。
- 沙箱原先缺 RISC-V GCC、QEMU 和 `bc`，且常规系统包源不可达。临时安装 Zig **0.13.0**（Clang/LLD **18.1.6**），编译 GNU binutils **2.42** 的 RISC-V `objdump`/`objcopy`，从源码构建 QEMU **9.2.0** 和 `bc` **6.7.6**。
- 临时 `gcc` 包装器将 `-march=rv64gc` 改为 `-target riscv64-freestanding -mcpu=generic_rv64+m+a+f+d+c+zicsr+zifencei`，并加 `-Wno-typedef-redefinition -fno-sanitize=all`。Zig 默认 UBSan 会因 [`riscv.h`](../kernel/riscv.h#L213) 的 `(1L << 63)` 触发 trap；关闭该 sanitizer 是工具链适配，不证明表达式可移植。
- 临时 LLD 包装器仅对 `user/user.ld` 增加 `--no-rosegment -e start`：默认 LLD 曾生成 xv6 `exec` 不接受的非页对齐 LOAD 段并引发 `panic: exec`；修正后通过回归。特殊的 `_forktest` 链接不受影响。
- Zig 生成的带调试信息 `_usertests` 曾超过 xv6 文件大小上限；构建 `fs.img` 前对用户 ELF 使用 GNU `objcopy --strip-all`，保留可加载段。故本次不等价于项目默认 GCC 和未 strip 构建。编译选项见 [MAP 内核构建日志](results/2026-10-28-sandbox/map-build.log)，包装器、版本和二进制/源码包哈希见 [environment.txt](results/2026-10-28-sandbox/environment.txt)。自建 QEMU 的 GLib 配置依赖当时取到的未固定提交 gvdb `main`；归档哈希已记录，但这不是完整锁定的、一键重建环境。
- 早期失败记录：[内核未输出契约](results/2026-10-28-sandbox/early-failed-contract-before-sanitizer-adjustment.log)及[启动指令诊断](results/2026-10-28-sandbox/early-boot-ubsan-diagnostic.txt)、[LLD 调整前的 `panic: exec`](results/2026-10-28-sandbox/early-failed-contract-before-lld-adjustment.log)、[strip 前超过 `MAXFILE` 的构建](results/2026-10-28-sandbox/early-failed-build-before-strip.log)。它们是已通过临时适配绕过的早期失败，不是上表最终状态。
- 全部大型构建、磁盘和 RAM 镜像都留在临时缓存，不加入仓库；上表日志是实际输出。它们包含的时延/`cycles` 仅为自测输出，**不是公平性能实验**。

## 本次执行的测试命令

下面 `B` 是当时的临时缓存目录，已经预先构建好 EMUL、MAP 及独立镜像；并非承诺该目录会长期保存。`test-xv6.py` 重建 `fs.img`，`contract_pmem2.py` 重建 `guest-ram.img`，只能在副本运行。

```sh
B=/home/user/.cache/pmfs-sandbox
export PATH="$B/bin:$B/deps/bin:$PATH" TOOLPREFIX=riscv64-test- CPUS=1

cd "$B/xv6"  # 已构建 EMUL 内核、用户程序已 strip
python3 tools/contract_pmfs.py --strict --probe --posix --file --timeout 60
python3 test-xv6.py -q usertests

cd "$B/xv6-smp"  # 独立 EMUL 副本
CPUS=3 python3 test-xv6.py -q usertests
CPUS=3 python3 test-xv6.py usertests  # 不加 -q，包含慢速项

cd "$B/xv6-crash"  # 独立 EMUL 副本；仅测试 xv6 原生块文件系统
python3 test-xv6.py crash

cd "$B/xv6-map"  # 已 clean，按默认 CFLAGS + -DPMEM_MAP 重建内核
python3 tools/contract_pmfs.py --pmem --strict --probe --posix --file --timeout 60
python3 tools/contract_pmem2.py

python3 "$B/reboot-probe-run.py" --root "$B/xv6-reboot-probe" \
  --qemu "$B/qemu-build/qemu-system-riscv64" --log-dir "$B" --label reboot-normal
# 退出 1：读回失败，而非脚本成功
python3 "$B/reboot-probe-run.py" --root "$B/xv6-reboot-diagnostic" \
  --qemu "$B/qemu-build/qemu-system-riscv64" --log-dir "$B" \
  --label reboot-diagnostic --boot-marker PROBE_BOOT_NO_SELFTEST
# 退出 1：定位变体读回仍失败
```

## 复做补充读回实验

以下是已备好 RISC-V GCC/binutils、`qemu-system-riscv64`、Python、`bc` 的 Linux 主机上的复做路径；步骤已用本次 Zig 包装器在新副本重跑，标准 GCC 环境尚未验证。该命令会在复制品里 `make clean`、重建 `fs.img` 并强杀其 QEMU；**不要在原仓库或任何有价值的镜像上运行**。不同编译器/QEMU 需另行记录版本及结果，不能视为与本次二进制完全相同。

```sh
ROOT="$(pwd)"  # 原仓库根目录
W="$(mktemp -d)"
tar --exclude=.git --exclude=fs.img --exclude=guest-ram.img -C "$ROOT" -cf - . | tar -C "$W" -xf -
cd "$W"
export TOOLPREFIX=riscv64-unknown-elf-  # 按本机工具链调整
cp "$ROOT/docs/results/2026-10-28-sandbox/pmreboot.c" user/pmreboot.c
make clean

# 与当前 Makefile 的 UPROGS 列表一致，并额外加入实验程序。
PROGS="$(python3 - <<'PY'
import re
from pathlib import Path
s = Path('Makefile').read_text().split('UPROGS=', 1)[1].split('\n\n', 1)[0]
print(' '.join('user/' + n for n in re.findall(r'\$U/(_\w+)', s)))
PY
)"
CFLAGS="$(printf 'print-flags:\n\t@echo $(CFLAGS)\n' | make -s --no-print-directory -f Makefile -f - print-flags)"
make CFLAGS="$CFLAGS -DPMEM_MAP" kernel/kernel
make mkfs/mkfs $PROGS user/_pmreboot
for p in $PROGS user/_pmreboot; do "${TOOLPREFIX}objcopy" --strip-all "$p"; done
mkfs/mkfs fs.img README $PROGS user/_pmreboot

python3 "$ROOT/docs/results/2026-10-28-sandbox/reboot-probe-run.py" \
  --root "$W" --log-dir "$W/logs" --label normal --trials 2
# 正常内核当前预期退出 1（读回失败）；每轮使用独立临时 RAM 文件。
```

定位变体另用一个副本，应用诊断补丁并重建内核；它仅用于区分启动自测影响，不能代替原始内核结果：

```sh
WD="$(mktemp -d)"
cp -a "$W/." "$WD/"
cd "$WD"
patch -p1 < "$ROOT/docs/results/2026-10-28-sandbox/diagnostic-no-boot-selftests.patch"
CFLAGS="$(printf 'print-flags:\n\t@echo $(CFLAGS)\n' | make -s --no-print-directory -f Makefile -f - print-flags)"
make CFLAGS="$CFLAGS -DPMEM_MAP" kernel/kernel
python3 "$ROOT/docs/results/2026-10-28-sandbox/reboot-probe-run.py" \
  --root "$WD" --log-dir "$WD/logs" --label diagnostic --trials 2 \
  --boot-marker PROBE_BOOT_NO_SELFTEST
# 当前预期退出 1（读回失败）。
```

两个复做步骤均在本次 Zig 包装器环境下另用新副本验证过各一轮，分别仍得到 `fd=-1` 和 `fd=0, flen=0`；[原始内核复做输出](results/2026-10-28-sandbox/recipe-validation-summary.log)与[定位变体复做输出](results/2026-10-28-sandbox/recipe-validation-diagnostic-summary.log)随记录保存。
