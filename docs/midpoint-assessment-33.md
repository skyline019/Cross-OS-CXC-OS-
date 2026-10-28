# Midpoint assessment 33（planted debt clearance / M303–M306）

**日期**：会话落地窗（相对基线 M302 / v2.92.0 → **v2.93.0**）  
**结论**：HARD 预置 `.o` 债 **13→0** 已清完；`strtol→__intscan→__shgetc/__shlim` 调用图与 `hstb` ssp/errno/uflow/malloc/free 保留为种植实现。机上 CCDRV：`planted: 0`、`inputs_s: 57`、`spawn_as: 57`。**禁止简化**契约守住。**未宣称 A2。**

## 本窗交付对照

| 里程碑 | 状态 | 证据 |
|---|---|---|
| M303 mem/str/bridge | 完成 | `memrchr`/`memcpy`/`memmove`/`strcspn`/`strspn`/`strpbrk`/`strrchr`/`tcap_bridge_add` + 别名；HARD 重叠/空拒绝集/末次字符加深 |
| M304 hstb+strdup | 完成 | `plant_src` 多符号 stub（ssp/errno/uflow/malloc/free）+ `strdup`→`strlen`/`malloc`/`memcpy` |
| M305 strtol 链 | 完成 | 分叶 `strtol` / `__intscan` / `__shgetc` / `__shlim`；EOF→`__uflow`；符号/基数/endptr 加深 |
| M306 planted0 lock | 完成 | `planted == 0`；`spawn_as == inputs_s`；contract/check；本文件；**v2.93.0** |

## 冻结面（未削弱）

- `CXC_OS_SELF_TEST_PASS`、`peer_ok=1`、M13 Echo  
- `spawn_ld: 1`；`spawn_as == inputs_s`（双窗仍兼容 legacy `4`）  
- CCDRV：`ONBOX_CCDRV_PASS` + `exit: 42` + `WMSG:CD`；**`planted == 0`**  
- HARD 探针未删未缩；strtol 链未压成单叶；ssp 依赖未静默去掉  
- host `CC_CONF` + `--require-exec`；禁止 `compile-elf` 作 CI 绿条  

## 复现

```sh
python3 tools/run_as_plant_check.py
python3 tools/run_cc_host_test.py
python3 tools/cc_conformance.py --strict --elf-check --require-exec
cmake --build build/uefi --target qemu-test
# serial: planted: 0 inputs_s: 57 spawn_as: 57 ONBOX_CCDRV_PASS
```

## 下一窗建议

1. asm_mode / 语义网继续加深（不回退 planted 锁）  
2. Windows CI 保持 `mode=qemu|run`  
3. 仍 **未宣称 A2**
