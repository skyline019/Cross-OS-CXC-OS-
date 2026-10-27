# FCS 底层存储 → PMFS 对照

## 提交协议（与 FCS `sb_flush` 同构）

```
1. 写数据页并 persist          // pmfs_write_data_page
2. pmdev_sync()                // 屏障：数据先入持久域
3. 清交替槽 → 写新 SB 页       // 含 COMMIT_WORD + chk
4. pmdev_sync()                // 屏障：SB 持久
5. 仅成功时 sb_slot = slot     // 失败不翻槽，避免擦掉唯一完好点
```

掉电语义：半写 SB ⇒ `chk`/`state` 失败 ⇒ `pmfs_mount` 选另一槽更高 `epoch`。

## 布局

| 页号 | 用途 |
|---|---|
| 0 | SB slot A |
| 1 | SB slot B |
| 2… | 数据（`tail` 单调分配） |

## EMUL

无 NVDIMM 时，`pmfs_init` 使用 2MiB 静态缓冲模拟 FCS `RamDev`，串口应出现：

```text
pmfs: using EMUL buffer ...
pmfs: mount ok ...
pmfs: STORAGE_SELFTEST_PASS ...
```
