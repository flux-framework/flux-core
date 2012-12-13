DEPFIG = \
	comms.eps \
	commstk.eps \
	cmb.eps \
	zmqtbon.eps \
	dnbd.eps \
	pathwalk.eps \

all: $(DEPFIG)

%.eps: %.dia
	dia --nosplash --export=$@ --filter=eps-builtin $<

%.eps: %.gp
	gnuplot $<
