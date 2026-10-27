# 自有贡献与创新尖刺（合题后）

## 尖刺（唯一不可替代点）

**在教学操作系统 xv6 上，构建可机器判定的崩溃与掉电契约套件。**

- 产物：`ONBOX_PMFS_*`（含 `PERSIST`/`FILE`/`PMEM2`）+ `make contract*`
- 相对「又一个双槽 PMEM-FS」：方法学与验收资产。

## C1. 契约化崩溃与验收门（尖刺主体）

- EMUL/MAP/用户态全阶段串口契约；CI 挂接。
- 证据：`make contract` → `ONBOX_PMFS_PASS`。

## C2. 去块设备文件布局 + 有序行级 persist（系统柱 · 合题）

- 双槽 SB、inode/dentry 页、`pmem_flush` 行遍历 + fence、B3/WA、文件 API。
- 前序 FCS/Aurora 约束下沉；xv6 C 自研重写。
- 证据：`FILE_PASS` + `PERSIST_PASS` + `CRASH_PASS` + `wa_x100`。

## C3. PMEM2 NVDIMM 仿真掉电一行过（实验柱）

- `memory-backend-file` 杀启；`ONBOX_PMFS_PMEM2_PASS`。
- 证据：`make contract-pmem2` → `PMEM2_CONTRACT_PASS boots=2`。

## 非贡献

- 完整内核 VFS 替换根；真 NVDIMM 驱动主线；并入 FCS/Aurora 源码树。
