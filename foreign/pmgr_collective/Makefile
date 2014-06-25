NAME=pmgr_collective-1.0_7

all: .built

.built: $(NAME).tgz
	tar xzf $(NAME).tgz
	make -C $(NAME) all
	touch $@

clean:
	rm -rf $(NAME)
	rm -f .built
