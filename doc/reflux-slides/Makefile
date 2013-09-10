DEP_KEEP=\
	../fig/zmq-broker-hkvs.eps \
	../fig/reflux_rpc.eps \
	../fig/reflux_rev_rpc.eps \
	../fig/reflux_event.eps \
	../fig/reflux_arch_on_node.eps \
	../fig/reflux_arch_off_node.eps \
	../fig/reflux_kvs_merkel.eps \
	../fig/reflux_generic.eps \
	../fig/reflux_dealer_router.eps

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

#%.pdf: %.tex
#	pdflatex $<
#	pdflatex $<

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
