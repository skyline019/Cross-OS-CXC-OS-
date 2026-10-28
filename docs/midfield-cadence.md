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
| 第十三次 | 约 M125–M134 | [`midpoint-assessment-13.md`](midpoint-assessment-13.md) |
| 第十四次 | 约 M135–M144 | [`midpoint-assessment-14.md`](midpoint-assessment-14.md) |
| 第十五次 | 约 M145–M154 | [`midpoint-assessment-15.md`](midpoint-assessment-15.md) |
| 第十六次 | 约 M155–M164 | [`midpoint-assessment-16.md`](midpoint-assessment-16.md) |
| 第十七次 | 约 M165–M174 | [`midpoint-assessment-17.md`](midpoint-assessment-17.md) |
| 第十八次 | 约 M175–M184 | [`midpoint-assessment-18.md`](midpoint-assessment-18.md) |
| 第十九次 | 约 M185–M194 | [`midpoint-assessment-19.md`](midpoint-assessment-19.md) |
| 第二十次 | 约 M195–M204 | [`midpoint-assessment-20.md`](midpoint-assessment-20.md) |
| 第二十一次 | 约 M205–M214 | [`midpoint-assessment-21.md`](midpoint-assessment-21.md) |
| 第二十二次 | 约 M215–M224 | [`midpoint-assessment-22.md`](midpoint-assessment-22.md) |
| 第二十三次 | 约 M225–M234 | [`midpoint-assessment-23.md`](midpoint-assessment-23.md) |
| 第二十四次 | 约 M235–M244 | [`midpoint-assessment-24.md`](midpoint-assessment-24.md) |
| 第二十五次 | 约 M245–M254 | [`midpoint-assessment-25.md`](midpoint-assessment-25.md) |
| 第二十六次 | 约 M255–M264 | [`midpoint-assessment-26.md`](midpoint-assessment-26.md) |
| 第二十七次 | 约 M265–M274 | [`midpoint-assessment-27.md`](midpoint-assessment-27.md) |
| 第二十八次 | 约 M275–M284 | [`midpoint-assessment-28.md`](midpoint-assessment-28.md) |
| 第二十九次 | 约 M285 | [`midpoint-assessment-29.md`](midpoint-assessment-29.md) |
| 第三十次 | 约 M286 | [`midpoint-assessment-30.md`](midpoint-assessment-30.md) |
| 第三十一次 | 约 M287–M298 | [`midpoint-assessment-31.md`](midpoint-assessment-31.md) |
| 第三十二次 | 约 M299–M302 | [`midpoint-assessment-32.md`](midpoint-assessment-32.md) |

当前基线：**M302 / v2.92.0**（GUIUX 仍可选；迷你 IDE + CCDRV 串口主合同；QEMU 验收底线 **256 MiB**）。  
**A2-SEM（M287–M302）**：语义网 59/59 + `planted:13`/`inputs_s:44` + asm_mode 深 CF + Win `CC_CONF --require-exec` — 见 [`midpoint-assessment-32.md`](midpoint-assessment-32.md)；方言 [`cxc-a2-dialect.md`](cxc-a2-dialect.md)。

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
