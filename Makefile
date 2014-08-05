SUBDIRS = zmq-broker pmi-test

all: $(SUBDIRS)

$(SUBDIRS):
	make -C $@ all

clean:
	for f in $(SUBDIRS); do make -C $$f $@; done

# subdir dependencies
pmi-test: zmq-broker

.PHONY: all clean $(SUBDIRS)
