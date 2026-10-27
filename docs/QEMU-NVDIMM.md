# QEMU NVDIMM 仿真窗口（PMFS 合题）

> 主线口径：guest 物理窗口 `[PMEMBASE, PMEMEND)` = **QEMU 模拟的 NVDIMM**；  
> `memory-backend-file` 掉电二次启动验证窗口级 persist。EMUL 仅快回归。

## 地址映射（内存管理改造）

```
KERNBASE ── 0x80000000   易失 DRAM（内核+用户）
PHYSTOP  ── +128MiB
PMEMBASE ── PHYSTOP      ← NVDIMM 仿真起点（kvmmap，-DPMEM_MAP）
PMEMEND  ── +32MiB
```

`kernel/vm.c` 仅在 `-DPMEM_MAP` 时映射该窗；默认 EMUL 不触碰，避免 store fault。

## MAP：扩容 DRAM 充当 NVDIMM

```bash
make qemu-pmem          # 重建 -DPMEM_MAP，-m 160M
make contract-pmem      # 断言 mode=MAP backend=map + PMEM1
```

契约行示例：`ONBOX_PMFS_PMEM0_PASS mode=MAP backend=map size=...`

## PMEM2：文件后端掉电（演示默认耐久路径）

整机 guest RAM 落到宿主机文件，`kill` 后再启可读回探针魔数。  
注意：`-m 160M` 必须与 `memory-backend-file` 的 `size=160M` 一致。

```bash
make contract-pmem2     # truncate guest-ram.img → boot1 → kill → boot2 → PMEM2_PASS
```

对应串口：

1. boot1：`ONBOX_PMFS_PMEM1_PASS`（并写入 durable word）
2. boot2：`ONBOX_PMFS_PMEM2_PASS persist=ok`

## 验收探针

| 行 | 含义 |
|----|------|
| `PMEM0 mode=MAP backend=map` | NVDIMM 仿真窗口可用 |
| `PMEM0 mode=EMUL backend=emul` | 快回归假缓冲 |
| `PMEM1 magic=ok` | 写-刷-读 |
| `PERSIST_PASS flush_lines=` | 行级刷写协议可观测 |
| `PMEM2 persist=ok` | 杀进程再启仍在 |
