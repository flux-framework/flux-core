DEP_KEEP=
DEP=	

CLEANFILES=$(DEP)


all: slides.pdf

view: slides.pdf
	evince $<

html: print.tex
	latex2html slides

clean:
	rm -f *.out *.dvi *.aux *.log 
	rm -f slides.ps slides.pdf  
	rm -f print.ps print.pdf  
	rm -f *~ *.autosave
	rm -rf slides
	rm -f $(CLEANFILES)
	

veryclean: clean
	rm -f $(DEP)

slides.dvi: slides.tex $(DEP) $(DEP_KEEP)

%.dvi: %.tex
	latex $<
	latex $<

%.eps: %.dia
	dia --nosplash --export=$@ --filter=eps-builtin $<

%.png: %.dia
	dia --nosplash --export=$@ $<

%.pdf: %.ps
	ps2pdf -dPDFSETTINGS=/prepress $<

%.eps : %.gp
	gnuplot $<

%.ps: %.dvi
	dvips -t a4 $< -o $@

%.eps: %.dot
	dot -Tps $< -o $@

%.png: %.dot
	dot -Tpng $< -o $@
