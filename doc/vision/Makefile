SECTIONS = resmgmt.tex framework.tex runtime.tex monitor.tex project.tex \
	reqs.tex

all: report.pdf

view: all
	evince report.pdf

report.pdf: report.tex

clean:
	rm -f *.aux *.log *.dvi *.pdf *.blg *.bbl

report.dvi: report.tex $(SECTIONS) ../bib/project.bib
	rm -f *.log *.aux *.blg *.bbl
	latex report
	bibtex report
	latex report
	latex report

%.ps: %.dvi
	dvips -P pdf -G0 -o $@ $<
%.pdf: %.ps
	ps2pdf $<

#%.eps: %.dia
#	dia --nosplash --export=$@ --filter=eps-builtin $<
