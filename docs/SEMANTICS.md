# PMFS 文件子系统：硬语义清单（子集写死）

> 顶尖答辩口径：不是「接近 POSIX」，而是 **可见点 / 差集 / 边界** 可陈述、可测。

## 1. 崩溃一致点（哪些写可见）

| 操作 | 掉电/remount 后可见？ | 机制 |
|------|----------------------|------|
| `create` / `mkdir`（未另存） | 否* | 仅内存 dent；`mkdir` 当前会 `store_meta` |
| `write`（未 `fsync`） | **否** | chunk 可能在 memtable/stream 窗；inode extent 未落页 |
| `fsync` / `fclose`(dirty) | **是** | `pmstream_flush`→`pm_seal` + inode/dentry 页 + `pmfs_commit` |
| `unlink` 成功返回后 | 元数据可见删除 | `store_meta`；chunk GC 非主线 |
| KV `put` 未 `seal` | **否** | 与文件 nosync 同模型 |

契约：`ONBOX_PMFS_REMOUNT_PASS`（sync 后字节等价）、`ONBOX_PMFS_FAULT_SEALMID_PASS`（未 sync 不可见）。

## 2. 与 POSIX 差集表

| POSIX 期望 | PMFS | 说明 |
|------------|------|------|
| 路径解析 `/a/b` 多层 | 有限深度（dent 存全路径 + parent） | 非完整 VFS namei |
| `O_APPEND` / `lseek` | **无** | 每次 read 自 offset 0 拼 extent |
| 多进程同 fd 共享 | **无** | 每 open 独立 stream 槽 |
| `rename` / 硬链接 | **无** | |
| `mmap` / 页缓存 | **无** | 直达 KV get |
| 权限 / uid | **无** | |
| 根挂载替换 | **不做** | 并行于 virtio 根 |
| `fsync` 数据+元数据 | **有**（子集） | 行级 flush+fence+双槽 SB |

深化用户态：`pmfs_posix` 走真实 `mkdir/create/fwrite/fsync/fread/readdir/unlink`，不再仅 path-hash KV shim。

## 3. 边界（刻意不声称）

| 边界 | 现状 |
|------|------|
| 大文件 | `PMFILE_MAX_EXT=8` × `PM_VAL_MAX=512` ≈ 4KiB/文件量级 |
| 多写者 | 单 open 写；无字节范围锁 |
| 并发 | UP 契约默认 `CPUS=1`；无多核一致性证明 |
| 半页损坏元数据 | inode 页带 magic+chk；损坏 → 拒绝加载 |
| 真 NVDIMM 乱序 | EMUL/MAP 难真实 reorder；以「跳过 fence + 撕槽」代理 |

## 4. 故障注入矩阵（可机器判定）

| 注入 | 期望 | 契约行 |
|------|------|--------|
| remount 后 readback | 与 sync 前字节相等 | `REMOUNT_PASS` |
| 未 fsync remount | 新内容不可见 | `FAULT_SEALMID_PASS` |
| inode 页半页撕坏 | chk 失败，不把垃圾当真相 | `FAULT_HALFPAGE_PASS` |
| 活动 SB 撕坏 | 回退另一槽 | 既有 `CRASH_PASS` |
| PMEM2 杀启 | magic/persist 存活 | `PMEM2_PASS` |
