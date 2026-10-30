# 消融实验说明

> **旧 A0–A4 表仅为历史原型观测，不得作为论文公平消融结论。**  
> 当前同 PMFS 内软件消融与请求计数见：  
> - [results/2026-10-28-efficiency/README.md](results/2026-10-28-efficiency/README.md)  
> - [PERFORMANCE-ENDURANCE.md](PERFORMANCE-ENDURANCE.md)  
> - 访客工具 `pmstats`；测试 `./test-xv6.py efficiency`

## 当前消融在测什么

在**同一 PMFS 后端与载荷族**上切换策略（例如 CDC vs 固定分块、相同内容写跳过、索引溢出回退），记录：

- 软件写/刷写/查询**请求计数**（不是介质磨损）  
- 必要的功能正确性断言  

公平面只陈述这些效应量；架构面（相对 xv6）另见 [results/2026-10-28-perf](results/2026-10-28-perf/README.md)。两套结论禁止混读。

## 历史 A0–A4（留档）

同一 payload、旧契约自测切换 CDC 与 batch seal；`cycles` 实为 `r_time`；设备/刷写/持久语义当时未充分验证。

| 代号 | CDC | batch seal | 含义 |
|------|-----|------------|------|
| A0 | 开 | 开 | Rabin + flush 时 seal |
| A1 | 关 | 开 | 定长 + 批量 seal |
| A2 | 开 | 关 | 每 chunk 立即 seal |
| A3 | 关 | 关 | 定长 + 立即 seal |
| A4 | 开 | 开 | A0 复测 |

历史抽样（EMUL，仅追溯）：

| 变体 | cycles | wa_x100 |
|------|--------|---------|
| A0 | 30029 | 1600 |
| A1 | 27490 | 1600 |
| A2 | 52066 | 4800 |
| A3 | 36238 | 3200 |
| A4 | 24146 | 1600 |

运行旧路径：`make contract-full`（破坏性，仅 disposable）。新实验请走 efficiency / perf 结果目录。
