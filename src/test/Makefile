TOP =             $(shell pwd)/..
LIBDIR =          $(TOP)/lib
EXTRA_CFLAGS +=   -I$(TOP)/include -I$(TOP)/util
EXTRA_LDFLAGS +=  -L$(TOP)/lib $(TOP)/util/libutil.a

TESTRDL_INPUT_FILE := $(TOP)/conf/hype.lua

LDFLAGS = -Wl,-rpath,$(LIBDIR)

export CFLAGS LDFLAGS TESTRDL_INPUT_FILE

LDADD_CLI = $(EXTRA_LDFLAGS) -ljson -lzmq -lczmq -lutil -lm -lpthread

BUILD = tenc tkvswatch tcommit tasyncsock tmunge trdl

all: $(BUILD)

check:
	$(TOP)/cmbd -c "make xcheck"

screencheck:
	@test -n "$$SIZE" || echo "Usage: make screencheck SIZE=N [SID=name]"
	@for rank in `seq 0 $$(($$SIZE - 1))`; do \
		$(TOP)/flux screen --sid=$${SID:-screen} \ --run $$rank \
			 make xcheck; \
	done	

xcheck:
	@export LUA_CPATH=";;$(TOP)/dlua/?.so"; \
	export LUA_PATH=";;$(TOP)/dlua/?.lua"; \
	rc=0; \
	if test -n "$$TMPDIR"; then echo TMPDIR=$$TMPDIR; fi; \
	for tst in $${TESTS:-t[0-9][0-9]}; do \
		./$$tst; \
		if [ $$? -eq 0 ]; then \
		  echo PASS: $$tst; \
		else \
		  echo FAIL: $$tst; \
		  rc=1; \
		fi \
	done; \
	exit $$rc

tenc: tenc.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDADD_CLI)

tkvswatch: tkvswatch.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDADD_CLI) -lcmb

tcommit: tcommit.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDADD_CLI) -lcmb

tasyncsock: tasyncsock.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDADD_CLI) -lcmb

tmunge: tmunge.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDADD_CLI) -lcmb

trdl: trdl.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDADD_CLI) -lrdl
clean:
	rm -f *.o a.out
	rm -f $(BUILD)
