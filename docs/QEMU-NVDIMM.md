# QEMU 持久内存窗口（PMFS-KV）

## MAP：扩容 DRAM

```bash
make qemu-pmem          # 重建 -DPMEM_MAP，-m 160M
make contract-pmem      # 断言 mode=MAP + PMEM1
```

## PMEM2：文件后端掉电

整机 guest RAM 落到宿主机文件，`kill` 后再启可读回探针魔数。  
注意：`-m 160M` 必须与 `memory-backend-file` 的 `size=160M` 一致，否则 QEMU 会报 NUMA/RAM mismatch。

```bash
make contract-pmem2     # truncate guest-ram.img → boot1 → kill → boot2 → PMEM2_PASS
# 手工：
PMEM_RAM_FILE=./guest-ram.img make qemu-pmem-file
```

对应串口：

1. boot1：`ONBOX_PMFS_PMEM1_PASS`（并写入 durable word）
2. boot2：`ONBOX_PMFS_PMEM2_PASS persist=ok`

## 验收探针

| 行 | 含义 |
|----|------|
| `PMEM0 mode=MAP` | 窗口可用 |
| `PMEM1 magic=ok` | 写-刷-读 |
| `PMEM2 persist=ok` | 杀进程再启仍在 |
