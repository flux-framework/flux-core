DEPFIG = \
	comms.eps \
	commstk.eps \
	cmb.eps \
	zmqtbon.eps \
	pathwalk.eps

all: $(DEPFIG)

%.eps: %.dia
	dia --nosplash --export=$@ --filter=eps-builtin $<

%.eps: %.gp
	gnuplot $<
