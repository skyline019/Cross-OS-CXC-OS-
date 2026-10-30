# 直接 PMEM 实验核心：边界与双根重启补充

> 这是先前[单文件实验核心](../../THESIS-DIRECTCORE-EXPERIMENT.md)的补充验证，不是完整 VFS、真实掉电或真实 NVDIMM 结果。

QEMU 9.2 补丁、独立 32 MiB 文件后端 NVDIMM、NVMe 根盘、三 hart、`PMEM_ZICBOM` 指令路径；kernel、fs.img、QEMU 和源码哈希在 [`metadata.json`](metadata.json) 与 [`source-files.sha256`](source-files.sha256)，构建环境见[先前实验环境](../2026-10-30-directcore/environment.txt)。实验使用可丢弃的独立镜像与私有根盘副本。

**53 个更新，107 次启动全部通过**：13 个边界大小（0、1、63、64、65、127、128、129、255、256、257、511、512 B）、短/长文件反复缩短与重建、相同行复用及固定种子的 24 个补充案例。43 次内容变化产生新版本，10 次相同内容写不修改镜像；相同内容包含空文件以及 generation 相差 256 但实际字节相同的情形。

每次写后由独立 host oracle 遍历**两个根**，核验各 epoch 的完整期望内容、对象范围、别名和元数据校验；然后在单独的新 QEMU 启动中让 guest 读回。53 次只读重启前后的整个 NVDIMM SHA-256 均不变；输入 kernel/fs.img 也不变。该测试不是逐 store 随机故障重放，仍假设受保护介质未发生无关损坏，数据行本身没有 CRC。两个根内容由外部预期模式检查，不意味着 guest 能检测静默数据损坏。

- [`summary.json`](summary.json)、逐案例 [`cases.jsonl`](cases.jsonl)、[完整控制台](console.txt)、[107 个串口日志](serial/)；初始化和最终 32 MiB 镜像分别压缩为 [`initial.pmem.gz`](initial.pmem.gz)、[`final.pmem.gz`](final.pmem.gz)。
- 主机单元测试 [`host-tests.txt`](host-tests.txt)；结果文件摘要见 `SHA256SUMS`。
- 复现（仅独立副本，先按实验说明构建 `DIRECT_CORE=1`、NVDIMM 和 Zicbom 内核，输出目录必须不存在）：

```sh
python3 tools/directcore_boundary_recovery.py --qemu "$QEMU" --backend nvme --cpus 3 --zicbom \
  --random-steps 24 --seed 20261030 --output /tmp/directcore-boundaries
```
