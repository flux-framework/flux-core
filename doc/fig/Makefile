DEPFIG = \
	comms.eps \
	commstk.eps \
	cmb.eps \
	zmqtbon.eps \
	dnbd.eps \
	pathwalk.eps \
	wrap_graph.ps \
	WRAP_Architecture.ps \
	WRAP_Base.ps \
	WRAP_grow.ps \
	WRAP_newJobFunction.ps \
	comms_ex1a.eps \
	comms_ex1b.eps \
	comms_ex1c.eps \
	comms_ex1d.eps \
	mon_ex1a.eps \
	mon_ex1b.eps \
	mon_iface.eps

all: $(DEPFIG)

%.eps: %.dia
	dia --nosplash --export=$@ --filter=eps-builtin $<

%.eps: %.gp
	gnuplot $<

wrap_graph.ps: ../wrap_thrust/wrap.dot
	dot -Tps $< -o $@
WRAP_Architecture.ps: ../wrap_thrust/fig/WRAP_Architecture.pdf
	pdf2ps $< $@
WRAP_Base.ps: ../wrap_thrust/fig/WRAP_Base.pdf
	pdf2ps $< $@
WRAP_grow.ps: ../wrap_thrust/fig/WRAP_grow.pdf
	pdf2ps $< $@
WRAP_newJobFunction.ps: ../wrap_thrust/fig/WRAP_newJobFunction.pdf
	pdf2ps $< $@
