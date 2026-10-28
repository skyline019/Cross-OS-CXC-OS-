# 本科毕业论文 LaTeX（PMFS v3）

> **正文已按 v3 证据重写**（chap01–06、摘要、附录、图数据）。提交前仍须：换学校封面模板、填姓名、导师最终审阅措辞。  
> 写作依据与文档索引见 [docs/README.md](../../docs/README.md)。

## 章节与证据

| 章 | 内容 | 主要证据 |
|----|------|----------|
| 1 绪论 | 题面四要件、贡献、非目标 | THESIS / CONTRIBUTIONS |
| 2 相关工作 | PMFS/NOVA/崩溃测试/前序 | RELATED-WORK |
| 3 设计 | `/pm`、后端、可见点、checkpoint | ARCHITECTURE / SEMANTICS |
| 4 实现 | VFS、COW、刷写、oracle | CORE-WALKTHROUGH |
| 5 实验 | recovery + matched perf + 消融 | results/2026-10-28-* |
| 6 结论 | 合题口径与展望 | DEFENSE |
| 附录 | 复现命令与设备边界 | CONTRACT / QEMU-NVDIMM |

`chapters/current-evidence.tex` 为早期可移植草稿，**默认未 `\include`**；内容已吸收进第 5 章。

## 编译

```bash
cd thesis/latex
make          # xelatex → bibtex → xelatex ×2
```

依赖：TeX Live（`ctex`、`pgfplots`）、Noto CJK、TeX Gyre。

## 图

`figures/plots.tex` 使用 `docs/results/2026-10-28-perf` 与 efficiency 摘录数字。图注已写明契约不等价；勿删限制句。

## 定稿检查清单

- [ ] 封面宏 / 学校模板  
- [ ] 摘要与第 1/5/6 章无「无条件掉电」「真机加速」完成式  
- [ ] 表内数字与 results README 一致  
- [ ] 参考文献 [BIB-AUDIT.md](BIB-AUDIT.md)  
- [ ] 附录复现命令在 disposable checkout 抽测  
