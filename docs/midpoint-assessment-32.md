# Midpoint assessment 32（A2 deepen / M299–M302）

**日期**：会话落地窗（相对基线 M298 / v2.91.0 → **v2.92.0**）  
**结论**：asm_mode 深控制流 + planted batch3 + Windows `CC_CONF` 真跑（`mode=qemu|run`）已落地。机上 CCDRV：`planted: 13`、`inputs_s: 44`、`spawn_as: 44`。**未宣称 A2。**

## 本窗交付对照

| 里程碑 | 状态 | 证据 |
|---|---|---|
| M299 asm_mode CF | 完成 | `S-for`/`S-do`/`S-switch`/`S-break`/`S-continue`/`S-ternary`/`S-nested` + as ops；host 回编同构 |
| M300 planted batch3 | 完成 | +9 叶（40 总）；`memset`/`memcmp`/`strcmp`/`strncmp`/`strcpy`/`stpcpy`/`strcat`/`strchrnul`/`atoi`；musl 别名 `__strchrnul`/`__stpcpy`；`planted: 13` / `inputs_s: 44`；contract `planted≤22` |
| M301 Win CC_CONF exec | 完成 | WSL x86_64 → `mode=run`（或 qemu-user→`qemu`）；`--require-exec` 禁止 CI `compile-elf` |
| M302 docs/VERSION | 完成 | 本文件；dialect / a2-mini-cc / README / cadence；**v2.92.0** |

## 冻结面（未削弱）

- `CXC_OS_SELF_TEST_PASS`、`peer_ok=1`、M13 Echo  
- `spawn_ld: 1`；`spawn_as == inputs_s`（双窗仍兼容 legacy `4`）  
- CCDRV：`ONBOX_CCDRV_PASS` + `exit: 42` + `WMSG:CD`；`planted ≤ 22`（经验值 13）  
- host `CC_CONF: pass=59 bug=0` + `--elf-check`；CI `--require-exec`

## 复现

```sh
python3 tools/cc_conformance.py --strict --elf-check --require-exec
python3 tools/run_cc_host_test.py
python3 tools/run_as_plant_check.py
cmake --build build/uefi --target qemu-test
# serial: planted: 13 inputs_s: 44 spawn_as: 44 ONBOX_CCDRV_PASS
```

## 下一窗建议

1. 继续 planted 退坡：深叶 `hstb` / `stol` / `iscn` / `mmov`（`__memcpy_fwd`）等  
2. asm_mode 覆盖更多 `cc_emit_*`（聚合 / 调用约定边角）  
3. Windows CI 保持 `mode=qemu`；勿回退到 `compile-elf` 作绿条  
4. 仍 **未宣称 A2**
