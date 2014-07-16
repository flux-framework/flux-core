DEPFIG = \
	zmq_dealer_router.eps \
	zmq_pub_sub.eps \
	cmbd_sockets.eps \
	cmbd_tree.eps \
	8tri-ok.eps \
	8tri-fail.eps \
	kvs_update_1.eps \
	kvs_update_2.eps \
	kvs_update_3.eps \

all: $(DEPFIG)

%.eps: %.dia
	dia --nosplash --export=$@ --filter=eps-builtin $<

%.eps: %.gp
	gnuplot $<

tree-flat.eps: treedot.lua
	./treedot.lua [0-31] flat | dot -Teps -o $@
tree-binary.eps: treedot.lua
	./treedot.lua [0-31] binary | dot -Teps -o $@
tree-trinary.eps: treedot.lua
	./treedot.lua [0-31] trinary | dot -Teps -o $@
tree-binomial.eps: treedot.lua
	./treedot.lua [0-31] binomial | dot -Teps -o $@
tree-degenerate.eps: treedot.lua
	./treedot.lua [0-31] degenerate | dot -Teps -o $@

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

clean:
	rm -f $(DEPFIG)
