DEPSEC = \
	resmgmt.tex \
	framework.tex \
	runtime.tex \
	monitor.tex \
	project.tex \
	reqs.tex

DEPFIG = \
	comms.eps \
	commstk.eps \
	cmb.eps \
	zmqtbon.eps

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

%.eps: %.dia
	dia --nosplash --export=$@ --filter=eps-builtin $<
