# 自有贡献与创新尖刺（顶尖口径）

## 尖刺（唯一不可替代点）

**在教学操作系统 xv6 上，构建可机器判定的崩溃与掉电契约套件——验收的是语义与故障，而不是「又一个双槽结构」。**

相对 NOVA/PMFS/同类教学移植：差异化在 **方法学验收资产**（串口阶段码 + 宿主脚本 + CI + remount 字节等价 + 故障注入），不在声称实现完整生产 FS。

- 产物：`ONBOX_PMFS_*`（含 `PERSIST`/`FILE`/`REMOUNT`/`FAULT`/`PMEM2`/`ABLATION`）+ `make contract*`
- 答辩 15 秒：契约套件让正确性「grep 一行即可判过」。

## C1. 契约化崩溃与验收门（尖刺主体）

- EMUL/MAP/PMEM2；POS/NEG/CHAOS 平级；remount 内容等价；半页 chk / 未 sync 不可见。
- 证据：`make contract-strict` → `ONBOX_PMFS_PASS`。

## C2. 去块设备文件布局 + 行级 persist + CDC/stream 融合（系统柱）

- 双槽 SB、inode/dentry（magic+chk）、文件经 CDC→KV run、fuse 计数。
- 证据：`FILE_PASS fused=1` + `PERSIST_PASS` + `REMOUNT_PASS equal=1`。

## C3. PMEM2 NVDIMM 仿真掉电 + 公平消融效应量（实验柱）

- `memory-backend-file` 杀启；A0–A4 输出 `cycles`/`wa_x100`。
- 证据：`contract-pmem2`；`ABLATION_* wa_x100=`。

## 非贡献

- 完整内核 VFS 替换根；真 NVDIMM 驱动主线；并入 FCS/Aurora 源码树；多写者并发证明。
