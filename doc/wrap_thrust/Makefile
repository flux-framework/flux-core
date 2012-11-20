all: wrap.pdf

fig/wrap_graph.pdf: wrap.dot 
	dot -Tpdf $^ -o wrap_dot.pdf
	pdfcrop wrap_dot.pdf 
	mv wrap_dot-crop.pdf $@

wrap.pdf: wrap.tex wrap.bib fig/wrap_graph.pdf fig/WRAP_Architecture.pdf fig/WRAP_Base.pdf fig/WRAP_grow.pdf fig/WRAP_newJobFunction.pdf
	pdflatex wrap
	bibtex wrap
	pdflatex wrap
	pdflatex wrap

clean:
	rm -f *.pdf fig/wrap.pdf *~ *.log *.aux *.bbl *.blg
