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

# for screen3
check1:
	export FLUX_API_PATH=/tmp/rank.1; make check
check2:
	export FLUX_API_PATH=/tmp/rank.2; make check
check3: check check1 check2

check:
	@export LUA_CPATH=";;$(TOP)/dlua/?.so"; \
	export LUA_PATH=";;$(TOP)/dlua/?.lua"; \
	for tst in $${TESTS:-t[0-9][0-9]}; do \
		./$$tst; \
		if [ $$? -eq 0 ]; then \
		  echo PASS: $$tst; \
		else \
		  echo FAIL: $$tst; \
		  exit 1; \
		fi \
	done

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
