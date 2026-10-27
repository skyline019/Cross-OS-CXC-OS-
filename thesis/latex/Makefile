# 本科毕设 PDF：WSL 下 xelatex + bibtex
MAIN = main
LATEX = xelatex -interaction=nonstopmode -halt-on-error
BIBTEX = bibtex

.PHONY: all clean view

all: $(MAIN).pdf

$(MAIN).pdf: $(MAIN).tex refs.bib chapters/*.tex
	$(LATEX) $(MAIN)
	$(BIBTEX) $(MAIN)
	$(LATEX) $(MAIN)
	$(LATEX) $(MAIN)

clean:
	rm -f *.aux *.bbl *.blg *.log *.out *.toc *.lof *.lot *.fls *.fdb_latexmk *.synctex.gz
	rm -f chapters/*.aux

view: $(MAIN).pdf
	xdg-open $(MAIN).pdf 2>/dev/null || true
	@ls -la $(MAIN).pdf
