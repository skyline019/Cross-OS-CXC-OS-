# 第二十五次中场评估（约 M245–M254）

**版本**：v2.50.0  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_HARD_PASS`（`exit: 42` + `edge: 1` + `DMSG`/`DHINT`）。QEMU 底线 **128 MiB**。

## 本窗交付

1. **M245–M247**：第三 RO `PT_LOAD`（`PF_R`）+ 真 `.bss` NOBITS；host 断言 `e_phnum==3` / `filesz<memsz`。
2. **M248–M249**：紧凑 ABI — `foffs` 由 `fsizes` 累加自然对齐；`sizeof(Pair)==2`；访问与 sizeof 同布局。
3. **M250–M252**：深 musl 闭包（strcspn/strspn/strpbrk/strrchr/memmove/strdup/strtol + intscan/shgetc）；`hard_stubs`（`__stack_chk_fail`/`___errno_location`/`__uflow`/malloc）；入口 `arch_prctl(ARCH_SET_FS)` 配 canary；`CC_LINK_MAX=96`、`max_files=128`。
4. **M253**：as 按实种加厚（`rep movsb`/`cld`/`%fs:0x28`/`movslq`/`%r8` xor）+ 非法操作数边缘。
5. **M254**：整链 → `ONBOX_HARD_PASS` + `WMSG:HD` + `hard: 1`；chaos `0xC1F4`。机上收口补丁：导出 STT_NOTYPE 文本别名（`__memcpy_fwd`）、rela/节容量、`and rsp,-16` SysV 对齐（musl SSE）、virtio-blk 20 MiB、`strpbrk` 探针 haystack。

| 里程碑 | 摘要 | 证据 |
|---|---|---|
| M245–M247 | RO PT_LOAD + NOBITS | host m245-ro / m245-bss |
| M248–M249 | 紧凑 ABI | host m248-compact-pair |
| M250–M252 | 深 musl + stubs | on-box 真调 + rsp 闭包 |
| M253 | as 加厚 | host m253-as + 3 edges |
| **M254** | 整链 HARD | `ONBOX_HARD_PASS` + `WMSG:HD` + `hard: 1` + `edge: 1` |

## 债表清零（本窗子集）

| 债 | 状态 |
|---|---|
| `.rodata` 同 RW / 无第三 RO PT_LOAD | **已清** |
| 字段 pad-to-8；sizeof/访问不一致 | **已清** |
| `.bss` 名接受但非真 NOBITS | **已清** |
| strtol/strrchr/memmove/strcspn/strpbrk 深闭包 | **已清** |
| as 相对 musl `-S` 过窄 | **已清**（按实种面） |

## 仍未还（明确不在本窗 / 不挡 HARD）

- 真 gcc/g++/binutils 移植与自举落盘
- 完整 DWARF/`eh_frame` 消费与调试器

**未宣称 A2**。

## CI / 合同同步

1. `contract_test.py` / CI grep → `ONBOX_HARD_PASS` + `WMSG:HD` + `hard: 1`
2. chaos 种子 `0xC1F4`
3. VERSION **2.50.0**；重建 `user/prebuilt/cxc_cc.elf`
