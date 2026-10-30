# 踩坑与失败叙述（顶批加分：可复核）

> 目的：证明系统是「调出来的」，不是一次性写成的。论文「实现难点 / 实验威胁」可直接引用本节。  
> v3 修复前后对照另见 [LOCAL-SANDBOX-TESTS.md](LOCAL-SANDBOX-TESTS.md)、[PMFS-REPAIR-REPORT.md](PMFS-REPAIR-REPORT.md)；总索引 [README.md](README.md)。

## P0. 启动栈溢出（stack0 = 4KiB）

| 项 | 内容 |
|----|------|
| **现象** | `MOUNT_PASS` 后挂死；或稍后 `usertrap sepc=0x50505050…` / `init exiting` |
| **根因** | `main()` 跑在 `stack0`（原 4096B/hart）；`pmfs_storage_selftest` 等函数栈上 `uint8 page[4096]`×2 → 溢出，污染邻近内存（`0x50='P'` 来自测试图案） |
| **修复** | 页缓冲改 `static`；`stack0`/`entry.S` 扩至 **16KiB/hart** |
| **教训** | 教学内核启动栈极薄；自测勿在 boot 路径上放大数组 |
| **证据位置** | `kernel/start.c`、`kernel/entry.S`、`kernel/pmfs.c` 注释 |

## P1. `rdcycle` 非法指令（scause=0x2）

| 项 | 内容 |
|----|------|
| **现象** | `pmfs_bench` 一调用 `cycles()` 即 `panic: kerneltrap`，`stval≈0xc0002573` |
| **根因** | `timerinit` 只置位 `mcounteren.TM`（time），未开 `CY`；S 态执行 `rdcycle` 非法 |
| **修复** | `sys_cycles` 改为已允许的 `r_time()`（语义为相对时间单位，文档写明） |
| **教训** | RISC-V 计数器权限与 CSR 选择必须对齐；论文勿把 `r_time` 写成墙钟 µs |
| **证据位置** | `kernel/sysproc.c`、`kernel/start.c` |

## P2. QEMU NUMA / RAM size mismatch

| 项 | 内容 |
|----|------|
| **现象** | `contract-pmem2` boot1 无任何 `mode=MAP`；QEMU 瞬间退出 |
| **根因** | 仅 `-object memory-backend-file,size=160M -numa node,memdev=mem`，默认 `-m` 仍为 128M → `total memory for NUMA nodes (0xa000000) should equal RAM size (0x8000000)` |
| **修复** | 文件后端启动同步加 `-m 160M` |
| **教训** | 掉电实验脚本必须先手工跑通 QEMU 报错；契约超时≠内核逻辑错 |
| **证据位置** | `Makefile` `QEMUOPTS_PMEM_FILE`、`docs/QEMU-NVDIMM.md` |

## P3. MAP 探测在 `-m 128M` 下写 PMEMBASE 必崩

| 项 | 内容 |
|----|------|
| **现象** | 默认 `make qemu` 启动即 store fault，`stval=0x88000000` |
| **根因** | 映射了无物理后备的 `[PMEMBASE,PMEMEND)` 并做写探测 |
| **修复** | 仅 `-DPMEM_MAP` 时 `kvmmap`+探测；默认 EMUL 不触碰该窗 |
| **证据位置** | `kernel/vm.c`、`kernel/pmem.c` |

## P4. Bench 过早 kill 截断契约行

| 项 | 内容 |
|----|------|
| **现象** | 日志出现 `median_pmfs=241contract: OK`，脚本 parse 失败 |
| **根因** | 匹配到 `BENCH_PASS` 子串即杀 QEMU，行未打完 |
| **修复** | 等待 `median_xv6=` / `phys=` / `wa_x100=` 齐全再退出 |
| **证据位置** | `tools/contract_pmfs.py` |

## P5. 多 trial / 扫参撑爆 cell 段数

| 项 | 内容 |
|----|------|
| **现象** | trial0 成功，trial1 `BENCH_FAIL code=2/3`（put/seal） |
| **根因** | `PM_MAX_SEGS=8`，每次 seal attach 一段；自测+多 trial 超限 |
| **修复** | 提高 `PM_SMAX`/`PM_MAX_SEGS`、B3 budget；扫参采用**每 N 独立开机** |
| **证据位置** | `kernel/pmalgo.h`、`tools/run_bench_sweep.py` |

## P6. 题面「文件系统」vs 实现「KV」不一致（过程债→已合题）

| 项 | 内容 |
|----|------|
| **现象** | 早期文档写 inode/POSIX，实现一度偏并行 KV |
| **修复** | 升格 `/pm` 普通 fd + COW/KV；语义清单写死差集 |
| **教训** | 顶批最怕题实不符；见 `SEMANTICS.md` |

## P7. v3 修复前：跨进程文件读不回（基线债）

| 项 | 内容 |
|----|------|
| **现象** | 契约 PASS，但杀 QEMU 后已 sync 文件/KV 读失败 |
| **根因** | 破坏性 boot 自测、RAM-only run 描述、元数据原地覆写等（见路线图 P0） |
| **修复** | COW 根、安全挂载、显式 format、recovery oracle |
| **证据** | `LOCAL-SANDBOX-TESTS.md`、`PMFS-REPAIR-REPORT.md` |

## P8. 把 MAP/共享 RAM 写成 NVDIMM（文档债）

| 项 | 内容 |
|----|------|
| **现象** | 旧论文/摘要完成式表述 |
| **修复** | 分后端表；补丁 nvdimm 单列；THESIS/SEMANTICS/QEMU-NVDIMM 统一口径 |
| **教训** | 题面关键词必须对齐设备身份证据（QMP） |

---

## 论文「威胁与局限」可直接粘贴的四段

1. **测量：** guest `r_time`；matched bench 对齐操作但**持久契约不等价**；结论限于架构级软件路径。  
2. **设备：** 未打补丁 MAP≠NVDIMM；实验补丁非上游通用。  
3. **持久域：** 默认 touch+fence；可选 CBO 在 TCG 无真实写回；杀进程≠物理掉电。  
4. **工程史：** P0–P8 表明正确性与口径经过故障闭环，而非理想路径截图。
