# 中场节奏契约

**规则**：每完成约 **10 个里程碑** 做一次正式中场评估，并在同一窗口内 **统一 CI、修问题、同步测试**。

| 中场 | 覆盖 | 文档 |
|---|---|---|
| 第一次 | M0–M13 | [`midpoint-assessment.md`](midpoint-assessment.md) |
| 第二次 | M14–M24 | [`midpoint-assessment-2.md`](midpoint-assessment-2.md) |
| 第三次（计划） | M25–M34 | `midpoint-assessment-3.md`（届时新建） |

当前基线：**M25 / v0.26.0**（第三次中场周期内）。下一正式中场目标：**M34**。

## 中场窗口必做

1. 撰写中场评估（进度 / 证据 / 债项 / 下一条主线）。
2. 统一 `.github/workflows/ci.yml` 与当前串口/peer 合同。
3. 修复门禁中暴露的问题（禁止靠删断言或盲目加超时“变绿”）。
4. 同步 `tools/contract_test.py`、host `ctest`、CI `grep` 与新增里程碑证据。

## 每个里程碑（中场之间）

- 新串口证据 → 同变更集更新 `contract_test.py` 成功路径断言。
- 新验收标记 → CI 默认 `qemu-test` 后的 Assert 步骤同步 `grep`。
- 保持 `PASS` + `peer_ok=1`；冻结的 M13 Echo / peer 字符串不改写。
