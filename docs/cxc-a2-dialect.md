# CXC_A2 方言清单（用例名为规范引用）

**基线**：A2-SEM / M287+。每条「支持」必须对应 [`tools/cc_conformance.py`](../tools/cc_conformance.py) 用例名；CI `--strict` 防止 xfail 腐烂。

**未宣称 A2。** 本表描述教学子集，不是 C89/C99。

## 1. 支持（conformance `ok`）

| 面 | 用例名 |
|---|---|
| 控制流 | `while` `do_while` `for` `else_if` `switch` `continue` `break` `ternary` |
| 运算 | `add_sub_mul` `logic` `inc_dec` `plus_eq` `minus_eq` `signed_div` `signed_mod` `op_shift_arith` `op_mod_eq` `op_xor` `op_not_bit` `op_cast` `op_unsigned_div` |
| 数据 | `array_sum` `struct_field` `struct_ptr_local` `global` `const_global` `char_scalar` `var_index` `char_array` `struct_array` `struct_assign` `struct_return` `multi_dim` |
| 指针 | `ptr_deref` `ptr_double` `ptr_char_index` `ptr_scaling` `ptr_param_index` `typed_ptr_param` |
| 函数 | `nested_call` `fib` `recursion` `return_void` |
| 文本 | `write_tag` `write_esc` `io_write_str_long` |
| 预处理 | `pp_define` `pp_line_comment` `pp_block_comment` |
| 容量 | `cap_globals_16` `cap_structs_6` `cap_struct_fields_6` `cap_loop_depth_4` `cap_locals_56` `cap_ident_20` `fn_five_args` |
| 程序 | `prog_bubble` `prog_stack` `prog_sort_call` |

## 2. 已知缺口（`xfail_*`，修复后须改 kind 并 `--strict` 转绿）

| 债 | 用例 | 里程碑 |
|---|---|---|
| D1 词法 | 未知字符/`#` 裸用 → 编译失败（不再静默 EOF） | M287 已硬失败 |
| D2–D8 / D3 / D6 / D7 | M288–M292 语义网已全绿（59=`pass`） | 完成 |
| D11 种植 | spawn 双窗；planted 22≤33（31 叶机上 as） | M293/M295/M298 |
| D12 诊断 | 失败追加 `DLOC:line:col`；`cc -E`/`-S`；S-* 回编 | M296/M297 |
| D9/D10 | ELF 同余 + `--elf-check` + [中场 31](midpoint-assessment-31.md) | M294 完成 |

## 3. 「能独立跑复杂方言」四条（目标，非现状）

1. **语义**：59 例 `bug=0 xpass=0`，机上子集全绿  
2. **规模**：单文件 ≥800 行级可编可跑  
3. **真实性**：宿主种植 `.o` ≤ 输入 50% 且单调下降  
4. **可移植**：ET_EXEC `p_offset ≡ p_vaddr (mod align)`，宿主可复算  

## 4. 复现

```sh
python3 tools/cc_conformance.py --strict
```
