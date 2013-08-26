
LIBUTIL_SOURCES := log.c hostlist.c cbuf.c util.c zmsg.c hljson.c lz4.c

libutil.a: $(LIBUTIL_SOURCES:.c=.o)
	ar rcs $@ $^

clean:
	rm -f *.a *.o
