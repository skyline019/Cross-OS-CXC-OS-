# 前序约束下沉说明（非第三方移植清单）

FCS、Aurora 为**作者原生前序项目**；本课题把其中设计约束下沉到 xv6，并**自研重写**全部内核代码。  
关系图与答辩口径见 [PRIOR-ART-OWN.md](PRIOR-ART-OWN.md)；尖刺见 [CONTRIBUTIONS.md](CONTRIBUTIONS.md)；总索引 [README.md](README.md)。

本仓库**不**把前序源码树拷进 git，也**不** git submodule 依赖外部私有树。

## 1. 来自作者 FCS 的约束 → xv6 落点

| FCS 约束 | 本仓库落点（v3） |
|---|---|
| 页设备 + sync | `kernel/pmdev.*`；EMUL≈RamDev |
| 双槽 + commit word + 校验 | `pmfs` checkpoint / 8B 提交词 |
| 只承诺完好提交点 | recovery oracle + 损坏拒绝挂载 |
| mem→run、Bloom/fence、WA | `pmalgo.*`（C 重写） |
| 延迟释放 / 压缩思想 | 双槽可达性 + 显式 `pmcompact`（有界，非全量 FCS） |

**刻意不带入：** 异步 IO 线程、分层归并 DP 全量、溢出大值链、生产级 GC。

## 2. 来自作者 Aurora 的约束 → xv6 落点

| Aurora 约束 | 本仓库落点 |
|---|---|
| 键量化 / 粗桶 | `pm_key_coarse` / posting |
| 二值 posting | `pm_posting`；满表 fallback |
| 查询 LRU | `pmindex` qcache |

详见 [AURORA-INDEX.md](AURORA-INDEX.md)。

## 3. 来自 cxc-os 的工程手法

| 手法 | 落点 |
|---|---|
| 可脚本化契约 | 旧 `ONBOX_PMFS_*` + **v3** `test-recovery*` / `test-model` |
| 持久与易失分离 | PM 窗 vs `kalloc`；MAP 不足拒绝挂载 |

**不带入：** Mini-IDE / GUI / UEFI。

## 4. 差异声明（答辩用）

- 前序：用户态/其他内核场景；本课题：**xv6-riscv 教学内核内**。  
- 新增尖刺：可复现提交验收（切点 oracle、规格负控、轨迹）+ `/pm` 文件子集。  
- 失败史：[PITFALLS.md](PITFALLS.md)、[LOCAL-SANDBOX-TESTS.md](LOCAL-SANDBOX-TESTS.md)。
