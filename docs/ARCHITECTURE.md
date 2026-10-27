# PMFS 架构（字节寻址文件子系统）

## 0. 产品形态

- **是**：并行于 virtio 根 FS 的 PMEM **文件子系统**（inode/dentry + `create/open/read/write/fsync`）与 KV 引擎（`put/get/seal`）。
- **不是**：替换 `/` 的完整 POSIX VFS。

## 1. 物理与虚拟地址（NVDIMM 仿真）

```
KERNBASE ─────────── 0x80000000
  │  kernel + user pages（易失 DRAM）
PHYSTOP  ─────────── KERNBASE + 128MiB
PMEMBASE ─────────── PHYSTOP          ← QEMU NVDIMM 仿真窗
  │  双槽 SB / inode / dentry / run
PMEMEND  ─────────── PMEMBASE + 32MiB
```

| 模式 | 构建 | 运行 | backend 字段 |
|------|------|------|--------------|
| EMUL | 默认 | `-m 128M`，8MiB 缓冲 | `emul` |
| MAP | `-DPMEM_MAP` | `-m 160M` | `map` |
| PMEM2 | `-DPMEM_MAP` | `memory-backend-file,share=on` | `map` + 掉电 |

## 2. 软件分层

```
用户 create/mkdir/fopen/fread/fwrite/fsync  |  put/get/seal/stat
        │                                    │
   pmfile（dir + extent 元数据）              │
        │                                    │
   pmstream（滑窗）→ pmcdc（Rabin/Fixed）     │
        │                                    │
   pmalgo（chunk→mem→run、B*、fuse_*）◄───────┘
        │
   pmindex（量化/posting/qcache）
        │
   pmfs（双槽 SB + commit）
        │
   pmdev → pmem_flush(行) → pmem_fence → MAP/EMUL
```

融合证明：`fuse_puts` / `fuse_seals`；消融 A0–A4 见 `docs/ABLATION.md`；契约平级 POS/NEG/CHAOS 见 `docs/CONTRACT.md`。

## 3. 盘上布局

| 页 | 内容 |
|---|---|
| 0 / 1 | SB 双槽 |
| 2 | inode 表 |
| 3 | dentry 表 |
| ≥4 | 不可变 run 数据页 |

## 4. 崩溃与刷写契约

- 协议：`store → flush(64B lines) → fence → store SB → flush → fence`
- 文件：`fsync` = 写 inode/dentry 页 + `pmfs_commit`
- 验收：`PERSIST_PASS` / `FILE_PASS` / `CRASH_PASS` / `PMEM2_PASS`
