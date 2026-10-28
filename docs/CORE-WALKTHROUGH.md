# PMFS v3 核心工作与代码直讲

> 面向答辩与论文实现章（对应 `thesis/latex` 第 3–4 章）。路径相对仓库根。  
> 精确 API/失败语义以 [SEMANTICS.md](SEMANTICS.md) 为准；布局与发布顺序见 [ARCHITECTURE.md](ARCHITECTURE.md)；答辩提纲见 [DEFENSE.md](DEFENSE.md)。

## 0. 一句话

在保留 xv6 virtio 根的前提下，提供固定挂载点 **`/pm`** 上的受限文件系统：内容经 CDC/stream 落入 **不可变 KV run**；元数据为 **COW inode/dentry**；用 **双槽 checkpoint + 8B 提交词**发布根。未成功 `fsync`/checkpoint 的修改**不承诺**故障后可见。

```text
普通 fd: open/write/fsync @ /pm/...
        → VFS 逐组件 walk → PM inode + 共享工作缓冲
        → pmstream / pmcdc → pm_put → seal run
        → COW 元数据页 → pmfs checkpoint 提交词
专用:    pmfs_put/get/seal 与专用文件句柄（与普通 fd 互斥）
```

---

## 1. 分层（读代码地图）

```text
用户：路径 / getdents / lseek / append / rename / link
  └─ VFS vnode（固定 /pm）── 原生 inode → bio/log/virtio
                         └─ PM inode pin → struct file
                              └─ pmfile（COW 元数据 + stream）
                                   └─ pmcdc / pmstream
                                        └─ pmalgo（run / 索引 / fuse）
                                             └─ pmfs（双槽 checkpoint、可达性、页复用）
                                                  └─ pmdev（页布局 + dirty）
                                                       └─ pmem（窗口；可选 CBO.CLEAN）
```

| 区域 | 内容 |
|------|------|
| 页 0/1 | 双 checkpoint |
| 页 2/3 | 保留 |
| 其后 | KV run / COW 元数据快照（可复用双槽均不可达页） |

关键模块：`kernel/vfs*.c`、`kernel/pmfile.c`、`kernel/pmfs.c`、`kernel/pmalgo.c`、`kernel/pmstream.c`、`kernel/pmcdc.c`、`kernel/pmem.c`、`kernel/pmdev.c`。

---

## 2. 持久协议（最重要的不变量）

设计顺序（与 [ARCHITECTURE.md](ARCHITECTURE.md) 一致）：

1. 追加内容/run；分配必要的 inode/dentry COW 页；对各页请求 persist + fence。  
2. 对**非当前** checkpoint 槽：对齐 8B 状态/校验写零并 persist；写入新头（未提交）并 persist。  
3. **一次 8B store**写入提交状态及校验 → persist + fence；再更新内存中的 epoch/槽号。

含义：

- 提交词之前 crash → 恢复应看到**上一完整根**。  
- 提交词之后 → **新完整根**。  
- 默认 `pmem_flush` 是 touch+fence，**不是**硬件写回；`PMEM_ZICBOM` 才发 `CBO.CLEAN`，且平台持久域仍需声明。

恢复：选通过范围/格式/校验的最高 epoch；重建 run/index；失败则**拒绝挂载、不自动清空**。单槽坏可回退；有效头引用的数据坏则挂载失败。

---

## 3. VFS 与文件可见性

- `/pm` 为固定合成挂载点；绝对路径从原生根开始，相对路径从 cwd。  
- 普通文件：`open/read/write/lseek/fsync/close`、`mkdir`、空目录删除、PM 内 `rename`、同后端硬链接。  
- 独立 `open`：各自 offset；同一 PM inode **共享工作缓冲**（未 sync 写对其它打开立即可见）。  
- `dup`/`fork`：共用打开对象与 offset。  
- 成功 `fsync`：在当前软件协议内先落数据/元数据，再 checkpoint 发布。  
- 全局 `sync()`：先 xv6 日志，再扫 PM 脏缓冲；**不是**全库单事务。

容量（量级）：约 40 inode/dentry、每文件 4 extent、文件 ≤1024 B、KV 值 ≤512 B。详见 SEMANTICS。

---

## 4. KV / CDC / stream

- `pm_put`：进 memtable（未 seal 不承诺持久）。  
- `pm_seal`：mem → 不可变 run →（常与元数据一起）进入 checkpoint 发布路径。  
- 普通 fd 写入：优先探测 CDC；超 extent/切分预算则固定分块。  
- **无内容去重**；CDC 只负责切分。  
- 消融与软件请求计数：`./test-xv6.py efficiency`、[PERFORMANCE-ENDURANCE.md](PERFORMANCE-ENDURANCE.md)。

---

## 5. 维护：页复用与显式压缩

- 双槽可达性扫描：两代根仍引用的页受保护。  
- 双槽都不可达的页可复用为元数据或新 run。  
- `pmcompact`：显式全链 COW 压缩；要求 memtable/stream 空、有不相交暂存空间；近满可失败。  
- **不是**全设备 GC / 硬件磨损均衡。见 [MAINTENANCE.md](MAINTENANCE.md)。

---

## 6. 设备后端（别说错）

| 后端 | 是什么 | 不是什么 |
|------|--------|----------|
| EMUL | 8 MiB 静态易失缓冲 | 真持久 |
| MAP | `PHYSTOP` 后普通 guest RAM | NVDIMM |
| 共享 RAM 文件 | 整机/窗口 file-backend，便于杀进程重启 | 物理掉电 |
| 补丁 NVDIMM | QEMU 9.2.0 冷插拔独立 nvdimm + 私有 FDT | 上游通用 / NFIT / 热插拔 |

---

## 7. 验收如何卡死正确性

| 层级 | 命令/产物 | 证明到哪 |
|------|-----------|----------|
| 旧自测 smoke | `make contract-full` | 阶段码；破坏性；非跨进程文件恢复证明 |
| 进程重启 oracle | `make test-recovery*` | 显式软件切点 + 同后端重启读回 |
| VFS | `make test-vfs` / `test-vfs-recovery` | 路径、硬链接、子树 rename |
| 规格模型 | `make test-model` | 有界协议 + 负控；非从内核抽轨迹 |
| 轨迹重放 | `make test-trace-replay` | 源级范围事件采样；非指令级 |

总闸叙事：**方法学验收**（可复现 oracle + 诚实假设）>「又一个双槽结构」。

---

## 8. 时间线（答辩一张表）

| 时刻 | 状态 | 杀 QEMU 后期望 |
|------|------|----------------|
| `write` 未 `fsync` | 工作缓冲 / 可能 memtable | **不承诺**保留 |
| `fsync` 成功返回 | 数据+元数据已走发布协议 | 读回本次完整版本 |
| checkpoint 切点中途 | 插桩 oracle 覆盖的边界 | 旧完整或新完整（依切点） |
| 撕坏单槽 | 另一槽合法 | 回退 |
| 空白/v2/校验烂 | — | 拒绝挂载，不自动 format |

---

## 9. 改代码入口

| 目标 | 入口 |
|------|------|
| 刷写/CBO | `kernel/pmem.c` |
| checkpoint | `kernel/pmfs.c` |
| 文件/COW 元数据 | `kernel/pmfile.c` |
| run/索引 | `kernel/pmalgo.c` / `pmindex.c` |
| 切分 | `pmcdc.c` / `pmstream.c` |
| `/pm` 路径 | VFS 相关 + `sysfile` |
| 新测试 | `tools/pmfs_recovery.py`、`test-xv6.py`、`docs/CONTRACT.md` |
