# 论文图清单

> **旧 SVG / 串口图基于历史 BENCH，不得直接支撑新性能或掉电结论。**  
> 重绘图数据请用 `docs/results/2026-10-28-*`；写法见 [EXPERIMENT.md](../EXPERIMENT.md)。  
> 架构叙述以 [ARCHITECTURE.md](../ARCHITECTURE.md) / [CORE-WALKTHROUGH.md](../CORE-WALKTHROUGH.md) 为准。

| 文件 | 用途 | 状态 |
|------|------|------|
| `fig-n-latency.svg` | 历史 N–延迟 | 须按 perf 结果重绘 |
| `fig-wa-vs-n.svg` | 历史 WA vs N | 须按新计数重绘 |
| `fig-pmem2-serial.md` | 旧 PMEM2 魔数串口 | 附录 smoke；非掉电证明 |
| `fig-arch-mermaid.md` | 架构一层图 | 对照 ARCHITECTURE 更新后再用 |

建议新图（重写论文时）：

1. `/pm` VFS 与 checkpoint 发布顺序（自 ARCHITECTURE）  
2. recovery oracle 切点示意（自 CONTRACT / TRACE-REPLAY）  
3. 对齐工作量延迟/吞吐（自 results/2026-10-28-perf）  
4. 同 PMFS 软件请求消融（自 results/2026-10-28-efficiency）  
