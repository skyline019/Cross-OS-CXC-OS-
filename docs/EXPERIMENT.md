# 实验章写法指引（对齐 PMFS v3）

> 本文是**写作模板与证据指针**；第 5 章已写入 `thesis/latex/chapters/chap05.tex`。  
> 原始日志与统计以 [results/README.md](results/README.md) 为准；旧 [BENCH.md](BENCH.md) / [ABLATION.md](ABLATION.md) 仅历史。

## 5.1 环境

| 项 | 写什么 |
|----|--------|
| 基线 | xv6-riscv，本课题 PMFS v3 分支 |
| 模拟器 | QEMU `virt`；写明版本（如 9.2.0）与是否打 NVDIMM 补丁 |
| 后端 | EMUL / 普通 MAP / 共享 RAM / 实验性独立 NVDIMM；**勿把 MAP 写成 NVDIMM** |
| 刷写 | 默认 touch+fence；可选 Zicbom `CBO.CLEAN` + 平台假设 |
| 并发 | 报告实际 `CPUS=` |
| 计时 | `r_time` 相对 ticks；不作墙钟 µs / 真机 MB/s |

复现入口：[CONTRACT.md](CONTRACT.md)、[PMFS-REPAIR-REPORT.md](PMFS-REPAIR-REPORT.md)、[QEMU-NVDIMM.md](QEMU-NVDIMM.md)。

## 5.2 功能与恢复（尖刺证据）

| 类别 | 建议引用 | 可说 | 不可说 |
|------|----------|------|--------|
| 安全挂载 | repair report / recovery | 空白/v2/损坏拒绝；显式 format | 自动修好任意损坏 |
| 文件/KV 跨进程读回 | `test-recovery*` | 同后端进程杀后字节一致（oracle 切点） | 物理掉电 / 缓存乱序 |
| VFS | `test-vfs*`、[results/...-vfs](results/2026-10-28-vfs/README.md) | `/pm`、硬链接、子树 rename | 完整 POSIX |
| 规格模型 | `test-model` | 有界协议 + 负控抓错 | 从内核抽出的形式化证明 |
| 轨迹 | [TRACE-REPLAY](TRACE-REPLAY.md) | 源级范围事件采样 | 指令级穷举 |

旧 `ONBOX_PMFS_*` / `contract-pmem2` 魔数：可作 smoke 附录，**不能**单独支撑掉电章节。

## 5.3 性能与消融

### 架构面（相对 xv6）

- 证据：[results/2026-10-28-perf](results/2026-10-28-perf/README.md)  
- 要求：普通 fd、相同成功操作数、读回/EOF 校验、原始 ticks  
- 必须写明：xv6 `fsync`≠设备 flush；PMFS 默认≠硬件写回 → **持久语义不等价**

### 公平面（同 PMFS 内）

- 证据：[results/2026-10-28-efficiency](results/2026-10-28-efficiency/README.md)、[PERFORMANCE-ENDURANCE](PERFORMANCE-ENDURANCE.md)  
- CDC/固定、写跳过、索引溢出、`pmstats` 软件请求计数  
- 不可把软件计数说成介质磨损改善  

两套结论**分表**，禁止混读旧 fratio。

## 5.4 维护与空间

- [results/2026-10-28-gc](results/2026-10-28-gc/README.md)、[MAINTENANCE](MAINTENANCE.md)  
- 页复用、显式压缩、满盘安全失败  

## 5.5 实现难点

- [PITFALLS.md](PITFALLS.md)  
- [LOCAL-SANDBOX-TESTS.md](LOCAL-SANDBOX-TESTS.md)（修复前失败史）  

## 5.6 威胁效度（建议固定四条）

1. QEMU TCG / 普通文件后端不能证明物理持久域。  
2. 进程 `SIGKILL` ≠ 宿主掉电或缓存丢写。  
3. 性能样本环境与工具链需写明；外推真机需重新校准。  
4. 资源上限与粗粒度锁限制外部效度。  

## 5.7 小结句（可用）

> 实验主线是可复现的提交协议与故障 oracle；性能报告对齐工作量下的软件路径成本，并显式分离持久语义不等价与设备模型边界。
