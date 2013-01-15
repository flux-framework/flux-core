DEPSEC = \
	framework.tex \
	monitor.tex \
	project.tex \
	reqs.tex \
	resmgmt.tex \
	runtime.tex

DEPFIG = \
	../fig/comms.eps \
	../fig/commstk.eps \
	../fig/cmb.eps \
	../fig/zmqtbon.eps \
	../fig/pathwalk.eps \
	../fig/dnbd.eps \
	../fig/wrap_graph.ps \
	../fig/WRAP_Architecture.ps \
	../fig/WRAP_Base.ps \
	../fig/WRAP_grow.ps \
	../fig/WRAP_newJobFunction.ps

DEPBIB = \
	../bib/project.bib \
	../bib/rfc.bib

all: report.pdf

view: all
	evince report.pdf

report.pdf: report.tex

clean:
	rm -f *.aux *.log *.dvi *.pdf *.blg *.bbl

report.dvi: report.tex $(DEPSEC) $(DEPFIG) $(DEPBIB)
	rm -f *.log *.aux *.blg *.bbl
	latex report
	bibtex report
	latex report
	latex report

%.ps: %.dvi
	dvips -P pdf -G0 -o $@ $<
%.pdf: %.ps
	ps2pdf $<
