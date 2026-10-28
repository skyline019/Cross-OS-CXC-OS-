# 本科毕业论文 LaTeX（PMFS-KV）

> **旧草稿，不能直接提交。** 现有正文源码将普通 RAM/file-backed RAM 称为 NVDIMM，并对缓存刷写、掉电一致性及历史性能结果作了未经充分验证的完成式表述。`main.pdf` 为旧制品，本次未重编或验证。请根据[当前语义](../../docs/SEMANTICS.md)和[验证记录](../../docs/results/2026-10-28-efficiency/README.md)整体重写论文和重生成 PDF。
>
> 新增 [`chapters/current-evidence.tex`](chapters/current-evidence.tex) 作为可供重写时使用的当前证据章节，包含明确限制及消融数据；**尚未接入旧 `main.tex`，也尚未编译校验**。不可只把该章拼进旧正文就当作完整论文。

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
