latex     = latex
pdflatex  = pdf$(latex)
dvips     = dvips -Pgeneric -o$@ $<
epstopdf  = epstopdf --outfile=$@ $<.inter
epsfix    = eps2eps -dEmbedAllFonts=true -dSubsetFonts=true -dEPSCrop=true -dPDFSETTINGS=/prepress $< $<.inter

########################################################################

texs =  Main.tex	 \
	Abstract.tex	 \
	Introduction.tex \
	Paradigm.tex     \
        Conceptual.tex   \
	FLUX.tex         \
	FLUX-Overview.tex \
	FLUX-CMB.tex \
	FLUX-LWJ.tex \
	FLUX-KVS.tex     \
	Results-Perf.tex \
	Results-KAP.tex \
	Results-Setup.tex \
	Results-Analysis.tex \
	Results-Case.tex \
	Related.tex      \
	Discussions.tex \
	Conclusion.tex        

#txts = fig/mpa-code-frag.txt fig/Challenges.pdf

bibs = ../bib/project.bib  ../bib/rfc.bib

epsfigs = ../fig/kvs_update_1.eps \
	../fig/kvs_update_2.eps \
	../fig/kvs_update_3.eps \
	../fig/tree-binary.eps \
	../fig/tree-degenerate.eps \
	../fig/tree-trinary.eps \
	../fig/tree-binomial.eps \
	../fig/tree-flat.eps


#epsfigs = fig/fgfs_mpi_perf.eps \
	  fig/fgfs_mpi_sync_perf.eps \
	  fig/fgfs_mpi_sync_perf_cons.eps \
	  fig/fgfs_serial_perf.eps \
	  fig/fgfs_mrnet_perf.eps

pdffigs = $(epsfigs:.eps=.pdf) 

styles = llncs.cls splncs.bst splncs03.bst splncs_srt.bst

########################################################################

all: Flux-JSSPP.pdf 

Flux-JSSPP.pdf: $(texs) $(bibs) $(pdffigs) $(txts) $(styles)
	$(pdflatex) Main
	bibtex Main
	$(pdflatex) Main
	$(pdflatex) Main
	mv Main.pdf $@

Flux-JSSPP.ps: %.ps: %.dvi $(epsfigs)
	$(dvips)

Flux-JSSPP.dvi: $(texs) $(bibs) $(epsfigs) $(txts) $(styles)
	$(latex) Main
	bibtex Main
	$(latex) Main
	$(latex) Main
	mv Main.dvi $@

$(pdffigs): %.pdf: %.eps
	$(epsfix)
	$(epstopdf)


clean:
	rm -f Main.aux Main.bib Main.dvi Main.log Main.blg Main.bbl Main.cb Main.cb2 Main.out
	rm -f $(pdffigs)
	rm -rf Flux-JSSPP.pdf Main.pdf

clobber: clean	
	rm -f Flux-JSSPP.pdf

view: Flux-JSSPP.pdf
	evince $<
