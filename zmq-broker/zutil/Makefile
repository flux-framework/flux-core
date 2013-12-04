CFLAGS += -I..
LIBZUTIL_SOURCES := zio.c cbuf.c

all: libzutil.a zio-test

libzutil.a: $(LIBZUTIL_SOURCES:.c=.o)
	ar rcs $@ $^

zio-test: zio-test.o
	$(CC) -o zio-test zio-test.o $(CFLAGS) libzutil.a ../util/libutil.a -ljson -lczmq -lssl -luuid

clean:
	rm -f *.a *.o
