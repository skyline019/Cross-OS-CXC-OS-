# QEMU 后端、实验性 NVDIMM 与 Zicbom

> **未修改的上游 RISC-V `virt` MAP 仍是普通 RAM。** 本仓另提供 QEMU 9.2.0 实验补丁以冷插拔独立 `nvdimm`。普通文件后端和 QEMU 进程杀启不等于真实 NVDIMM 硬件或物理掉电。

| 模式 | 构建/运行方式 | 可验证 | 不可据此验证 |
|---|---|---|---|
| EMUL | 默认 `-m 128M`，内核静态 8 MiB 缓冲 | 同启动的软件行为 | 重启保留、硬件持久化 |
| 普通 MAP | `-DPMEM_MAP`、`-m 160M`，额外普通 RAM | 地址窗口与运行时行为 | 独立 NVDIMM、掉电持久化 |
| 共享 RAM 文件 | MAP + 160 MiB `memory-backend-file,share=on` | 杀 QEMU、复用同一整机 RAM 文件的进程重启 | 宿主电源故障、独立设备、缓存持久域 |
| **实验 NVDIMM** | 本仓 QEMU 9.2.0 补丁，`-device nvdimm`，独立 32 MiB 共享文件 | QMP 设备身份/地址/后端、冷插拔发现和文件进程重启 | 上游通用 virt 支持、NFIT、热插拔、硬件掉电 |
| 可选 Zicbom | `-DPMEM_ZICBOM`、兼容 CPU；测试用 QEMU `-cpu rv64,zicbom=true` | 指令编码和 TCG 执行路径 | 真实缓存写回和到达持久域 |

`kernel/pmem.c` 默认逐缓存行读取并 fence，是统计/插桩而非硬件写回。Zicbom 路径发出 `CBO.CLEAN`，假定 64 B 缓存块并设置权限；QEMU 9.2 的 [RISC-V CBO TCG helper](https://github.com/qemu/qemu/blob/v9.2.0/target/riscv/op_helper.c) 不模拟缓存层级。真实平台必须分别确定扩展支持、块大小、权限、写回完成、持久域、故障原子性和持久顺序。

## 实验 QEMU 补丁

[补丁](../tools/patches/qemu-9.2.0-riscv-pmfs-nvdimm.patch) 在 QEMU 9.2.0 `virt` 中注册现有 `nvdimm`/`pc-dimm` 实现，仅支持：

- `-machine virt,nvdimm=on,acpi=off -m 128M,maxmem=160M,slots=1`；冷插拔一个 `nvdimm`，固定 `[0x88000000,0x8a000000)`，普通 RAM 为前面的 128 MiB。其他大小、位置、自定义 DTB 和动态热插拔被拒绝。
- 设备有独立 `memory-backend-file`；QEMU 生成私有 `/pmem@88000000` FDT 节点，`compatible="xv6,pmfs-nvdimm"`、`reg=<地址,大小>`。这不是标准 ACPI/NFIT 或通用 NVDIMM 命名空间发现。
- `PMEM_NVDIMM` 内核在 `kalloc` 前读取并验证 FDT 范围、设备节点、地址和大小；缺失时拒绝挂载且不回退为 EMUL。使用可信固件和该固定布局，不声称支持任意 DTB/设备树。
- `tools/check_nvdimm_device.py` 通过 QMP 验证设备类型为 `nvdimm`、独立 memdev、32 MiB 地址及 128 MiB 普通 RAM。进程重启 oracle 使用临时独立后端，而不是整机共享 RAM。

构建补丁 QEMU（下载自官方发布，以下目录必须是可丢弃副本）：

```sh
curl -fLO https://download.qemu.org/qemu-9.2.0.tar.xz
tar -xf qemu-9.2.0.tar.xz
patch -d qemu-9.2.0 -p1 < tools/patches/qemu-9.2.0-riscv-pmfs-nvdimm.patch
mkdir qemu-build && cd qemu-build
../qemu-9.2.0/configure --target-list=riscv64-softmmu --disable-docs --disable-werror
ninja -j2 qemu-system-riscv64
cd ..
python3 tools/check_nvdimm_device.py --qemu "$PWD/qemu-build/qemu-system-riscv64"
make test-recovery-nvdimm QEMU="$PWD/qemu-build/qemu-system-riscv64"
make test-recovery-nvdimm-zicbom QEMU="$PWD/qemu-build/qemu-system-riscv64"
```

上述 `make` 目标会 `make clean` 并重建工作目录的 `fs.img`，请只在 disposable checkout 执行；测试器为每次实验创建并清理临时磁盘及 NVDIMM 文件。正常 MAP 中 `pmformat --force` 也会破坏性擦除未挂载介质，先备份。

## 普通 MAP 的地址限制

`PMEMBASE=0x88000000`，名义窗口 32 MiB；普通 `kalloc` 不分配该窗口。普通 MAP 使用 QEMU 的 DTB 地址截去 DTB 页面及其后部分；本次 `-m 160M` 得到 30 MiB，另保留首 4 KiB。这只是 QEMU virt 连续 DRAM 的保守近似，不是设备发现。窗口不足时失败关闭，不回退 EMUL。

QEMU 9.2 的 [NVDIMM 文档](https://github.com/qemu/qemu/blob/v9.2.0/docs/nvdimm.txt) 示例使用支持 NVDIMM 的 `pc` machine；检查过的上游 9.2/10.1 RISC-V [`virt.c`](https://github.com/qemu/qemu/blob/v9.2.0/hw/riscv/virt.c) 未见等价 memory-device 接入，因此上述是可复验的**本仓补丁**而非上游现成能力。`virtio-pmem` 是不同设备，不应称为 NVDIMM；普通宿主文件不自动保证宿主断电后的持久性。
