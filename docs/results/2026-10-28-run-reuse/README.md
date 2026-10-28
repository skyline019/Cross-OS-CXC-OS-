# 双槽 run 页复用、近满测试与原生 rename 重启摘录

这些是**本地隔离工作副本**上 QEMU 9.2.0 TCG 和 Zig 0.13.0 交叉工具链的结果，不是旧 commit 的 CI 结果，也不是硬件掉电试验。相关工作树尚未提交，以下三个故障/空间快照不是同一个最终二进制；另对当前默认源码快照完成了三 hart 回归，但最终状态尚未跑 GitHub CI。

## 当前默认构建回归

[`final-default-console.txt`](final-default-console.txt) 记录三 hart 下效率、指标、GC 基础测试和 VFS 基础测试通过；[`final-usertests-console.txt`](final-usertests-console.txt) 记录完整 xv6 `usertests` 的 `ALL TESTS PASSED`。该默认内核及源码哈希与[性能测试元数据](../2026-10-28-perf/matched/metadata.json)相同；仅重启/故障类测试使用下述单独快照。

## 32 页 EMUL 空间边界

[`gc-small-console.txt`](gc-small-console.txt) 记录单 hart、`PMFS_EMUL_BYTES=131072`：

- 初始逐 run 写满：26 个 run、`tail=capacity=32`，全链压缩和新建因缺少不相交空间而**安全失败**，数据与 checkpoint 不变。
- 另一独立启动中先接近满盘，显式 compact、主动轮换两槽后，单键变更写 400 次、显式 compact 49 次；新 run 页累计复用 449 次，tail 维持 28/32。它只说明指定的单键维护模式，没有自动 GC、持续保留空间、一般长负载或物理磨损结论。
- 该快照源码哈希 `db512c1c5cc74bab9a65f06d2bee87067aed7bee2232841ebfef514e72cabd90`、内核哈希 `3e6efcb37efaaf78f7235674611e7015acbd1e205cd6892ed18d1b42fee6a803`。`gc_reused_run_pages` 包含分配尝试（包括可能回滚），不是硬件 I/O。

## MAP 多次进程重启

[`gc-map-console.txt`](gc-map-console.txt) 和 [`gc-map/`](gc-map/) 的 48 份完整串口：三 hart、普通共享 RAM MAP；覆盖 v2 非物理递减链、400 次维护后的新/旧槽回退、完整压缩前后 KV 及文件检查、旧槽损坏失败关闭、页写/提交词软件切点及 94 个旧 run 页在两槽退役后被复用。

一个负结果是 [`18-repeated-page-write-rollbacks.log`](gc-map/18-repeated-page-write-rollbacks.log)：压缩输出消耗了 tail 以下空洞，240 次注入回滚均临时追加、复用计数为 0；每次 tail 恢复且检查点不变，不意味着该布局无需备用空间。[`09-compact-normal.log`](gc-map/09-compact-normal.log) 的冷启动压缩请求读取 2,666,592 B、写入 81,984 B（软件计数），反映有界可达性扫描和全链维护代价，不能直接与不同夹具的旧数值比较。

MAP 快照源码哈希 `901e98b5958a7fe9768006ef8b5ac736b3530938fd7ad83b33d529c447a90b19`、内核 `87d4b37609577ed5d6ec83b1fbd71e5d9a58d34e8e4eb19b877782d97cc7bbd3`，编译为 `PMEM_MAP PMFS_FAULT_INJECT`。

## 原生后端受限 rename

[`native-vfs-console.txt`](native-vfs-console.txt)、[`native-rename-console.txt`](native-rename-console.txt) 和 [`native-rename/`](native-rename/) 记录单 hart 的同目录、目标不存在的普通文件 rename，打开 fd/inode 保持及不支持场景；另在原生 xv6 块日志的 5 个切点杀进程并重启，全部恢复预期旧/新名字，丢弃已提交日志的负控被发现。跨目录、目录移动、不同 inode 目标替换及跨后端 rename 仍不支持；此进程终止模型不模拟硬件扇区撕裂或真实断电。

原生测试快照源码哈希同上、内核 `398bffae84e8a7eb18f523045b05725a6970649c6bb0e0dcb9597c6986537b06`，编译为 `XV6_LOG_FAULT_INJECT`。工具链和 QEMU 见[性能实验环境](../2026-10-28-perf/environment.txt)。
