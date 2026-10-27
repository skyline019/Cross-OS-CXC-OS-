# 图：PMEM2 掉电前后串口对照

> 插入论文时可将本页导出为截图，或直接引用下列原文块。  
> 复现：`make contract-pmem2`

## Boot1（种植耐久魔数，尚无「二次启动存活」行）

```text
pmem: MAP ready base=0x88000000 size=0x2000000
ONBOX_PMFS_PMEM0_PASS mode=MAP size=33550336
ONBOX_PMFS_PMEM1_PASS magic=ok
…
ONBOX_PMFS_PASS
```

说明：`PMEM1` 成功后写入 durable word（`PMEM2_DURABLE`）；本趟**不应**出现 `PMEM2_PASS`（首次启动无「存活」语义）。

## 杀进程（模拟掉电）

```text
# contract_pmem2.py: SIGKILL QEMU；guest-ram.img 保留 share=on 内容
```

## Boot2（同一 `memory-backend-file`）

```text
pmem: MAP ready base=0x88000000 size=0x2000000
ONBOX_PMFS_PMEM2_PASS persist=ok          ← 尖刺证据行
ONBOX_PMFS_PMEM0_PASS mode=MAP …
ONBOX_PMFS_PMEM1_PASS magic=ok
…
ONBOX_PMFS_PASS
ONBOX_PMFS_PMEM2_CONTRACT_PASS boots=2
```

## 论文一句

> 同一文件后端、杀 QEMU 再启后，串口出现 `ONBOX_PMFS_PMEM2_PASS persist=ok`，证明窗口级 persist 跨「掉电」仍可机器判定。
