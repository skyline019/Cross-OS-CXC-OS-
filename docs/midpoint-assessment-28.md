# 第二十八次中场评估（M275–M284 / CCDRV）

**版本**：v2.80.0  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_CCDRV_PASS`（`exit: 42` + `WMSG:CD` + `ccdrv: 1` + `spawn_cc: 3` + `spawn_as: 4` + `spawn_ld: 1` + `edge: 1`）。  
`spawn_*` 为 HARD 会话内核 `execve` 审计；`ccdrv`/`tcap` 由产物证据推导。大 `lib.c` 同进程编译；`a.c`/`b.c` 嵌套 `disk/cc -c` 证明进程边界。

## 本窗交付

1. **M275–M276**：`cc_main.c` → `disk/cc`/`disk/cxc-cc`；argv `-c`/`-o`/`@job.rsp`。
2. **M277**：ET_EXEC 仅 spawn `disk/ld`；`.s` 仅 spawn `disk/as`。
3. **M278**：多 `.c`；`a.c`/`b.c` 嵌套 `disk/cc -c`；`exec_disk_cc` 审计（`spawn_cc: 3`）。
4. **M279**：`for`/`while`/数组/`#include "hdr.h"` 入 HARD 链。
5. **M280–M282**：同盘 soak；默认 bare exec → `@disk/job.rsp`；边缘改写 job→bad.c。
6. **M283–M284**：chaos `0xC2C6`；`ONBOX_CCDRV_PASS`；CI/docs。

| 里程碑 | 要点 | 合同 |
|---|---|---|
| **M284** | disk/cc argv 真驱动 | `ONBOX_CCDRV_PASS` + `WMSG:CD` + `ccdrv: 1` |

## 纪律

- **未宣称 A2**。CCDRV 是 gcc 形驱动面，不是自举。
- 禁止成功路径 in-process as/ld；禁止 probe 写死 spawn 计数。

## 验收清单

1. CI / `contract_test.py` → `ONBOX_CCDRV_PASS` + `WMSG:CD` + `spawn_cc: 3` + `spawn_as: 4` + `spawn_ld: 1` + `ccdrv: 1`
2. chaos 种子 `0xC2C6`
3. VERSION **2.80.0**；`user/prebuilt/{cc_cli,as_cli,ld_cli,cxc_cc}.elf` 同步
