presentation.pdf : presentation.tex Makefile
	latexmk -pdf presentation.tex -halt-on-error
	make clean
presentation2.pdf : presentation2.tex Makefile
	latexmk -pdf presentation2.tex -halt-on-error
	make clean
1sWRN.pdf : 1sWRN.tex Makefile
	latexmk -pdf 1sWRN.tex -halt-on-error
	make clean
SRN.pdf : SRN.tex Makefile
	latexmk -pdf SRN.tex -halt-on-error
	make clean
clean: 
	latexmk -c
