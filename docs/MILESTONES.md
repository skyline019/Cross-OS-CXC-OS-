# 里程碑与验收门（PMFS v3）

> 旧原型 smoke（M0–M6）保留为历史；**当前验收以本表 + [CONTRACT.md](CONTRACT.md) + [results/](results/) 为准。**  
> 12 周规划与基线审计见 [EXCELLENCE-ROADMAP.md](EXCELLENCE-ROADMAP.md)。

## 当前产品形态

并行于 virtio 根的受限 **`/pm` 文件子集** + KV/CDC/stream + 双槽 COW checkpoint；  
**不是**完整 VFS/POSIX、**不是**未声明假设下的物理掉电证明。

## 里程碑矩阵

| ID | 内容 | 主要判据 | 状态口径 |
|----|------|----------|----------|
| V0 | v3 格式、安全挂载、显式 `pmformat --force` | 空白/v2/损坏拒绝挂载；[PMFS-REPAIR-REPORT](PMFS-REPAIR-REPORT.md) | 已有证据 |
| V1 | 受限普通 fd + `/pm` | [SEMANTICS](SEMANTICS.md)；`test-vfs` | 已有证据 |
| V2 | 硬链接、子树 rename、seek/append | [results/2026-10-28-vfs](results/2026-10-28-vfs/README.md) | 已有证据 |
| V3 | 进程重启 + 软件切点 oracle（MAP/Zicbom/补丁 NVDIMM） | `make test-recovery*`；`PMFS_RECOVERY_PASS` | 已有证据（≠物理掉电） |
| V4 | 有界规格模型 + 负控 | `make test-model` | 已有证据（有界规格） |
| V5 | 页复用 + 显式 COW 压缩 | `test-pmfs-gc*`；[MAINTENANCE](MAINTENANCE.md) | 已有证据（近满可失败） |
| V6 | 对齐工作量 bench + 软件消融 | [results/2026-10-28-perf](results/2026-10-28-perf/README.md)、[efficiency](results/2026-10-28-efficiency/README.md) | 已有证据（持久语义仍不等价） |
| V7 | 源级轨迹重放 | `make test-trace-replay`；[TRACE-REPLAY](TRACE-REPLAY.md) | 已有证据（非指令级） |
| P | 论文按 v3 重写 + 新图 | [thesis/latex/main.pdf](../thesis/latex/main.pdf) | **正文已重写**；待学校模板/导师审阅 |
| H | 导师确认设备/题面产品形态 | 书面确认 NVDIMM 补丁路线与 `/pm` 并行子集 | **待确认** |

## 历史原型切片（仅追溯）

| ID | 内容 | 判据 |
|----|------|------|
| M0–M4 | 旧 `ONBOX_PMFS_*` / MAP / CRASH / syscall / bench | 旧 `contract*` |
| M1b | PMEM2 共享 RAM 魔数双启动 | `contract-pmem2`（非掉电） |
| M5–M6 | 旧 CDC 消融 / REMOUNT·FAULT 自测 | [ABLATION.md](ABLATION.md) 历史表 |

## 答辩验收最小集（建议）

```sh
make test-model
CPUS=3 make test-vfs
make test-recovery CPUS=3 RECOVERY_ARGS="--log-dir .ci-logs/recovery"
# 可选：补丁 QEMU
# make test-recovery-nvdimm QEMU=/path/to/patched/qemu-system-riscv64
```

叙事：**可复现的提交协议 + 故障 oracle + 诚实假设**；性能只报对齐工作量的软件路径数字，并写清持久语义不等价。
