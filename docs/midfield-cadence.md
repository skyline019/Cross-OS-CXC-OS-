# 中场节奏契约

**规则**：每完成约 **10 个里程碑** 做一次正式中场评估，并在同一窗口内 **统一 CI、修问题、同步测试**。

| 中场 | 覆盖 | 文档 |
|---|---|---|
| 第一次 | M0–M13 | [`midpoint-assessment.md`](midpoint-assessment.md) |
| 第二次 | M14–M24 | [`midpoint-assessment-2.md`](midpoint-assessment-2.md) |
| 第三次 | M25–M34 | [`midpoint-assessment-3.md`](midpoint-assessment-3.md) |
| 第四次 | M35–M44 | [`midpoint-assessment-4.md`](midpoint-assessment-4.md) |
| 第五次（计划） | M45–M54 | `midpoint-assessment-5.md`（届时新建） |

当前基线：**M52 / v0.53.0**（mini-libc write/_exit）。下一正式中场目标：**M54**。

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

先补证据、再定根因、再改代码；不得以“先让 CI 绿”替代根因修复。

## 每个里程碑（中场之间）

- 新串口证据 → 同变更集更新 `contract_test.py` 成功路径断言。
- 新验收标记 → CI 默认 `qemu-test` 后的 Assert 步骤同步 `grep`。
- 保持 `PASS` + `peer_ok=1`；冻结的 M13 Echo / peer 字符串不改写。
