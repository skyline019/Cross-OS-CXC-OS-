# Midpoint assessment 31（A2-SEM / M287–M298）

**日期**：会话落地窗（相对基线 M286 / v2.82.0 → **v2.91.0**）  
**结论**：CXC_A2 语义网 **59/59 PASS**；**M295–M298** 已落地（planted 二批 + asm_mode 加深）。机上 CCDRV 目标：`spawn_as`/`spawn_cc` 双窗；`planted: 22`、`inputs_s: 35`。

## 本窗交付对照

| 里程碑 | 状态 | 证据 |
|---|---|---|
| M287 仪表 + 词法硬失败 | 完成 | `cc_conformance.py`、`TOK_UNKNOWN`、`cxc-a2-dialect.md` |
| M288 统一寻址 | 完成 | `ptr_scaling` / `ptr_param_index` / `prog_sort_call` 转 PASS |
| M289 整数语义 | 完成 | `op_*` / `return_void` / 64-bit hex |
| M290 聚合 | 完成 | `char_array` / `struct_*` / `typed_ptr_param` / `multi_dim` |
| M291 预处理 | 完成 | `//` `/* */` `#define`；`CC_LIT_MAX=32` 无静默截断 |
| M292 容量墙 | 完成 | ident64 / locals128 / globals64 / …；原 `cap_*` 全绿 |
| M293 种植/驱动 | 完成 | 凡 `.c` 均 `spawn_cc_c`；`spawn_cc` 双窗 |
| M294 诊断/ELF/中场 | 完成 | ET_EXEC 同余；`--elf-check` |
| M295 planted→as | 完成 | 23 叶 `user/plant_src/*.s`；当时 `planted: 30` / `inputs_s: 27` |
| M296 DLOC / -E / -S | 完成 | `DLOC:L:C`；`cc -E`→`cc_preprocess`；`cc -S`→as 回编 |
| M297 asm_mode CF | 完成 | S-local/if/while/call GAS + as opcode；host 回编同构 |
| M298 planted batch2 | 完成 | +8 叶（31 总）；`planted: 22`；`inputs_s: 35`；VERSION 2.91.0 |

## 冻结面（未削弱）

- `CXC_OS_SELF_TEST_PASS`、`peer_ok=1`、M13 Echo  
- `spawn_ld: 1`；`spawn_as` 双窗（`4` 或 `inputs_s`）  
- CCDRV：`ONBOX_CCDRV_PASS` + `exit: 42` + `WMSG:CD`；`planted ≤ 33`

## 复现

```sh
python3 tools/cc_conformance.py --strict --elf-check
python3 tools/run_cc_host_test.py
python3 tools/run_as_plant_check.py
cmake --build build/uefi --target qemu-test
```

## 下一窗建议

1. asm_mode 扩到更多 `cc_emit_*`（深控制流 / 调用）  
2. 继续 planted 退坡（深叶 / `hstb` 等）  
3. 版本标签建议：**v2.91.0**（planted≤33 + DLOC/-E/-S）
