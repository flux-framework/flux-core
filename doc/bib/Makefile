DEPBIB = project.bib rfc.bib

all: report.pdf

view: all
	evince report.pdf

report.pdf: report.tex

clean:
	rm -f *.aux *.log *.dvi *.pdf *.blg *.bbl

report.dvi: report.tex $(DEPBIB)
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
