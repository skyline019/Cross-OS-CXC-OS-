# 公平消融 A0–A4（CDC / Seal / Fixed）+ 效应量

同一 payload、同一 PMFS 页设备与 KV 核；仅切换切分与刷写策略。

| 代号 | CDC | batch seal | 含义 |
|------|-----|------------|------|
| A0 | 开 | 开 | 默认：Rabin 切分 + flush 时 seal |
| A1 | 关 | 开 | 定长 chunk（消融对照） |
| A2 | 开 | 关 | 每 chunk 立即 seal |
| A3 | 关 | 关 | 定长 + 立即 seal |
| A4 | 开 | 开 | 复测 A0（稳定性） |

内核自测输出（含效应量）：

```
ONBOX_PMFS_ABLATION_A0_PASS cdc=1 batch=1 dputs=… dseals=… cycles=… wa_x100=…
…
ONBOX_PMFS_ABLATION_PASS variants=5 fair=1 effects=1
```

### 实测效应量（EMUL，`make contract-strict` 抽样）

| 变体 | CDC | batch | cycles | wa_x100 |
|------|-----|-------|--------|---------|
| A0 | 1 | 1 | 30029 | 1600 |
| A1 | 0 | 1 | 27490 | 1600 |
| A2 | 1 | 0 | 52066 | 4800 |
| A3 | 0 | 0 | 36238 | 3200 |
| A4 | 1 | 1 | 24146 | 1600 |

- `cycles`：该变体 write+fsync 的相对时间（`r_time`）
- `wa_x100`：该变体增量 phys/logical×100（KV 密封路径）
- `dputs` / `dseals`：融合计数，证明未旁路 `pm_put`/`pm_seal`
- 解读：关闭批量 seal（A2/A3）抬升 WA/时延；A0/A4 同配置复测同量级

## 两套结论口径

| 平面 | 怎么说 | 怎么不说 |
|------|--------|----------|
| **公平面** | A0–A4 同 payload、同设备，只改切分/刷写 | 不宣称绝对最优 CDC |
| **架构面** | PMFS-File vs xv6 `fs+log` 量级差来自去 virtio 日志 | 不把 fratio 当同等 POSIX 加速比 |

运行：`make contract-strict`；全量 N 扫：`make bench-sweep`。
