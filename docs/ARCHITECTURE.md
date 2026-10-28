# PMFS v3 架构与发布顺序

> 这是受限 xv6 实验系统。普通 MAP 是 QEMU RAM；另有需打补丁的独立实验性 NVDIMM，默认持久操作不提供硬件写回。验收证据见[修复报告](PMFS-REPAIR-REPORT.md)，精确 API 限制见[语义](SEMANTICS.md)。

```text
用户程序：路径（绝对/相对）、目录 fd、seek/append、PM 内 rename
          ├─ VFS vnode：逐组件遍历 + 固定 /pm 挂载点
          │    ├─ 原生 inode → 块日志 / virtio
          │    └─ PM inode pin → struct file（每独立 open 独立 offset）
          │                        └─ 每 PM inode 共享工作缓冲
          └─ 专用 pmfs 文件/KV syscall ─────────┐
                           PMFS sleep lock ── pmfile
                                  COW inode/dentry 快照
                                        |
                            pmstream → pmcdc → pmalgo
                                      |         追加 run、重建索引
                                   pmindex（有界缓存/索引）
                                        |
                           pmfs：双槽 checkpoint、追加分配
                                        |
                        pmdev：页布局与 dirty bitmap（不走 bio/log/virtio）
                                        |
                          pmem：窗口内存、可选 CBO.CLEAN / fence
```

这是专用 PMFS 页布局和内存地址复制，不是直接把文件数据交给 xv6 块缓冲/virtio；但实现仍以 4 KiB 页组织并复制数据，不等于已经消除了所有块/页抽象。普通 xv6 根文件系统依然经 virtio 和日志。

## 地址及容量

- QEMU virt 普通 DRAM `[0x80000000, 0x88000000)` 供 xv6 分配；MAP 的名义窗口为 `[0x88000000, 0x8a000000)`，首 4 KiB 为保留探针区，后续为 PMFS。内核将 DTB 起始向下页对齐，截去 DTB 及其后的窗口，以免覆盖固件在启动时重写的区域。
- 对本次 QEMU 9.2 `-m 160M`，DTB 从 `0x89e00000` 开始，窗口可用 30 MiB、PMFS 使用 30 MiB 减 4 KiB。该近似只针对已观察的 virt 连续 DRAM，不是通用内存或 NVDIMM 设备发现。MAP 缺少足够 RAM 时拒绝启动该文件系统，不降级为 EMUL。
- EMUL 是静态 8 MiB 易失缓冲。共享 RAM 测试将**全部 160 MiB guest RAM**映射到 `memory-backend-file,share=on`，不是独立设备。实验补丁 QEMU 则提供独立的 32 MiB `nvdimm` 冷插拔设备及私有 FDT 节点，guest 在页分配前发现并校验它；不支持通用设备发现或动态热插拔。更多说明见[QEMU 后端](QEMU-NVDIMM.md)。

## 介质布局与关键不变量

PMFS 区内页 0/1 为双 checkpoint，页 2/3 保留，后续页按 tail 追加。checkpoint 记录 epoch、tail、run 链头、inode 和 dentry 快照根；元数据、run 页有边界和校验。已提交数据和旧根不被后续操作原地覆盖。没有安全回收或 compaction，耗尽即失败。

一次根发布的设计顺序：

1. 追加内容/run 和必要的 inode/dentry 页；请求各页持久化并执行屏障。
2. 对非当前 checkpoint 槽的对齐 8 B 状态/校验词写零并持久化；写入新头（仍未提交）并持久化。
3. 一次 8 B store 写入提交状态及校验，请求持久化并执行屏障；随后在内存更新 epoch/槽号并返回。

`pmem_persist` 的**语义取决于平台**：默认实现是 touch+fence；只有可选 Zicbom 路径真正发出 `CBO.CLEAN`，且仍需平台保证持久域。协议还依赖 8 B store 故障原子性、正确屏障和校验未碰撞，不能从双槽本身推导普适掉电安全。

恢复时选择通过范围、格式和校验的最高 epoch checkpoint，重建 run/index，校验文件元数据及关联；失败时拒绝挂载、不自动清空。单个无效 checkpoint 可以回退另一个；有效头引用的数据损坏会使挂载失败，而不是悄悄假装旧数据正确。

## VFS 算法、并发与生命周期

`vfs_walk` 对每个分量执行后端实际查找并持有 inode 引用；只有原生根的 `pm` 进入 PMFS，PM 根的 `..` 回到原生根。PM inode pin 使用独立 spinlock，允许 `fork` 在进程锁下复制 cwd 引用而不睡眠；对原生 vnode 的 `iput` 在 xv6 日志事务内完成。目录 ABI 提供后端无关的固定长度项；原生 on-disk `dirent` 格式没有改变。

每个普通 PM inode 最多有一个共享的有界工作缓冲和一个底层槽；独立 `open` 获得各自偏移，`dup`/`fork` 共用打开对象和偏移。PMFS sleep lock 串行化缓冲、写入、append 选尾和同步；专用文件句柄与普通打开同一 inode 互斥。全局同步在 file table spinlock 下取得引用快照，释放 spinlock 后才进入可睡眠的 PMFS 同步。该粗粒度锁保证当前受限操作路径的互斥，不是可扩展多核设计或完整线性化证明。

子树 rename 先检查目标、环和所有后代的新路径长度，再复制旧 dentry 表供普通错误回滚，修改内存图后**一次**调用元数据 COW checkpoint 发布。重写复杂度 `O(受限 dentry 数)`；活动目录或文件的引用按 inode 保持，目标若被引用则拒绝替换。重启 oracle、实际代码变异负控与规格模型见 [VFS 验证记录](results/2026-10-28-vfs/README.md)。调用过程、资源上限、失败及全局 `sync` 细节见[语义](SEMANTICS.md)。
