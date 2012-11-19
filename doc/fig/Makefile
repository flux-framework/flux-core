DEPFIG = \
	comms.eps \
	commstk.eps \
	cmb.eps \
	zmqtbon.eps

all: $(DEPFIG)

%.eps: %.dia
	dia --nosplash --export=$@ --filter=eps-builtin $<
