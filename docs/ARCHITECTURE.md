# PMFS-KV 架构（字节寻址键值存储）

## 0. 产品形态

- **是**：并行于 virtio 根 FS 的 PMEM **KV 引擎**（`pmfs_put/get/seal/stat`）。
- **不是**：POSIX inode/目录文件系统（该方向若做，另开里程碑）。

## 1. 物理与虚拟地址

```
KERNBASE ─────────── 0x80000000
  │  kernel + user pages（易失 DRAM）
PHYSTOP  ─────────── KERNBASE + 128MiB
PMEMBASE ─────────── PHYSTOP
  │  字节寻址持久区（双槽 SB / 数据 run）
PMEMEND  ─────────── PMEMBASE + 32MiB
```

| 模式 | 构建 | 运行 |
|------|------|------|
| EMUL | 默认 | `-m 128M`，2MiB 静态缓冲 |
| MAP | `-DPMEM_MAP` | `-m 160M` |
| PMEM2 | `-DPMEM_MAP` | `memory-backend-file,share=on` 杀启 |

## 2. 软件分层

```
用户 pmfs_put/get/seal/stat
        │
   pmalgo（mem→run、B1/B2/B3、WA）
        │
   pmindex（量化 + posting + qcache）
        │
   pmfs（双槽 SB + commit）
        │
   pmdev → pmem_persist → MAP 或 EMUL
```

## 3. 盘上布局（相对设备基址）

| 页 | 内容 |
|---|---|
| 0 / 1 | SB 双槽 |
| ≥2 | 不可变 run 数据页 |

无 inode 表。

## 4. 崩溃契约

双槽 + epoch + 校验；`CRASH` 自测；`PMEM2` 验证窗口级掉电存活。
