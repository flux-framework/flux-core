SUBDIRS = foreign pepe zmq-broker

all clean:
	for f in $(SUBDIRS); do make -C $$f $@; done
