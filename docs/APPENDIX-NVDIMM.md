# 附录：真 NVDIMM / virtio-pmem（规划，非主线）

> **不要喧宾夺主：** 顶批主线是 KV + **契约尖刺**；真设备仅附录。未做不等于缺陷。

主线验收以 **QEMU 扩容 RAM + memory-backend-file（PMEM2）** 为准。

## A. 目标

把现有 `pmem_*` API 接到真实或更接近的持久内存后端，保持：

- `pmem_base_va` / `pmem_size` / `pmem_persist`
- 上层 `pmfs` / `pmalgo` **零改或少改**

## B. 可选路径

| 路径 | 平台 | 工作量 | 说明 |
|------|------|--------|------|
| A. 现状 PMEM2 | RISC-V virt | 已完成 | 文件后端掉电 |
| B. virtio-pmem | 视 QEMU 配置 | 中 | 需设备探测与 BAR 映射 |
| C. x86 pc + nvdimm | x86 实验机 | 高 | ACPI NFIT；需另分支内核 |
| D. 宿主机 DAX 文件 | Linux + mmap | 中 | 用户态对照实验，非 xv6 内 |

## C. 建议答辩表述

> 架构上已把「字节窗口 + persist」从块设备栈解耦；主线用 QEMU 文件后端完成掉电契约。  
> 真 NVDIMM 属于设备枚举与固件差异，列为附录扩展，不改变存储核与算法结论。

## D. 若实施 B 的检查单

1. QEMU 启用 virtio-pmem；记录 guest 物理基址。  
2. `kvmmap` 该范围；`pmem_ready` 探测。  
3. 复跑 `contract-pmem` / 自研掉电脚本。  
4. 更新 `BENCH.md` 增「后端=virtio-pmem」一行。
