SUBDIRS = foreign pepe zmq-broker pmi-test

all: $(SUBDIRS)

$(SUBDIRS):
	make -C $@ all

clean:
	for f in $(SUBDIRS); do make -C $$f $@; done

# subdir dependencies
pepe: foreign
pmi-test: zmq-broker

.PHONY: all clean $(SUBDIRS)
