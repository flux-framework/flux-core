SUBDIRS = pmgr_collective

all: $(SUBDIRS)

$(SUBDIRS):
	make -C $@ all

clean:
	for f in $(SUBDIRS); do make -C $$f $@; done

.PHONY: all clean $(SUBDIRS)
