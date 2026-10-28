# 图：PMEM2 进程重启串口对照（历史草稿）

> 仅为整机共享 RAM 文件的重启探针，不证明 NVDIMM、文件恢复或物理掉电安全；参见[优秀毕设路线图](../EXCELLENCE-ROADMAP.md)。

> 下列内容是旧文档示意，并非本次核验日志；当前 `make contract-pmem2` 使用临时 RAM 文件，但构建仍会重建 `fs.img`。如需论文引用，应重新运行并保存原始日志。

## Boot1（写入探针魔数）

```text
pmem: MAP ready base=0x88000000 size=0x2000000
ONBOX_PMFS_PMEM0_PASS mode=MAP size=33550336
ONBOX_PMFS_PMEM1_PASS magic=ok
…
ONBOX_PMFS_PASS
```

说明：`PMEM1` 成功后写入 durable word（`PMEM2_DURABLE`）；本趟**不应**出现 `PMEM2_PASS`（首次启动无「存活」语义）。

## 杀 QEMU 进程（并非宿主掉电）

```text
# contract_pmem2.py: SIGKILL QEMU；guest-ram.img 保留 share=on 内容
```

## Boot2（同一 `memory-backend-file`）

```text
pmem: MAP ready base=0x88000000 size=0x2000000
ONBOX_PMFS_PMEM2_PASS persist=ok          ← 重启魔数探针
ONBOX_PMFS_PMEM0_PASS mode=MAP …
ONBOX_PMFS_PMEM1_PASS magic=ok
…
ONBOX_PMFS_PASS
ONBOX_PMFS_PMEM2_CONTRACT_PASS boots=2
```

## 合理解读

> 在该共享文件后端上，杀 QEMU 进程并重启后可检查一个探针魔数；这不验证文件内容恢复、缓存持久顺序或宿主断电安全。
