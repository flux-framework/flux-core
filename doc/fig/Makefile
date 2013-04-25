DEPFIG = \
	comms.eps \
	commstk.eps \
	cmb.eps \
	comms_ex1a.eps \
	comms_ex1b.eps \
	comms_ex1c.eps \
	comms_ex1d.eps \
	comms_ex1e.eps \
	comms_ex1f.eps \
	comms_ex1g.eps \
	comms_zmq_reqrep.eps \
	comms_zmq_pubsub.eps \
	comms_zmq_pushpull.eps \
	comms_zmq_reduct.eps \
	comms_sctp.eps \
	wrap_graph.ps \
	WRAP_Architecture.ps \
	WRAP_Base.ps \
	WRAP_grow.ps \
	WRAP_newJobFunction.ps \
	mon_ex1a.eps \
	mon_ex1b.eps \
	mon_iface.eps \
	mon_iface_log.eps \
	mon_iface_snmp.eps \
	job-hierarchy-job0.ps \
	job-hierarchy-job0-x.ps \
	job-hierarchy-job0-x-y.ps \
	job-hierarchy-lwj.ps \
	resource-hierarchy.ps \
	redis-pmi.eps \
	zmq-broker.eps \
	zmq-broker-tree.eps \
	zmq-broker-pmi.eps \
	zmq-broker2.eps \
	zmq-broker-pmi2.eps \

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

%.ps: %.pdf
	pdf2ps $< $@

ketama: zmq-broker-keys.dat
	R -q -e "x <- read.csv('$<', header = F, comment.char = \"#\"); summary(x); sd(x[ , 1])"
modula: zmq-broker-modula.dat
	R -q -e "x <- read.csv('$<', header = F, comment.char = \"#\"); summary(x); sd(x[ , 1])"
