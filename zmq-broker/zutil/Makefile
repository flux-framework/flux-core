topdir :=     $(shell cd .. && pwd)
builddir :=   $(shell pwd)

CFLAGS += -I$(topdir) -I$(topdir)/include -I$(topdir)/util \
          -DWITH_LSD_NOMEM_ERROR_FUNC -DWITH_LSD_FATAL_ERROR_FUNC
LDFLAGS += -L$(topdir)/lib -Wl,-rpath $(topdir)/lib


LIBZUTIL_SOURCES := zio.c cbuf.c forkzio.c kz.c

TEST_programs := zio-test flux-zio-test

all: libzutil.a

libzutil.a: $(LIBZUTIL_SOURCES:.c=.o)
	ar rcs $@ $^

tests: libzutil.a $(TEST_programs)

%-test : %-test.o
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS) \
	 libzutil.a ../util/libutil.a -ljson -lczmq -lcmb
clean:
	rm -f *.a *.o $(TEST_programs)
