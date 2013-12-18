topdir :=     $(shell cd .. && pwd)
builddir :=   $(shell pwd)

CFLAGS += -I$(topdir) -I$(topdir)/include -I$(topdir)/util
LDFLAGS += -L$(topdir)/lib -Wl,-rpath $(topdir)/lib

LIBZUTIL_SOURCES := zio.c cbuf.c forkzio.c kz.c

TEST_programs := zio-test flux-zio-test

all: libzutil.a

libzutil.a: $(LIBZUTIL_SOURCES:.c=.o)
	ar rcs $@ $^

tests: libzutil.a $(TEST_programs)

%-test : %-test.o
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS) \
	 libzutil.a ../util/libutil.a -ljson -lczmq -lssl -luuid -lcmb
clean:
	rm -f *.a *.o $(TEST_programs)
