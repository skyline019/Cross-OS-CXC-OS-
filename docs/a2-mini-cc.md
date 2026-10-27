# A2 机上迷你编译器（cxc-cc）路线

**目标**：不依赖宿主机，在 CXC/OS 上用**完整内部编译器**编译并运行合理范围内的程序（通向 g++/gcc）。  
**当前基线**：M284 — **CCDRV**：`disk/cc` argv CLI（`-c`/`-o`/`@job.rsp`）；嵌套 `cc -c` + 外部 `disk/as`→`disk/ld`；`ONBOX_CCDRV_PASS` + `WMSG:CD`；第二十八次中场。

## 1. 语言分期（合同只增不减）

| 阶段 | 语法面 | 证据口径（示例） |
|---|---|---|
| M154 | 整链加深合流 | 曾用 `ONBOX_DEEP_PASS` + `WMSG:DP` |
| M164 | musl 闭包 + as/前端 | 曾用 `ONBOX_MCLS_PASS` + `WMSG:MC` |
| M174 | 已种未调闭合 + 浅 ctype | 曾用 `ONBOX_CLSR_PASS` + `WMSG:CL` |
| M184 | 浅 ctype/mem + 无 io memcpy | 曾用 `ONBOX_SURF_PASS` + `WMSG:SF` |
| M194 | ctype 加宽 + 布局 sizeof | 曾用 `ONBOX_WIDE_PASS` + `WMSG:WD` |
| M204 | ctype 叶子 + as 加厚 | 曾用 `ONBOX_CTYP_PASS` + `WMSG:CT` |
| M214 | 剩余浅叶子 + as | 曾用 `ONBOX_LEAF_PASS` + `WMSG:LF` |
| M224 | 浅 walk + mem-walk as | 曾用 `ONBOX_WALK_PASS` + `WMSG:WK` |
| M234 | 浅 wrap + as | 曾用 `ONBOX_WRAP_PASS` + `WMSG:WR` |
| M244 | 浅 netw + as | 曾用 `ONBOX_NETW_PASS` + `WMSG:NW` |
| M254 | HARD 结构/深闭包还债 | 曾用 `ONBOX_HARD_PASS` + `WMSG:HD` |
| M264 | OS 容量还债 + 胖 ELF + as 首跨 | 曾用 `ONBOX_TCAP_PASS` + `WMSG:TC` |
| M274 | cxc-cc 外部 as→ld 真驱动 | 曾用 `ONBOX_TDRV_PASS` + `WMSG:TD` |
| **M284（本窗）** | disk/cc argv 真驱动 | `NMSG:42` + `WMSG:CD` + `ONBOX_CCDRV_PASS` + `ccdrv: 1` + `spawn_cc: 3` + `spawn_as: 4` + `spawn_ld: 1` |

**未宣称 A2**。`ONBOX_CCDRV_PASS` 是合同加深标签，不是 A2。

## 2. 实现形态

- ELF：ET_EXEC 有 rodata 时 `phnum=3`（RX + **R** rodata + RW data/bss）；`.bss` 真 `SHT_NOBITS`（`p_filesz < p_memsz`）
- 前端：紧凑 `foffs`；`sizeof` 与访问共用布局；`char**`/`void*`/`->` 不回退
- musl：真调 str* 闭包；`hard_stubs.o` 提供 ssp/errno/uflow/malloc
- **disk/cc**：argv `-c`/`-o`/`@rsp`；`.c` 嵌套 `disk/cc -c`；`.s`→`disk/as`；ET_EXEC 仅 `disk/ld`；spawn 由内核 `execve` 审计；混沌种子 `0xC2C6`；cxcfs `max_files=256` / `max_file_bytes=4MiB`；virtio 128 MiB

## 3. 纪律

大跨步；边缘与混沌同权；禁止宣称 A2；禁止用浅种植/注释 RO/pad-to-8 残留冒充本窗还债。
