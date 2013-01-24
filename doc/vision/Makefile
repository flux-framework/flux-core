DEPSEC = \
	project.tex \
	framework.tex \
	resmgmt.tex \
	monitor.tex \
	io.tex \
	runtime.tex \
	reqs.tex \
	process.tex

DEPFIG = \
	../fig/comms.eps \
	../fig/commstk.eps \
	../fig/cmb.eps \
	../fig/zmqtbon.eps \
	../fig/comms_ex1a.eps \
	../fig/comms_ex1b.eps \
	../fig/comms_ex1c.eps \
	../fig/comms_ex1d.eps \
	../fig/mon_ex1a.eps \
	../fig/mon_ex1b.eps \
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
