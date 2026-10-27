# PMFS 架构草案

## 1. 物理与虚拟地址

```
KERNBASE ─────────── 0x80000000
  │  kernel + user pages（易失 DRAM）
PHYSTOP  ─────────── KERNBASE + 128MiB     ← 现有 xv6 kalloc 上界
PMEMBASE ─────────── PHYSTOP               ← 新增：PMEM 窗口起点
  │  字节寻址持久区（超级块 / inode / 数据）
PMEMEND  ─────────── PMEMBASE + PMEMSIZE   ← 默认 32–64MiB
```

QEMU：总内存 `PHYSTOP-KERNBASE+PMEMSIZE`，或用 `memory-backend-file,share=on`
把 PMEM 窗口落到宿主机文件（见 `docs/QEMU-NVDIMM.md`）。

内核在 `kvminit` 中把 `[PMEMBASE, PMEMEND)` **直映射**为可读写（后续可加 UXN/PXN 策略），
**不**进入 `kalloc` 空闲链。

## 2. 软件分层（当前 + 目标）

```
用户 syscall (open/read/write/…)     ← 未接（M2+）
        │
   pmfs 逻辑（inode/目录）            ← 未接
        │
   pmfs 存储核（双槽 SB + tail 分配 + commit）  ← 已有（FCS 移植）
        │
   pmdev（页设备：read/write/sync）              ← 已有
        │
   pmem_persist（flush+fence）
        │
   PMEM 直映射 或 EMUL 2MiB 缓冲
```

对照：原版路径 `sysfile → fs → bio → virtio_disk`（块设备抽象）在 PMFS **主路径上绕开**。
对比实验时保留原版 fs 镜像，另挂 PMFS 根或独立设备名。

## 3. 盘上布局（逻辑）

| 偏移（相对 PMEMBASE） | 内容 |
|---|---|
| 0 | Superblock slot A |
| 4KiB | Superblock slot B（双槽） |
| 8KiB | Inode 表 |
| … | 空闲图 / 分配位图 |
| … | 数据区（页或可变 extent） |

超级块字段（草案）：magic、epoch、root inum、inode 区偏移/长度、数据区偏移、checksum。

## 4. 崩溃一致性（轻量）

借鉴 FCS：**双槽 SB + epoch + 校验**；数据先写后刷，最后原子翻转「当前槽」。

提交序列（单写者简化版）：

1. 将脏元数据/数据写入 PMEM（`memcpy` 到直映射地址）。
2. 对涉及缓存行执行 `pmem_flush` + `pmem_fence`。
3. 写备用 SB（新 epoch、新根指针）并 flush+fence。
4. 校验通过后，该槽成为最新完好提交点。

恢复：扫描两槽 → 丢弃校验失败 → 选更高 epoch → 重建内存 inode 缓存（可无或薄缓存）。

## 5. 与原版 log 的对比点

| | xv6 log+virtio | PMFS |
|---|---|---|
| 寻址 | 512B 块 | 字节 / 缓存行 |
| 写路径 | 日志块 + 回放 | 原地 + 刷行 |
| 一致性 | 块日志 | 双槽 SB + flush |
| 主要延迟 | DMA/中断/队列 | CPU 存储层次 + fence |

## 6. 模块文件

| 文件 | 职责 |
|---|---|
| `kernel/pmem.h/.c` | 窗口、映射、flush/fence、探针 |
| `kernel/pmfs.h/.c` | 布局、mount、CRUD、恢复 |
| `docs/*` | 课题说明与 QEMU 参数 |
