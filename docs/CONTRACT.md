# ONBOX_PMFS 严格测试契约（完整 FS + CDC + Stream + Remount/Fault）

所有契约行必须可被 `grep` / `tools/contract_pmfs.py` 匹配。

## 行格式

- 成功：`ONBOX_PMFS_<STAGE>_PASS` + 空格分隔 `key=value`
- 失败：`ONBOX_PMFS_<STAGE>_FAIL stage=<S> code=<C>`
- 总闸：`ONBOX_PMFS_PASS`

## 内核必过子集（平级）

`PMEM0` `MOUNT` `STORE` `INDEX` `ALGO` `CRASH` `PERSIST`
`FILE` `CDC` `STREAM` `POS_FILE` `NEG_FILE` `CHAOS_FILE` `ABLATION`
`REMOUNT` `FAULT`
→ `ONBOX_PMFS_PASS`

`make contract-strict` 另要求：`POS/NEG_CDC`、`POS/NEG_STREAM`、`EDGE_FILE`、`ABLATION_A0/A4`、`FAULT_SEALMID`、`FAULT_HALFPAGE`、消融行含 `wa_x100=`。

## MAP / PMEM2

- MAP：`PMEM0 mode=MAP backend=map` + `PMEM1`
- PMEM2：`ONBOX_PMFS_PMEM2_PASS persist=ok`；脚本印 `PMEM2_CONTRACT_PASS boots=2`

## 用户态

- `_pmfs_probe` → `SYSCALL_PASS`
- `_pmfs_file` → `FILEUSER_PASS`
- `_pmfs_bench [N]` → `BENCH_PASS`（`median_file` / `thr_file` / `wa_x100`）
- `_pmfs_posix` → `POSIX_PASS`（**真实文件 API 深化**：mkdir/readdir/fsync/reopen）

## 语义证明

- `REMOUNT_PASS bytes=N equal=1`：sync 后元数据重载，文件内容字节等价
- `FAULT_SEALMID_PASS invisible=1`：未 fsync 写不可见
- `FAULT_HALFPAGE_PASS chk_reject=1`：inode 半页损坏被 magic+chk 拒绝

详见 `docs/SEMANTICS.md`。
