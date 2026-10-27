# 本科毕业论文 LaTeX（PMFS-KV）

## 产出

- PDF：`main.pdf`（A4，约 21 页，xelatex）
- 精修图：`figures/plots.tex`（pgfplots 嵌入第 5 章）

## WSL 编译

```bash
cd /mnt/e/OS/workspace-study/xv6-pmfs/thesis/latex
make          # xelatex → bibtex → xelatex ×2
```

依赖：`texlive`（`ctex`、`pgfplots`、`biblatex`/`bibtex`）、字体 **Noto Serif/Sans CJK SC**、TeX Gyre。

## 封面信息

编辑 `main.tex` 中宏：`\ThesisAuthor`、`\ThesisAdvisor`、`\ThesisSchool` 等，或替换为学校正式封面模板。

## 数据来源

实验表与图数据来自仓库 `docs/BENCH.md`（`make bench-sweep`）。
