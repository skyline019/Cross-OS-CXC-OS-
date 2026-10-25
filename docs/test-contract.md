# CXC/OS 中场测试契约

## 1. 目的与基本原则

本契约覆盖 M13 / v0.14.0 十帧 UDP/ring 合同，以及 M14 / v0.15.0 在同环上追加的 256 帧 stress（不改写 M13 的 10/10 串口证据）。实现位于：

- `tools/contract_test.py`：主机模型检查、场景编排、结果与哈希生成；
- `tools/run_qemu.py`：保留原有 runner，并增加隔离 UDP 故障和确定性硬掉电控制；
- `contract-results/`：契约执行结果目录。

基本原则：

1. 正常成功必须同时有内核 PASS、M13 十帧 peer、M14 256 帧 stress（`validation_scope=m14-raw-arp-icmp-udp-ring-stress`）和 UDP/ring 不变量；
2. 失败路径必须出现预期 panic 且无 `CXC_OS_SELF_TEST_PASS`；
3. 故障 fixture 必须尽量只改变一个语义，避免把 checksum 错误误当成端口/长度错误；
4. 测试工具本身也接受契约检查，不能通过修改预期来隐藏真实失败；
5. 主机模型、QEMU 实测、持久化能力和形式证明是不同证据层级，不互相冒充。

## 2. 覆盖矩阵

| 契约类别 | 自动化内容 | 本次证据 |
|---|---|---|
| 正确 | 13 个 golden response、IPv4/UDP checksum、正常完整启动、掉电后的干净启动 | 全部通过 |
| 失败路径 | 每比特损坏、截断/扩展、缺设备/无 peer、坏 ICMP/UDP、IPv4 checksum、UDP length、padding、port、fragment、长帧截断 | 全部按预期拒绝 |
| 掉电 | 在 packet/protocol/UDP ready 和第 3/8 个 UDP reply 后 SIGKILL QEMU；混沌场景另有两处掉电 | 7 次硬掉电；镜像哈希不变；恢复启动通过 |
| 复合 | SMP/内存/APIC 与全部设备和协议组合；wrap 后的故障点 | 全部通过 |
| 嵌套 | 外层 PMM/SMP/EDU/raw/ARP/ICMP 已成功，内层 UDP 最后一步失败；主机 100×100 嵌套循环 | 逆序失败路径无 PASS；模型 10000 次通过 |
| 循环 | 五次完整冷启动循环；主机 ring 模型 10000 次提交 | 5/5 启动；模型 1250 次/ring wrap |
| 能力矩阵 | UP+64 MiB、8 vCPU+xAPIC+256 MiB，以及固定/随机 APIC、CPU、内存组合 | 全部通过 |
| 复杂 | 16 vCPU+512 MiB 全功能复合、前九帧提交后第十帧失败 | 全部通过 |
| 事务 | 主机模型覆盖十个失败点；QEMU 验证第九帧已提交、第十帧 checksum 失败 | 失败帧无 reply/recycle 部分提交 |
| 高压 | 16 vCPU+512 MiB 重复全功能启动，叠加内核既有 PMM 2000 步/heap 1000 步；主机 10000 次 ring 与随机帧 | 全部通过 |
| 混沌 | 固定种子 20261025；六个随机拓扑/故障/掉电 QEMU 场景；10000 个随机 blob×4 validator | 全部通过或可复现拒绝 |

“嵌套”不是宣称存在 nested transaction API；“事务”是当前自检路径的软件提交合同；“高压”是有限时长复合压力，不是数小时 soak。

## 3. 主机侧检查

`contract_test.py --host-only` 执行 9 项快速检查：

1. **Golden correctness**：13 个响应；UDP payload=148、UDP frame bytes=642、全部请求或响应合计 836 字节；
2. **Exhaustive bits**：6688 个单比特损坏全部被对应 validator 拒绝；
3. **长度路径**：875 个截断/扩展变体全部被拒绝；
4. **隔离故障矩阵**：60 个可用于负对照的 UDP fixture；
5. **嵌套循环模型**：100×100，共 10000 次 ring commit，四个 head/tail 序列各 wrap 1250 次；
6. **256 帧稳定性**：顺序 commit 256 次，每环 wrap=32，free slots 保持 4；
7. **突发满环**：`observe_only` 填满 4 个 free slot 后拒绝超额，M13 十帧路径仍可用；
8. **事务失败点模型**：第 0–9 帧分别失败，允许 RX 已观察，但 reply/recycle 不得部分提交；
9. **确定性混沌**：10000 个随机 blob 分别送入 4 个 validator，无异常且全部拒绝。

主机 ring 模型不是内核执行证据；真实 NIC 仍以每次启动的 M13 十帧 + M14 256 帧 stress 合同和 QEMU 故障场景为准。

## 4. 新增 QEMU 故障注入

### 隔离 UDP 故障

```bash
python3 tools/run_qemu.py --image artifacts/cxc-os.img --check \
  --udp-fault checksum --udp-fault-index 9 \
  --expect 'CXC_OS_PANIC: e1000e MSI-X' \
  --work-dir build/fault-udp-9
```

可选 fault：

| kind | 语义 | index 限制 |
|---|---|---|
| `checksum` | 翻转 UDP checksum | 0–9 |
| `ipv4-checksum` | 翻转 IPv4 header checksum | 0–9 |
| `udp-length` | UDP length 减一并重算 checksum，隔离长度不一致 | 0–9 |
| `padding` | 修改最短帧 padding，其他 checksum 保持有效 | 0–6 |
| `port` | 修改目的端口并重算 UDP checksum | 0–9 |
| `fragment` | 设置 MF 并重算 IPv4 checksum | 0–9 |
| `truncated` | 删除一个实际 IP/payload 帧字节 | 7–9 |

`padding` 只适用于有 Ethernet padding 的短帧；`truncated` 只适用于无 padding 的长帧。

### 确定性硬掉电

```bash
python3 tools/run_qemu.py --image artifacts/cxc-os.img --check \
  --power-cut-after-udp 3 --work-dir build/power-after-3
```

也支持 `--power-cut packet-ready`、`protocol-ready`、`udp-ready`。peer 在对应阶段暂停，主循环再 SIGKILL QEMU，防止“看到 marker 时已经推进到下一阶段”的工具竞态。第 N 个 UDP reply 后同样暂停，N 只允许 1–9。

掉电合同仅包括：

- QEMU 被硬杀，未输出正常 PASS；
- `snapshot=on` 的原始镜像 SHA-256 不变；
- 后续全新 QEMU 启动能重新通过完整 M13+M14 合同。

不包括宿主机真实断电、物理机断电、持久化数据恢复、fsync 原子性或文件系统崩溃一致性；当前没有这些持久化机制。

## 5. 事务与复合失败合同

`transaction-ninth-commit-then-fault` 在前九帧完整成功后，对第十帧注入坏 UDP checksum。强制结果：

- request/reply=`9/9`；RX/TX IRQ=`10/9`；
- heads=`5/4`、tails=`0/4`；
- payload=115，RX/TX committed frame bytes=`567/567`；
- wrap=`1/1/2/1`，recycled=9；
- 无第十帧 reply，无 PASS。

这证明当前同步自检路径在失败前没有提交回复和回收计数。它不是通用事务、回滚或掉电持久性声明；硬件 RX head 可以已经前进。

## 6. 测试工具自身发现的问题

首次完整执行为主机 7/7、QEMU 36/37。失败场景是随机选择的短帧 `truncated@6`：该帧 IP 数据只有 58 字节，原始 60 字节中含两个 Ethernet padding；删除一个 padding 后，NIC 仍补到合法最短帧，内核接受是正确行为。

处理不是忽略失败，而是：

- 保留失败原因；
- 将真正截断负例限制到无 padding 的 index 7–9；
- 修正混沌生成器和 fixture 矩阵；
- 从头重跑完整契约，最终主机 7/7、QEMU 37/37。

此前还发现 marker 掉电可能越过目标阶段，已通过 peer 阶段暂停点修复。这两处都属于测试工具契约修正，不是通过放宽内核检查获得通过。

## 7. 本次结果与复现

本次完整结果：

- 主机检查：**7/7 PASS**；
- QEMU 场景：**37/37 PASS**；
- `contract-results/`：37 个 QEMU 场景目录、stdout/serial/peer 日志、JSON 报告、文本说明；
- 结果目录共 112 个文件，`SHA256SUMS` 包含其余 111 个文件，已执行全量哈希检查。

复现：

```bash
python3 tools/contract_test.py \
  --image artifacts/cxc-os.img \
  --work-dir contract-results \
  --seed 20261025

python3 tools/contract_test.py --host-only \
  --work-dir build/contract-host --seed 20261025
```

结果摘要见 `contract-results/CONTRACT_VALIDATION.txt`，机器可读命令和耗时见 `contract-results/contract-report.json`。

CI 配置加入 host-only 检查和 Linux 完整契约，但本环境仍未实际执行远端 GitHub Actions 或 Windows 本地构建，不作已通过声明。

## 8. 边缘合同（M74 起生效；权重不低于成功路径）

自第七次中场（[`midpoint-assessment-7.md`](midpoint-assessment-7.md)）起，测试契约**显式纳入边缘**，且对通向 A2 的新主线（块设备 / 持久根 / 大跨步 syscall）**边缘比较重要**。

### 8.1 原则

1. **成功路径仍必要**，但不单独构成「可合并」；缺少约定边缘用例视为合同未完成。  
2. **边缘与负例失败与成功路径失败同等阻塞**：不得删边缘断言换绿；不得用「happy path 已绿」豁免。  
3. **用测试调试**：先写可复现边缘/负例（host 模型或 QEMU），再改内核/驱动；大块变更同变更集预留调试点（串口 stage/failure、路径计数等）。  
4. 冻结的 M13 Echo / peer 成功字符串**仍不改写**；边缘扩域加在用户态/FS/块设备/新 syscall 面。

### 8.2 边缘类别（最低清单；按切片增量落地）

| 类别 | 示例（随切片具体化） | 期望 |
|---|---|---|
| 非法输入 | 空路径、超长路径、坏 flags、越界 LBA | 明确 errno / 拒绝；无 panic（除非刻意故障探针） |
| 缺失/不存在 | `ENOENT` open/unlink；空设备 | 可区分于其他错误 |
| 资源/忙 | fd 表满、inode 满、unlink 时 fd 仍打开 | 稳定拒绝；可回归 |
| 边界长度 | 0 字节读写、恰好 `max_file_bytes`、BUFSIZ 边界 | 与成功路径同批证据 |
| 权限/类型 | 对目录 unlink、对文件 getdents | 拒绝且不破坏其他 inode |
| 故障注入 | 块设备读失败、短读（后续切片） | 有串口/合同标记；可定位 |

### 8.3 落地节奏

- **M74 窗口**：立约（本节）；默认 `qemu-test` 成功路径保持 M13–M73 绿。  
- **M75+ 大跨步**：每个大块交付至少附带该切片相关的边缘自动化（host 或 QEMU）；缺边缘 = 变更集未完成。  
- **M84 窗口**：第八次中场核对 M75–M83 边缘（OOB LBA、rename miss/xdev、ECHILD 等）仍在 CI；默认成功路径保持绿。  
- Full midpoint（正确/失败/掉电/…）网络矩阵继续有效；用户/块设备边缘是**追加面**，不替代也不削弱网络合同。
