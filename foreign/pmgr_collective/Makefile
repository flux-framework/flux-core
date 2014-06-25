NAME=pmgr_collective-1.0_7
export OPT=-g -O0 -Wall -DHAVE_PMI

all: .built

.built: $(NAME).tgz
	tar xzf $(NAME).tgz
	make -C $(NAME) all
	touch $@

clean:
	rm -rf $(NAME)
	rm -f .built
