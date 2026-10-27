# 公平消融 A0–A4（CDC / Seal / Fixed）

同一 payload、同一 PMFS 页设备与 KV 核；仅切换切分与刷写策略。

| 代号 | CDC | batch seal | 含义 |
|------|-----|------------|------|
| A0 | 开 | 开 | 默认：Rabin 切分 + flush 时 seal |
| A1 | 关 | 开 | 定长 chunk（消融对照） |
| A2 | 开 | 关 | 每 chunk 立即 seal |
| A3 | 关 | 关 | 定长 + 立即 seal |
| A4 | 开 | 开 | 复测 A0（稳定性） |

内核自测输出：

```
ONBOX_PMFS_ABLATION_A0_PASS cdc=1 batch=1 dputs=… dseals=…
…
ONBOX_PMFS_ABLATION_PASS variants=5 fair=1
```

`dputs` / `dseals` 为相对融合计数增量，证明文件路径仍走 `pm_put` / `pm_seal`。

运行：`make contract-strict`（含 ABLATION 闸门）。
