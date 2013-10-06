all: ll_list.ps ll_list.pdf

ll_list.dvi: ll_list.tex
	latex ll_list.tex
	# Once more, to fix up the references.
	latex ll_list.tex

ll_list.ps: ll_list.dvi
	dvips ll_list.dvi
ll_list.pdf: ll_list.tex
	pdflatex ll_list.tex

clean:
	rm -f ll_list.log ll_list.aux ll_list.dvi ll_list.ps ll_list.pdf
