# ONBOX_PMFS 严格测试契约（完整 FS + CDC + Stream）

所有契约行必须可被 `grep` / `tools/contract_pmfs.py` 匹配。

## 行格式

- 成功：`ONBOX_PMFS_<STAGE>_PASS` + 空格分隔 `key=value`
- 失败：`ONBOX_PMFS_<STAGE>_FAIL stage=<S> code=<C>`
- 总闸：`ONBOX_PMFS_PASS`

## 内核必过子集（平级）

`PMEM0` `MOUNT` `STORE` `INDEX` `ALGO` `CRASH` `PERSIST`
`FILE` `CDC` `STREAM` `POS_FILE` `NEG_FILE` `CHAOS_FILE` `ABLATION`
→ `ONBOX_PMFS_PASS`

`make contract-strict` 另要求：`POS/NEG_CDC`、`POS/NEG_STREAM`、`EDGE_FILE`、`ABLATION_A0/A4`。

## MAP / PMEM2

- MAP：`PMEM0 mode=MAP backend=map` + `PMEM1`
- PMEM2：`ONBOX_PMFS_PMEM2_PASS persist=ok`；脚本印 `PMEM2_CONTRACT_PASS boots=2`

## 用户态

- `_pmfs_probe` → `SYSCALL_PASS`
- `_pmfs_file` → `FILEUSER_PASS`（目录 `/u` + 文件）
- `_pmfs_bench [N]` → `BENCH_PASS`（含 `median_file` / `thr_file` / `wa_x100`）
- `_pmfs_posix` → `POSIX_PASS`（薄 shim）

## 融合证明

`FILE` / `STREAM` / `ABLATION` 行含 `fuse_puts` / `fuse_seals` 或 `fused=1`，证明 chunk→`pm_put`/`pm_seal`→index 路径未旁路。
