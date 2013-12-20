TOP =             $(shell pwd)/..
LIBDIR =          $(TOP)/lib
EXTRA_CFLAGS +=   -I$(TOP)/include -I$(TOP)/util
EXTRA_LDFLAGS +=  -L$(TOP)/lib $(TOP)/util/libutil.a

LDFLAGS = -Wl,-rpath,$(LIBDIR)

export CFLAGS LDFLAGS

LDADD_CLI = $(EXTRA_LDFLAGS) -ljson -lzmq -lczmq -lcrypto -lutil -luuid

BUILD = tbase64 tpmikvs tkvs tkvswatch

all: $(BUILD)

# for screen3
check1:
	export FLUX_API_PATH=/tmp/rank.1; make check
check2:
	export FLUX_API_PATH=/tmp/rank.2; make check
check3: check check1 check2

check:
	@for tst in $${TESTS:-t[0-9][0-9]}; do \
		./$$tst; \
		if [ $$? -eq 0 ]; then \
		  echo PASS: $$tst; \
		else \
		  echo FAIL: $$tst; \
		fi \
	done

tbase64: tbase64.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDADD_CLI)

tpmikvs: tpmikvs.o
	$(CC) $(LDFLAGS) -o $@ $< -L$(LIBDIR) -lpmi

tkvs: tkvs.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDADD_CLI) -lcmb

tkvswatch: tkvswatch.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDADD_CLI) -lcmb -lpthread

clean:
	rm -f *.o a.out
	rm -f $(BUILD)
