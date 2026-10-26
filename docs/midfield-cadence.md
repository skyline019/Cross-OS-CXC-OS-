# 中场节奏契约

**规则**：每完成约 **10 个里程碑** 做一次正式中场评估，并在同一窗口内 **统一 CI、修问题、同步测试**。  
**M74 起补充**：中场之间以 **大跨步** 为主（非继续堆薄烟测切片）；大块变更必须预留调试点；边缘用例纳入测试契约且权重不低于成功路径（见 [`test-contract.md`](test-contract.md) §8）。  
**M76 起产品目标**：见 [`product-goals.md`](product-goals.md) — **A2 仍为一阶段主靶**（机上 g++/gcc 编译运行、不依赖宿主机）；客体为 **ELF 系统**；宿主 Win/Linux 可构建；允许相对 Linux/Windows 的创新内部设计；**完整功能优先**，遇瓶颈修债+调试点，禁止薄切片拆除通向 A2 的能力。

| 中场 | 覆盖 | 文档 |
|---|---|---|
| 第一次 | M0–M13 | [`midpoint-assessment.md`](midpoint-assessment.md) |
| 第二次 | M14–M24 | [`midpoint-assessment-2.md`](midpoint-assessment-2.md) |
| 第三次 | M25–M34 | [`midpoint-assessment-3.md`](midpoint-assessment-3.md) |
| 第四次 | M35–M44 | [`midpoint-assessment-4.md`](midpoint-assessment-4.md) |
| 第五次 | M45–M54 | [`midpoint-assessment-5.md`](midpoint-assessment-5.md) |
| 第六次 | M55–M64 | [`midpoint-assessment-6.md`](midpoint-assessment-6.md) |
| 第七次 | M65–M74 | [`midpoint-assessment-7.md`](midpoint-assessment-7.md) |
| 第八次 | M75–M84 | [`midpoint-assessment-8.md`](midpoint-assessment-8.md) |
| 第九次 | M85–M94 | [`midpoint-assessment-9.md`](midpoint-assessment-9.md) |
| 第十次 | 约 M95–M104 | [`midpoint-assessment-10.md`](midpoint-assessment-10.md) |
| 第十一次 | 约 M105–M114 | [`midpoint-assessment-11.md`](midpoint-assessment-11.md) |
| 第十二次 | 约 M115–M124 | [`midpoint-assessment-12.md`](midpoint-assessment-12.md) |

当前基线：**M129 / v2.20.0**（机上 mini-cc：as + mini-ld `link.rsp`→ET_EXEC；QEMU 验收底线 **128 MiB**）。下一正式中场目标：**约 M134**。

## 中场窗口必做

1. 撰写中场评估（进度 / 证据 / 债项 / 下一条主线）。
2. 统一 `.github/workflows/ci.yml` 与当前串口/peer 合同。
3. 修复门禁中暴露的问题——**必须修根因**（禁止只靠加超时、删矩阵/断言、重试“变绿”）。
4. 同步 `tools/contract_test.py`、host `ctest`、CI `grep` 与新增里程碑证据。

## 问题处理抉择（硬契约）

| 允许 | 禁止（除非同变更集已证明并修复根因） |
|---|---|
| 修内核/驱动/harness 缺陷 | 单纯加大 `--timeout` / 空转上限 |
| 补失败现场证据（串口/peer 入 CI 日志与 artifact） | 删除或跳过合同用例、SMP 矩阵、串口 grep |
| 保持或收紧 PASS/`peer_ok` 断言 | 用重试掩盖偶发 hang |
| 先加边缘/负例复现再用测试定位 | 无证据的大块重写 |

先补证据、再定根因、再改代码；不得以“先让 CI 绿”替代根因修复。

## 每个里程碑（中场之间）— M74 后大跨步

- **大跨步优先**：交付整层**完整**能力（例：持久块设备读写 API，而非探针后拆除）；避免无解除 A2 硬阻塞的零散烟测，也避免「PASS 后拆掉」制造新债。
- **A2 对齐**：切片须能说明如何缩短「机上 g++/gcc」路径；不能说明的旁路演示降级排期。
- **调试点预留**：大块代码同变更集必须有可观测 stage/failure/计数或门控调试输出，保证失败可定位。
- **遇瓶颈**：先调试点 → 修遗留债务 → 再完成功能；禁止缩小合同冒充完成。
- **用测试调试**：新增/收紧自动化（含边缘）先于或同于产品修复。
- 新串口证据 → 同变更集更新 `contract_test.py`；CI Assert `grep` 同步。
- 保持 `PASS` + `peer_ok=1`；冻结的 M13 Echo / peer 字符串不改写。
- **边缘纳入范围**：非法输入、边界长度、缺资源、busy/ENOENT 等按 [`test-contract.md`](test-contract.md) §8；边缘失败与成功路径失败同等阻塞合并。
