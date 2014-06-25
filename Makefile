SUBDIRS = foreign pepe zmq-broker

all: $(SUBDIRS)

$(SUBDIRS):
	make -C $@ all

clean:
	for f in $(SUBDIRS); do make -C $$f $@; done

pepe: foreign

.PHONY: all clean $(SUBDIRS)
