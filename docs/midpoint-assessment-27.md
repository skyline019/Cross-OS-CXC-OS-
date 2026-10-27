# 第二十七次中场评估（M265–M274 / TDRV）

**版本**：v2.70.0  
本地实证：默认 `qemu-test` → `CXC_OS_SELF_TEST_PASS` + `peer_ok=1`；机上合同至 `ONBOX_TDRV_PASS`（`exit: 42` + `WMSG:TD` + `tdrv: 1` + `spawn_as: 4` + `spawn_ld: 1` + `edge: 1`）。  
`spawn_as`/`spawn_ld` 为 HARD 会话内内核 `execve("disk/as"|"disk/ld")` 审计计数；`tcap`/`tdrv` 由产物证据推导，禁止 probe 写死 / volatile BSS 辅证。

## 本窗交付

1. **M265**：mini_libc `fork`/`waitpid`/`run_tool`（fork+execve+wait4）。
2. **M266–M267**：`as.c`/`ld.c` CLI 化种植为 `disk/as`/`disk/ld`；`cxc-cc` 成功路径 **零** 进程内 `as_compile`/`ld_link`。
3. **M268**：子进程非 0 → `DMSG` + exit 4；无 in-process fallback（cxc-cc 不再链接 as.c/ld.c）。
4. **M269–M270**：`t.s`/`tcap_br.o`/`x1.s`/`x2.s` 入 `link.rsp`；≥3 次外部 as + 1 次 ld。
5. **M271–M272**：无新增 ENOSYS 阻塞（沿用既有 fork/exec/wait/open/read/write）；as+ld+cc+fat 同盘 soak。
6. **M273–M274**：host `expect_fail` ≥3；chaos `0xC2B6`；`ONBOX_TDRV_PASS`。

| 里程碑 | 要点 | 合同 |
|---|---|---|
| **M274** | 外部 as→ld 真驱动 | `ONBOX_TDRV_PASS` + `WMSG:TD` + `tdrv: 1` |

## 纪律

- **未宣称 A2**。TDRV 是进程边界合同，不是 gcc 自举。
- 禁止用宿主 `.o` 种植冒充机上 as/ld；bridge 仅辅证。
- musl-static mini-as 保留为 `disk/mini_as` 容量回归，不覆盖驱动 `disk/as`。

## 验收清单

1. CI / `contract_test.py` → `ONBOX_TDRV_PASS` + `WMSG:TD` + `spawn_as: 4` + `spawn_ld: 1` + `tdrv: 1`
2. chaos 种子 `0xC2B6`
3. VERSION **2.70.0**；`user/prebuilt/{as_cli,ld_cli,cxc_cc}.elf` 与种植头同步
