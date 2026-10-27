# 相关工作对比（论文第 2 章素材）

| 工作 | 问题域 | 与本文 |
|------|--------|--------|
| xv6 fs+log | 教学块 FS | 对照基线；仍作根 FS |
| Linux PMFS（Dulloor et al., EuroSys'14）/ NOVA（Xu & Swanson, FAST'16） | PMEM 文件 FS | 动机参考；本文为 **可测文件子集 + KV 核** |
| **作者 FCS** | 嵌入式耐久引擎（原生前序） | **约束下沉**；代码自研重写 |
| **作者 Aurora** | 索引（原生前序） | 量化/posting 约束；xv6 重写 |
| cxc-os | 自研内核契约工程 | `ONBOX_*` 手法参考；PMFS 码表重写 |
| **本文 PMFS** | 教学 OS 上文件子系统 + KV 核 + **契约套件** | 尖刺 = 可机器判定崩溃/掉电 |

防「移植题」标准答：`PRIOR-ART-OWN.md`。
