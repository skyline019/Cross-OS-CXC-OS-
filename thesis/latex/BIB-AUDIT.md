# 参考文献严格校验记录（2026-10-28）

对照 ACM DL / USENIX / JCST / DBLP。

## 已修正的硬伤

| 原条目 | 问题 | 处理 |
|--------|------|------|
| `pmfs2013` Xu & Swanson, FAST 2014 | **张冠李戴**：PMFS 实为 Dulloor et al., EuroSys 2014《System Software for Persistent Memory》；Xu & Swanson 的相关名作为 NOVA (FAST'16) | 改为 `pmfs2014` |
| `bpfs2009` 题名 “BPFS: Better Persistence…” | **题名虚构**；正题为 *Better I/O Through Byte-Addressable, Persistent Memory*，SOSP'09:133–146 | 改正题名与全作者 |
| `chen2016nvm` Shimin Chen, JCST 2016 | **查无此文**；NVMM 综述正本为 Liu et al., JCST 36(1):4–32, 2021 | 改为 `nvmm2021` |
| `mnemosyne2011` Andres **Jair** Tack | 第二作者为 Andres **Jaan** Tack；页码 91–104 | 已改 |
| `lsm2011` | 键名与年份不符；SIGMOD **2012**:217–228 | 改为 `blsm2012` |

## 补全与增补

- NOVA / Strata / Bloom / bLSM：补 pages、publisher、doi
- xv6：作者写全为 M. Frans Kaashoek
- 正文增引 `qemu`、`riscvpriv`；删除未引用的 `gbt7714`

## 作者前序条目（`fcsown` / `auroraown` / `cxcos`）

标明「未公开发表 / 工程实践」，避免伪造成正式出版物。封面定稿后可将「作者」换成真实姓名。
