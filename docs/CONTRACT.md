# ONBOX_PMFS 严格测试契约（PMFS-KV）

所有契约行必须可被 `grep` / `tools/contract_pmfs.py` 匹配。

## 行格式

- 成功：`ONBOX_PMFS_<STAGE>_PASS` + 空格分隔 `key=value`
- 失败：`ONBOX_PMFS_<STAGE>_FAIL stage=<S> code=<C>`
- 总闸（必过子集全部成功后）：`ONBOX_PMFS_PASS`

## 必过子集（EMUL / 默认 `make qemu`）

`PMEM0` `MOUNT` `STORE` `INDEX` `ALGO` `CRASH` → 然后 `ONBOX_PMFS_PASS`

## MAP 模式（`make contract-pmem`）

额外：`PMEM0 mode=MAP` + `PMEM1`

## 掉电二次启动（`make contract-pmem2`）

`memory-backend-file` 杀 QEMU 再启 → **必须**出现一行：

`ONBOX_PMFS_PMEM2_PASS persist=ok`

脚本成功时再印：`ONBOX_PMFS_PMEM2_CONTRACT_PASS boots=2`

## 用户态

- `_pmfs_probe` → `ONBOX_PMFS_SYSCALL_PASS`
- `_pmfs_posix` → `ONBOX_PMFS_POSIX_PASS`（路径 hash→KV 薄 shim，非内核 VFS）
- `_pmfs_bench [N]` → `ONBOX_PMFS_BENCH_PASS`（含 `wa_x100`）

## Stage 码表

| Stage | code 含义 |
|-------|-----------|
| PMEM0 | 1 未就绪；2 尺寸不符 |
| PMEM1 | 1 not ready；2 魔数错 |
| PMEM2 | 1 掉电丢失（脚本侧断言） |
| MOUNT | 1 format；2 双槽皆坏 |
| STORE / INDEX / ALGO | 自测负码绝对值 |
| CRASH | 1 半写活动槽被挂；2 未提交可见 |
| SYSCALL / BENCH | 见用户程序 |
