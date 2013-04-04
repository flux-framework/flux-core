DEPSEC = \
	redis-pmi.tex \
	zmq-broker.tex \

DEPFIG = \
	../fig/redis-pmi.eps \
	../fig/zmq-broker.eps \

DEPBIB = 

all: report.pdf

view: all
	evince report.pdf

report.pdf: report.tex

clean:
	rm -f *.aux *.log *.dvi *.pdf *.blg *.bbl

report.pdf: report.tex $(DEPSEC) $(DEPFIG) $(DEPBIB)
	rm -f *.log *.aux *.blg *.bbl
	pdflatex -shell-escape report
	#bibtex report
	#pdflatex -shell-escape report
	pdflatex -shell-escape report
