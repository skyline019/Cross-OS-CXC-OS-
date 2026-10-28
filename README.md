# xv6-pmfs（毕设：持久内存文件子系统）

本仓库在 MIT xv6-riscv 上实现 **PMFS v3** 原型：并行于 virtio 根的受限 `/pm` 文件系统 + KV/CDC/stream，以及可复现的提交/故障验收。

> 完整中文状态、限制与文档地图：**[THESIS.md](THESIS.md)**  
> 文档总索引：**[docs/README.md](docs/README.md)**  
> 上游 xv6 说明仍见无扩展名的 [`README`](README) 文件。

## 快速开始

依赖：RISC-V GCC/binutils、`qemu-system-riscv64`、Python 3、`bc`。

```sh
make qemu                 # EMUL：每次启动空的易失 PMFS
make test-model           # 有界协议规格 + 负控
CPUS=3 make test-vfs      # VFS / 硬链接等
make test-recovery CPUS=3 RECOVERY_ARGS="--log-dir .ci-logs/recovery"
```

破坏性目标（`contract*`、部分 recovery/bench）会 `make clean` 并重建 `fs.img`——请在**可丢弃 checkout**运行。

## 诚实边界（必读）

| 说法 | 事实 |
|------|------|
| MAP | 普通 guest RAM，**不是** NVDIMM |
| 实验性 NVDIMM | 仅本仓 QEMU 9.2.0 补丁 + 私有 FDT |
| 默认刷写 | touch + fence，**不是**硬件写回 |
| Zicbom | 可选 `CBO.CLEAN`；TCG 不模拟持久缓存 |
| 杀 QEMU | 进程重启 oracle ≠ 物理掉电 |
| 旧论文 PDF / 旧 BENCH 表 | **不可**直接当新结论提交 |

## 文档入口（精简）

| 文档 | 内容 |
|------|------|
| [docs/SEMANTICS.md](docs/SEMANTICS.md) | 支持语义与差集 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 架构与 checkpoint |
| [docs/CORE-WALKTHROUGH.md](docs/CORE-WALKTHROUGH.md) | 核心代码直讲 |
| [docs/CONTRACT.md](docs/CONTRACT.md) | 测试命令与覆盖 |
| [docs/PMFS-REPAIR-REPORT.md](docs/PMFS-REPAIR-REPORT.md) | 修复与实测表 |
| [docs/results/](docs/results/) | 2026-10-28 各轮原始结果 |

许可与 xv6 致谢见 [`README`](README) / [`LICENSE`](LICENSE)。
