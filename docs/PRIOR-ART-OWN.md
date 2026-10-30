# 作者前序原生项目与本课题关系

> 答辩口径：**FCS、Aurora 是作者本人的原生前序工作**；本毕设是把其中**已验证的设计约束**下沉到 xv6 教学内核，并新增**可复现提交验收方法学**与受限 `/pm` 文件子系统。不是「抄别人开源 PMEM-FS/KV」。

## 1. 关系图

```text
作者前序（原生）                 本课题（xv6-riscv / PMFS v3）
─────────────────               ─────────────────────────────
FCS 耐久存储引擎     ──约束──►   双槽 checkpoint / run / WA / 压缩思想
Aurora 索引思想      ──约束──►   量化桶 / posting / qcache
cxc-os 契约工程      ──手法──►   串口门 + recovery/model 脚本（重写）
                               ▲
                               │ 本仓库自研 C + VFS /pm + NVDIMM 补丁路径
                               │ 尖刺：可复现验收，非算法首创声明
```

## 2. 什么算「自研」、什么算「约束复用」

| 类别 | 内容 |
|------|------|
| 约束复用 | 「只承诺完好提交点」「不可变 run」「有界索引」等语义 |
| 本仓自研 | 全部 `kernel/pm*.c`、VFS `/pm`、用户工具、QEMU 补丁与测试脚本 |
| 本仓新增尖刺 | 进程重启切点 oracle、有界规格负控、轨迹重放、对齐工作量 bench |
| 明确不入库 | FCS/Aurora/cxc-os 源码树 |

## 3. 被问「是不是移植题」时的标准答

1. 前序是我的项目；本课题换到 **xv6 + RISC-V + QEMU**，代码路径与运行时不同。  
2. 是 **约束下沉 + 重写**，不是 diff 搬运；对照表见 [BORROWINGS.md](BORROWINGS.md)。  
3. 差异化在 **可复现验收 + `/pm` 文件子集**；不声称首创 COW/PMEM-FS。

## 4. 路径索引（只读，不入库）

- FCS / Aurora：见本机前序工程与 [AURORA-INDEX.md](AURORA-INDEX.md)、[FCS-STORAGE.md](FCS-STORAGE.md)  
- 总索引：[README.md](README.md)
