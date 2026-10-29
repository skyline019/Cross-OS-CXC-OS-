# CXC_A2 方言清单（用例名为规范引用）

**基线**：M307 审计后（→ M308–M316 计划见 [`plan-m307-m316.md`](plan-m307-m316.md)）。
每条「支持」必须对应 [`tools/cc_conformance.py`](../tools/cc_conformance.py) 用例名；CI `--strict` 防止 xfail 腐烂。

**未宣称 A2。** 本表描述教学子集，不是 C89/C99。

**当前计数（M307）**：`total=94 pass=64 xfail=30 bug=0 xpass=0 reject=0`。

> **M307 更正**：`struct_array` / `struct_return` / `multi_dim` 三个用例此前只做**声明**就 `return 0`，
> 从未读写该特性却算 `pass` —— `pass=59` 里至少有 3 个是空的。现已改成真读写，并按现状转为 `xfail_compile`。

## 1. 支持（conformance `ok` / `must_reject`）

| 面 | 用例名 |
|---|---|
| 控制流 | `while` `do_while` `for` `else_if` `switch` `continue` `break` `ternary` |
| 运算 | `add_sub_mul` `logic` `inc_dec` `plus_eq` `minus_eq` `signed_div` `signed_mod` `op_shift_arith` `op_mod_eq` `op_xor` `op_not_bit` `op_cast` `op_unsigned_div` |
| 数据 | `array_sum` `struct_field` `struct_ptr_local` `global` `const_global` `char_scalar` `var_index` `char_array` `struct_assign` |
| 指针 | `ptr_deref` `ptr_double` `ptr_char_index` `ptr_scaling` `ptr_param_index` `typed_ptr_param` |
| 函数 | `nested_call` `fib` `recursion` `fn_five_args` `return_void` |
| 文本 | `write_tag` `write_esc` `io_write_str_long`（≤32 字节） |
| 预处理 | `pp_define` `pp_line_comment` `pp_block_comment`（对象宏/注释已支持；函数宏不支持） |
| 容量上界（成对） | 63 globals / 32 structs / 16 fields / 8 loop nesting / 128 locals / 63 fns / 64-char ident；`*_over` 为 `must_reject` |
| 程序 | `prog_bubble` `prog_stack` `prog_sort_call` |

## 2. 已知缺口（`xfail_*`，修复后须改 kind 并 `--strict` 转绿）

### 2.1 会**静默算错或崩溃**（P0，M308–M311）

| 用例 | 现象 | 里程碑 |
|---|---|---|
| `arr_one_elem` | `int a[1]` 被当标量：`a[0]` 取到值 42 再解引用 → SIGSEGV | M308 |
| `struct_byval_param` | 结构体按值传参只拷第一个字段（返回 40，期望 42） | M309 |
| `nested_struct_member` | `v.a.x` 不解析（嵌套结构体） | M309 |
| `untyped_param_index` | 无类型形参 `s` 的 `s[n]` 按 8 字节步长索引自己栈帧（`slen("hello")` 错值） | M310 |
| `switch_fallthrough` | `case` 体结束即跳走（C 是 fall-through），静默偏差 | M311 |

### 2.2 「真实 C 程序每天都在用」的语法（M312–M314）

| 用例 | 缺口 | 里程碑 |
|---|---|---|
| `op_bit_or` / `op_bit_and` | 二元 `\|` / `&` 缺失（只有 `\|\|`、一元 `&`） | M312 |
| `enum_named` / `enum_as_type` | 只支持匿名 `enum {…}`；`enum E{…}` 与其类型名不支持 | M312 |
| `multi_declarator` / `multi_declarator_init` | `int a, b;`、`int x=1, y=2;` 不解析 | M312 |
| `char_escape_nl` / `char_escape_hex` | 字符常量只认 `'A'`；`'\n'` `'\0'` `'\xNN'` 不支持 | M312 |
| `type_short` `type_long_long` `static_local` | `short` / `long long` / `static` 存储类不支持 | M313 |
| `typedef_anon_struct` | `typedef struct { … } P;` 不解析（具名 struct 可用） | M313 |
| `global_array` `global_struct` | 全局只能是标量；数组/结构体全局不支持 | M313 |
| `array_decay` | 调用点数组→指针退化不支持（须显式 `&v[0]`） | M313 |
| `for_decl_c99` | `for (int i = 0; …)` 不支持 | M313 |
| `main_argv` | `main` 必须零参数，无法拿 `argv` | M313 |
| `func_ptr` `type_union` `type_float` `goto_label` | 函数指针 / union / 浮点 / goto 不支持 | M314 |

### 2.3 体积与历史债（M315）

| 用例 | 缺口 |
|---|---|
| `write_33_bytes` | `CC_LIT_MAX=32` 截住 `write "…"`，需要字面量池 |
| `cap_globals_over` / `cap_fns_over` | 常量说 64 / 128，实测上界 63 / 63（差一 + 另一张 64 项表） |

### 2.4 已还清（保留追溯）

| 债 | 状态 |
|---|---|
| D1 词法未知字符 → 静默 EOF | M287 已硬失败 |
| D2–D8 语义网（指针/整数/聚合） | M288–M292 完成 |
| D9/D10 诊断 `DLOC` + ELF 同余 | M294–M297 完成 |
| D11 种植 `.o` | M295–M306，`planted == 0` |
| D12 `-E` / `-S` / S-* 回编 | M296–M299 完成 |
| D13 Windows `CC_CONF` 真跑 | M301 完成 |

## 3. 「能独立跑复杂方言」四条（目标，非现状）

1. **语义**：`bug=0 xpass=0`，且 `xfail` 按里程碑单调降为 0；机上子集全绿
2. **规模**：单文件 ≥800 行级可编可跑（含结构体表 + 字符串 + 位运算）
3. **真实性**：宿主种植 `.o` ≤ 输入 50% 且单调下降（当前 `planted: 0`）
4. **可移植**：ET_EXEC `p_offset ≡ p_vaddr (mod align)`，宿主可复算

## 4. 复现

```sh
python3 tools/cc_conformance.py --strict --elf-check --require-exec
python3 tools/cc_conformance.py --report        # 每例状态 + 缺口原因
python3 tools/cc_conformance.py --only enum_    # 单点迭代
```
