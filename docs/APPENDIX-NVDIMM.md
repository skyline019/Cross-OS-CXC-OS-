# NVDIMM 与替代后端：设备选择备忘

> 题目要求的是 QEMU 模拟的 NVDIMM 设备；未打补丁的 MAP / 整机共享 RAM **不**等于 NVDIMM。本仓已有实验性补丁路径，细节见[QEMU-NVDIMM.md](QEMU-NVDIMM.md)。完整决策与验收见[优秀毕设路线图](EXCELLENCE-ROADMAP.md)；总索引见[README.md](README.md)。

| 路线 | 与现有 RISC-V 基础的关系 | 可宣称的范围 | 风险 |
|---|---|---|---|
| 当前 MAP / 共享 RAM | 普通 RAM 窗口 / 整机共享文件 | 进程重启魔数及文件/KV 内容回归 | 不是独立设备；没有物理缓存持久域与掉电证明 |
| QEMU `virt` 接入 `nvdimm` | 已提供 9.2.0 实验补丁，单固定冷插拔设备、私有 FDT | 验证 QMP 设备身份、映射和进程重启；见 `QEMU-NVDIMM.md` | 非上游通用支持；无 NFIT/热插拔；普通文件不证明物理掉电 |
| QEMU x86 PC + `nvdimm` | 可参考官方示例，但需要另做 x86 xv6 移植 | x86 下的设备和指令实验 | 与本仓 RISC-V 主线差异大，不是简单更换 QEMU 参数 |
| `virtio-pmem` | 先验证目标 machine 的实际支持，并实现相应发现/flush | 独立的半虚拟 PMEM 设备 | 不等于 NVDIMM；文件后端持久化协议与 CPU 缓存刷写不同，需导师确认是否符合题意 |
| 宿主机 DAX / 真 PMEM | 若有硬件可做补充实测 | 依赖具体硬件、平台持久域与映射条件 | 不属于现有 xv6/QEMU 证据；无法假设普遍可得 |

无论选择哪条路线，都要分别举证：设备识别及地址映射、guest 实际刷写指令、平台持久域/原子性假设、文件恢复、故障模型和对比实验。QEMU TCG 不模拟完整缓存层级，普通文件后端的宿主掉电持久性也不能从进程重启推出。

参考：[QEMU vNVDIMM 文档 v9.2.0](https://github.com/qemu/qemu/blob/v9.2.0/docs/nvdimm.txt)、[QEMU virtio-pmem 文档 v9.2.0](https://github.com/qemu/qemu/blob/v9.2.0/docs/system/devices/virtio-pmem.rst)、[当前 QEMU 后端说明](QEMU-NVDIMM.md)。
