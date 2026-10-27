# ONBOX_PMFS 严格测试契约（PMFS 文件子系统）

所有契约行必须可被 `grep` / `tools/contract_pmfs.py` 匹配。

## 行格式

- 成功：`ONBOX_PMFS_<STAGE>_PASS` + 空格分隔 `key=value`
- 失败：`ONBOX_PMFS_<STAGE>_FAIL stage=<S> code=<C>`
- 总闸：`ONBOX_PMFS_PASS`

## 必过子集（EMUL）

`PMEM0` `MOUNT` `STORE` `INDEX` `ALGO` `CRASH` `PERSIST` `FILE` → `ONBOX_PMFS_PASS`

## MAP / PMEM2

- MAP：`PMEM0 mode=MAP backend=map` + `PMEM1`
- PMEM2：`ONBOX_PMFS_PMEM2_PASS persist=ok`；脚本印 `PMEM2_CONTRACT_PASS boots=2`

## 用户态

- `_pmfs_probe` → `SYSCALL_PASS`
- `_pmfs_file` → `FILEUSER_PASS`
- `_pmfs_bench [N]` → `BENCH_PASS`（含 `median_file` / `thr_file` / `wa_x100`）
- `_pmfs_posix` → `POSIX_PASS`（薄 shim）

## Stage 码表（增补）

| Stage | code 含义 |
|-------|-----------|
| PERSIST | 1 行计数不足；2 二次 flush 未递增 |
| FILE | 1–12 见 `pmfile_selftest` |
| PMEM0 | 含 `backend=emul\|map` |
