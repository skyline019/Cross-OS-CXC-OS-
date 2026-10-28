# 相关工作对照（论文第 2 章素材）

> 归属与差异须作者核对原文后再定稿；**不声称**首创 COW、缓存刷写或自动崩溃测试。  
> 候选贡献边界：[CONTRIBUTIONS.md](CONTRIBUTIONS.md)；前序自有：[PRIOR-ART-OWN.md](PRIOR-ART-OWN.md)。

| 工作 | 问题域 | 与本文（v3） |
|------|--------|----------------|
| xv6 `fs+log` | 教学块 FS | 对照基线；仍作根 FS；原生 `fsync` 无设备 flush |
| Linux PMFS（Dulloor et al., EuroSys'14） | PMEM 文件 FS | 动机：字节寻址布局；本文为教学内核**受限子集** |
| NOVA（Xu & Swanson, FAST'16） | 日志结构 PMEM FS | 动机：细粒度持久；本文不追求生产完整语义 |
| BPFS 等 | PMEM 上 COW / 细粒度持久 | 本文采用 COW 元数据+checkpoint，属同一家族，实现与验收自研 |
| CrashMonkey / PMTest / Witcher 等 | 崩溃/一致性自动测试 | 方法学对照；本文为 xv6+QEMU **进程重启切点 oracle** + 有界规格模型 |
| 作者 FCS | 嵌入式耐久 KV（原生前序） | 双槽/run/WA 等**约束下沉**；C 重写，不并入源码 |
| 作者 Aurora | 索引（原生前序） | 量化/posting/qcache 约束；xv6 重写 |
| cxc-os 类契约 | 串口阶段验收 | 手法参考；本课题码表/oracle 重写 |
| **本文 PMFS v3** | 教学 OS 上 `/pm` 子集 + KV/CDC + COW checkpoint + 可复现验收 | **尖刺：方法学验收资产**（非「又一个双槽」） |

## 建议正文对比轴（至少写清三轴）

1. **接口：** 完整 VFS vs 固定 `/pm` 子集 + 专用 syscall  
2. **持久假设：** 真机 PMEM 指令 vs QEMU touch/CBO + 声明的故障模型  
3. **验收：** 手工故障 vs 可脚本化进程重启 oracle / 有界模型负控  

防「移植题」标准表述见 [PRIOR-ART-OWN.md](PRIOR-ART-OWN.md)。
