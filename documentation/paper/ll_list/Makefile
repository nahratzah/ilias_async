LATEX ?= latex
PDFLATEX ?= pdflatex
DVIPS ?= dvips
RM ?= rm


all: ll_list.ps ll_list.pdf

ll_list.dvi: ll_list.tex
	${LATEX} ll_list.tex
	# Once more, to fix up the references.
	${LATEX} ll_list.tex

ll_list.ps: ll_list.dvi
	${DVIPS} ll_list.dvi
ll_list.pdf: ll_list.tex
	${PDFLATEX} ll_list.tex

clean:
	${RM} -f ll_list.log ll_list.aux ll_list.dvi ll_list.ps ll_list.pdf
